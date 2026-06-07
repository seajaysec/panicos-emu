#!/bin/bash
# Engine verb-logic tests in a temp sandbox (no device/root/network).
set -u
HERE="$(cd "$(dirname "$0")/.." && pwd)"
FAIL=0
t(){ if eval "$2"; then echo "  ok: $1"; else echo "  FAIL: $1"; FAIL=1; fi; }

setup(){
  SBOX="$(mktemp -d)"
  export PE_PREFIX="$SBOX/emu" PE_ROMS="$SBOX/roms" PE_REPO="$HERE"
  export PE_ES_TARGET="$SBOX/es_systems.cfg" PE_ES_ORIG="$SBOX/es_systems.orig"
  mkdir -p "$PE_PREFIX/cores" "$PE_ROMS"
  printf '<systemList>\n  <system><name>tools</name><path>/x</path><extension>.sh</extension><command>%%ROM%%</command><platform>tools</platform></system>\n</systemList>\n' > "$PE_ES_ORIG"
}
teardown(){ rm -rf "$SBOX"; }

setup
t "status runs without error" "bash '$HERE/bin/panicos-emu-install.sh' --status >/dev/null 2>&1"
teardown

setup
bash "$HERE/bin/panicos-emu-install.sh" --quick-setup --no-graft >/dev/null 2>&1
t "quick-setup writes enabled-systems"   "[ -s '$PE_PREFIX/.enabled-systems' ]"
t "quick-setup includes snes"            "grep -qx snes '$PE_PREFIX/.enabled-systems'"
t "quick-setup excludes nds"             "! grep -qx nds '$PE_PREFIX/.enabled-systems'"
teardown

setup
bash "$HERE/bin/panicos-emu-install.sh" --install snes --no-graft >/dev/null 2>&1
t "install adds system"      "grep -qx snes '$PE_PREFIX/.enabled-systems'"
bash "$HERE/bin/panicos-emu-install.sh" --install snes --no-graft >/dev/null 2>&1
t "install is idempotent"    "[ \"\$(grep -cx snes '$PE_PREFIX/.enabled-systems')\" = 1 ]"
teardown
setup
printf 'snes\nnes\n' > "$PE_PREFIX/.enabled-systems"
mkdir -p "$PE_ROMS/snes"; echo dummy > "$PE_ROMS/snes/game.sfc"
bash "$HERE/bin/panicos-emu-install.sh" --remove snes --no-graft >/dev/null 2>&1
t "remove drops system"  "! grep -qx snes '$PE_PREFIX/.enabled-systems'"
t "remove keeps other"   "grep -qx nes '$PE_PREFIX/.enabled-systems'"
t "remove keeps ROMs"    "[ -f '$PE_ROMS/snes/game.sfc' ]"
teardown

setup
printf 'snes\n' > "$PE_PREFIX/.enabled-systems"
: > "$PE_PREFIX/cores/snes9x_libretro.so"
: > "$PE_PREFIX/cores/snes9x2010_libretro.so"
bash "$HERE/bin/panicos-emu-install.sh" --set-core snes snes9x2010 --no-graft >/dev/null 2>&1
t "set-core records override" "grep -q 'snes|snes9x2010' '$PE_PREFIX/.core-overrides'"
t "es_systems uses override"  "grep -q 'retroarch.sh %CORE% snes9x2010' '$PE_ES_TARGET'"
teardown

setup
printf 'snes\n' > "$PE_PREFIX/.enabled-systems"
bash "$HERE/bin/panicos-emu-install.sh" --install snes snes9x --no-graft >/dev/null 2>&1
t "install with core list still enables system" "grep -qx snes '$PE_PREFIX/.enabled-systems'"
teardown

setup
printf 'snes\n' > "$PE_PREFIX/.enabled-systems"
: > "$PE_PREFIX/cores/snes9x_libretro.so"
: > "$PE_PREFIX/cores/snes9x2010_libretro.so"
bash "$HERE/bin/panicos-emu-install.sh" --remove-core snes snes9x2010 --no-graft >/dev/null 2>&1
t "remove-core deletes that core only" "[ ! -f '$PE_PREFIX/cores/snes9x2010_libretro.so' ] && [ -f '$PE_PREFIX/cores/snes9x_libretro.so' ]"
t "remove-core keeps system enabled (cores remain)" "grep -qx snes '$PE_PREFIX/.enabled-systems'"
teardown

setup
printf 'snes\n' > "$PE_PREFIX/.enabled-systems"
: > "$PE_PREFIX/cores/snes9x_libretro.so"
bash "$HERE/bin/panicos-emu-install.sh" --remove-core snes snes9x --no-graft >/dev/null 2>&1
t "remove-core of last core disables system" "! grep -qx snes '$PE_PREFIX/.enabled-systems'"
teardown

setup
printf 'snes\n' > "$PE_PREFIX/.enabled-systems"
: > "$PE_PREFIX/cores/snes9x_libretro.so"
: > "$PE_PREFIX/cores/snes9x2010_libretro.so"
printf 'snes|snes9x2010\n' > "$PE_PREFIX/.core-overrides"
bash "$HERE/bin/panicos-emu-install.sh" --remove-core snes snes9x2010 --no-graft >/dev/null 2>&1
t "remove-core drops override when it was the default" "! grep -q 'snes|snes9x2010' '$PE_PREFIX/.core-overrides'"
teardown

setup
printf 'snes\nnes\n' > "$PE_PREFIX/.enabled-systems"
: > "$PE_PREFIX/cores/snes9x_libretro.so"
mkdir -p "$PE_ROMS/snes"; echo dummy > "$PE_ROMS/snes/game.sfc"
bash "$HERE/bin/panicos-emu-install.sh" --remove snes --no-graft >/dev/null 2>&1
t "remove deletes the system's cores" "[ ! -f '$PE_PREFIX/cores/snes9x_libretro.so' ]"
t "remove still keeps ROMs"            "[ -f '$PE_ROMS/snes/game.sfc' ]"
teardown

setup
printf 'snes\n' > "$PE_PREFIX/.enabled-systems"
: > "$PE_PREFIX/cores/snes9x_libretro.so"
bash "$HERE/bin/panicos-emu-install.sh" --remove snes --no-graft >/dev/null 2>&1
t "remove of LAST system exits 0 (no set-e abort)" "[ \$? = 0 ]"
bash "$HERE/bin/panicos-emu-install.sh" --render-only >/dev/null 2>&1
t "render-only with empty selection exits 0" "[ \$? = 0 ]"
teardown

setup
: > "$PE_PREFIX/.enabled-systems"
bash "$HERE/bin/panicos-emu-install.sh" --render-only >/dev/null 2>&1
t "render-only on a bare device exits 0" "[ \$? = 0 ]"
teardown

setup
echo "20260101" > "$PE_PREFIX/.installed-rocknix"
out="$(bash "$HERE/bin/panicos-emu-install.sh" --check-update 2>/dev/null)"
t "check-update reports available" "echo \"\$out\" | grep -q '^update:'"
echo "$(jq -r .rocknix_version "$HERE/rocknix.lock")" > "$PE_PREFIX/.installed-rocknix"
out2="$(bash "$HERE/bin/panicos-emu-install.sh" --check-update 2>/dev/null)"
t "check-update reports current"   "echo \"\$out2\" | grep -q '^uptodate:'"
teardown

# ---- systems.conf integrity (Full ROCKNIX coverage) ----
CORES_FIXTURE="$HERE/tests/fixtures/rocknix-cores.txt"
conf_rows_all(){ grep -vE '^[[:space:]]*#|^[[:space:]]*$' "$HERE/systems.conf"; }

t "fixture exists" "[ -s '$CORES_FIXTURE' ]"
t "every systems.conf row has exactly 5 pipe-fields" \
  "[ -z \"\$(conf_rows_all | awk -F'|' 'NF!=5{print NR}')\" ]"
t "every row has non-empty name and extensions" \
  "[ -z \"\$(conf_rows_all | awk -F'|' '{n=\$1;e=\$4;gsub(/[[:space:]]/,\"\",n);gsub(/^[ \t]+|[ \t]+\$/,\"\",e);if(n==\"\"||e==\"\")print NR}')\" ]"
t "system names are unique" \
  "[ \"\$(conf_rows_all | awk -F'|' '{n=\$1;gsub(/[[:space:]]/,\"\",n);print n}' | sort | uniq -d | wc -l | tr -d ' ')\" = 0 ]"

# every core referenced by systems.conf must be a real ROCKNIX core (catches typos / case)
UNKNOWN_CORES="$(conf_rows_all | awk -F'|' '{print $3}' | tr ' ' '\n' | sed '/^$/d' \
                 | LC_ALL=C sort -u | comm -23 - <(LC_ALL=C sort -u "$CORES_FIXTURE"))"
t "every core in systems.conf is a real ROCKNIX core" "[ -z \"\$UNKNOWN_CORES\" ]"
[ -z "$UNKNOWN_CORES" ] || printf "  (unknown cores: %s)\n" "$UNKNOWN_CORES"

# Full coverage must include representatives from each new family
for s in arcade neogeo psx psp dreamcast saturn 3do c64 amiga msx zxspectrum zx81 \
         dos scummvm doom quake wolf3d pico8 tic80 wasm4 atari2600 lynx vectrex; do
  t "systems.conf includes $s" \
    "conf_rows_all | awk -F'|' '{n=\$1;gsub(/[[:space:]]/,\"\",n);print n}' | grep -qxF '$s'"
done

setup
bash "$HERE/bin/panicos-emu-install.sh" --full-setup --no-graft >/dev/null 2>&1
t "full-setup writes enabled-systems"        "[ -s '$PE_PREFIX/.enabled-systems' ]"
t "full-setup enables EVERY systems.conf row" \
  "[ \"\$(grep -vE '^[[:space:]]*#|^[[:space:]]*\$' '$HERE/systems.conf' | wc -l | tr -d ' ')\" = \"\$(sort -u '$PE_PREFIX/.enabled-systems' | grep -c .)\" ]"
t "full-setup includes nds (not excluded like quick-setup)" "grep -qx nds '$PE_PREFIX/.enabled-systems'"
t "full-setup includes arcade" "grep -qx arcade '$PE_PREFIX/.enabled-systems'"
teardown

exit $FAIL
