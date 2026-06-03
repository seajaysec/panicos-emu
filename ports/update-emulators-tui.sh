#!/bin/bash
# panicos-emu — fullscreen terminal menu (runs inside foot, navigated via gptokeyb).
export LANG=C.UTF-8 LC_ALL=C.UTF-8
CLONE="/storage/.panicos-emu"
INSTALL="$CLONE/bin/panicos-emu-install.sh"
UNINSTALL="$CLONE/bin/panicos-emu-uninstall.sh"

options=(
  "Update emulators        (sync with ROCKNIX, apply systems.conf)"
  "Install ALL cores       (full ROCKNIX parity)"
  "Re-render config only   (no download)"
  "Uninstall               (keep your ROMs)"
  "Quit"
)
sel=0

draw(){
  printf '\033[H\033[2J'
  echo "==================================================="
  echo "     panicos-emu   ·   Emulator Manager"
  echo "==================================================="
  echo
  local i
  for i in "${!options[@]}"; do
    if [ "$i" = "$sel" ]; then printf '   \033[7m %s \033[0m\n\n' "${options[$i]}"
    else printf '    %s\n\n' "${options[$i]}"; fi
  done
  echo "---------------------------------------------------"
  echo "   D-pad: move    A: select    B / Select: quit"
}

key(){ local k r; IFS= read -rsn1 k; if [ "$k" = $'\e' ]; then IFS= read -rsn2 -t 0.05 r 2>/dev/null; k+="$r"; fi; printf '%s' "$k"; }
pause(){ echo; echo "   --- press A to return to the menu ---"; while :; do case "$(key)" in ''|$'\n'|$'\r') break;; esac; done; }
net_ok(){ timeout 8 ping -c1 github.com >/dev/null 2>&1; }
run(){ printf '\033[H\033[2J'; echo ">> $1"; echo; shift; "$@" 2>&1; echo; echo "Restart EmulationStation (or reboot) to apply changes."; pause; }

while :; do
  draw
  case "$(key)" in
    $'\e[A'|k) sel=$(( (sel - 1 + ${#options[@]}) % ${#options[@]} )) ;;
    $'\e[B'|j) sel=$(( (sel + 1) % ${#options[@]} )) ;;
    ''|$'\n'|$'\r')
      case "$sel" in
        0) if net_ok; then run "Updating…" bash -c "git -C '$CLONE' pull --ff-only; bash '$INSTALL'";
           else printf '\033[H\033[2J'; echo "No network — connect Wi-Fi and try again."; pause; fi ;;
        1) if net_ok; then run "Installing all cores…" bash -c "git -C '$CLONE' pull --ff-only; bash '$INSTALL' --all-cores";
           else printf '\033[H\033[2J'; echo "No network — connect Wi-Fi and try again."; pause; fi ;;
        2) run "Re-rendering config…" bash "$INSTALL" --render-only ;;
        3) run "Uninstalling (keeping ROMs)…" bash "$UNINSTALL" --keep-roms ;;
        4) exit 0 ;;
      esac ;;
    $'\e') exit 0 ;;
  esac
done
