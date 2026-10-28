#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""The step-by-step guide to PATTERNS and SONG: one SVG a step (a device screen at 1.5x with numbered marks, what to
press beside it) and docs/song-guide/README.md linking them, from the same text.

  tools/gen_song_guide.py            # writes docs/song-guide/
  tools/gen_song_guide.py --check    # exit 1 when a file there differs from what it would write

The screens are drawings in the redesign's palette (docs/design/mock), not firmware renders: they follow the device's
layout (firmware/src/ui_patterns.c, ui_popup.c) closely enough to find things on it. The firmware's own renders are the
manual's screenshots (tests/ui_render.c). Keep the controls here in step with web/manual/index.html "Songs".
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "song-guide"

BG, SURF, ROW, LINE = "#17141F", "#221E2C", "#262033", "#2E2939"
TEXT, SOFT, MID, DIM = "#F3EEF8", "#C9C2D6", "#9A93A8", "#5A5468"
THEME, REC, MARK = "#F5A623", "#FF5A6E", "#7FE0FF"
TRK = ["#FF7A5C", "#FFC145", "#46D9B0", "#9D8CFF"]
TRK_NAME = ["drums", "bass", "chords", "lead"]
FONT = "Rubik, Inter, 'Helvetica Neue', Arial, sans-serif"
S = 1.5                                                  # the screen's scale on the page
SX, SY = 28, 92                                          # the screen's place on the page
W, H = 940, 92 + 360 + 36


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def t(x, y, s, size=10, fill=TEXT, anchor="start", weight=400):
    return (f'<text x="{x:g}" y="{y:g}" font-size="{size:g}" fill="{fill}" text-anchor="{anchor}" '
            f'font-weight="{weight}">{esc(s)}</text>')


def r(x, y, w, h, fill, rx=0, stroke=None, sw=1, dash=None, opacity=None):
    a = f' stroke="{stroke}" stroke-width="{sw:g}"' if stroke else ""
    a += f' stroke-dasharray="{dash}"' if dash else ""
    a += f' opacity="{opacity:g}"' if opacity is not None else ""
    return f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" rx="{rx:g}" fill="{fill}"{a}/>'


# ------------------------------------------------------------------ the example song --------------------------------
# a section: per track a pattern number (None: REST), its length text, AUTO or set
SECT = {
    "A": ([1, 1, None, None], "2 bars", True),
    "B": ([2, 2, 1, None], "4 bars", True),
    "C": ([2, 3, 2, 1], "3 bars", False),
}
PAT_BARS = {(0, 1): 1, (0, 2): 1, (1, 1): 2, (1, 2): 4, (1, 3): 4, (2, 1): 2, (2, 2): 2, (3, 1): 4}   # (track, P): bars


# ------------------------------------------------------------------ device screens (240 x 240) ----------------------
def head(left="", right="", right_fill=THEME, chip=None, chip_fill=TEXT):
    o = [t(8, 19, left, 12, TEXT, weight=500)]
    if chip:
        w = 10 + 5.6 * len(chip)
        o.append(r(232 - w, 7, w, 16, ROW, 4, chip_fill, 1, "3 2") + t(232 - w / 2, 18.5, chip, 8, chip_fill, "middle", 600))
    elif right:
        o.append(t(232, 19, right, 12, right_fill, "end", 600))
    return o


def brief(length):
    """a card's length, bars:beats ("2 bars" -> "2:0")"""
    words = length.split()
    bars = int(words[0]) if "bar" in length else 0
    beats = int(words[-2]) if "beat" in length else 0
    return f"{bars}:{beats}"


def cell(o, k, b, x, y, h, playing, used, queued=False):
    """a pattern as PATTERNS and the section page draw it: its number in the track's colour"""
    c = TRK[k]
    size = 10 if h < 24 else 12
    ny = y + h / 2 + size * 0.36
    if playing:
        o.append(r(x, y, 23, h, c, 4) + t(x + 11.5, ny, str(b + 1), size, BG, "middle", 600))
    elif used:
        o.append(r(x, y, 23, h, SURF, 4) + t(x + 11.5, ny, str(b + 1), size, c, "middle", 600))
    elif queued:
        o.append(r(x, y, 23, h, SURF, 4) + t(x + 11.5, ny, str(b + 1), size, MID, "middle", 600))
    else:                                                    # (empty: no number)
        o.append(r(x + 0.5, y + 0.5, 22, h - 1, "none", 4, LINE))
    if queued:
        o.append(r(x - 1, y - 1, 25, h + 2, "none", 5, c, 1.4, "2 2"))


def chip(o, k, y, h, sel):
    c = TRK[k]
    if sel:
        o.append(r(2, y + 2, 24, h - 4, c, 6) + t(14, y + h / 2 + 4.5, str(k + 1), 13, BG, "middle", 600))
    else:
        o.append(r(2.5, y + 2.5, 23, h - 5, BG, 6, c, 1.5) + t(14, y + h / 2 + 4.5, str(k + 1), 13, c, "middle", 600))


def song_screen(cols, cursor=0, playing=None, queued=None, sel_range=None, status=None, msg=None, foot=None,
                playhead=None):
    """SONG: a card a column (letter, bars:beats), a block a track (its pattern's number; REST), "+" at the end"""
    o = [r(0, 0, 240, 240, BG)]
    left = msg if msg else "0:24" if cols else ""
    o += head(left, "SONG" if not status else "", chip=status)
    shown = cols + ["+"]
    first = max(0, min(cursor - 1, len(shown) - 4))
    for j, name in enumerate(shown[first:first + 4]):
        i = first + j
        x = 9 + 56 * j
        sel = i == cursor
        if name == "+":
            o.append(r(x, 31, 54, 34, "none", 6, THEME if sel else LINE, 1.2, "3 2"))
            o.append(t(x + 27, 54, "+", 18, THEME if sel else DIM, "middle", 500))
            continue
        pats, length, auto = SECT[name]
        inrange = sel_range and sel_range[0] <= i <= sel_range[1]
        on = i == playing
        fill = TEXT if sel else THEME if on else "#3A3348" if inrange else SURF
        ink = BG if sel or on else TEXT
        o.append(r(x, 31, 54, 34, fill, 6))
        o.append(t(x + 5, 46, name, 12, ink, weight=600))
        o.append(t(x + 4, 60, brief(length), 10, BG if sel or on else MID))
        if i == queued:
            o.append(r(x - 1.5, 29.5, 57, 37, "none", 7, THEME, 1.5, "3 2"))
        for k, p in enumerate(pats):
            y = 70 + 32 * k
            if p:
                o.append(r(x, y, 54, 28, TRK[k], 5) + t(x + 7, y + 18.5, str(p), 12, BG, weight=600))
            else:
                o.append(t(x + 7, y + 18.5, "REST", 10, DIM, weight=500))
    for j in range(1, 4):
        o.append(r(8 + 56 * j, 68, 1, 128, LINE))
    if playhead is not None:
        o.append(r(playhead, 30, 2, 166, TEXT))
    if foot is None and cursor < len(cols):
        foot = (f"{cols[cursor]} \u00b7 {SECT[cols[cursor]][1]}{' AUTO' if SECT[cols[cursor]][2] else ''}",
                f"used {cols.count(cols[cursor])}x")
    elif foot is None:
        foot = ("OCT+ adds what plays now", "")
    o.append(t(8, 216, foot[0], 12 if cursor < len(cols) else 10, TEXT if cursor < len(cols) else MID, weight=500))
    if foot[1]:
        o.append(t(232, 216, foot[1], 10, MID, "end"))
    o.append(r(8, 228, 224, 6, SURF, 3))
    if playing is not None and not status:
        o.append(r(8, 228, 80, 6, TEXT, 3))
    return o


def patterns_screen(playing, used, queued=None, sel_trk=0, song=None, song_cur=0, msg=None):
    """PATTERNS: a row a track, its 16 patterns as numbered cells, 1-8 above 9-16 (the one playing filled, ones with
    notes raised, empty outlined, queued dashed)"""
    o = [r(0, 0, 240, 240, BG)]
    o += head(msg or "\u25b6 124", "PATTERNS")
    for k in range(4):
        y = 30 + 43 * k
        chip(o, k, y, 43, k == sel_trk)
        for b in range(16):
            cell(o, k, b, 30 + 26 * (b % 8), y + 2 + 21 * (b // 8), 19, b == playing[k], b in used[k],
                 bool(queued) and queued.get(k) == b)
        o.append(r(33 + 26 * (playing[k] % 8), y + 17 + 21 * (playing[k] // 8), 17, 2, BG, 1))
    o.append(t(8, 226, "SONG", 10, MID, weight=500))
    if song:
        for i, l in enumerate(song[:6]):
            x = 62 + 29 * i
            on = i == song_cur
            o.append(r(x, 209, 26, 24, TEXT if on else SURF, 6) + t(x + 13, 226, l, 11, BG if on else MID, "middle", 500))
    else:
        o.append(t(62, 226, "Empty", 10, DIM))
    return o


def section_screen(name, length, auto=True, uses=2, chip_text="SECTION", looping=False, edited=None):
    """the section page: the length (its use count at the right), PATTERNS' cells with the section's pattern filled,
    a lane a track"""
    pats = list(SECT[name][0])
    if edited:
        for k, v in edited.items():
            pats[k] = v
    o = [r(0, 0, 240, 240, BG)]
    o += head("120", chip=f"LOOPING {name}" if looping else f"{chip_text} {name}", chip_fill=THEME)
    o.append(r(4, 31, 232, 22, SURF, 5) + t(10, 46, "LENGTH", 8, MID, weight=500))
    o.append(t(80, 47, length, 13, TEXT, weight=500))
    if auto:
        ax = 80 + 7.2 * len(length) + 8
        o.append(r(ax, 34, 40, 16, THEME, 5) + t(ax + 20, 46, "AUTO", 8.5, BG, "middle", 700))
    o.append(t(230, 46, f"{uses}x", 10, MID, "end"))
    used = [{0, 1, 2}, {0, 1, 2, 3}, {0, 1}, {0}]
    for k in range(4):
        y = 54 + 36 * k
        chip(o, k, y, 36, False)
        if pats[k] is None:
            o.append(r(30, y + 1, 205, 34, SURF, 4) + t(132, y + 22, "REST", 10, MID, "middle", 500))
            continue
        for b in range(16):
            cell(o, k, b, 30 + 26 * (b % 8), y + 1 + 18 * (b // 8), 16, b == pats[k] - 1, b in used[k])
    bars = int(length.split()[0])
    for bar in range(bars):
        x = 30 + bar * 202 / bars
        o.append(r(x, 201, 1, 36, LINE))
    for k in range(4):
        y = 204 + 8 * k
        if pats[k] is None:
            for x in range(30, 232, 6):
                o.append(r(x, y + 3, 2, 1, DIM))
            continue
        lb = PAT_BARS[(k, pats[k])]
        at, i = 0, 0
        while at < bars:
            end = min(at + lb, bars)
            x0, x1 = 30 + at * 202 / bars, 30 + end * 202 / bars
            o.append(r(x0 + 1, y, x1 - x0 - 2, 6, TRK[k] if i == 0 else TRK[k] + "88", 2))
            at += lb
            i += 1
        if lb * ((bars + lb - 1) // lb) > bars:          # (its last loop cut where the section ends)
            o.append(r(230, y - 1, 2, 8, REC))
    if looping:
        o.append(r(112, 201, 2, 36, TEXT))
    return o


def sheet(under, title, sub, rows, sel=0):
    """an action sheet over a screen: its title, five rows at most"""
    o = list(under) + [r(0, 0, 240, 240, BG, opacity=0.72)]
    rows = rows[:5]
    y0 = 240 - 34 - 34 * len(rows) - 4
    o.append(r(4, y0, 232, 34 + 34 * len(rows), SURF, 10, LINE))
    o.append(t(14, y0 + 23, title, 14, TEXT, weight=600) + t(226, y0 + 23, sub, 10, MID, "end"))
    for i, (label, value, red) in enumerate(rows):
        y = y0 + 34 + 34 * i
        if i == sel:
            o.append(r(4, y, 232, 34, "#3A2E22") + r(4, y, 4, 34, THEME))
        o.append(t(14, y + 22, label, 14, REC if red else TEXT if i == sel else SOFT, weight=500 if i == sel else 400))
        if value:
            o.append(t(226, y + 22, value, 12, THEME if i == sel else MID, "end"))
    return o


def confirm(under, question, sub):
    o = list(under) + [r(0, 0, 240, 240, BG, opacity=0.72)]
    o.append(r(14, 70, 212, 100, SURF, 12, LINE))
    o.append(t(120, 102, question, 13, TEXT, "middle", 700) + t(120, 124, sub, 11, MID, "middle"))
    o.append(t(120, 154, "OCT− no    OCT+ yes", 11, SOFT, "middle", 500))
    return o


# ------------------------------------------------------------------ a page a step -----------------------------------
def mark(n, x, y, w, h):
    """a numbered mark on the screen, in screen units"""
    X, Y = SX + x * S, SY + y * S
    return (r(X - 2, Y - 2, w * S + 4, h * S + 4, "none", 7, MARK, 2.2) +
            f'<circle cx="{X - 2:g}" cy="{Y - 2:g}" r="11" fill="{MARK}"/>' + t(X - 2, Y + 2.5, str(n), 12, BG, "middle", 700))


KEY = "#FFD27A"                                           # a control's name in the text


def rich(x, y, s, size=14, fill=SOFT, wrap=56):
    """text with [KEY]s (a control, KNOB_1 for "KNOB 1") in bold, wrapped at about wrap characters; -> its svg and
    the y after it. Spacing is the text's own."""
    import re
    tokens = []                                          # (text, is a control, a space before it)
    for m in re.finditer(r"(\s*)(\[[^\]]+\]|[^\s\[]+)", s):
        word = m.group(2)
        key = word.startswith("[")
        tokens.append((word[1:-1].replace("_", " ") if key else word, key, bool(m.group(1))))
    lines, line, n = [], [], 0
    for w, key, sp in tokens:
        if n + len(w) > wrap and line and sp:
            lines.append(line); line, n = [], 0
            sp = False
        line.append((w, key, sp and bool(line))); n += len(w) + sp
    if line:
        lines.append(line)
    out = []
    for ln in lines:
        body = "".join((" " if sp else "") + (f'<tspan fill="{KEY}" font-weight="700">{esc(w)}</tspan>' if key else esc(w))
                       for w, key, sp in ln)
        out.append(f'<text x="{x:g}" y="{y:g}" font-size="{size:g}" fill="{fill}" xml:space="preserve">{body}</text>')
        y += size + 9
    return "".join(out), y


def page(n, total, part, title, screen, marks, actions, see, tip):
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="{FONT}">',
         r(0, 0, W, H, "#0E0C13"),
         '<defs><clipPath id="scr"><rect width="240" height="240" rx="6"/></clipPath></defs>',
         t(SX, 36, f"STEP {n} OF {total}  ·  {part}", 12, MARK, weight=700),
         t(SX, 68, title, 26, TEXT, weight=700),
         f'<g transform="translate({SX} {SY}) scale({S})"><g clip-path="url(#scr)">' + "".join(screen) +
         '</g><rect width="240" height="240" rx="6" fill="none" stroke="#3A3446"/></g>']
    for i, m in enumerate(marks):                       # (x, y, w, h), numbered in order; or (n, x, y, w, h)
        o.append(mark(*m) if len(m) == 5 else mark(i + 1, *m))
    x, y = SX + 360 + 44, SY + 14
    o.append(t(x, y, "DO THIS", 12, MID, weight=700))
    y += 28
    for i, a in enumerate(actions):
        o.append(f'<circle cx="{x + 10}" cy="{y - 5}" r="11" fill="{MARK}"/>' + t(x + 10, y - 0.5, str(i + 1), 12, BG, "middle", 700))
        svg, y = rich(x + 30, y, a, 14, TEXT, 46)
        o.append(svg)
        y += 6
    y += 8
    o.append(t(x, y, "YOU SEE", 12, MID, weight=700))
    svg, y = rich(x, y + 24, see, 13, SOFT, 58)
    o.append(svg)
    if tip:
        y += 6
        svg, y2 = rich(x + 14, y + 24, tip, 12.5, SOFT, 58)
        o.append(r(x, y, W - x - 24, y2 - y + 2, "#1C1826", 8, "#2E2939") + r(x, y, 4, y2 - y + 2, THEME, 2))
        o.append(svg)
        y = y2
    h = max(H, int(y) + 24)                             # (a long step: the page grows to hold its text)
    o[0] = o[0].replace(f'viewBox="0 0 {W} {H}" width="{W}" height="{H}"', f'viewBox="0 0 {W} {h}" width="{W}" height="{h}"')
    o[1] = r(0, 0, W, h, "#0E0C13")
    o.append("</svg>")
    return "\n".join(o)


# ------------------------------------------------------------------ the steps ---------------------------------------
P_PLAY = [0, 0, 0, 0]
P_USED = [{0, 1, 2}, {0, 1, 2, 3}, {0, 1}, {0}]
STEPS = [
    ("patterns", "PATTERNS", "Open PATTERNS",
     patterns_screen(P_PLAY, P_USED, sel_trk=0),
     [(0, 30, 28, 172), (30, 75, 206, 40), (30, 32, 23, 19)],
     ["Hold [SEQ] for a moment (or press [SEQ] and turn [SELECT] to PATTERNS).",
      "Each row is a track: 1 drums, 2 bass, 3 chords, 4 lead, in its colour. Its cells are its patterns: 1-8 above, "
      "9-16 below.",
      "The filled cell is the pattern the track plays now; the line under its number shows how far it has played."],
     "Cells with notes are raised, their number in the track's colour; empty patterns are plain outlines, no number. "
     "At the foot: the song's sections, by letter.",
     "A pattern is one track's loop. Write its notes on SEQ > NOTES; this page chooses which one plays."),
    ("choose-patterns", "PATTERNS", "Choose what each track plays",
     patterns_screen([1, 1, 0, 0], P_USED, queued={1: 2}, sel_trk=1),
     [(30, 32, 206, 40), (30, 75, 206, 40)],
     ["Turn [KNOB_1] to pick track 1's pattern, [KNOB_2] track 2's, [KNOB_3] and [KNOB_4] tracks 3 and 4.",
      "Or: hold [SEQ] and press a white key (patterns 1-16) for the selected track; black keys pick the track."],
     "Stopped, the pattern changes at once. Playing, it waits for that track's loop to end: the waiting one is dashed.",
     "Each track changes on its own. To switch every track together, use a section (steps 5 and 19)."),
    ("pattern-sheet", "PATTERNS", "Copy or delete a pattern",
     sheet(patterns_screen([1, 1, 0, 0], P_USED, sel_trk=0), "Pattern 2", "T1",
           [("Copy to…", "", False), ("Delete pattern", "", True)]),
     [(4, 138, 232, 68)],
     ["Select the track ([ALGORITHM] or a black key with [SEQ] held) and its pattern.",
      "Tap [OCT+]: the pattern's sheet.",
      "Copy to…: turn any knob to the place, [OCT+] copies. Delete pattern asks first."],
     "The sheet over the grid. [OCT−] closes it; hold [EDIT] to undo.",
     "Copies are separate patterns. A song only points at patterns: editing pattern 2's notes changes every section "
     "using it."),
    ("open-song", "SONG", "Go to SONG",
     song_screen([], cursor=0),
     [(9, 31, 54, 34), (8, 202, 224, 20)],
     ["On PATTERNS, turn [SELECT] one page on: SONG.",
      "A new song is empty: one + card, \"OCT+ adds what plays now\"."],
     "SONG in the top right. Columns will appear left to right as you add sections.",
     "A section is one part of the song: the pattern (or a rest) each track plays, and how long."),
    ("add-first-section", "SONG", "Add your first section (A)",
     song_screen(["A"], cursor=1, msg="ADDED A"),
     [(65, 31, 54, 34), (9, 31, 54, 165)],
     ["Set each track's pattern for the first part (step 2): drums 1, bass 1. On SONG, turn [KNOB_1] to the + card.",
      "Tap [OCT+]: section A, a column of what every track plays."],
     "Card A with its length in bars:beats (2:0, two bars: AUTO, its longest pattern), its column showing each "
     "track's pattern number; REST is a resting track. The cursor stays on + for the next one.",
     "A track can rest in a section: set it on the section page (step 10), turning below pattern 1."),
    ("add-more-sections", "SONG", "Add more sections",
     song_screen(["A", "B", "A"], cursor=3, msg="PLACED A"),
     [(2, 65, 31, 54, 34), (3, 121, 31, 54, 34)],
     ["Hold [SEQ], change patterns for the next part (drums 2, bass 2, chords 1), let go.",
      "Turn [SELECT] back to SONG, [KNOB_1] to +, tap [OCT+]: section B.",
      "Patterns that match a section place it again: back to A's patterns, [OCT+] on +: A."],
     "\"ADDED B\" for new patterns, \"PLACED A\" for ones a section already has. Up to 26 sections (A-Z), 64 columns.",
     "Both As are the same section: what you change in one changes the other."),
    ("read-the-grid", "SONG", "Read the SONG grid",
     song_screen(["A", "B", "A", "C"], cursor=1),
     [(65, 31, 54, 34), (8, 68, 232, 32), (8, 202, 150, 20), (180, 202, 54, 20)],
     ["Cards: a column each, its section's letter and length in bars:beats (2:3 = 2 bars 3 beats, 4:0 = 4 bars).",
      "Rows: a track each, the number of the pattern it plays in each column; REST rests.",
      "Footer: the picked column's section and length, AUTO when it follows its longest pattern.",
      "How often the song uses that section."],
     "The picked column has a white card. The header shows the song's length in minutes and seconds.",
     "Four columns show at a time; [KNOB_1] scrolls."),
    ("change-column-section", "SONG", "Change which section a column plays",
     song_screen(["A", "C", "A", "C"], cursor=1),
     [(65, 31, 54, 34), (65, 68, 54, 128)],
     ["Turn [KNOB_1] to the column.",
      "Turn [KNOB_2]: it plays the next (or previous) section in use, B -> C -> A."],
     "The column's card and blocks change to that section's.",
     "If it was the section's last column, that section is gone and its letter is free again."),
    ("open-section", "SECTION PAGE", "Open a section",
     section_screen("B", "4 bars", True, uses=1),
     [(4, 30, 232, 24), (0, 54, 240, 144), (0, 200, 240, 38), (206, 32, 30, 20)],
     ["On SONG, turn [KNOB_1] to a column and tap [OCT+]. The top row: its length in words (AUTO).",
      "Its tracks: PATTERNS' numbered cells, the section's pattern filled; REST across a resting track.",
      "At the foot a lane a track: how its pattern loops in the length (lighter: later loops).",
      "How many columns play this section."],
     "SECTION B in the header. Whatever you change here changes every column of B.",
     "[OCT−] goes back to SONG."),
    ("edit-section", "SECTION PAGE", "Change the section's patterns",
     section_screen("B", "4 bars", True, uses=1, edited={2: 2, 3: None}),
     [(30, 127, 206, 34), (0, 162, 240, 36)],
     ["Turn [KNOB_1] to [KNOB_4]: tracks 1-4's pattern in this section.",
      "Turn below the first pattern for REST: that track is silent here."],
     "The filled cell moves; the lanes below follow. Every column of the section changes with it.",
     "Tracks 5-8: select one with [ALGORITHM]; the knobs then work on its group."),
    ("set-length", "SECTION PAGE", "Set the section's length",
     section_screen("C", "3 bars", False, uses=1),
     [(4, 30, 232, 24), (226, 210, 12, 12)],
     ["Turn [SELECT]: one beat a click, from its AUTO length. Turn it below one beat for AUTO again (LENGTH: AUTO).",
      "A pattern longer than the section is cut where it ends: the red mark."],
     "The AUTO chip goes when you set a length. On SONG its card shows it as bars:beats (3:0). Shorter patterns loop "
     "to fill the section.",
     "There are no repeats: make a section longer, or place it twice. Every track starts again at each column."),
    ("hear-section", "SECTION PAGE", "Hear just this section",
     section_screen("B", "4 bars", True, uses=1, looping=True),
     [(150, 6, 86, 18), (108, 199, 10, 40)],
     ["Press [PLAY]: the section loops, alone. Change patterns or the length while it plays.",
      "Press [PLAY] again to stop; [OCT−] back to SONG."],
     "LOOPING B in the header and a playhead across the lanes. Changes are heard from the next pass.",
     ""),
    ("section-sheet", "SECTION PAGE", "Sounds, punches, a copy",
     sheet(section_screen("B", "4 bars", True, uses=1), "Section B", "in song 1x",
           [("Set preset", "T2", False), ("Remove preset", "T2", False),
            ("Punch FX", "", False), ("Copy as new section", "", False)]),
     [(4, 66, 232, 172)],
     ["On the section page tap [OCT+]: its sheet. [KNOB_2] picks a row, [OCT+] does it.",
      "Set preset: a sound for the selected track in this section ([ALGORITHM] picks the track).",
      "Punch FX (or [FX]): master effects at steps of the section."],
     "Set preset, Remove preset, Punch FX, Copy as new section. (The length is on [SELECT], step 11.) A section's "
     "sound plays in that section only; the normal one returns elsewhere and on STOP.",
     "Copy as new section makes the next letter with the same contents, then shows it, for a variation."),
    ("copy-as-new", "SONG", "Make a variation of a section",
     sheet(song_screen(["A", "B", "A", "C"], cursor=1), "Song", "4 columns",
           [("Duplicate section", "", False), ("Copy as new section", "", False), ("Move section", "", False),
            ("Copy section", "", False), ("Delete section", "", True)], sel=1),
     [(4, 66, 232, 172)],
     ["On SONG pick the column, then hold [OCT+]: the song's sheet.",
      "[KNOB_2] to Copy as new section, [OCT+]."],
     "A new letter (D) after it, with B's patterns, sounds and length. Change D on its page; B stays as it was.",
     "Duplicate section places the same B again instead: change one, both change."),
    ("move-section", "SONG", "Move a section",
     song_screen(["A", "B", "A", "C"], cursor=3, status="MOVE TO..."),
     [(150, 6, 86, 18), (121, 31, 54, 34)],
     ["Pick the column, hold [OCT+], choose Move section.",
      "Turn [KNOB_1] to where it goes and tap [OCT+]; [OCT−] cancels."],
     "MOVE TO... in the header while you choose. It goes in before the column you pick (the + card: the end).",
     "Copy section works the same way and leaves the original where it is."),
    ("delete-section", "SONG", "Delete a column",
     confirm(song_screen(["A", "B", "A", "C"], cursor=2), "DELETE SECTION A?", "This column only"),
     [(14, 70, 212, 100)],
     ["Pick the column, hold [OCT+], choose Delete section.",
      "[OCT+] yes, [OCT−] no."],
     "The column goes and the rest close up. \"This column only\": A still plays in its other column.",
     "\"Its last column\" means the section goes with it. Hold [EDIT] to undo."),
    ("select-columns", "SONG", "Work on several columns",
     song_screen(["A", "B", "A", "C"], cursor=2, sel_range=(1, 2), status="SELECTED"),
     [(9, 31, 110, 34)],
     ["Hold [OCT+] and turn [KNOB_1]: the columns from the cursor on are selected.",
      "Let go, tap [OCT+]: Copy, Move, Duplicate or Delete sections, or Clear selection."],
     "SELECTED in the header, the chosen cards tinted. Copies keep their letters: they are the same sections.",
     ""),
    ("play-song", "SONG", "Play the song",
     song_screen(["A", "B", "A", "C"], cursor=1, playing=1, playhead=93),
     [(65, 31, 54, 34), (8, 226, 224, 10)],
     ["Turn [KNOB_1] to where to start and press [PLAY]; [PLAY] again stops.",
      "The column playing is coloured; a playhead and the progress bar move."],
     "Each section plays its length, then every track starts the next column together. The last column stops "
     "the song, unless Loop song (hold [OCT+]) is on.",
     "[PLAY] on the + card starts at the first column. STOP gives each track back the pattern it had before."),
    ("launch-live", "SONG", "Launch sections live",
     song_screen(["A", "B", "A", "C"], cursor=1, playing=0, queued=1, status="B NEXT"),
     [(150, 6, 86, 18), (65, 31, 54, 34)],
     ["Pick a column and tap [REC]: its section for every track at once (stopped: it starts playing).",
      "While patterns play, tap [REC] on another: it waits for the end of the one playing."],
     "B NEXT in the header, B's card dashed until it starts. The first launch waits for the next bar.",
     "Change a track by hand afterwards and the header says A CHANGED: add it as a section on + to keep it."),
    ("undo", "SONG", "Undo and redo",
     song_screen(["A", "B", "A", "C"], cursor=1, msg="UNDO SONG"),
     [(0, 0, 240, 28)],
     ["Hold [EDIT]: the last song edit is undone (up to eight).",
      "Hold [EDIT] and press [OCT−] to undo, [OCT+] to redo."],
     "UNDO SONG, REDO SONG in the header. Stop the song first: while it plays the screen says STOP TO UNDO.",
     "Saving: SONG is part of the project; save it as usual (hold [SAVE])."),
]


def slug(i, s):
    return f"{i + 1:02d}-{s[0]}.svg"


def guide_md():
    total = len(STEPS)
    md = ["# PATTERNS and SONG, step by step", "",
          "How to choose patterns, build a song out of sections, edit and move them, and play it live. Each step is one",
          "picture: the screen with numbered marks, and what to press. The full reference is the user manual",
          "([`web/manual/index.html`](../../web/manual/index.html): \"Patterns, sections and songs\", \"Patterns\" and",
          "\"Songs\"); the design notes are in [`docs/design/README.md`](../design/README.md) \"SONG sections\".", "",
          "The pictures are drawings of the device's screens, made by `tools/gen_song_guide.py`; run it again after the",
          "screens change.", "",
          "**The words:** a *pattern* is one track's loop, shown everywhere as its number (1-16) in the track's colour. A",
          "*section* is one part of the song, shown as its letter (A-Z): the",
          "pattern or a rest (REST) for every track, and a length (on SONG's cards bars:beats: 2:3 is 2 bars 3 beats). The",
          "*song* is sections in columns, left to right; a section can be placed in as many columns as you like, and",
          "they all play the same. A *track* is its numbered chip at the left of each row.", "",
          "## Contents", ""]
    part = None
    for i, s in enumerate(STEPS):
        if s[1] != part:
            part = s[1]
            md.append(f"- **{part}**")
        md.append(f"  - [{i + 1}. {s[2]}](#{i + 1}-{s[2].lower().replace(' ', '-').replace(chr(39), '').replace('(', '').replace(')', '').replace(',', '')})")
    md.append("")
    part = None
    for i, s in enumerate(STEPS):
        if s[1] != part:
            part = s[1]
            md += [f"## {part}", ""]
        md += [f"### {i + 1}. {s[2]}", "", f"![Step {i + 1}: {s[2]}]({slug(i, s)})", ""]
        for j, a in enumerate(s[5]):
            md.append(f"{j + 1}. {a.replace('[', '**').replace(']', '**').replace('_', ' ')}")
        md += ["", f"*You see:* {s[6].replace('[', '**').replace(']', '**').replace('_', ' ')}", ""]
        if s[7]:
            md += [f"> {s[7].replace('[', '**').replace(']', '**').replace('_', ' ')}", ""]
    md += ["## Quick reference", "",
           "| Where | Control | Does |", "| --- | --- | --- |",
           "| PATTERNS | KNOB 1-4 | each track's pattern (playing: at its loop end) |",
           "| PATTERNS | SEQ held + white key | the selected track's pattern |",
           "| PATTERNS | OCT+ | the pattern's sheet: Copy to..., Delete pattern |",
           "| PATTERNS | SELECT | on to SONG |",
           "| SONG | KNOB 1 | pick a column (past the last: +) |",
           "| SONG | KNOB 2 | which section the column plays |",
           "| SONG | OCT+ | on a column: its section page; on +: add what plays now |",
           "| SONG | OCT+ held | Duplicate, Copy as new, Move, Copy, Delete section; Loop song; Clear song |",
           "| SONG | OCT+ held + KNOB 1 | select several columns, then OCT+ for their sheet |",
           "| SONG | PLAY | play from the picked column / stop |",
           "| SONG | REC | launch the picked column's section live |",
           "| SONG | FX | the picked section's master punches |",
           "| SONG | EDIT held | undo (EDIT + OCT- / OCT+: undo / redo) |",
           "| Section page | KNOB 1-4 | tracks 1-4's pattern in the section (below pattern 1: REST) |",
           "| Section page | SELECT | its length, a beat a click (below a beat: AUTO) |",
           "| Section page | PLAY | loop just this section / stop |",
           "| Section page | OCT+ | Set / Remove preset, Punch FX, Copy as new section |",
           "| Section page | OCT- | back to SONG |", ""]
    return "\n".join(md)


def files():
    out = {slug(i, s): page(i + 1, len(STEPS), s[1], s[2], s[3], s[4], s[5], s[6], s[7]) + "\n" for i, s in enumerate(STEPS)}
    out["README.md"] = guide_md()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    want = files()
    if a.check:
        bad = [n for n, c in want.items() if not (OUT / n).is_file() or (OUT / n).read_text() != c]
        for n in bad:
            print(f"gen_song_guide: {n} out of date", file=sys.stderr)
        sys.exit(1 if bad else 0)
    OUT.mkdir(parents=True, exist_ok=True)
    for n, c in want.items():
        (OUT / n).write_text(c)
    print(f"gen_song_guide: {len(want) - 1} steps and README.md -> {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
