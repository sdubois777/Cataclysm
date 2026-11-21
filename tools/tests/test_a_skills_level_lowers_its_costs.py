"""A skill's level lowers its costs, and a reserve already held is re-priced wherever a level can rise.

WHY THIS EXISTS. Issue #2802, the project owner's ruling of 2026-11-21: "make it so leveling the skill lowers the cost
and increases the damage ... That way, the higher you level Subjugate the more mobs you can control." Ruled by the
coordinating session the same day: each level multiplies a skill's mana cost, its Fervour cost and what each thing it
makes reserves by `UCataclysmSkillLevels::CostFactorPerLevel`, and a held reserve is RE-PRICED when its skill levels,
because a thrall is taken for good.

WHY IT IS CHECKED FROM PYTHON. A level can rise in four places -- a kill's experience, the character's own experience
lifting a capped skill, and two direct sets used by saves and tests -- and only some are reached by an automation test.
Continuous integration compiles no C++. A re-price call dropped from one of them leaves every test passing while the
reserve stays at level 1's figure in that case.

WHAT IS CHECKED:

  1. each of the four places calls `RepriceReservesForLevelsGained` with the levels it took before the change;
  2. the Fervour cost, the mana cost and a summon's reserve each multiply by `UCataclysmSkillLevels::CostFactorOf`.

`Cataclysm.LevelCosts.` (`CataclysmLevelCostsTests.cpp`) tests what the factor does.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
STATE = SOURCE / "Player" / "CataclysmPlayerState.cpp"
ABILITY = SOURCE / "AbilitySystem" / "CataclysmGameplayAbility.cpp"
TEMPLATES = SOURCE / "AbilitySystem" / "CataclysmSkillTemplates.cpp"


def code(path: pathlib.Path) -> str:
    """The file with each `//` comment taken off its line, so a mention in a comment cannot satisfy a check."""
    return "\n".join(re.sub(r"//.*$", "", line) for line in path.read_text(encoding="utf-8").splitlines())


def squeeze(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str, where: str) -> str:
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    raise AssertionError(f"{where}: the body of {signature!r} never closes")


@pytest.mark.parametrize("signature, before", [
    ("int32 ACataclysmPlayerState::GrantExperience(", "SkillsBefore"),
    ("void ACataclysmPlayerState::SetLevelAndExperience(", "SkillsBefore"),
    ("void ACataclysmPlayerState::GrantSkillExperience(", "Before"),
    ("void ACataclysmPlayerState::SetSkillExperience(", "Before"),
])
def test_every_place_a_level_can_rise_reprices_what_the_skill_made(signature: str, before: str) -> None:
    found = squeeze(body(code(STATE), signature, "CataclysmPlayerState.cpp"))
    assert f"const TMap<FName, int32> {before} = SkillLevelsNow();" in found, (
        f"{signature} no longer takes the skill levels before it changes anything")
    assert f"RepriceReservesForLevelsGained({before});" in found, (
        f"{signature} can raise a skill's level and does not re-price what the skill made")


def test_the_reprice_reaches_every_skill_whose_level_rose() -> None:
    found = squeeze(body(code(STATE), "void ACataclysmPlayerState::RepriceReservesForLevelsGained(",
                         "CataclysmPlayerState.cpp"))
    assert "UCataclysmSkillLevels::RepriceReservesOf(this, Each.Key);" in found, "a risen level re-prices nothing"


@pytest.mark.parametrize("path, signature, what", [
    (ABILITY, "float UCataclysmGameplayAbility::FervourCostFor(", "the Fervour cost"),
    (ABILITY, "float UCataclysmGameplayAbility::ManaCostAtTheHoldersLevel(", "the mana cost"),
    (TEMPLATES, "float FervourReserveFor(", "a summon's reserve"),
])
def test_each_cost_is_lowered_by_the_skills_level(path: pathlib.Path, signature: str, what: str) -> None:
    found = squeeze(body(code(path), signature, path.name))
    assert "UCataclysmSkillLevels::CostFactorOf(" in found, f"{what} is not lowered by the skill's level"
