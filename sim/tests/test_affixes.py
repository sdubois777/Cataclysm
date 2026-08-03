"""Tests for the resistance affix families."""

from __future__ import annotations

import pytest

from cataclysm_sim import affixes as af


# --------------------------------------------------------------------------
# The structure the project owner specified
# --------------------------------------------------------------------------

def test_per_type_value_falls_as_a_family_covers_more():
    """Stated by the project owner: single resistance affixes have the highest
    values, hybrid lower, all-resistance the least.

    Checked as a STRICT decrease. A sorted-list comparison allows ties, so three
    families all granting the same value would pass a test named 'falls' -- and
    that is exactly the case where breadth becomes free and the whole structure
    collapses.
    """
    ordered = sorted(af.RESISTANCE_FAMILIES, key=lambda f: f.breadth)
    values = [f.value_at(7) for f in ordered]
    for wider, narrower in zip(values[1:], values, strict=False):
        assert wider < narrower, f"{values} is not strictly decreasing"


def test_total_coverage_rises_as_a_family_covers_more():
    """The other half of the trade. If a narrow family also gave more in total,
    the broad ones would be strictly worse and there would be no choice.

    Also a strict increase, for the same reason.
    """
    ordered = sorted(af.RESISTANCE_FAMILIES, key=lambda f: f.breadth)
    coverage = [f.total_coverage(7) for f in ordered]
    for wider, narrower in zip(coverage[1:], coverage, strict=False):
        assert wider > narrower, f"{coverage} is not strictly increasing"


def test_the_three_families_cover_one_two_and_all_eight():
    assert af.SINGLE_RESISTANCE.breadth == 1
    assert af.HYBRID_RESISTANCE.breadth == 2
    assert af.ALL_RESISTANCE.breadth == len(af.DAMAGE_TYPES) == 8


# --------------------------------------------------------------------------
# The crossover, which is the reason for having three families
# --------------------------------------------------------------------------

def test_a_single_resistance_affix_is_best_when_one_cataclysm_is_active():
    assert af.best_family(active_cataclysms=1) is af.SINGLE_RESISTANCE


def test_an_all_resistance_affix_is_best_when_all_eight_are_active():
    assert af.best_family(active_cataclysms=8) is af.ALL_RESISTANCE


def test_the_hybrid_family_wins_somewhere_in_the_middle():
    """If no tier preferred the hybrid, it would be a family with no purpose."""
    winners = {af.best_family(n).name for n in range(1, 9)}
    assert af.HYBRID_RESISTANCE.name in winners


def test_all_three_families_are_the_best_choice_at_some_tier():
    """The point of three rather than one. A family that never wins is dead
    content that still costs design and loot table space."""
    winners = {af.best_family(n).name for n in range(1, 9)}
    assert winners == {f.name for f in af.RESISTANCE_FAMILIES}


def test_the_efficient_family_only_ever_broadens_as_tiers_rise():
    """It should not flip back and forth. Breadth must be non-decreasing across
    the run, or the progression is noise rather than a curve."""
    breadths = [af.best_family(n).breadth for n in range(1, 9)]
    assert breadths == sorted(breadths)


# --------------------------------------------------------------------------
# The slot budget
# --------------------------------------------------------------------------

def test_capping_at_tier_eight_costs_a_reasonable_share_of_slots():
    """Reaching the cap on all eight is the hardest defensive requirement in the
    game. It should be a real cost and should not consume the whole character."""
    slots = af.slots_to_cap(af.ALL_RESISTANCE, tier=8, active_cataclysms=8)
    assert 8 <= slots <= 16, f"{slots:.1f} slots"
    assert slots / af.TOTAL_AFFIX_SLOTS < 0.25


def test_capping_at_tier_one_is_cheap():
    """Early on only one damage type is attacking, so resistance should be a
    minor tax rather than a build-defining one."""
    slots = af.slots_to_cap(af.SINGLE_RESISTANCE, tier=1, active_cataclysms=1)
    assert slots <= 5


def test_using_the_wrong_family_costs_meaningfully_more():
    """If the families were within a slot or two of each other everywhere, the
    choice would not matter and three families would be pointless."""
    at_eight_wrong = af.slots_to_cap(af.SINGLE_RESISTANCE, 8, 8)
    at_eight_right = af.slots_to_cap(af.ALL_RESISTANCE, 8, 8)
    assert at_eight_wrong > 2 * at_eight_right


def test_the_slot_total_matches_the_gear_the_design_describes():
    assert af.GEAR_PIECES == 18
    assert af.AFFIX_SLOTS_PER_PIECE == 4
    assert af.TOTAL_AFFIX_SLOTS == 72


# --------------------------------------------------------------------------
# Wasted coverage, which is what drives the crossover
# --------------------------------------------------------------------------

def test_breadth_beyond_the_active_cataclysms_is_wasted():
    """An all-resistance affix covers eight damage types. With two attacking,
    six of those are worth nothing. This is the whole mechanism."""
    with_two = af.ALL_RESISTANCE.useful_coverage(7, active_cataclysms=2)
    with_eight = af.ALL_RESISTANCE.useful_coverage(7, active_cataclysms=8)
    assert with_two == pytest.approx(with_eight / 4)


def test_a_narrow_family_wastes_nothing_even_at_high_tiers():
    single = af.SINGLE_RESISTANCE
    assert (single.useful_coverage(7, 1)
            == single.useful_coverage(7, 8)
            == single.value_at(7))


def test_useful_coverage_is_zero_with_nothing_attacking():
    assert af.ALL_RESISTANCE.useful_coverage(7, active_cataclysms=0) == 0.0


# --------------------------------------------------------------------------
# Affix tiers
# --------------------------------------------------------------------------

def test_there_are_seven_affix_tiers():
    """The crafting material that levels an affix raises it 'up to t7'."""
    assert af.AFFIX_TIERS == (1, 2, 3, 4, 5, 6, 7)
    assert set(af.TIER_FRACTIONS) == set(af.AFFIX_TIERS)


def test_a_perfect_roll_at_tier_seven_is_the_family_top_value():
    for family in af.RESISTANCE_FAMILIES:
        assert family.value_at(7, roll=1.0) == pytest.approx(family.top_value)


# --------------------------------------------------------------------------
# Every tier is a range, which is what the crafting system acts on
# --------------------------------------------------------------------------

def test_every_tier_is_a_range_not_a_single_value():
    """Two crafting materials do nothing without this. The Corrupted Mote
    rerolls an affix value, and the Primal Spark perfects rolls on gear.
    Perfecting is meaningless if a tier has one value."""
    for family in af.RESISTANCE_FAMILIES:
        for tier in af.AFFIX_TIERS:
            low, high = family.range_at(tier)
            assert high > low, f"{family.name} T{tier} is a point, not a range"


def test_a_roll_moves_the_value_within_its_band():
    family = af.SINGLE_RESISTANCE
    low, high = family.range_at(5)
    assert family.value_at(5, roll=0.0) == pytest.approx(low)
    assert family.value_at(5, roll=1.0) == pytest.approx(high)
    assert low < family.average_at(5) < high


def test_a_roll_outside_zero_to_one_is_clamped_rather_than_extrapolated():
    """A bad caller must not produce an affix outside its own tier."""
    family = af.SINGLE_RESISTANCE
    low, high = family.range_at(5)
    assert family.value_at(5, roll=-3.0) == pytest.approx(low)
    assert family.value_at(5, roll=9.0) == pytest.approx(high)


def test_a_perfect_roll_can_beat_the_tier_above_but_never_two_above():
    """Bands overlap between adjacent tiers deliberately, because that is the
    only way a roll can matter with seven tiers: a band large enough to change a
    build is necessarily larger than the gap between tiers.

    The overlap is bounded to one tier, which makes a perfect roll worth chasing
    without making tier meaningless. If it reached two tiers, a lucky T5 could
    beat an unlucky T7 and the crafting action that raises a tier would stop
    being worth using.
    """
    for family in af.RESISTANCE_FAMILIES:
        for tier in range(3, 8):
            two_below_best, = (family.range_at(tier - 2)[1],)
            this_worst, _ = family.range_at(tier)
            assert this_worst > two_below_best, (
                f"{family.name}: a perfect T{tier - 2} ({two_below_best:.2f}) "
                f"beats a worst T{tier} ({this_worst:.2f})")


def test_the_bands_do_overlap_between_adjacent_tiers():
    """Asserted rather than merely allowed. If a future change quietly removed
    the overlap, rolls would go back to being worth about one slot in 72, which
    is the state this replaced."""
    family = af.SINGLE_RESISTANCE
    seventh_worst, _ = family.range_at(7)
    _, sixth_best = family.range_at(6)
    assert sixth_best > seventh_worst


def test_the_lowest_tier_cannot_roll_to_nothing():
    """An affix that can roll to zero is a wasted slot rather than a weak one."""
    for family in af.RESISTANCE_FAMILIES:
        low, _ = family.range_at(1)
        assert low > 0.0, f"{family.name} T1 can roll to zero"


def test_crafting_a_perfect_set_saves_several_slots_not_a_fraction_of_one():
    """The project owner's test of whether the range is worth having: a roll has
    to change how many affixes it takes to cap resistances. An earlier version
    saved about one slot out of 72, which nobody would craft for."""
    perfect = af.slots_to_cap(af.ALL_RESISTANCE, 8, 8, roll=1.0)
    minimum = af.slots_to_cap(af.ALL_RESISTANCE, 8, 8, roll=0.0)
    assert minimum - perfect >= 3.0, (
        f"perfect rolls save only {minimum - perfect:.1f} slots")


def test_a_minimum_roll_is_worth_three_quarters_of_a_perfect_one():
    """States the band width as a property rather than leaving it in a constant,
    so a change to it is deliberate."""
    for family in af.RESISTANCE_FAMILIES:
        for tier in af.AFFIX_TIERS:
            low, high = family.range_at(tier)
            assert low / high == pytest.approx(0.75)


def test_affix_value_rises_with_every_tier():
    for family in af.RESISTANCE_FAMILIES:
        values = [family.value_at(t) for t in af.AFFIX_TIERS]
        assert values == sorted(values)
        assert len(set(values)) == len(values)


def test_the_tier_curve_is_linear():
    """Every step up is worth the same as every other, so the value of one more
    upgrade never falls off.

    This is a deliberate pressure point. The game's central tension is that a day
    at the forge is a day not defending the empire, so choosing to upgrade rather
    than run a dungeon has to stay uncomfortable for the whole run. A
    front-loaded curve, which an earlier version used, hands over most of an
    affix's value early and makes the later tiers easy to skip.
    """
    for tier in af.AFFIX_TIERS:
        assert af.TIER_FRACTIONS[tier] == pytest.approx(tier / 7.0)


def test_every_tier_step_is_worth_the_same():
    """The property that keeps the upgrade decision uncomfortable. Stated
    separately from the formula above, because a future curve could satisfy the
    endpoints and still sag in the middle."""
    steps = [af.TIER_FRACTIONS[t + 1] - af.TIER_FRACTIONS[t]
             for t in range(1, 7)]
    assert max(steps) == pytest.approx(min(steps)), f"uneven steps: {steps}"


def test_no_single_tier_step_hands_over_most_of_an_affix():
    """A front-loaded curve would. At seven even steps each is about 14%."""
    steps = [af.TIER_FRACTIONS[t + 1] - af.TIER_FRACTIONS[t]
             for t in range(1, 7)]
    assert max(steps) < 0.20


def test_an_affix_tier_outside_one_to_seven_is_rejected():
    for bad in (0, 8, -1):
        with pytest.raises(ValueError, match="affix tier"):
            af.SINGLE_RESISTANCE.value_at(bad)


# --------------------------------------------------------------------------
# Against the design
# --------------------------------------------------------------------------

def test_there_is_one_resistance_per_damage_type():
    from cataclysm_sim.character import DAMAGE_TYPES as SHEET_TYPES
    assert af.DAMAGE_TYPES == SHEET_TYPES


def test_the_cap_matches_the_character_sheet():
    from cataclysm_sim.character import SOFT_CAPS
    assert af.RESISTANCE_CAP == SOFT_CAPS["resistance_war"] == 70.0


# --------------------------------------------------------------------------
# Health and damage affixes
# --------------------------------------------------------------------------

def test_health_and_damage_come_in_a_flat_and_an_increased_kind():
    for pair in (af.HEALTH_AFFIXES, af.DAMAGE_AFFIXES):
        kinds = {a.kind for a in pair}
        assert kinds == {"flat", "increased"}
        assert len({a.stat for a in pair}) == 1, "a pair must target one stat"


def test_an_unknown_affix_kind_is_rejected():
    with pytest.raises(ValueError, match="flat.*increased"):
        af.StatAffix("Bad", "max_health", "multiplicative", 10.0)


def test_stat_affixes_use_the_same_curve_and_band_as_resistance_affixes():
    """One shared curve across the whole pool. A family computing its own would
    drift the moment either constant changed."""
    for affix in af.HEALTH_AFFIXES + af.DAMAGE_AFFIXES:
        for tier in af.AFFIX_TIERS:
            assert affix.range_at(tier) == af.tier_band(affix.top_value, tier)
            low, high = affix.range_at(tier)
            assert low / high == pytest.approx(1.0 - af.ROLL_BAND_FRACTION)


def test_a_flat_affix_is_worth_more_when_the_character_has_more_increases():
    """Flat points are multiplied by every increase already on the character,
    which is why the two kinds cannot be compared by their face values."""
    flat = af.FLAT_HEALTH
    assert (flat.added_value(base_before_increases=2000, existing_increases=2.0)
            > flat.added_value(base_before_increases=2000, existing_increases=0.0))


def test_an_increased_affix_is_worth_more_when_the_character_has_more_base():
    inc = af.INCREASED_HEALTH
    assert (inc.added_value(base_before_increases=6000, existing_increases=2.0)
            > inc.added_value(base_before_increases=2000, existing_increases=2.0))


def test_an_increased_affix_is_worth_nothing_with_no_base_to_scale():
    """The same rule the character sheet already states: attributes and
    increases scale a value that came from somewhere else."""
    assert af.INCREASED_HEALTH.added_value(0.0, 2.0) == 0.0
    assert af.FLAT_HEALTH.added_value(0.0, 2.0) > 0.0


def test_flat_wins_early_and_increased_wins_late():
    """The crossover is the whole reason for having two kinds. If one always
    won, the other would be dead content."""
    for pair in (af.HEALTH_AFFIXES, af.DAMAGE_AFFIXES):
        crossover = af.crossover_base(pair, existing_increases=2.0)
        below = af.better_kind(pair, crossover * 0.5, 2.0)
        above = af.better_kind(pair, crossover * 2.0, 2.0)
        assert below.kind == "flat"
        assert above.kind == "increased"


def test_the_health_crossover_lands_inside_a_real_build():
    """Not at either end. A level 100 Ravager has 2,110 base health, so a
    crossover far below that would make flat health pointless, and one far above
    it would make increased health pointless."""
    from cataclysm_sim.classes import DEMONIC_CLASSES
    base = DEMONIC_CLASSES["Ravager"].base_at("max_health", 100)
    crossover = af.crossover_base(af.HEALTH_AFFIXES, existing_increases=2.0)
    assert base < crossover < base * 3


def test_the_crossover_moves_with_the_increases_already_on_the_character():
    """A character deep in Vitality wants flat health for longer, because its
    increases multiply every flat point. That is the interaction working."""
    low = af.crossover_base(af.HEALTH_AFFIXES, existing_increases=0.0)
    high = af.crossover_base(af.HEALTH_AFFIXES, existing_increases=2.0)
    assert high > low


# --------------------------------------------------------------------------
# Damage affixes are pinned to the target that makes the enemy numbers work
# --------------------------------------------------------------------------

def test_the_damage_target_comes_from_the_enemy_derivation():
    from cataclysm_sim.enemy_stats import PLAYER_DAMAGE_FACTOR
    assert PLAYER_DAMAGE_FACTOR == 0.25


def test_a_small_damage_investment_still_needs_a_weapon():
    """Solving the pipeline backwards gives what a weapon has to supply.

    Only checked for SMALL investments now. At 125% per increased affix, a
    character spending six slots each way already overshoots the current damage
    target and the requirement goes negative, which means affixes alone exceed
    it and a weapon would be pointless. That conflict is recorded by
    test_the_damage_affixes_overshoot_the_current_damage_target below rather
    than being asserted away here.
    """
    target = 6327 * 0.25
    for flat_slots, inc_slots in ((0, 0), (1, 1), (2, 2)):
        need = af.weapon_base_damage_needed(target, flat_slots, inc_slots)
        assert 0 < need <= target, (
            f"{flat_slots} flat and {inc_slots} increased slots need {need:.0f}")


def test_spending_no_slots_on_damage_needs_the_weapon_to_supply_everything():
    target = 6327 * 0.25
    assert af.weapon_base_damage_needed(target, 0, 0) == pytest.approx(target)


def test_more_damage_affixes_always_reduce_what_the_weapon_must_supply():
    target = 6327 * 0.25
    needs = [af.weapon_base_damage_needed(target, n, n) for n in range(0, 9)]
    assert needs == sorted(needs, reverse=True)
    assert len(set(needs)) == len(needs)


# --------------------------------------------------------------------------
# Slot restrictions
# --------------------------------------------------------------------------

def test_the_gear_slots_sum_to_the_pieces_the_design_describes():
    assert sum(af.GEAR_SLOTS.values()) == af.GEAR_PIECES == 18


def test_there_are_eight_rings():
    """Rings are the flexible slots, and there being eight of them is why."""
    assert af.GEAR_SLOTS["Ring"] == 8


def test_every_affix_family_is_restricted_to_some_slots():
    """Without restrictions every slot is interchangeable and gearing has no
    puzzle in it: a player fills all 72 with whatever is strongest."""
    for affix in af.HEALTH_AFFIXES + af.DAMAGE_AFFIXES:
        assert affix.allowed_slots
        assert affix.slots_available() < af.TOTAL_AFFIX_SLOTS


def test_damage_and_health_go_on_different_pieces():
    """If the two lists were the same, the restriction would only reduce the
    total and would not force any trade."""
    damage = set(af.FLAT_DAMAGE.allowed_slots)
    health = set(af.FLAT_HEALTH.allowed_slots)
    assert damage != health
    assert damage - health, "damage has no slot of its own"
    assert health - damage, "health has no slot of its own"


def test_rings_take_every_kind_of_affix():
    """The flexible slots a build uses to fix whatever it is short of."""
    for allowed in (af.OFFENSIVE_SLOTS, af.DEFENSIVE_SLOTS, af.RESISTANCE_SLOTS):
        assert "Ring" in allowed


def test_a_weapon_cannot_carry_health_or_resistance():
    """Armour and jewellery defend. A weapon does not."""
    assert "Weapon" not in af.DEFENSIVE_SLOTS
    assert "Weapon" not in af.RESISTANCE_SLOTS
    assert "Weapon" in af.OFFENSIVE_SLOTS


def test_slots_available_counts_pieces_times_slots_per_piece():
    assert af.slots_available_to(frozenset({"Ring"})) == 8 * af.AFFIX_SLOTS_PER_PIECE
    assert af.slots_available_to(frozenset({"Head"})) == af.AFFIX_SLOTS_PER_PIECE
    assert af.slots_available_to(frozenset()) == 0
    assert af.slots_available_to(frozenset(af.GEAR_SLOTS)) == af.TOTAL_AFFIX_SLOTS


def test_an_affix_allowing_a_slot_that_does_not_exist_is_rejected():
    with pytest.raises(ValueError, match="slots that do not exist"):
        af.StatAffix("Bad", "max_health", "flat", 10.0, frozenset({"Cape"}))


def test_resistance_can_go_almost_anywhere_and_damage_cannot():
    """Capping eight resistances is the hardest defensive requirement, so it
    should not also be the most slot-restricted."""
    assert af.slots_available_to(af.RESISTANCE_SLOTS) > af.slots_available_to(
        af.OFFENSIVE_SLOTS)


# --------------------------------------------------------------------------
# A known inconsistency, recorded rather than hidden
# --------------------------------------------------------------------------

def test_increased_damage_is_the_value_the_project_owner_set():
    assert af.INCREASED_DAMAGE.top_value == 125.0


def test_the_damage_affixes_overshoot_the_current_damage_target():
    """RECORDS A CONFLICT rather than asserting a desired state.

    Increased damage at 125% per affix is eight times what the earlier proposal
    used. `enemy_stats.PLAYER_DAMAGE_FACTOR` still says a player hits for 25% of
    their Power Score, 1,582 at tier 8, and a character spending only four flat
    and four increased slots on damage already needs a weapon supplying almost
    nothing to reach it.

    The two cannot both be right. Either the damage target rises, which means
    common enemies need more health, or the affix is smaller. The ratio the
    project owner set -- 1 to 3 player hits to kill a common enemy -- is about
    relative sizes and survives either choice.

    This test fails as soon as that is resolved, which is intended: it is here so
    the conflict cannot be forgotten.
    """
    target = 6327 * 0.25
    need = af.weapon_base_damage_needed(target, flat_slots=4, increased_slots=4)
    assert need < 100, (
        f"a modest damage build still needs a weapon supplying {need:.0f}, so "
        "the conflict this test records may have been resolved")
