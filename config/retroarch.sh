#!/bin/bash
# panicos-emu — sandboxed RetroArch launcher (rendered to /storage/emulators/retroarch/retroarch.sh).
# Usage: retroarch.sh <core_basename> <rom path...>
# Sandbox libs take precedence so RetroArch uses ROCKNIX's consistent ffmpeg/etc set,
# while the rest of the system is untouched.
HERE="/storage/emulators/retroarch"
LOG="$HERE/last-launch.log"
export LD_LIBRARY_PATH="$HERE/lib:/usr/lib:/lib:${LD_LIBRARY_PATH}"
export PAN_MESA_DEBUG=forcepack   # H700/Panfrost quirk (ROCKNIX sets this)
export HOME=/storage
CORE="$1"; shift
# Rejoin remaining args into one ROM path. EmulationStation's %ROM% substitution can reach us
# as multiple words when a filename has spaces; rejoining tolerates that regardless of escaping.
ROM="$*"
{
  echo "=== launch $(date) ==="
  echo "core=$CORE  argc=$#  rom=[$ROM]"
  echo "env: WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
  echo "     SDL_VIDEODRIVER=$SDL_VIDEODRIVER SDL_AUDIODRIVER=$SDL_AUDIODRIVER"
} > "$LOG" 2>&1
exec "$HERE/bin/retroarch" -L "$HERE/cores/${CORE}_libretro.so" \
     --config "$HERE/config/retroarch.cfg" "$ROM" >> "$LOG" 2>&1
