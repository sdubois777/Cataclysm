"""The count of undesigned skills in the C++ comment must match the table.

WHY THIS EXISTS, AND IT IS NOT A HYPOTHETICAL. The class comment on
`UCataclysmUndesignedSkill` said for weeks that every skill the design names has
"NO numbers ... There is nothing yet to build a real ability from". That was true
when it was written on 2026-08-03 and stopped being true as skills were designed.
On 2026-08-21 it was read, believed, and a recommendation was made on it that was
wrong: by then 58 of the 112 named skills had a shape and its parameters.

A COMMENT WITH A NUMBER IN IT IS A CLAIM, and this repository already treats
claims that way -- `tools/tests/test_game_readme_is_true.py` does the same for
`game/README.md`. This is the same guard for the one comment that has already
misled somebody.

WHAT IT COMPARES. The number in the comment against
`game/Data/WeaponSkills.csv`, which is what the engine actually reads. That file
is generated from `docs/All_Things_Cataclysm.xlsx`, so designing one of the
remaining skills in the workbook and regenerating the table fails this until the
comment is corrected.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
          / "CataclysmUndesignedSkill.h")

TABLE = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def rows() -> list[dict[str, str]]:
    if not TABLE.is_file():
        pytest.skip(f"{TABLE.name} is not present")

    # `utf-8-sig` because the generator writes a byte order mark, which would
    # otherwise become part of the first column's name and make every lookup on
    # it fail.
    with TABLE.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def named(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    return [row for row in rows if (row.get("SkillName") or "").strip()]


def shaped(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    return [row for row in named(rows) if (row.get("Shape") or "").strip()]


def undesigned(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    return [row for row in named(rows) if not (row.get("Shape") or "").strip()]


def claimed(name: str) -> int:
    """One number out of the class comment, by the label in front of it."""
    text = read(HEADER)
    match = re.search(rf"{name}:\s*(\d+)", text)
    if not match:
        pytest.fail(f"could not find '{name}' in {HEADER.name}; the comment "
                    "that claims it has been rewritten, so check the number is "
                    "still true and update this test's label")
    return int(match.group(1))


def test_the_comment_says_how_many_skills_are_undesigned(rows):
    """The number in the comment is the number of named, shapeless rows."""
    assert claimed("UNDESIGNED SKILLS") == len(undesigned(rows)), (
        f"{HEADER.name} claims {claimed('UNDESIGNED SKILLS')} undesigned "
        f"skills; {TABLE.name} holds {len(undesigned(rows))}. Correct the "
        "comment, or the workbook if the table is what is wrong."
    )


def test_the_comment_says_how_many_skills_are_named(rows):
    assert claimed("NAMED SKILLS") == len(named(rows)), (
        f"{HEADER.name} claims {claimed('NAMED SKILLS')} named skills; "
        f"{TABLE.name} holds {len(named(rows))}."
    )


def test_the_two_counts_account_for_every_named_skill(rows):
    """Undesigned plus designed is every skill with a name, or one of the two
    numbers is measuring something other than what the comment says it is."""
    assert len(undesigned(rows)) + len(shaped(rows)) == len(named(rows))


def test_some_skills_really_are_designed(rows):
    """THE CONTROL. Every test above is satisfied by a table in which nothing has
    a shape, which is exactly the state the stale comment described. This is what
    says the distinction is real."""
    assert len(shaped(rows)) > 0, (
        "no skill in the table has a shape, so this class is not a placeholder "
        "for a minority any more and the comment needs rewriting rather than "
        "renumbering"
    )
