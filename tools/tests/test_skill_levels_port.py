"""The game's skill levels and the simulation's copy hold the same numbers. Issue #2764.

`game/Source/Cataclysm/Character/CataclysmSkillLevels.h` and `.cpp` are the game's; `sim/cataclysm_sim/skill_levels.py`
is the copy the opening measurement runs (`sim/analyse_opening.py`, section 5). Two copies of a number are two numbers,
so every constant and every paired level is read out of the C++ and compared, and so is the shape of the price: a share
of the character's experience to the paired character level, which is what lets a re-tuned character curve move it.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "sim"))
sys.path.insert(0, str(ROOT / "tools" / "tests"))

from cataclysm_sim import skill_levels as py  # noqa: E402
from test_the_play_loop_is_logged_at_each_step import function_body, read_code  # noqa: E402

HEADER = ROOT / "game" / "Source" / "Cataclysm" / "Character" / "CataclysmSkillLevels.h"
SOURCE = ROOT / "game" / "Source" / "Cataclysm" / "Character" / "CataclysmSkillLevels.cpp"


def constant(name: str) -> str:
    match = re.search(rf"static constexpr \w+ {name} = ([0-9.]+);", read_code(HEADER))
    assert match, f"{name} is not a constexpr in {HEADER.relative_to(ROOT).as_posix()}"
    return match.group(1)


@pytest.mark.parametrize("cpp, python", [
    ("FirstLevel", "FIRST_LEVEL"),
    ("MaxLevel", "MAX_LEVEL"),
    ("DamageFactorPerLevel", "DAMAGE_FACTOR_PER_LEVEL"),
    ("CostFactorPerLevel", "COST_FACTOR_PER_LEVEL"),
    ("PriceShareOfTheCharacters", "PRICE_SHARE_OF_THE_CHARACTERS"),
])
def test_each_constant_is_the_same(cpp: str, python: str) -> None:
    assert float(constant(cpp)) == float(getattr(py, python)), (
        f"UCataclysmSkillLevels::{cpp} is {constant(cpp)} and skill_levels.{python} is {getattr(py, python)}")


def test_the_paired_character_levels_are_the_same_and_twenty() -> None:
    match = re.search(r"PairedCharacterLevels\[[^\]]*\]\s*=\s*\{([^}]*)\}", read_code(SOURCE))
    assert match, "no PairedCharacterLevels table in CataclysmSkillLevels.cpp"
    game = tuple(int(n) for n in re.findall(r"\d+", match.group(1)))
    assert game == py.PAIRED_CHARACTER_LEVELS, f"the game pairs {game} and the simulation {py.PAIRED_CHARACTER_LEVELS}"
    assert len(game) == py.MAX_LEVEL == 20
    assert list(game) == sorted(game) and len(set(game)) == len(game), "the paired levels must rise every level"


def test_the_price_is_a_share_of_the_characters_curve_to_the_paired_level() -> None:
    body = function_body(read_code(SOURCE), "double UCataclysmSkillLevels::ExperienceToReach(")
    assert "PriceShareOfTheCharacters" in body and "UCataclysmExperience::TotalToReach(PairedCharacterLevel(" in body, (
        "ExperienceToReach no longer prices a skill level as a share of the character's experience to the paired "
        "character level, which is what the port and the ruling say")


def test_level_twenty_deals_two_point_eight_five_times_level_one() -> None:
    assert py.damage_factor(1) == 1.0
    assert round(py.damage_factor(20), 2) == 2.85, "the owner's ruling: level 20 is 2.85 times level 1, Cleave's"
    assert py.damage_factor(25) == py.damage_factor(20), "the cap is 20"
