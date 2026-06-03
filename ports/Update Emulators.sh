#!/bin/bash
###############################################################################
# Update Emulators.sh — lands in Ports as "Update Emulators".
# Opens a fullscreen terminal (foot) with a gamepad-navigable menu to update /
# install / uninstall the emulator suite. Drives the repo clone at /storage/.panicos-emu.
###############################################################################
export LANG=C.UTF-8 LC_ALL=C.UTF-8

# PortMaster control.txt gives us $GPTOKEYB, get_controls, $CFW_NAME (gamepad mapping helpers)
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
for d in /opt/system/Tools/PortMaster /opt/tools/PortMaster "$XDG_DATA_HOME/PortMaster" /roms/ports/PortMaster; do
  [ -d "$d" ] && controlfolder="$d" && break
done
[ -n "$controlfolder" ] && source "$controlfolder/control.txt" 2>/dev/null
[ -n "${CFW_NAME:-}" ] && [ -f "$controlfolder/mod_${CFW_NAME}.txt" ] && source "$controlfolder/mod_${CFW_NAME}.txt" 2>/dev/null
type get_controls >/dev/null 2>&1 && get_controls

CLONE="/storage/.panicos-emu"
GPTK="$CLONE/ports/update-emulators.gptk"
TUI="$CLONE/ports/update-emulators-tui.sh"
FONT="monospace:size=18"

if [ ! -f "$TUI" ]; then
  foot --fullscreen -f "$FONT" -e bash -c \
    'echo "panicos-emu is not installed (expected /storage/.panicos-emu)."; echo; echo "Re-run the bootstrap over SSH. See the repo README."; sleep 8' 2>/dev/null
  exit 1
fi

# gamepad -> keyboard for the menu; gptokeyb watches "foot" and exits when it closes.
[ -n "${GPTOKEYB:-}" ] && $GPTOKEYB "foot" -c "$GPTK" &

foot --fullscreen -f "$FONT" -e bash "$TUI"

pkill -f "gptokeyb.*foot" 2>/dev/null || true
