<div align="center">

# 🎮 panicos-emu

**Maintainable console emulation for [PanicOS](https://github.com/djhardrich/PanicOS), grafted from [ROCKNIX](https://rocknix.org) — without touching anything PanicOS owns.**

*Anbernic RG35XX Pro / Allwinner H700 · RetroArch + libretro · self-updating*

</div>

---

PanicOS is a deliberately lean, ROCKNIX-lineage appliance for the norns/PanicTracker world — it ships **no console emulators** on purpose (its own `es_systems.cfg` says so, and points you at *"layer them on top via overlay"*). `panicos-emu` is that layer: it pulls RetroArch and cores straight from ROCKNIX releases into a self-contained `/storage` sandbox, wires them into EmulationStation, and keeps the whole thing updatable against **both** ROCKNIX and PanicOS — always deferring to PanicOS where it has an opinion.

## ✨ What you get

- **13 console systems** out of the box — Game Boy/Color, NES, SNES, GBA, Genesis, Master System, Game Gear, ColecoVision, Neo Geo Pocket, PC Engine, WonderSwan, N64, Nintendo DS.
- **Multiple cores per system** — every listed core is installed (e.g. SNES → `snes9x` / `snes9x2010` / `beetle_supafaust`); the first is the default. Switch a system's default by reordering one line in `systems.conf` (no re-download — the cores are already there).
- **ROCKNIX's device-tuned QOL, inherited** — its full base `retroarch.cfg`, **153 per-core option presets** (mupen64 `4:3`/dynamic-recompiler, melonDS layout/JIT, …), per-core `.opt` files, and **1,800+ slang shaders** (LCD/scanline/CRT filters for the 640×480 panel) — all layered *under* our PanicOS overrides so nothing regresses.
- **Self-updating** — a GitHub Action tracks ROCKNIX daily; an on-device **Update Emulators** menu entry applies updates when *you* choose.
- **Correct on this exact hardware** — Wayland/`glcore` video and SDL2→PipeWire audio routed exactly the way PanicOS routes everything else (no muted-sink surprises, no codec-rate crackle).
- **Reversible & non-invasive** — everything lives in `/storage` plus one backed-up `/etc` file. One command restores stock.

## 🧱 Architecture — base + override

| Layer | Source | Edited? | Lands on device |
|------|--------|---------|-----------------|
| **Upstream** | ROCKNIX public releases — RetroArch, cores, libs, gamepad autoconfig | never | `/storage/emulators/retroarch/{bin,lib,cores,autoconfig}` |
| **Override** | this repo — `config/retroarch.cfg` (PanicOS appendconfig, always wins), `systems.conf` | yes, by you | `config/panicos-override.cfg`, `/etc/emulationstation/es_systems.cfg` |
| **Content** | your ROMs | — | `/storage/roms/<system>/` |

### The hard invariant
The installer writes **only** under `/storage/emulators/retroarch`, `/storage/roms`, and the single file `/etc/emulationstation/es_systems.cfg` — the overlay-layering spot the PanicOS maintainer explicitly sanctioned. That file is **backed up** to `es_systems.cfg.panicos-orig` and regenerated as a **superset of the pristine original** (PanicOS's own *tools*/*ports* inherited verbatim, never frozen or compounded). The installer **never** touches `/usr`, the audio codec pin, norns, or ports — so it can't conflict with a PanicOS deviation, and it self-heals if a PanicOS update replaces the file.

## 🔄 How updates flow

```
ROCKNIX release ──(daily GitHub Action, auto-commit)──▶ rocknix.lock on the repo
                                                              │
        you run "Update Emulators" on the device  ◀──────────┘   (manual · needs Wi-Fi)
                      │
                      ├─ git pull this repo (public HTTPS)
                      └─ bin/panicos-emu-install.sh
                            ├─ fetch the ROCKNIX SYSTEM image for the locked version
                            ├─ extract RetroArch + every core in systems.conf (+ full lib closure)
                            ├─ regenerate es_systems (superset of pristine /etc, per-system core defaults)
                            └─ self-test · preserve saves/states/bios/roms
```

- **ROCKNIX updates** — the Action bumps `rocknix.lock`; the device applies it next time you run the menu entry. Nothing is ever applied unattended.
- **PanicOS updates** — handled by PanicOS; our `/storage` layer survives, and the next run rebuilds the `/etc` override from the new pristine stock.
- **Binaries are never committed** — always fetched fresh from ROCKNIX, so the repo stays tiny and current.

## ⚙️ Configuration

### `systems.conf` — declarative, one line per console
```
# name | fullname | cores (first = default) | extensions | platform
snes | Super Nintendo | snes9x snes9x2010 beetle_supafaust | sfc smc | snes
nds  | Nintendo DS    | melonds melondsds desmume          | nds     | nds
```
- **Add a system, or change a system's default core:** edit a line (first core = default) → commit → run **Update Emulators**. The installer fetches what's needed and regenerates everything. Cores ROCKNIX doesn't ship are skipped automatically (never fatal).
- **Per-game core selection is not available** on this PanicOS EmulationStation build — it ships a stripped gamelist menu with no metadata editor, so cores are chosen **per-system** (the line above). All alternate cores are installed regardless, so changing a default is instant.
- **Per-game tweaks that DO work:** in a running game, hotkey **+ X** → **Quick Menu** → set Shaders / Core Options / aspect, then **Overrides → Save Game Override** so they auto-load for that game next time.

### Full ROCKNIX parity
Want *every* core ROCKNIX builds available, not just the ones in `systems.conf`?
```sh
bash bin/panicos-emu-install.sh --all-cores
```
The choice is remembered across updates.

## 🚀 Install

Pick whichever you like — **no SSH required** for the first option.

### A) PortMaster autoinstall (no computer login)
1. Download **`panicos-emu.zip`** from the [latest release](https://github.com/seajaysec/panicos-emu/releases/latest).
2. Copy it onto the device at **`/storage/roms/ports/autoinstall/`** (over the SD card, USB, or scp).
3. Launch **PortMaster** once (or reboot). It installs an **Update Emulators** entry into **Ports**.
4. Run **Update Emulators** → it sets itself up and opens the on-device manager (needs Wi-Fi the first time).

### B) One-liner (SSH)
SSH in (`root` / `panicos` by default) and run:
```sh
curl -fsSL https://raw.githubusercontent.com/seajaysec/panicos-emu/master/bootstrap.sh | bash
```
Add `| bash -s -- --all-cores` for every ROCKNIX core (full parity).

Either way: **restart EmulationStation** (Quit) or reboot to see the systems, then copy ROMs into
`/storage/roms/<system>/`. After setup, everything is done from the device — see below.

## 🕹️ On-device manager (the "Update Emulators" entry)

A fullscreen, gamepad-driven SDL2 app. From the main menu:
- **Manage emulators** — a per-system list showing **installed / partial / missing** cores and rom
  counts (green / yellow / red). Toggle the systems you want (**A**), or **Y** = all, **X** = all
  missing; **Start** applies — it fetches just the chosen targets and regenerates EmulationStation.
- **Update** — sync with ROCKNIX and re-apply your selection.
- **Install ALL cores** — full ROCKNIX parity.
- **Uninstall (keep ROMs)**.

Live progress streams on-screen. **D-pad** moves · **A** selects · **B/Select** back/quit.

<details><summary>Private fork? (deploy-key setup)</summary>

If you fork this **private**, the one-liner can't fetch it without auth. Either make your fork
public, or add a read-only deploy key and clone over SSH:
```sh
ssh-keygen -t ed25519 -N "" -f /storage/.ssh/id_ed25519_panicos_emu          # on the device
gh repo deploy-key add /path/to/id_ed25519_panicos_emu.pub -R <you>/panicos-emu -t panicos-device
git -c core.sshCommand="ssh -i /storage/.ssh/id_ed25519_panicos_emu -o IdentitiesOnly=yes" \
    clone git@github.com:<you>/panicos-emu.git /storage/.panicos-emu
git -C /storage/.panicos-emu config core.sshCommand "ssh -i /storage/.ssh/id_ed25519_panicos_emu -o IdentitiesOnly=yes"
bash /storage/.panicos-emu/bin/panicos-emu-install.sh
```
</details>

## 🎮 Controls & BIOS

- **Gamepad** is auto-configured from ROCKNIX's exact `H700 Gamepad` profile. In game: **hotkey (button 10)** + **START** = quit · **+ X** = RetroArch menu · **+ L1/R1** = save/load state.
- **BIOS** goes in `/storage/roms/bios/`. Most systems need none. Nintendo DS uses `bios7.bin`/`bios9.bin`/`firmware.bin` if present (else melonDS FreeBIOS); ColecoVision needs `colecovision.rom`; PC-Engine CD needs `syscard3.pce`.

## 🩺 Troubleshooting

| Symptom | Fix |
|--------|-----|
| New system not showing | Restart EmulationStation (Quit → it relaunches). es_systems changes need a reload. |
| Game black-screens | Set `video_driver` to `gl` then `sdl2` in `config/retroarch.cfg`, commit, re-run. |
| No sound | Audio must be `sdl2` (PanicOS routes via SDL→PipeWire to the codec; RA's native pipewire driver hits a muted sink). |
| A game crashes instantly | Check `/storage/emulators/retroarch/last-launch.log` — it records the core, ROM, env, and RetroArch's full output. |
| DS game won't boot | Add real DS BIOS to `/storage/roms/bios/`, or try the `desmume` core (HLE, no BIOS). |

## 🧹 Uninstall
```sh
bash bin/panicos-emu-uninstall.sh            # restores stock /etc, removes the sandbox
bash bin/panicos-emu-uninstall.sh --keep-roms # ...but keep your ROMs
```
Then restart EmulationStation. PanicOS / norns / USB / audio are untouched either way.

## 📦 Repo layout
```
rocknix.lock                   pinned ROCKNIX version (auto-bumped by the Action)
systems.conf                   declarative systems + cores
config/retroarch.cfg           PanicOS override layer (sdl2 audio @48k, glcore, sandbox paths)
                               -> applied via --appendconfig over ROCKNIX's pulled base config
config/retroarch.sh            sandbox launch wrapper (config layering + core selection + logging)
bin/panicos-emu-install.sh     installer / updater (idempotent, self-healing, --all-cores)
bin/panicos-emu-uninstall.sh   full revert
ports/Update Emulators.sh      on-device menu entry (git pull + install)
.github/workflows/sync-rocknix.yml   daily ROCKNIX tracker
```

## 🔬 Provenance & notes
Grafted from `ROCKNIX-H700.aarch64` — the same Allwinner H700 SoC and Mesa/Panfrost + Vulkan + PipeWire stack as PanicOS, so RetroArch and cores are built for exactly this hardware. RetroArch runs as a Wayland client of `panicos-sway` (the `glcore` driver auto-selects the Wayland context) and as an SDL audio stream into `CDC PCM Codec-0` at 48 kHz to match the pinned codec rate.

<div align="center">
<sub>Not affiliated with ROCKNIX, PanicOS, or libretro. Bring your own legally-obtained ROMs and BIOS.</sub>
</div>
