# Selective SHAYPE port plan

> **Superseded (2026-10-27).** The SHAYPE port (commit `5593756`) was replaced by a simpler SHAPE: a rhythm of ON / OFF
> steps over the normal arp, edited with M3 / OCT− held and the white keys (`firmware/src/arp_shape.c`, README "Sounds,
> chords and the arp", DEV.md row 18). The Felucca core and its vectors are gone. This plan is kept for the history.

## Goal

Add a compact Shape Arp derived from Felucca's SHAYPE implementation alongside Melodee's existing arpeggiator. The existing UP, DN, UPDN, RND, ORD and REPEAT modes must remain available and behaviorally unchanged. The result must:

- draw and play an editable pattern of rows over a held chord;
- work with physical keys, routed MIDI, sequencer notes, recording, internal clock and external MIDI clock;
- save with projects, pattern-bank containers and templates;
- follow Melodee's current UI and navigation conventions;
- fit the current firmware without restoring SID or TRIO;
- retain the required Felucca and WaveLoop attribution.

This is a selective port, not a cherry-pick of Felucca midi25–midi27.

## Source and provenance

Use the current Felucca implementation as the behavioral reference:

- repository: `/Users/tjaved/Personal/felucca`
- core: `firmware/src/arp_shape.c`
- playback host: `firmware/src/seq.c`
- device editor: `firmware/src/ui_arp.c`
- initial integration commit: `93f2d2e`
- later direction fixes: `bcd9957`
- PEND behavior and key-attach work: `8e5c90b`

Port retained code with its applicable GPL identifier and attribution. Mixed Melodee files must retain Melodee's copyright and identify retained Felucca code as required by `AGENTS.md`. Record the exact source commit in the new core file and tests.

## Memory budget

Baseline nav build at the start of this work:

- application image: 564,356 bytes
- maximum application image: 577,248 bytes
- flash headroom: 12,892 bytes
- low SRAM tail: 1,752 bytes
- normal data SRAM tail: 56 bytes
- retained SRAM tail: 48 bytes
- audio/resource arena: 122,080 bytes available; required minimum 49,152 bytes

Felucca's complete midi25 integration added about 11,096 bytes of image, 544 bytes of main RAM and 440 bytes of pool. That complete port is too close to Melodee's limits and cannot use normal or retained RAM directly.

Hard gates for this port:

- net image growth: target at most 7.5 KiB; reject above 9.5 KiB without first reclaiming unrelated flash;
- final flash reserve: at least 3 KiB;
- normal data SRAM growth: zero, except growth proven to fit with at least 32 bytes still free;
- retained SRAM growth: zero;
- resource-pool growth: at most 1.5 KiB;
- audio working arena: remain above the existing tested minimum for all engine/FX combinations;
- no new allocation or unbounded work in the audio callback.

Measure after every phase with the nav configuration, `MELODEE_KEY_NAV=1`.

## MVP feature set

The first complete version includes:

- all existing arp modes, settings, playback and saved-project behavior unchanged;
- SHAYPE appended as a new `P_AMODE` choice;
- eight rows over the held chord;
- 1–64 pattern steps;
- at most 30 stored shape notes per track;
- note lengths in whole arp steps;
- simultaneous rows on one step, up to eight sounding arp notes;
- RATE, GATE, SWING and HOLD;
- direction: UP, DOWN, PEND, WYNDO and SHYFT;
- boundary: WRAP or OCT;
- immediate start from step zero when a stopped arp receives its first chord;
- keys, MIDI and sequencer chords as input;
- current Melodee behavior of recording what the arp actually plays;
- a 16-step device viewport with cursor and playhead;
- persistence and migration from existing projects.

The MVP deliberately excludes:

- Felucca's ARP 3 page;
- factory shape selection beyond one default shape;
- ROOT and shape-wide octave offset;
- KEY ATTACH NOW/NOTE/CYCLE;
- ZOOM, SHIFT, MIRROR and MUTATE operations;
- SysEx ARP_GET/ARP_SET;
- a web-editor SHAYPE tab.

These are optional follow-up work and must not be used to justify exceeding the memory gates.

## Parameter reuse

Do not add track parameters for the MVP. Append `SHAPE` to the existing `P_AMODE` enum. Share RATE, GATE, SWING and HOLD with the conventional arp, but keep the conventional OCT, PROB and ORD values intact while SHAPE is selected.

### Existing modes

The current controls and ranges remain unchanged:

- ARP: MODE, RATE, OCT, GATE
- ARP 2: SWG, PROB, HOLD, ORD

### SHAPE mode

- ARP: MODE, RATE, DIR, GATE
- ARP 2: SWG, STEPS, HOLD, BOUND

Store DIR, STEPS and BOUND in the three reserved bytes of each 64-byte `arp_pat_t` block. The page may retain the existing parameter IDs as UI/control addresses, but its value accessors must read and write the shape block instead of overwriting `track_t.p[P_AOCT]`, `P_APROB` or `P_AORDER`.

The shared parameter IDs are:

1. `P_AMODE`: existing OFF/UP/DN/UPDN/RND/ORD/REPEAT values, then SHAPE
2. `P_ARATE`: RATE in every mode
3. `P_AOCT`: conventional OCT; its page position addresses shape-block DIR while in SHAPE
4. `P_AGATE`: GATE in every mode
5. `P_ASWING`: SWG in every mode
6. `P_APROB`: conventional PROB; its page position addresses shape-block STEPS while in SHAPE
7. `P_AHOLD`: HOLD in every mode
8. `P_AORDER`: conventional ORD; its page position addresses shape-block BOUND while in SHAPE

Dynamic labels, ranges, enum names and value pointers must depend on `P_AMODE` without changing stored parameter IDs. Switching between conventional and SHAPE modes preserves both engines' settings. For the MVP, DIR, STEPS and BOUND are not motion-recordable; RATE, GATE, SWING and HOLD retain normal motion support. MIDI/control edits to the three contextual positions must update the shape block safely rather than the conventional parameter bytes.

## Runtime-state placement

Add no shape arrays to `track_t` and no default-BSS arrays. Normal data SRAM has only 56 bytes free.

Place bounded runtime state in `.pool`:

- four `arp_pat_t` patterns: 4 × 64 = 256 bytes;
- four sounding-note sets: note, velocity if needed, and gate deadline for at most eight notes;
- per-track pass offset and small playback counters that do not fit existing `track_t` fields;
- any chord snapshot required by the selected MVP scheduling behavior.

Reuse existing clock fields such as `arp_pos` and `arp_idx` where both arp engines have compatible meanings. Do not repurpose `arp_note` or `arp_off` in a way that breaks the conventional single-note arp. Keep a single documented owner and reset path for every shared field when switching between conventional and SHAPE modes.

Do not allocate shape state from retained RAM. Do not consume the optional 28 KiB cache merely to make the feature fit.

## Phase 0: baseline and size probe

1. Save the current nav artifact and map/size reports.
2. Record image and all SRAM/resource tails.
3. Run the current host suite before changing arp behavior.
4. Add a temporary build report that attributes image and RAM changes to each phase.
5. Confirm the exact maximum size of the existing NOR project objects before changing the wire format.

Exit condition: reproducible baseline numbers and passing tests.

## Phase 1: compact pure core

Create `firmware/src/arp_shape.c` from the Felucca core, retaining only what the MVP calls:

- packed `arp_pat_t` and note accessors;
- default-pattern loading;
- voicing, boundary resolution and event generation;
- UP, DOWN, PEND, WYNDO and SHYFT pass behavior;
- find, paint, drop, fit and validation helpers.

Remove from the compiled MVP core:

- all but the default factory pattern;
- RAND if retaining it increases code or test cost materially;
- subdivide/zoom;
- shift;
- reverse/flip;
- mutation/empty-cell helpers;
- key-attach code.

Import only the corresponding Felucca golden vectors and regenerate a reduced vector set pinned to the chosen source commit.

Exit conditions:

- pure-core host tests pass;
- image growth is measured;
- the core has no dependency on UI, tracks, storage or hardware.

## Phase 2: parallel playback path

Keep the current list-and-mode arp implementation in `firmware/src/seq.c`. Add a SHAPE branch selected only by the new appended `P_AMODE` value. Factor shared clock, HOLD and input ownership only where doing so is behaviorally neutral and smaller than duplicating it.

Playback requirements:

1. Build a unique, sorted chord from held keys/MIDI and the sequencer's active notes.
2. Feed that chord into the shape step.
3. Start all events on a shape step and retain independent gate deadlines.
4. Retriggering the same pitch ends its earlier ownership before starting the new one.
5. Stop, panic, engine changes and ARP OFF release every shape-owned note.
6. HOLD preserves the existing latch semantics.
7. Internal and external clocks retain residual phase across blocks and clock bursts.
8. Swing affects alternating shape steps without accumulating drift.
9. Recording captures the pitches and gates actually produced by the shape arp, preserving current Melodee behavior.
10. Existing chord input and chord-sequencer playback continue to feed both arp engines correctly.
11. Switching between a conventional mode and SHAPE releases notes owned by the previous engine and starts the selected engine cleanly.
12. Conventional UP, DN, UPDN, RND, ORD and REPEAT output remains regression-identical.

For the MVP, chord changes take effect on the next shape step. A chord arriving from silence while stopped starts step zero immediately. Do not port the three KEY ATTACH modes yet.

Add focused tests for:

- multiple rows on one step;
- independent note lengths;
- duplicate-pitch merging/retriggering;
- every direction and boundary;
- HOLD and complete key release;
- keys, USB MIDI, TRS MIDI and sequencer chord input;
- internal/external clock parity and recovered clock bursts;
- recording and undo;
- stop/panic with no hanging notes;
- chord sequencer into Shape Arp.

Exit conditions:

- playback tests and target instruction budgets pass;
- no hanging voices;
- resource-pool growth remains within budget;
- accumulated image growth remains below 5.5 KiB at this phase;
- conventional arp golden/event tests remain unchanged.

## Phase 3: parameters and compatibility

Append SHAPE after REPEAT in `N_AMODE`; never renumber an existing mode. Add context-sensitive labels, enum lists, ranges and backing-value accessors for the page positions normally occupied by `P_AOCT`, `P_APROB` and `P_AORDER`.

Introduce a new project format marker for stored shape blocks, but do not migrate conventional arp settings:

- every old `P_AMODE` value remains the same mode;
- RATE, OCT, GATE, SWING, PROB, HOLD and ORD remain unchanged;
- projects without shape blocks receive a valid default shape with default DIR, STEPS and BOUND on every track;
- new and initialized tracks default to conventional ARP OFF, with a default shape waiting if SHAPE is later selected;
- malformed shape data falls back to the default without changing the selected conventional mode.

Because shape-only settings live in the shape block, legacy OCT/PROB/ORD bytes never need reinterpretation or one-time destructive conversion.

Exit condition: old project fixtures retain identical arp settings and output, while SHAPE settings and patterns round-trip.

## Phase 4: device editor and drawing

Adapt the feature to Melodee's UI instead of copying Felucca's screen implementation wholesale.

### Drawing

Keep the current `pv_arp()` note-order illustration for conventional modes. When `P_AMODE` is SHAPE, draw an eight-row shape grid instead:

- 16 steps visible at once;
- cursor column framed;
- current playback column accented;
- held lengths drawn as continuous cells;
- rows use the four knob shades consistently;
- OFF dims the grid but does not hide the stored pattern;
- no new persistent status card or toast is used for cursor/playback state.

Cursor and playhead are page content, not header status. This follows `docs/design/README.md` status placement rules.

### Editing

On either ARP page:

- the existing preset/navigation encoder moves the shape cursor through STEPS;
- the first eight white keys select rows 1–8 at the cursor;
- tapping an empty row creates a one-step note;
- tapping an occupied row removes that note;
- holding a row key and turning knob 4 edits its length, consuming that knob gesture instead of changing the page parameter;
- EDIT clears all notes beginning at the cursor step;
- normal chord-playing keys remain available outside the reserved row-key range;
- pattern edits do not retrigger sounding notes.

Keep normal page knobs and SELECT page navigation. Context-sensitive labels make clear whether the conventional or SHAPE controls are active. Do not add ARP 3 for the MVP.

Add UI tests for key ownership, LEDs, cursor wrapping, lengths, full-pattern refusal, clear-step behavior, page changes and incremental redraw. Add render/lint scenes for empty, edited, playing and OFF grids.

Exit conditions:

- UI tests and render lint pass;
- LCD load remains within current page limits;
- accumulated image growth remains below 7.5 KiB.

## Phase 5: persistence

Shape patterns are track-owned project data, not per-note-pattern data. Save one 64-byte block for each of the four audio tracks.

Integrate with Melodee's current formats, not Felucca FUN9:

1. Define a new project magic/version after FUN22.
2. Append or place four validated shape blocks without changing existing field IDs.
3. Increase the serialized project extent only after proving all project NOR objects, templates, backup buffers and full pattern-bank containers still fit.
4. Update full-bank offsets and hashes in `pattern_store.c` without reducing the 32-bank guarantee.
5. Keep old FUN18–FUN22 and older supported fixtures readable.
6. Initialize missing or damaged shape blocks to the default shape; never reject an otherwise recoverable old project solely for missing shape data.
7. Include shape blocks in project copy, template, backup/restore, autosave dirty detection and host archive tests.
8. Ensure older firmware behavior is documented before writing the new format; users must back up before downgrade if older firmware cannot read the new extent.

If adding 256 bytes to every full project-bank record violates the guaranteed flash capacity, use a single project-level extension block in the existing full container rather than duplicating it per one of the 32 note banks. Do not reduce `NPAT` or project slot count.

Exit conditions:

- cold-load, save, backup and power-cut tests pass;
- all 32 pattern banks remain available;
- malformed shape data falls back safely;
- final image retains at least 3 KiB flash reserve.

## Phase 6: integration and hardware validation

Run:

- full host suite;
- UI renders and layout lint;
- target instruction and CPU budgets;
- project/backup/storage safety tests;
- web tests, even though the MVP has no SHAYPE web editor;
- nav build with `MELODEE_KEY_NAV=1`.

Hardware checks:

1. Hold two-, three- and four-note chords and verify every row mapping.
2. Edit cells and lengths while stopped and playing.
3. Verify UP, DOWN, PEND, WYNDO and SHYFT over several passes.
4. Verify WRAP and OCT at low and high MIDI pitches.
5. Verify HOLD, stop, panic, sound changes and project changes leave no hanging notes.
6. Record arp output and play it back with matching pitches and approximate gates.
7. Verify internal clock, USB clock and TRS clock.
8. Save, power-cycle and reload every project slot and a non-current pattern bank.
9. Recheck drum 808/909 switching and M1/M2/M3 navigation for regressions.

## Optional follow-up order

Only after the MVP passes memory and hardware gates:

1. A small factory-pattern list, if it fits without a third page.
2. Shape octave offset.
3. ROOT selection.
4. SHIFT and MIRROR.
5. ZOOM.
6. MUTATE.
7. KEY ATTACH NOTE/NOW/CYCLE.
8. SysEx pattern transfer.
9. Web editor tab.

Each optional feature needs its own image-size delta and tests. Stop adding features when flash reserve reaches 3 KiB.

## Definition of done

The port is complete when:

- the selective core has pinned provenance and vector parity;
- Shape Arp coexists with the old arp engine as an appended mode;
- every conventional arp mode remains regression-identical;
- playback, editing, persistence and migration pass focused tests;
- the complete host suite passes;
- the nav artifact builds with at least 3 KiB flash reserve and no normal/retained RAM overflow;
- all hardware checks pass;
- project downgrade limitations and controls are documented.
