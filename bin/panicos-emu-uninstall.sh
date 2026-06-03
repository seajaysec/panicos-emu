#!/bin/bash
# panicos-emu-uninstall.sh — full revert to stock PanicOS launcher.
# Removes ONLY what panicos-emu added. PanicOS / norns / ports / USB / audio untouched.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "must run as root" >&2; exit 1; }

KEEP_ROMS=0
for a in "$@"; do [ "$a" = "--keep-roms" ] && KEEP_ROMS=1; done

echo "[emu] removing sandbox + ES override…"
rm -rf /storage/emulators/retroarch
rm -f  /storage/.emulationstation/es_systems.cfg   # reverts ES to /etc default (tools+ports)
rm -f  "/storage/roms/ports/Update Emulators.sh" "/storage/roms/ports/Update Emulators.log"

if [ "$KEEP_ROMS" = 0 ]; then
  echo "[emu] removing console rom dirs (pass --keep-roms to keep them)…"
  for s in gb nes snes gba genesis mastersystem gamegear colecovision ngp pcengine wonderswan n64 bios; do
    rm -rf "/storage/roms/$s"
  done
else
  echo "[emu] keeping /storage/roms/* (--keep-roms)"
fi

rm -rf /storage/.panicos-emu-build
echo "[emu] done. Restart EmulationStation to return to the stock launcher."
echo "[emu] (the repo clone at /storage/.panicos-emu is left in place; rm it too if you want.)"
