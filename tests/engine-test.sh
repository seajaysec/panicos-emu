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
echo "20260101" > "$PE_PREFIX/.installed-rocknix"
out="$(bash "$HERE/bin/panicos-emu-install.sh" --check-update 2>/dev/null)"
t "check-update reports available" "echo \"\$out\" | grep -q '^update:'"
echo "$(jq -r .rocknix_version "$HERE/rocknix.lock")" > "$PE_PREFIX/.installed-rocknix"
out2="$(bash "$HERE/bin/panicos-emu-install.sh" --check-update 2>/dev/null)"
t "check-update reports current"   "echo \"\$out2\" | grep -q '^uptodate:'"
teardown

exit $FAIL
