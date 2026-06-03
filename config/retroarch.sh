#!/bin/bash
# panicos-emu — sandboxed RetroArch launcher (rendered to /storage/emulators/retroarch/retroarch.sh).
# Invoked by EmulationStation as:  retroarch.sh <selected-core> <default-core> <rom path...>
#   <selected-core> = ES %CORE% substitution (the per-game/per-system core choice, or the default).
#                     If ES doesn't substitute it, or the core isn't installed, we fall back.
#   <default-core>  = the system's default core (always a valid installed core).
# Sandbox libs take precedence so RetroArch uses ROCKNIX's consistent ffmpeg/etc set.
HERE="/storage/emulators/retroarch"
LOG="$HERE/last-launch.log"
export LD_LIBRARY_PATH="$HERE/lib:/usr/lib:/lib:${LD_LIBRARY_PATH}"
export PAN_MESA_DEBUG=forcepack   # H700/Panfrost quirk (ROCKNIX sets this)
export HOME=/storage
SEL="${1:-}"; DEF="${2:-}"; shift 2 2>/dev/null || shift $#
# Rejoin remaining args into one ROM path (ES may pass a spaced filename as multiple words).
ROM="$*"
# Use the selected core if it's a real installed core; otherwise the system default.
if [ -n "$SEL" ] && [ -f "$HERE/cores/${SEL}_libretro.so" ]; then CORE="$SEL"; else CORE="$DEF"; fi
{
  echo "=== launch $(date) ==="
  echo "selected=$SEL default=$DEF -> core=$CORE  rom=[$ROM]"
  echo "env: WAYLAND_DISPLAY=$WAYLAND_DISPLAY SDL_VIDEODRIVER=$SDL_VIDEODRIVER SDL_AUDIODRIVER=$SDL_AUDIODRIVER"
} > "$LOG" 2>&1
exec "$HERE/bin/retroarch" -L "$HERE/cores/${CORE}_libretro.so" \
     --config "$HERE/config/retroarch.cfg" "$ROM" >> "$LOG" 2>&1
