#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Upstream's changes to the code our features replace: the checklist for a replay (DEV.md "Following upstream
releases", the upstream-replay skill).

Where a feature of ours replaces upstream code, that code stays in its file verbatim inside a marker that is never
defined, so upstream's edits to it merge without conflicts:

    #ifdef MELODEE_UPSTREAM_SONG                    /* upstream's, replaced by song_sect.c (...) */
    ... upstream's text ...
    #else                                           (optional: ours, in its place)
    #endif

A clean merge there means nothing reached our version. This lists every upstream change between two tags inside
those blocks, and in the upstream files we keep but do not use (SUPERSEDED), so each can be ported to ours or marked
not applicable.

  tools/upstream_shadow.py --from v1.6.0-rc.5 --to v1.6.0-rc.6     # the report; exit 1 when there is something
  tools/upstream_shadow.py --list                                  # every marked block, by file and line

Run it before or after the rebase: a block is found in either tag's text.
"""
import argparse
import difflib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MARKERS = ("MELODEE_UPSTREAM_SONG",)                    # one per feature that replaces upstream code
SUPERSEDED = {                                           # upstream's file, kept verbatim and unused: ours
    "tests/song_mode_test.c": "tests/song_sect_test.c",
}
SCAN = ("firmware/src", "firmware/hal", "tests")


def git(*a):
    return subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True).stdout


def at(tag, path):
    r = subprocess.run(["git", "show", f"{tag}:{path}"], cwd=ROOT, capture_output=True, text=True)
    return r.stdout.split("\n") if r.returncode == 0 else None


def blocks(lines):
    """[(marker, first, last)] 0-based line spans of the upstream part of each block (#ifdef .. #else / #endif)"""
    out, depth, cur = [], 0, None
    for i, l in enumerate(lines):
        s = l.strip()
        if cur is None:
            m = re.match(r"#ifdef\s+(\w+)", s)
            if m and m.group(1) in MARKERS:
                cur, depth = (m.group(1), i + 1), 0
            continue
        if re.match(r"#if", s):
            depth += 1
        elif depth == 0 and (s.startswith("#else") or s.startswith("#endif")):
            out.append((cur[0], cur[1], i - 1))
            cur = None
            if s.startswith("#else"):                   # (skip ours, to its #endif)
                d = 0
                for j in range(i + 1, len(lines)):
                    t = lines[j].strip()
                    if t.startswith("#if"): d += 1
                    elif t.startswith("#endif"):
                        if d == 0: break
                        d -= 1
        elif s.startswith("#endif"):
            depth -= 1
    return out


def find(hay, needle):
    """-> (index in hay, the needle's part found) for its lines found there exactly, else (None, needle). A block may
    begin or end with a line or two of ours that joins it to the code around (a "} else"): those are left out"""
    for cut in range(0, 3):
        for a in range(0, cut + 1):
            part = needle[a:len(needle) - (cut - a)]
            if not part:
                continue
            n = len(part)
            for i in range(len(hay) - n + 1):
                if hay[i:i + n] == part:
                    return i, part
    return None, needle


def where(ops, x, upper):
    """base line x as a line of the other text (upper: the end of a span, past whatever replaced it)"""
    for tag, i1, i2, j1, j2 in ops:
        if i1 <= x < i2 or (x == i2 and upper and tag != "equal"):
            return j1 + (x - i1) if tag == "equal" else (j2 if upper else j1)
    return ops[-1][4] if ops else x


def owner(text):
    """the block's name: its first function, else its first define or statement"""
    for l in text:
        m = re.match(r"(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\(", l)
        if m and not l.startswith((" ", "\t", "#", "/")):
            return m.group(1)
    for l in text:
        if l.strip() and not l.strip().startswith(("/*", "*")):
            return l.strip()[:48]
    return ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--from", dest="old", help="the upstream tag we were on")
    ap.add_argument("--to", dest="new", help="the upstream tag we move to")
    ap.add_argument("--list", action="store_true", help="only list the marked blocks")
    a = ap.parse_args()
    files = sorted(p for d in SCAN for p in (ROOT / d).rglob("*") if p.suffix in (".c", ".h"))
    marked = []
    for p in files:
        lines = p.read_text(errors="replace").split("\n")
        for m, s, e in blocks(lines):
            marked.append((p.relative_to(ROOT).as_posix(), m, s, e, lines[s:e + 1]))
    if a.list or not (a.old and a.new):
        for f, m, s, e, text in marked:
            print(f"{f}:{s + 1}-{e + 1}  {m}  {owner(text)}")
        print(f"{len(marked)} marked blocks; superseded files: {', '.join(SUPERSEDED)}")
        return 0
    todo = 0
    print(f"Upstream {a.old} -> {a.new}: its changes to the code we replace\n")
    for f, m, s, e, text in marked:
        old, new = at(a.old, f), at(a.new, f)
        if old is None or new is None:
            print(f"- [ ] {f}: {owner(text)}: the file is not in {a.old if old is None else a.new}; check by hand")
            todo += 1
            continue
        (i, text), base, other = find(old, text), old, new
        if i is None:
            (i, text), base, other = find(new, text), new, old
        if i is None:
            print(f"- [ ] {f}:{s + 1}: {owner(text)} ({m}): the block matches neither tag's text; compare it by hand")
            todo += 1
            continue
        sm = difflib.SequenceMatcher(None, base, other, autojunk=False)
        ops = sm.get_opcodes()
        end = i + len(text)
        if not any(op[0] != "equal" and op[1] < end and op[2] > i for op in ops) and \
                not any(op[0] == "insert" and i < op[1] < end for op in ops):
            continue
        lo, hi = where(ops, i, 0), where(ops, end, 1)
        a_txt, b_txt = (base[i:end], other[lo:hi]) if base is old else (other[lo:hi], base[i:end])
        print(f"- [ ] {f}:{s + 1}: {owner(text)} ({m}): upstream changed it; port to ours or note why not")
        for l in list(difflib.unified_diff(a_txt, b_txt, a.old, a.new, lineterm="", n=1))[2:]:
            print("      " + l)
        todo += 1
    for up, ours in SUPERSEDED.items():
        d = git("diff", "--stat", a.old, a.new, "--", up).strip()
        if d:
            print(f"- [ ] {up} (replaced by {ours}): upstream changed it; port its new cases to ours or note why not")
            print("      " + d.splitlines()[-1].strip())
            todo += 1
    print(f"\n{todo} to review" if todo else "Nothing upstream changed in the code we replace.")
    return 1 if todo else 0


if __name__ == "__main__":
    sys.exit(main())
