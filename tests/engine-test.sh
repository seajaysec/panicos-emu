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
exit $FAIL
