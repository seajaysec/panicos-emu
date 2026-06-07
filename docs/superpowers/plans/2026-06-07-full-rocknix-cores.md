# Full ROCKNIX Core Coverage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose every ROCKNIX-shipped libretro core (~129 binaries, ~80 EmulationStation systems) as a manageable/playable system, surfaced via a new "Full" third state of the manager's Quick Setup toggle.

**Architecture:** No new subsystem — ride the existing install pipeline. Expand `systems.conf` (the single source of truth that already drives status, Library, ES-config generation, ROM folders, and per-core management); add a `--full-setup` installer mode that selects all systems and grafts all cores (reusing the `--all-cores` and selection plumbing); turn the GUI's boolean cores toggle into a 3-state preset. Cores absent from the image are already skipped non-fatally, so broad coverage is safe.

**Tech Stack:** Bash (installer + tests), C/SDL2 (`emu-manager` GUI, cross-compiled to aarch64 via Docker), GitHub Actions CI (`bash -n`, shellcheck, `tests/engine-test.sh`, aarch64 GUI compile).

**Spec:** `docs/superpowers/specs/2026-06-07-full-rocknix-cores-design.md`

---

## File Structure

| File | Responsibility | Action |
|------|----------------|--------|
| `tests/fixtures/rocknix-cores.txt` | Authoritative list of core basenames ROCKNIX 20260601 ships; lets CI validate `systems.conf` core names offline | Create |
| `systems.conf` | Declarative system→cores→extensions→platform map | Modify (expand to 80 rows) |
| `tests/engine-test.sh` | Sandbox verb-logic + config tests | Modify (add systems.conf integrity + `--full-setup` tests) |
| `bin/panicos-emu-install.sh` | Installer/engine | Modify (add `--full-setup`) |
| `src/emu-manager.c` | SDL2 manager GUI | Modify (3-state Quick Setup toggle) |
| `bin/emu-manager` | Prebuilt aarch64 GUI binary the device runs | Rebuild + commit |
| `README.md` | User-facing docs | Modify (system count, Full preset) |

---

## Task 1: Core fixture + `systems.conf` integrity test (failing first)

**Files:**
- Create: `tests/fixtures/rocknix-cores.txt`
- Modify: `tests/engine-test.sh` (insert before final `exit $FAIL`, line 116)

- [ ] **Step 1: Create the core fixture** (the exact 129 basenames ROCKNIX 20260601 ships, `LC_ALL=C` sorted)

Create `tests/fixtures/rocknix-cores.txt`:

```
81
DoubleCherryGB
a5200
arduous
atari800
b2
beetle_gba
beetle_lynx
beetle_ngp
beetle_pce
beetle_pce_fast
beetle_pcfx
beetle_supafaust
beetle_supergrafx
beetle_vb
beetle_wswan
bk
bluemsx
bsnes2014_accuracy
bsnes2014_balanced
bsnes2014_performance
bsnes_mercury_accuracy
bsnes_mercury_balanced
bsnes_mercury_performance
cap32
crocods
daphne
desmume
dosbox_core
dosbox_pure
duckstation
easyrpg
ecwolf
emuscv
fake08
fbalpha2012
fbalpha2019
fbneo
fceumm
flycast
flycast2021
fmsx
freechaf
freeintv
freej2me
fuse
gambatte
gearboy
gearcoleco
geargrafx
gearlynx
gearsystem
genesis_plus_gx
genesis_plus_gx_wide
geolith
gpsp
gw
handy
hatari
jaxe
mame
mame2003_plus
mame2010
mame2015
melonds
melondsds
mesen
mesen-s
mgba
minivmac
mojozork
mu
mupen64plus
mupen64plus_next
neocd
nestopia
np2kai
o2em
opera
parallel_n64
pcsx_rearmed
pcsx_rearmed32
picodrive
pokemini
potator
ppsspp
prboom
prosystem
puae
puae2021
px68k
quasi88
quicknes
race
same_cdi
sameboy
sameduck
scummvm
skyemu
smsplus
snes9x
snes9x2002
snes9x2005_plus
snes9x2010
stella
swanstation
tgbdual
theodore
tic80
tyrquake
uae4arm
uzem
vba_next
vbam
vecx
vice_x128
vice_x64
vice_xpet
vice_xplus4
vice_xvic
vircon32
virtualjaguar
vitaquake2
vitaquake2-rogue
vitaquake2-xatrix
vitaquake2-zaero
wasm4
x1
yabasanshiro
```

- [ ] **Step 2: Add the integrity tests** to `tests/engine-test.sh`, immediately before line 116 (`exit $FAIL`):

```bash
# ---- systems.conf integrity (Full ROCKNIX coverage) ----
CORES_FIXTURE="$HERE/tests/fixtures/rocknix-cores.txt"
conf_rows_all(){ grep -vE '^[[:space:]]*#|^[[:space:]]*$' "$HERE/systems.conf"; }

t "fixture exists" "[ -s '$CORES_FIXTURE' ]"
t "every systems.conf row has exactly 5 pipe-fields" \
  "[ -z \"\$(conf_rows_all | awk -F'|' 'NF!=5{print NR}')\" ]"
t "every row has non-empty name and extensions" \
  "[ -z \"\$(conf_rows_all | awk -F'|' '{n=\$1;e=\$4;gsub(/[[:space:]]/,\"\",n);gsub(/^[ \t]+|[ \t]+\$/,\"\",e);if(n==\"\"||e==\"\")print NR}')\" ]"
t "system names are unique" \
  "[ \"\$(conf_rows_all | awk -F'|' '{n=\$1;gsub(/[[:space:]]/,\"\",n);print n}' | sort | uniq -d | wc -l | tr -d ' ')\" = 0 ]"

# every core referenced by systems.conf must be a real ROCKNIX core (catches typos / case)
UNKNOWN_CORES="$(conf_rows_all | awk -F'|' '{print $3}' | tr ' ' '\n' | sed '/^$/d' \
                 | sort -u | comm -23 - <(sort -u "$CORES_FIXTURE"))"
t "every core in systems.conf is a real ROCKNIX core" "[ -z \"\$UNKNOWN_CORES\" ]"

# Full coverage must include representatives from each new family
for s in arcade neogeo psx psp dreamcast saturn 3do c64 amiga msx zxspectrum zx81 \
         dos scummvm doom quake wolf3d pico8 tic80 wasm4 atari2600 lynx vectrex; do
  t "systems.conf includes $s" \
    "conf_rows_all | awk -F'|' '{n=\$1;gsub(/[[:space:]]/,\"\",n);print n}' | grep -qx $s"
done
```

- [ ] **Step 3: Run the tests — expect FAIL** (fixture passes/structure passes on the old 13-row file, but the `includes <system>` assertions fail because those rows don't exist yet)

Run: `bash tests/engine-test.sh`
Expected: several `FAIL: systems.conf includes arcade` (etc.) lines; overall non-zero exit.

- [ ] **Step 4: Replace `systems.conf`** with the full-coverage map. Overwrite the whole file with:

```
# panicos-emu — declarative EmulationStation systems.
# One line per system.  Fields separated by '|':
#   name | fullname | cores | extensions | platform
#
# - name      : ES system id AND the rom folder name under /storage/roms/<name>/
# - cores     : ONE OR MORE libretro core basenames, space-separated. The FIRST is the
#               default; the rest become alternates you can pick per-game in ES
#               (Game Options -> Core). Cores ROCKNIX doesn't ship are skipped automatically.
# - extensions: lowercase, space-separated. Installer adds UPPERCASE variants + .zip.
# - platform  : ES platform + theme key (box art / theming).
#
# Add a system or an alternate core: edit a line, commit, run "Update Emulators" on device.
# Quick Setup offers: Recommended (default core), Recommended (all cores), or Full
# (every system + every core ROCKNIX ships). Full == the installer's --full-setup.
#
# name         | fullname                       | cores (default first)                                                                                  | extensions                                  | platform
gb             | Game Boy / Color               | gambatte sameboy gearboy tgbdual mgba vbam DoubleCherryGB                                               | gb gbc dmg sgb cgb                          | gb
gba            | Game Boy Advance               | mgba gpsp vbam beetle_gba vba_next                                                                      | gba agb bin                                 | gba
snes           | Super Nintendo                 | snes9x snes9x2010 snes9x2002 snes9x2005_plus beetle_supafaust mesen-s bsnes2014_balanced bsnes_mercury_balanced | sfc smc swc fig bs st                | snes
nes            | Nintendo Entertainment System  | fceumm nestopia mesen quicknes                                                                         | nes fds unf unif                            | nes
n64            | Nintendo 64                    | mupen64plus_next parallel_n64                                                                          | n64 z64 v64 ndd                             | n64
nds            | Nintendo DS                    | melonds melondsds desmume                                                                              | nds ids dsi                                 | nds
genesis        | Sega Genesis / Mega Drive      | genesis_plus_gx genesis_plus_gx_wide picodrive                                                         | md gen smd bin mdx 68k sgd                  | genesis
segacd         | Sega CD / Mega CD              | genesis_plus_gx picodrive                                                                              | cue iso chd m3u                             | segacd
sega32x        | Sega 32X                       | picodrive                                                                                             | 32x bin md smd                              | sega32x
mastersystem   | Sega Master System             | genesis_plus_gx picodrive gearsystem smsplus                                                           | sms bin rom                                 | mastersystem
gamegear       | Sega Game Gear                 | genesis_plus_gx gearsystem smsplus                                                                     | gg                                          | gamegear
sg1000         | Sega SG-1000                   | genesis_plus_gx gearsystem smsplus bluemsx                                                             | sg sc                                       | sg-1000
saturn         | Sega Saturn                    | yabasanshiro                                                                                          | bin cue iso chd mds ccd                     | saturn
dreamcast      | Sega Dreamcast                 | flycast flycast2021                                                                                    | chd cdi gdi cue bin lst m3u                 | dreamcast
colecovision   | ColecoVision                   | gearcoleco bluemsx                                                                                     | col cv bin rom                              | colecovision
pcengine       | PC Engine / TurboGrafx-16      | beetle_pce_fast beetle_pce geargrafx                                                                   | pce cue ccd chd toc m3u                     | pcengine
supergrafx     | PC Engine SuperGrafx           | beetle_supergrafx beetle_pce geargrafx                                                                 | sgx pce cue chd                             | supergrafx
pcfx           | PC-FX                          | beetle_pcfx                                                                                           | cue ccd toc chd                             | pcfx
ngp            | Neo Geo Pocket / Color         | beetle_ngp race                                                                                       | ngp ngc ngpc npc                            | ngp
neogeo         | Neo Geo (AES/MVS)              | fbneo geolith                                                                                         | neo 7z                                      | neogeo
neogeocd       | Neo Geo CD                     | neocd                                                                                                 | cue chd                                     | neogeocd
wonderswan     | WonderSwan / Color             | beetle_wswan                                                                                          | ws wsc pc2 pcv2                             | wonderswan
psx            | Sony PlayStation               | pcsx_rearmed swanstation duckstation                                                                   | cue bin img chd pbp ecm mds m3u iso         | psx
psp            | Sony PSP                       | ppsspp                                                                                                | iso cso pbp elf prx chd                     | psp
3do            | 3DO Interactive Multiplayer    | opera                                                                                                | iso bin chd cue                             | 3do
cdi            | Philips CD-i                   | same_cdi                                                                                             | chd iso cue                                 | cdi
jaguar         | Atari Jaguar                   | virtualjaguar                                                                                        | j64 jag rom abs cof bin prg                 | atarijaguar
lynx           | Atari Lynx                     | handy beetle_lynx                                                                                     | lnx lyx o bll                               | atarilynx
atari2600      | Atari 2600                     | stella                                                                                               | a26 bin                                     | atari2600
atari5200      | Atari 5200                     | a5200                                                                                                | a52 bin                                     | atari5200
atari7800      | Atari 7800                     | prosystem                                                                                            | a78 bin cdf                                 | atari7800
vectrex        | Vectrex                        | vecx                                                                                                 | vec bin                                     | vectrex
intellivision  | Intellivision                  | freeintv                                                                                             | int bin rom                                 | intellivision
odyssey2       | Magnavox Odyssey 2 / Videopac  | o2em                                                                                                 | bin                                         | odyssey2
channelf       | Fairchild Channel F            | freechaf                                                                                             | bin chf                                     | channelf
arduboy        | Arduboy                        | arduous                                                                                              | hex                                         | arduboy
virtualboy     | Virtual Boy                    | beetle_vb                                                                                            | vb vboy bin                                 | virtualboy
pokemini       | Pokemon Mini                   | pokemini                                                                                             | min                                         | pokemini
gameandwatch   | Game & Watch                   | gw                                                                                                  | mgw                                         | gameandwatch
supervision    | Watara Supervision             | potator                                                                                             | sv bin                                      | supervision
megaduck       | Mega Duck                      | sameduck                                                                                            | bin duck                                    | megaduck
scv            | Epoch Super Cassette Vision    | emuscv                                                                                               | bin cart rom                                | scv
arcade         | Arcade (MAME / FBNeo)          | mame2003_plus fbneo mame2010 mame2015 fbalpha2019 fbalpha2012 mame                                     | 7z cue ccd                                  | arcade
daphne         | Laserdisc (Daphne)             | daphne                                                                                               | cue m3u                                     | daphne
c64            | Commodore 64                   | vice_x64                                                                                             | d64 t64 prg crt tap g64 m3u                 | c64
c128           | Commodore 128                  | vice_x128                                                                                            | d64 t64 prg crt tap g64 m3u                 | c128
vic20          | Commodore VIC-20               | vice_xvic                                                                                            | d64 t64 prg crt tap a0 b0 rom m3u           | vic20
plus4          | Commodore Plus/4               | vice_xplus4                                                                                          | d64 t64 prg crt tap g64 m3u                 | plus4
pet            | Commodore PET                  | vice_xpet                                                                                            | d64 t64 prg crt tap g64 m3u                 | pet
amiga          | Commodore Amiga                | puae puae2021 uae4arm                                                                                 | adf adz dms ipf hdf hdz lha uae m3u 7z chd  | amiga
msx            | MSX / MSX2                     | bluemsx fmsx                                                                                          | rom mx1 mx2 dsk cas m3u                     | msx
amstradcpc     | Amstrad CPC                    | cap32 crocods                                                                                        | dsk sna tap cdt kcr m3u                     | amstradcpc
zxspectrum     | ZX Spectrum                    | fuse                                                                                                 | tzx tap z80 rzx scl trd dsk sna szx         | zxspectrum
zx81           | Sinclair ZX81                  | 81                                                                                                  | p tzx t81                                   | zx81
x68000         | Sharp X68000                   | px68k                                                                                               | dim img d88 hdf m3u                         | x68000
x1             | Sharp X1                       | x1                                                                                                  | dx1 2d 2hd d88 tap cmd                      | x1
pc88           | NEC PC-8800                    | quasi88                                                                                             | d88 u88 m3u                                 | pc88
pc98           | NEC PC-98                      | np2kai                                                                                              | d98 fdi fdd hdi hdm xdf 88d                 | pc98
dos            | MS-DOS                         | dosbox_pure dosbox_core                                                                              | dosz exe com bat iso chd cue m3u conf       | dos
macintosh      | Apple Macintosh (68k)          | minivmac                                                                                            | dsk img hvf                                 | macintosh
palm           | Palm OS                        | mu                                                                                                  | prc pqa pdb img                             | palm
bbcmicro       | Acorn BBC Micro                | b2                                                                                                  | ssd dsd                                     | bbcmicro
thomson        | Thomson MO / TO                | theodore                                                                                            | fd sap k7 m7 m5 rom                         | thomson
bk             | Elektronika BK-0010/11         | bk                                                                                                  | bin                                         | bk
doom           | DOOM (PrBoom)                  | prboom                                                                                              | wad iwad pwad                               | doom
quake          | Quake (TyrQuake)               | tyrquake                                                                                            | pak                                         | quake
quake2         | Quake II (vitaQuake2)          | vitaquake2 vitaquake2-rogue vitaquake2-xatrix vitaquake2-zaero                                        | pak                                         | quake
wolf3d         | Wolfenstein 3D (ECWolf)        | ecwolf                                                                                              | wl6 wl1 sod sdm n3d pk3                     | wolf3d
scummvm        | ScummVM                        | scummvm                                                                                             | scummvm svm                                 | scummvm
pico8          | PICO-8 (fake-08)               | fake08                                                                                              | p8 png                                      | pico8
tic80          | TIC-80                         | tic80                                                                                               | tic                                         | tic80
wasm4          | WASM-4                         | wasm4                                                                                               | wasm                                        | wasm4
easyrpg        | RPG Maker 2000/2003 (EasyRPG)  | easyrpg                                                                                             | ldb lzh easyrpg                             | easyrpg
uzebox         | Uzebox                         | uzem                                                                                                | uze                                         | uzebox
vircon32       | Vircon32                       | vircon32                                                                                            | v32                                         | vircon32
chip8          | CHIP-8                         | jaxe                                                                                                | ch8 sc8 xo8                                 | chip8
j2me           | Java ME (J2ME)                 | freej2me                                                                                            | jar                                         | j2me
zmachine       | Z-Machine (interactive fiction)| mojozork                                                                                            | dat z1 z3 z5 z8                             | zmachine
```

- [ ] **Step 5: Run the tests — expect PASS**

Run: `bash tests/engine-test.sh`
Expected: all `ok:` lines (including every `systems.conf includes <system>`), exit 0.
If `every core in systems.conf is a real ROCKNIX core` FAILs, run
`bash -c 'set -e; H=.; grep -vE "^[[:space:]]*#|^[[:space:]]*$" systems.conf | awk -F"|" "{print \$3}" | tr " " "\n" | sed "/^\$/d" | sort -u | comm -23 - <(sort -u tests/fixtures/rocknix-cores.txt)'`
to print the offending core name and fix the typo.

- [ ] **Step 6: Commit**

```bash
git add tests/fixtures/rocknix-cores.txt tests/engine-test.sh systems.conf
git commit -m "feat: expand systems.conf to full ROCKNIX coverage (~70 systems)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Installer `--full-setup` mode

**Files:**
- Modify: `bin/panicos-emu-install.sh` (arg parsing ~line 60-83; new function near `quick_setup` ~line 408-418; `main()` ~line 437)
- Modify: `tests/engine-test.sh` (add a `--full-setup` test block before `exit $FAIL`)

- [ ] **Step 1: Write the failing test** — add to `tests/engine-test.sh` before `exit $FAIL`:

```bash
setup
bash "$HERE/bin/panicos-emu-install.sh" --full-setup --no-graft >/dev/null 2>&1
t "full-setup writes enabled-systems"        "[ -s '$PE_PREFIX/.enabled-systems' ]"
t "full-setup enables EVERY systems.conf row" \
  "[ \"\$(grep -vE '^[[:space:]]*#|^[[:space:]]*\$' '$HERE/systems.conf' | wc -l | tr -d ' ')\" = \"\$(sort -u '$PE_PREFIX/.enabled-systems' | grep -c .)\" ]"
t "full-setup includes nds (not excluded like quick-setup)" "grep -qx nds '$PE_PREFIX/.enabled-systems'"
t "full-setup includes arcade" "grep -qx arcade '$PE_PREFIX/.enabled-systems'"
teardown
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash tests/engine-test.sh`
Expected: FAIL — `--full-setup` is an unknown arg, the script `die`s, so `.enabled-systems` is never written (`full-setup writes enabled-systems` FAILs).

- [ ] **Step 3: Add the `--full-setup` flag.** In the arg-parse `for a in "$@"` loop (after the `--quick-setup) QUICK=1 ;;` line), add:

```bash
  --full-setup) FULL=1 ;;
```

And add `FULL=0` to the variable-initialization line (the one that sets `FORCE=0; RENDER_ONLY=0; ALL_CORES=0; STATUS=0; QUICK=0; ...`):

```bash
FORCE=0; RENDER_ONLY=0; ALL_CORES=0; STATUS=0; QUICK=0; NOGRAFT=0; CHECK=0; UPDATE=0; DEFAULTS_ONLY=0; FULL=0
```

- [ ] **Step 4: Add the `full_setup()` function** immediately after the `quick_setup()` function (after its closing `}`):

```bash
# ---- enable EVERY system + flag all-cores graft (the "Full" preset) ---------
full_setup(){
  mkdir -p "$PREFIX"
  conf_rows | awk -F'|' '{n=$1; gsub(/[[:space:]]/,"",n); if(n!="") print n}' > "$SELECTION"
  ALL_CORES=1
  log "full setup: $(grep -c . "$SELECTION" | tr -d ' ') systems selected (all cores)"
}
```

- [ ] **Step 5: Wire it into `main()`.** Find the Quick-Setup line in `main()`:

```bash
  if [ "$QUICK" = 1 ]; then quick_setup; FORCE=1; fi
```

and add directly beneath it:

```bash
  if [ "$FULL" = 1 ]; then full_setup; FORCE=1; fi
```

- [ ] **Step 6: Update the help/usage text.** In the header comment block (lines ~22-27) add a line under the `--all-cores` usage:

```bash
#   panicos-emu-install.sh --full-setup # enable EVERY system + graft all cores (the "Full" preset)
```

- [ ] **Step 7: Run test to verify it passes**

Run: `bash tests/engine-test.sh`
Expected: all `ok:`, exit 0.

- [ ] **Step 8: Syntax check (matches CI)**

Run: `bash -n bin/panicos-emu-install.sh && shellcheck -S warning bin/panicos-emu-install.sh || true`
Expected: no syntax error from `bash -n`.

- [ ] **Step 9: Commit**

```bash
git add bin/panicos-emu-install.sh tests/engine-test.sh
git commit -m "feat: --full-setup installer mode (enable all systems + all cores)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: GUI 3-state Quick Setup toggle

**Files:**
- Modify: `src/emu-manager.c` (state var ~line 227; `render_qsetup` ~line 479-499; `on_qsetup` ~line 602-616)
- Rebuild + commit: `bin/emu-manager`

No automated unit test (SDL2 GUI); verification is a clean aarch64 compile (same as CI) plus a manual device check.

- [ ] **Step 1: Replace the toggle state variable.** Change line ~227 from:

```c
/* quick setup */
static int qs_allcores=0;          /* 0 = default core per system, 1 = all cores */
```

to:

```c
/* quick setup: 0 = recommended/default core, 1 = recommended/all cores, 2 = Full (every system+core) */
static int qs_mode=0;
#define NQSMODE 3
```

- [ ] **Step 2: Rewrite the toggle row + descriptive lines in `render_qsetup()`.** Replace the body from the `draw_text(30,66,...)` "Installs the recommended emulator set:" line through the end of the row-0 toggle block (the `tog`/`snprintf` block, ~lines 481-491) with:

```c
    static const char *QS_MODE_LBL[3]={
        "Recommended - default core", "Recommended - all cores", "Full - every system + core" };
    static const char *QS_MODE_SUB[3]={
        "one core per recommended system",
        "every core each recommended system lists",
        "ALL ~129 ROCKNIX cores, every system enabled" };
    if(qs_mode==2){
        draw_text(30,66,1,"Installs EVERY system & core ROCKNIX ships:",180,185,200);
        draw_text(36,88,1,"consoles + arcade + computers + DOS + game engines",200,205,215);
        draw_text(30,148,1,"Includes Nintendo DS and all niche systems.",170,150,150);
        draw_text(30,168,1,"Large download - many systems need BIOS/content you add.",200,200,150);
    } else {
        draw_text(30,66,1,"Installs the recommended emulator set:",180,185,200);
        draw_text(36,88,1,QS_CHIPS,200,205,215);
        draw_text(30,148,1,"Nintendo DS is excluded (too slow on this handheld).",170,150,150);
        draw_text(30,168,1,"~1.5 GB download - a few minutes - needs Wi-Fi.",200,200,150);
    }

    /* row 0: cores/preset toggle */
    int y0=206;
    if(hsel==0) fill(24,y0-8,SCREEN_W-48,42,36,78,140);
    char tog[64]; snprintf(tog,sizeof tog,"Mode: %s", QS_MODE_LBL[qs_mode]);
    draw_text(44,y0,2,tog,210,215,160);
    draw_text(44,y0+22,1, QS_MODE_SUB[qs_mode],150,155,170);
```

(Delete the original `draw_text(30,66,...)`, `draw_text(36,88,...)`, `draw_text(30,148,...)`, `draw_text(30,168,...)` lines and the original row-0 block they precede, since they are now inside the if/else above. Keep everything from the `static const char*acts[]` line onward unchanged.)

- [ ] **Step 3: Update `on_qsetup()`** — replace case 0 and case 1 (~lines 607-611):

```c
            case 0: qs_mode=(qs_mode+1)%NQSMODE; break;   /* cycle preset */
            case 1:
                if(qs_mode==2)      cmd_start(SYNC "--full-setup",ST_HOME);
                else if(qs_mode==1) cmd_start(SYNC "--quick-setup",ST_HOME);
                else                cmd_start(SYNC "--quick-setup --defaults",ST_HOME);
                screen=ST_PROGRESS; break;
```

(Cases 2 "Customize" and 3 "Cancel" unchanged.)

- [ ] **Step 4: Compile-check (aarch64, same as CI).** Requires Docker.

Run:
```bash
docker run --rm --platform linux/arm64 -v "$PWD:/build" -w /build debian:bookworm-slim sh -c '
  apt-get update -qq && apt-get install -y -qq gcc libsdl2-dev pkg-config
  gcc -O2 -Wall src/emu-manager.c -o /tmp/emu-manager $(pkg-config --cflags --libs sdl2) -lm
  test -x /tmp/emu-manager && echo "compiled OK"'
```
Expected: `compiled OK`, no warnings about `qs_allcores` (all references replaced).

- [ ] **Step 5: Rebuild the committed device binary.**

Run: `bash scripts/build-emu-manager.sh`
Expected: produces `bin/emu-manager`; final `file bin/emu-manager` reports `ELF 64-bit LSB ... ARM aarch64`.

- [ ] **Step 6: Commit**

```bash
git add src/emu-manager.c bin/emu-manager
git commit -m "feat: 3-state Quick Setup toggle (Recommended default/all, Full)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Documentation

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update the system count + feature bullet.** In `README.md`, find the line beginning `- **13 console systems** available` and replace that bullet with:

```markdown
- **~70 systems available** — from the recommended console set (Game Boy/Color, NES, SNES, GBA, Genesis, Master System, Game Gear, ColecoVision, Neo Geo Pocket, PC Engine, WonderSwan, N64, Nintendo DS) up to full ROCKNIX parity: arcade (MAME/FBNeo/Neo Geo), PlayStation, PSP, Dreamcast, Saturn, 3DO, every Atari/handheld, home computers (C64, Amiga, MSX, CPC, ZX, X68000, PC-98), DOS, ScummVM, and game engines (Doom, Quake 1/2, Wolf3D, PICO-8, TIC-80, WASM-4, EasyRPG).
```

- [ ] **Step 2: Document the Quick Setup presets.** Find the line beginning `- **Self-updating**` and insert this bullet immediately before it:

```markdown
- **One-tap presets** — Quick Setup's Mode toggle picks how much to install: **Recommended · default core** (one core per recommended system), **Recommended · all cores** (every core each recommended system lists), or **Full · every system + core** (all ~129 ROCKNIX cores, every system enabled). Full is a large download and many of its systems need BIOS/content you supply.
```

- [ ] **Step 3: Verify no stale "13 console systems" references remain.**

Run: `grep -n "13 console" README.md`
Expected: no output.

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs: document ~70-system Full coverage and Quick Setup presets

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Final verification

- [ ] **Run the full engine test suite:** `bash tests/engine-test.sh` → all `ok:`, exit 0.
- [ ] **Syntax-check all shell:** `for f in bin/*.sh scripts/*.sh bootstrap.sh "ports/Update Emulators.sh"; do bash -n "$f"; done` → no errors.
- [ ] **Confirm the GUI binary is the rebuilt one:** `file bin/emu-manager` → aarch64 ELF, and `git status` shows it staged/committed.
- [ ] **(Optional, on device)** `--full-setup` then boot one core per category (a console game, an arcade `.zip`, a Doom `.wad`) through the sandbox launcher to confirm end-to-end.

---

## Self-Review notes (author)

- **Spec coverage:** §4 systems.conf → Task 1; §5 `--full-setup` → Task 2; §6 3-state toggle → Task 3; §9 docs → Task 4; §7 testing → tests embedded in Tasks 1–2 + Final verification. Non-goals (2048/PortMaster, content-less launch) intentionally excluded — the base-image fix is documented separately for the PanicOS maintainer (out of band, not tracked in this repo).
- **Edge cases honored:** ZX81 core literally named `81` (used verbatim, fixture-validated); `scummvm` extensions reduced to `scummvm svm`; empty/multi-`.info` cores (`gearlynx`, `mupen64plus`, `skyemu`) not surfaced as primaries but remain in the fixture (still grafted by `--all-cores`).
- **Type/name consistency:** `qs_mode`/`NQSMODE` used consistently in Task 3 (replaces `qs_allcores`); `FULL`/`full_setup`/`--full-setup`/`SELECTION` consistent across Task 2; `conf_rows_all`/`CORES_FIXTURE`/`UNKNOWN_CORES` consistent in Task 1.
- **No placeholders:** every code/step block is concrete and complete.
```

