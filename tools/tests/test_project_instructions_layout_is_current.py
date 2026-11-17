"""The layout table in `CLAUDE.md` still describes this repository.

WHAT THIS GUARDS. `CLAUDE.md` is the first thing anybody reads and nothing
checked any claim in it. Issue #662 found that it stated the fast test suite
takes about 7 seconds when it takes about 65, and the same pass found that the
layout table had no row for `tools/` at all -- while `tools/tests/` holds more of
the fast suite than `sim/tests/` does, and `tools/requirements.txt` carries the
only dependency in the repository. Both had been true for a long time.

A timing cannot be tested, because it belongs to the machine that measured it.
A path can. These two tests are the half of the file that is checkable:

- every path the table names is really there, so a directory that is renamed or
  removed cannot leave a row pointing at nothing;
- every top-level directory is named somewhere in the table, which is the check
  that would have caught `tools/` missing.

WHAT THIS DOES NOT GUARD. Whether a row's description is accurate, and none of
the timings. Those still need reading.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
INSTRUCTIONS = REPO_ROOT / "CLAUDE.md"

#: A path in the layout table's first column, which is always in backticks. The
#: path is taken exactly as written; the test below is what deals with a trailing
#: slash and with a row that names a glob rather than one file.
LAYOUT_ROW = re.compile(r"^\|\s*`([^`]+)`\s*\|")

#: Directories at the top level that are deliberately not described, because they
#: are not part of the project's own structure.
NOT_PART_OF_THE_LAYOUT = {".git", ".github", ".claude", ".ruff_cache", ".venv",
                          "__pycache__", ".pytest_cache", ".vscode", ".idea"}


def layout_table_paths() -> list[str]:
    """Every path named in the first column of the Layout table.

    EMPTY WHEN THE TABLE CANNOT BE FOUND, rather than raising. A renamed heading
    is a real thing to report and a ValueError traceback does not say what went
    wrong; the first test below is what turns the empty list into a sentence.
    """
    text = INSTRUCTIONS.read_text(encoding="utf-8")

    start = text.find("## Layout")
    end = text.find("## Commands", start + 1)
    if start < 0 or end < 0:
        return []

    return [match.group(1)
            for line in text[start:end].splitlines()
            if (match := LAYOUT_ROW.match(line))]


def test_the_layout_table_was_found_and_is_not_empty() -> None:
    """The two tests below read a real table rather than an empty match.

    Without this, renaming the heading or changing the table's shape would make
    both of them pass by having nothing to check.
    """
    paths = layout_table_paths()

    assert len(paths) >= 5, (
        "The Layout table in CLAUDE.md yielded "
        f"{len(paths)} paths, which is too few to be the real table. Either the "
        "'## Layout' or '## Commands' heading changed, or the rows no longer "
        "start with a path in backticks."
    )


def test_every_path_the_layout_table_names_exists() -> None:
    """No row points at something that is no longer there."""
    missing = []
    for named in layout_table_paths():
        # A row may name a glob, such as `sim/analyse_*.py`. The directory it
        # sits in is what is checked; whether any file still matches is a
        # separate question this test does not ask.
        probe = REPO_ROOT / named.rstrip("/")
        if "*" in named:
            probe = REPO_ROOT / pathlib.PurePosixPath(named).parent

        if not probe.exists():
            missing.append(named)

    assert not missing, (
        "The Layout table in CLAUDE.md names paths that do not exist: "
        f"{', '.join(missing)}. CLAUDE.md is the first thing anybody reads, so a "
        "row pointing at nothing sends them looking for something that is not "
        "there."
    )


def test_every_top_level_directory_is_in_the_layout_table() -> None:
    """Nothing real is left undescribed.

    THIS IS THE ONE THAT FOUND SOMETHING. `tools/` was absent from the table
    until issue #662, while holding more of the fast test suite than `sim/tests/`
    and the only dependency the repository has.
    """
    named = layout_table_paths()

    undescribed = []
    for entry in sorted(REPO_ROOT.iterdir()):
        if not entry.is_dir() or entry.name in NOT_PART_OF_THE_LAYOUT:
            continue
        # A row for a subdirectory describes its parent well enough: `game/`
        # would be covered by `game/Source/Cataclysm/` alone.
        if not any(path == entry.name or path.startswith(f"{entry.name}/")
                   for path in named):
            undescribed.append(entry.name)

    assert not undescribed, (
        "These top-level directories are not named anywhere in the Layout table "
        f"in CLAUDE.md: {', '.join(undescribed)}. Add a row saying what each is "
        "for, or add it to NOT_PART_OF_THE_LAYOUT in this file if it is not part "
        "of the project's structure."
    )
