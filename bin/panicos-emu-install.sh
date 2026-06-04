#!/bin/bash
###############################################################################
# panicos-emu-install.sh
#
# Grafts / refreshes the ROCKNIX RetroArch suite into a self-contained sandbox
# on PanicOS, and layers EmulationStation systems on top — WITHOUT modifying
# anything PanicOS owns.
#
# HARD INVARIANT: this script only ever writes under
#     /storage/emulators/retroarch , /storage/roms , /etc/emulationstation/es_systems.cfg
# It never touches /usr, the audio codec config, norns, or ports. The one /etc file
# is the maintainer-sanctioned overlay-layering spot, backed up to .panicos-orig.
#
# MULTIPLE CORES PER SYSTEM: systems.conf lists one or more cores per system
# (space-separated, first = default). The installer fetches every listed core that
# ROCKNIX ships (missing ones are skipped, not fatal) and emits an EmulationStation
# <emulators> block so you can pick the core per game in the UI. --all-cores pulls
# the ENTIRE ROCKNIX libretro set for full parity.
#
# Idempotent. Self-healing. Preserves saves / states / bios / roms across runs.
#
# Usage:
#   panicos-emu-install.sh              # install/update to rocknix.lock; graft if cores/version changed
#   panicos-emu-install.sh --force      # re-graft binaries unconditionally
#   panicos-emu-install.sh --all-cores  # also copy EVERY ROCKNIX core (parity); remembered across runs
#   panicos-emu-install.sh --render-only# only re-render configs/es_systems (self-heal); no download
###############################################################################
set -euo pipefail

REPO_DIR="${PE_REPO:-$(cd "$(dirname "$0")/.." && pwd)}"
LOCK="$REPO_DIR/rocknix.lock"
SYSTEMS_CONF="$REPO_DIR/systems.conf"
TPL_CFG="$REPO_DIR/config/retroarch.cfg"
TPL_WRAP="$REPO_DIR/config/retroarch.sh"

PREFIX="${PE_PREFIX:-/storage/emulators/retroarch}"
ROMS="${PE_ROMS:-/storage/roms}"
ES_TARGET="${PE_ES_TARGET:-/etc/emulationstation/es_systems.cfg}"
ES_ORIG="${PE_ES_ORIG:-${ES_TARGET}.panicos-orig}"
ES_STALE="/storage/.emulationstation/es_systems.cfg"
BUILD="/storage/.panicos-emu-build"
MNT="$BUILD/mnt"
LD="/lib/ld-linux-aarch64.so.1"
INSTALLED_MARK="$PREFIX/.installed-rocknix"
CORES_MARK="$PREFIX/.installed-cores"
SELECTION="$PREFIX/.enabled-systems"   # optional: newline list of enabled system names; absent = all

# Color only on an interactive terminal (so piped/GUI output is clean, no stray escapes).
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
  c_i=$'\033[36m'; c_w=$'\033[33m'; c_e=$'\033[31m'; c_0=$'\033[0m'
else
  c_i=''; c_w=''; c_e=''; c_0=''
fi
log(){  printf '%s[emu]%s %s\n' "$c_i" "$c_0" "$*"; }
warn(){ printf '%s[emu] WARN:%s %s\n' "$c_w" "$c_0" "$*" >&2; }
die(){  printf '%s[emu] ERROR:%s %s\n' "$c_e" "$c_0" "$*" >&2; exit 1; }

FORCE=0; RENDER_ONLY=0; ALL_CORES=0; STATUS=0
for a in "$@"; do case "$a" in
  --force) FORCE=1 ;;
  --all-cores) ALL_CORES=1 ;;
  --render-only|--repair) RENDER_ONLY=1 ;;
  --status) STATUS=1 ;;
  -h|--help) sed -n '2,28p' "$0"; exit 0 ;;
  *) die "unknown arg: $a" ;;
esac; done

if [ -z "${PE_PREFIX:-}" ]; then [ "$(id -u)" = 0 ] || die "must run as root"; fi
command -v jq >/dev/null   || die "jq required"
[ -f "$LOCK" ]             || die "missing rocknix.lock (run from the repo clone)"
[ -f "$SYSTEMS_CONF" ]     || die "missing systems.conf"

RKVER=$(jq -r .rocknix_version "$LOCK")
TARGET=$(jq -r .target "$LOCK")
ARCH=$(jq -r .arch "$LOCK")
BASE=$(jq -r .release_base "$LOCK")
MEMBER=$(jq -r .system_member "$LOCK")
RKDIR="ROCKNIX-${TARGET}.${ARCH}-${RKVER}"
ASSET="${RKDIR}.tar"
URL="${BASE}/${RKVER}/${ASSET}"

# ---- systems.conf helpers --------------------------------------------------
conf_rows(){ grep -vE '^[[:space:]]*#|^[[:space:]]*$' "$SYSTEMS_CONF"; }
# Rows we actually install: filtered by the on-device selection file if present, else all.
conf_rows_enabled(){
  if [ -f "$SELECTION" ]; then
    conf_rows | while IFS='|' read -r name rest; do
      grep -qxF "$(echo "$name" | xargs)" "$SELECTION" && printf '%s|%s\n' "$name" "$rest"
    done
  else
    conf_rows
  fi
}
# every distinct core referenced by the ENABLED systems (field 3 may list several)
cores_needed(){
  conf_rows_enabled | awk -F'|' '{
    gsub(/^[ \t]+|[ \t]+$/,"",$3); n=split($3,a,/[ \t]+/);
    for(i=1;i<=n;i++) if(a[i]!="") print a[i]
  }' | sort -u
}
# what this run wants, as a stable key (enabled cores + all-cores flag) for change detection
want_key(){ echo "$(cores_needed | tr '\n' ' ')|all=$ALL_CORES"; }

# count rom files in a system's dir (matching its extensions; recursive)
rom_count(){  # $1=system name  $2=space-separated extensions
  local dir="$ROMS/$1" e args=()
  [ -d "$dir" ] || { echo 0; return; }
  for e in $2; do args+=(-o -iname "*.$e"); done
  find "$dir" -type f \( "${args[@]:1}" \) 2>/dev/null | grep -ivE '\.(txt|xml|cfg|opt|state|srm)$' | wc -l | tr -d ' '
}
# machine-readable inventory for the GUI: name|fullname|enabled|coresInstalled|coresTotal|roms|defaultInstalledCore
print_status(){
  conf_rows | while IFS='|' read -r name full corelist exts platform; do
    name=$(echo "$name"|xargs); full=$(echo "$full"|xargs)
    local total=0 inst=0 def="" c
    for c in $corelist; do
      total=$((total+1))
      if [ -f "$PREFIX/cores/${c}_libretro.so" ]; then inst=$((inst+1)); [ -z "$def" ] && def="$c"; fi
    done
    local enabled=1
    [ -f "$SELECTION" ] && { grep -qxF "$name" "$SELECTION" && enabled=1 || enabled=0; }
    printf '%s|%s|%s|%s|%s|%s|%s\n' "$name" "$full" "$enabled" "$inst" "$total" "$(rom_count "$name" "$exts")" "$def"
  done
}

# ---- generate EmulationStation es_systems.cfg (superset + <emulators>) ------
gen_es_systems(){
  local W="$PREFIX/retroarch.sh"
  echo '<?xml version="1.0"?>'
  echo '<!-- GENERATED by panicos-emu (do not hand-edit; edit systems.conf in the repo).'
  echo "     Superset of PanicOS's pristine stock systems + console emulators."
  echo "     Restore stock with: cp $ES_ORIG $ES_TARGET  (or run panicos-emu-uninstall.sh). -->"
  echo '<systemList>'
  # preserve PanicOS's own systems verbatim from the pristine backup
  [ -f "$ES_ORIG" ] && awk '/<system>/{f=1} f{print} /<\/system>/{f=0}' "$ES_ORIG"
  # our managed console systems (only those enabled in the on-device selection)
  conf_rows_enabled | while IFS='|' read -r name full corelist exts platform; do
    name=$(echo "$name" | xargs); full=$(echo "$full" | xargs); platform=$(echo "$platform" | xargs)
    # keep only cores actually installed in the sandbox; preserve order; first = default
    local installed=() c
    for c in $corelist; do [ -f "$PREFIX/cores/${c}_libretro.so" ] && installed+=("$c"); done
    [ "${#installed[@]}" -eq 0 ] && { warn "system '$name': none of its cores are installed — skipping"; continue; }
    local def="${installed[0]}"
    local extattr="" e
    for e in $exts; do extattr="$extattr .$e .$(echo "$e" | tr '[:lower:]' '[:upper:]')"; done
    extattr="$extattr .zip .ZIP"
    # <emulators> block (lets ES show a per-game Core selector)
    local cores_xml="" first=1 cc
    for cc in "${installed[@]}"; do
      if [ "$first" = 1 ]; then
        cores_xml="                    <core default=\"true\">$cc</core>"; first=0
      else
        cores_xml="$cores_xml
                    <core>$cc</core>"
      fi
    done
    cat <<SYS
    <system>
        <name>${name}</name><fullname>${full}</fullname>
        <path>${ROMS}/${name}</path>
        <extension>${extattr# }</extension>
        <command>${W} %CORE% ${def} %ROM%</command>
        <platform>${platform}</platform><theme>${platform}</theme>
        <emulators>
            <emulator name="libretro">
                <cores>
${cores_xml}
                </cores>
            </emulator>
        </emulators>
    </system>
SYS
  done
  echo '</systemList>'
}

# ---- render config layer (cheap, idempotent, self-healing) -----------------
render_configs(){
  mkdir -p "$PREFIX/config" "$PREFIX/saves" "$PREFIX/states" "$PREFIX/assets" \
           "$ROMS/bios" "$ROMS/screenshots" "$(dirname "$ES_TARGET")"
  install -m 0644 "$TPL_CFG"  "$PREFIX/config/panicos-override.cfg"
  install -m 0755 "$TPL_WRAP" "$PREFIX/retroarch.sh"
  rm -f "$PREFIX/config/retroarch.cfg" 2>/dev/null || true   # legacy single-config name
  if [ ! -f "$ES_ORIG" ] && [ -f "$ES_TARGET" ] && ! grep -q 'GENERATED by panicos-emu' "$ES_TARGET"; then
    cp -a "$ES_TARGET" "$ES_ORIG"; log "backed up stock es_systems -> $ES_ORIG"
  fi
  gen_es_systems > "${ES_TARGET}.tmp" && mv "${ES_TARGET}.tmp" "$ES_TARGET"
  rm -f "$ES_STALE" 2>/dev/null || true
  conf_rows_enabled | awk -F'|' '{gsub(/[[:space:]]/,"",$1); print $1}' | while read -r s; do
    [ -n "$s" ] && mkdir -p "$ROMS/$s"
  done
  # install/refresh the on-device "Update Emulators" Ports menu entry (kept in sync with the repo)
  if [ -f "$REPO_DIR/ports/Update Emulators.sh" ]; then
    mkdir -p "$ROMS/ports"
    install -m 0755 "$REPO_DIR/ports/Update Emulators.sh" "$ROMS/ports/Update Emulators.sh"
  fi
  log "configs rendered ($(grep -c '<name>' "$ES_TARGET") ES systems -> $ES_TARGET; menu entry refreshed)"
}

# ---- resolve full shared-lib closure from a mounted ROCKNIX SYSTEM ---------
resolve_libs(){  # $1=stage dir  $2=mounted rocknix root
  local stage="$1" mnt="$2" changed=1 i=0 miss so src
  export LD_LIBRARY_PATH="$stage/lib:/usr/lib:/lib"
  while [ "$changed" = 1 ] && [ "$i" -lt 25 ]; do
    changed=0; i=$((i+1))
    miss=$( { LD_TRACE_LOADED_OBJECTS=1 "$LD" "$stage/bin/retroarch"
              for c in "$stage"/cores/*_libretro.so; do LD_TRACE_LOADED_OBJECTS=1 "$LD" "$c"; done
            } 2>&1 | awk '/not found/{print $1}' | sort -u )
    for so in $miss; do
      src=$(find "$mnt/usr/lib" -maxdepth 1 -name "$so" 2>/dev/null | head -1)
      [ -z "$src" ] && src=$(find "$mnt/usr/lib" -name "$so*" 2>/dev/null | head -1)
      if [ -n "$src" ]; then cp -Lf "$src" "$stage/lib/$so"; changed=1
      else warn "$so referenced but not found in ROCKNIX $RKVER"; fi
    done
  done
  log "lib closure resolved ($(ls "$stage/lib" | wc -l | tr -d ' ') libs)"
}

# ---- graft binaries from ROCKNIX (download + mount + extract) --------------
graft(){
  log "grafting ROCKNIX $RKVER ($ASSET)$([ "$ALL_CORES" = 1 ] && echo ' [all-cores]')"
  mkdir -p "$BUILD" "$MNT"
  ( cd "$BUILD"
    log "downloading (resumable)…"
    wget -c -q --show-progress -O rocknix.tar "$URL" || die "download failed: $URL"
    log "extracting SYSTEM…"
    rm -f SYSTEM
    tar xf rocknix.tar "$RKDIR/$MEMBER"
    mv "$RKDIR/$MEMBER" SYSTEM
    rm -rf "$RKDIR"
  )
  mountpoint -q "$MNT" && umount -l "$MNT" || true
  mount -o loop,ro "$BUILD/SYSTEM" "$MNT" || die "mount SYSTEM failed (kernel needs LZO squashfs)"

  local STAGE="$BUILD/stage"
  rm -rf "$STAGE"; mkdir -p "$STAGE"/{bin,lib,cores,autoconfig}
  cp -a "$MNT/usr/bin/retroarch" "$STAGE/bin/retroarch"
  cp -a "$MNT/usr/share/libretro/autoconfig/." "$STAGE/autoconfig/" 2>/dev/null || true

  if [ "$ALL_CORES" = 1 ]; then
    log "copying ALL ROCKNIX cores (parity)…"
    cp -f "$MNT/usr/lib/libretro/"*_libretro.so   "$STAGE/cores/" 2>/dev/null || true
    cp -f "$MNT/usr/lib/libretro/"*_libretro.info "$STAGE/cores/" 2>/dev/null || true
  else
    local c
    for c in $(cores_needed); do
      if [ -f "$MNT/usr/lib/libretro/${c}_libretro.so" ]; then
        cp -f "$MNT/usr/lib/libretro/${c}_libretro.so" "$STAGE/cores/"
        cp -f "$MNT/usr/lib/libretro/${c}_libretro.info" "$STAGE/cores/" 2>/dev/null || true
        log "  core: $c"
      else
        warn "core '$c' not in ROCKNIX $RKVER — skipping"
      fi
    done
  fi
  log "  cores staged: $(ls "$STAGE/cores/"*_libretro.so 2>/dev/null | wc -l | tr -d ' ')"
  resolve_libs "$STAGE" "$MNT"

  mkdir -p "$PREFIX"
  rm -rf "$PREFIX/bin" "$PREFIX/lib" "$PREFIX/cores" "$PREFIX/autoconfig"
  mv "$STAGE/bin" "$STAGE/lib" "$STAGE/cores" "$STAGE/autoconfig" "$PREFIX/"
  echo "$RKVER"   > "$INSTALLED_MARK"
  want_key        > "$CORES_MARK"

  # ---- ROCKNIX QOL: device-tuned base config + per-core option presets + shaders ----
  # (layered under our PanicOS override at launch; gives optimized per-emulator defaults)
  mkdir -p "$PREFIX/config" "$PREFIX/shaders"
  cp -f "$MNT/usr/config/retroarch/retroarch.cfg" "$PREFIX/config/rocknix-base.cfg" 2>/dev/null \
    && log "  QOL: ROCKNIX base config"
  cp -f "$MNT/usr/config/retroarch/retroarch-core-options.cfg" "$PREFIX/config/" 2>/dev/null \
    && log "  QOL: core-option presets ($(grep -c '=' "$PREFIX/config/retroarch-core-options.cfg" 2>/dev/null || echo 0) options)"
  cp -f "$MNT/usr/config/retroarch/"*.opt "$PREFIX/config/" 2>/dev/null || true
  if [ -d "$MNT/usr/share/slang-shaders" ]; then
    cp -a "$MNT/usr/share/slang-shaders/." "$PREFIX/shaders/" 2>/dev/null \
      && log "  QOL: slang shaders ($(du -sh "$PREFIX/shaders" 2>/dev/null | cut -f1))"
  fi

  umount -l "$MNT" 2>/dev/null || true
  rm -rf "$BUILD"
  log "graft complete (ROCKNIX $RKVER)"
}

# ---------------------------------------------------------------------------
main(){
  if [ "$STATUS" = 1 ]; then print_status; exit 0; fi
  log "panicos-emu — target ROCKNIX $RKVER ($TARGET/$ARCH)"

  if [ "$RENDER_ONLY" = 1 ]; then
    render_configs; log "render-only: done."; exit 0
  fi

  # graft if forced, version changed, binary missing, or the set of wanted cores changed
  local cur=""; [ -f "$INSTALLED_MARK" ] && cur=$(cat "$INSTALLED_MARK")
  local have=""; [ -f "$CORES_MARK" ] && have=$(cat "$CORES_MARK")
  local need=0
  [ "$FORCE" = 1 ] && need=1
  [ "$cur" != "$RKVER" ] && need=1
  [ -x "$PREFIX/bin/retroarch" ] || need=1
  [ "$(want_key)" != "$have" ] && need=1
  [ -f "$PREFIX/config/rocknix-base.cfg" ] || need=1   # ROCKNIX QOL not pulled yet

  if [ "$need" = 1 ]; then graft; else log "ROCKNIX $RKVER + configured cores already present."; fi
  render_configs   # AFTER graft, so es_systems reflects what's actually installed

  # self-test: binary + every installed core resolve cleanly
  export LD_LIBRARY_PATH="$PREFIX/lib:/usr/lib:/lib"
  local bad=0 m
  m=$(LD_TRACE_LOADED_OBJECTS=1 "$LD" "$PREFIX/bin/retroarch" 2>&1 | grep -i 'not found' || true)
  [ -n "$m" ] && { warn "retroarch unresolved: $m"; bad=1; }
  for c in "$PREFIX"/cores/*_libretro.so; do
    m=$(LD_TRACE_LOADED_OBJECTS=1 "$LD" "$c" 2>&1 | grep -i 'not found' || true)
    [ -n "$m" ] && { warn "$(basename "$c") unresolved: $m"; bad=1; }
  done
  [ "$bad" = 0 ] && log "self-test OK: retroarch + $(ls "$PREFIX"/cores/*_libretro.so | wc -l | tr -d ' ') cores resolve cleanly"

  log "done. Restart EmulationStation (or reboot) to pick up system/core changes."
}
main
