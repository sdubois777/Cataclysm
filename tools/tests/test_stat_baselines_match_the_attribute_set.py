"""A stat's starting value, pinned across the model, the C++ and the design.

WHY THIS EXISTS. Issue #243. `loot_quantity` started at zero in both
`sim/cataclysm_sim/character.py` and
`game/Source/Cataclysm/AbilitySystem/CataclysmCombatAttributeSet.cpp`, while every
source of loot quantity in the project is a percentage INCREASE. An increase
applied to zero is zero, so the stat was permanently zero however many attribute
points and affixes were spent on it. Nothing errored; the number was just always
zero, which is why it survived.

THE RULE THIS FILE HOLDS. A stat that is a percentage OF something starts at 100,
because 100 means unchanged. A stat that is an added percentage starts at 0 and
needs a flat source to give an increase something to scale.

    area of effect              percentage of what the skill does       100
    damage over time frequency  percentage of what the skill does       100
    loot quantity               percentage of what the dungeon drops    100
    magic find                  added percentage, flat source exists      0

WHAT IT CHECKS AND WHERE.

    sim/cataclysm_sim/character.py                       the model
    game/Source/.../CataclysmCombatAttributeSet.cpp      what the game runs on
    docs/Cataclysm_GDD_v2.md                             the design document

NOTHING ON A PULL REQUEST COMPILES THE C++, so the value in the attribute set is
read out of the source text rather than run. That is enough to catch the two ways
it goes wrong: the number being changed in one copy and not the other, and the
initialiser being deleted.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
COMBAT_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
              / "CataclysmCombatAttributeSet.cpp")
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: The model's stat name against the C++ initialiser that has to agree with it.
#: Every stat here is one whose starting value is a decision rather than an
#: accident, so a change to either copy has to be a change to both.
PAIRS: tuple[tuple[str, str], ...] = (
    ("area_of_effect", "InitAreaOfEffect"),
    ("dot_frequency", "InitDotFrequency"),
    ("loot_quantity", "InitLootQuantity"),
    ("magic_find", "InitMagicFind"),
    ("movement_speed", "InitMovementSpeed"),
    ("cooldown_reduction", "InitCooldownReduction"),
)

#: Stats that are a percentage OF something, so their unchanged value is 100.
PERCENTAGE_OF_SOMETHING = frozenset({"area_of_effect", "dot_frequency",
                                     "loot_quantity"})


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim import character
    return character


@pytest.fixture(scope="module")
def cpp() -> str:
    if not COMBAT_CPP.is_file():
        pytest.skip(f"{COMBAT_CPP.name} is not present")
    return COMBAT_CPP.read_text(encoding="utf-8")


def initialiser(source: str, name: str) -> float:
    match = re.search(rf"{name}\(\s*([0-9.]+)f?\s*\)", source)
    assert match, (
        f"{COMBAT_CPP.name} does not call {name}. Every stat with a chosen "
        "starting value has to be initialised, or it starts at zero by "
        "accident rather than by decision.")
    return float(match.group(1))


@pytest.mark.parametrize("stat,init_name", PAIRS)
def test_the_cpp_starting_value_matches_the_model(model, cpp, stat, init_name):
    assert initialiser(cpp, init_name) == pytest.approx(
        model.DEFAULT_STAT_LINE[stat].base), (
        f"{init_name} in {COMBAT_CPP.name} and {stat} in character.py "
        "disagree about what the stat starts at")


@pytest.mark.parametrize("stat", sorted(PERCENTAGE_OF_SOMETHING))
def test_a_percentage_of_something_starts_at_one_hundred(model, stat):
    """Zero here is the whole of issue #243: it leaves every increase with
    nothing to scale, silently."""
    assert model.DEFAULT_STAT_LINE[stat].base == pytest.approx(100.0), (
        f"{stat} is a percentage of something, so it starts at 100. A zero "
        "makes every increase to it produce zero and nothing reports an error.")


def test_magic_find_is_deliberately_not_one_of_them(model):
    """It is an added percentage, not a percentage of something, and it has a
    flat source. Listed here so a later reader does not 'fix' it to 100 by
    analogy with loot quantity."""
    assert "magic_find" not in PERCENTAGE_OF_SOMETHING
    assert model.DEFAULT_STAT_LINE["magic_find"].base == pytest.approx(0.0)


def test_every_source_of_loot_quantity_really_is_an_increase(model):
    """The reason loot quantity needs a baseline of 100 rather than a flat
    source. If a flat loot quantity affix is ever added, this is the test that
    says the reasoning has changed and the baseline is worth revisiting."""
    from cataclysm_sim import affixes

    assert "loot_quantity" in model.ATTRIBUTE_EFFECTS["luck"]

    granting = [a for a in affixes.AFFIX_POOL if a.stat == "loot_quantity"]
    assert granting, "no affix grants loot quantity at all"
    for affix in granting:
        assert affix.kind == "increased", (
            f"{affix.name} is a {affix.kind} source of loot quantity. If loot "
            "quantity now has a flat source, the case for its baseline of 100 "
            "has changed; see issue #243 and docs/DECISIONS.md.")


class TestTheDesignDocumentSaysTheSame:
    @staticmethod
    def body() -> str:
        if not GDD.is_file():
            pytest.skip("the design document is not present")
        return GDD.read_text(encoding="utf-8")

    def test_it_states_the_loot_quantity_baseline(self):
        assert "Loot quantity has a baseline of 100%" in self.body(), (
            "the design document does not say what loot quantity starts at. "
            "Without it, a reader of the attribute table sees only percentage "
            "increases and no baseline for them to apply to.")

    def test_it_states_that_magic_find_is_the_other_shape(self):
        assert "Magic find is not the same shape" in self.body()

    def test_it_still_states_the_area_of_effect_baseline_it_was_derived_from(
            self):
        assert "their baseline is 100% rather than zero" in self.body()
