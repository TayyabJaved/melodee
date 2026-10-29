# Melodee (our fork): working on this repo

@AGENTS.md

Melodee is custom firmware for the M-VAVE FM-1 (JieLi AC79, pi32v2 core), written in C, with a C / Python / web host
test suite. This repo is **our fork** of [keremimo/melodee](https://github.com/keremimo/melodee): our features ride as
a stack of commits on top of an upstream release tag, and [DEV.md](DEV.md) is the map of that stack. Read DEV.md's
"Following upstream releases" before touching history, and the feature's own section in DEV.md before changing it.

## Build and test

```sh
source ~/.cache/melodee-venv/bin/activate   # the tests call python3 directly; tools need the venv's packages
./build.sh                                  # ~18 s; always before the tests (they read build/gen and build/*)
tests/run_tests.sh                          # ~100 s; must end with ALL HOST TESTS PASSED
```

- One package only: `build/melodee.fwsc`, the M1/M2/M3 key layer included (`MELODEE_KEY_NAV` defaults to 1).
  Some test variants build with `MELODEE_KEY_NAV=0`: guard test code that touches `fm1_nav` with `#if MELODEE_KEY_NAV`.
- The UI render job lints every screen in every palette: it must report **0 lint findings**
  (`build/ui_new/report.txt`, PNGs in `build/ui_new/<PALETTE>/`).
- To run one test, copy its line from `tests/run_tests.sh` (`$FW` is `-w -Ibuild/gen -Ifirmware/src`), e.g.
  `cc -O1 -DMELODEE_KEY_NAV=1 -DMELODEE_USB_AUDIO=1 -w -Ibuild/gen -Ifirmware/src tests/ui_test.c -o /tmp/ui_test -lm`.
- `GOLDEN_UPDATE=1` / `BUDGET_UPDATE=1` only when a change is meant to alter sound or cost; never to silence a failure.
- Known flaky under load: `tests/audio_cpu_take_test.py` and the CPU sequence runner (timing checks). If only they
  fail, rerun them alone (`python3 tests/audio_cpu_take_test.py`); report it rather than ignoring it.
- Doc pictures are generated: `python3 tools/gen_shortcuts.py` (docs/shortcuts/) and `python3 tools/gen_song_guide.py`
  (docs/song-guide/). Edit the generators, never the SVGs; `--check` lists stale files.
- Flashing is the user's: `! tools/flash.sh --no-build` (back up the device first). Nothing here is verified on
  hardware until they say so.

## Code

- Match the surrounding code: its terse comment style (what and why, in short clauses), naming, and idiom. New files
  carry the SPDX line and `Copyright (C) 2026 Kerem Kilic (Ellic Studio)`; keep Leo Kuroshita's notice on retained
  Felucca code (AGENTS.md).
- Keep a feature in our own files with thin, commented hooks in upstream files (`ui_nav.c` is included at the end of
  `ui_input.c`; its `nav_*()` calls are the UI hooks; song sections live in `song_sect.c`, `ui_sect*.c`). Fewer
  upstream lines touched means easier replays.
- Never rewrite or delete upstream code in place. Keep it verbatim inside `#ifdef MELODEE_UPSTREAM_<FEATURE>` (never
  defined), put ours in our file, and leave a hook tagged with our file's name (DEV.md "Replacing upstream code").
  `tools/upstream_shadow.py` then lists upstream's later changes to that code at each replay, so they get ported.
- Target limits: no 64-bit divide on the target (`__udivdi3` link error; use `uint32_t` or `chain_div_samples`),
  canvases are at most 64 rows (`cv_begin`), static RAM (`.data+.bss`) is nearly full: put larger state in the zeroed
  pool, `__attribute__((section(".pool")))`. The build prints image size and RAM; read them after a change.
- Watch multi-line comments next to code: a `/* ... */` that wraps onto the next code line can swallow a statement.
- When the HAL (`firmware/hal/fm1_input.h`) gains a function or field, update the host stand-ins in
  `tests/hostsim.c` and `tests/settings_test.c`.
- UI status follows docs/design/README.md "Status indicators": ongoing state in the shared header, toasts for brief
  explanations, dialogs for decisions. Navigation follows [docs/NAVIGATION.md](docs/NAVIGATION.md): SELECT sideways
  (pages) and stopping at the ends, PRESETS the bigger step and the rows, KNOB 2 "which one", KNOB 1 "its value".

## Docs to keep in step

A behaviour change usually touches several of: `web/manual/index.html` (the manual), `docs/SHORTCUTS.md` and its
generator, `docs/NAVIGATION.md`, `docs/song-guide/` (its generator), `docs/design/README.md`, `README.md`, the release
notes (`docs/RELEASE_*.md`), and DEV.md (the feature's section and the fork table). Write for the user of the device:
plain words, what happens on screen.

## Git

- Commit or push only when the user asks. Never commit to `main` (a mirror of `upstream/main`). Never force-push a
  `tj/*` branch that firmware was built from: a new upstream base gets a new branch.
- **Committing our work: use the `fork-commit` skill.** It runs the checks and records the commit in DEV.md's fork
  table, so the stack stays replayable.
- **Pulling a new upstream release and replaying our stack: use the `upstream-replay` skill.**
- Releases are built and attested only by `zvenson/fm1-build/.github/workflows/build.yml@v1` (BUILDING.md →
  Releases). Never substitute a local build for a release package.
