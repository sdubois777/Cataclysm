"""A `dev/null` directory of Git LFS hooks must never reach a commit.

WHY THIS FILE EXISTS. A directory called `dev/null/` appears at the repository
root from time to time, holding the four Git LFS hook scripts `post-checkout`,
`post-commit`, `post-merge` and `pre-push`. It is untracked, so `git add -A`
sweeps it into a commit; that happened once during the work on issue #166 and
the commit had to be amended to take the four files out. It also makes `git
status` dirty, which destroys the "working tree clean" check that every task
starts with. Issue #195 records the incident.

WHAT PUTS IT THERE. A git command running with `core.hooksPath` set to
`/dev/null`. Git for Windows resolves that value to a relative path, so Git LFS
writes its hooks into `dev/null/` under whatever the working directory is, and
in this repository that is the root. Reproduced on this machine with:

    git init probe && cd probe
    git -c core.hooksPath=/dev/null lfs install --local

Any command that runs the Git LFS filter over a tracked file reinstalls the
hooks, not only `git lfs install`. `git clone`, `git checkout` and `git add` all
create the directory when `core.hooksPath` has that value, and this repository
tracks `*.uasset` and `*.umap` through LFS, so those are ordinary daily
commands. Which program supplies the setting was NOT identified: it is in no git
config file on this machine (`git config --show-origin --get core.hooksPath`
returns nothing) and in neither shell's environment, so it must be a per-command
`-c` flag from some tool.

WHAT IS CHECKED HERE. That `.gitignore` ignores the directory, that it is
anchored to the repository root so a real `dev/` directory further down is still
tracked, and that the four hook filenames in particular are covered. `git
check-ignore` works on pathnames rather than on files that exist, so nothing is
created on disk.
"""

from __future__ import annotations

import pathlib
import subprocess

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

#: The four hook scripts Git LFS installs. These are the files that turned up
#: inside `dev/null/`.
LFS_HOOK_NAMES = ("post-checkout", "post-commit", "post-merge", "pre-push")


def is_ignored(path: str) -> bool:
    """Whether git ignores ``path``, which is relative to the repository root.

    ``git check-ignore`` exits 0 when the path is ignored and 1 when it is not.
    Any other exit code is a real failure and is raised rather than read as an
    answer, because a broken git invocation returning 1 would otherwise look
    like "not ignored" and pass a check that should have errored.
    """
    completed = subprocess.run(
        ["git", "check-ignore", "-q", "--no-index", path],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    if completed.returncode not in (0, 1):
        raise RuntimeError(
            f"git check-ignore failed on {path!r} with exit code "
            f"{completed.returncode}: {completed.stderr.strip()}"
        )
    return completed.returncode == 0


@pytest.mark.parametrize("hook_name", LFS_HOOK_NAMES)
def test_the_stray_lfs_hook_directory_is_ignored(hook_name: str) -> None:
    """Each of the four hook scripts under `dev/null/` is ignored."""
    assert is_ignored(f"dev/null/{hook_name}"), (
        f"dev/null/{hook_name} is not ignored, so `git add -A` would commit it. "
        "Add /dev/ to .gitignore. See issue #195."
    )


def test_a_hook_name_nobody_has_seen_yet_is_ignored() -> None:
    """The whole directory is ignored, not only the four filenames above.

    Git LFS decides which hooks it writes and the set has grown before. A rule
    that listed today's four filenames would pass every check above and still
    let a fifth hook through. This asks about a name that does not exist.

    Note the path has no trailing slash. `git check-ignore --no-index "dev/"`
    reports a match against an empty pattern in this repository even when no
    rule matches, so a trailing-slash path cannot be used to ask the question.
    """
    assert is_ignored("dev/null/pre-receive"), (
        "dev/null/pre-receive is not ignored, so the .gitignore rule is a list "
        "of individual hook filenames rather than the directory. Ignore /dev/ "
        "instead. See issue #195."
    )


def test_the_rule_is_anchored_to_the_repository_root() -> None:
    """A `dev/` directory below the root is still tracked.

    Without the leading slash, `dev/` would hide any directory of that name
    anywhere in the tree. `game/Source/.../dev/` is a plausible thing for
    someone to add later, and losing it silently would be worse than the problem
    being solved.
    """
    assert not is_ignored("sim/dev/something.py"), (
        "sim/dev/something.py is ignored, which means the .gitignore rule for "
        "the stray Git LFS hook directory is missing its leading slash and is "
        "hiding real directories called dev/ anywhere in the tree."
    )


def test_gitignore_says_what_the_rule_is_for() -> None:
    """The rule carries a comment naming the issue.

    An unexplained `/dev/` in `.gitignore` reads like a mistake and invites
    deletion, which brings the problem straight back.
    """
    gitignore = (REPO_ROOT / ".gitignore").read_text(encoding="utf-8")
    line_index = next(
        (i for i, line in enumerate(gitignore.splitlines()) if line.strip() == "/dev/"),
        None,
    )
    assert line_index is not None, "No /dev/ line in .gitignore. See issue #195."

    preceding = gitignore.splitlines()[max(0, line_index - 12) : line_index]
    comment = "\n".join(preceding)
    assert "#195" in comment, (
        "The /dev/ line in .gitignore has no comment naming issue #195, so the "
        "next person to read it cannot tell what it is for."
    )
