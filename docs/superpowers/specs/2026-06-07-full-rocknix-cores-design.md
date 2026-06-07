# Full ROCKNIX core coverage + "Full" Quick-Setup preset — design

**Date:** 2026-06-07
**Status:** approved-pending-review
**Author:** brainstormed with djhardrich

## 1. Problem & corrected premise

The ask started as "add the rest of the ROCKNIX-compiled RetroArch cores (like 2048) as an option."
On-device investigation (ROCKNIX 20260601, RG35XX Pro / H700) corrected the premise:

- **2048 is *not* a ROCKNIX core.** The image ships `2048_libretro.info` but **no `2048_libretro.so`**.
  The binary the user had came bundled inside a *PortMaster* port. `--all-cores` could never produce it.
- The 2048 port failed because PortMaster's launcher runs `$raloc/retroarch`, which on PanicOS
  (CFW unrecognized) defaults to `/usr/bin/retroarch` — and PanicOS ships no retroarch in its
  read-only `/usr`. That is a **base-image** gap, documented separately in `panicos.txt`
  (recommended `/usr/bin/retroarch` dispatcher shim). **Out of scope here.**
- ROCKNIX 20260601 ships **129 real core `.so` binaries** (vs. 293 `.info` stubs). `systems.conf`
  currently exposes ~24 across 13 systems. ~100 shipped binaries are unexposed.
- **Proven:** the panicos-emu sandbox `retroarch` + its lib closure resolve cleanly and can load
  cores (verified down to PortMaster's bundled 2048 core under the sandbox libs). So anything we
  expose through the existing wrapper **will actually boot** — no new launch mechanism is needed.

### Simplifying discovery
Every *game* core ROCKNIX ships takes **content files** (Doom→WAD, Quake→PAK, Pico-8→`.p8`,
TIC-80→`.tic`, WASM-4→`.wasm`, ScummVM→game files; the `supports_no_game=true` computer cores
also normally take disk images). There are **no content-less "the-core-is-the-game" cores** in
ROCKNIX's `.so` set to special-case. Therefore this feature is **content authoring + a preset**,
riding entirely on the existing install pipeline — not a launcher change.

## 2. Goals

1. Expose **every system ROCKNIX's 129 shipped cores cover** as EmulationStation systems, grouping
   multiple cores per system as per-game selectable alternates (the existing pattern).
2. Add a **"Full" preset**: one action that enables every system and grafts all their cores.
3. Surface "Full" as the **third state of the Quick Setup cores toggle**
   (`Recommended · default core` → `Recommended · all cores` → `Full · every system + core`).

### Non-goals
- Literal 2048 / PortMaster retroarch ports → handled by `panicos.txt` (base-image fix).
- Content-less "boot to menu" launch entries → no ROCKNIX `.so` needs it; YAGNI.
- Shipping BIOS/ROMs/content → user-supplied, as today.

## 3. Architecture — ride the existing pipeline

No new subsystem. Three touch points:

| Layer | File | Change |
|------|------|--------|
| Content | `systems.conf` | Add rows for all ROCKNIX-covered systems (the bulk of the work) |
| Installer | `bin/panicos-emu-install.sh` | Add `--full-setup` (select-all + all-cores graft + render) |
| GUI | `src/emu-manager.c` | Quick Setup cores toggle becomes 3-state; "Full" runs `--full-setup` |

**Why this is safe:** the installer already (a) skips cores not present in the image
(`warn … skipping`, non-fatal), (b) resolves the full lib closure for whatever cores land,
(c) generates `es_systems.cfg` only for systems with an installed core, (d) creates ROM folders,
(e) drives the manager's Library/status from `systems.conf` rows. So expanding `systems.conf` and
adding a select-all+all-cores preset composes with existing behavior; generosity is free.

## 4. `systems.conf` expansion (derived from on-image `.info` metadata)

Extensions and grouping are taken from each shipped core's `.info` `systemname` /
`supported_extensions`. Format unchanged: `name | fullname | cores | extensions | platform`.
First core = default. Existing 13 rows keep their identity, gaining alternates.

### 4.1 Console / handheld systems (expanded existing + new)
```
# name        | fullname                         | cores (default first)                                              | extensions
gb            | Game Boy / Color                 | gambatte sameboy gearboy tgbdual mgba vbam DoubleCherryGB           | gb gbc dmg sgb cgb
gba           | Game Boy Advance                 | mgba gpsp vbam beetle_gba vba_next                                 | gba agb bin
snes          | Super Nintendo                   | snes9x snes9x2010 snes9x2002 snes9x2005_plus beetle_supafaust mesen-s bsnes2014_balanced bsnes_mercury_balanced | sfc smc swc fig bs st
nes           | Nintendo Entertainment System    | fceumm nestopia mesen quicknes                                     | nes fds unf unif
n64           | Nintendo 64                      | mupen64plus_next parallel_n64                                     | n64 z64 v64 ndd
nds           | Nintendo DS                      | melonds melondsds desmume                                         | nds ids dsi
genesis       | Sega Genesis / Mega Drive        | genesis_plus_gx genesis_plus_gx_wide picodrive                    | md gen smd bin mdx 68k sgd
segacd        | Sega CD / Mega CD                | genesis_plus_gx picodrive                                         | cue iso chd m3u
sega32x       | Sega 32X                         | picodrive                                                        | 32x bin md smd
mastersystem  | Sega Master System               | genesis_plus_gx picodrive gearsystem smsplus                     | sms bin rom
gamegear      | Sega Game Gear                   | genesis_plus_gx gearsystem smsplus                               | gg
sg1000        | Sega SG-1000                     | genesis_plus_gx gearsystem smsplus bluemsx                       | sg sc
saturn        | Sega Saturn                      | yabasanshiro                                                     | bin cue iso chd mds ccd
dreamcast     | Sega Dreamcast                   | flycast flycast2021                                              | chd cdi gdi cue bin lst m3u
colecovision  | ColecoVision                     | gearcoleco bluemsx                                               | col cv bin rom
pcengine      | PC Engine / TurboGrafx-16        | beetle_pce_fast beetle_pce geargrafx                             | pce cue ccd chd toc m3u
supergrafx    | PC Engine SuperGrafx             | beetle_supergrafx beetle_pce geargrafx                           | sgx pce cue chd
pcfx          | PC-FX                            | beetle_pcfx                                                      | cue ccd toc chd
ngp           | Neo Geo Pocket / Color           | beetle_ngp race                                                  | ngp ngc ngpc npc
neogeo        | Neo Geo (AES/MVS)                | fbneo geolith                                                    | zip 7z neo
neogeocd      | Neo Geo CD                       | neocd                                                            | cue chd
wonderswan    | WonderSwan / Color               | beetle_wswan                                                     | ws wsc pc2 pcv2
psx           | Sony PlayStation                 | pcsx_rearmed swanstation duckstation                             | cue bin img chd pbp ecm mds m3u iso
psp           | Sony PSP                         | ppsspp                                                           | iso cso pbp elf prx chd
3do           | 3DO Interactive Multiplayer      | opera                                                            | iso bin chd cue
cdi           | Philips CD-i                     | same_cdi                                                         | chd iso cue
jaguar        | Atari Jaguar                     | virtualjaguar                                                    | j64 jag rom abs cof bin prg
lynx          | Atari Lynx                       | handy beetle_lynx                                                | lnx lyx o bll
atari2600     | Atari 2600                       | stella                                                           | a26 bin
atari5200     | Atari 5200                       | a5200                                                            | a52 bin
atari7800     | Atari 7800                       | prosystem                                                        | a78 bin cdf
vectrex       | Vectrex                          | vecx                                                             | vec bin
intellivision | Intellivision                    | freeintv                                                         | int bin rom
odyssey2      | Magnavox Odyssey 2 / Videopac    | o2em                                                             | bin
channelf      | Fairchild Channel F              | freechaf                                                         | bin chf
arduboy       | Arduboy                          | arduous                                                          | hex
virtualboy    | Virtual Boy                      | beetle_vb                                                        | vb vboy bin
pokemini      | Pokemon Mini                     | pokemini                                                         | min
gameandwatch  | Game & Watch                     | gw                                                               | mgw
supervision   | Watara Supervision               | potator                                                          | sv bin
megaduck      | Mega Duck                        | sameduck                                                         | bin duck
scv           | Epoch Super Cassette Vision      | emuscv                                                           | bin cart rom
```

### 4.2 Arcade & laserdisc
```
arcade        | Arcade (MAME / FBNeo)            | mame2003_plus fbneo mame2010 mame2015 fbalpha2019 fbalpha2012 mame | zip 7z cue ccd
daphne        | Laserdisc (Daphne)               | daphne                                                           | zip cue m3u
```

### 4.3 Home computers
```
c64           | Commodore 64                     | vice_x64                                                         | d64 t64 prg crt tap g64 m3u zip
c128          | Commodore 128                    | vice_x128                                                        | d64 t64 prg crt tap g64 m3u zip
vic20         | Commodore VIC-20                 | vice_xvic                                                        | d64 t64 prg crt tap 20 40 60 a0 b0 rom m3u
plus4         | Commodore Plus/4                 | vice_xplus4                                                      | d64 t64 prg crt tap g64 m3u zip
pet           | Commodore PET                    | vice_xpet                                                        | d64 t64 prg crt tap g64 m3u zip
amiga         | Commodore Amiga                  | puae puae2021 uae4arm                                            | adf adz dms ipf hdf hdz lha uae m3u zip 7z chd
msx           | MSX / MSX2                       | bluemsx fmsx                                                     | rom mx1 mx2 dsk cas m3u
amstradcpc    | Amstrad CPC                      | cap32 crocods                                                   | dsk sna tap cdt kcr m3u zip
zxspectrum    | ZX Spectrum                      | fuse                                                            | tzx tap z80 rzx scl trd dsk sna szx zip
zx81          | Sinclair ZX81                    | 81                                                             | p tzx t81
x68000        | Sharp X68000                     | px68k                                                          | dim img d88 hdf m3u
x1            | Sharp X1                         | x1                                                             | dx1 2d 2hd d88 tap cmd zip
pc88          | NEC PC-8800                      | quasi88                                                        | d88 u88 m3u
pc98          | NEC PC-98                        | np2kai                                                         | d98 fdi fdd hdi hdm xdf 88d zip
dos           | MS-DOS                           | dosbox_pure dosbox_core                                        | zip dosz exe com bat iso chd cue m3u conf
macintosh     | Apple Macintosh (68k)            | minivmac                                                       | dsk img hvf zip
palm          | Palm OS                          | mu                                                             | prc pqa pdb img zip
bbcmicro      | Acorn BBC Micro                  | b2                                                             | ssd dsd
thomson       | Thomson MO / TO                  | theodore                                                       | fd sap k7 m7 m5 rom
bk            | Elektronika BK-0010/11           | bk                                                             | bin
```

### 4.4 Game engines & fantasy consoles
```
doom          | DOOM (PrBoom)                    | prboom                                                         | wad iwad pwad
quake         | Quake (TyrQuake)                 | tyrquake                                                       | pak
quake2        | Quake II (vitaQuake2)            | vitaquake2 vitaquake2-rogue vitaquake2-xatrix vitaquake2-zaero | pak
wolf3d        | Wolfenstein 3D (ECWolf)          | ecwolf                                                         | wl6 wl1 sod sdm n3d pk3
scummvm       | ScummVM                          | scummvm                                                        | scummvm svm
pico8         | PICO-8 (fake-08)                 | fake08                                                         | p8 png
tic80         | TIC-80                           | tic80                                                          | tic
wasm4         | WASM-4                           | wasm4                                                          | wasm
easyrpg       | RPG Maker 2000/2003 (EasyRPG)    | easyrpg                                                       | ldb zip lzh easyrpg
uzebox        | Uzebox                           | uzem                                                          | uze
vircon32      | Vircon32                         | vircon32                                                      | v32
chip8         | CHIP-8                           | jaxe                                                          | ch8 sc8 xo8
j2me          | Java ME (J2ME)                   | freej2me                                                      | jar
zmachine      | Z-Machine (interactive fiction)  | mojozork                                                      | dat z1 z3 z5 z8
```

### 4.5 Edge cases & decisions
- **`81` core / `zx81`:** the ROCKNIX core basename is literally `81` (→ `81_libretro.so`). Used verbatim.
- **Empty/odd `.info`:** `gearlynx`, `mupen64plus` (legacy), `skyemu` (multi gb/gba/nds) ship a `.so`
  but have empty or multi-system `.info`. They are **not listed as primary cores**; `mupen64plus_next`/
  `parallel_n64`, `handy`/`beetle_lynx`, `mgba` cover those systems. (They still get grafted by
  `--all-cores`; they're simply not surfaced as ES alternates. Acceptable — generosity is free.)
- **ScummVM extensions:** the core's `.info` advertises a garbage 1KB extension list; we use a sane
  subset (`scummvm svm`) — the conventional `.scummvm` launcher-file approach.
- **`platform`/theme key** = the `name` column in all cases above (matches existing convention:
  `<platform><theme>` both point at `name`). Systems whose theme key the active PanicOS theme lacks
  fall back gracefully in ES (cosmetic only).
- **`fbneo`** intentionally appears under both `arcade` and `neogeo` (it handles both); duplicates
  across systems are fine — the installer dedups core *files*, not rows.
- **Theme keys that differ from `name`** (if any are needed for art) are deferred to implementation;
  default is `name == platform == theme`.

## 5. Installer change — `--full-setup`

Add a single new mode mirroring the existing `--quick-setup`/`--all-cores` plumbing:

- New flag `--full-setup` sets `FULL=1`.
- When `FULL=1`:
  1. `full_setup()`: write `$SELECTION` with **every** `conf_rows` system name (parallel to
     `quick_setup()` which writes the recommended subset).
  2. Set `ALL_CORES=1` and `FORCE=1` (so the all-cores graft path runs and the choice applies even
     on an already-set-up device — same rationale as `--quick-setup`).
  3. Proceed through the normal full-graft → `render_configs` flow.
- `-h/--help` text + the header comment block updated to mention `--full-setup`.

No change to `graft`, `render_configs`, `gen_es_systems`, status, or per-core management — they
already handle "all systems enabled, all cores present."

## 6. GUI change — 3-state Quick Setup toggle

In `src/emu-manager.c`:
- Replace the boolean `qs_allcores` with a 3-state `qs_mode ∈ {0,1,2}`:
  - `0` = Recommended · default core only → `SYNC "--quick-setup --defaults"`
  - `1` = Recommended · all cores per system → `SYNC "--quick-setup"`
  - `2` = Full · every system + every core → `SYNC "--full-setup"`
- `render_qsetup()` row 0 label/sub-label cycles through the three modes; when mode `2` is selected,
  show the heavier warning (e.g. "~all ROCKNIX cores · large download · many systems need BIOS/content").
- `on_qsetup()` case 0 cycles `qs_mode = (qs_mode+1)%3`; case 1 (Install) dispatches by `qs_mode`.
- The Settings "Install ALL cores (parity)" entry stays (it's the cores-only, no-system-enable
  power action); Full is the user-facing "give me everything" path.

## 7. Testing

- Extend `tests/engine-test.sh` to assert, against a mounted/extracted ROCKNIX image (or a fixture):
  - every core named in `systems.conf` either exists in the image **or** is tolerated as skipped;
  - `--full-setup` (dry/`PE_*`-overridden run) enables all systems and renders an `es_systems.cfg`
    whose system count == number of `systems.conf` rows with ≥1 installed core;
  - generated `<extension>` lists are non-empty for every rendered system.
- CI (`.github/workflows/ci.yml`) continues to compile `emu-manager` and run the engine test.
- Manual device check: `--full-setup` on the RG35XX Pro, then boot at least one core per category
  (a console, an arcade ROM, a Doom WAD) to confirm the sandbox launch path works end-to-end.

## 8. Risks & mitigations

- **Download/disk size:** all-cores graft + full lib closure is large. Mitigation: cached image
  (already), explicit GUI warning on the Full state, and it's opt-in.
- **ES system sprawl (~80 systems):** expected and intended for "Full". Recommended preset remains
  the lean default; quick-setup default core path is untouched.
- **Empty systems without BIOS/content:** show no games (harmless); documented in README.
- **Theme gaps:** cosmetic fallback only.

## 9. Documentation

- `README.md`: bump "13 console systems" → full count; document the 3-state Quick Setup and the Full
  preset; note BIOS/content are user-supplied.
- `systems.conf` header comment: mention `--full-setup` and the broadened coverage.
- `panicos.txt`: already written (base-image PortMaster fix; cross-referenced, separate concern).
```

