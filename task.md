# Tasks: UI navigation and header fixes

Three problems found while playing `v1.6.0-rc.4-nav`. All three are upstream behaviour, so each fix edits upstream
files: keep every change small, give each task its own commit, and offer the fixes upstream (DEV.md "Contributing
upstream") so we don't carry them forever.

Contents:

1. [Page position: always "2/4", on every page](#1-page-position-always-24-on-every-page)
2. [ENV, LFO and EDIT: one home per page](#2-env-lfo-and-edit-one-home-per-page)
3. [Bluetooth in the header: only in Settings](#3-bluetooth-in-the-header-only-in-settings)

Suggested order: 3 (smallest), then 1, then 2 (2 changes navigation, and 1 should count what 2 walks).

## Status (2026-10-26): all three done, not yet tried on the device

| Task | Commit | Notes |
| --- | --- | --- |
| 3. Bluetooth in the header | UI: Bluetooth in the header only on its page | As planned |
| 2. ENV, LFO and EDIT | UI: ENV, LFO and EDIT pages, one home each | Done in 2's order of commits, before 1 |
| 1. Page position | UI: page position as "2/4" after the BPM | HOME's "1/2" stays optional (see below) |

Code: `firmware/src/ui_nav.c` (ours), hooks marked "ui_nav.c" in `ui.c`, `ui_draw.c`, `ui_input.c`,
`ui_sections.c`, `ui_menu.c` and `main.c`. Tests: `tests/nav_test.c`.

Where the implementation differs from the plan below:

- **HOME:** its "1/2" is optional context after its octave pill, not the fixed slot. HOME has only two pages, and an
  MPC octave there is the more useful information (an upstream render check expects it).
- **Track LFO names:** "TRACK LFO", "TRACK LFO 2" and "TRACK DEST" are header names only. The pages keep their
  titles, so the sound designer, the tests and the web editor, which find pages by title, are unchanged.
- **MOD's source "LFO"** keeps its name: renaming it would change the editor's tables (`web/editor.html` MSRC) too.
- **Upstream tests changed:** the FM6 and CZ-1 EDIT cycles, the Prophet section tests (now from P5 FILTER, Filter ->
  Mod) and the EDIT map test (from FM6's OUT / SCALE) follow the new pages.
- **SID:** its ENV shows the track ADSR and ENV DEST as before; not changed.

---

## 1. Page position: always "2/4", on every page

### Problem

Turning SELECT through a family's pages (ARP, ARP 2, GEN …), the header sometimes shows dots, sometimes "2/4",
and sometimes nothing.

### Cause

`head_pages()` in `firmware/src/ui_draw.c` gets whatever header space is left after everything else:

- Dots when they fit; "k/n" when they don't; nothing when even "k/n" doesn't fit.
- What is left depends on the BPM's width, the REC mark, the octave pill, the song row, the `BLE` / `HOST` tags,
  the battery glyph and the page name's width. So it changes from page to page and moment to moment.
- When a message, a Bluetooth status ("BLE AVAILABLE", "SCANNING" …), `WAIT LOOP` or `ERASING` is showing, the
  branch that draws the page name and its position is skipped entirely.
- HOME's two pages (Stage, the chord view) have their own dots.

### Solution

- **One format:** "k/n" in the label face (`TY_LB`), never dots. HOME shows "1/2" / "2/2" the same way.
- **A fixed place:** draw it right after the BPM and REC mark, on the left. The right side's status, messages and
  page name then never compete with it.
- **Never dropped:** when the header is crowded, the others give way first, in this order: the octave pill, the
  song row, the page name's face (`TY_TX` → `TY_LB`), then the page name ellipsised.
- **Shown on every page with two or more pages,** including while a status or message is showing.
- **Counts what SELECT walks** (task 2): the same page list for the number and for SELECT.
- **Design doc:** `docs/design/README.md` "Status indicators" lists page dots as optional context that gives way.
  Update it in our fork: the page position is required context, "k/n", on the left.

### Files

- `firmware/src/ui_draw.c`: `head_pages()`, `draw_head()` (layout order, HOME's dots)
- `docs/design/README.md`: "Status indicators"

### Tests

- `tests/ui_render.c`: crowded-header scenes (a 3-digit BPM, REC, an octave pill, a song row, `BLE` and `HOST`,
  low battery, a long page name) on multi-page families. The layout lint must find "k/n" unclipped in each.
- A check that every page of a family with two or more pages draws "k/n", and single-page families draw none.

---

## 2. ENV, LFO and EDIT: one home per page

### Problem

On Prophet, FM6 and CZ-1:

- SELECT on an ENV or LFO page walks on into the other EDIT pages, and the lit button changes as it goes (ENV,
  LFO, EDIT).
- Pressing EDIT on an LFO page opens a vertical menu (the section map) instead of EDIT's pages, and LFO stays lit.

### Cause

- These engines have their own envelope and LFO pages (`native_titles()` in `firmware/src/ui.c`: "P5 FLT ENV",
  "EG RATE", "FM LFO" …). ENV and LFO open those pages, but the pages belong to the EDIT family (`FAM_EDIT`).
- `page_scroll()` (SELECT) walks the current page's family, so from a native ENV page it walks every EDIT page.
- The lit button comes from `cur_fam()` in `firmware/src/ui_input.c`, which picks ENV or LFO by the page's title.
  As SELECT moves, the light jumps between ENV, LFO and EDIT.
- EDIT pressed on any page of an engine that has sections opens the map (`smap_open()`). Native ENV and LFO pages
  count as EDIT pages, so EDIT there opens the map, and LFO stays lit.

### Background: two layers of envelopes and LFOs

Melodee has two layers, and that is where the confusion starts.

**Track layer (Melodee's own, on every track):**

| Page | What it does |
| --- | --- |
| ENV | an ADSR |
| ENV DEST | what the ADSR moves: pitch, filter, shape |
| LFO, LFO 2 | the track LFO: rate, wave, phase, fade, sync, trigger, polarity |
| LFO DEST | what it moves: pitch, filter, shape, amplitude |
| MOD | the modulation matrix (the track LFO is one of its sources) |

**Engine layer (the synth's own controls, part of its sound):**

| Engine | Its envelopes | Its LFO |
| --- | --- | --- |
| Prophet | P5 FLT ENV, P5 AMP ENV, P5 ENV MOD | P5 LFO, P5 WHEEL |
| FM6 | EG RATE, EG LVL (the operators), PITCH EG, PITCH LV | FM LFO |
| CZ-1 | C1 / C2 PIT, WAV and AMP envelopes | CZ VIBRATO |

**How they combine today:**

- **Envelopes: one way per engine.**
  - On Prophet, FM6 and CZ-1 the engine's envelopes shape and end every note. The track ADSR and ENV DEST would
    do nothing, so they are hidden, and ENV shows the engine's envelopes instead.
  - On the other engines, ENV shows the track ADSR.
  - To check: SID also uses its own envelopes, but has no native ENV pages, so its ENV button shows the track
    ADSR. Only ENV DEST seems to work there, following the chip's envelope.
- **LFOs: two at once on Prophet, FM6 and CZ-1.** The engine's LFO (part of the patch, saved with the preset) and
  the track LFO (modulates the engine from outside, through LFO DEST and MOD) both run. LFO shows the engine's
  first, then the track's.
- **The same pages in two places:** the engine's envelope and LFO pages also belong to EDIT. They appear among
  EDIT's sections ("Filter" holds P5 FILTER and P5 FLT ENV; "LFO" holds P5 LFO), and SELECT reaches them from
  EDIT too.

So there aren't two ways to set the same envelope. What's confusing is that the same engine page has two homes
(ENV or LFO, and EDIT), and that the two LFOs look alike.

### Options

**Decided (2026-10-26): option A.**

| Option | ENV / LFO buttons | EDIT | Two LFOs | Verdict |
| --- | --- | --- | --- | --- |
| **A. One home per page** (chosen) | ENV: the envelopes that shape this sound (the engine's, else the track ADSR). LFO: the engine's LFO, then the track LFO, DEST and MOD | The engine's other pages only; its envelope and LFO pages leave EDIT's chain and sections | Kept: the engine's shown as "P5 LFO" …, the track's as "TRACK LFO" | Each page in one place, so the light always matches the page |
| B. The button you pressed owns the pages | As A | All engine pages, envelopes and LFO included | Kept | A page still has two homes; the light shows how you got there. Less change, still two ways in |
| C. ENV / LFO for the track layer only | Track ADSR and track LFO only | All engine pages, envelopes and LFO included | Kept | On Prophet, FM6 and CZ-1, ENV would show nothing useful: the engine's envelopes would be buried in EDIT |
| D. Hide the track LFO on engines with their own | Engine pages only | As A | Engine's only | Loses a feature: the track LFO on amplitude and shape, and as a MOD source |

### Solution (option A, chosen)

- **Each engine page has one home:**
  - its envelope pages: **ENV**
  - its LFO pages: **LFO**
  - everything else: **EDIT**
- **EDIT's chain and sections** skip the engine's envelope and LFO pages. Prophet's "Filter" section is then
  P5 FILTER only, and its filter envelope is under ENV. Sections left empty disappear.
- **The track LFO stays** on every engine: it is an extra, useful on top of a synth's own LFO.
- **LFO pages: the same order and naming on every engine,** each page saying whose it is:
  1. the synth's own LFO pages, if it has them, by their full title: **P5 LFO**, **P5 WHEEL**, **P5 WHEEL 2**,
     **FM LFO**, **CZ VIBRATO**. Today the header shortens them to "LFO" (`sec_chip()` strips "P5 " and "FM "),
     the same as the track's page.
  2. the track LFO, on every engine: **TRACK LFO**, **TRACK LFO 2**, **TRACK DEST** (today LFO, LFO 2, LFO DEST).
     Named TRACK even where it is the only LFO, so the same page always has the same name.
  3. **MOD**. Its source "LFO" (the track LFO) shows as **TRK LFO** if it fits.
- The synth's envelope pages under ENV are shown by their full title too (P5 FLT ENV …).
- **CZ-1's envelopes:** six envelopes (pitch, wave, amp for each line), five pages each (R1-4, R5-8, L1-4, L5-8,
  POINT): 30 pages. Today ENV lists only the six R1-4 pages. With option A, ENV owns all 30: SELECT walks an
  envelope's five pages, PRESETS jumps to the next envelope (as EDIT's sections do).
- **SELECT** walks only the lit button's pages, and the header's "k/n" (task 1) counts them.
- **The light** follows the page, which now always matches the button that owns it.
- **EDIT pressed:**
  - from ENV or LFO: goes to EDIT (its last page used), and EDIT lights
  - on an EDIT page: opens the section map, as now
- **PRESETS** jumps a section only in EDIT.
- **SID:** check what its ENV page does. If the track ADSR does nothing there, show only ENV DEST, or give SID
  native ENV pages for its chip envelope.
- Update README's page lists and `docs/design/README.md` "Sound pages, fast".

### Files

- `firmware/src/ui.c`: `open_family()`, `page_scroll()`, `fam_pages()`, `native_page()`
- `firmware/src/ui_input.c`: `cur_fam()`, the `B_EDIT` press (`smap_open()`), `ui_leds()`
- `firmware/src/ui_sections.c`: `sec_build()` skips the engine's envelope and LFO pages
- `firmware/src/ui_draw.c`: `head_chip_text()` shows the engine's envelope and LFO pages by their full title
- `firmware/src/params.c`: the track LFO pages' titles (TRACK LFO, TRACK LFO 2, TRACK DEST); MOD's source name
- `firmware/src/ui_draw.c`: `head_pages()` counts the button's pages
- `README.md`, `docs/design/README.md` "Sound pages, fast"

### Tests

- `tests/ui_test.c`, for Prophet, FM6, CZ-1, SID and an engine without its own pages (DRUM):
  - ENV then SELECT around: stays on envelope pages, ENV lit, wraps.
  - LFO then SELECT around: the engine's LFO pages (header "P5 LFO" …), then TRACK LFO, TRACK LFO 2, TRACK DEST
    and MOD; LFO lit. On DRUM: TRACK LFO, TRACK LFO 2, TRACK DEST, MOD.
  - The header names fit (the layout lint), with task 1's "k/n" beside them.
  - EDIT then SELECT around: never reaches an envelope or LFO page; EDIT lit.
  - LFO, then EDIT: an EDIT page, EDIT lit, no map. EDIT again: the map. EDIT again: closed.
  - The section map has no empty section.
- UI renders of the section tabs and map for Prophet, FM6 and CZ-1 updated (the layout lint passes).

---

## 3. Bluetooth in the header: only in Settings

### Problem

With the radio on, Bluetooth is always in the header:

- A `BLE` tag while connected, which takes room from the page name and position.
- While not connected, its status ("BLE AVAILABLE", "SCANNING") replaces the page name and position altogether.

### Cause

Upstream's design (`docs/design/README.md`, "`BLE`"): while the radio is on, the header keeps a BLE tag, and setup,
scanning, waiting and failures replace the normal context. Code: `ble_head_text()`, `head_shared_status()` and
the `tag` in `draw_head()`, all in `firmware/src/ui_draw.c`.

### Solution

- **On the Bluetooth page** (Settings > Bluetooth, `ui.menu == 3`): the full status in the header, as now.
- **Everywhere else, nothing persistent:** no `BLE` tag, no "BLE AVAILABLE" or "SCANNING" (scanning only runs on
  the Bluetooth page anyway).
- **Exception, the pairing code:** `PIN 000042` stays in the header on every screen while pairing. It has to be
  typed on the other device, and it lasts only as long as the pairing.
- **Changes as brief toasts** (the design doc's place for brief explanations): "BLE CONNECTED",
  "BLE DISCONNECTED", and failures ("BLE TIMEOUT", "PAIRING FAILED").
- **Settings' Bluetooth row** shows the state (OFF / ON / CONNECTED), so it can be checked without the header.
- Update the "`BLE`" paragraph of `docs/design/README.md` and `docs/BLE_MIDI.md` ("The shared header shows
  connection/setup state") in our fork.

### Files

- `firmware/src/ui_draw.c`: `ble_head_text()`, `head_shared_status()`, the `BLE` tag in `draw_head()`
- `firmware/src/midi_ble.c`: toasts on connect / disconnect / failure (where `mb.generation` changes)
- `firmware/src/ui_menu.c`: the Bluetooth row's state
- `docs/design/README.md`, `docs/BLE_MIDI.md`

### Tests

- `tests/ble_tests.py` UI renders (`ble_ui_render`): no BLE header text off the Bluetooth page while available or
  connected; the PIN shown on any page while pairing; full status on the Bluetooth page.
- The connect / disconnect toasts appear once per change.

---

## Definition of done (each task)

- Host tests pass in both builds (`tests/run_tests.sh`; `CC="cc -DMELODEE_KEY_NAV=1" tests/run_tests.sh`).
- Both builds link; flash checked against the budget (DEV.md "Our footprint and budget"; 7.6 KB left).
- Tried on the device.
- README / design doc updated where behaviour changes.
