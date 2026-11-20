"""A skill's level: what it costs, what caps it, and what it does to the skill's damage. Issue #2764.

A PORT of `game/Source/Cataclysm/Character/CataclysmSkillLevels.cpp`, which is the game's copy;
`tools/tests/test_skill_levels_port.py` holds the constants and the paired levels equal to it. The shape was ruled by the
project owner on 2026-11-19 ("I like the idea of skills leveling as you use them"), and `docs/DECISIONS.md` that day has
the research and the rulings.

  * A creature's experience is split at its death among the skills that damaged it, by the share of its health each
    removed; the basic attack does not level.
  * Skill level L costs PRICE_SHARE_OF_THE_CHARACTERS of the character's own experience up to the character level Path
    of Exile pairs with gem level L (poedb.tw, Fireball and Cleave, "Requires Level"). 0.25 is a judgement.
  * A skill is capped at MAX_LEVEL and at the level paired with the character's level; experience above it is kept.
  * Each level multiplies the skill's damage percentage by DAMAGE_FACTOR_PER_LEVEL: level 20 is 2.85 times level 1, as
    Path of Exile's Cleave is (179.4% of base damage at gem level 1, 511.2% at 20).

THE CHARACTER'S CURVE IS HANDED IN rather than imported: `total_to_reach(level)` is the experience from level 1 to
`level`, which `sim/analyse_experience_curve.py` works out and the game's `UCataclysmExperience::TotalToReach` matches
(`tools/tests/test_experience_curve_port.py`). This module stays free of that script, as the package stays free of
every dependency.
"""

from __future__ import annotations

from typing import Callable

FIRST_LEVEL = 1
MAX_LEVEL = 20
DAMAGE_FACTOR_PER_LEVEL = 1.0567
PRICE_SHARE_OF_THE_CHARACTERS = 0.25

#: Path of Exile's "Requires Level" for gem levels 1 to 20, the same for Fireball and Cleave (poedb.tw, 2026-11-19).
PAIRED_CHARACTER_LEVELS = (1, 2, 4, 7, 11, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 67, 70)


def paired_character_level(skill_level: int) -> int:
    """The character level paired with a skill level, clamped to 1 and MAX_LEVEL."""
    return PAIRED_CHARACTER_LEVELS[min(max(skill_level, FIRST_LEVEL), MAX_LEVEL) - 1]


def experience_to_reach(skill_level: int, total_to_reach: Callable[[int], float]) -> float:
    """A skill's total experience at the start of this level: 0 at level 1."""
    return PRICE_SHARE_OF_THE_CHARACTERS * total_to_reach(paired_character_level(skill_level))


def level_for(experience: float, character_level: int, total_to_reach: Callable[[int], float]) -> int:
    """The highest level paid for whose paired character level the character has reached."""
    level = FIRST_LEVEL
    for following in range(FIRST_LEVEL + 1, MAX_LEVEL + 1):
        if paired_character_level(following) > character_level:
            break
        if experience < experience_to_reach(following, total_to_reach):
            break
        level = following
    return level


def damage_factor(skill_level: int) -> float:
    """What a skill at this level multiplies its damage percentage by: 1 at level 1."""
    return DAMAGE_FACTOR_PER_LEVEL ** (min(max(skill_level, FIRST_LEVEL), MAX_LEVEL) - 1)


def split_a_kill(kill_experience: float, removed_by_skill: dict, max_health: float) -> dict:
    """A creature's experience shared among the skills that hurt it, by the share of its health each removed: against
    its maximum health, or everything the skills removed when that is more, so the shares never pass the whole kill."""
    removed = sum(max(0.0, amount) for amount in removed_by_skill.values())
    whole = max(max_health, removed)
    if kill_experience <= 0 or whole <= 0:
        return {}
    return {skill: kill_experience * amount / whole for skill, amount in removed_by_skill.items() if amount > 0}
