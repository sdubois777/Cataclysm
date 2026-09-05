---
name: work-issue
description: Take a GitHub issue from reconnaissance through to a merged pull request and a closed issue, filing new issues for anything found along the way. Use when asked to work, fix, or implement a numbered issue.
disable-model-invocation: true
---

# Work a GitHub issue

Issue to work: $ARGUMENTS

Do not skip to step 3. The reconnaissance is the point.

## 1. Read the issue and its context

```bash
gh issue view $ARGUMENTS --comments
```

Note what the issue actually asks for, and what it does not. If the issue is
vague, or you find it is really two problems, say so before starting. Splitting
it into separate issues is usually right.

## 2. Deep reconnaissance

Before writing anything:

- Read the code the change touches, and the code that calls it.
- Check whether the behaviour already exists somewhere. Duplicated logic is a
  common outcome of skipping this.
- Read git history for anything that looks arbitrary, using
  `git log -p -- <path>`, rather than guessing why it is that way.
- Identify what could break. Name the files.
- For anything spanning more than a couple of files, use plan mode or delegate
  the reading to subagents so it does not crowd out the implementation.

Report what you found before proposing a change. If reconnaissance shows the
issue is based on a wrong assumption, stop and say so — that is a successful
outcome, not a failure.

## 3. Branch and implement

Branch from `development`, which is the working branch. `main` is production and
only receives release merges.

```bash
git checkout development && git pull
git checkout -b fix/short-description    # or feat/, chore/
```

Match the surrounding code's style, naming, and comment density. Keep the change
scoped to the issue; anything else you notice becomes a new issue, not a bigger
diff.

## 4. Test it thoroughly

Not "does it run" — does it do what was asked, and does neighbouring behaviour
still hold?

- Write tests for what you built, and put them in the directory that matches
  what the test is about. `sim/tests/` is for the Python empire simulation under
  `sim/cataclysm_sim/`. `tools/tests/` is for everything else: the scripts in
  `tools/`, the data they generate for the engine, the C++ under `game/`, and the
  checks that the design documents, the simulation and the game agree on a
  number. Choose by subject rather than by import — 69 of the files in
  `tools/tests/` import `cataclysm_sim` to read a number the game has to match,
  and they belong where they are. `tools/tests/` is the usual answer: on
  2026-09-05 it held 171 test files against `sim/tests/`'s 24.
- Confirm a new guard actually fails when it should. A check that cannot fail is
  worthless. Prove it with `tools/prove_guard.py` for Python, or with
  `prove_cpp_guard` in `tools/unreal_build.py` for C++, rather than breaking and
  restoring the file with a script written on the spot. A hand-rolled
  break-and-restore usually leaves the file's modification time and size
  unchanged, so the run reads a stale compiled copy and reports something that
  did not happen. `CLAUDE.md` has a worked example of each.
- Run the full fast suite and the linter:

```bash
python -m pytest
python -m ruff check .
```

- If you touched the power model, the day loop, or a tuning constant, also run:

```bash
cd sim && python -m cataclysm_sim.scoring
cd sim && python verify_scoring_port.py
```

- Fix bugs you find in the thing you just built, in this change, as you go.

## 5. File issues for everything else you found

For each finding that is real but out of scope:

```bash
gh issue create --title "..." --body "..." --label bug
```

Write it self-contained: name the files, describe what is wrong and how you
noticed, and give enough context to act on without this conversation. One issue
per concern.

## 6. Open the pull request

```bash
git push -u origin <branch>
gh pr create --base development --title "..." --body "..."
```

The body states what changed, why, and the evidence it works — the commands you
ran and what they printed. Name the issue in plain text, such as `Issue #N.`, and
do not reach for a closing keyword. GitHub only honours `Closes #N` when the pull
request merges into the repository's *default* branch, which here is `main`;
these merge into `development`, so the keyword does nothing and the issue is left
open. Step 7 is what closes it. List any new issues you filed.

## 7. Confirm, close the issue, and report

Wait for continuous integration:

```bash
gh pr checks --watch
```

Once the pull request has merged, close the issue by hand. Nothing closes it for
you:

```bash
gh issue close <N> --comment "Fixed in #<PR>"
```

Then read the issue back, rather than assuming the close worked:

```bash
gh issue view <N> --json state
```

It has to print `CLOSED`. Run those as two separate commands: a `gh` command
chained onto another with `;` or `&&`, or piped into anything, is refused by the
permission system, and the refusal names no reason.

Then report: what changed, what you tested and what the output was, what you
filed, and anything you deliberately left undone. If a check failed, lead with
that.
