---
name: fork-commit
description: Commit work on the Melodee fork so it stays tracked in DEV.md's fork table and replayable onto future upstream releases. Use whenever the user asks to commit, save progress in git, "commit this", "make a commit for the navigation work", wrap up a feature, or record what changed in DEV.md, on this repo. Also use when the user asks whether changes are recorded in DEV.md, or to tidy which commit holds which feature.
---

# Commit a change to the Melodee fork

Our features are a stack of commits on top of an upstream release tag. DEV.md's table ("Following upstream releases",
the `| # | Commit | What it changes | Upstream files it hooks into | Drop when |` table) is the map of that stack: each
row is one feature, and a replay onto a new upstream release walks the rows in order. A commit that is not in the
table, or a row whose files are wrong, turns the next replay into archaeology. This skill keeps the two in step.

## 1. Check where we are

```sh
git status -sb            # branch, ahead / behind
git diff --stat           # what changed
git log --oneline -8
```

- On `main`, stop: it mirrors `upstream/main`. Ask which `tj/*` branch to use.
- Only commit what the user asked to commit. If unrelated changes are in the tree, say so and leave them unstaged
  rather than sweeping them in.

## 2. Decide which row the change belongs to

Read DEV.md's table and the feature sections. For each logical change in the diff, decide:

- **A new feature** → a new row, placed before the docs row ("DEV.md, plan.md, our docs"), which stays last.
  Its *Commit* column text becomes the commit subject.
- **A fix or extension of an existing row** → commit it as a fixup of that row's commit, so a later
  `git rebase -i --autosquash` folds it in: `git commit --fixup=<that row's commit>`. Find the commit with
  `git log --oneline --grep "<row title>"` (the subject matches the row's *Commit* text). If the row's work is spread
  over several working commits (DEV.md says which, under the last "Replayed onto …" paragraph), use the newest.
- **Docs only** (manual, SHORTCUTS.md, NAVIGATION.md, guides, release notes) → the docs row's commit.

If the change **replaces upstream code** (rewrites or deletes an upstream function, a layout, a test), check it
follows DEV.md's "Replacing upstream code" convention before committing: upstream's text stays verbatim inside
`#ifdef MELODEE_UPSTREAM_<FEATURE>` (never defined), ours lives in our file, the upstream file gets a tagged one-line
hook. `python3 tools/upstream_shadow.py --list` shows the marked blocks; `--from <base> --to <base>` must report
nothing (a block that "matches neither tag" was edited, and has to be put back). Code rewritten in place in an upstream
file is the thing that makes replays hard: move it out before it is committed.

If one diff mixes features in the same file, prefer one commit per feature by staging per file; if a file genuinely
mixes two rows, commit it with the larger row and name both in the body. Don't hand-split hunks with `git add -p`
(interactive; not available here).

## 3. Run the checks

```sh
source ~/.cache/melodee-venv/bin/activate
./build.sh && tests/run_tests.sh
python3 tools/gen_shortcuts.py --check && python3 tools/gen_song_guide.py --check
python3 tools/upstream_shadow.py --from "$(git describe --tags --abbrev=0)" --to "$(git describe --tags --abbrev=0)"
```

All must pass: the build's `ok` lines, `ALL HOST TESTS PASSED`, `0 lint findings`, the generators clean, and the
shadow check reporting nothing (every marked block still upstream's text). If the only
failure is the known flaky CPU timing test (`tests/audio_cpu_take_test.py`, the CPU sequence runner), rerun it alone and
report it. Any other failure: fix it or stop and tell the user; never commit a red suite.

## 4. Update DEV.md in the same commit

For the row (new or existing), make the table say what is true now:

- **What it changes:** the behaviour, in the table's terse style.
- **Upstream files it hooks into:** every upstream file the change touches (our own files too, after them), with the
  function names a conflict would show; and the tests changed.
- **Drop when:** "never (ours)", or what upstream would have to ship for us to drop it.

Then the lists under the table, so a replay knows what to expect:

- **Our own files (no conflicts):** add new files we own (sources, tests, docs, generators).
- **Expect conflicts in:** add upstream files this change edits that are not listed.
- **Upstream tests we changed on purpose:** add upstream tests whose expectations we changed, with what and why.
- The feature's own DEV.md section (`### <feature> (our mod)`), if it has one, for anything a maintainer needs:
  storage layout, hooks, things that break silently.

Keep DEV.md's voice: short clauses, code names in backticks.

## 5. Commit

- Subject: the row's *Commit* text (new rows), or let `--fixup` write it.
- Body: what changed and why in a few lines, the checks that passed (and any flaky rerun), and the DEV.md row number.
- End with the attribution lines the session's instructions give for commits, if any.

Use a heredoc so the message keeps its lines:

```sh
git add <files>
git commit -F - <<'EOF'
Navigation: one set of rules

SELECT pages only, stopping at the ends; PRESETS the bigger step and the rows ...
Checks: build ok, ALL HOST TESTS PASSED, 0 lint findings, generators clean. DEV.md row 22.
EOF
```

## 6. Report, and stop before pushing

Tell the user: the commit(s) and their rows, what DEV.md now says, the checks. Pushing publishes the work: ask first,
and never force-push a `tj/*` branch firmware was built from (if the branch has diverged from its remote, suggest a new
branch name instead). If fixups are waiting, mention that the next replay folds them in
(`GIT_SEQUENCE_EDITOR=: git rebase -i --autosquash <base tag>` on a branch nobody builds from yet).
