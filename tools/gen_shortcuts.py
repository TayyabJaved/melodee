#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""The pictures of docs/SHORTCUTS.md: the panel's buttons, a keyboard map for each key layer and a card of the knobs
and buttons for the main pages, as SVGs in docs/shortcuts/.

  tools/gen_shortcuts.py            # writes docs/shortcuts/
  tools/gen_shortcuts.py --check    # exit 1 when a file there differs from what it would write

Drawings in the redesign's palette (as tools/gen_song_guide.py), not firmware renders. The keyboard is the FM-1's 27
keys, F3 .. G5 (firmware/src/seq.c key_black). Each button keeps one colour in every picture, on its key too. Keep
the maps in step with the firmware (hal/fm1_input.h, panel.c, ui_layer.c, punch.c, ui.c's DRUM grid) and the text
with docs/SHORTCUTS.md and web/manual/index.html.
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "shortcuts"

BG, SURF, ROW, LINE = "#17141F", "#221E2C", "#262033", "#2E2939"
TEXT, SOFT, MID, DIM = "#F3EEF8", "#C9C2D6", "#9A93A8", "#5A5468"
THEME, REC, MARK = "#F5A623", "#FF5A6E", "#7FE0FF"
TRK = ["#FF7A5C", "#FFC145", "#46D9B0", "#9D8CFF"]
FONT = "Rubik, Inter, 'Helvetica Neue', Arial, sans-serif"
W = 860

# one colour a button, everywhere (its box, its key)
PAGE_BTN, MENU_BTN, PLAY_C, OCT_C, ACT_C = MARK, THEME, "#46D9B0", "#9D8CFF", "#FFD27A"
BTN_C = {"FX": PAGE_BTN, "SCL": PAGE_BTN, "ENV": PAGE_BTN, "LFO": PAGE_BTN, "EDIT": PAGE_BTN, "GLO": PAGE_BTN,
         "HOME": MENU_BTN, "SAVE": MENU_BTN, "ARP": MENU_BTN, "SEQ": MENU_BTN, "PLAY": PLAY_C, "REC": REC,
         "OCT−": OCT_C, "OCT+": OCT_C}


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def t(x, y, s, size=12, fill=TEXT, anchor="start", weight=400):
    return (f'<text x="{x:g}" y="{y:g}" font-size="{size:g}" fill="{fill}" text-anchor="{anchor}" '
            f'font-weight="{weight}">{esc(s)}</text>')


def r(x, y, w, h, fill, rx=0, stroke=None, sw=1, dash=None, opacity=None):
    a = f' stroke="{stroke}" stroke-width="{sw:g}"' if stroke else ""
    a += f' stroke-dasharray="{dash}"' if dash else ""
    a += f' opacity="{opacity:g}"' if opacity is not None else ""
    return f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" rx="{rx:g}" fill="{fill}"{a}/>'


def wrap(s, size, width):
    """s in lines of at most width px (a rough Rubik measure: 0.56 em a character)"""
    n = max(1, int(width / (size * 0.56)))
    out, cur = [], ""
    for w in s.split():
        if cur and len(cur) + 1 + len(w) > n:
            out.append(cur)
            cur = w
        else:
            cur = f"{cur} {w}" if cur else w
    if cur:
        out.append(cur)
    return out


def svg(h, body, label):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{h}" viewBox="0 0 {W} {h}" '
            f'font-family="{FONT}" role="img" aria-label="{esc(label)}">\n'
            f'{r(0, 0, W, h, BG)}\n' + "\n".join(body) + "\n</svg>\n")


def title(o, head, sub):
    o.append(t(28, 40, head, 22, TEXT, weight=600))
    y = 64
    for line in wrap(sub, 13, W - 56):
        o.append(t(28, y, line, 13, MID))
        y += 18
    return y + 6


# ------------------------------------------------------------------ the keyboard (27 keys, F3 .. G5) ----------------
NOTE = ["F", "F#", "G", "G#", "A", "A#", "B", "C", "C#", "D", "D#", "E"]
WW, WH, BW, BH = 50, 172, 30, 106
KX = (W - 16 * WW) / 2


def black(k):
    return (0x54A >> ((k + 5) % 12)) & 1


def name(k):
    return f"{NOTE[k % 12]}{3 + (k + 5) // 12}"


def place(k):
    return sum(1 for i in range(k) if black(i) == black(k))


def key_x(k):
    return KX + place(k) * WW if not black(k) else KX + (place(k - 1) + 1) * WW - BW / 2


def keyboard(o, y, keys, held=(), foot=None, legend=None, held_text="held for the layer"):
    """keys: k -> (label, colour); held: the keys held for the layer (outlined, HOLD). Black keys' labels sit in a
    band above the keys, the white keys' at their foot"""
    band = 40
    ky = y + band
    for k in range(27):                                 # white keys
        if black(k):
            continue
        x, lab = key_x(k), keys.get(k)
        fill = "#EDE8F2" if not lab else lab[1]
        o.append(r(x + 1, ky, WW - 2, WH, fill, 5, stroke=LINE, opacity=None if lab else 0.18))
        if k in held:
            o.append(r(x + 3, ky + 2, WW - 6, WH - 4, "none", 4, stroke=TEXT, sw=2.5, dash="5 3"))
        if lab:
            lines = lab[0].split("\n")
            for i, s in enumerate(lines):
                o.append(t(x + WW / 2, ky + WH - 34 - 14 * (len(lines) - 1 - i), s, 11, BG, "middle", 600))
        o.append(t(x + WW / 2, ky + WH - 10, name(k), 10, BG if lab else MID, "middle"))
    for k in range(27):                                 # black keys over them
        if not black(k):
            continue
        x, lab = key_x(k), keys.get(k)
        o.append(r(x, ky, BW, BH, ROW if not lab else lab[1], 4, stroke=BG, sw=2))
        if k in held:
            o.append(r(x + 2, ky + 2, BW - 4, BH - 4, "none", 3, stroke=TEXT, sw=2.5, dash="5 3"))
        o.append(t(x + BW / 2, ky + BH - 9, name(k), 8, BG if lab else DIM, "middle"))
        if lab:
            lines = lab[0].split("\n")
            for i, s in enumerate(lines):
                o.append(t(x + BW / 2, y + band - 8 - 13 * (len(lines) - 1 - i), s, 10.5, lab[1], "middle", 600))
    y = ky + WH + 22
    if legend:                                          # (name, colour) swatches
        x = 28
        for n, c in legend:
            o.append(r(x, y - 11, 14, 14, c, 3))
            o.append(t(x + 20, y, n, 12, SOFT))
            x += 34 + len(n) * 7.5
        y += 24
    if held:
        o.append(r(28, y - 11, 22, 14, "none", 3, stroke=TEXT, sw=2, dash="5 3"))
        o.append(t(58, y, held_text, 12, SOFT))
        y += 22
    for line in foot or ():
        for s in wrap(line, 13, W - 56):
            o.append(t(28, y, s, 13, SOFT))
            y += 18
        y += 4
    return y + 10


def keys_page(label, head, sub, keys, held=(), foot=None, legend=None, held_text="held for the layer"):
    o = []
    y = title(o, head, sub)
    y = keyboard(o, y + 14, keys, held, foot, legend, held_text)
    return svg(y, o, label)


def btn_keys(ks, names):
    return {k: (n, BTN_C[n]) for k, n in zip(ks, names)}


ROW1 = ["FX", "SCL", "ENV", "LFO", "EDIT", "GLO"]
ROW2 = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]
NAV_BLACK = [13, 15, 17, 20, 22, 25]                    # F#4 G#4 A#4 C#5 D#5 F#5 (hal/fm1_input.h FM1_NAV_KEY)
M3_WHITE = [0, 2, 4, 6, 7, 9, 12, 14, 16, 18, 19, 21]   # F3 .. D4, F4 .. D5 (FM1_NAV_WHITE)
F5, FS5, G5 = 24, 25, 26
WHITES = [k for k in range(27) if not black(k)]
BLACKS = [k for k in range(27) if black(k)]


def keys_m(n):
    if n == 1:
        keys = btn_keys(NAV_BLACK, ROW1)
        held, mod = (F5,), "F5"
    elif n == 2:
        keys = btn_keys(NAV_BLACK, ROW2)
        held, mod = (G5,), "G5"
    else:
        keys = btn_keys(M3_WHITE, ROW1 + ROW2)
        keys.update(btn_keys([20, 22], ["OCT−", "OCT+"]))
        keys[17] = ("HEAR", ACT_C)
        held, mod = (FS5,), "F#5"
    keys[held[0]] = (f"M{n}", SOFT)
    foot = {1: ["The top row of buttons on the black keys. F#5 is GLO here, not M3."],
            2: ["The bottom row of buttons on the black keys. F#5 is REC here, not M3."],
            3: ["All twelve buttons on the white keys, in one row: the top row's six, a gap at E4, then the bottom "
                "row's. C#5 / D#5 are OCT− / OCT+, A#4 hears the note or chord at the cursor (stopped).",
                "A key held holds its button, so long presses work (C4: undo, F4: the menu). Each key lights as its "
                "button does. On ARP in SHAPE mode the white keys stay SHAPE's steps. F5 and G5 play notes here."]}[n]
    return keys_page(f"Hold {mod} (M{n})", f"Hold {mod} (M{n})",
                     "Its keys press the panel's buttons, held as long as the key; one colour a button, as on the "
                     "panel picture.", keys, held, foot)


def keys_glo():
    keys = {k: (f"SOLO\n{i + 1}", TRK[i]) for i, k in enumerate([0, 2, 4, 6])}
    keys.update({k: (f"MUTE\n{i + 1}", TRK[i]) for i, k in enumerate([1, 3, 5, 8])})
    keys[7] = ("UN-\nMUTE\nALL", SOFT)
    keys[12] = ("TAP", THEME)
    return keys_page("GLO's main page", "GLO: its main page (tap GLO, or hold it)",
                     "Mutes latch (lit: sounding); a solo lasts while its key is held. KNOB 1–4 set the tracks' "
                     "levels; GLO + PLAY restarts from the top; OCT− puts back what the layer changed.", keys,
                     foot=["Tap GLO again for MIXER, GLOBAL, SYSTEM, WEIGHTS, WEIGHTS 2; SELECT walks them as one "
                           "list. Held, the page shows only while GLO is down."])


def keys_scl():
    keys = {k: (NOTE[k % 12], MARK) for k in range(27) if k not in (F5, FS5, G5)}
    return keys_page("Hold SCL", "Hold SCL: the root on the keys",
                     "Any key makes its note the ROOT. KNOB 1–4: ROOT, SCALE, CHORD, VOICING. OCT− puts back "
                     "what the layer changed.", keys,
                     foot=["F5, G5 and F#5 are M1, M2 and M3 (the same notes an octave down set those roots)."])


PUNCH = ["LOOP\n1/4", "LOOP\n1/8", "LOOP\n1/16", "LOOP\n1/32", "STUT-\nTER", "REV", "STOP", "HALF",
         "LOW\nPASS", "HIGH\nPASS", "PHONE", "CRUSH", "ALIAS", "GATE", "ECHO", "WOB-\nBLE",
         "KILL\nLOW", "KILL\nMID", "KILL\nHIGH", "RWD", "OCT\nUP", "FLAN-\nGER", "PAN", "FREE-\nZE", "LOOP\n1/8T",
         "MONO", "FADE\nOUT"]                           # punch.c PUNCH_NAME, short


PF = ["REPEAT", "TAPE", "FILTER", "DIRT", "MOTION", "SPACE", "MIX"]   # ui_layer.c PUNCH_FAM, punch_fam_col
PF_C = [TRK[0], TRK[1], TRK[2], "#FFB8A6", TRK[3], "#C9BFFF", MID]
PUNCH_FAM = [0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 5, 4, 2, 2, 2, 1, 1, 4, 4, 5, 0, 6, 6]


def punch_colour(i):
    return PF_C[PUNCH_FAM[i]]


def keys_fx():
    keys = {k: (PUNCH[i], punch_colour(i)) for i, k in enumerate(WHITES)}
    keys.update({k: (PUNCH[16 + i], punch_colour(16 + i)) for i, k in enumerate(BLACKS)})
    return keys_page("Hold FX: master punches", "Hold FX: a punch effect on each key",
                     "The effect lasts while the key is held, on the whole master. Double-tap FX to lock the layer; "
                     "KNOB 1 browses this reference on the screen.", keys,
                     legend=list(zip(PF, PF_C)),
                     foot=["F5 (ECHO), G5 (WOBBLE) and F#5 (FADE OUT) are M1, M2 and M3 with the key shortcuts "
                         "built in: reach those three in a section's punch editor with KNOB 1."])


def keys_seq():
    keys = {k: (f"P{i + 1}", THEME) for i, k in enumerate(WHITES[:8])}
    keys.update({k: (f"T{i + 1}", TRK[i % 4]) for i, k in enumerate(BLACKS[:8])})
    return keys_page("Hold SEQ: patterns and tracks", "Hold SEQ: patterns and tracks, on any page",
                     "The first eight white keys pick the selected track's pattern 1–8 (an empty one arms one "
                     "recording pass); the first eight black keys pick track 1–8. SEQ + SELECT steps the pattern.",
                     keys, (), ["Double-tap a used pattern key: one recording pass there. Hold a pattern key alone "
                                "for its dialog (the next picture)."])


def keys_dialog():
    keys = {k: ("COPY\nHERE", MARK) for k in WHITES[:8]}
    keys[WHITES[2]] = ("DEL-\nETE", REC)
    keys[8] = ("HALVE", THEME)
    keys[10] = ("DOUBLE", PLAY_C)
    return keys_page("A pattern key held: its dialog", "A pattern key held: its dialog",
                     "Hold a pattern key alone for a second (here pattern 3, A3). The same key again deletes it, "
                     "another pattern key copies it there; C#4 halves it, D#4 doubles it. OCT− cancels.",
                     keys, (4,), ["Keep the key (or SEQ) held to halve or double again; hold EDIT on the playing "
                                  "pattern to undo."], held_text="the pattern key held")


LANES = ["KICK", "SNARE", "CLAP", "HAT\nCL", "HAT\nOP", "TOM", "RIM", "BELL"]   # eng_drum.c, the lanes' order


def keys_drum():
    keys = {k: (f"{i + 1}", SOFT if i < 14 else MID) for i, k in enumerate(WHITES)}
    keys.update({k: (LANES[i], TRK[i % 4]) for i, k in enumerate(BLACKS[:8])})
    keys[20] = ("ACC", THEME)
    keys[22] = ("PAGE", MARK)
    return keys_page("The drum grid", "The drum grid (NOTES on a drum track)",
                     "White keys: the page's 16 steps (a tap sets the selected lane there). Black keys 1–8: the lane "
                     "(and play it). C#5 held: ACC, the white keys set accents. D#5: the next page (wraps).",
                     keys, (), ["Steps 15 and 16 are F5 and G5, M1 and M2: hold M3 (F#5) to tap them. F#5 (page up) "
                                "is M3; D#5 goes round. KNOB 1 STEP, 2 LANE, 3 HIT, 4 ACC edit the cursor's step; "
                                "tap EDIT clears it."])


def keys_shape():
    keys = {k: (f"{i + 1}", PLAY_C) for i, k in enumerate(WHITES)}
    keys[FS5] = ("M3", SOFT)
    return keys_page("ARP SHAPE steps", "ARP in SHAPE mode: hold M3 (F#5) or OCT−",
                     "On the ARP page with MODE SHAPE, the 16 white keys are the steps of the page PRESETS picks: a "
                     "tap turns one ON or OFF, the lit ones are ON.", keys, (FS5,),
                     ["With M3 held F5 and G5 are keys, steps 15 and 16. The M3 button shortcuts give way here."])


# ------------------------------------------------------------------ the panel's buttons -----------------------------
BUTTONS = [
    ("FX", "SHARED rack, then NOTE FX", "punches on the keys"),
    ("SCL", "SCALES, SCL, CHORD", "root on the keys"),
    ("ENV", "ENV, ENV DEST", "+ SELECT on NOTES: length"),
    ("LFO", "LFO pages, MOD", "+ a knob: a temporary change"),
    ("EDIT", "the engine's pages (again: its map); NOTES: delete", "undo"),
    ("GLO", "main page, MIXER, GLOBAL …", "main page while held"),
    ("HOME", "Stage (again: chord view)", "the menu"),
    ("SAVE", "PRESETS / USER / PROJECT", "save the project"),
    ("ARP", "ARP, ARP 2", ""),
    ("SEQ", "NOTES … SONG", "PATTERNS"),
    ("PLAY", "start / stop", "with REC: record"),
    ("REC", "arm (SONG: launch)", "capture"),
]
KNOBS = [("SELECT", "pages (Stage: BPM …)"), ("PRESETS", "sounds; EDIT: sections"),
         ("ALGORITHM", "the track"), ("KNOB 1–4", "the page's four controls")]


def buttons():
    o = []
    y = title(o, "The panel's buttons", "Tap and hold, in the panel's two rows (the M1 and M2 rows); one colour a "
              "button, the same on its shortcut keys.")
    bw, bh, gap = 132, 104, 6
    for row in range(2):
        for i in range(6):
            n, tap, hold = BUTTONS[row * 6 + i]
            x, yy, c = 28 + i * (bw + gap), y + 8 + row * (bh + gap), BTN_C[BUTTONS[row * 6 + i][0]]
            o.append(r(x, yy, bw, bh, SURF, 8, stroke=c, sw=2))
            o.append(r(x, yy, bw, 26, c, 8))
            o.append(r(x, yy + 18, bw, 8, c))
            o.append(t(x + bw / 2, yy + 18, n, 13, BG, "middle", 700))
            ly = yy + 44
            for s in wrap("tap: " + tap, 11, bw - 14)[:3]:
                o.append(t(x + 7, ly, s, 11, SOFT))
                ly += 14
            if hold:
                for s in wrap("hold: " + hold, 11, bw - 14)[:2]:
                    o.append(t(x + 7, ly, s, 11, MID))
                    ly += 14
    y += 8 + 2 * (bh + gap) + 10
    for i, (n, tap) in enumerate([("OCT−", "back, cancel; Stage: octave down"),
                                  ("OCT+", "enter, confirm; hold: the page's sheet; Stage: octave up")]):
        x = 28 + i * 3 * (bw + gap)
        ww = 3 * bw + 2 * gap
        o.append(r(x, y, ww, 40, SURF, 8, stroke=OCT_C, sw=2))
        o.append(r(x, y, 64, 40, OCT_C, 8))
        o.append(t(x + 32, y + 25, n, 13, BG, "middle", 700))
        o.append(t(x + 76, y + 25, tap, 12, SOFT))
    y += 52
    o.append(t(28, y + 6, "Both octave buttons on Stage: reset. Held together at power-on: calibration.", 12, MID))
    y += 26
    kw = (W - 56 - 3 * 10) / 4
    for i, (n, d) in enumerate(KNOBS):
        x = 28 + i * (kw + 10)
        o.append(r(x, y, kw, 54, SURF, 8, stroke=LINE))
        o.append(f'<circle cx="{x + 26:g}" cy="{y + 27:g}" r="15" fill="{ROW}" stroke="{SOFT}" stroke-width="2"/>')
        o.append(f'<line x1="{x + 26:g}" y1="{y + 27:g}" x2="{x + 26:g}" y2="{y + 15:g}" stroke="{SOFT}" '
                 f'stroke-width="2"/>')
        o.append(t(x + 50, y + 23, n, 12, TEXT, weight=600))
        o.append(t(x + 50, y + 40, d, 11, MID))
    return svg(y + 74, o, "The panel's buttons")


# ------------------------------------------------------------------ a page's controls ------------------------------
def card(label, head, sub, knobs, rows):
    """knobs: four (name, what) for KNOB 1..4 (None: unused); rows: (control, what) beside them"""
    o = []
    y = title(o, head, sub)
    kw = (W - 56 - 3 * 12) / 4
    shade = [TRK[0], "#FF9A82", "#FFB8A6", "#FFD5CA"]  # KNOB 1 the track's colour, the others its shades
    kh = 0
    for i, k in enumerate(knobs):
        x = 28 + i * (kw + 12)
        lines = wrap(k[1], 12, kw - 20) if k else []
        h = 96 + 16 * len(lines)
        kh = max(kh, h)
    for i, k in enumerate(knobs):
        x = 28 + i * (kw + 12)
        o.append(r(x, y, kw, kh, SURF, 10, stroke=LINE))
        c = shade[i] if k else DIM
        o.append(f'<circle cx="{x + kw / 2:g}" cy="{y + 34:g}" r="20" fill="{ROW}" stroke="{c}" stroke-width="3"/>')
        o.append(f'<line x1="{x + kw / 2:g}" y1="{y + 34:g}" x2="{x + kw / 2 + 10:g}" y2="{y + 18:g}" stroke="{c}" '
                 f'stroke-width="3" stroke-linecap="round"/>')
        o.append(t(x + kw / 2, y + 74, f"KNOB {i + 1}", 11, MID, "middle"))
        o.append(t(x + kw / 2, y + 92, k[0] if k else "—", 14, c if k else DIM, "middle", 600))
        for j, s in enumerate(wrap(k[1], 12, kw - 20) if k else []):
            o.append(t(x + kw / 2, y + 110 + 16 * j, s, 12, SOFT, "middle"))
    y += kh + 16
    cw = (W - 56 - 12) / 2
    col_y = [y, y]
    for i, (ctl, what) in enumerate(rows):
        col = 0 if col_y[0] <= col_y[1] else 1
        x, yy = 28 + col * (cw + 12), col_y[col]
        c = BTN_C.get(ctl.split()[-1], SOFT)            # (a button's own colour: "hold OCT+", "tap EDIT")
        lines = wrap(what, 12, cw - 24)
        h = 30 + 16 * len(lines)
        o.append(r(x, yy, cw, h, SURF, 8, stroke=LINE))
        o.append(r(x, yy, 5, h, c, 2))
        o.append(t(x + 16, yy + 20, ctl, 12, c, weight=700))
        for j, s in enumerate(lines):
            o.append(t(x + 16, yy + 38 + 16 * j, s, 12, SOFT))
        col_y[col] = yy + h + 8
    return svg(max(col_y) + 16, o, label)


CARDS = {
    "page-stage": ("Stage", "Stage (HOME)", "The live screen: a lane a track. The four knobs are the selected track's "
                   "sound, by engine (here the Prophet's).",
                   [("CUTOFF", "the filter"), ("RES", "resonance"), ("ENV", "filter envelope amount"),
                    ("RELEASE", "amp release")],
                   [("SELECT", "the HOME SELECT value (BPM by default)"), ("OCT− / OCT+", "octave; both: reset"),
                    ("tap HOME", "the chord view; again: back"),
                    ("hold OCT+", "Init sound, Save as user…, Favourite, Undo last load, Sound designer, Add track, "
                     "Remove track")]),
    "page-browser": ("The sound browser", "The sound browser (turn PRESETS)", "Each engine's factory sounds first, "
                     "then the user slots of that engine.",
                     [("LIST", "categories, FAV, RECENT"), ("SOUND", "scroll (as PRESETS)"), ("FAV", "favourite on / off"),
                      ("ENGINE", "another engine's init sound")],
                     [("OCT+", "keep the sound"), ("OCT−", "back to the one you had"),
                      ("hold EDIT", "back to the sound before browsing")]),
    "page-notes": ("NOTES", "SEQ › NOTES", "Steps and recorded takes on a piano roll. Keys and MIDI enter notes or "
                   "chords while stopped.",
                   [("STEP", "walk every step and each pitch of a chord"), ("PITCH", "on an empty step: insert"),
                    ("LENGTH", "quarter steps (hold ENV: fine)"), ("VEL", "1–127 (hold ENV: slide)")],
                   [("tap EDIT", "delete the highlighted pitch"), ("OCT+", "empty: a note; on a note: its sheet"),
                    ("hold OCT+", "with SELECT / KNOB 1 / 2: a range; alone: Quantize, Clear pattern, Clear motion"),
                    ("PRESETS", "zoom"), ("hold SCL + SELECT", "move the note"),
                    ("hold ENV + SELECT", "longer / shorter"), ("hold EDIT", "undo (recording: erase)"),
                    ("M3 + A#4", "hear the note or chord")]),
    "page-patterns": ("PATTERNS", "SEQ › PATTERNS (or hold SEQ)", "A row a track, a cell a pattern: the playing one "
                      "filled, ones with notes raised, a queued one dashed.",
                      [("TRACK 1", "its next pattern"), ("TRACK 2", "its next pattern"), ("TRACK 3", "its next pattern"),
                       ("TRACK 4", "its next pattern")],
                      [("OCT+", "Copy to…, Delete pattern"), ("OCT−", "clear what is queued"),
                       ("PRESETS", "the selected track's sound"), ("SELECT", "on to SONG"),
                       ("black keys", "tracks 1–8 (unarmed)")]),
    "page-song": ("SONG", "SEQ › SONG", "A column a section placed (its letter and bars:beats), played left to right; "
                  "the + column adds one.",
                  [("COLUMN", "pick one; past the last: +"), ("SECTION", "which section it plays"), None, None],
                  [("tap OCT+", "a column: its section page; +: add a section of what plays now"),
                   ("hold OCT+", "Duplicate, Copy as new, Move, Copy, Delete section, Loop song, Clear song"),
                   ("PLAY", "play from the picked column"), ("REC", "launch the section live"),
                   ("tap FX", "the section's punches"), ("hold EDIT", "undo a song edit")]),
    "page-section": ("The section page", "The section page (OCT+ on a column)", "One section's patterns and length; "
                     "what you change here changes every column of it.",
                     [("TRACK 1", "its pattern (below 1: REST)"), ("TRACK 2", "its pattern"), ("TRACK 3", "its pattern"),
                      ("TRACK 4", "its pattern")],
                     [("SELECT", "the length, a beat a detent (below one beat: AUTO)"), ("PLAY", "loop this section"),
                      ("OCT+", "Set preset, Remove preset, Punch FX, Copy as new section"),
                      ("tap FX", "Punch FX"), ("OCT−", "back to SONG")]),
    "page-punch": ("The punch editor", "The punch editor (FX on SONG or the section page)", "Master punches on a "
                   "section, in track 1's steps, on every loop of its pattern.",
                   [("EFFECT", "OFF clears it (or an effect key)"), ("LANE", "one of four"), ("START", "track 1's step"),
                    ("END", "track 1's step")],
                   [("OCT+", "a free punch"), ("EDIT", "remove it (hold: undo)"), ("PLAY", "audition"),
                    ("OCT−", "back")]),
    "page-motion": ("MOTION", "SEQ › MOTION", "Knob moves recorded into the pattern: arm REC, press PLAY, turn a sound "
                    "knob.",
                    [("PLAY", "motion on / off"), ("LANE", "SEQ held: clear it"), ("STEP", "in the lane"),
                     ("VALUE", "SEQ held: remove the point")],
                    [("OCT+", "Play, Clear that parameter, Clear all motion"), ("hold EDIT", "undo")]),
    "page-fx": ("The effects racks", "FX: SHARED, then NOTE FX", "SHARED: three slots every track can send to. NOTE "
                "FX: the track's note effects and its Send.",
                [("EFFECT", "the row's effect"), ("ROW", "a slot, RETURN, Send"), ("ROUTE", "SHARED: CHAIN / SPLIT"),
                 ("AMOUNT", "MIX, SEND or RETURN")],
                [("OCT+", "open the slot (empty: the picker)"), ("hold OCT+", "SHARED: Routing, Clear shared FX"),
                 ("SELECT", "the racks and used slots"), ("OCT−", "slot, rack, Stage")]),
    "page-mod": ("MOD", "LFO › MOD", "Four modulation routes.",
                 [("SOURCE", "LFO, ENV, VEL, KEY, …"), ("ROUTE", "1–4"), ("DEST", "PITCH, CUT, AMP, …"),
                  ("AMOUNT", "−100…+100 %")],
                 [("hold LFO + a knob", "a temporary change; OCT+ keeps it")]),
    "page-mixer": ("MIXER", "GLO › MIXER", "The selected track's mix; ALGORITHM picks the track.",
                   [("LEVEL", ""), ("PAN", ""), None, ("MUTE", "")],
                   [("SELECT", "GLO's pages as one list: main, MIXER, GLOBAL … WEIGHTS 2"),
                    ("GLO", "the next page")]),
    "page-projects": ("PROJECT", "SAVE › PROJECT", "Four slots, the template and NEW.",
                      [("SLOT", "A–D, TMPL, NEW"), ("BOOT", "loads at power-on"), ("LOAD", "GO"), ("SAVE", "GO")],
                      [("OCT+", "Load, Save here, Rename, Boot, Erase"), ("hold SAVE", "save to the slot")]),
}


def files():
    out = {"buttons.svg": buttons(), "keys-m1.svg": keys_m(1), "keys-m2.svg": keys_m(2), "keys-m3.svg": keys_m(3),
           "keys-glo.svg": keys_glo(), "keys-scl.svg": keys_scl(), "keys-fx.svg": keys_fx(),
           "keys-seq.svg": keys_seq(), "keys-pattern-dialog.svg": keys_dialog(), "keys-drum-grid.svg": keys_drum(),
           "keys-arp-shape.svg": keys_shape()}
    for n, c in CARDS.items():
        out[n + ".svg"] = card(*c)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    want = files()
    if a.check:
        bad = [n for n, c in want.items() if not (OUT / n).is_file() or (OUT / n).read_text() != c]
        for n in bad:
            print(f"gen_shortcuts: {n} out of date", file=sys.stderr)
        sys.exit(1 if bad else 0)
    OUT.mkdir(parents=True, exist_ok=True)
    for n, c in want.items():
        (OUT / n).write_text(c)
    print(f"gen_shortcuts: {len(want)} pictures -> {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
