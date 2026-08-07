"""Third-party asset packs must stay out of git.

WHAT THIS GUARDS. `.gitignore` excludes the Fab packs under `game/Content/`.
Nothing checked that it still does. Deleting that one line passed the whole test
suite and the linter, and the next `git add -A` would have staged roughly 17 GB
through Git LFS, against the 10 GiB a GitHub Free or Pro account includes per
month. The loss is not recoverable by editing: once pushed, the objects are in
the repository's LFS store and in every future clone.

This is the same shape as `test_map_built_data_is_not_committed.py`, which guards
the comparable rule about baked lighting data.

WHAT THIS DOES NOT GUARD. Whether excluding them is the right policy. That is
argued in `docs/DECISIONS.md` and in the comment block in `.gitignore` itself.
"""

from __future__ import annotations

import pathlib
import subprocess
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CONTENT = REPO_ROOT / "game" / "Content"

sys.path.insert(0, str(REPO_ROOT / "tools"))


def third_party():
    import third_party_content

    return third_party_content


def test_the_vendor_list_is_readable() -> None:
    """`.gitignore` still carries the fenced block this all depends on."""
    patterns = third_party().ignore_patterns()

    assert patterns, (
        "The third-party pack block in .gitignore is empty. Every vendor pack "
        "under game/Content/ is now committable, and a single `git add -A` "
        "would stage them through Git LFS."
    )
    for pattern in patterns:
        assert pattern.startswith("game/Content/"), (
            f"{pattern!r} is in the third-party block of .gitignore but does "
            "not name a folder under game/Content/. Either it belongs "
            "somewhere else in the file, or tools/third_party_content.py "
            "cannot derive a folder name from it."
        )


def test_every_vendor_folder_on_disk_is_ignored() -> None:
    """No pack folder present in this checkout is committable.

    Uses `git check-ignore`, so it tests what git actually does rather than
    re-implementing pattern matching and agreeing with itself.
    """
    if not CONTENT.is_dir():
        pytest.skip("game/Content/ does not exist in this checkout")

    module = third_party()
    present = [
        path for path in CONTENT.iterdir()
        if path.is_dir() and module.is_third_party(path.relative_to(CONTENT))
    ]
    if not present:
        pytest.skip(
            "No third-party packs are installed in this checkout, so there is "
            "nothing to check. They are absent on a fresh clone and in "
            "continuous integration by design.")

    not_ignored = []
    for folder in present:
        probe = folder / "probe.uasset"
        finished = subprocess.run(
            ["git", "check-ignore", "-q", str(probe.relative_to(REPO_ROOT))],
            cwd=REPO_ROOT, capture_output=True, check=False)
        # git check-ignore exits 0 when the path IS ignored, 1 when it is not.
        if finished.returncode != 0:
            not_ignored.append(folder.name)

    assert not not_ignored, (
        "These third-party pack folders under game/Content/ are NOT ignored by "
        f"git: {', '.join(not_ignored)}. Committing them would push gigabytes "
        "through Git LFS. Add a pattern for each to the block between "
        "# THIRD-PARTY-PACKS-BEGIN and # THIRD-PARTY-PACKS-END in .gitignore."
    )


def test_no_vendor_asset_is_tracked() -> None:
    """Nothing from a vendor pack is already in the index.

    The check above tests the rule. This tests the outcome, which is what
    actually matters and which would stay broken even if the rule were fixed
    after the fact: `.gitignore` does not untrack a file that is already
    tracked.
    """
    finished = subprocess.run(
        ["git", "ls-files", "game/Content/"],
        cwd=REPO_ROOT, capture_output=True, text=True, check=False)
    if finished.returncode != 0:
        pytest.skip("git ls-files failed; not a git checkout")

    module = third_party()
    tracked = []
    for line in finished.stdout.splitlines():
        relative = pathlib.PurePosixPath(line).relative_to("game/Content")
        if module.is_third_party(relative):
            tracked.append(line)

    assert not tracked, (
        "These files from a third-party asset pack are tracked by git: "
        f"{', '.join(tracked[:10])}"
        f"{' and more' if len(tracked) > 10 else ''}. Adding a .gitignore "
        "pattern does not untrack an already-tracked file; use "
        "`git rm --cached` as well."
    )


def test_authored_enemy_work_is_not_ignored() -> None:
    """The folder derived work goes in is committable.

    The counterpart to the rule above, and the one that would bite silently. A
    Blueprint saved in the wrong place is dropped by `git add` with no error, so
    the place it is supposed to go has to be verified rather than assumed.
    """
    probe = "game/Content/Enemies/Demonic/Brute/BP_Brute.uasset"
    finished = subprocess.run(
        ["git", "check-ignore", "-q", probe],
        cwd=REPO_ROOT, capture_output=True, check=False)

    assert finished.returncode != 0, (
        f"{probe} is ignored by git. That is where authored enemy assets are "
        "supposed to live -- see game/docs/content-layout.md -- so anything "
        "saved there would be silently dropped."
    )
