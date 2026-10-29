---
name: upstream-replay
description: Pull the latest upstream Melodee (keremimo/melodee) release and replay our fork's commit stack onto it on a new tj/<version> branch, resolving conflicts with DEV.md's notes and running the post-replay checks. Use whenever the user asks to pull upstream, update to a new Melodee release or release candidate (e.g. "rc.6 is out", "get the latest from Kerem"), rebase or replay our changes, sync the fork, or check what upstream changed since our base.
---

# Replay our stack onto a new upstream release

We keep our features as a stack of commits on top of an upstream tag (DEV.md, "Following upstream releases"). A
replay moves that stack onto a newer tag on a **new** branch, so every branch firmware was built from stays as it
was. Read DEV.md's "Following upstream releases" section fully before starting: its table, the "Expect conflicts in"
list, "Upstream tests we changed on purpose", and "What to check after every replay" are the playbook. The steps
below are the order to use them in.

## 1. Look before changing anything

```sh
git status -sb                                   # must be clean: commit (fork-commit skill) or stash first
git config --get rerere.enabled                  # true: git reuses past conflict resolutions
git fetch upstream --tags
git describe --tags --abbrev=0 HEAD              # our base tag, e.g. v1.6.0-rc.5
git tag -l 'v*' --sort=-v:refname | head         # the newest tags
git log --oneline --reverse <base>..HEAD         # our stack, in order
git log --oneline <base>..<target>               # what upstream changed
git diff --stat <base> <target>                  # which files
python3 tools/upstream_shadow.py --from <base> --to <target>   # upstream's changes to the code ours replaces
```

The last one matters most for keeping upstream's work: where a feature of ours replaces upstream code, that code stays
in its file verbatim inside `#ifdef MELODEE_UPSTREAM_<FEATURE>` (never defined), so upstream's edits there merge
without a conflict but reach nothing of ours. The tool lists each such edit (and changes to upstream test files ours
replaces). Preview it here; it becomes a checklist after the replay (step 4).

Pick the target: the newest **stable** tag unless the user asks for a release candidate (RCs come often; DEV.md
suggests a throwaway branch for them, and rerere then remembers the resolutions). Show the user: our base, the target,
upstream's change list, and which of their changed files overlap DEV.md's "Expect conflicts in" list. Agree on the
target and the new branch name, `tj/<target version>` (e.g. `tj/1.6.0-rc.6`). These are the user's calls.

## 2. Prepare the branch

```sh
git switch main && git merge --ff-only upstream/main       # main mirrors upstream; ask before pushing it
git switch -c tj/<target> <current tj branch>              # a copy of our stack
```

If fixup commits are waiting (`fixup! …` subjects) and this new branch is not built from yet, fold them into their
rows first, without an editor: `GIT_SEQUENCE_EDITOR=: git rebase -i --autosquash <base>`. Then check that
`git log --reverse <base>..HEAD` lines up with DEV.md's table; if DEV.md lists working commits that are not one per row,
leave them as they are unless the user wants them tidied.

## 3. Replay

```sh
git rebase --onto <target> <base>
```

For each stop:

1. `git status` and `git diff --name-only --diff-filter=U` for the conflicted files; `git log -1 REBASE_HEAD` for
   which of our commits is being applied, then its DEV.md row and feature section.
2. Resolve by the rule DEV.md gives: **keep upstream's change and re-apply ours on top.** Our hooks are calls into our
   own files (`nav_*()`, `gn_*()`, `seqt_*()`, `cseq_*()`, `shape_*()`, `sect_*()`) with a comment naming the file, so
   the conflict shows what is ours. Inside a `MELODEE_UPSTREAM_*` marker, always take upstream's new text whole: it is
   their code kept for reference, and the tool compares it, so it must stay verbatim. Files deleted on purpose (SID, TRIO) stay deleted (`git rm`). Upstream tests we changed on
   purpose: take their new version, then re-apply our listed changes.
3. A function upstream moved to another file takes our hook with it: `git grep` the function name and re-add our
   lines where it now lives (DEV.md's `param_kept` example).
4. A file that `git status` shows already resolved ("Resolved '<file>' using previous resolution") was filled in by
   rerere from an earlier replay. It is not staged: read it as you would your own fix, since the code around it may
   have changed since. A wrong one: `git rerere forget <file>`, resolve it by hand.
5. `git add <files>` and `git rebase --continue`. If a resolution is too uncertain, stop and ask; `git rebase --abort`
   returns to the start with nothing lost.

For a large conflict, building at that point (`./build.sh`) is cheaper than finding the break at the end.

## 4. Check the result

A clean rebase does not mean a working build, nor that upstream's work reached ours. First the port list:

```sh
python3 tools/upstream_shadow.py --from <base> --to <target>   # exit 1 while items are listed
```

For each item: read upstream's diff and the matching code in our file (the marker's comment names it; the block's
first line names the function). Port the change when it applies to ours (a fix, a new case, a renamed field, a new
parameter our version must take too), or decide it does not (it only concerns what we removed, such as the jam log or
held blocks) and say why. Ask the user when the choice changes behaviour. Keep the decisions for the DEV.md paragraph
(step 5). A block reported as matching neither tag means our marker no longer holds upstream's text: restore it from
the tag (`git show <target>:<file>`).

Then everything else:

```sh
source ~/.cache/melodee-venv/bin/activate
./build.sh && tests/run_tests.sh                 # ok lines, ALL HOST TESTS PASSED, 0 lint findings
MELODEE_KEY_NAV=0 tests/run_tests.sh             # upstream's keyboard too
python3 tools/gen_shortcuts.py --check && python3 tools/gen_song_guide.py --check
git grep -nE '(^|[^a-z_])nav_[a-z_]+\(' -- firmware/src ':!firmware/src/ui_nav.c'   # every UI hook still in place
```

Then walk DEV.md's "What to check after every replay" list item by item: static RAM and flash (read the build's
image and `.data+.bss` lines against the limits), our footprint, moved functions, the project record and its asserts,
parameter slot 45, pages and families, the web editor, the copied Tessera files, SHAPE, song sections, the chord
sequencer, the UI hooks. Say which you checked and how.

Set `MELODEE_VERSION` in `firmware/src/melodee.c` to the new base (and its `-nonav` twin), rebuild.

If upstream now ships something we carry (DEV.md's *Drop when*), drop that commit (`git rebase -i` by the user, or
`git revert` if the branch is built from) and note it.

## 5. Record it in DEV.md and commit

Add a paragraph like the existing one under the table: "Replayed onto <target> on <date> (`tj/<target>`): …" with the
commits dropped or squashed, the conflicts met and how each was resolved, the tests fixed after it, and each
`upstream_shadow.py` item: ported (to which function) or not applicable (why). Update the table
and lists for anything that moved (new hook files, renamed functions). Commit that with the docs row (the
`fork-commit` skill).

## 6. Hand back

Report: the new branch, the target tag, conflicts and their resolutions, check results (with any flaky rerun),
DEV.md's new paragraph, and what still needs the device (timing, audio and storage the host tests cannot see).
Pushing the new branch and `main` publishes them: ask first. Firmware for release comes only from the attested
`zvenson/fm1-build` workflow (BUILDING.md → Releases), never from this local build.
