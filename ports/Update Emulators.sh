#!/bin/bash
# panicos-emu — "Update Emulators": fullscreen SDL2 GUI (gamepad-driven emulator manager).
# Renders to the sway/Wayland session ES is already running in (env inherited).
BIN="/storage/.panicos-emu/bin/emu-manager"
export HOME=/storage
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-wayland}"
if [ -x "$BIN" ]; then exec "$BIN"; fi
echo "panicos-emu is not installed (expected $BIN)."
echo "Re-run the bootstrap over SSH — see the repo README."
sleep 6
