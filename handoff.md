```
Project: melodee — custom FM-1 (AC790x) music workstation firmware
Path: /Users/tjaved/personal/melodee
Branch: tj/1.6.0-rc.5

## Goal

ARP > MODE SHAPE, as the user redefined it on 2026-10-27: not WaveLoop's 8-row SHAYPE any more, but a rhythm for the
arp. Each step (STEPS 1..64) is ON or OFF; an ON step plays the next held note in DIR's order (UP DN UPDN RND), an OFF
step rests and does not use up a note. The keyboard always plays; holding M3 (F#5) or OCT- on the ARP page
turns the 16 white keys into steps 1..16 of the page PRESETS shows (tap toggles, ON steps light). The old modes stay.

## Current State

- Replayed onto upstream v1.6.0-rc.5 on branch `tj/1.6.0-rc.5` (2026-10-27); `tj/1.6.0-rc.4` is the old stack. Not
  pushed.
- SHAPE on rc.5: SHP1 lives in FBKP records (rc.5 always writes FBKP; `pattern_room` reserves its 44 B), FBKL..FBKO
  from our rc.4 build still load with their shapes, the dense FBKK fallback loads defaults.
- Tests: full host suite passes, standard and `CC="cc -DMELODEE_KEY_NAV=1"`.
- Sizes: image 551,792 B (nav 552,392 B), 25.4 KB under 577,248; bank 96,856 B (1.4 KB left); pool +1,100 B over
  upstream; audio arena 97,568 B. (Since 2026-10-28 there is one package, build/melodee.fwsc, with M1/M2/M3 built
  in; current sizes: DEV.md "Memory".)

## What's Left

1. Hardware pass (build/melodee.fwsc): SHAPE plays as UP at first; hold M3, tap white keys, the lights; rests;
   DIR; STEPS > 16 with PRESETS pages; the keyboard plays normally without M3; OCT- in the standard build; save/load.
2. Push `tj/1.6.0-rc.5` (ask first).

## Context

- Project: melodee, custom FM-1 (AC790x) firmware, /Users/tjaved/personal/melodee, branch tj/1.6.0-rc.5.
- Build: ./build.sh, one package with M1/M2/M3 built in (build/melodee.fwsc, the flashed one). Host suite:
  tests/run_tests.sh (compiles with M1/M2/M3 too).
- Attribution (AGENTS.md): new code is Kerem Kilic (Ellic Studio); arp_shape.c no longer holds Felucca code.

## Key Files

- firmware/src/arp_shape.c — SHAPE's state and helpers (shape_default, shape_steps, shape_on, shape_mode, pack/unpack)
- firmware/src/seq.c — arp_step (the step gate), keyboard_block (song.grid 3)
- firmware/src/params.c — N_AMODE, page_desc (ARP 2: STEPS, DIR)
- firmware/src/ui.c, ui_input.c, ui_name.c, ui_pages.c — editing (M3 / OCT-), lights, PRESETS pages, pv_shape
- firmware/src/project.c + pattern_store.c + project_stream.c — storage (SHP1 in wide records)
- tests/arp_shape_seq_test.c, tests/arp_shape_stream_test.c

## Gotchas

- Everything is one translation unit: melodee.c includes seq.c/params.c/ui*.c; hostsim.c includes the same for tests.
  trk_index() is static in seq.c; params.c uses (uint32_t)(TSEL - trk).
- arp_step runs in the audio interrupt (target budgets in tests/target_budget.txt); the UI flips a step with IRQs off.
- The SHP1 block sits after the bank tail's "must be zero" scan, in wide records only: the narrow FBKK layout runs to
  byte 27,884 and would be overwritten. A four-track song with its own shape saves as FBKL (upstream refuses it).
- FM1 flash limit 0x8C000 + 0xEE0 = 577,248 B.
- Committed and passing: SID/TRIO removal, chord sequencer and preview, drum black-key lanes, SHAPE, and M3 held
  turning F5 / G5 back into keys (folded into the F#5-as-M3 commit).
```
