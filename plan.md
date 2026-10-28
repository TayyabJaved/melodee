# Plan: our features on Melodee

- **Base:** upstream Melodee `v1.6.0-rc.4`
- **Branch:** `tj/1.6.0-rc.4` (ten commits on the upstream tag)
- **Details live elsewhere:** behaviour in [README.md](README.md); building, replaying and memory rules in [DEV.md](DEV.md)

Contents:

1. [Status](#1-status)
2. [Next steps](#2-next-steps)
3. [Memory and flash](#3-memory-and-flash)
4. [Feature candidates](#4-feature-candidates)
5. [Left out](#5-left-out)
6. [Reference](#6-reference)

---

## 1. Status

All features are done and pass the host tests. Only Bluetooth MIDI has been tried on hardware so far.

| Feature | Commit |
| --- | --- |
| M1/M2 key navigation (`MELODEE_KEY_NAV`), OCT- / OCT+ on M2 | M1/M2 key navigation |
| Humanize velocity (SCL: OFF / LOW / HIGH; `HVEL` in the editor) | Humanize velocity |
| Generators TURING, EUCLID, MARKOV, ATLAS, SERIES, BOUNCE / FALL (tracks 1–4) | Generators (ARP > GEN) |
| GEN page: MODE, SEED, CHANGE, EVERY | Generators |
| GEN 2 page: the mode's settings, GATE / MARKOV LOCK / ATLAS CHAOS | Generators |
| Seeds, Print to pattern, the project's `'G'` block | Generators |
| Pattern sheet: Double, Half, Euclid, Euclid turn | Pattern tools |
| Bluetooth MIDI that works on our FM-1: upstream PR #90's measured radio calibration, forced on | BLE: measure… (Charles Vestal), BLE: measured calibration… (ours) |
| UI: Bluetooth only on its page; ENV, LFO and EDIT pages, one home each; page position "2/4" ([task.md](task.md)) | UI: … (three commits) |
| SONG sections: one definition per letter, the section page (pattern per track, AUTO / set length, PLAY loops it), OCT+ on + adds what plays, REC launches live; no jam, repeats or held blocks; older songs converted; SEC1 ([mock](docs/design/mock/song_workflow_proposal.svg)) | SONG: sections (not yet committed) |
| DEV.md, this plan | DEV.md, plan.md |

Bluetooth on our FM-1: with upstream's code alone it scanned and advertised but connected to nothing. With the
calibration measured at radio start, the Mac connects ("Melodee DB18") and MIDI flows both ways (2026-10-26).

The replay onto 1.6.0-rc.4 is done: both builds link and both host test suites pass.

Tessera's planned EVOLVE page became GEN's CHANGE and EVERY knobs plus MARKOV LOCK, so no extra page was needed.

---

## 2. Next steps

In this order.

### 2.1 Device pass

On `build/melodee.fwsc`, the one build (M1/M2/M3 built in since 2026-10-28; before, the separate `-nav` package).
Recovery steps: DEV.md "Installing on the FM-1".

- [ ] M1/M2 rows, OCT- / OCT+; calibration (OCT- + OCT+ at power-on) still works
- [ ] Humanize LOW / HIGH on keys, chords and MIDI IN
- [ ] Each generator at slow and fast tempos stays in time (their code now runs from flash during the audio interrupt)
- [ ] Seeds repeat after PLAY; SEED / CHANGE apply on the bar; EVERY; GATE HOLD; MARKOV LOCK
- [ ] ATLAS on a DRUM track; BOUNCE and FALL
- [ ] Print and its undo; REC refused on a generator track
- [ ] Double, Half, Euclid, Euclid turn and their undo
- [ ] Save, power-cycle, load; a project with generators loads on stock Melodee with its steps
- [ ] A long run with heavy engines and effects (the audio arena is 1.3 KB smaller than upstream's)
- [x] Bluetooth: the Mac connects and MIDI flows
- [ ] Header: "2/4" after the BPM on every multi-page button; Bluetooth only on its page, toasts on connect /
      disconnect, the PIN on every screen while pairing
- [ ] ENV / LFO / EDIT on Prophet, FM6, CZ-1: SELECT stays on the button's pages, the light never jumps, EDIT from
      ENV / LFO goes to EDIT, the map from EDIT; CZ-1 PRESETS jumps envelopes
- [ ] Bluetooth: after a power cycle (radio on, the Mac reconnects); clock from a DAW (CLK BLE); a long session with
      music playing

### 2.2 Replay onto the final 1.6.0

When upstream tags it. Follow DEV.md "For each upstream release"; `rerere` already has this replay's resolutions.
Then update `MELODEE_VERSION` and the footprint table in DEV.md.

### 2.3 Offer features upstream

Each one accepted is one less commit to replay (DEV.md "Contributing upstream"):

- **PR #90 feedback:** our unit had a stored calibration and still needed the measurement; the PR overflows the
  main bank on rc.4 (we moved `fm1_rfc` and `fm1_rfc_x187` to `.pool`); its host tests need `fm1_rf_cal_on` in
  `tests/hostsim.c`
- the `XIP` list in `tools/audio_ram.py`
- Humanize velocity
- the pattern tools

---

## 3. Memory and flash

### 3.1 The four steps

| Step | What | State |
| --- | --- | --- |
| 1 | Our state out of the main RAM bank, into `.pool` (`gn`, `gn_cell`, `fm1_nav`) | **Done** |
| 2 | Generator code out of audio SRAM (`gn_next`, `gn_restart_all` on the `XIP` list) | **Done** |
| 3 | A build flag per feature, so a build can drop one | **Deferred** (see 3.3) |
| 4 | A budget for our footprint, checked at every replay | **Done** (DEV.md "Our footprint and budget") |

Result (without the BLE fix): we add 32 B to the main bank, 744 B to the pool and 356 B of audio code. The audio
arena is 1.1 KB smaller than upstream's (it was 7.6 KB before steps 1 and 2). The BLE fix adds 16 B of bank and
184 B of pool.

### 3.2 Flash per feature

Flash is the limit for new features: **21.2 KB** is left in the app slot (2026-10-28, the one build with M1/M2/M3 and
the song sections), shared with upstream's growth.

| Feature | Flash |
| --- | ---: |
| Generators | ~12.4 KB |
| BLE calibration fix (PR #90; leaves when upstream merges it) | ~5.1 KB |
| Pattern tools | ~2.0 KB |
| UI: header and navigation (`ui_nav.c`) | ~1.2 KB |
| Humanize, M1/M2, other hooks | ~1.5 KB |

### 3.3 Step 3: feature flags (deferred)

**When:** after a replay, less than about **4 KB** of flash is left, or a wanted feature does not fit. With the
BLE fix and the UI changes we are at 6.3 KB, so the next large feature may already need it.

**What it takes:**

- A `MELODEE_GEN` flag frees ~12 KB. It needs `#if` lines in about ten upstream files, which means more conflicts at
  each replay.
- A features-off build that the host tests also run.
- Re-saving a project in a build without generators must keep its `'G'` block.
- A pattern-tools flag is easy (4 hooks) but frees only 2 KB.

### 3.4 Cheaper savings first

Before adding flags, perhaps 1–2 KB:

- Share drawing code among the six GEN pictures (`pv_gen`, 1.8 KB).
- Shorten generator strings.

---

## 4. Feature candidates

After the device pass. Each new feature follows DEV.md "Conventions for our changes" and "Memory": its own file
with thin hooks, state in `.pool`, once-a-step code off the audio path, host tests, a footprint check, a device pass.

| Idea | Cost | Notes |
| --- | --- | --- |
| Humanize on generator notes | Small | One `hum_vel()` call in `gn_next()` |
| ATLAS per-part layer (KICK / SNR / HAT / TOMS on GEN's OCT+ hold) | Medium | From Tessera's ATLAS layer |
| POLY, AWAKE, MORPH generators | Medium each | POLY needs a rhythm knob; AWAKE its own key entry mode; MORPH overlaps EUCLID |
| Generators on MIDI tracks 5–8 | Large | Needs a block in the pattern-bank (FBKL) tail too |
| Generator settings in templates | Medium | A template layout change |
| Web editor support for GEN | Large | New editor commands |

---

## 5. Left out

From Tessera, as decided. Each overlaps a Melodee feature or adds a second system next to one:

- QUAD, CONSTELLATION
- the CHORD generator and QNT CHRD
- ANSWER
- FORM, TWEAK and the other step-logic pages
- mod cords and MATRIX
- the MACRO engine

---

## 6. Reference

### Tessera

`~/Personal/tessera` at `013c883` (Tessera 2.1, Theo Dainis, GPL-3.0-only). `gen.c`, `gen.h` and `gen_grids.h`
are copied byte-identical; new generators come from there.

### Storage

- The `'G'` block: version 4, 25 bytes a track, in the FUN22 record's tail after the `'U'` block. It is written
  only while a generator is on. Versions 1–3 still load, and older firmware ignores it.
- Unchanged: `P_COUNT`, the FBKL layout, user presets, the editor protocol, templates.
- Humanize reuses retired parameter slot 45.

### Attribution (AGENTS.md)

- Theo Dainis's notices on `gen.c` / `gen.h`; Émilie Gillet's on `gen_grids.h`.
- "Portions Copyright" for Tessera-derived code in `seq_tools.c` and the GEN pictures.
- LICENSING.md rows for Tessera, Grids, nørgård and Fall.

### Releases

Our build carries our own identity (`v1.6.0-rc.5`; `-nonav` for the comparison build without M1/M2/M3), never upstream's. Release packages come only from the
attested workflow (AGENTS.md "Releases").
