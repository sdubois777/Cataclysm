"""The eight primary attributes are named the same in the data and in the C++.

WHY THIS EXISTS. Three files name the eight primary attributes — Agility,
Ferocity, Constitution, Vitality, Mind, Spirit, Efficacy and Luck — and nothing
compared them:

    game/Source/Cataclysm/AbilitySystem/CataclysmPrimaryAttributeSet.h
        the Gameplay Ability System attribute set the running game uses. Each
        attribute is an `FGameplayAttributeData` member.

    game/Data/Attributes.csv
        generated from the Attributes sheet of docs/All_Things_Cataclysm.xlsx.
        Says what one point of each attribute does: agility grants 2% movement
        speed and 0.5% evasion per point, and so on.

    docs/Cataclysm_GDD_v2.md
        section VI, where the attributes are described.

A rename in one of them and not the others is silent. The attribute effect rows
would stop matching the attribute they belong to, and nothing would say so.

WHY IT MATTERS MORE NOW. The project owner decided on 2026-08-04 that gear must be
able to grant primary attributes, reversing an earlier rule; see reversal 1 of
"Nine decisions from an audit of the affix pool" in docs/DECISIONS.md. An
attribute affix has to name an attribute in its `Stat` column, so from that point
on a misspelt or renamed attribute is an affix that grants nothing.

WHERE THIS CAME FROM. These checks were written for issue #204 as part of a guard
that no affix grants an attribute. That rule is reversed and the guard is gone;
these checks are the part of it that is still true and still worth having, so they
moved here rather than being deleted with it.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ATTRIBUTE_CSV = REPO_ROOT / "game" / "Data" / "Attributes.csv"
ATTRIBUTE_SET_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" /
                        "AbilitySystem" / "CataclysmPrimaryAttributeSet.h")

#: How many there are. Stated because a rename that dropped one would otherwise
#: leave every comparison below happily agreeing on seven.
EXPECTED_COUNT = 8


def read_csv(path: pathlib.Path) -> list[dict[str, str]]:
    if not path.is_file():
        pytest.skip(f"{path.name} not present")
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def attribute_rows() -> list[dict[str, str]]:
    return read_csv(ATTRIBUTE_CSV)


@pytest.fixture(scope="module")
def primary_attributes(attribute_rows) -> frozenset[str]:
    """The attribute names in the generated table, lower-cased."""
    return frozenset(row["Attribute"].strip().lower() for row in attribute_rows)


def test_there_are_eight_of_them(primary_attributes):
    assert len(primary_attributes) == EXPECTED_COUNT, sorted(primary_attributes)


def test_the_generated_table_and_the_attribute_set_agree(primary_attributes):
    """The names in the data are the ones the running game uses.

    `CataclysmPrimaryAttributeSet.h` declares each attribute as an
    `FGameplayAttributeData` member. If the two lists disagree, one has been
    renamed and the attribute effect rows no longer describe the attribute they
    are attached to.
    """
    if not ATTRIBUTE_SET_HEADER.is_file():
        pytest.skip("the Unreal attribute set header is not present")
    declared = {name.lower() for name in re.findall(
        r"FGameplayAttributeData\s+(\w+);",
        ATTRIBUTE_SET_HEADER.read_text(encoding="utf-8"))}
    assert declared == set(primary_attributes), (
        f"only in {ATTRIBUTE_SET_HEADER.name}: "
        f"{sorted(declared - primary_attributes)}; "
        f"only in {ATTRIBUTE_CSV.name}: "
        f"{sorted(primary_attributes - declared)}")


def test_no_attribute_shares_a_name_with_a_stat_it_scales(attribute_rows,
                                                          primary_attributes):
    """An attribute and a stat must not collide.

    `Attributes.csv` maps each attribute to the stats it scales — agility to
    movement speed and evasion, vitality to maximum health and health
    regeneration. An attribute named after one of those stats would make
    "+N agility" and "+N movement speed" indistinguishable to anything reading a
    name, including an affix's `Stat` column.
    """
    scaled = {row["Stat"].strip().lower() for row in attribute_rows}
    collisions = sorted(scaled & primary_attributes)
    assert not collisions, collisions


def test_every_attribute_scales_at_least_one_stat(attribute_rows,
                                                  primary_attributes):
    """An attribute with no rows does nothing at all.

    It would still appear in both lists above and pass every other check here,
    because those compare names rather than effects.
    """
    with_effects = {row["Attribute"].strip().lower() for row in attribute_rows
                    if row["Stat"].strip()}
    assert with_effects == set(primary_attributes), sorted(
        set(primary_attributes) - with_effects)


def test_every_attribute_is_described_in_the_design_document(primary_attributes):
    """The design document names all eight.

    Checked case-insensitively against the whole document rather than one
    section, because the attributes are referred to in several places and pinning
    one section would break whenever the document is reorganised.
    """
    gdd = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
    if not gdd.is_file():
        pytest.skip("the design document is not present")
    body = gdd.read_text(encoding="utf-8").lower()
    missing = sorted(name for name in primary_attributes if name not in body)
    assert not missing, (
        f"{gdd.name} never mentions {missing}, which the game has as primary "
        "attributes")
