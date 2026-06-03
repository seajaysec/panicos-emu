#!/bin/bash
###############################################################################
# Update Emulators.sh  —  on-device entry point (lands in Ports as "Update Emulators")
#
# Pulls the latest panicos-emu repo (which auto-tracks ROCKNIX) and runs the
# installer. Requires network. Applies nothing unattended — you launch it.
#
# Installed to /storage/roms/ports/ by the installer's --add-port step (or copy
# it there manually). Progress prints to screen and to the log below.
###############################################################################
CLONE="/storage/.panicos-emu"
LOG="/storage/roms/ports/Update Emulators.log"

exec > >(tee "$LOG") 2>&1
echo "=================================================="
echo " panicos-emu :: Update Emulators"
echo "=================================================="

if [ ! -d "$CLONE/.git" ]; then
  echo "ERROR: repo clone not found at $CLONE"
  echo "Set it up once over SSH (see repo README: 'Wire device deploy key + clone')."
  sleep 8; exit 1
fi

echo "* checking network…"
if ! timeout 8 ping -c1 github.com >/dev/null 2>&1; then
  echo "ERROR: no network. Connect Wi-Fi and try again."
  sleep 8; exit 1
fi

echo "* pulling latest panicos-emu…"
git -C "$CLONE" pull --ff-only || { echo "git pull failed"; sleep 8; exit 1; }

echo "* running installer…"
bash "$CLONE/bin/panicos-emu-install.sh" "$@" || { echo "installer failed"; sleep 8; exit 1; }

echo
echo "Done. Restart EmulationStation (Main Menu > Quit > Restart) or reboot"
echo "to pick up any new systems. Log: $LOG"
sleep 6
