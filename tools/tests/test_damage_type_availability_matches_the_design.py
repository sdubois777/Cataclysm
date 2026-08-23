"""Which damage types a weapon can carry, checked document against data.

WHY THIS EXISTS. `docs/Cataclysm_GDD_v2.md` has a Damage Types and Skill
Availability table saying which weapon types each damage type appears on, and
`game/Data/WeaponSkills.csv` has one row per designed pairing. They are two
statements of one fact and nothing compared them. Issue #857 made that matter:
`UCataclysmDropRoll::RollDamageTypes` draws a dropped weapon's damage types from
the pairings in the CSV, so the CSV is now what decides what a weapon can carry
and the document is what a designer reads.

WHAT WOULD GO WRONG WITHOUT IT. A damage type added to the document but not the
sheet reads as available and never drops. One added to the sheet but not the
document drops and is undocumented. Neither errors.

A PAIRING COUNTS WHETHER OR NOT ITS SKILL IS DESIGNED, matching the C++. The
matrix carries a row for every combination the design allows and leaves the skill
name empty where it has not been written, so an undesigned pairing is visible
rather than absent. Availability is what the design allows.

WHAT IT DELIBERATELY DOES NOT CHECK. Whether the skill on a pairing is designed,
which is issue #62, and whether the roll draws well, which the C++ automation
tests under `Cataclysm.Drop` cover.
"""

from __future__ import annotations

import csv
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
WEAPON_SKILLS = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
ITEM_BASES = REPO_ROOT / "game" / "Data" / "ItemBases.csv"

#: The header of the table this file reads.
TABLE_HEADER = "| Damage Type | Available Weapon Types |"

#: The Chaos row states a rule rather than a list.
EVERY_WEAPON = "All weapon types"

#: The weapon type used by rows that apply whatever weapon is held -- the auras.
#: Not a weapon type, and counting it would give every weapon every damage type.
WEAPON_INDEPENDENT = "All"

#: What a base's MaxDamageTypes must be, by how many hands it takes. Stated in
#: `docs/Cataclysm_GDD_v2.md`: "A one-handed weapon can hold at most four damage
#: types; a two-handed weapon at most eight."
LIMIT_BY_HANDS = {0: 0, 1: 4, 2: 8}


def rows_of(path: pathlib.Path) -> list[dict]:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def skills() -> list[dict]:
    return rows_of(WEAPON_SKILLS)


@pytest.fixture(scope="module")
def bases() -> list[dict]:
    return rows_of(ITEM_BASES)


@pytest.fixture(scope="module")
def weapon_types(bases: list[dict]) -> set[str]:
    return {row["WeaponType"].strip() for row in bases
            if row["WeaponType"].strip()}


@pytest.fixture(scope="module")
def in_the_document(weapon_types: set[str]) -> dict[str, set[str]]:
    """Damage type to the weapon types the design document lists for it."""
    if not GDD.is_file():
        pytest.skip(f"{GDD.name} is not present")
    lines = GDD.read_text(encoding="utf-8").splitlines()

    try:
        start = next(i for i, line in enumerate(lines)
                     if line.strip() == TABLE_HEADER)
    except StopIteration:
        pytest.fail(
            f"{GDD.name} no longer has a table headed {TABLE_HEADER!r}. If it "
            "was renamed, fix this constant rather than deleting the test.")

    found: dict[str, set[str]] = {}
    # +2 skips the header row and the alignment row under it.
    for line in lines[start + 2:]:
        if not line.startswith("| "):
            break
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) < 2:
            break
        damage_type, listed = cells[0], cells[1]
        if listed.startswith(EVERY_WEAPON):
            found[damage_type] = set(weapon_types)
        else:
            found[damage_type] = {part.strip() for part in listed.split(",")
                                  if part.strip()}
    assert found, f"no rows were read from the table in {GDD.name}"
    return found


@pytest.fixture(scope="module")
def in_the_data(skills: list[dict]) -> dict[str, set[str]]:
    """Damage type to the weapon types the skill matrix has a row for."""
    found: dict[str, set[str]] = {}
    for row in skills:
        weapon = row["WeaponType"].strip()
        damage = row["DamageType"].strip()
        if not weapon or not damage or weapon == WEAPON_INDEPENDENT:
            continue
        found.setdefault(damage, set()).add(weapon)
    return found


def test_the_document_and_the_matrix_name_the_same_damage_types(
        in_the_document, in_the_data) -> None:
    assert set(in_the_document) == set(in_the_data), (
        "the Damage Types and Skill Availability table in "
        f"docs/Cataclysm_GDD_v2.md names {sorted(in_the_document)} and "
        f"game/Data/WeaponSkills.csv has rows for {sorted(in_the_data)}")


def test_every_damage_type_is_on_the_same_weapons_in_both(
        in_the_document, in_the_data) -> None:
    """THE ONE THAT MATTERS. A dropped weapon's damage types are drawn from the
    matrix, so a disagreement here is a weapon carrying something the design
    never gave it, or never carrying something the design did."""
    wrong = []
    for damage_type in sorted(set(in_the_document) & set(in_the_data)):
        document = in_the_document[damage_type]
        data = in_the_data[damage_type]
        if document == data:
            continue
        missing = sorted(document - data)
        extra = sorted(data - document)
        parts = []
        if missing:
            parts.append(f"the document lists {missing} and the matrix has no "
                         "row for them")
        if extra:
            parts.append(f"the matrix has rows for {extra} and the document "
                         "does not list them")
        wrong.append(f"{damage_type}: {'; '.join(parts)}")

    assert not wrong, (
        "the Damage Types and Skill Availability table in "
        "docs/Cataclysm_GDD_v2.md disagrees with game/Data/WeaponSkills.csv: "
        + " | ".join(wrong))


def test_chaos_is_on_every_weapon_type(in_the_data, weapon_types) -> None:
    """The document says so and gives the reason: "Chaos is unrestricted because
    the Chaos Shaper changes form based on weapon type." It is the only damage
    type stated as a rule rather than a list, so it is the one that would go
    stale silently when a weapon type is added."""
    assert in_the_data.get("Chaos", set()) == weapon_types, (
        "game/Data/WeaponSkills.csv does not give Chaos a row for every weapon "
        f"type; it is missing {sorted(weapon_types - in_the_data.get('Chaos', set()))}")


def test_every_base_carries_the_limit_its_hands_allow(bases) -> None:
    """`MaxDamageTypes` is four for a one-hander, eight for a two-hander and
    zero for everything else. The roll reads it, so a base with the wrong figure
    would quietly change how many types that weapon can hold."""
    wrong = [f"{row['BaseName']}: {row['Hands']} hands, MaxDamageTypes "
             f"{row['MaxDamageTypes']}"
             for row in bases
             if int(row["MaxDamageTypes"]) != LIMIT_BY_HANDS[int(row["Hands"])]]
    assert not wrong, (
        "game/Data/ItemBases.csv gives a base a MaxDamageTypes its hand count "
        f"does not allow: {wrong}")


def test_no_weapon_type_is_left_with_nothing_to_roll(
        in_the_data, weapon_types) -> None:
    """A weapon type with no damage types at all could drop and carry none,
    which the design does not allow: a weapon rolls "from one damage type up
    to" its cap."""
    empty = sorted(weapon_types - set().union(*in_the_data.values())
                   if in_the_data else weapon_types)
    assert not empty, (
        "game/Data/WeaponSkills.csv has no row at all for these weapon types, "
        f"so a dropped one could carry no damage type: {empty}")


def test_where_the_designed_types_bind_below_the_base_limit(
        in_the_data, bases, weapon_types) -> None:
    """RECORDS A FACT RATHER THAN CALLING IT WRONG, and the fact is load-bearing.

    For several weapons the number of damage types designed for them is lower
    than their base's MaxDamageTypes, so it is that number and not the base
    limit that caps a roll. A Shield's base limit is four and three types are
    designed for it. A 2H Crossbow's base limit is eight and three are designed.

    `UCataclysmDropRoll::MaxDamageTypesFor` takes this as a third limit
    alongside the base's own and the difficulty tier, and neither
    `docs/Cataclysm_GDD_v2.md` nor `sim/cataclysm_sim/affixes.py` mentions it.
    If this list ever empties, that third limit has stopped doing anything and
    the code carrying it should say so rather than sitting there unexercised.
    """
    designed: dict[str, int] = {weapon: 0 for weapon in weapon_types}
    for weapons in in_the_data.values():
        for weapon in weapons:
            designed[weapon] += 1

    limit_of = {row["WeaponType"].strip(): int(row["MaxDamageTypes"])
                for row in bases if row["WeaponType"].strip()}

    binds = sorted(weapon for weapon in weapon_types
                   if designed[weapon] < limit_of[weapon])
    assert binds, (
        "no weapon type has fewer damage types designed for it than its base "
        "allows, so the third limit in "
        "UCataclysmDropRoll::MaxDamageTypesFor no longer constrains anything. "
        "Either a weapon gained damage types or a base limit dropped; check "
        "which, and say so there.")
