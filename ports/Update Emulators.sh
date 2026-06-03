#!/bin/bash
# panicos-emu — "Update Emulators": fullscreen SDL2 GUI (gamepad-driven emulator manager).
# Self-bootstrapping: clones the public repo on first run, then launches the manager.
REPO="https://github.com/seajaysec/panicos-emu.git"
CLONE="/storage/.panicos-emu"
BIN="$CLONE/bin/emu-manager"
export HOME=/storage
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-wayland}"
if [ ! -x "$BIN" ]; then
  command -v git >/dev/null 2>&1 || { echo "git not found"; sleep 5; exit 1; }
  timeout 8 ping -c1 github.com >/dev/null 2>&1 || { echo "No network. Connect Wi-Fi, then relaunch."; sleep 6; exit 1; }
  echo "First run: downloading panicos-emu…"
  git clone --depth 1 "$REPO" "$CLONE" 2>&1 | tail -3
fi
[ -x "$BIN" ] && exec "$BIN"
echo "panicos-emu could not be set up. See github.com/seajaysec/panicos-emu"; sleep 6
