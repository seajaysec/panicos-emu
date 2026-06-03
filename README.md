# panicos-emu

Maintainable console emulation for **PanicOS** (the lean ROCKNIX-lineage norns/PanicTracker
appliance on the Anbernic RG35XX Pro / H700), grafted from **ROCKNIX** upstream and layered
on top **without modifying anything PanicOS owns**.

PanicOS deliberately ships no console emulators (it's a launcher/networking flavor — see the
comment in its `/etc/emulationstation/es_systems.cfg`). This repo adds RetroArch + cores in a
self-contained `/storage` sandbox and keeps it updatable against both ROCKNIX and PanicOS.

## Design: a base + override model

| Layer | Source | Edited? | Where it lands on device |
|-------|--------|---------|--------------------------|
| **Upstream** | ROCKNIX public releases (RetroArch binary, cores, libs, gamepad autoconfig) | never | `/storage/emulators/retroarch/{bin,lib,cores,autoconfig}` |
| **Override** | this repo (`config/`, `systems.conf`) — always wins | yes, by you | `/storage/emulators/retroarch/config`, `/storage/.emulationstation/es_systems.cfg` |
| **Content** | your ROMs | — | `/storage/roms/<system>/` |

### Hard invariant
The installer only ever writes under `/storage/emulators/retroarch`, `/storage/roms`, and
`/storage/.emulationstation/es_systems.cfg`. It **never** touches `/usr`, `/etc`, the audio
codec pin, norns, or ports — so it cannot conflict with a PanicOS deviation. The ES override is
regenerated each run as a **superset of PanicOS's live `/etc` systems** (so PanicOS's own
choices are inherited, never frozen), and it self-heals if a PanicOS update clobbers it.

## How updates flow

```
ROCKNIX release ──(daily GitHub Action, auto-commits)──▶ rocknix.lock on main
                                                              │
            you run "Update Emulators" on the device ◀────────┘ (manual; needs network)
                          │
                          ├─ git pull this repo
                          └─ bin/panicos-emu-install.sh
                                 ├─ fetch ROCKNIX SYSTEM for the locked version
                                 ├─ extract retroarch + cores in systems.conf + full lib closure
                                 ├─ re-render configs + es_systems (superset of live /etc)
                                 └─ self-test; preserve saves/states/bios/roms
```

- **ROCKNIX updates**: the Action bumps `rocknix.lock`; the device applies it next time you run the menu entry.
- **PanicOS updates**: handled by PanicOS itself; our layer lives in `/storage` and survives. If a PanicOS
  update ever replaces the ES override, the next `--render-only`/update run rebuilds it from the new `/etc`.
- Binaries are **never committed** — always pulled fresh from ROCKNIX, so the repo stays tiny and current.

## Files

```
rocknix.lock                  pinned ROCKNIX version + target (auto-bumped by the Action)
systems.conf                  declarative ES systems: name|fullname|core|exts|platform
config/retroarch.cfg          our RetroArch config (48kHz to match the codec pin, glcore)
config/retroarch.sh           sandbox launch wrapper
bin/panicos-emu-install.sh    installer/updater (idempotent, self-healing)
bin/panicos-emu-uninstall.sh  full revert
ports/Update Emulators.sh     on-device menu entry (git pull + install)
.github/workflows/sync-rocknix.yml   daily ROCKNIX tracker (auto-commit to main)
```

## Add a system / change a core
Edit `systems.conf` (one line), commit, then run **Update Emulators** on the device. Done.

## First-time device setup (one-time, over SSH)

The device pulls this private repo with a read-only **deploy key**:

```sh
# on the device (HOME=/storage), generate a key
ssh-keygen -t ed25519 -N "" -f /storage/.ssh/id_ed25519_panicos_emu

# add the PUBLIC key as a read-only Deploy Key (from a machine with gh):
gh repo deploy-key add /path/to/id_ed25519_panicos_emu.pub -R seajaysec/panicos-emu -t panicos-device

# clone on the device using that key
git -c core.sshCommand="ssh -i /storage/.ssh/id_ed25519_panicos_emu -o IdentitiesOnly=yes" \
    clone git@github.com:seajaysec/panicos-emu.git /storage/.panicos-emu
git -C /storage/.panicos-emu config core.sshCommand \
    "ssh -i /storage/.ssh/id_ed25519_panicos_emu -o IdentitiesOnly=yes"

# install + drop the Ports menu entry
bash /storage/.panicos-emu/bin/panicos-emu-install.sh
cp "/storage/.panicos-emu/ports/Update Emulators.sh" /storage/roms/ports/
```

Then restart EmulationStation. After this, updates are just the **Update Emulators** menu item.

## Controls (ROCKNIX `H700 Gamepad` autoconfig)
In game: **hotkey = button 10**. hotkey+START = quit, hotkey+X = RetroArch menu,
hotkey+L1/R1 = save/load state.

## Uninstall
`bash bin/panicos-emu-uninstall.sh` (add `--keep-roms` to keep your ROMs), then restart ES.

## Notes
- ColecoVision needs `colecovision.rom` in `/storage/roms/bios`; PC-Engine CD needs `syscard3.pce`.
- If a game black-screens, change `video_driver` to `gl` or `sdl2` in `config/retroarch.cfg`, commit, re-run.
- Provenance: grafted from `ROCKNIX-H700.aarch64` (same SoC/Panfrost+Vulkan stack as PanicOS).
