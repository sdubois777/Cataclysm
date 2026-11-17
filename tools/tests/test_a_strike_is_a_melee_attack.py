"""Every weapon skill carrying `Type.Strike` carries `Type.Melee`.

WHY. The project owner ruled on 2026-08-26, under issue #999, that a strike is
what "melee" means for a weapon skill, and every row then carrying `Type.Strike`
was given `Type.Melee`. A melee-scoped bonus reaches a skill only through that
tag, so a strike row without it silently misses every "melee" bonus in the
game: `Demonic_Greatsword_Ultimate`, The Whole Weight, did, until issue #944.
`tools/generate_datatables.py` (`tags_with_slot`) now refuses such a row, and
this holds both the rule and the generated data to it.

WHAT IT DOES NOT DECIDE. A skill with the engine shape Strike that is tagged as a
point-blank area rather than as a strike is not covered by #999; whether those
count as melee is a separate ruling, recorded on issue #944.
"""

from __future__ import annotations

import csv
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WEAPON_SKILLS = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402


def tags_of(row: dict[str, str]) -> list[str]:
    return [t.strip() for t in row["Tags"].split(",") if t.strip()]


def test_the_generator_refuses_a_strike_without_the_melee_tag() -> None:
    with pytest.raises(gen.DataError, match="Weapon Skills row 7: carries Type.Strike and not Type.Melee"):
        gen.tags_with_slot("Element.Demonic, Type.Strike", "Heavy", "Weapon Skills row 7")


def test_the_generator_accepts_a_strike_that_is_melee() -> None:
    assert gen.tags_with_slot("Type.Strike, Type.Melee", "Heavy", "row") == (
        "Type.Strike, Type.Melee, Slot.Heavy")


def test_every_generated_strike_is_a_melee_attack() -> None:
    with WEAPON_SKILLS.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    strikes = [row for row in rows if "Type.Strike" in tags_of(row)]
    missing = [row["Name"] for row in strikes if "Type.Melee" not in tags_of(row)]

    # THE COUNT, so the check cannot pass on an empty file: 31 rows carried
    # Type.Strike on development af957ae1.
    assert len(strikes) >= 31, f"only {len(strikes)} rows carry Type.Strike"
    assert not missing, (
        f"{len(missing)} weapon skill(s) carry Type.Strike and not Type.Melee: "
        f"{', '.join(missing)}. A strike is a melee attack (issue #999).")
