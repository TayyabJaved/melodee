# Developing Melodee

A short guide to building, testing and installing our changes. The full reference is
[BUILDING.md](BUILDING.md); this file covers the day-to-day steps. It was checked on an
Apple silicon Mac (macOS, 18 cores) on 2026-10-25 against `main` at 1.5.0.

## One-time setup (macOS)

```sh
# 1. FriBidi, for Pillow's Raqm text layout (the UI font is rasterised at build time)
brew install fribidi

# 2. Python environment. Homebrew's python3 refuses global pip installs, so use a venv.
#    This is the same path tools/flash.sh uses, so the two share it.
python3 -m venv ~/.cache/melodee-venv
~/.cache/melodee-venv/bin/python -m pip install -r tools/requirements-build.txt mido python-rtmidi

# 3. JieLi pi32v2 toolchain (Linux x86-64 binaries) -> ~/.jieli/toolchain
tools/get_toolchain.sh

# 4. JieLi AC79 SDK -> ~/fw-AC79_AIoT_SDK (the build uses three files from it)
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
git -C ~/fw-AC79_AIoT_SDK rev-parse HEAD   # CI expects d179b4484759423312073f5fbb232501aa491047

# 5. Web test dependencies (jsdom)
npm ci --prefix web
```

You also need **Docker Desktop running** (`open -a Docker`). The toolchain is Linux
x86-64 only, so `tools/build.py` runs each compiler step in a `linux/amd64`
`debian:bookworm-slim` container under Rosetta. The first build pulls that image. Keep the
checkout under `/Users` so Docker can mount it.

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations.

## Everyday loop

Activate the venv first. `tests/run_tests.sh` calls `python3` directly, so it needs the
venv's Python on `PATH`.

```sh
source ~/.cache/melodee-venv/bin/activate

./build.sh             # ~18 s: firmware + generated headers
tests/run_tests.sh     # ~100 s: every host test (C, Python, web), in parallel
```

**Always run `./build.sh` before the tests.** The tests compile the firmware sources from
`firmware/src` against headers that the build generates in `build/gen/`. They also inspect
`build/melodee.fwsc`, `build/melodee.bin` and the disassembly in `build/melodee.dis` (the
target CPU budget). The test script only checks that `build/melodee.fwsc` exists, not
whether it is current.

The build writes these files:

| File | What |
| --- | --- |
| `build/melodee.fwsc` | installable package, dev identity `FM-1_900` |
| `build/melodee.bin` | application image |
| `build/loader/ota.bin` | USB-MIDI update loader |

Both the build and the tests must finish without errors. The build ends with a list of
`ok` lines covering SRAM placement, the stack audit, the audio arena and register access.
A layout or memory regression fails the build itself. The tests end with
`ALL HOST TESTS PASSED`; any failures are listed again at the end. Lines such as
`note: -22 % (BUDGET_UPDATE=1 to keep it)` are only information: the function got cheaper
than its recorded budget.

### Test options

| Command | Effect |
| --- | --- |
| `SOUND=0 tests/run_tests.sh` | skip the audio render/regression tests (not much faster on a many-core Mac; the UI jobs dominate) |
| `SOUND=only tests/run_tests.sh` | run only the sound tests |
| `UI=renders tests/run_tests.sh` | only render UI screenshots |
| `PARALLEL=1 tests/run_tests.sh` | run one job at a time (quieter timing figures) |
| `VERBOSE=1` | print every render |

Per-job logs are written to `build/host/log/<n>.log`.

### Running one test

There is no filter option. Each job compiles one `tests/<name>.c` with `cc` and runs it.
Find its line in [tests/run_tests.sh](tests/run_tests.sh) and copy the flags (`$FW` means
`-w -Ibuild/gen -Ifirmware/src`):

```sh
cc -O1 -ffp-contract=off -Wall -Wno-unused-function -w -Ibuild/gen -Ifirmware/src \
   -o build/host/chord_test tests/chord_test.c -lm && build/host/chord_test
```

Some tests take extra `-D` flags or arguments (for example `ui_test` takes
`-DMELODEE_USB_AUDIO=1`), so copy the whole line.

### Sound and CPU baselines

`tests/regress.c` hashes a render of every engine and preset and compares it with
`tests/golden.txt`. Instruction-cost limits are in `tests/cpu_baseline.txt` and
`tests/target_budget.txt`. When a change is **meant** to alter sound or cost:

```sh
GOLDEN_UPDATE=1 tests/run_tests.sh   # rewrite the golden hashes
BUDGET_UPDATE=1 tests/run_tests.sh   # rewrite the cost baselines
```

Then review the diff and commit only the entries the change was meant to affect. Never
update these just to silence an unexpected failure.

### Checking changes without hardware

- **UI:** the UI test job draws every screen in every palette with the real drawing code
  and lints for clipped or overlapping text. The PNGs land in
  `build/ui_new/<PALETTE>/*.png`, and the findings in `build/ui_new/report.txt`. For new
  or changed screens and indicators, follow [docs/design/README.md](docs/design/README.md),
  including its "Status indicators" section.
- **Audio:** `build/host/hostsim` (built by the test run) renders the DSP to a WAV file:
  `build/host/hostsim ENGINE PRESET MONO out.wav`. The options are at the top of
  [tests/hostsim.c](tests/hostsim.c), for example `SECS=`, `CHORD=`, `DRUMS=1`, `STEPS=`
  and `TRACKS=dir`.

## Installing on the FM-1

```sh
tools/flash.sh               # build, then install build/melodee.fwsc over USB-MIDI
tools/flash.sh --no-build    # install the existing build/melodee.fwsc
tools/flash.sh --info        # identity of the connected FM-1
```

`flash.sh` uses the same `~/.cache/melodee-venv` and calls
`tools/fm1_install.py --yes`. It asks for confirmation before installing any package that
does not have the dev identity `FM-1_900`. The FM-1 must be running Melodee (or Felucca)
and be connected by USB. Close other apps that use MIDI, and keep the device charged.
`--port NAME` selects a MIDI port.

If an install goes wrong:

- **The device shows as "Melodee Update":** it is in the update loader. Run the install
  again; sectors that are already verified are skipped.
- **Black screen:** switch on again, or hold **OCT−** at power-on. Three short flashes mean
  the update loader is starting; reinstall.
- **One long flash, or the device enumerates as USB `4c4a:8057`:** it is in ROM UBOOT.
  Follow [BUILDING.md → Recover a black-screen FM-1](BUILDING.md#recover-a-black-screen-fm-1)
  (`tools/fm1_recover.py`; needs `brew install libusb` and `pip install pyusb`).

Host tests do not prove that a build boots or sounds right. After a firmware change,
check on the device that it boots, that keys, knobs, audio and MIDI work, that settings
and projects persist, and that the update entry still works.

## Web editor

The editor is the single file [web/editor.html](web/editor.html), and its protocol is in
[web/EDITOR_PROTOCOL.md](web/EDITOR_PROTOCOL.md). Web MIDI needs Chrome or Edge on
`localhost` or HTTPS:

```sh
cd web && python3 -m http.server 8000 --bind 127.0.0.1
# http://localhost:8000/editor.html          real FM-1 (Melodee firmware only)
# http://localhost:8000/editor.html?mock=1   simulated device, no hardware
```

What the protocol can and cannot set on a live device (for building songs from a script or an AI) is mapped in
[docs/PROTOCOL_COVERAGE.md](docs/PROTOCOL_COVERAGE.md).

Chrome asks for MIDI/SysEx permission on the first Connect. The editor only talks to
Melodee firmware. On stock V15 or TransitFM1 it gets no reply. Its tests are
`node web/test_web.mjs`, which `tests/run_tests.sh` also runs.

### Generators (our mod)

ARP > GEN: Tessera's TURING, EUCLID, MARKOV, ATLAS, SERIES and BOUNCE on tracks 1–4. They are a regular feature, not behind a flag.

- `firmware/src/gen.c`, `gen.h`, `gen_grids.h`: copied unchanged from `~/Personal/tessera` (`013c883`). Keep them
  byte-identical so future generators can be added from there.
- `firmware/src/gen_track.c`: our adapter. `gn_step()` turns each generator step into a `step_t` that `seq_step()`
  plays, swapped in at one line of `seq_grid_tick()`.
- Pages: GEN (MODE, SEED, CHANGE, EVERY) and GEN 2 (the mode's settings, GATE / MARKOV LOCK / ATLAS CHAOS), knob ids 0..7 in
  `gn_page_desc()`. CHANGE's offsets come from `gn_changes()`: change *i* depends only on (seed, *i*), which is what
  makes turning back exact. Keep that property if you change it.
- Saving: an optional `'G'` block after the `'U'` block in the FUN22 record (`project.c`), written only while a
  generator is on: version 4 (25 bytes a track); versions 1 (14), 2 (18) and 3 (19) still load. Older firmware ignores
  it. A `_Static_assert` in `project.c` checks it fits the record's tail (68 bytes are left); a new mode appends to
  the `GN_` enum (saved numbers).
  MARKOV's LOCK phrase is not saved: it is the first pass from the seed, the same at every PLAY.
- TURING's display (`pv_gen` in `ui_pages.c`): the step sounding at the left, then the next steps in playing order
  (`gn_turing_hit()`), the loop's first step in knob 4's colour (`gn_turing_first()`). The loop position is
  `turing.step_idx`, which `gen.c` leaves unused for TURING: `gn_next()` advances it after each tick and
  `gn_change_now()` resets it (PLAY, a new seed, a CHANGE), so `gen.c` stays unchanged.
- Tests: `tests/gen_test.c` (the copied generators) and `tests/gen_track_test.c` (sequencer, Print, storage, page,
  TURING's display), plus the `gen_*` UI renders.

### Pattern tools (our mod)

`firmware/src/seq_tools.c` (adapted from Tessera's, over upstream's `pattern_double` / `pattern_half`, as SEQ's pattern
dialog uses them): Double, Half, Euclid and Euclid turn in the pattern sheet (`ui_popup.c` `SHEET_PATTERN`). Test:
`tests/seq_tools_test.c`.

## Matching CI

The nightly workflow ([.github/workflows/nightly.yml](.github/workflows/nightly.yml)) runs
`./build.sh --nightly` and then `tests/run_tests.sh` with the `MELODEE_*` options it sets.
A plain local `./build.sh` already enables the same performance defaults
(`firmware/src/performance_options.h`). To compile the host tests with the CI flags
explicitly, pass them through `CC`, for example
`CC="cc -DMELODEE_FM6_FAST=1 ..." tests/run_tests.sh`.

Build options are environment variables such as `MELODEE_USB_HOST=0 ./build.sh`; see
[BUILDING.md → Build options](BUILDING.md#build-options).

### M1/M2 key navigation (our mod)

The keyboard navigation layer is built in: `./build.sh` makes **one package, `build/melodee.fwsc`**, with it and every
other change of ours (`MELODEE_KEY_NAV` defaults to 1 in `firmware/src/melodee.c`; `MELODEE_KEY_NAV=0 ./build.sh` builds
upstream's keyboard, version `-nonav`, for comparisons only). M1 (F5), M2 (G5) and M3 (F#5, alone) with the black keys
press panel buttons, M3 also with the white keys F3 .. D5 (all twelve in one row, E4 a gap after GLO, `FM1_NAV_WHITE`,
`fm1_input_nav_whites`; kept notes on ARP > SHAPE by `nav_keep`'s `fm1_input_nav_whites_on`; each key lit as its
button, plane by plane: `ui_nav.c` `nav_mirror`), and F#5 keeps a momentary edit. M3 + A#4 presses no panel button: the layer's `FM1_NAV_ACTION` bit (14 of `fm1_in.buttons`, past the
matrix's 0..13; `panel.c` `B_NAV_HEAR`) makes `ui_input.c` call `notes_preview_key()` (`seq_notes.c`): the step at the
cursor heard, its whole chord, with NOTE PREVIEW off too. While M3 is held, F5 and G5 are ordinary keys, not M1 / M2 (in ARP > SHAPE: steps 15 and 16). The UI frees F5 / G5 /
F#5 to be notes for a while (`fm1_input_nav_free`, from `nav_keep`): all three while FX is held or locked and in the
punch editor, F5 / G5 on the drum grid. Key mapping: [BUILDING.md → Build options](BUILDING.md#build-options); the code is in
`firmware/hal/fm1_input.h` (`FM1_NAV_*`, three rows), the rows in `panel.c` `panel_nav()`, the keep in
`ui_nav.c` `nav_keep()`. The host stand-ins for the HAL are in `tests/hostsim.c` and `tests/settings_test.c`.

Build and install it as any change (until 2026-10-28 it was a separate `-nav` package, `build/melodee-nav.fwsc`; there is
no second package now):

```sh
./build.sh && tools/flash.sh --no-build
```

`flash.sh` with a package path installs that file without rebuilding. It works from
Melodee, the official V15 or TransitFM1 (which answers as `FM-1_015`). Run
`tools/flash.sh --info` first to check that the FM-1 is detected.
`tests/key_nav_test.c` covers it on the host, and `tests/run_tests.sh` compiles every test with it, as the firmware
is built (`MELODEE_KEY_NAV=0 tests/run_tests.sh` after a `MELODEE_KEY_NAV=0` build tests without it). With the flag 0
the M1/M2 code is compiled out entirely.

### Humanize velocity (our mod)

SCL list > **Humanize** (OFF / LOW / HIGH) is a regular track setting, not behind a
flag. It reuses the retired parameter slot 45 (`P_HUMVEL` in `firmware/src/core.h`), so
no storage format changes. Tests: the `humanize` checks in `tests/chord_test.c`, the
slot-45 checks in `tests/upreset_test.c`, `web/test_web.mjs`, and the
`scl_humanize` UI render.

### Chord sequencer (our mod)

SEQ > **CHORDS** / **CHORDS 2** on melodic tracks: a step holds one note plus the chord to play on it (CHORD, VOICE,
STRUM, DIR, VEL, LENGTH, NUDGE), kept in the step's spare note bytes 1..3 with a check, so no storage format
changes (`firmware/src/chord_seq.c` plays them, `firmware/src/ui_chords.c` edits them; `chord.c`'s
`chord_make_as` / `chord_strum_begin_as` take an entry's settings). Upstream code that empties a step must clear
those bytes (`step_clear`, `seq_entry_notes`). Tests: `seq_edit_test.c`, the roll renders.

### SHAPE arp (our mod)

ARP > MODE **SHAPE** (value 7): a rhythm of ON / OFF steps over the normal arp (`firmware/src/arp_shape.c`; the
gate in `seq.c` `arp_step`). STEPS and DIR sit in PROB and ORD's places on ARP 2 (`shape.ctrl`, not those
bytes). M3 or OCT− held on the ARP page: the white keys are the steps. Stored in `project_t.shape` and, on flash,
in wide bank records only (SHP1, before the checksum; see "What to check after every replay"). Tests:
`arp_shape_seq_test.c` (also built with `MELODEE_KEY_NAV`), `arp_shape_stream_test.c`.

### FX pages: SHARED first (our mod)

Since upstream 1.5.0 a track's own rack holds only the note effects (NOTE TAIL, NOTE ECHO) and every audio effect
lives on SHARED. We put SHARED first: `PAGES` in `params.c` lists SHARED and its slots before the track's rack,
renamed **NOTE FX** (its slots stay FX 1–3). The track rack's last row, the track's send, reads **Send** instead of
"Shared" (`ui_draw.c`, `ui_pages.c` `pv_rack`); SHARED's RETURN is unchanged. Navigation (`fx_rack_page()` in
`ui.c` finds a rack by scope, never by title): FX from elsewhere opens SHARED and, pressed again, moves on to NOTE FX;
FX on a slot's page returns to that slot's rack; OCT− on a slot goes to its rack, on either rack home; OCT+ on Send
still opens SHARED; an emptied slot falls back to its own rack. No stored data changes (page indices are not saved).
Docs: README's FX section, `docs/design/README.md` "FX rack", `web/manual/index.html` (its `fx_rack.png` screenshot
still shows the old title until the manual shots are regenerated).

### Song sections (our mod)

Upstream's SONG arranged rows (a bank per track, a repeat count, blocks held across rows, track 1's loop as the
clock, a jam log taken with REC held). Ours is columns of **sections** A–Z (mock:
`docs/design/mock/song_workflow_proposal.svg`; behaviour: the manual's "Patterns, sections and songs" and "Songs",
`docs/design/README.md` "SONG sections").

- **Where it lives:** in our own files, as upstream's SONG stays in its files: `song_sect.c` (the model, the section
  clock, live launch, SEC1's coding; included near the top of `song_chain.c`), `ui_sect.h` / `ui_sect.c` (SONG's
  editing, the section page, the keys; at the top and end of `ui_song.c`), `ui_sect_draw.c` (PATTERNS, SONG and the
  section page drawn as one family; at the end of `ui_patterns.c`), `ui_sect_sheets.c` (the sheets; in `ui_popup.c`).
  Upstream's code ours replaces is **kept verbatim** in its file inside `#ifdef MELODEE_UPSTREAM_SONG` (never
  defined): its edits merge cleanly and `tools/upstream_shadow.py` lists them for porting (see "Conventions" and
  "What to check after every replay"). In upstream's files the rest is one-line hooks, each tagged with our file's
  name (`/* (song_sect.c) */`): `song_chain.c` (4), `ui_song.c` (5), `ui_patterns.c` (1), `ui_popup.c` (3),
  `ui_input.c`, `seq.c` (5), `pattern_store.c` (4), `project.c`, `editor.c`, `ui_draw.c`, `ui_song_sound.c`,
  `ui_song_punch.c`.
- **Data:** upstream's per-row arrays stay the columns (`chain_config`, `chain_patterns`); `chain_config_t` adds
  `sect[64]` (the column's letter) and `len[64]` (sixteenths, 0 = AUTO). Every column of a letter holds the same
  patterns, sounds, punches and length: edits go through `sect_sync` (`song_chain.c`), `sect_consistent` checks it.
  Repeats are always 1, `CHAIN_HOLD` never stays in a loaded song. The sequencer, song sounds and punches still read
  rows, as upstream does.
- **Timing** (`song_sect.c`, our `chain_tick` / `chain_apply` / `chain_fx_tick` / `chain_row_samples`): the clock
  counts the section's length (`sect_clk.len`, from `chain_row_samples_of`: the set length, else the longest pattern
  of the tracks in use; recomputed on a tempo change or `len_dirty`) and every track restarts at each column.
  `sect_clk.solo` loops one column (the section page's PLAY: `chain_prepare_solo` in `ui_sect.c` around upstream's
  `chain_prepare_from`, which is unchanged: with nothing held across columns its seek is zero). Punch steps stay
  track 1's, restarting per loop of its pattern (`sect_clk.fx_loop` against `seq_loops[0]`).
- **Live launch:** `live` (`song_sect.c`), ticked from `seq_advance_part` after `chain_tick`; `live_apply`
  switches every track at once, `live.carry` starts them at the boundary's sample, `live.rest` silences the
  section's rests through `chain_rest` until a track switches by hand (`seq.c`). `seq_stop` resets it. Header
  states come from `song_edit_status` (`ui_song.c`): LOOPING A, B NEXT, A CHANGED.
- **UI:** `ui_sect.c` (columns, `song_add_playing`, `song_column_section`, the section page `sp`, `sect_pattern` /
  `sect_length` / `sect_step` / `sect_play`, `song_launch`; the keys `sect_song_knob` / `sect_song_oct`; ours of
  `song_arm`, `song_insert`, `song_apply_destination`, `song_edit_status`), `ui_sect_draw.c` (the cells, the cards,
  `sect_draw`), `ui_sect_sheets.c` (SHEET_SONG, SHEET_SECTION); hooks in `ui_input.c` (REC launch, PLAY, PRESETS,
  SELECT), `ui_song_sound.c` and `ui_song_punch.c` (per section), `ui_draw.c` (the SECTION A chip, the delete
  question).
- **Storage:** SEC1 (layout in `pattern_store.c`, `BANK_SECT_OFF`; coding in `song_sect.c` `sect_block_*`): magic, 64 letters, 64 lengths, the 196 bytes before SHP1,
  written with it in every FBKL-family record; `pattern_room` reserves it. A record without it (upstream's, the
  dense FBKK) is converted by `song_sections_normalize(0)` after its banks are in (lengths need their timings);
  with it, `song_sections_normalize(1)` keeps the letters if they still describe the columns. `song_note` tells the
  UI (`ui_notices`) when held blocks restarted or the song was cut at 26 sections. The editor's BANK_SONG and SONG
  sets are converted the same way (`editor.c`).
- **Guide:** [docs/song-guide](docs/song-guide/README.md), twenty step pictures and their page, written by
  `tools/gen_song_guide.py` (drawings, not renders: change its steps with the controls; `--check` lists stale files).
  `docs/SHORTCUTS.md`'s pictures, `docs/shortcuts/`, are `tools/gen_shortcuts.py`'s (the keyboard maps follow
  `hal/fm1_input.h`, `panel.c`, `ui_layer.c`, `punch.c`; `--check` the same way).
- **Tests:** `song_sect_test.c` (ours, in place of upstream's `song_mode_test.c`, which is kept verbatim but not
  run: lengths, rests, columns, adding, persistence, older songs),
  `song_preset_test.c`, `song_punch_test.c`, `ui_test.c` (live launch, the section page, SONG keys),
  `ui_render.c` (the scenes converted; `song_section` shows the section page), `editor_test.c`,
  `project_stream_test.c`, `pattern_test.c`, `midi_track_test.c`.

Release packages must come from the attested `zvenson/fm1-build` workflow on a tag; never
publish a local `--release` build. See [BUILDING.md → Releases](BUILDING.md#releases).

## Where things are

| Path | What |
| --- | --- |
| `firmware/src/` | application. `melodee.c` is a unity build that `#include`s the other sources |
| `firmware/src/eng_*.c` | synth engines (FM6, CZ, Prophet, DRUM; PHYS, NOISE … with `MELODEE_LEGACY_EXTRAS`) |
| `firmware/src/ui*.c` | screens, input and drawing |
| `firmware/src/seq*.c`, `pattern*.c` | sequencer and patterns |
| `firmware/hal/` | register-level drivers. Register access is allowed only here, and the build checks it |
| `firmware/loader/`, `firmware/stub/` | OTA loader and boot stub |
| `tools/build.py` | the build (code generation, compile in Docker, link, layout checks, packaging) |
| `tests/` | host tests. `run_tests.sh` lists them all |
| `web/` | installer, editor and their Node tests |

## Memory

The regions are laid out in [firmware/app.ld](firmware/app.ld). `tools/build.py` checks every limit below after
the link and prints the results as `ok` lines; any failure stops the build.

### Limits

Measured on the standard build of `tj/1.6.0-rc.6-trial` (1.6.0-rc.6 plus our stack, 2026-10-28).
This is the one package, M1/M2/M3 included.
The flash limit is not the 581,564 B app slot: the image must end before the commit-marker sector
(`tools/fm1pkg_make.py` `BODY_MAX` 0x8C000 after the 3,808 B stub), 577,248 B in all. Earlier versions of this file
measured against the slot and overstated the room by about 4.3 KB.

| Region | Size | Used | Left | What lives there | When it is exceeded |
| --- | ---: | ---: | ---: | --- | --- |
| Flash app image | **577,248 B** (body 0x8C000 + stub 0xEE0) | 570,616 B | 6,632 B | code, `const` data, the compressed SRAM images | build fails: "app body overlaps the commit-marker sector" |
| Low SRAM (`RAMTEXT`) | 32,512 B | 30,728 B | 1,784 B (to the arena) | code that runs while flash is busy, DSP kernels and tables | link fails |
| **Bank** (`.data` + `.bss`) | 98,304 B | 96,584 B | **1,720 B** (to the arena) | ordinary globals | link fails: "region `RAM' overflowed" |
| **Pool** region | 357,888 B | `.pool` 134,052 B, resident audio code, CPU1 stacks 4,096 B | arena 89,504 B | `.pool` buffers, audio code, then the audio working arena | build fails if the arena drops under 49,152 B or the pool's headroom under 8,192 B |
| Foreground stack | 12,288 B | 9,604 B deepest path + 2,048 B required margin | **636 B** | the main loop (UI, storage) | build's stack audit fails |
| Interrupt stack | 6,144 B | 4,520 B (audio 2,988 + BLE 720 + timer 220 + fault 464 + 128) | 1,624 B | interrupts, nested | build's stack audit fails |
| CPU1 stack | 3,072 B | 1,828 B + 768 B guard and margin | 476 B | the second core's renderer | build's stack audit fails |
| Retained (`NOINIT`) | 15,664 B | all but 48 B | 48 B (to the arena) | state that survives resets: panel calibration, settings, retained patterns | link fails |
| Cache RAM | 28,672 B | given to audio after the boot self-test | | | |

- **Bank:** ordinary globals. 1.7 KB are left on the rc.6 trial (it was 72 B on rc.4; the song sections gave back
  80 B, the jam log's 577 B minus the columns' letters and lengths); keep our state out of it all the same.
- **Pool:** buffers marked `__attribute__((section(".pool")))`. They are zeroed at boot right after `.bss`
  (`main.c`). The rest of the pool region holds the resident audio code, then the **audio working arena**: engine
  states and effect delay lines, allocated on demand.
- **There is no free RAM.** Whatever is left of the low SRAM, bank, pool and retained regions goes to the audio
  arena. Moving a buffer from the bank to the pool fixes a link error, but it costs the arena the same bytes. Only
  making data smaller saves RAM.

### Rules for our code

- **New state goes in the pool**, with `__attribute__((section(".pool")))`. The section is not loaded from flash,
  so an initializer there is silently lost: start from zero, or set values in code (as `gn_defaults()` does).
- **Tables that never change are `static const`.** They stay in flash and cost no RAM.
- **Code reached from the audio interrupt runs from SRAM.** `tools/audio_ram.py` copies everything the audio interrupt
  can reach into the pool's resident audio code, and growing that code shrinks the arena. The sequencer tick is one
  of these: `seq_grid_tick()`, and whatever it calls. To keep our code out of it, give it a `noinline` entry point and
  add that name to `XIP` in `tools/audio_ram.py`. The function and everything only it reaches then stay in flash, and
  the audio code calls it through a pointer. The generators do this (`gn_next()`, `gn_restart_all()`); in the
  sequencer tick only the inline check `gn_step()` remains. Do this for work done once a step, not per sample. Code
  that is not on the audio path, such as Print, the pages and the pattern tools, is in flash anyway.
- **No large local arrays in main-loop code.** The foreground stack has 636 B left beyond its margin. Use a static
  pool buffer instead. Locals in audio-path code count against the interrupt stack.
- **Nothing of ours goes in `NOINIT`** (state that survives resets) unless it must.
- After each build, read the `ok` lines, and build the nav variant too.

### Largest symbols

Measured on `tj/1.6.0-rc.4` (before the rc.5 replay, the SID removal and the SHAPE rhythm): sizes have moved
since; re-measure before relying on a number.

**Bank** (98,232 B used; these 15 are about half of it, the rest is ~950 smaller variables)

| Symbol | Bytes | What | File |
| --- | ---: | --- | --- |
| `trk` | 9,312 | the tracks (four parts, then the MIDI-only tracks) | `core.h` |
| `part_voice` | 5,120 | the parts' synth voices | `core.h` |
| `up_meta` | 4,992 | user preset index: names and flags | `upreset.c` |
| `proj_wire_u` | 4,104 | transfer scratch for project and bank records | `eng_fm6.c` |
| `FM6_SIN` | 4,100 | FM6 sine table, filled at boot | generated (`gen_tables.py`) |
| `recording_run` | 4,096 | recorded notes' playback state (1,024 notes) | `recording.c` |
| `recording_due` | 4,096 | recorded notes' next times | `recording.c` |
| `set_rec` | 3,648 | the settings record; kept in the bank on purpose so the arena keeps the pool | `project.c` |
| `recording_next` | 2,048 | recorded notes' order | `recording.c` |
| `abuf` | 2,048 | audio output buffer | `audio.c` |
| `FM6_MKI_LOG`, `FM6_MKI_EXP` | 2 × 2,048 | FM6 MARK I tables, filled at boot | generated |
| `chain`, `chain_config` | 1,808 + 1,604 | the song: its columns, their sections (host sizes; the jam log, 577 B, removed) | `song_chain.c` |
| `bll` | 1,416 | Bluetooth LE link layer | `ble/ble_ll.c` |
| `midi_voice`, `midi_sel_on` | 1,280 + 1,280 | MIDI-only tracks' voices, held MIDI notes | `core.h`, `midi_control.c` |

**Pool** (111,096 B, 42 symbols)

| Symbol | Bytes | What | File |
| --- | ---: | --- | --- |
| `cv_primary_u` | 30,720 | the screen canvas | `gfx.c` |
| `pattern_chunks_ram` | 13,312 | pattern storage | `pattern.c` |
| `recording` | 9,216 | recorded notes | `recording.c` |
| `ua` | 8,264 | USB audio streaming | `usb_audio_stream.c` |
| `audio_batch_jobs` | 7,296 | audio render jobs | `audio_batch.h` |
| `pattern_stage` | 5,712 | each track's pending pattern | `pattern.c` |
| `bank_buf` | 4,292 | a loaded preset bank | `upreset.c` |
| `pattern_link` | 3,584 | pattern chunk links | `pattern.c` |
| `df_sum` | 2,688 | screen change tracking | `gfx.c` |
| `recording_heap_store` and 4 indexes | 2,560 + 4 × 2,048 | recorded note indexes | `recording.c` |
| `mb` | 1,720 | BLE MIDI | `midi_ble.c` |
| `undo_sram` | 1,612 | undo levels | `ui.c` |

**Ours:** `gn` (generators) 736 B and `fm1_nav` (M1/M2) 60 B, both in the pool; the song sections
grow `chain` and `chain_config` by 192 B each (a letter and a length per column) and `live` (12 B) is new, while
upstream's `jam` (577 B) is gone. See the budget below.

To list them again after a build (sizes in hex; `.L_MergedGlobals` entries are the compiler's bundles of small
variables, overlapping the named ones):

```sh
nm -S -n build/melodee.elf | awk '$1>="01c08000" && $1<"01c20000"' | grep -v MergedGlobals | sort -k2,2r | head -20   # bank
nm -S -n build/melodee.elf | awk '$1>="01c20000" && $1<"01c77600" && $3 ~ /^[bBdD]$/' | sort -k2,2r | head -20       # pool
```

### Our footprint and budget

What our stack adds on top of plain upstream (our one build, M1/M2/M3 included; without them, `MELODEE_KEY_NAV=0`,
in brackets; 2026-10-28, rc.6 trial):

| | Upstream 1.6.0-rc.6 | With our stack | Ours | Our budget |
| --- | ---: | ---: | ---: | ---: |
| Flash image | 556,368 B | 570,616 B | +14,248 B (+12,932 B) | 20,000 B |
| Bank | 96,648 B | 96,584 B | −64 B (−80 B) | 64 B |
| Pool (`.pool`) | 132,868 B | 134,052 B | +1,184 B (+1,100 B) | 4,096 B |
| Audio arena | 85,984 B | 89,504 B | +3,520 B (+3,936 B) | |

The SID and TRIO removal gives back about 16 KB of upstream's flash and its resident audio code (hence the larger
arena); our features (generators, the chord sequencer, the pattern tools, SHAPE, the UI changes, M1/M2/M3) take about
25 KB of flash in all, the song sections about 3.4 KB of it (the section page, live launch, converting older songs).
The BLE fix is one line since upstream merged PR #90 in rc.5.

- **Bank:** what is ours is the far-call pointers `audio_ram.py` generates for our flash-resident audio entries
  (they must be loaded data), `seqt`'s 2 B (it starts non-zero), the columns' letters and lengths, and alignment;
  removing upstream's jam log more than pays for them.
- **Audio arena:** the song sections take 960 B of it: the section clock, live launch and section lengths run in the
  audio interrupt (`chain_tick`, `live_tick`, `chain_row_samples_of`), so they are resident audio code.
- **Flash** limits new features: 6.6 KB is left for upstream's growth and ours together (PATTERNS' numbered cells
  replaced its note-bar drawing: 1.4 KB back).
  Prefer `static const` tables, short strings and small pictures (`pv_gen` alone is 1.8 KB), and reuse upstream
  helpers instead of copying them.
- Before this setup, the generators were inlined into the sequencer tick: 6.9 KB of resident audio code, and `gn` in
  the bank overflowed it.

Measure it at every replay, and after adding anything big. Build plain upstream in a worktree and compare the `ok`
lines, then the symbols:

```sh
git worktree add --detach ../melodee-upstream v1.6.0-rc.5
(cd ../melodee-upstream && ./build.sh) | grep -E 'image|arena'
./build.sh | grep -E 'image|arena'
python3 - ../melodee-upstream/build/melodee.elf build/melodee.elf <<'PY'
import subprocess, sys
def syms(elf):
    out = subprocess.run(['nm', '-S', elf], capture_output=True, text=True).stdout
    return {p[3]: (int(p[0], 16), int(p[1], 16)) for p in map(str.split, out.splitlines()) if len(p) == 4}
def region(a):
    return ('bank' if 0x01c08000 <= a < 0x01c20000 else 'pool/audio' if 0x01c20000 <= a < 0x01c77600 else
            'flash' if a >= 0x02000000 else 'other')
up, ours = syms(sys.argv[1]), syms(sys.argv[2])
grow = sorted(((n - up.get(k, (0, 0))[1], region(a), k) for k, (a, n) in ours.items()), reverse=True)
for d, r, k in grow[:25]:
    if d > 0: print(f'{d:6d} {r:10s} {k}')
PY
git worktree remove ../melodee-upstream
```

## Conventions for our changes

From [AGENTS.md](AGENTS.md):

- Credit new files to `Kerem Kilic (Ellic Studio)` with the SPDX licence line, as in the
  existing headers. Keep Leo Kuroshita's notice on retained Felucca code
  (`Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments`), and
  keep all third-party notices.
- Ongoing state is shown in the shared header. Toasts are for brief explanations, dialogs
  for decisions.
- Keep a feature in its own files with thin hooks into upstream code (`gen_track.c`,
  `seq_tools.c`, one line in `seq.c`; `ui_nav.c`, included at the end of `ui_input.c`). Upstream merges then
  conflict little. Mark each hook with a short comment naming our file, so a conflict shows what it belongs to.
- **Replacing upstream code:** don't delete or rewrite it in place. Keep upstream's text verbatim inside
  `#ifdef MELODEE_UPSTREAM_<FEATURE>` (never defined; today `MELODEE_UPSTREAM_SONG`), write ours in our file under the
  same function name, and leave a tagged hook where needed. Upstream's later edits to that text then merge without
  conflicts, and `tools/upstream_shadow.py --from <old> --to <new>` lists each of them so it is ported to ours (or
  noted as not applying). A marker may end with `#else` and ours in place, for a line or two. An upstream test file
  ours replaces stays verbatim and unrun, listed in the tool's `SUPERSEDED`. Prefer calling upstream's functions to
  copying them: what ours calls gets their fixes for free.

## Following upstream releases

This repo is a fork of [keremimo/melodee](https://github.com/keremimo/melodee). The remotes
are `origin` (this fork) and `upstream` (keremimo). Upstream's `main` carries stable
releases; its `next` carries release candidates (tags such as `v1.6.0-rc.4`).

Our approach: **start from upstream's latest release and replay our changes onto it.** Our
work is a stack of commits kept on top of an upstream release tag, in this order (`git log --reverse
<upstream tag>..HEAD`):

| # | Commit | What it changes | Upstream files it hooks into | Drop when |
| --- | --- | --- | --- | --- |
| 1 | M1/M2 key navigation (`MELODEE_KEY_NAV`) | F5 / G5 + black keys press panel buttons; on by default, in the one package (`build/melodee.fwsc`; `0`: the `-nonav` comparison build) | `hal/fm1_input.h`, `panel.c`, `ui_input.c`, `melodee.c`, `tools/build.py`, `BUILDING.md`; host stand-ins in `tests/hostsim.c`, `tests/settings_test.c` | never (ours) |
| 2 | Humanize velocity | SCL > Humanize (OFF / LOW / HIGH), parameter slot 45, web editor `HVEL` | `core.h`, `params.c` (`param_kept`), `seq.c`, `midi_control.c`, `motion.c`, `project.c`, `upreset.c`, `ui_list.c`, `web/editor.html`, `web/EDITOR_PROTOCOL.md`; tests | upstream takes it |
| 3 | Generators (ARP > GEN) | Tessera's generators on tracks 1–4, GEN / GEN 2 pages, the project's `'G'` block; TURING's display from the step sounding, its loop's first step marked | new `gen*.c/h`, `gen_track.c`; `seq.c` (one line), `params.c`, `project.c`, `project_stream.c`, `tracks.c`, `ui.c`, `ui_draw.c`, `ui_input.c`, `ui_pages.c`, `ui_popup.c`, `tools/audio_ram.py` (`XIP`) | never (ours) |
| 4 | Pattern tools | Double, Half, Euclid, Euclid turn in the pattern sheet | new `seq_tools.c`; `ui.c` (one include), `ui_popup.c` | upstream takes it |
| 5 | BLE: measure the radio calibration with a stored one too | measure record 187 even when a calibration is stored (upstream merged PR #90, which measures only when none is stored, in rc.5; its buffers and host stub are upstream's now) | `midi_ble.c` (one line) | upstream measures with a stored calibration too (check on the device) |
| 6 | UI: Bluetooth in the header only on its page | status only on Settings > Bluetooth, toasts, the BLUETOOTH row's state; adds `ui_nav.c` | `ui_draw.c`, `ui_menu.c`, `main.c` (one line), `ui_input.c` (includes `ui_nav.c` at its end) | never (ours) |
| 7 | UI: ENV, LFO and EDIT pages, one home each | an engine's envelope / LFO pages are ENV's / LFO's only; TRACK LFO names | `ui.c` (`native_titles` CZ list, `page_scroll`, `open_family`), `ui_sections.c`, `ui_input.c` (PRESETS), `ui_draw.c` | never (ours) |
| 8 | UI: page position as "2/4" | "k/n" after the BPM on every page | `ui_draw.c` (`draw_head`) | never (ours) |
| 9 | UI: PRESETS shows the section's name | the section's name in the header | `ui_sections.c` (`sec_turn`) | never (ours) |
| 10 | UI: PRESETS opens a section's first page …; no save page in EDIT | first page on a section jump, the name in the page name's place, STORE pages out of EDIT | `ui_sections.c`, `ui_draw.c`, `ui.c` | never (ours) |
| 11 | M1/M2: F#5 as M3 … and keep | M3 + C#5 / D#5 = OCT− / OCT+, M3 + A#4 hears the NOTES cursor's note or chord (`FM1_NAV_ACTION`, `notes_preview_key`), F#5 keeps a momentary edit; M3 held, F5 / G5 are keys, not M1 / M2 | `hal/fm1_input.h`, `panel.c`, `ui_input.c`, `ui_nav.c`, `seq_notes.c`; tests `key_nav_test.c`, `nav_test.c`, `settings_test.c` and `hostsim.c` stand-ins | never (ours) |
| 12 | UI: drum NOTES always the grid | the grid shows programmed and recorded hits (short bars); pattern sheet Drum view Grid / Notes | `ui.c` (`grid_on`), `ui_pages.c` (`pv_grid`), `ui_graph.c` (`graph_grid`), `ui_popup.c` (`SHEET_PATTERN`, its last row) | never (ours) |
| 13 | M1/M2/M3: light their map while held | the modifier's keys and the buttons they press light, the usual lights step aside | `ui_input.c` (`ui_leds`, one line); host stand-in in `tests/hostsim.c` stores the rows | never (ours) |
| 14 | Remove SID and TRIO; add the chord sequencer | the SID engine, its drum kits (KIT SID / SID DIGI) and TRIO deleted (engine IDs 3 and 6 retired, a stored SID kit plays the 909); SEQ > CHORDS / CHORDS 2: a step is one note plus chord, voicing, strum, direction, nudge in its spare note bytes; adds `chord_seq.c`, `ui_chords.c` | deletes `eng_sid.c`, `drum_sid.c`, `eng_trio.c`, their tests, `tools/gen_tables.py`'s SID tables, `assets/resid/` and its `LICENSING.md` row; `engines.c`, `core.h` (`NENG_SHOWN`), `eng_drum.c`, `voice.c`, `params.c` (pages, WEIGHTS), `midi_control.c`, `audio_profile.h` / `audio_batch.h` (the SID category kept as a placeholder), `browse.c`, `icons.c`, `ui_menu.c`, `chord.c` (`chord_make_as`, `chord_strum_begin_as`), `seq.c`, `seq_notes.c`, `ui.c`, `ui_draw.c`, `ui_graph.c`, `ui_input.c`, `ui_nav.c`, `tools/build.py`, `tools/prepare_release.py`, `web/editor.html`; many tests | never (ours) |
| 15 | Chord preview | every chord-entry edit is heard | `ui_chords.c`; `tests/seq_edit_test.c` | never (ours) |
| 16 | Drum keys are the grid's black keys | a drum track's first eight black keys play its eight lanes everywhere, white keys silent | `eng_drum.c` (`drum_keys`); `tests/mpc_octave_test.c`, `tests/ui_test.c`, `tests/chord_test.c` | never (ours) |
| 17 | ARP: SHAPE mode, a rhythm of ON / OFF steps | ARP > MODE SHAPE (7): each step ON (the next held note, DIR's order) or OFF (a rest); STEPS and DIR in PROB and ORD's places on ARP 2 (`shape.ctrl`, not those bytes); M3 / OCT− held on the ARP page: the white keys are the steps; stored in `project_t.shape` (10 B a track) and, on flash, in the SHP1 block of FBKP/FBKQ records (FBKL–FBKO as our rc.4 build wrote them) | new `arp_shape.c`; `seq.c` (`arp_step`: the gate, `keyboard_block`), `params.c` (include, MODE name, `page_desc`), `ui.c`, `ui_input.c` (`ui_leds`, `shape_keys`, PRESETS), `ui_name.c` (`keys_mode`), `ui_pages.c` (`pv_arp`), `project.c`, `pattern_store.c`, `web/editor.html` (MODE names) | never (ours) |
| 18 | FX: SHARED first, then the track's NOTE FX | SHARED (audio) is the first FX page and FX opens it; the track's rack is NOTE FX, its send row "Send" | `params.c` (`PAGES` order, title), `ui.c` (`fx_rack_page`, the FX button), `ui_input.c` (`fx_rack_enter` / `fx_rack_back`), `ui_draw.c`, `ui_pages.c`, `docs/design/README.md`, `web/manual/index.html`; `fx_test.c`, `midi_insert_test.c`, `midi_knob_test.c`, `midi_track_test.c`, `ui_render.c` | never (ours) |
| 19 | SONG: sections, the section page, live launch | a song is columns of sections A–Z (one definition each: patterns or rests, a length, sounds, punches); OCT+ on + adds what plays; the section page (OCT+ on a column) sets each track's pattern and the length (AUTO: the longest pattern), PLAY loops it; REC launches a section live; the jam log, repeats and held blocks removed; older songs convert on load; the SEC1 block before SHP1; one look on PATTERNS, the section page and SONG (pattern numbers in the cells, empty cells blank, REST, SONG's cards in bars:beats) | ours: `song_sect.c`, `ui_sect.h`, `ui_sect.c`, `ui_sect_draw.c` (PATTERNS' numbered cells, SONG, the section page), `ui_sect_sheets.c`, `tools/upstream_shadow.py`; upstream's replaced code kept verbatim under `MELODEE_UPSTREAM_SONG` in `song_chain.c`, `ui_song.c`, `ui_patterns.c`, `ui_popup.c`, `ui_input.c`, `seq.c` (the jam), `tracks.c`, `ui_draw.c`; hooks in those and `core.h` (`chain_config_t.sect`, `.len`), `pattern_store.c` (SEC1's layout, `pattern_room`), `project.c`, `editor.c`, `ui_song_sound.c`, `ui_song_punch.c`; tests `song_sect_test.c` (in place of `song_mode_test.c`, kept unrun), `song_preset_test.c`, `song_punch_test.c`, `ui_test.c`, `ui_render.c`, `editor_test.c`, `project_stream_test.c`, `pattern_test.c`, `midi_track_test.c` | never (ours) |
| 20 | GLO: its main page first | a GLO tap opens the GLO layer locked open as its main page (mutes, solos, TAP, levels); GLO again: MIXER, GLOBAL .. WEIGHTS 2, then the main page; SELECT: the six as one list, no wrap (GLO SELECT only with GLO held); held: the layer while held; no double-tap lock for GLO | `ui_layer.c` (`glo_main`, `glo_open`, `glo_scroll`, `layer_lock_input`, the footer), `ui_nav.c` (MIXER in GLO's `nav_pages`, `nav_pos`), `ui_input.c`, `ui.c` (`open_global`'s comment); tests `ui_test.c`, `layer_screen_test.c`, `ui_render.c` (`layer_glo_main`) | never (ours) |
| 21 | M3: the white keys press all twelve buttons | M3 + F3 .. D4: FX SCL ENV LFO EDIT GLO, E4 a gap (a note), F4 .. D5: HOME SAVE ARP SEQ PLAY REC, held as long as the key; on ARP > SHAPE they stay its steps; M3's keys lit as their buttons (lit, dim, glow, breathing; dark ones glow), the buttons their usual lights | `hal/fm1_input.h` (`FM1_NAV_WHITE`, `fm1_input_nav_whites`, `_whites_on`), `panel.c` (`panel_nav`), `ui_nav.c` (`nav_keep`, `nav_leds`, `nav_mirror`), `ui_input.c` (`ui_leds`), `ui.c`; tests `key_nav_test.c`, `nav_test.c`, `hostsim.c` and `settings_test.c` stand-ins | never (ours) |
| 22 | Navigation: one set of rules | docs/NAVIGATION.md's decisions: SELECT pages only, stopping at both ends (and `nav_next_group`; ENV / LFO taps still go round); PRESETS the bigger step and the row where KNOB 2 picks one (lists, sheets, value lists, the menu, slots, FX racks, MOD, MOTION, the drum grid's lanes, the punch editor's lanes), the section page's length, PRESETS: GO TO STAGE where it has nothing to do; the section map KNOB 1 / PRESETS category, KNOB 2 / SELECT page; slot pages KNOB 1 ten slots; the menu's PRESETS stops; the section page SELECT walks the sections (`sect_step`), its position k/n of them; GLO's main page 1/6; F5 G5 F#5 freed under FX and on the drum grid | `ui_nav.c` (`nav_scroll`, `nav_next_group`, `nav_pos`, `nav_head_pos`, `nav_keep`), `ui.c` (`open_family`), `ui_input.c` (PRESETS, SELECT, the map, sheets, `slot_jump`), `ui_designer_input.c`, `ui_menu.c`, `ui_song.c` (`sect_step`), `ui_song_punch.c`, `hal/fm1_input.h` (`fm1_input_nav_free`); tests `ui_test.c` (`test_navigation_rules`), `nav_test.c`, `layer_screen_test.c`, `key_nav_test.c`, `scale_picker_test.c`, `fx_test.c`, `hostsim.c` | never (ours) |
| 23 | DEV.md, plan.md, our docs | this file, the plan, task.md, README's sections on our features; the manual's and design notes' sections on them; docs/SHORTCUTS.md, docs/NAVIGATION.md, the PATTERNS and SONG guide and their generated pictures; the rc.5 notes | `README.md`, `web/manual/index.html`, `docs/design/README.md`, `BUILDING.md`, `docs/RELEASE_1.6.0-rc.5.md` | never (ours); keep it last |

Replayed onto 1.6.0-rc.5 on 2026-10-27 (`tj/1.6.0-rc.5`): PR #90's own commit dropped (upstream merged it), the
SHAPE commits squashed, the docs commit last again. Conflicts met: `melodee.c` (the version), `project_stream.c`
(rc.5's owner check beside our generator block), `tests/project_test.c` (`note7`), `docs/design/README.md` (Rubik
back), `web/editor.html` (32 slots), `fm1_ble_rf.h` (upstream's), and `pattern_store.c`: rc.5's FBKP replaced the
SHAPE storage written for rc.4 (now in FBKP, `pattern_room` keeps its 44 B). Tests fixed after it: the
`recorded_note()` / `recorded_set_note()` accessors in `seq_tools_test.c` and `gen_track_test.c`; the CHORDS card
label is CHRD (CHORD is cut in Rubik).

Replay onto 1.6.0-rc.6 on 2026-10-28 (`tj/1.6.0-rc.6`, tried first as `tj/1.6.0-rc.6-trial`): 29 commits replayed; rc.5's branch
unchanged. Conflicts: version strings (rc.6 and its `-nonav` twin); both sides' additions to lists and docs kept;
SID/TRIO removal kept CR78/FM and their golden renders, browser and web metadata; SHAPE/SEC1 storage kept clear
of rc.6's FBKQ pattern-preset IDs; the shared song/pattern sound picker and note-preview helpers kept upstream's
new code with our hooks. `drum_sid_gone.c` retains the lane kits' level helper without the SID runtime. CR78/FM
factory presets are 2/3 after SID's two presets are removed; stored KIT IDs stay 7/8 (6 still plays the 909).
Post-replay fixes: restored DRUM's upstream `.cap` and `.desc` callbacks and `track_desc`'s unconditional callback
lookup (CR78/FM are not legacy extras). Without `.cap`, each hit allocated another kit voice, breaking lanes,
retriggers and level bounds. Drum tests now check the registered callbacks and the public descriptor path.
`ui_test.c` keeps upstream's pattern-knob checks without `pt_page` (ours displays all 16); KIT turns skip SID and
stop at FM. `drum_kits_test.c` uses four presets and corrected names; `song_preset_test.c` drops the retired SID
from upstream's new native-pattern-snapshot cases. The manual describes Section > Pattern > Default sound priority. `tools/upstream_shadow.py` (rc.5 to rc.6) listed 7
items, all upstream's PATTERNS drawing in `ui_patterns.c`. Ported to `ui_sect_draw.c`: Copy to another track
(`ui.ptc_to` in `pt_row`) and ALGORITHM picking Copy to's track (`pt_group()` in `patterns_draw`). Not applicable: the
two pages (`ptv.focus`, `pt_page`, `pt_numbers`' dots, `pt_row`'s off-page tick), `pt_prog_bar`'s fixed width and
`pt_bars`' narrow cells, as ours shows all 16 in numbered cells (a design choice, kept). Checks: build ok (570,616 B,
6.6 KB of flash left), `MELODEE_KEY_NAV=0` suite passed; the standard suite passed but for `audio_cpu_take_test.py`
(lateness) and `mod_test`'s cost (+5.0 %, at its limit), both passing alone: load-sensitive, upstream's tests. Device (2026-10-29, the build of this branch): basic navigation
tried on the FM-1, working; not yet tried there: the CR78 / FM kits, sections with pattern presets, USB hubs, song
playback and storage round trips.

Shadow report, all seven rc.5→rc.6 items reviewed (the report still lists historical changes; decisions here):
- layout / `ptv.focus`: not applicable, no page state in our all-16 layout;
- `pt_page`: not applicable, no paging;
- `pt_prog_bar`: not applicable, our cells keep their own progress-bar width;
- `pt_bars`: not applicable, our cells show numbers, not note-density bars;
- `pt_numbers`: not applicable, numbers already live inside every cell;
- `pt_row`: cross-track Copy to highlighting and invalidation ported to `ui_sect_draw.c` (`ui.ptc_to`);
  paging ticks and upstream's cell geometry not applicable;
- `patterns_draw`: `pt_group()` ported, so Copy to displays the destination track group; paging not applicable.
Every marker matches rc.6's exact text (`--from v1.6.0-rc.6 --to v1.6.0-rc.6`: no changes).

Checks: both full suites passed (`PARALLEL=4`, standard and `MELODEE_KEY_NAV=0`), 0 lint findings; both builds,
golden renders, web tests, generators and shadow check passed. At default parallelism the CPU live-take timing
check failed twice and installer safety once; both passed alone, and both complete reduced-parallelism runs passed.
Compared a plain rc.6 build (Memory above); copied Tessera generators remain byte-identical. Slot 45 / Humanize,
GEN pages, SHAPE mode 7, project extents, SEC1/SHP1 room beside FBKQ, chord spare bytes and UI hooks checked in
source and their host tests. No feature dropped. Device timing, audio, CR78 HATS, per-pattern/section sound priority
and save/reload on rc.6 still need a hardware pass; the user's earlier device pass covered rc.5, not this base.

Since the rc.5 replay the newer rows are working commits, not yet one a row: `e82af04` (row 19), `0367108` (the guide,
row 23, and row 1's one build: `MELODEE_KEY_NAV` on by default), `4034a6a` (row 19's one look), `fd6b582` (rows 20 and 11's M3 + A#4, the
PATTERNS empty cells), `96d9cc3` (row 21 and the shortcuts docs), then row 22 (the navigation rules, after it), then row 19's move into our own files ("SONG sections: our own files", 2026-10-28: `song_sect.c`, `ui_sect*.c`, upstream's song code kept under `MELODEE_UPSTREAM_SONG`, `tools/upstream_shadow.py`; a commit of its own, not a fixup, as the later commits edit the same lines).
They stay as they are (the work is mixed across files, and splitting it would risk builds that work today); new
work goes in as one commit per row, or as fixups of a row's commit (below), and the next replay can fold those in.
Keep DEV.md and the docs (row 23) last.

Our own files (no conflicts): `gen_track.c`, `seq_tools.c`, `ui_nav.c`, `chord_seq.c`, `ui_chords.c`, `arp_shape.c`
(the SHAPE arp), the copied Tessera files, `drum_sid_gone.c` (the removed SID's level helper for CR78/FM), our tests (`key_nav_test.c`, `gen_test.c`, `gen_track_test.c`,
`seq_tools_test.c`, `nav_test.c`, `arp_shape_seq_test.c`, `arp_shape_stream_test.c`), DEV.md, plan.md, task.md, and
the song sections' files (`song_sect.c`, `ui_sect.h`, `ui_sect.c`, `ui_sect_draw.c`, `ui_sect_sheets.c`,
`tests/song_sect_test.c`), `tools/upstream_shadow.py`, the docs with their generators: `docs/SHORTCUTS.md`, `docs/NAVIGATION.md`, `docs/shortcuts/` (`tools/gen_shortcuts.py`),
`docs/song-guide/` (`tools/gen_song_guide.py`), `docs/design/mock/song_workflow_proposal.svg`. After a replay, run
both generators with `--check`: a control upstream moved shows as a stale picture only if we change the text.
Our hooks in upstream files are calls into our files, so a conflict shows what it belongs to: the UI's call
`nav_*()` functions (`ui_nav.c`), the generators' `gn_*()` (`gen_track.c`), the pattern tools' `seqt_*()`
(`seq_tools.c`), the chord sequencer's `cseq_*()` (`chord_seq.c`, `ui_chords.c`), the SHAPE arp's `shape_*()` and
its `shape` state (`arp_shape.c`); M1/M2's sit under `#if MELODEE_KEY_NAV`. To list the chord sequencer's:
`git grep -nE '(^|[^a-z_])cseq_[a-z_]+\(' -- firmware/src ':!firmware/src/chord_seq.c' ':!firmware/src/ui_chords.c'`. To list the UI's:
`git grep -nE '(^|[^a-z_])nav_[a-z_]+\(' -- firmware/src ':!firmware/src/ui_nav.c'` (about 20 lines, plus
`ui.c`'s prototypes).

**Expect conflicts in:** `ui_draw.c` (`draw_head`), `ui.c` (`grid_on`), `ui_input.c`, `ui_sections.c`, `ui_pages.c`
(`pv_grid`, `pv_arp`), `ui_popup.c`, `seq.c`, `params.c`, `chord.c`, `engines.c`, `voice.c`, `eng_drum.c`,
`project.c`, `pattern_store.c`, the song's hooks (`song_chain.c`, `ui_song.c`, `ui_patterns.c`, `ui_popup.c`,
`seq.c`, `ui_song_sound.c`, `ui_song_punch.c`, `editor.c`: one-line hooks now; upstream's song code inside the markers
merges, then needs `tools/upstream_shadow.py`, see "Song sections"), the navigation's (`ui_layer.c` GLO, `ui_menu.c`,
`ui_designer_input.c`, `panel.c`, `seq_notes.c`, `hal/fm1_input.h`: rows 20–22), README, `web/manual/index.html` and
`docs/design/README.md` (our sections in upstream's text). **Deleted upstream files** (`eng_sid.c`, `drum_sid.c`, `eng_trio.c`, the
SID tests): when upstream changes them, keep them deleted (`git rm`), then `git grep -iw sid` for new references. Upstream tests we changed on purpose (when upstream changes one, keep their change and re-apply ours):

- **UI and navigation:** `tests/ui_test.c` (the FM6 / CZ-1 EDIT cycles, the section tests, the general-engine
  fixture after SID, drum keys), `tests/layer_screen_test.c` (the EDIT map test), `tests/seq_notes_test.c` (drum
  roll tests run with Drum view Notes), `tests/ui_render.c` (our renders), `tests/settings_test.c` and
  `tests/hostsim.c` (the M1/M2/M3 stand-ins), `tests/no_effects_test.c`.
- **SID and TRIO removed:** deleted `sid_test.c`, `sid_filter_test.c`, `drum_sid_test.c`, `drum_sid_fast_test.c`;
  five engine categories instead of six in `audio_admission_test.c`, `audio_matrix_test.c`,
  `audio_batch_profile_test.c`; SID fixtures replaced in `drum_test.c` (two kits), `midi_insert_test.c`,
  `mod_test.c`, `mpc_octave_test.c`, `performance_tools_test.c` (NOISE, with `MELODEE_LEGACY_EXTRAS`),
  `resources_test.c`, `song_preset_test.c` (including rc.6's new native pattern snapshots), `drum_kits_test.c`
  (CR78/FM factory indices 2/3, four presets; registered callbacks and CR78's descriptor), `tuning_test.c`, `voice_weights_test.c` (WEIGHTS: P5 CZ-1 FM6 DRUM);
  the SID's flags and kernels out of `performance_defaults_test.py`, `target_budget.py` / `.txt`,
  `cpu_baseline.txt`; `golden.txt` / `golden_retired_extras.txt` without the SID renders;
  `recorded_note_wire_test.c` (its frozen hash without the SID workload: the other 144 cases unchanged).
- **Our features:** `chord_test.c` (Humanize; the drum track's key is a black key), `seq_edit_test.c` (chord
  entries, their preview), `upreset_test.c` (slot 45), `project_test.c` (shape defaults; the ten-byte snapshot still
  16,456 B), `ble_rf_replay_test.py` (the stored-calibration measurement), `web/test_web.mjs`.
- **Song sections:** `song_mode_test.c` (upstream's, kept verbatim, not run: `song_sect_test.c` replaces it),
  `song_preset_test.c` and `song_punch_test.c` (per-section
  sounds and punches, no held blocks), `ui_test.c` (SONG keys, live launch instead of the jam log, `test_chain` on
  section lengths; `ui_power_on` resets `sp` and `live`), `ui_render.c`, `editor_test.c` (repeats become a length),
  `project_stream_test.c` (valid sections in its oracles), `pattern_test.c`, `midi_track_test.c` (one-beat sections).
- **FX pages (SHARED first):** `fx_test.c` (the FX button opens SHARED, OCT− on a rack goes home, NOTE FX titles),
  `midi_insert_test.c`, `midi_knob_test.c`, `midi_track_test.c`, `ui_render.c` (the track rack is "NOTE FX").
- **rc.6 trial:** `ui_test.c` `test_patterns_queue` (upstream's page checks: ours shows all 16, no `pt_page`),
  `drum_kits_test.c` (CR78 and FM are presets 2 and 3, four presets, the engine's `.cap` / `.desc`),
  `song_preset_test.c` (no SID engine in the native-snapshot cases).
- **GLO and navigation (rows 20–22):** `ui_test.c` (GLO's cycle and its SELECT list, `test_tempo_select`; the EDIT
  cycles end on their last page, `engine_cycle` clears `fam_last`; the EDIT map's KNOB 1 / KNOB 2 swapped; the section
  page's length on PRESETS; FM6 STORE moved with KNOB 2; `test_navigation_rules`), `layer_screen_test.c` (a GLO tap
  locks, GLO SELECT on its main page, the EDIT map's encoder roles, SELECT stopping at a section's ends),
  `scale_picker_test.c` and `fx_test.c` (SELECT stops at the last page), `settings_test.c` and `hostsim.c` (the
  M-key stand-ins: `FM1_NAV_ACTION`, the white keys, `fm1_input_nav_free`).

`git log <upstream tag>..HEAD` shows exactly what we change. When upstream absorbs one of
our changes, drop that commit; upstream fixed our web editor port and favorites fixes
in 1.5.1.

### Branches

- `main`: an exact mirror of `upstream/main`. Never commit to it.
- `tj/<upstream version>` (for example `tj/1.5.0`, `tj/1.6.0`): our stack on that release. A
  new upstream release gets a new branch. Never force-push a branch that firmware was built
  from, so every build stays reproducible.

### One-time setup

```sh
git remote add upstream https://github.com/keremimo/melodee.git   # (already done)
git config rerere.enabled true    # git remembers conflict resolutions and reuses them
```

rerere fills a reused resolution into the file but leaves it unstaged: review it before `git add`, since the code
around it may have changed. `git rerere forget <file>` drops a wrong one. Its memory is `.git/rr-cache`, local to
this clone and never pushed: copy it along if you replay from another machine or a fresh clone.

### For each upstream release

Only replay onto stable release tags. Release candidates come several a day; try one in
a throwaway branch if needed (rerere then remembers its resolutions for the final release).

```sh
git fetch upstream --tags
git switch main && git merge --ff-only upstream/main && git push origin main

git switch -c tj/1.6.0 tj/1.6.0-rc.5  # a copy of our current stack
git rebase --onto v1.6.0 v1.6.0-rc.5  # replay our commits onto the new release
# for each conflict: edit the file, git add <file>, git rebase --continue
# give up and start again: git rebase --abort

./build.sh && tests/run_tests.sh            # one build, M1/M2/M3 included
git push -u origin tj/1.6.0
```

Then update `MELODEE_VERSION` in `firmware/src/melodee.c` to the new base (for example
`v1.6.0`, and `v1.6.0-nonav` for the comparison build), build `build/melodee.fwsc`, and test on the device.

To tidy the stack (squash a fix into its feature commit, reorder, reword):
`git rebase -i <upstream tag>` on a branch nobody builds from yet. A fix to a row is committed as
`git commit --fixup=<the row's commit>`; `GIT_SEQUENCE_EDITOR=: git rebase -i --autosquash <upstream tag>` folds every
fixup into its row with no editor. With Claude Code, the `fork-commit` skill (`.claude/skills/`) commits a change with
its row and the checks, and the `upstream-replay` skill walks this section for a new release (CLAUDE.md).

### What to check after every replay

A clean rebase does not mean a working build, and a clean merge inside our markers means upstream's change reached
nothing of ours. First run `python3 tools/upstream_shadow.py --from <old tag> --to <new tag>`: every item it lists
is upstream code we replace that changed; port it to our file, or note in the replay paragraph why it does not apply
(it exits 1 while items are listed; `--list` shows every marked block). Then these, which broke silently in the trial
replay onto 1.6.0-rc.4, with no conflict shown:

- **Static RAM:** the `.data + .bss` bank (98,304 B) can overflow when upstream grows. The
  link fails with "region `RAM' overflowed". Move our larger state into the zeroed pool
  with `__attribute__((section(".pool")))`, as upstream does for its own buffers (see "Memory").
- **Flash:** the image must stay under 577,248 B (the commit-marker sector, not the 581,564 B slot); the build
  prints the image size and fails with "app body overlaps the commit-marker sector".
- **Our footprint:** compare against plain upstream ("Our footprint and budget" under "Memory"). If upstream inlines
  something new into the audio path that calls our code, the resident audio code line shows it. Under about 4 KB of
  flash left: trim, or add a `MELODEE_GEN` flag (plan.md, [3.3 Step 3: feature flags](plan.md#33-step-3-feature-flags-deferred)).
- **Moved upstream functions:** a hook we added to an upstream function is lost if
  upstream moves that function to another file. Example: `param_kept()` moved from `ui.c`
  to `params.c` in 1.6.0, which would make Humanize reset on sound loads. Re-add our lines
  where it now lives (`git grep` the function name).
- **The project record:** our generator block sits in the FUN22 record's spare tail. A
  `_Static_assert` in `project.c` fails the build if it no longer fits. Check that the
  assert measures what upstream now stores. In 1.6.0 the record's song chain became
  `chain_legacy_t`, so measure the field, not a type name.
- **Parameter slot 45** (`P_HUMVEL`): it must still be a retired slot upstream
  (`P_RESERVED45` in `core.h`), and `P_COUNT` changes need the stored-layout handling
  described in "Humanize velocity" above.
- **Pages and families:** GEN and GEN 2 sit in the ARP round (`params.c` `PAGES`).
- **Web editor:** when both sides changed the same code, prefer upstream's version and
  keep only our additions (the `HVEL` parameter).
- **Copied Tessera files:** `gen.c`, `gen.h` and `gen_grids.h` stay byte-identical to
  Tessera.
- **SHAPE arp:** `AM_SHAPE` is 7, the value after upstream's last arp mode; if upstream adds a mode at 7, ours must
  move (and stored songs with 7 migrate). Its hooks in `arp_step` (the step gate before the note list, the sorted list,
  no PROB) and `keyboard_block` (`song.grid` 3: white keys silent) must survive upstream changes there. Its storage:
  `project_t.shape` after `gen[]`; the old ten-byte snapshots' size excludes both (`proj_import`,
  `tests/project_test.c` keeps 16,456 B). On flash the SHP1 block sits in the last 44 bytes before a bank record's
  checksum, in the FBKL family only: FBKP, or FBKQ with pattern presets (`pattern_room` keeps its steps clear of the block, a
  `_Static_assert` checks its fixed fields end before it), and FBKL–FBKO from our rc.4 build (four tracks too: the
  validator accepts that with the magic). The dense FBKK fallback runs its tail to byte 27,884 and never has it: it
  loads the default shapes. Upstream refuses a record with SHP1 data where it expects zeros.
- **Song sections:** the song keeps upstream's per-row arrays (`chain_config`, `chain_patterns`) as its columns and
  adds a section id and a length per column; `sect_sync` keeps every column of a section identical, so the
  sequencer, song sounds, punches and FBKP/FBKQ's song block read rows as upstream does. Upstream changes to SONG editing
  (`ui_song.c`), row timing (`chain_tick`: ours counts the section's length, never track 1's loop), `CHAIN_HOLD`,
  repeats or the jam log (`seq.c`, removed) conflict with ours: replay them as sections, and load their new fields
  through `song_sections_normalize`. SEC1 sits in the 196 bytes before SHP1 (`BANK_SECT_OFF`, kept clear by
  `pattern_room`); upstream refuses a record with it, as with SHP1.
- **Chord sequencer:** entries live in note bytes 1..3 of a one-note step; upstream code that leaves stale bytes
  there must clear them (`step_clear`, `seq_entry_notes`). The DRUM engine is the only general engine left: tests
  that need one use a legacy engine with `MELODEE_LEGACY_EXTRAS`.
- **UI hooks** (`ui_nav.c`): the `git grep` above lists every `nav_*()` call in upstream files; each must still sit
  where it was (`page_scroll`, `open_family`, `sec_build`, `sec_on`, `sec_go`, `sec_turn`, `smap_pages`,
  `head_chip_text`, `draw_head`, the PRESETS knob, the momentary keep, the main loop). Things that change silently:
  - page titles: `native_titles()` in `ui.c` (our CZ-1 list is per envelope, e.g. "C1 PIT ") and `SEC_RULE` in
    `ui_sections.c` find pages by title; a renamed upstream page drops out of ENV / LFO or its section
  - `draw_head()` in `ui_draw.c`: our page position is drawn right after the REC mark (`nav_head_pos`), the old
    page dots are not called (`head_pages` unused); a new item upstream adds to the header may need room from it
  - the momentary keep (`OCT+` in `ui_input.c`, `momentary` in `ui.c`): `nav_keep()` sits next to it
  - `tests/nav_test.c` and `tests/key_nav_test.c` catch most of these (the suite builds with M1/M2/M3, as the
    firmware does; `MELODEE_KEY_NAV=0 tests/run_tests.sh` also checks upstream's keyboard)

Our host tests catch most of these; the RAM, flash and record checks fail the build itself.
Still do a device pass for each new base. Upstream changes can affect timing, audio and
storage in ways the host tests cannot see.

### Contributing upstream

Every feature keremimo accepts is one less commit to carry. Small, tested, documented
features (Humanize, pattern tools) are the best candidates. Open a pull request from a
branch with just that commit on top of `upstream/main` or `upstream/next`.

## Troubleshooting

| Message | Fix |
| --- | --- |
| `build.sh: Docker is not running` | `open -a Docker`, wait for it to start |
| `... Pillow has no Raqm` | `brew install fribidi`; `build.sh` already points `DYLD_FALLBACK_LIBRARY_PATH` at `/opt/homebrew/lib` |
| `error: externally-managed-environment` from pip | use the venv above |
| `no toolchain in ...` / `no JieLi AC79 SDK in ...` | run setup steps 3 and 4, or set `JIELI_TOOLCHAIN` / `AC79_SDK` |
| `run ./build.sh first` from the tests | build first; tests need `build/` |
