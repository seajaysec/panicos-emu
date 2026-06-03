#!/bin/bash
# panicos-emu — sandboxed RetroArch launcher (rendered to /storage/emulators/retroarch/retroarch.sh).
# Usage: retroarch.sh <core_basename> "<rom path>"
# Sandbox libs take precedence so RetroArch uses ROCKNIX's consistent ffmpeg/etc set,
# while the rest of the system is untouched.
HERE="/storage/emulators/retroarch"
export LD_LIBRARY_PATH="$HERE/lib:/usr/lib:/lib:${LD_LIBRARY_PATH}"
export PAN_MESA_DEBUG=forcepack   # H700/Panfrost quirk (ROCKNIX sets this)
export HOME=/storage
CORE="$1"; shift
exec "$HERE/bin/retroarch" -L "$HERE/cores/${CORE}_libretro.so" --config "$HERE/config/retroarch.cfg" "$@"
