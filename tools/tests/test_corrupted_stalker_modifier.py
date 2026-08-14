"""The Corrupted Stalker dungeon modifier exists in the data, not only in prose.

WHY THIS EXISTS. Issue #504. `docs/Cataclysm_GDD_v2.md` describes the Corrupted
Stalker in full in section VIII — a corrupted former player character, drawn from
a shared table of characters consumed by Worn Residue, that hunts the player
across a dungeon's floors. **It was the only dungeon modifier described in prose
and missing from the data.**

WHY THAT WAS NOT COSMETIC. Every dungeon modifier carries a weight, and the sum
of the weights on a dungeon is the Modifier Score in the Enemy Score formula in
section X. A modifier with no weight cannot be scored, so a dungeon carrying this
one could not have its difficulty computed at all.

WHAT WAS DECIDED, by the project owner on 2026-08-14: weight 20, the top band,
and it applies to all eight Cataclysms.

"APPLIES TO ALL EIGHT" ALREADY HAD A NAME. The issue expected a new category. It
is `Generic`, which ten enemy modifiers already use for the same meaning and
which `tools/generate_datatables.py` already accepts in `CATACLYSM_TYPES`. No
code changed. This file holds that choice, because the tempting alternative is a
new word like "All" and two names for one concept in the shipped tables is the
defect the Magic Find decision of 2026-08-05 was written to prevent.

WHAT IS ASSERTED HERE.

    the modifier is in the generated dungeon modifier table
    its weight is 20 and that band is not empty of other members, so 20 still
      means what it meant when it was chosen
    its Cataclysm Type is Generic, and Generic means something in this project
      rather than being invented here
    it is the only dungeon modifier using Generic, so the value is not quietly
      spreading
    the design document states both values and no longer says they are unset
"""

from __future__ import annotations

import csv
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DUNGEON_MODIFIERS = REPO_ROOT / "game" / "Data" / "DungeonModifiers.csv"
ENEMY_MODIFIERS = REPO_ROOT / "game" / "Data" / "EnemyModifiers.csv"

MODIFIER = "Corrupted Stalker"

#: The top weight band. Decided 2026-08-14.
WEIGHT = 20.0

#: The Cataclysm Type meaning "any of the eight". Not invented here.
ANY_CATACLYSM = "Generic"


def unwrapped(text: str) -> str:
    return " ".join(text.split())


def rows_of(path: pathlib.Path) -> list[dict]:
    if not path.is_file():
        pytest.skip(f"{path.name} has not been generated")
    with path.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def modifiers() -> list[dict]:
    return rows_of(DUNGEON_MODIFIERS)


@pytest.fixture(scope="module")
def stalker(modifiers: list[dict]) -> dict:
    row = next((r for r in modifiers if r["ModifierName"] == MODIFIER), None)
    assert row is not None, (
        f"{DUNGEON_MODIFIERS.name} has no {MODIFIER!r} row. It is described in "
        f"full in section VIII of the design document, and a modifier with no "
        f"row has no weight, so a dungeon carrying it cannot be scored. "
        f"Issue #504.")
    return row


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return unwrapped(GDD.read_text(encoding="utf-8"))


# --------------------------------------------------------------------------
# The row, and the two values that were decided
# --------------------------------------------------------------------------

def test_its_weight_is_the_top_band(stalker) -> None:
    """The weight is what makes the modifier scorable at all."""
    assert float(stalker["Weight"]) == WEIGHT, (
        f"{MODIFIER} has weight {stalker['Weight']}, not {WEIGHT}. The top band "
        f"was chosen on 2026-08-14 because this modifier places an enemy "
        f"carrying a real player's class, level, passives, equipment and "
        f"skills. Issue #504.")


def test_the_top_band_still_has_other_members(modifiers) -> None:
    """A band of one says nothing about difficulty. Weight 20 was chosen
    relative to what else sits there — Edict of Silence, Reality Twister, The
    Reaper — so if those moved, 20 no longer means what it meant."""
    at_top = [r["ModifierName"] for r in modifiers
              if float(r["Weight"]) == WEIGHT]
    assert len(at_top) >= 5, (
        f"only {len(at_top)} dungeon modifier(s) sit at weight {WEIGHT}: "
        f"{sorted(at_top)}. Twelve did besides the Corrupted Stalker when that "
        f"weight was chosen. A band with almost nothing in it no longer "
        f"describes a level of difficulty. Issue #504.")


def test_it_applies_to_every_cataclysm(stalker) -> None:
    """The pool it draws from holds characters consumed in every Cataclysm, so
    tying it to one would misdescribe the mechanic."""
    assert stalker["CataclysmType"] == ANY_CATACLYSM, (
        f"{MODIFIER} has Cataclysm Type {stalker['CataclysmType']!r}, not "
        f"{ANY_CATACLYSM!r}. It draws from a shared table holding characters "
        f"consumed in every Cataclysm. Issue #504.")


def test_generic_is_not_a_word_invented_for_this_row() -> None:
    """The load-bearing check, and the one that stops a second name for one
    idea. `Generic` was already the Cataclysm Type meaning "any of the eight",
    used by the enemy modifier table. If those entries were renamed, this row's
    value became a private invention and should follow them."""
    enemy = rows_of(ENEMY_MODIFIERS)
    generic = [r["ModifierName"] for r in enemy
               if r["CataclysmType"] == ANY_CATACLYSM]
    assert generic, (
        f"no enemy modifier uses the Cataclysm Type {ANY_CATACLYSM!r} any more. "
        f"That value was chosen for the Corrupted Stalker because it already "
        f"meant 'any of the eight' elsewhere in this project rather than being "
        f"invented for one row. If the enemy modifiers now use a different "
        f"word, the dungeon modifier should use the same one. Issue #504.")


def test_it_is_the_only_dungeon_modifier_that_applies_everywhere(
        modifiers) -> None:
    """Not a rule, a tripwire. It is the first and only Generic dungeon
    modifier. A second one appearing is a design change nobody has recorded,
    because a Generic modifier is drawable by all eight Cataclysms and so
    competes with every other pool at once."""
    everywhere = sorted(r["ModifierName"] for r in modifiers
                        if r["CataclysmType"] == ANY_CATACLYSM)
    assert everywhere == [MODIFIER], (
        f"these dungeon modifiers apply to every Cataclysm: {everywhere}. "
        f"Only {MODIFIER!r} is meant to. A Generic dungeon modifier can be "
        f"drawn by all eight pools, so adding one is a larger change than "
        f"adding an ordinary modifier and needs its own decision. Issue #504.")


# --------------------------------------------------------------------------
# The design document agrees with the data
# --------------------------------------------------------------------------

def test_the_document_states_the_weight(document) -> None:
    assert "**Weight 20, the top band.**" in document, (
        "the Corrupted Stalker section does not state the modifier's weight. "
        "It is 20. Issue #504.")
    assert "**Weight.** Not set." not in document, (
        "the Corrupted Stalker section still says its weight is not set. It "
        "was set to 20 on 2026-08-14. Issue #504.")


def test_the_document_states_the_cataclysm_type_and_why(document) -> None:
    """Naming the value is not enough. A reader meeting the only Generic
    dungeon modifier in the project needs to know it is deliberate and that the
    word already meant something."""
    assert "Its Cataclysm Type is Generic" in document, (
        "the Corrupted Stalker section does not say which Cataclysm Type the "
        "modifier carries. Issue #504.")
    assert "That is not a new category" in document, (
        "the section states the Cataclysm Type without saying that Generic "
        "already meant 'any of the eight' in the enemy modifier table. Without "
        "that, the next reader may take it for an invented one-off and replace "
        "it. Issue #504.")
