# Emulator Manager GUI — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the stopgap "Manage" UI with a first-class, gamepad-driven SDL2 app that bootstraps and maintains the emulator suite (install/remove/update/swap-core per system, with honest state + confirmations).

**Architecture:** Thin native front-end (`src/emu-manager.c`, SDL2) over the proven shell engine (`bin/panicos-emu-install.sh`). The engine gains machine verbs (`--quick-setup`, `--install`, `--remove`, `--set-core`, `--update`; `--status` exists). The GUI renders state from `--status`, runs verbs, and streams their output. `.enabled-systems` is the source of truth for what's installed.

**Tech Stack:** C + SDL2 (embedded 8×8 font, no SDL_ttf), POSIX shell, jq, git; aarch64 cross-compile via Docker. Target: Anbernic RG35XX Pro (Allwinner H700) running PanicOS (sway/Wayland + PipeWire).

---

## Conventions & prerequisites

- **Device access:** `ssh panicos` (root, key auth). Repo clone on device: `/storage/.panicos-emu`. Dev repo: `/Users/seajay/gits/panicos-emu`.
- **Deploy a change:** commit+push on the Mac, then `ssh panicos 'git -C /storage/.panicos-emu pull --ff-only'`.
- **Cross-compile the GUI:** `bash scripts/build-emu-manager.sh` (Docker arm64 + SDL2) → `bin/emu-manager` (commit the binary).
- **Commit trailer:** end every commit body with `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- **Engine testability:** Task 1 makes the engine's paths env-overridable so verb logic can be tested in a temp sandbox on any machine (no root/device/network). Grafting (download/extract) is integration-tested on-device only.

## File structure

| File | Responsibility | Action |
|------|----------------|--------|
| `bin/panicos-emu-install.sh` | Engine: status + verbs + es_systems generation | Modify (env-overridable paths; add verbs) |
| `recommended-systems.conf` | Newline list of Quick-Setup default systems (all but `nds`) | Create |
| `tests/engine-test.sh` | Bash assertions for verb logic in a temp sandbox | Create |
| `src/emu-manager.c` | GUI: state machine + screens (reuses font/terminal/streamer) | Rewrite the menu/state section |
| `bin/emu-manager` | Prebuilt aarch64 binary | Rebuild + commit each GUI task |
| `scripts/build-emu-manager.sh` | Docker cross-compile | Exists |
| `README.md` | Docs | Modify (Task 15) |

---

## Phase 1 — Engine verbs (TDD)

### Task 1: Env-overridable paths + test harness

**Files:**
- Modify: `bin/panicos-emu-install.sh` (the `PREFIX`/`ROMS`/`ES_TARGET`/`ES_ORIG`/`SELECTION`/`REPO_DIR` definitions near the top)
- Create: `tests/engine-test.sh`

- [ ] **Step 1: Write the failing test**

Create `tests/engine-test.sh`:
```bash
#!/bin/bash
# Engine verb-logic tests in a temp sandbox (no device/root/network).
set -u
HERE="$(cd "$(dirname "$0")/.." && pwd)"
FAIL=0
t(){ if eval "$2"; then echo "  ok: $1"; else echo "  FAIL: $1"; FAIL=1; fi; }

setup(){
  SBOX="$(mktemp -d)"
  export PE_PREFIX="$SBOX/emu" PE_ROMS="$SBOX/roms" PE_REPO="$HERE"
  export PE_ES_TARGET="$SBOX/es_systems.cfg" PE_ES_ORIG="$SBOX/es_systems.orig"
  mkdir -p "$PE_PREFIX/cores" "$PE_ROMS"
  printf '<systemList>\n  <system><name>tools</name><path>/x</path><extension>.sh</extension><command>%%ROM%%</command><platform>tools</platform></system>\n</systemList>\n' > "$PE_ES_ORIG"
}
teardown(){ rm -rf "$SBOX"; }

# placeholder assertion — real cases added in later tasks
setup
t "status runs without error" "bash '$HERE/bin/panicos-emu-install.sh' --status >/dev/null 2>&1"
teardown
exit $FAIL
```

- [ ] **Step 2: Run it to verify it fails**

Run: `bash tests/engine-test.sh`
Expected: FAIL — the engine still hardcodes `/storage/...` paths and requires root, so `--status` errors.

- [ ] **Step 3: Make paths env-overridable**

In `bin/panicos-emu-install.sh`, replace the fixed path block with env-overridable defaults, and make the root check skip when sandboxed:
```sh
PREFIX="${PE_PREFIX:-/storage/emulators/retroarch}"
ROMS="${PE_ROMS:-/storage/roms}"
REPO_DIR="${PE_REPO:-$(cd "$(dirname "$0")/.." && pwd)}"
ES_TARGET="${PE_ES_TARGET:-/etc/emulationstation/es_systems.cfg}"
ES_ORIG="${PE_ES_ORIG:-${ES_TARGET}.panicos-orig}"
ES_STALE="/storage/.emulationstation/es_systems.cfg"
SELECTION="$PREFIX/.enabled-systems"
INSTALLED_MARK="$PREFIX/.installed-rocknix"
CORES_MARK="$PREFIX/.installed-cores"
```
And gate the root requirement so sandboxed runs (PE_PREFIX set) don't need root:
```sh
if [ -z "${PE_PREFIX:-}" ]; then [ "$(id -u)" = 0 ] || die "must run as root"; fi
```
Keep `LOCK`/`SYSTEMS_CONF` pointed at `$REPO_DIR`.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash tests/engine-test.sh`
Expected: PASS — `status runs without error`.

- [ ] **Step 5: Commit**
```bash
git add bin/panicos-emu-install.sh tests/engine-test.sh
git commit -m "test(engine): env-overridable paths + sandbox test harness"
```

### Task 2: `--quick-setup` + recommended set

**Files:**
- Create: `recommended-systems.conf`
- Modify: `bin/panicos-emu-install.sh` (flag parsing; add `quick_setup()`)
- Modify: `tests/engine-test.sh`

- [ ] **Step 1: Write the failing test** — append to `tests/engine-test.sh` before `exit`:
```bash
setup
bash "$HERE/bin/panicos-emu-install.sh" --quick-setup --no-graft >/dev/null 2>&1
t "quick-setup writes enabled-systems"   "[ -s '$PE_PREFIX/.enabled-systems' ]"
t "quick-setup includes snes"            "grep -qx snes '$PE_PREFIX/.enabled-systems'"
t "quick-setup excludes nds"             "! grep -qx nds '$PE_PREFIX/.enabled-systems'"
teardown
```

- [ ] **Step 2: Run to verify it fails**

Run: `bash tests/engine-test.sh`
Expected: FAIL — `--quick-setup`/`--no-graft` unknown; no enabled-systems written.

- [ ] **Step 3: Implement**

Create `recommended-systems.conf` (every system except DS):
```
gb
nes
snes
gba
genesis
mastersystem
gamegear
colecovision
ngp
pcengine
wonderswan
n64
```
In `bin/panicos-emu-install.sh` flag parser add `--quick-setup) QUICK=1 ;;` and `--no-graft) NOGRAFT=1 ;;` (init `QUICK=0 NOGRAFT=0`). Add:
```sh
quick_setup(){
  mkdir -p "$PREFIX"
  local rec="$REPO_DIR/recommended-systems.conf"
  if [ -f "$rec" ]; then grep -vE '^\s*#|^\s*$' "$rec" > "$SELECTION"
  else conf_rows | awk -F'|' '{gsub(/ /,"",$1);print $1}' | grep -vx nds > "$SELECTION"; fi
  log "quick setup: $(wc -l < "$SELECTION" | tr -d ' ') systems selected"
}
```
In `main()`, before the graft decision: `if [ "$QUICK" = 1 ]; then quick_setup; fi`. Guard the actual graft with `[ "$NOGRAFT" = 1 ]` (skip graft + render when set — for tests).

- [ ] **Step 4: Run to verify it passes**

Run: `bash tests/engine-test.sh`
Expected: PASS — all three quick-setup assertions.

- [ ] **Step 5: Commit**
```bash
git add recommended-systems.conf bin/panicos-emu-install.sh tests/engine-test.sh
git commit -m "feat(engine): --quick-setup writes recommended set (all but DS)"
```

### Task 3: `--install <system>`

**Files:** Modify `bin/panicos-emu-install.sh`, `tests/engine-test.sh`

- [ ] **Step 1: Failing test** — append:
```bash
setup
bash "$HERE/bin/panicos-emu-install.sh" --install snes --no-graft >/dev/null 2>&1
t "install adds system"      "grep -qx snes '$PE_PREFIX/.enabled-systems'"
bash "$HERE/bin/panicos-emu-install.sh" --install snes --no-graft >/dev/null 2>&1
t "install is idempotent"    "[ \"$(grep -cx snes '$PE_PREFIX/.enabled-systems')\" = 1 ]"
teardown
```

- [ ] **Step 2: Run to verify it fails** — `bash tests/engine-test.sh` → FAIL (`--install` unknown).

- [ ] **Step 3: Implement** — flag: `--install) shift_val INSTALL_SYS "$@"` — simplest: parse `--install=<sys>` OR a following arg. Use a positional capture:
```sh
# in the arg loop:
--install) ACTION=install ;;
--remove)  ACTION=remove ;;
--set-core) ACTION=setcore ;;
*) if [ -n "${ACTION:-}" ] && [ -z "${ARG1:-}" ]; then ARG1="$a";
   elif [ -n "${ACTION:-}" ] && [ -z "${ARG2:-}" ]; then ARG2="$a";
   else die "unknown arg: $a"; fi ;;
```
Add helper + dispatch:
```sh
enable_system(){ mkdir -p "$PREFIX"; touch "$SELECTION"; grep -qxF "$1" "$SELECTION" || echo "$1" >> "$SELECTION"; }
disable_system(){ [ -f "$SELECTION" ] && grep -vxF "$1" "$SELECTION" > "$SELECTION.tmp" && mv "$SELECTION.tmp" "$SELECTION"; }
```
In `main()` (after status handling): `case "${ACTION:-}" in install) enable_system "$ARG1";; remove) disable_system "$ARG1";; esac` — then fall through to graft+render (so install grafts the new cores and re-renders) unless `--no-graft`.

- [ ] **Step 4: Run to verify it passes** — `bash tests/engine-test.sh` → PASS.

- [ ] **Step 5: Commit**
```bash
git add bin/panicos-emu-install.sh tests/engine-test.sh
git commit -m "feat(engine): --install <system> (enable + graft + render)"
```

### Task 4: `--remove <system>` (never deletes ROMs)

**Files:** Modify `bin/panicos-emu-install.sh`, `tests/engine-test.sh`

- [ ] **Step 1: Failing test** — append:
```bash
setup
printf 'snes\nnes\n' > "$PE_PREFIX/.enabled-systems"
mkdir -p "$PE_ROMS/snes"; echo dummy > "$PE_ROMS/snes/game.sfc"
bash "$HERE/bin/panicos-emu-install.sh" --remove snes --no-graft >/dev/null 2>&1
t "remove drops system"      "! grep -qx snes '$PE_PREFIX/.enabled-systems'"
t "remove keeps other"       "grep -qx nes '$PE_PREFIX/.enabled-systems'"
t "remove keeps ROMs"        "[ -f '$PE_ROMS/snes/game.sfc' ]"
teardown
```

- [ ] **Step 2: Run to verify it fails** — FAIL (remove not wired / would need it to not touch roms).

- [ ] **Step 3: Implement** — `disable_system` already added (Task 3); ensure `--remove` dispatch calls it and that the render/graft path NEVER deletes `$ROMS/*`. Confirm `render_configs` only `mkdir`s rom dirs (it does). For `--remove`, skip graft (nothing to fetch); just `disable_system` + `render_configs`.

- [ ] **Step 4: Run to verify it passes** — PASS.

- [ ] **Step 5: Commit**
```bash
git add bin/panicos-emu-install.sh tests/engine-test.sh
git commit -m "feat(engine): --remove <system> (disable + render, ROMs untouched)"
```

### Task 5: `--set-core <system> <core>` (default-core override)

**Files:** Modify `bin/panicos-emu-install.sh`, `tests/engine-test.sh`

- [ ] **Step 1: Failing test** — append:
```bash
setup
printf 'snes\n' > "$PE_PREFIX/.enabled-systems"
: > "$PE_PREFIX/cores/snes9x2010_libretro.so"
: > "$PE_PREFIX/cores/snes9x_libretro.so"
bash "$HERE/bin/panicos-emu-install.sh" --set-core snes snes9x2010 --no-graft >/dev/null 2>&1
t "set-core records override" "grep -q 'snes|snes9x2010' '$PE_PREFIX/.core-overrides'"
t "es_systems uses override"  "grep -q 'retroarch.sh %CORE% snes9x2010' '$PE_ES_TARGET'"
teardown
```

- [ ] **Step 2: Run to verify it fails** — FAIL.

- [ ] **Step 3: Implement** — add `OVERRIDES="$PREFIX/.core-overrides"` (lines `system|core`). Dispatch `setcore) set_core "$ARG1" "$ARG2";;`:
```sh
set_core(){ mkdir -p "$PREFIX"; touch "$OVERRIDES"; grep -vE "^$1\\|" "$OVERRIDES" > "$OVERRIDES.tmp" 2>/dev/null; echo "$1|$2" >> "$OVERRIDES.tmp"; mv "$OVERRIDES.tmp" "$OVERRIDES"; }
override_core(){ [ -f "$OVERRIDES" ] && awk -F'|' -v s="$1" '$1==s{print $2}' "$OVERRIDES" | head -1; }
```
In `gen_es_systems`, after computing `installed[]` and `def="${installed[0]}"`, apply the override if its core is installed:
```sh
local ov; ov="$(override_core "$name")"
[ -n "$ov" ] && [ -f "$PREFIX/cores/${ov}_libretro.so" ] && def="$ov"
```
`--set-core` then runs `render_configs` (no graft).

- [ ] **Step 4: Run to verify it passes** — PASS.

- [ ] **Step 5: Commit**
```bash
git add bin/panicos-emu-install.sh tests/engine-test.sh
git commit -m "feat(engine): --set-core <sys> <core> default-core override (survives updates)"
```

### Task 6: Update detection + `--update`

**Files:** Modify `bin/panicos-emu-install.sh`, `tests/engine-test.sh`

- [ ] **Step 1: Failing test** — append:
```bash
setup
echo "20260101" > "$PE_PREFIX/.installed-rocknix"
out="$(bash "$HERE/bin/panicos-emu-install.sh" --check-update 2>/dev/null)"
t "check-update reports available" "echo \"$out\" | grep -q '^update:'"
echo "$(jq -r .rocknix_version "$HERE/rocknix.lock")" > "$PE_PREFIX/.installed-rocknix"
out2="$(bash "$HERE/bin/panicos-emu-install.sh" --check-update 2>/dev/null)"
t "check-update reports current"   "echo \"$out2\" | grep -q '^uptodate:'"
teardown
```

- [ ] **Step 2: Run to verify it fails** — FAIL.

- [ ] **Step 3: Implement** — flags `--check-update) CHECK=1 ;;` and `--update) UPDATE=1 ;;`. Early in `main()`:
```sh
if [ "${CHECK:-0}" = 1 ]; then
  cur="$(cat "$INSTALLED_MARK" 2>/dev/null || echo none)"
  [ "$cur" = "$RKVER" ] && echo "uptodate:$RKVER" || echo "update:$cur->$RKVER"
  exit 0
fi
```
`--update` just sets `FORCE=1` semantics for already-enabled systems: it runs the normal graft+render path (which re-fetches at the locked version). No new code beyond routing `UPDATE` into the standard install path.

- [ ] **Step 4: Run to verify it passes** — PASS. Run the whole suite: `bash tests/engine-test.sh` → all green.

- [ ] **Step 5: Commit**
```bash
git add bin/panicos-emu-install.sh tests/engine-test.sh
git commit -m "feat(engine): --check-update / --update (suite-wide version compare)"
```

### Task 7: On-device integration check of the verbs

**Files:** none (verification)

- [ ] **Step 1:** Deploy: `ssh panicos 'git -C /storage/.panicos-emu pull --ff-only -q'`.
- [ ] **Step 2:** `ssh panicos 'bash /storage/.panicos-emu/bin/panicos-emu-install.sh --status'` → 13 lines, pipe-delimited.
- [ ] **Step 3:** `ssh panicos 'bash /storage/.panicos-emu/bin/panicos-emu-install.sh --check-update'` → `uptodate:` or `update:`.
- [ ] **Step 4:** `ssh panicos 'bash /storage/.panicos-emu/bin/panicos-emu-install.sh --remove wonderswan && grep -c . /storage/emulators/retroarch/.enabled-systems'` then re-add with `--install wonderswan`; confirm es_systems reflects it and ROM counts are unchanged.
- [ ] **Step 5:** Commit (no-op) — note results in the PR/commit message of the next task.

---

## Phase 2 — GUI state machine & screens

> The GUI reuses the existing `font_init`/`draw_char`/`draw_text`/`fill`, the ANSI-stripping mini-terminal, and `cmd_start`/`cmd_poll` from `src/emu-manager.c`. Each task replaces or adds a screen, then **cross-compiles + headless-smoke-tests**. Interactive behavior is verified on-device by the user at the end.

### Task 8: State-machine skeleton + Home screen

**Files:** Modify `src/emu-manager.c`

- [ ] **Step 1: Replace the state enum + menu data + main loop dispatch.** Define states and a `screen` variable:
```c
enum { ST_HOME, ST_LIBRARY, ST_SHEET, ST_CONFIRM, ST_QSETUP, ST_PROGRESS, ST_SETTINGS, ST_HELP };
static int screen = ST_HOME, hsel = 0;
```
Home tiles:
```c
static const char *HOME[] = { "Quick Setup", "Library", "Update", "Settings", "Help / ROMs" };
#define NHOME 5
```
Add a `status_summary()` that runs `INSTALL " --status"` and counts enabled rows + sums roms, and `check_update()` that runs `INSTALL " --check-update"` and returns 1 if it starts with `update:`. Render Home: title, `"%d systems · %d games"`, an "update available" badge if `check_update()`, and the tiles (highlight `hsel`). Input: up/down move `hsel`; A → switch screen (Quick Setup→ST_QSETUP, Library→load+ST_LIBRARY, Update→cmd_start update + ST_PROGRESS, Settings→ST_SETTINGS, Help→ST_HELP); B → quit.

- [ ] **Step 2: Cross-compile** — `bash scripts/build-emu-manager.sh` → Expected: `bin/emu-manager: ELF 64-bit ... ARM aarch64`.
- [ ] **Step 3: Headless smoke** — deploy, then `ssh panicos '... emu-manager &' ; sleep 3` and confirm process alive + no missing libs (`LD_TRACE_LOADED_OBJECTS=1`). Expected: running, 0 missing.
- [ ] **Step 4: Commit**
```bash
git add src/emu-manager.c bin/emu-manager
git commit -m "feat(ui): home screen + state-machine skeleton"
```

### Task 9: Library screen (real state, filters, scroll)

**Files:** Modify `src/emu-manager.c`

- [ ] **Step 1: Implement.** Extend the existing `sysrow_t`/`manage_load()` parser to also store `update` (computed from a one-shot `check_update()` applied to enabled rows) and a `def[24]` core string (7th `--status` field). Add `int lib_filter` (0=All 1=Installed 2=Get 3=Updates) and `lib_top/lib_sel`. Render rows with badge logic:
```c
/* badge: enabled&&inst>0 -> ON(green); update -> UPD(yellow); else GET(grey) */
```
Each row: `[badge] Fullname        core/“not installed · you have N”   Ngames`. Filter tabs across the top; D-pad scroll (reuse VIS windowing); A → open ST_SHEET for `SYS[lib_sel]`; X cycles `lib_filter`; B → ST_HOME.
- [ ] **Step 2: Cross-compile** — `bash scripts/build-emu-manager.sh`.
- [ ] **Step 3: Headless smoke** — launch on device 3s; confirm it parses `--status` (no crash). Expected: running.
- [ ] **Step 4: Commit**
```bash
git add src/emu-manager.c bin/emu-manager
git commit -m "feat(ui): Library screen — true ON/GET/UPD state, filters, scroll"
```

### Task 10: Action sheet (context actions → verbs)

**Files:** Modify `src/emu-manager.c`

- [ ] **Step 1: Implement.** A `sheet_for(sysrow_t*)` builds a context action list:
  - GET → `{ "Install", "Change core", "Cancel" }`
  - ON  → `{ "Change core", "Remove", "Reinstall", "Cancel" }`
  - UPD → `{ "Update", "Change core", "Remove", "Cancel" }`
  Render as an overlay panel (reuse `fill` + `draw_text`), `ssel` cursor. A on:
  - Install → `cmd_start(INSTALL " --install <name>", ST_LIBRARY)` ; ST_PROGRESS.
  - Update  → `cmd_start(INSTALL " --update", ST_LIBRARY)`.
  - Remove / Reinstall → ST_CONFIRM (Task 11).
  - Change core → a sub-list of that system's installed cores (from `def`/status); A → `cmd_start(INSTALL " --set-core <name> <core>", ST_LIBRARY)`.
  B → back to ST_LIBRARY.
- [ ] **Step 2: Cross-compile.** **Step 3: Smoke.** **Step 4: Commit** `"feat(ui): per-system action sheet wired to engine verbs"`.

### Task 11: Confirmations

**Files:** Modify `src/emu-manager.c`

- [ ] **Step 1: Implement.** A `confirm_t { char title[64], body[160], cmd[256]; }` set when entering ST_CONFIRM. Render centered: title, body, `[A] Yes   [B] No`. A → `cmd_start(confirm.cmd, ST_LIBRARY)` + ST_PROGRESS; B → back to ST_SHEET. Wire Remove → `body="Removes the core. Keeps your N ROMs."`, `cmd=INSTALL " --remove <name>"`; Reinstall → `body="Already installed & up to date. Re-download anyway?"`, `cmd=INSTALL " --install <name> --force"`.
- [ ] **Step 2: Cross-compile.** **Step 3: Smoke.** **Step 4: Commit** `"feat(ui): confirmations for remove/reinstall"`.

### Task 12: Quick Setup screen

**Files:** Modify `src/emu-manager.c`

- [ ] **Step 1: Implement.** ST_QSETUP renders the recommended-set summary (static chips list mirroring `recommended-systems.conf` + the "DS excluded / N64 heavy / ≈1.5 GB · Wi-Fi" notes) and actions `[Install] [Customize in Library] [Cancel]`. Install → `cmd_start(INSTALL " --quick-setup", ST_HOME)` + ST_PROGRESS; Customize → load + ST_LIBRARY; Cancel → ST_HOME.
- [ ] **Step 2: Cross-compile.** **Step 3: Smoke.** **Step 4: Commit** `"feat(ui): Quick Setup confirm screen"`.

### Task 13: Progress polish + Settings + Help

**Files:** Modify `src/emu-manager.c`

- [ ] **Step 1: Implement.** Progress (ST_PROGRESS): keep the streamed mini-terminal; add a header line and, when the stream contains a `current / total` byte pair (the engine's download line), draw a bar. On `g_done`, footer "Restart EmulationStation to see your systems. A: back" → returns to `after_run`. Settings (ST_SETTINGS): a small menu — "Install ALL cores (parity)" → `cmd_start(INSTALL " --all-cores", ST_HOME)`; "BIOS folder: /storage/roms/bios" (info); "Re-render config" → `INSTALL " --render-only"`. Help (ST_HELP): static text — ROM paths, controls, repo URL. B returns to ST_HOME from both.
- [ ] **Step 2: Cross-compile.** **Step 3: Smoke.** **Step 4: Commit** `"feat(ui): progress bar + Settings + Help"`.

---

## Phase 3 — Integration, on-device verification, docs

### Task 14: Full on-device verification (user-driven)

**Files:** none

- [ ] **Step 1:** Deploy latest (`git pull` on device) and confirm `bin/emu-manager` runs fullscreen.
- [ ] **Step 2:** User test matrix (record results): Home shows correct counts/badge; Library badges match reality; A on a GET system installs with live progress; Remove asks + keeps ROMs; Change core swaps the default; Quick Setup on a fresh `.enabled-systems` reproduces the set; Update path; B returns to ES cleanly.
- [ ] **Step 3:** Fix any issues found (loop back to the relevant task).
- [ ] **Step 4: Commit** any fixes.

### Task 15: Docs + memory

**Files:** Modify `README.md`; update memory

- [ ] **Step 1:** Update README "On-device manager" section to describe the real screens (Home/Quick Setup/Library/action sheet/confirm/progress) and remove any stale wording.
- [ ] **Step 2:** Update the project memory (`panicos-emu-repo`) noting the GUI is a real state-machine app driving engine verbs.
- [ ] **Step 3: Commit**
```bash
git add README.md
git commit -m "docs: on-device manager (Home/Library/action sheet/Quick Setup)"
git push
```

---

## Self-review notes

- **Spec coverage:** Home (T8), Quick Setup (T2/T12), Library state+filters (T6/T9), action sheet (T10), confirmations (T11), progress (T13), settings/help (T13), engine verbs (T2–T6), controls (existing loop), error handling (engine net/space checks + non-zero exit shown in progress), testing (T1 harness + T7/T14). `.enabled-systems` source of truth (T2–T4); `--set-core` override (T5); update detection (T6).
- **Consistency:** verb names (`--quick-setup`, `--install`, `--remove`, `--set-core`, `--update`, `--check-update`, `--status`, `--no-graft`) used identically across engine and GUI tasks. State enum names (`ST_*`) consistent T8→T13.
- **Non-goals respected:** no batch multi-select, no es_features/per-game ES core picker, DS excluded from `recommended-systems.conf`.
