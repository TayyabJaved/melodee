# Navigation: which control moves you where

Which control moves through pages, rows and options on each screen, taken from the input code
(`firmware/src/ui_input.c` and the files it calls). It began as an audit (2026-10-28); the decisions it led to are at
the end, and the tables describe the firmware as it is now. For what each value does, see
[SHORTCUTS.md](SHORTCUTS.md) and the [manual](../web/manual/index.html).

## The rules

| Control | Its job | Ends |
|---|---|---|
| **SELECT** | Sideways: the pages of the open section, the button that is lit (`ui_nav.c` `nav_scroll`). Never up and down a list | Stops at the first and last |
| **PRESETS** | The bigger step: sounds where a sound is the subject; elsewhere the page's next part, or the row (up and down) wherever KNOB 2 picks one | Stops |
| **ALGORITHM** | The selected track, on every screen (not in the sound designer) | Stops |
| **KNOB 2** | *Which one*: the row of a list, a sheet, the menu, a rack, the slots; the item in a picker | Stops |
| **KNOB 1** | *Its value* on a row; the group in a picker; the cursor along NOTES and SONG; ten slots on the slot pages | — |
| **OCT+** | In: open, confirm, the row's sheet. **Hold**: the page's sheet | — |
| **OCT−** | Out: the innermost thing first, then Stage | — |

Two things still go round on purpose: NOTES' cursor (a pattern is a loop), and a page button tapped again (ENV, LFO,
GLO … go on through their pages and back to the first, as a button has only one direction).

## PRESETS, screen by screen

| Screen | PRESETS does | Code |
|---|---|---|
| Stage, SAVE › PRESETS, PATTERNS | Browse the selected track's sounds | `browse_turn` |
| SCL › SCALES (the picker) | The next / previous scale | `scale_picker_step` |
| SEQ › CHORDS | The step | `cseq_preset` |
| ARP in SHAPE mode | The page of 16 steps the white keys edit | `shape_page_turn` |
| NOTES (piano roll) | Zoom (16 steps down to one) | `ui.note_zoom` |
| An engine's own ENV / LFO pages (Prophet, FM6, CZ-1) | The next envelope or LFO group | `nav_preset` |
| An engine's EDIT pages in sections | The next / previous section | `sec_turn` |
| FM6's operator pages | The operator, 1–6 | `fm6_opsel` |
| SONG's section page | The section's length, a beat a detent | `sect_length` |
| List pages (SCL, VOICE, FM LFO, FM BEND) | The row | `list_knob` |
| Sheets and value lists (pickers) | The row | `sheet_input`, `vlist_input` |
| The settings menu (hold HOME) | The row, as KNOB 2 | `ui_menu.c` |
| ABOUT and other documents in the menu | Scroll | `ui_menu.c` |
| The EDIT section map | The category, as KNOB 1 | `smap_input` |
| USER, FM6 STORE, P5 STORE, PROJECT | The slot, one a detent | `page_knob` |
| FX racks, MOD, MOTION | The row, the route, the lane | `page_knob` |
| The drum grid | The lane | `page_knob` |
| The punch editor | The lane | `ui_song_punch.c` |
| The sound designer | The stage (Features … Play) | `designer_stage` |
| BLE page | The row, as KNOB 2 | `ui_ble.c` |
| Everything else (TEMPO, PATTERN, ARP, MIXER, GLOBAL, SONG, …) | Nothing: the toast **PRESETS: GO TO STAGE** | — |

## SELECT, where it is not "pages"

| Screen | SELECT does |
|---|---|
| Stage and the chord view | The HOME SELECT value (BPM by default) |
| GLO's pages (main, MIXER, GLOBAL … WEIGHTS 2) | Pages, as one list; GLO held: GLO SELECT |
| GLO held on any page | GLO SELECT (BPM by default) |
| SEQ held | The selected track's pattern |
| NOTES with SCL or ENV held (or a key, while entering) | Move the note / its length |
| OCT+ held on NOTES, USER slots or SONG | Extend the selection |
| SONG's section page | The song's next / previous section (A, B, C … in use), its first column |
| The EDIT section map | Its page (as KNOB 2) |
| The sound designer | The page within the stage |
| The settings menu | Nothing (its rows are up and down: KNOB 2, PRESETS) |

## The page position

The header shows "k/n" after the BPM wherever SELECT walks two or more: a button's pages (an EDIT section's pages as
its tabs show), HOME's two, GLO's six with its main page as 1/6, and on the section page the song's sections (2/3:
the second of three). The punch editor has none.

## Every screen

"Pages" is what SELECT walks; "Which one" is what picks the row or item; "Value" sets it.

| Screen | Pages | Which one | Value | Notes |
|---|---|---|---|---|
| Stage | — (HOME again: chord view) | ALGORITHM (track) | KNOB 1–4 (sound) | SELECT: HOME SELECT |
| Sound browser | — | KNOB 2 / PRESETS (sound) | — | KNOB 1 the list, KNOB 3 FAV, KNOB 4 engine |
| Engine EDIT pages | SELECT | PRESETS (section) | KNOB 1–4 | EDIT again: the section map |
| EDIT section map | KNOB 2 / SELECT (page) | KNOB 1 / PRESETS (category) | — | OCT+ opens |
| ENV, LFO, LFO 2, DEST | SELECT | — | KNOB 1–4 | Engine pages first, PRESETS between their groups |
| MOD | SELECT | KNOB 2 / PRESETS (route 1–4) | KNOB 1, 3, 4 | Engines with their own LFO pages: PRESETS the group |
| FX racks | SELECT (racks, used slots) | KNOB 2 / PRESETS (row) | KNOB 1 effect, 4 amount (SHARED: 3 ROUTE) | OCT+ opens a slot |
| SCL list | SELECT | KNOB 2 / PRESETS (row) | KNOB 1 | OCT+ on Scale: SCALES |
| SCALES picker | — | KNOB 2 / PRESETS (scale) | — | KNOB 1 the family |
| VOICE, FM LFO, FM BEND lists | SELECT | KNOB 2 (row; PRESETS where not a section) | KNOB 1 | |
| ARP, ARP 2 | SELECT | — | KNOB 1–4 | SHAPE: PRESETS the step page |
| NOTES | SELECT | KNOB 1 (step, round the loop) | KNOB 2 pitch, 3 length, 4 velocity | PRESETS zoom |
| Drum grid | SELECT | KNOB 1 step, KNOB 2 / PRESETS lane (or the keys) | KNOB 3 hit, 4 accent | D#5: next page of steps; F5 G5 steps 15, 16 |
| PATTERN, TEMPO | SELECT | — | KNOB 1–4 | |
| PATTERNS | SELECT (on to SONG) | KNOB *n* is track *n* | KNOB 1–4 queue | PRESETS: sound |
| SONG | SELECT | KNOB 1 (column) | KNOB 2 (its section) | |
| Section page | SELECT (the song's sections) | KNOB *n* is track *n* | KNOB 1–4 pattern, PRESETS length | OCT− back to SONG |
| Punch editor | — | KNOB 2 / PRESETS (lane) | KNOB 1 effect, 3 start, 4 end | |
| MOTION | SELECT | KNOB 2 / PRESETS (lane), KNOB 3 (step) | KNOB 4 | KNOB 1 playback on / off |
| GLO main page | SELECT | — | KNOB 1–4 levels | |
| MIXER | SELECT | ALGORITHM (track) | KNOB 1 level, 2 pan, 4 mute | |
| GLOBAL, SYSTEM | SELECT | — | KNOB 1–4 | |
| SAVE › USER, FM6 STORE, P5 STORE | SELECT | KNOB 2 / PRESETS (a slot), KNOB 1 (ten) | — | OCT+ the slot's sheet |
| SAVE › PROJECT | SELECT | KNOB 1, KNOB 2 or PRESETS (a slot; there are six) | — | Load, Save, Boot: its sheet |
| Sheets (hold OCT+) | — | KNOB 2 / PRESETS (row) | KNOB 1 | OCT+ does it, OCT− closes |
| Value lists (a picker) | — | KNOB 2 / PRESETS (item) | — | Effects: KNOB 1 the family |
| The settings menu | — | KNOB 2 / PRESETS (row) | KNOB 1 or OCT+ | SELECT nothing |
| NAME | — | KNOB 1 (cursor) | KNOB 2 (character) | The keys type |
| NEW song | — | — | KNOB 1–4 | |
| Sound designer | SELECT (page) | PRESETS / OCT± (stage) | KNOB 1–4 | |

## The M-keys (F5, G5, F#5)

They are M1, M2 and M3 except where the keys need them (`ui_nav.c` `nav_keep`, `hal/fm1_input.h`
`fm1_input_nav_free`): while FX is held or locked and in the punch editor all three play their effects (ECHO, WOBBLE,
FADE OUT); on the drum grid F5 and G5 are steps 15 and 16, and F#5 stays M3 so M3 + A#4 still hears the step. On ARP
in SHAPE mode M3's white-key shortcuts give way to SHAPE's steps.

## What the audit found

Before these changes, roughly most confusing first:

1. PRESETS had ten jobs, and on many pages did nothing at all, with no message.
2. The EDIT section map put the group (category) on KNOB 2 and the item (page) on KNOB 1, the other way round from
   the browser, SCALES and the effect picker.
3. The ends differed: SELECT went round a section's pages but stopped on GLO's; the menu stopped on KNOB 2 and went
   round on PRESETS.
4. KNOB 1 and KNOB 2 did the same thing on the slot pages.
5. SELECT set the section page's length, while the header showed SONG's page position there ("5/6").
6. The SCL list ignored PRESETS although the SCALES picker one step in used it.
7. PROJECT: the manual said KNOB 2–4 set BOOT, LOAD and SAVE; they never did (only its sheet does).
8. F5, G5 and F#5 were always M1–M3, so held FX could not play ECHO, WOBBLE or FADE OUT, and the drum grid lost steps
   15 and 16 and page up.

## Decisions (2026-10-28)

What was asked, and what the firmware does now.

| # | Question | Decision | Now |
|---|---|---|---|
| 1 | What PRESETS means | **a**: the bigger step | Sounds on Stage, the browser and PATTERNS; the page's next part elsewhere; the row wherever KNOB 2 picks one |
| 2 | PRESETS with nothing to do | **a**: a message | **PRESETS: GO TO STAGE**, nothing changed |
| 3 | Group and item in pickers | **a**: KNOB 1 the group | The EDIT map: KNOB 1 / PRESETS the category, KNOB 2 / SELECT the page |
| 4 | The ends | **a**: stop everywhere | SELECT, the menu, the groups, sections and rows stop. Kept going round: NOTES' cursor (a loop) and a page button tapped again |
| 5 | KNOB 1 when it picks "which one" | **a** | NOTES and SONG keep KNOB 1 as the cursor; USER and the STOREs: KNOB 2 a slot, KNOB 1 ten. PROJECT, with six slots, keeps one for both |
| 6 | The section page | PRESETS the length, SELECT the sections | PRESETS sets the length a beat a detent; SELECT walks the song's sections (letters in use), stopping at the first and last, a loop playing stops; header k/n of them |
| 7 | SELECT in the settings menu | Kept: SELECT is never up and down | SELECT does nothing there; KNOB 2 and PRESETS move the row |
| 8 | The page position | **a**: wherever SELECT walks | GLO's main page 1/6, the section page its sections |
| 9 | PRESETS in the menu | **a**: stops, as KNOB 2 | Stops at the first and last rows |
| 10 | The M-keys under other layers | **a**: step aside | All three under FX and in the punch editor; on the drum grid F5 and G5 (F#5 stays M3 for A#4's hear and the M3 shortcuts) |
