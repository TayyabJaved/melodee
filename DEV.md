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
- Tests: `tests/gen_test.c` (the copied generators) and `tests/gen_track_test.c` (sequencer, Print, storage, page),
  plus the `gen_*` UI renders.

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

`MELODEE_KEY_NAV=1 ./build.sh` builds the keyboard navigation layer (version
`v1.6.0-rc.4-nav`): M1 (F5), M2 (G5) and M3 (F#5, alone) with the black keys press panel buttons, and F#5 keeps a
momentary edit. Key mapping: [BUILDING.md → Build options](BUILDING.md#build-options); the code is in
`firmware/hal/fm1_input.h` (`FM1_NAV_*`, three rows), the rows in `panel.c` `panel_nav()`, the keep in
`ui_nav.c` `nav_keep()`. The host stand-ins for the HAL are in `tests/hostsim.c` and `tests/settings_test.c`.

Save that package under its own name so `build/melodee.fwsc` stays the standard build, then
install it:

```sh
MELODEE_KEY_NAV=1 ./build.sh && cp build/melodee.fwsc build/melodee-nav.fwsc
tools/flash.sh build/melodee-nav.fwsc
```

`flash.sh` with a package path installs that file without rebuilding. It works from
Melodee, the official V15 or TransitFM1 (which answers as `FM-1_015`). Run
`tools/flash.sh --info` first to check that the FM-1 is detected.
`tests/key_nav_test.c` covers it on the host. To compile the rest of the host suite with
the flag as well, run `CC="cc -DMELODEE_KEY_NAV=1" tests/run_tests.sh` after a flag-on
build. Without the flag, the M1/M2 code is compiled out entirely.

### Humanize velocity (our mod)

SCL list > **Humanize** (OFF / LOW / HIGH) is a regular track setting, not behind a
flag. It reuses the retired parameter slot 45 (`P_HUMVEL` in `firmware/src/core.h`), so
no storage format changes. Tests: the `humanize` checks in `tests/chord_test.c`, the
slot-45 checks in `tests/upreset_test.c`, `web/test_web.mjs`, and the
`scl_humanize` UI render.

Release packages must come from the attested `zvenson/fm1-build` workflow on a tag; never
publish a local `--release` build. See [BUILDING.md → Releases](BUILDING.md#releases).

## Where things are

| Path | What |
| --- | --- |
| `firmware/src/` | application. `melodee.c` is a unity build that `#include`s the other sources |
| `firmware/src/eng_*.c` | synth engines (FM6, CZ, Prophet, DRUM, PHYS, NOISE, SID, …) |
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

Measured on the standard build of `tj/1.6.0-rc.4` (1.6.0-rc.4 plus our stack). The nav build is within a few
dozen bytes of it.

| Region | Size | Used | Left | What lives there | When it is exceeded |
| --- | ---: | ---: | ---: | --- | --- |
| Flash app slot | 581,564 B | 575,220 B | 6,344 B | code, `const` data, the compressed SRAM images | packaging fails: "app is N B, the slot is 581564 B" |
| Low SRAM (`RAMTEXT`) | 32,512 B | 30,760 B | 1,752 B (to the arena) | code that runs while flash is busy, DSP kernels and tables | link fails |
| **Bank** (`.data` + `.bss`) | 98,304 B | 98,232 B | **72 B** (to the arena) | ordinary globals | link fails: "region `RAM' overflowed" |
| **Pool** region | 357,888 B | `.pool` 111,292 B, resident audio code 128,584 B, CPU1 stacks 4,096 B | arena 113,920 B | `.pool` buffers, audio code, then the audio working arena | build fails if the arena drops under 49,152 B or the pool's headroom under 8,192 B |
| Foreground stack | 12,288 B | 9,608 B deepest path + 2,048 B required margin | **632 B** | the main loop (UI, storage) | build's stack audit fails |
| Interrupt stack | 6,144 B | 4,500 B (audio 2,972 + BLE 716 + timer 220 + fault 464 + 128) | 1,644 B | interrupts, nested | build's stack audit fails |
| CPU1 stack | 3,072 B | 1,828 B + 768 B guard and margin | 476 B | the second core's renderer | build's stack audit fails |
| Retained (`NOINIT`) | 15,664 B | all but 48 B | 48 B (to the arena) | state that survives resets: panel calibration, settings, retained patterns | link fails |
| Cache RAM | 28,672 B | given to audio after the boot self-test | | | |

- **Bank:** ordinary globals. With 72 B left, anything that grows breaks the link.
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
- **No large local arrays in main-loop code.** The foreground stack has 632 B left beyond its margin. Use a static
  pool buffer instead. Locals in audio-path code count against the interrupt stack.
- **Nothing of ours goes in `NOINIT`** (state that survives resets) unless it must.
- After each build, read the `ok` lines, and build the nav variant too.

### Largest symbols

Same build as the limits above.

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
| `chain`, `chain_config` | 1,600 + 1,412 | the song chain | `song_chain.c` |
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

**Ours:** `gn` (generators) 736 B and `fm1_nav` (M1/M2, nav builds only) 60 B, both in the pool. See the budget
below.

To list them again after a build (sizes in hex; `.L_MergedGlobals` entries are the compiler's bundles of small
variables, overlapping the named ones):

```sh
nm -S -n build/melodee.elf | awk '$1>="01c08000" && $1<"01c20000"' | grep -v MergedGlobals | sort -k2,2r | head -20   # bank
nm -S -n build/melodee.elf | awk '$1>="01c20000" && $1<"01c77600" && $3 ~ /^[bBdD]$/' | sort -k2,2r | head -20       # pool
```

### Our footprint and budget

What our stack adds on top of plain upstream (standard build; nav in brackets):

| | Upstream 1.6.0-rc.4 | With our stack | Ours | Our budget |
| --- | ---: | ---: | ---: | ---: |
| Flash image | 552,556 B | 575,220 B | +22,664 B (+23,016 B) | 20,000 B + the BLE fix |
| Bank | 98,184 B | 98,232 B | +48 B (+64 B) | 64 B |
| Pool (`.pool`) | 110,360 B | 111,292 B | +932 B (+992 B) | 4,096 B |
| Resident audio code | 128,232 B | 128,584 B | +352 B | 1,024 B |
| Audio arena | 115,200 B | 113,920 B | −1,280 B (−1,568 B) | |

About 5.1 KB of the flash, 16 B of the bank and 184 B of the pool are the BLE calibration fix (upstream PR #90),
which leaves our stack once upstream merges it; our own features are ~17.5 KB of flash.

- **Bank:** what is left of ours is 16 B of far-call pointers that `audio_ram.py` generates (they must be loaded
  data), `seqt`'s 2 B (it starts non-zero) and alignment. Upstream itself has only ~120 B spare there.
- **Flash** is the budget that limits new features: 6.3 KB is left (6.0 KB in the nav build) for upstream's growth
  and ours together.
  Prefer `static const` tables, short strings and small pictures (`pv_gen` alone is 1.8 KB), and reuse upstream
  helpers instead of copying them.
- Before this setup, the generators were inlined into the sequencer tick: 6.9 KB of resident audio code, and `gn` in
  the bank overflowed it.

Measure it at every replay, and after adding anything big. Build plain upstream in a worktree and compare the `ok`
lines, then the symbols:

```sh
git worktree add --detach ../melodee-upstream v1.6.0-rc.4
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

## Following upstream releases

This repo is a fork of [keremimo/melodee](https://github.com/keremimo/melodee). The remotes
are `origin` (this fork) and `upstream` (keremimo). Upstream's `main` carries stable
releases; its `next` carries release candidates (tags such as `v1.6.0-rc.4`).

Our approach: **start from upstream's latest release and replay our changes onto it.** Our
work is a stack of commits kept on top of an upstream release tag, in this order (`git log --reverse
<upstream tag>..HEAD`):

| # | Commit | What it changes | Upstream files it hooks into | Drop when |
| --- | --- | --- | --- | --- |
| 1 | M1/M2 key navigation (`MELODEE_KEY_NAV`) | F5 / G5 + black keys press panel buttons | `hal/fm1_input.h`, `panel.c`, `ui_input.c`, `melodee.c`, `tools/build.py`, `BUILDING.md`; host stand-ins in `tests/hostsim.c`, `tests/settings_test.c` | never (ours) |
| 2 | Humanize velocity | SCL > Humanize (OFF / LOW / HIGH), parameter slot 45, web editor `HVEL` | `core.h`, `params.c` (`param_kept`), `seq.c`, `midi_control.c`, `motion.c`, `project.c`, `upreset.c`, `ui_list.c`, `web/editor.html`, `web/EDITOR_PROTOCOL.md`; tests | upstream takes it |
| 3 | Generators (ARP > GEN) | Tessera's generators on tracks 1–4, GEN / GEN 2 pages, the project's `'G'` block | new `gen*.c/h`, `gen_track.c`; `seq.c` (one line), `params.c`, `project.c`, `project_stream.c`, `tracks.c`, `ui.c`, `ui_draw.c`, `ui_input.c`, `ui_pages.c`, `ui_popup.c`, `tools/audio_ram.py` (`XIP`) | never (ours) |
| 4 | Pattern tools | Double, Half, Euclid, Euclid turn in the pattern sheet | new `seq_tools.c`; `ui.c` (one include), `ui_popup.c` | upstream takes it |
| 5 | BLE: measure the radio calibration … (Charles Vestal, upstream PR #90, unchanged) | the radio measures its own calibration | `hal/fm1_ble.h`, `hal/fm1_ble_rf.h`, new `hal/fm1_rf_cal.h`, `midi_ble.c` | upstream merges a fix that works on this unit (check on the device) |
| 6 | BLE: measured calibration with a stored one too | measure even with a stored calibration; PR #90's buffers in `.pool`; host stub | `hal/fm1_ble_rf.h`, `midi_ble.c`, `tests/hostsim.c` | with 5 |
| 7 | UI: Bluetooth in the header only on its page | status only on Settings > Bluetooth, toasts, the BLUETOOTH row's state; adds `ui_nav.c` | `ui_draw.c`, `ui_menu.c`, `main.c` (one line), `ui_input.c` (includes `ui_nav.c` at its end) | never (ours) |
| 8 | UI: ENV, LFO and EDIT pages, one home each | an engine's envelope / LFO pages are ENV's / LFO's only; TRACK LFO names | `ui.c` (`native_titles` CZ list, `page_scroll`, `open_family`), `ui_sections.c`, `ui_input.c` (PRESETS), `ui_draw.c` | never (ours) |
| 9 | UI: page position as "2/4" | "k/n" after the BPM on every page | `ui_draw.c` (`draw_head`) | never (ours) |
| 10 | UI: PRESETS shows the section's name | the section's name in the header | `ui_sections.c` (`sec_turn`) | never (ours) |
| 11 | UI: PRESETS opens a section's first page …; no save page in EDIT | first page on a section jump, the name in the page name's place, STORE pages out of EDIT | `ui_sections.c`, `ui_draw.c`, `ui.c` | never (ours) |
| 12 | M1/M2: F#5 as M3 … and keep | M3 + C#5 / D#5 = OCT− / OCT+, F#5 keeps a momentary edit | `hal/fm1_input.h`, `panel.c`, `ui_input.c` | never (ours) |
| 13 | DEV.md, plan.md | this file, the plan, task.md, README's sections on our features | `README.md` | never (ours); keep it last |

Our own files (no conflicts): `gen_track.c`, `seq_tools.c`, `ui_nav.c`, the copied Tessera files, our tests
(`key_nav_test.c`, `gen_test.c`, `gen_track_test.c`, `seq_tools_test.c`, `nav_test.c`), DEV.md, plan.md, task.md.
Our hooks in upstream files are calls into our files, so a conflict shows what it belongs to: the UI's call
`nav_*()` functions (`ui_nav.c`), the generators' `gn_*()` (`gen_track.c`), the pattern tools' `seqt_*()`
(`seq_tools.c`); M1/M2's sit under `#if MELODEE_KEY_NAV`. To list the UI's:
`git grep -nE '(^|[^a-z_])nav_[a-z_]+\(' -- firmware/src ':!firmware/src/ui_nav.c'` (16 lines in 1.6.0-rc.4, plus
`ui.c`'s prototypes).

**Expect conflicts in:** `ui_draw.c` (`draw_head`), `ui.c`, `ui_input.c`, `ui_sections.c`, `seq.c`, `params.c`,
`project.c` and README. Upstream tests we changed on purpose: `tests/ui_test.c` (the FM6 / CZ-1 EDIT cycles, the
section tests), `tests/layer_screen_test.c` (the EDIT map test), `tests/ui_render.c` (our renders): when upstream
changes those tests, keep their change and re-apply ours.

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

### For each upstream release

Only replay onto stable release tags. Release candidates come several a day; try one in
a throwaway branch if needed (rerere then remembers its resolutions for the final release).

```sh
git fetch upstream --tags
git switch main && git merge --ff-only upstream/main && git push origin main

git switch -c tj/1.6.0 tj/1.6.0-rc.4  # a copy of our current stack
git rebase --onto v1.6.0 v1.6.0-rc.4  # replay our commits onto the new release
# for each conflict: edit the file, git add <file>, git rebase --continue
# give up and start again: git rebase --abort

./build.sh && tests/run_tests.sh
MELODEE_KEY_NAV=1 ./build.sh && CC="cc -DMELODEE_KEY_NAV=1" tests/run_tests.sh
git push -u origin tj/1.6.0
```

Then update `MELODEE_VERSION` in `firmware/src/melodee.c` to the new base (for example
`v1.6.0` / `v1.6.0-nav`), build `build/melodee-nav.fwsc`, and test on the device.

To tidy the stack (squash a fix into its feature commit, reorder, reword):
`git rebase -i <upstream tag>` on a branch nobody builds from yet.

### What to check after every replay

A clean rebase does not mean a working build. These broke silently in the trial replay
onto 1.6.0-rc.4, with no conflict shown:

- **Static RAM:** the `.data + .bss` bank (98,304 B) can overflow when upstream grows. The
  link fails with "region `RAM' overflowed". Move our larger state into the zeroed pool
  with `__attribute__((section(".pool")))`, as upstream does for its own buffers (see "Memory").
- **Flash:** the app slot is 581,564 B; the build prints the image size.
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
- **UI hooks** (`ui_nav.c`): the `git grep` above lists every `nav_*()` call in upstream files; each must still sit
  where it was (`page_scroll`, `open_family`, `sec_build`, `sec_on`, `sec_go`, `sec_turn`, `smap_pages`,
  `head_chip_text`, `draw_head`, the PRESETS knob, the momentary keep, the main loop). Things that change silently:
  - page titles: `native_titles()` in `ui.c` (our CZ-1 list is per envelope, e.g. "C1 PIT ") and `SEC_RULE` in
    `ui_sections.c` find pages by title; a renamed upstream page drops out of ENV / LFO or its section
  - `draw_head()` in `ui_draw.c`: our page position is drawn right after the REC mark (`nav_head_pos`), the old
    page dots are not called (`head_pages` unused); a new item upstream adds to the header may need room from it
  - the momentary keep (`OCT+` in `ui_input.c`, `momentary` in `ui.c`): `nav_keep()` sits next to it
  - `tests/nav_test.c` and `tests/key_nav_test.c` catch most of these: run both builds' suites

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
