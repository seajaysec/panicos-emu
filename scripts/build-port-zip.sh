#!/bin/bash
# Assemble the PortMaster autoinstall bundle: dist/panicos-emu.zip
#
# Layout inside the zip (PortMaster "version 4" port):
#   Update Emulators.sh        <- the launcher (self-bootstraps the clone, execs the GUI)
#   panicos-emu/port.json      <- PortMaster metadata
#
# Reproducible: sources live in ports/. Run locally or from the release workflow.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="dist/panicos-emu.zip"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

mkdir -p dist "$STAGE/panicos-emu"
cp "ports/Update Emulators.sh" "$STAGE/Update Emulators.sh"
cp "ports/port.json"           "$STAGE/panicos-emu/port.json"
chmod 0755 "$STAGE/Update Emulators.sh"

rm -f "$OUT"
( cd "$STAGE" && zip -q -X -r -D "$OLDPWD/$OUT" "Update Emulators.sh" "panicos-emu" )
echo "built $OUT"
unzip -l "$OUT"
