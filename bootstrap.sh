#!/bin/bash
###############################################################################
# panicos-emu bootstrap — one-shot first-time setup on a PanicOS device.
#
# SSH into your device (root) and run:
#   curl -fsSL https://raw.githubusercontent.com/seajaysec/panicos-emu/master/bootstrap.sh | bash
#
# Want every ROCKNIX core (parity)?  add args after a --:
#   curl -fsSL .../bootstrap.sh | bash -s -- --all-cores
#
# Re-running is safe: it updates the clone and re-installs. After setup, updates are just the
# "Update Emulators" entry under Ports (no SSH needed). Requires this repo to be PUBLIC.
###############################################################################
set -euo pipefail

REPO_URL="https://github.com/seajaysec/panicos-emu.git"
CLONE="/storage/.panicos-emu"

say(){ printf '\033[36m[panicos-emu]\033[0m %s\n' "$*"; }
die(){ printf '\033[31m[panicos-emu] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || die "run as root (you're on the device's root shell)"
grep -qi panicos /etc/os-release 2>/dev/null || die "this is for PanicOS devices only"
command -v git >/dev/null 2>&1 || die "git not found on this device"
timeout 8 ping -c1 github.com >/dev/null 2>&1 || die "no network — connect Wi-Fi first"

if [ -d "$CLONE/.git" ]; then
  say "updating existing clone…"
  git -C "$CLONE" remote set-url origin "$REPO_URL"
  git -C "$CLONE" config --unset core.sshCommand 2>/dev/null || true   # migrate off any old deploy key
  git -C "$CLONE" fetch -q origin
  git -C "$CLONE" reset -q --hard origin/HEAD
else
  say "cloning panicos-emu…"
  git clone -q --depth 1 "$REPO_URL" "$CLONE"
fi

say "running installer (downloads RetroArch + cores from ROCKNIX; first run takes a few min)…"
bash "$CLONE/bin/panicos-emu-install.sh" "$@"

echo
say "done!  Restart EmulationStation (Quit from the menu) or reboot to see the systems."
say "Copy ROMs into /storage/roms/<system>/  (gb, nes, snes, gba, genesis, n64, nds, …)."
say "Update anytime from the device: Ports → 'Update Emulators'."
