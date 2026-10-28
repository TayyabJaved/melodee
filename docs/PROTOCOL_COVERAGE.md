# What the editor protocol can and cannot set

This is a map for building a project live on a connected FM-1 through the web editor's USB MIDI SysEx protocol,
for example by an AI that writes a song and sends it command by command, then saves it on the device. It lists every
part of a Melodee project and says whether the protocol can write it, read it, or neither, and which command does it.

It was checked against the firmware's command handlers (`firmware/src/editor*.c`) on `tj/1.6.0-rc.5`, not only
against [web/EDITOR_PROTOCOL.md](../web/EDITOR_PROTOCOL.md). That file is the reference for the byte layout of each
command. It grew version by version and some of its numbers are older than the code: where the two differ, this file
gives the code's values (marked *code*).

## The short version

**Can be built live:** engines and presets, every track parameter, every global parameter (BPM, swing, the shared
FX rack), the native FM6 / CZ-1 / Prophet patches, all 64 steps of every pattern (notes, chords, drum hits, accents,
slides, ties, velocity, chance, chord-sequencer entries), which of the 16 patterns plays, each pattern's LEN / DIV /
SWING / GATE, the song (up to 64 columns of rests and patterns, as rows the device turns into sections), knob
recordings (motion), mixing, saving to
slots 1–4.

**Cannot:** the generators (ARP > GEN), SHAPE's steps, timed (unquantized) recorded notes, the song's punch FX lane
and song-local sound snapshots, the song columns of MIDI tracks 5–8, adding tracks, the project's name, saving to
slots 5–32, plain PLAY / STOP outside a song.

## Talking to the device

- A request is `F0 7D 46 4C <cmd> <args…> F7`; every valid request gets exactly one reply with the same `<cmd>`.
  Invalid fixed-length requests get no reply, so time out and treat silence as "refused".
- Every data byte is 7 bit. A value (v14) is two bytes, LSB first, holding value + 8192.
- `INFO` (1) first: it gives `P_COUNT`, `G_COUNT`, `NSTEP` (64), `P_E0`, the engine names, and tagged capability
  blocks. Read the bank tag `50 04 <NPAT> <CHAIN_ROWS>` for the pattern count (*code*: 16) and section count (64).
- `DESC` (5) gives any parameter's label, format, range, default and enum names. Look parameters up by **label**,
  never by a hard-coded id: ids can move between releases.
- Most writes refuse with rc 2 / 3 or are ignored while a **song plays** (`chain_busy()`); stop the song first.
- Editing goes into the device's RAM. Nothing is kept until `PROJECT` save (9); a power cycle loses unsaved work.

## Coverage, part by part

### Tracks and sounds

| What | Status | How |
| --- | --- | --- |
| Select a track | ✅ write | `TRACK` (27) track. Only tracks that exist (`trk_n()`); the reply lists tracks 1–4 only (`ED_TRK` = 4) |
| Engine of a track | ✅ write | select the track, then `PRESET` (8) engine, preset; or `SET` (3) global `ENG` |
| Factory preset | ✅ write | `PRESET` (8); preset numbers past 127 send the high bits as a third byte. `NAMES` (10) lists them |
| Every track parameter (filter, envelopes, LFO, ARP, SCL, chord keys, Humanize, note FX, sends, level, pan …) | ✅ write | `TRACK_PARAM` (31) track, id, v14 for any track 0–7 without changing the selection; or `SET` (3) scope 0 on the selected track. Reserved ids are refused |
| Level and mute | ✅ write | `TRACK_MIX` (28), or `TRACK_PARAM` |
| FM6 voice (DX7, 128 bytes) | ✅ write | `FM6_PUT` (69) target track, index, 128 bytes; `FM6_GET` (68) reads |
| CZ-1 tone | ✅ write | `CZ_PUT` (76) target 0 = track; `CZ_GET` (75) reads |
| Prophet program | ✅ write | command 95: op 1 native frame PUT, op 2 field edit (offset, value), op 3 name; op 0 reads |
| Drum kit patch (per-lane tune, decay …) | ⚠️ indirect | no direct track write. `LIBRARY` (80) can store a drum patch in a user slot (a flash write), then load it onto a track (op 4) |
| User presets | ✅ write | `UP_*` (16–21) and `LIBRARY` (80); these write flash |
| Add a track (MIDI-only tracks 5–8) | ❌ | no command; tracks are added on the device |

### Steps and patterns

| What | Status | How |
| --- | --- | --- |
| A step of the playing pattern | ✅ write | `TRACK_STEP` (30) track, index 0–63, then n, note0..3, time, flags, vel, hits (3 bytes), chance; or `STEP_SET` (7) on the selected track |
| Notes and chords (up to 4 per step) | ✅ write | n = 1..4, note0..note3 (MIDI 0–127) |
| Ties, rests | ✅ write | time: 0 NOTE, 1 TIE (holds the step before; note length = the TIE steps after it), 2 REST |
| Accent, slide | ✅ write | flags: 1 accent, 2 slide |
| Velocity | ✅ write | vel 0–127 |
| Drum hits and lane accents | ✅ write | hits: `hit & 127`, `acc & 127`, then bit 7 of each (bit 0 hit, bit 1 acc). Lane l = bit l, 8 lanes |
| Chance | ✅ write | 0–100 (the 12th step byte); without it a write keeps the step's own |
| Chord-sequencer entry (SEQ > CHORDS) | ✅ write, raw | encoded in note1..note3 of a one-note step; see "Chord-sequencer entries" below |
| Choose which of the 16 patterns plays (8 on a MIDI-only track) | ✅ write | `PATTERN` (73) track, 1, bank (0-based). While stopped it applies at once; while playing it queues to the loop end. Step writes always go to the active pattern, so select a pattern, then write its steps |
| Copy a pattern | ✅ write | `PATTERN` (73) track, 2, source, destination (copying into the active pattern needs STOP) |
| Pattern LEN (1–64), DIV, SWING, GATE | ✅ write | track parameters `LEN`, `DIV`, `SWG`, `GATE` (look up by label, scope 0: the global swing is also labelled `SWG`) via `TRACK_PARAM`; they belong to the active pattern |
| Timed (unquantized) recorded notes | ❌ | the project's 1,024 recorded notes have no command; record them on the device |
| Pattern tools (Double, Half, Euclid) | — | device actions, not data; compute the steps yourself and write them |

### Song

| What | Status | How |
| --- | --- | --- |
| Sections: patterns of tracks 1–4 and repeats | ⚠️ partly | `BANK_SONG` (74) op 1: count 0–64, then per row T1..T4 bank and repeat 1–16. *code*: bank 16 = REST, 17 = continue the section before (not at row 0, not after a REST). Since sections (2026-10-28) the device converts the rows: identical rows share a letter (26 at most), a row's length is track 1's bank × its repeat (AUTO when that is its longest bank), a continuation gives its column the block's bank (it restarts). Replies report repeat 1 |
| Section letters, set lengths (the section page) | ❌ | not on the wire: an op 1 replaces them with converted ones |
| Start / stop the song | ✅ write | `BANK_SONG` op 2 [start row], op 3 |
| Loop the song | ✅ write | `BANK_SONG` op 4, 0/1 |
| Song columns of MIDI tracks 5–8 | ❌ | only four columns on the wire |
| Punch FX lane (timed master FX per section) | ❌ | none; and **replacing the arrangement clears the punch lane** |
| Song-local sound snapshots | ❌ | none |

### Global and mix

| What | Status | How |
| --- | --- | --- |
| BPM, swing, tuning, clock source, master settings | ✅ write | `SET` (3) scope 1, by label (`BPM`, …) |
| Shared FX rack: three slots, their effect, MIX, controls, TONE, ROUTE (chain / split), RETURN | ✅ write | `SET` scope 1: the shared slot effect ids go through the same safe select as the device (one REVERB). In `DESC` the route is the global labelled `MODE` (CHAIN / SPLIT; the device's page calls it ROUTE) and the return `RETURN` |
| Each track's send to SHARED | ✅ write | track parameter `SEND` (and `SEND 2`, `SEND 3` in split) via `TRACK_PARAM` |
| Live master punch FX (FX held + key) | ❌ | keys only |

### Motion (knob recordings)

| What | Status | How |
| --- | --- | --- |
| Events: step, parameter, value | ✅ write | `MOTION` (64) track, 3, step, id, v14; 4 deletes; 2 clears; 1 on / off. Tracks 1–4 only, at most 64 events in the project |
| Native-engine motion (Prophet, FM6, CZ ids) | ✅ write | `MOTION` with the native ids the motion capability tag advertises |

### Our features

| What | Status | How |
| --- | --- | --- |
| Humanize velocity (SCL) | ✅ write | track parameter `HVEL` (slot 45) |
| Chord keys | ✅ write | track parameters `CHRD`, `VOIC` |
| ARP modes UP … REPEAT | ✅ write | track parameter `MODE` and the ARP parameters |
| ARP MODE SHAPE | ⚠️ partly | `MODE` = 7 selects it, but the steps (ON / OFF), STEPS and DIR are not parameters. Writing `PROB` / `ORD` changes the real ARP bytes, not SHAPE's. A new song starts with every step ON, 16 steps, UP |
| Generators (ARP > GEN: TURING … BOUNCE) | ❌ | their mode, seed, CHANGE and settings are not parameters and have no command |
| Chord sequencer | ✅ write | steps; see below |
| Drum black-key lanes, FX page order | — | UI only, nothing to write |

### Projects

| What | Status | How |
| --- | --- | --- |
| Save the music in RAM to a slot | ✅ | `PROJECT` (9) 1, slot 0–3 (slots 1–4); stops the transport first; ~2 s; no reply means it failed |
| Load a slot | ✅ | `PROJECT` 0, slot 0–3 |
| Slots 5–32 | ❌ | `PROJECT` takes 0–3 only (the device's project pool has more) |
| Project name | ❌ | named on the device (SAVE > PROJECT > NAME) |
| A whole project as one file | ⚠️ binary | `BACKUP_GET` / `BACKUP_PUT` (66 / 67) move one project object (ids 2–5, 64–91) as validated binary; see the file-based route in the plan, not this one |

### Transport

| What | Status | How |
| --- | --- | --- |
| Play / stop a song | ✅ | `BANK_SONG` op 2 / 3 |
| Plain PLAY / STOP (no song) | ❌ | no editor command. MIDI Start / Stop are followed only with the clock set to USB (`CLK` = USB), and then the host must also send MIDI clock for the tempo |
| Undo | ❌ | none |

## Chord-sequencer entries

A chord-sequencer entry lives in a step with **one note** (n = 1, time NOTE, no drum hits): note0 is the root, and
note1..note3 carry 21 bits, 15 for the settings and 6 as a check (`firmware/src/chord_seq.c`):

```
chord  0..9   OFF, DIA3, DIA7, MAJ, MIN, DOM7, MAJ7, MIN7, SUS4, POW
voice  0..4   CLOSE, OPEN, INV1, INV2, +OCT
strum  0..7   OFF, 1/8, 1/16, 1/32, 1/64, 1/128, 16T, 32T
dir    0..2   DOWN, UP, ALT
nudge  0..24  12 = on the step; 0 = 12/24 of a step early, 24 = 12/24 late

v     = chord + 10*voice + 50*strum + 400*dir + 1200*nudge
check = (((v * 2654435761) mod 2^32) >> 26) XOR 0x2D
w     = v | check << 15
note1 = w & 127,  note2 = (w >> 7) & 127,  note3 = w >> 14
```

All settings at rest (chord OFF, voice CLOSE, strum OFF, dir DOWN, nudge 12) is a plain note: write zeros. The
entry's velocity and length are the step's own (vel, and the TIE steps after it). A step that fails the check plays
as a plain note.

## Building a song live: the order that works

1. `INFO`, then `DESC` for every parameter you will touch: map labels to ids and ranges.
2. Stop any song (`BANK_SONG` op 3).
3. Globals (scope 1): `BPM`, `SWG`, the shared FX slots, `MODE` (the route), `RETURN`.
4. For each track 1–4: `TRACK` select, `PRESET` engine and preset (or a native patch), then `TRACK_PARAM` for the
   sound, scale, ARP, chord keys, note FX and `SEND`.
5. For each track and each pattern you use: `PATTERN` select it, set `LEN` / `DIV` / `SWG` / `GATE`, write its 64
   steps with `TRACK_STEP` (rests for the unused ones). Send all 12 step bytes: a shorter write keeps the step's old
   drum hits and chance.
6. `MOTION` events if any.
7. `BANK_SONG` op 1 with the sections (a row a column; give each the repeat that makes its length: track 1's
   bank × repeat; rows with the same banks become one section); op 2 to listen.
8. Select the pattern each track should start on, then `PROJECT` save to a slot 1–4. Name it on the device.

Writes are fast but not instant: send the next request after the reply, not before. A full song (4 tracks × 16
patterns × 64 steps) is about 4,100 step writes.

## What would close the gaps

Each of these is a small firmware addition, if the live route becomes the main one:

- A generator command (get / set a track's `gn_save` bytes) and a SHAPE command (mask, STEPS, DIR).
- `PROJECT` slots 0–31 and a name argument.
- Plain transport (play / stop) without the clock set to USB.
- Song columns for tracks 5–8, the punch FX lane, and the sections themselves (each column's letter and length).
- A recorded-notes read / write, if AI-written unquantized parts are wanted.
