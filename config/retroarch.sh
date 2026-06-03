#!/bin/bash
# panicos-emu — sandboxed RetroArch launcher (rendered to /storage/emulators/retroarch/retroarch.sh).
# Invoked by EmulationStation as:  retroarch.sh <selected-core> <default-core> <rom path...>
#   <selected-core> = ES %CORE% (per-game/per-system choice or the default). Falls back if unset/missing.
#   <default-core>  = the system's default core (always a valid installed core).
# Config is layered: ROCKNIX's tuned base (rocknix-base.cfg) + our PanicOS override (panicos-override.cfg).
HERE="/storage/emulators/retroarch"
LOG="$HERE/last-launch.log"
BASE="$HERE/config/rocknix-base.cfg"
OVR="$HERE/config/panicos-override.cfg"
export LD_LIBRARY_PATH="$HERE/lib:/usr/lib:/lib:${LD_LIBRARY_PATH}"
export PAN_MESA_DEBUG=forcepack   # H700/Panfrost quirk (ROCKNIX sets this)
export HOME=/storage
SEL="${1:-}"; DEF="${2:-}"; shift 2 2>/dev/null || shift $#
ROM="$*"   # rejoin: ES may pass a spaced filename as multiple words
if [ -n "$SEL" ] && [ -f "$HERE/cores/${SEL}_libretro.so" ]; then CORE="$SEL"; else CORE="$DEF"; fi
# Layer ROCKNIX base + PanicOS override when the base is present; else our override alone.
if [ -f "$BASE" ]; then CFG=(--config "$BASE" --appendconfig "$OVR"); else CFG=(--config "$OVR"); fi
{
  echo "=== launch $(date) ==="
  echo "selected=$SEL default=$DEF -> core=$CORE  rom=[$ROM]"
  echo "config: ${CFG[*]}"
  echo "env: WAYLAND_DISPLAY=$WAYLAND_DISPLAY SDL_VIDEODRIVER=$SDL_VIDEODRIVER SDL_AUDIODRIVER=$SDL_AUDIODRIVER"
} > "$LOG" 2>&1
exec "$HERE/bin/retroarch" -L "$HERE/cores/${CORE}_libretro.so" "${CFG[@]}" "$ROM" >> "$LOG" 2>&1
