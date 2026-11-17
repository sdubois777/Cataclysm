"""A creature placed in a level can be given a rarity, and only a real one.

WHY THIS EXISTS. Until 2026-08-19 `ACataclysmEnemyCharacter::RarityStep` was a
`VisibleAnywhere` property, which greys the field out in the editor's Details
panel, and `SetRarityStep` had no caller outside the automation tests. The thing
meant to supply a rarity is the enemy generator, which is issue #508 and does not
exist. So **every creature in a play session was Common and nothing could change
it** — the drop rate, the magic find a rarer enemy adds to its own drops, and the
boss stun rule all read this field and all sat at rung zero, with no way to try
any other by hand.

It was found when the project owner asked how to set a creature to Boss in order
to see loot drop, and the answer was that they could not.

WHY THESE ARE PYTHON TESTS READING C++ SOURCE. Continuous integration compiles no
C++ at all, so nothing on a pull request would notice the property reverting.
This is the same arrangement
`tools/tests/test_enemy_tables_match_the_model.py` uses for
`FirstBossRarityStep`, and its own header says why.

WHAT IS NOT CHECKED HERE. That the Details panel actually honours the specifier,
which is Unreal's behaviour rather than this project's, and that a rarity set on a
placed creature survives the level being saved.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ENEMY_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmEnemyCharacter.h")

sys.path.insert(0, str(REPO_ROOT / "sim"))

#: The `UPROPERTY(...)` immediately above `int32 RarityStep`. The inner
#: alternation is what lets the body contain its own brackets, which it does:
#: `meta = (ClampMin = "0", ClampMax = "5")`.
PROPERTY = re.compile(
    r"UPROPERTY\((?P<body>[^)]*(?:\([^)]*\)[^)]*)*)\)\s*\n\s*int32 RarityStep")


@pytest.fixture(scope="module")
def specifiers() -> str:
    """Everything inside the rarity step's `UPROPERTY(...)`."""
    if not ENEMY_HEADER.is_file():
        pytest.fail(f"{ENEMY_HEADER.relative_to(REPO_ROOT)} does not exist")

    found = PROPERTY.search(ENEMY_HEADER.read_text(encoding="utf-8"))
    assert found, (
        "CataclysmEnemyCharacter.h no longer has a UPROPERTY immediately above "
        "'int32 RarityStep'. If the field was renamed, rename it here too. If "
        "the UPROPERTY was removed, the rarity is no longer a saved property, "
        "so a rarity set on a placed creature would not survive the level "
        "being saved.")
    return found.group("body")


def test_a_placed_creatures_rarity_can_be_set_in_the_editor(specifiers) -> None:
    """The Details panel has to accept a rarity or the ladder is unreachable."""
    assert "EditInstanceOnly" in specifiers, (
        f"The rarity step's UPROPERTY reads '{specifiers.strip()}'. It has to "
        "carry EditInstanceOnly, or the Details panel will not accept a rarity "
        "on a placed creature and nothing else in the project sets one.")

    assert "VisibleAnywhere" not in specifiers, (
        "The rarity step's UPROPERTY is VisibleAnywhere again, which greys the "
        "field out in the Details panel. That is the state this test was "
        "written to prevent returning to.")


def test_a_rarity_belongs_to_the_encounter_and_not_to_the_class(specifiers) -> None:
    """`EditAnywhere` would also allow a Blueprint default, and it should not.

    The field's own comment states the rule: rarity is the encounter's business
    and not the class's, and the same Brute is a Common in one room and an Elite
    in the next. A rarity set on the Brute Blueprint would be a class-wide answer
    to a per-encounter question, inherited by every Brute placed afterwards.
    """
    assert "EditAnywhere" not in specifiers, (
        f"The rarity step's UPROPERTY reads '{specifiers.strip()}'. "
        "EditAnywhere allows a Blueprint default as well as an instance value, "
        "so a rarity could be baked into the Brute Blueprint and inherited by "
        "every Brute placed after it. EditInstanceOnly is the specifier that "
        "keeps rarity a property of the encounter.")


def test_a_rarity_typed_into_the_panel_cannot_leave_the_ladder(specifiers) -> None:
    """The clamp `SetRarityStep` applies, applied to the Details panel too.

    Typing into a Details field does not go through that function. Without a
    clamp, a negative step makes `IsBoss`'s `RarityStep >= FirstBossRarityStep`
    comparison meaningless, and a step above the ladder matches no row in
    `game/Data/EnemyDrops.csv`, so the creature drops nothing at all.
    """
    low = re.search(r'ClampMin\s*=\s*"(-?\d+)"', specifiers)
    high = re.search(r'ClampMax\s*=\s*"(-?\d+)"', specifiers)

    assert low and high, (
        f"The rarity step's UPROPERTY reads '{specifiers.strip()}'. It needs "
        'meta = (ClampMin = "0", ClampMax = "<the last rung>") so a value typed '
        "into the Details panel cannot leave the rarity ladder.")

    assert int(low.group(1)) == 0, (
        f"The rarity step clamps to a minimum of {low.group(1)}. Common is rung "
        "0 and the ladder has nothing below it.")


def test_the_top_of_the_clamp_is_the_top_of_the_models_ladder(specifiers) -> None:
    """Pinned to the model rather than to the number, so a new rung fails here.

    A maximum below the top rung makes that rarity impossible to set by hand,
    which is the fault this whole file was written about. One above it lets a
    creature be given a rarity with no row in `EnemyDrops.csv`.
    """
    from cataclysm_sim.enemy_stats import RARITY_ORDER

    high = re.search(r'ClampMax\s*=\s*"(-?\d+)"', specifiers)
    assert high, "no ClampMax; the test above says what to do about it"

    top = len(RARITY_ORDER) - 1
    assert int(high.group(1)) == top, (
        f"The rarity step clamps to a maximum of {high.group(1)} and the "
        f"model's ladder in sim/cataclysm_sim/enemy_stats.py ends at {top}, "
        f"which is {RARITY_ORDER[-1]!r}. Move the clamp with the ladder.")
