"""The Corrupted Stalker's fight model: the multipliers that set it beside a Boss on the same floor.

Issue #1308, ruled by the coordinating session on 2026-10-25: the simulation's tier-aware Boss on the middle floor of
a fifty-floor Basic dungeon, the Stalker's potions counted. `sim/cataclysm_sim/fight.py` says what it models and what
it cannot know.
"""

from __future__ import annotations

import math
import pathlib
import re

import pytest

from cataclysm_sim import affixes as af
from cataclysm_sim import damage as dmg
from cataclysm_sim import fight

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
STALKER_CPP = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character" / "CataclysmCorruptedStalkerCharacter.cpp"
EFFECTS_CPP = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon" / "CataclysmDungeonModifierEffects.cpp"
TIERS = range(1, 9)


@pytest.fixture(scope="module")
def rows():
    return fight.table()


def test_every_tier_has_a_finite_positive_pair(rows):
    assert [r.tier for r in rows] == list(TIERS)
    for r in rows:
        assert math.isfinite(r.health_multiplier) and r.health_multiplier > 0, r
        assert math.isfinite(r.damage_multiplier) and r.damage_multiplier > 0, r


def test_at_its_health_multiplier_it_takes_as_long_to_kill_as_the_boss(rows):
    for r in rows:
        assert r.stalker_seconds_to_kill() == pytest.approx(r.boss_seconds_to_kill, rel=1e-9), r.tier


def test_at_its_damage_multiplier_it_kills_the_player_as_fast_as_the_boss_counted_blow_by_blow(rows):
    """An independent path: `damage.hits_to_kill` walks the blows one at a time, so it agrees with the multiplier's
    closed form only if every layer of the player's defence is a share of the hit. One blow either way."""
    for r in rows:
        player = fight._defender(fight.reference_player(r.tier), r.tier)
        blows = dmg.hits_to_kill(dmg.Attacker(damage=r.stalker_hit * r.damage_multiplier, damage_type="Demonic"),
                                 player)
        assert abs(blows * fight.STALKER_SECONDS_BETWEEN_BLOWS - r.boss_seconds_to_kill_the_player) \
            <= fight.STALKER_SECONDS_BETWEEN_BLOWS + 1e-9, r.tier


def test_its_potions_are_counted(rows):
    """Ruled: drinks lower the health it needs. Every tier's fight is long enough for at least one."""
    for r in rows:
        assert r.drinks >= 1, r.tier
        assert r.drinks <= fight.POTION_SLOTS * fight.POTION_MAX_CHARGES // fight.POTION_CHARGES_PER_DRINK
        without = r.boss_seconds_to_kill * r.player_dps_on_stalker / r.stalker_built_health
        assert r.health_multiplier < without, r.tier


def test_the_ruled_tables_are_the_model_fitted_to_the_games_measured_build():
    """Ruled 2026-10-26: the tables are `fitted_to_the_game` to two decimals at every tier."""
    for tier in TIERS:
        game = fight.fitted_to_the_game(tier)
        assert round(game.health_multiplier, 2) == fight.HEALTH_MULTIPLIERS[tier - 1], tier
        assert round(game.damage_multiplier, 2) == fight.DAMAGE_MULTIPLIERS[tier - 1], tier


def test_the_ruled_tables_are_the_games():
    """The game's two tables, read out of its source, are this model's."""
    source = EFFECTS_CPP.read_text(encoding="utf-8")
    for name, model in (("CorruptedStalkerHealthMultipliers", fight.HEALTH_MULTIPLIERS),
                        ("CorruptedStalkerDamageMultipliers", fight.DAMAGE_MULTIPLIERS)):
        found = re.findall(r"constexpr float " + name + r"\[8\] = \{([^}]*)\};", source)
        assert len(found) == 1, name
        assert tuple(float(v.strip().rstrip("f")) for v in found[0].split(",")) == model, name


def test_the_models_stalker_is_the_games_measured_one(rows):
    """Its built health within 2.5% and its blow within 1.5% of what window w60 measured, at every tier: measured on
    2026-10-26 as 1.7% to 2.2% above for health and 0.7% to 1.4% below for the blow, neither cause measured. Until
    2026-10-26 its blow was the reference player's hit and a third short; `fight.stalker_hit` says what was missing."""
    for r in rows:
        assert r.stalker_built_health == pytest.approx(fight.GAME_BUILT_HEALTH[r.tier - 1], rel=0.025), r.tier
        assert r.stalker_hit == pytest.approx(fight.GAME_BLOW[r.tier - 1], rel=0.015), r.tier


def _affix_row(affix: af.StatAffix) -> str:
    return "Stat_" + affix.name.replace(" ", "_")


def test_the_model_wears_the_games_demonic_entry():
    """The pieces here are a copy of `DemonicEntry` in the game's source, and are read back out of it."""
    source = STALKER_CPP.read_text(encoding="utf-8")
    names = dict(re.findall(r'const TCHAR\* const (\w+) = TEXT\("(\w+)"\);', source))
    pieces = re.findall(r'StalkerPiece\(Slot::\w+, TEXT\("(\w+)"\), (\w+), (\w+)\)', source)
    assert len(pieces) == 18, pieces
    game = [(base.split("_", 1)[1], (names[first], names[second])) for base, first, second in pieces]
    model = [(base, tuple(_affix_row(a) for a in pair)) for base, pair in fight.DEMONIC_PIECES]
    assert model == game
