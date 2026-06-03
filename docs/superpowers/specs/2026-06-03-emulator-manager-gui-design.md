# panicos-emu Manager — GUI design

**Date:** 2026-06-03
**Status:** Approved (brainstorm), ready for implementation plan
**Component:** `src/emu-manager.c` (native SDL2 app) + verbs in `bin/panicos-emu-install.sh`

## Problem

PanicOS ships no console emulators. `panicos-emu` already grafts RetroArch + ROCKNIX cores and
maintains them, but the **on-device experience** has been a stopgap: first a headless shell
script (blank screen), then a foot terminal (no UTF-8 locale on this Buildroot device), then a
crude SDL menu whose "manage" list faked a selection state — you couldn't clearly see what was
installed vs missing, couldn't say "install these," and got no confirmation before an expensive
re-download.

This design replaces that with a **first-class, native, gamepad-driven app** for two jobs:
1. **Bootstrap** — a dead-simple way to go from nothing to a working emulator set.
2. **Maintain** — install / remove / update / swap-core per system, with clear state and confirmations.

## Goals

- Clear, honest per-system **state** (installed / not installed / update available) — never a fake checkbox.
- Dead-simple first run via **Quick Setup** (one confirm, then it just works).
- Explicit per-item **actions** with **confirmation** for expensive/destructive ones (reinstall, remove).
- Honest **progress** for the slow (~1.5 GB) download — steps, bar, ETA, live log.
- Self-contained native app (SDL2 + embedded font; reads the H700 pad via SDL_Joystick).

## Non-goals (YAGNI)

- **Batch multi-select** install — rejected; per-item press-A-to-act is the model.
- **Per-game core selection in EmulationStation** — impossible on this ES build (stripped menu); core choice lives in this app's action sheet instead.
- **DS installed by default** — too slow on H700; available in the Library, not in Quick Setup.
- Rewriting the install engine in C; configgen; touching anything PanicOS owns.

## Architecture

A **thin native front-end over the existing shell engine.**

```
  emu-manager (SDL2, C)                 panicos-emu-install.sh (shell engine)
  ─────────────────────                 ────────────────────────────────────
  render state + capture input  ──────▶ --status            (inventory, machine-readable)
  run a verb, stream its stdout ──────▶ --quick-setup        (install recommended set)
  show progress / confirmations ─────▶ --install <system>   (add one system)
                                ─────▶ --remove <system>    (drop one system, keep ROMs)
                                ─────▶ --set-core <sys> <core>
                                ─────▶ --update             (git pull + re-graft installed)
```

- **Why this split:** the engine is proven (download/extract/lib-closure/es_systems). Rewriting it
  in C is large and risky for no benefit. The GUI stays small and focused; engine and GUI are each
  testable in isolation. The pure-shell/foot TUI already failed (locale), so a real SDL2 GUI is required.
- **GUI ↔ engine contract:** the GUI shells out (`fork`/`exec` `sh -c`), captures stdout+stderr via a
  pipe, and renders it in a mini-terminal that strips ANSI/CSI. The engine emits plain output when not
  a tty (`NO_COLOR`, tty-gated color) so the stream is clean.

### Source-of-truth & state model

- **`.enabled-systems`** (`/storage/emulators/retroarch/.enabled-systems`): newline list of system
  names the user wants installed. This is the source of truth for "what's installed."
  - `--install <sys>` adds the name, grafts its cores, regenerates es_systems.
  - `--remove <sys>` removes the name, regenerates es_systems (drops it). ROMs are never touched;
    core `.so`s may remain on disk (harmless, hidden).
  - `--quick-setup` writes the recommended set and installs it.
  - `--set-core <sys> <core>` records a per-system **default-core override** (a small file the engine
    reads when generating es_systems, so the choice survives updates) and re-renders. The chosen core
    must be one of that system's installed cores; the action sheet only offers installed cores.
- A system's **state** for the Library:
  - **ON** = name in `.enabled-systems` and its default core `.so` present.
  - **GET** = not enabled (or core missing).
  - **UPD** = enabled **and** the installed ROCKNIX version (`.installed-rocknix`) is older than
    `rocknix.lock`. Update is effectively **suite-wide** (a new ROCKNIX release re-grafts all installed
    systems); the badge surfaces on installed rows and as a Home badge. There is no per-system version.
- **`--status`** output (one line per system in `systems.conf`, ALL systems, not just enabled):
  `name|fullname|enabled|coresInstalled|coresTotal|romCount|defaultInstalledCore`
  The GUI derives ON/GET/UPD from `enabled`, core counts, and a separate version check.

## Screens & flows

State machine: `HOME → {QUICK_SETUP_CONFIRM, LIBRARY, UPDATE, SETTINGS, HELP}`,
`LIBRARY → ACTION_SHEET → {CONFIRM →} PROGRESS`, any action → `PROGRESS → (done) back`.

1. **Home** — status line (systems installed · game count · ROCKNIX version · up-to-date/updates).
   Tiles: **Quick Setup**, **Library**, **Update** (badge when newer ROCKNIX), **Settings**, **Help/ROMs**.
   On a device with nothing enabled, Quick Setup is visually primary.
2. **Quick Setup confirm** — lists the recommended set (all systems that run well; **N64** with a
   "some games heavy" note; **DS excluded**), shows "≈1.5 GB · a few minutes · needs Wi-Fi", and
   **Install / Customize in Library / Cancel**. Install → `--quick-setup` → PROGRESS.
3. **Library** — scrollable per-system list: name, state badge (✓ ON / + GET / ↻ UPD), game count,
   active core, and a "you have N games" hint for un-installed systems you own ROMs for. Filter tabs:
   All / Installed / Get / Updates. Press **A** → ACTION SHEET.
4. **Action sheet** (context-sensitive to the row's state):
   - GET → **Install** (+ Change core before install)
   - ON → **Change core ▸**, **Remove** (danger; "keeps your ROMs"), **Reinstall** (confirm)
   - UPD → **Update**
   - always **Cancel**
5. **Confirmations** — full-screen yes/no for **Reinstall** ("already installed & current — re-downloads
   ~X, continue?") and **Remove** ("removes the core, keeps your N ROMs"). A = confirm, B = cancel.
6. **Progress** — header "Installing… step N of M · don't power off", a 4-step checklist (check →
   download → extract/copy cores → configure), a download bar (MB / speed / ETA), and a live log tail
   (ANSI-stripped). On completion: "Done — restart EmulationStation (or reboot) to see your systems.
   Press A." B during a safe phase cancels.
7. **Update** — global; runs `--update`, shown via PROGRESS. Surfaced on Home (badge) and as a Library
   banner when `rocknix.lock` > installed version.
8. **Settings** — toggle **all-cores parity**, show **BIOS folder** path + which BIOS are present,
   advanced (force re-render). Minimal.
9. **Help / ROMs** — static screen: where to drop ROMs (`/storage/roms/<system>/`), controls, link.

**Controls:** D-pad / left stick = move; **A** = select/confirm; **B** = back / cancel / exit;
**Start** = also confirm. Read via SDL_Joystick using the H700 indices (A=1, B=0, up=13…), plus
keyboard fallback (arrows/Enter/Esc) for development.

## Error handling

- **No network** (install/update): detect before downloading; show a clear message, return to menu.
- **No disk space**: engine checks; surfaces in the log; GUI shows failure state.
- **Core missing in ROCKNIX** (e.g. `beetle_cygne`): engine warns and skips; not fatal.
- **Engine non-zero exit**: PROGRESS shows "Failed — see log", A returns to menu. The launcher self-heals
  on next run; `.enabled-systems` is only written on success paths where possible.
- **Launched without a clone**: the Ports launcher self-bootstraps (clones the public repo) before exec.

## Testing

- **Engine verbs**: test `--status` output shape, `--install/--remove` mutate `.enabled-systems` and
  regenerate es_systems correctly, idempotency, and that ROMs are never deleted. Runnable over SSH.
- **GUI**: headless smoke (launches fullscreen, parses `--status`, no missing libs); button-index
  mapping verified against the H700 pad. Interactive flows (navigation, confirm, progress) verified
  on-device by the user (cannot be driven over SSH).
- **Clean-room**: uninstall (keep ROMs) → Quick Setup reproduces a working set.

## Reuse vs new

- **Reuse:** SDL2 shell + embedded 8×8 font + ANSI-stripping mini-terminal + command streamer (in the
  current `emu-manager.c`); the entire install engine; the bootstrap + autoinstall onboarding.
- **New:** the screen/state machine (Home, Quick Setup, Library list with real state + filters, action
  sheet, confirmations, settings, help); engine verbs (`--quick-setup`, `--install`, `--remove`,
  `--set-core`, `--update`); update-available detection (version compare).
- **Replace:** the current fake-selection "Manage" screen.
