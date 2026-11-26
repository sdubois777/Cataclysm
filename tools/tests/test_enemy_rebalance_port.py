"""The game's enemy rebalance factors are the simulation's, and the game applies them where it builds a creature.

WHY THIS EXISTS. Issue #2884: every creature's health and damage are multiplied by a factor per difficulty tier,
fitted to the reference character under the whole-number affix ranges of issue #2883. The factors are written twice,
`UCataclysmEnemyScore::HealthFactorByTier` and `DamageFactorByTier` in the game and `enemy_stats.HEALTH_FACTOR_BY_TIER`
and `DAMAGE_FACTOR_BY_TIER` in the simulation. Continuous integration compiles no C++, so this holds the two equal and
checks the game multiplies by them. Two copies of a number are two numbers.

WHAT IS CHECKED:

  1. each C++ table is the simulation's, tier by tier, and has eight entries;
  2. tier 1 is 1.00 in both, which is what keeps the tier 1 ring scales and the opening guards standing;
  3. `UCataclysmEnemyStrength::StartingFiguresFor` multiplies health by `HealthFactor` and damage by `DamageFactor`
     of the floor's difficulty tier, and does not move armour;
  4. the simulation's `stats_for` applies them the same way.
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
HEADER = SOURCE / "Dungeon" / "CataclysmEnemyScore.h"
STRENGTH = SOURCE / "Dungeon" / "CataclysmEnemyStrength.cpp"

sys.path.insert(0, str(REPO_ROOT / "sim"))
from cataclysm_sim import enemy_stats  # noqa: E402


def _table(name: str) -> tuple[float, ...]:
    text = HEADER.read_text(encoding="utf-8")
    match = re.search(rf"static constexpr double {name}\[8\] = \{{([^}}]*)\}};", text)
    assert match, f"{name}[8] is not declared in {HEADER.name}"
    return tuple(float(value) for value in match.group(1).split(","))


def _code(path: pathlib.Path) -> str:
    text = re.sub(r"/\*.*?\*/", "", path.read_text(encoding="utf-8"), flags=re.S)
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def test_the_games_health_factors_are_the_simulations():
    assert _table("HealthFactorByTier") == enemy_stats.HEALTH_FACTOR_BY_TIER


def test_the_games_damage_factors_are_the_simulations():
    assert _table("DamageFactorByTier") == enemy_stats.DAMAGE_FACTOR_BY_TIER


def test_tier_one_is_untouched_in_both():
    assert enemy_stats.rebalance_factors(1) == (1.0, 1.0)
    assert _table("HealthFactorByTier")[0] == 1.0 and _table("DamageFactorByTier")[0] == 1.0


def test_the_game_multiplies_health_and_damage_where_it_builds_a_creature():
    code = _code(STRENGTH)
    start = code.index("bool UCataclysmEnemyStrength::StartingFiguresFor(")
    body = code[start:code.index("return true;", start)]
    health = body[body.index("Out.Health ="):body.index(";", body.index("Out.Health ="))]
    damage = body[body.index("Out.AttackDamage ="):body.index(";", body.index("Out.AttackDamage ="))]
    armour = body[body.index("Out.Armour ="):body.index(";", body.index("Out.Armour ="))]
    assert "UCataclysmEnemyScore::HealthFactor(Floor.DifficultyTier)" in health
    assert "UCataclysmEnemyScore::DamageFactor(Floor.DifficultyTier)" in damage
    assert "Factor(" not in armour


def test_the_simulation_multiplies_health_and_damage_by_the_tiers_factors():
    plain = enemy_stats.stats_for("Common", 1000.0, tier=1)
    for tier in range(1, 9):
        health_factor, damage_factor = enemy_stats.rebalance_factors(tier)
        built = enemy_stats.stats_for("Common", 1000.0, tier=tier)
        assert built.health == plain.health * health_factor
        assert built.damage_per_hit == plain.damage_per_hit * damage_factor
        assert built.armor == plain.armor
