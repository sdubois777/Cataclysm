"""The measurement behind the issue #2853 drop supply walks the model's own drops.

`sim/measure_drop_supply.py` re-implements the item roll and the kill so the levers that put back the supply before #2853
can reach inside them. Its figures are only worth quoting if, with no lever pulled, it rolls exactly what `loot` rolls;
these are the checks that say so, and the checks on what it states about the gem curve and the band. The measurement
itself is too slow for the suite and is run by hand.
"""

import random

import pytest

import measure_drop_supply as d
from cataclysm_sim import affixes as af
from cataclysm_sim import loot, player_power


@pytest.mark.parametrize("tier", [1, 2, 8])
@pytest.mark.parametrize("seed", range(6))
@pytest.mark.parametrize("slot", ["Chest", "Ring", "Weapon"])
def test_the_item_roll_is_the_models_own_when_no_lever_is_pulled(tier, seed, slot):
    ours = d.roll_item(slot, tier, 300.0, random.Random(seed))
    theirs = loot.roll_item(slot, tier, 300.0, random.Random(seed))
    assert ours == theirs


def test_the_lever_puts_the_all_resistances_family_back_on_a_tier_one_drop_and_nothing_else():
    rng = random.Random(3)
    seen = 0
    for _ in range(600):
        seen += sum(rolled.affix is af.ALL_RESISTANCE
                    for rolled in d.roll_item("Chest", 1, 300.0, rng, all_resistance_at_tier_one=True).affixes)
    assert seen > 0, "the lever did not put the family back"
    for seed in range(10):
        assert d.roll_item("Chest", 2, 300.0, random.Random(seed), all_resistance_at_tier_one=True) == loot.roll_item(
            "Chest", 2, 300.0, random.Random(seed)), "the lever changed a tier 2 drop"


def test_one_floor_walked_as_shipped_is_the_models_own_kills():
    """Same seed, same floor: the walk's kill against `loot.roll_drops_from_kill` and `loot.roll_gem_drops_from_kill`
    called directly, in the order a kill rolls them."""
    ours = d.Walker(d.SHIPPED)
    d.walk(ours, 2, random.Random(5), gap_rule=False)

    theirs = d.Walker(d.SHIPPED)
    rng = random.Random(5)
    for floor in range(1, 3):
        enemy = d.scoring.enemy_scores(1, d.DUNGEON_TYPE, d.DUNGEON_SUBTYPE, 2, floor)
        for _ in range(round(d.curve.POPULATION)):
            rarity = d.spawn_rarity(rng)
            theirs.gain(round(enemy[rarity]))
            for item in loot.roll_drops_from_kill(rarity, 1, 0.0, 100.0, rng):
                theirs.wear_if_better(item)
            theirs.gems.extend(loot.rarity_index(g) for g in loot.roll_gem_drops_from_kill(rarity, 1, 0.0, 100.0, rng))
    theirs.gain(round(d.scoring.final_boss_score(1, d.DUNGEON_TYPE, d.DUNGEON_SUBTYPE, 2)))
    for item in loot.roll_drops_from_kill("Boss", 1, 0.0, 100.0, rng):
        theirs.wear_if_better(item)
    theirs.gems.extend(loot.rarity_index(g) for g in loot.roll_gem_drops_from_kill("Boss", 1, 0.0, 100.0, rng))

    assert ours.character() == theirs.character()
    assert ours.character().gear, "nothing was worn, so this compared two empty characters"


def test_the_supply_before_puts_back_the_old_gear_column_and_tier_eights_gem_rate():
    for rarity, figure in {"Common": 0.16, "Elite": 0.5, "Legendary": 1.0, "Herald": 2.0, "Boss": 5.0,
                           "Cataclysm Boss": 12.0}.items():
        assert loot.expected_gear_drops(rarity) * d.BEFORE.gear == pytest.approx(figure)
    for tier in range(1, 9):
        assert loot.gem_drop_share(tier) * d.BEFORE.gems_factor(tier) == pytest.approx(1.0)
        assert d.SHIPPED.gems_factor(tier) == 1.0
    assert d.BEFORE.rule.below_at() == 0.6 and d.SHIPPED.rule is d.gap_rewards.RULED


def test_a_walked_character_is_scored_by_the_model():
    c = d.Walker(d.SHIPPED)
    d.walk(c, 1, random.Random(7), gap_rule=False)
    character = c.character()
    assert character.gear
    assert sum(d.terms(character)) == pytest.approx(player_power.level_term(character) + player_power.gear_term(
        character) + player_power.gem_term(character) + player_power.resistance_term(character))
    assert all(rarity <= loot.rarity_index(loot.highest_droppable_rarity(1)) for rarity in
               (piece.rarity for piece in character.gear))


@pytest.mark.parametrize("name", [name for name, _ in d.GEM_CURVES])
def test_every_gem_curve_measured_keeps_every_tier_above_its_need(name):
    share = dict(d.GEM_CURVES)[name]
    for tier in range(1, 9):
        assert d.carried(share, tier) >= d.need(tier), (name, tier)


def test_the_shipped_curve_is_the_first_measured_and_less_steep_than_halving_at_every_step():
    name, share = d.GEM_CURVES[0]
    assert share is loot.gem_drop_share and "shipped" in name
    assert all(share(tier + 1) / share(tier) < 2.0 for tier in range(1, 8))
    eighths = dict(d.GEM_CURVES)["eighths, tier / 8"]
    assert eighths(2) / eighths(1) == 2.0, "the even split is as steep as halving between tiers 1 and 2"


def test_the_gem_walk_reads_the_curve_at_the_tier():
    """Tier 1 under the shipped curve drops about 0.13 of what tier 1 dropped before, over the same walk."""
    shipped = [d.gems_walked(loot.gem_drop_share, 1, seed)[-1] for seed in range(4)]
    before = [d.gems_walked(lambda tier: 1.0, 1, seed)[-1] for seed in range(4)]
    assert sum(shipped) / sum(before) == pytest.approx(loot.gem_drop_share(1), abs=0.04)


@pytest.mark.parametrize("tier", range(1, 9))
def test_the_ruled_band_holds_the_curves_character_and_still_cuts_the_next_tiers(tier):
    assert d.gap_rewards.RULED.below_at(tier) >= d.above_lowest(tier, d.curve_character_score(tier)) + 0.1 - 1e-9
    if tier < 8:
        assert d.next_tiers_character_factor(tier, d.gap_rewards.RULED) <= 0.2
