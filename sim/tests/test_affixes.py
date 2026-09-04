"""Tests for the affix pool: resistance, health and damage."""

from __future__ import annotations

import pytest

from cataclysm_sim import affixes as af
from cataclysm_sim import enemy_stats as es


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
    so a change to it is deliberate.

    ON AN AFFIX WITH NO STATED FLOOR, which is what this asserted of every
    affix until issue #1230. A floor lifts the bottom of the whole range
    without moving the top, so it narrows every band; the test below says
    what a floored one does instead.
    """
    unfloored = [a for a in af.AFFIX_POOL if not a.floor]
    assert unfloored, "every affix now states a floor, so nothing is checked"
    for affix in unfloored:
        for tier in af.AFFIX_TIERS:
            low, high = affix.range_at(tier)
            assert low / high == pytest.approx(1.0 - af.ROLL_BAND_FRACTION)


def test_a_stated_floor_narrows_the_band_and_never_widens_it():
    """What a floor does to a tier's range, said as a property.

    The floor is added and the ladder runs across what is left, so the top
    of every band is unchanged and the bottom comes up. A band that got
    WIDER would mean the map was not monotone, and
    ``BandsOverlapByExactlyOneTier`` rests on it being monotone.
    """
    floored = [a for a in af.AFFIX_POOL if a.floor]
    assert floored, "no affix states a floor, so nothing is checked"
    for affix in floored:
        for tier in af.AFFIX_TIERS:
            low, high = affix.range_at(tier)
            assert low / high > 1.0 - af.ROLL_BAND_FRACTION
            assert low < high


def test_a_floor_is_the_worst_a_player_can_be_handed():
    """Tier 1, the bottom of the band, on an un-upgraded piece."""
    for affix in af.AFFIX_POOL:
        if not affix.floor:
            continue
        assert affix.value_at(1, roll=0.0, gear_level=0) == pytest.approx(
            affix.floor), affix.name
    for family in af.RESISTANCE_FAMILIES:
        if not family.floor:
            continue
        assert family.value_at(1, roll=0.0, gear_level=0) == pytest.approx(
            family.floor), family.name


def test_a_stated_floor_does_not_move_the_top():
    """The whole point of the shape chosen for issue #1230: no ceiling moves."""
    for affix in af.AFFIX_POOL:
        assert affix.value_at(7, roll=1.0,
                              gear_level=af.MAX_GEAR_LEVEL) == pytest.approx(
            affix.top_value), affix.name
    for family in af.RESISTANCE_FAMILIES:
        assert family.value_at(7, roll=1.0,
                               gear_level=af.MAX_GEAR_LEVEL) == pytest.approx(
            family.top_value), family.name


def test_an_affix_with_no_floor_is_unchanged_by_the_floor_arithmetic():
    """A floor of zero derives the same tenth the three ladders always gave,
    and the remap is the identity at exactly that value. So this is one piece
    of arithmetic reaching the same answer rather than a branch that could rot.
    """
    for tier in af.AFFIX_TIERS:
        for roll in (0.0, 0.5, 1.0):
            for gear in (0, 5, af.MAX_GEAR_LEVEL):
                low, high = af.tier_band(120.0, tier)
                raw = (af.roll_within(low, high, roll)
                       / af.gear_level_multiplier(af.MAX_GEAR_LEVEL)
                       * af.gear_level_multiplier(gear))
                assert af.affix_value(120.0, tier, roll, gear) == \
                    pytest.approx(raw)


def test_affix_value_rises_with_every_tier():
    for family in af.RESISTANCE_FAMILIES:
        values = [family.value_at(t) for t in af.AFFIX_TIERS]
        assert values == sorted(values)
        assert len(set(values)) == len(values)


def test_the_tier_curve_is_geometric():
    """A constant RATIO between tiers, which replaced a constant DIFFERENCE on
    2026-09-03 for issue #1179.

    The ladder ran `tier / 7`, so tier 7 was worth seven times tier 1. It now
    spans 3.0, because that ladder multiplied by the gear level ladder put a
    starting affix at 3.04% of the endgame value. Both ladders were eased rather
    than either removed; see TIER_LADDER_SPAN.

    GEOMETRIC RATHER THAN A FLATTER LINEAR LADDER, and that is not a taste.
    Squashing a linear ladder to span 3.0 lets a perfect tier 5 roll beat a poor
    tier 7 one, which is a two-tier overlap that
    `Cataclysm.Item.BandsOverlapByExactlyOneTier` forbids. A constant ratio
    keeps the bound at every tier, which the test below states directly.
    """
    for tier in af.AFFIX_TIERS:
        expected = af.TIER_LADDER_SPAN ** ((tier - 7) / 6)
        assert af.TIER_FRACTIONS[tier] == pytest.approx(expected)

    assert af.TIER_FRACTIONS[7] == pytest.approx(1.0)
    assert (af.TIER_FRACTIONS[7] / af.TIER_FRACTIONS[1]
            == pytest.approx(af.TIER_LADDER_SPAN))


def test_every_tier_step_is_worth_the_same_share_of_the_tier_below():
    """The property that keeps the upgrade decision uncomfortable, restated for
    a geometric ladder.

    It used to be a constant DIFFERENCE: every step added the same fraction of
    the affix. It is now a constant RATIO: every step multiplies by 1.2009. The
    pressure the old test protected is unchanged and is stronger -- see the test
    below, which is why.

    Stated separately from the formula above, because a future curve could
    satisfy the endpoints and still sag in the middle.
    """
    ratios = [af.TIER_FRACTIONS[t + 1] / af.TIER_FRACTIONS[t]
              for t in range(1, 7)]
    assert max(ratios) == pytest.approx(min(ratios)), f"uneven ratios: {ratios}"
    assert max(ratios) == pytest.approx(af.TIER_LADDER_SPAN ** (1 / 6))


def test_the_tier_curve_is_back_loaded_rather_than_front_loaded():
    """WHAT THE LINEAR LADDER WAS PROTECTING, and a geometric one protects it
    harder.

    The reasoning recorded on TIER_FRACTIONS rejects a front-loaded curve
    because it "hands over most of an affix's value early and makes the later
    tiers easy to skip", and the forge-versus-dungeon decision has to stay
    uncomfortable for the whole run.

    A geometric ladder is back-loaded: the step from tier 6 to tier 7 is worth
    more than the step from tier 1 to tier 2, so the last tiers are the ones
    that matter most and the hardest to skip.
    """
    steps = [af.TIER_FRACTIONS[t + 1] - af.TIER_FRACTIONS[t]
             for t in range(1, 7)]
    assert steps == sorted(steps), f"the steps do not grow: {steps}"
    assert steps[-1] > steps[0] * 2.0, (
        f"the last step {steps[-1]:.4f} is not clearly larger than the first "
        f"{steps[0]:.4f}, so the ladder is no longer back-loaded")


def test_a_perfect_roll_can_beat_the_tier_above_but_never_two_tiers_above():
    """THE BOUND THAT DECIDED THE LADDER'S SHAPE. Issue #1179.

    `ROLL_BAND_FRACTION` records that the one-tier overlap is deliberate -- it
    is what the reroll and perfect crafting materials act on -- and that it is
    bounded to exactly one tier. `Cataclysm.Item.BandsOverlapByExactlyOneTier`
    asserts the same thing in the engine.

    Easing the ladder narrows the gaps between tiers, so the bound had to be
    rechecked rather than assumed. A LINEAR ladder squashed to span 3.0 breaks
    it: a perfect tier 5 beats a poor tier 7. A geometric one holds, because the
    ratio between tiers stays constant while the band stays a fixed share.
    """
    for tier in af.AFFIX_TIERS:
        low, high = af.tier_band(1.0, tier)

        if tier >= 3:
            two_below_high = af.tier_band(1.0, tier - 2)[1]
            assert two_below_high < low, (
                f"a perfect T{tier - 2} roll ({two_below_high:.4f}) beats the "
                f"worst T{tier} roll ({low:.4f}), so the overlap now spans two "
                f"tiers. See ROLL_BAND_FRACTION.")

    # And the one-tier overlap really is there, so rerolling is worth doing.
    overlaps = [t for t in af.AFFIX_TIERS[1:]
                if af.tier_band(1.0, t - 1)[1] > af.tier_band(1.0, t)[0]]
    assert overlaps, (
        "no perfect roll can beat the tier above anywhere, so the Corrupted "
        "Mote and Primal Spark crafting materials have nothing to act on")


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
    drift the moment either constant changed.

    ASKED OF THE TWO CLASSES DIRECTLY SINCE ISSUE #1230, rather than of a
    StatAffix against the raw band. Both now run their range through the same
    floor arithmetic, so comparing one of them against the unfloored ladder
    would only say that this particular affix states no floor.
    """
    for affix in af.HEALTH_AFFIXES + af.DAMAGE_AFFIXES:
        twin = af.AffixFamily(affix.name, breadth=1,
                              top_value=affix.top_value, floor=affix.floor)
        for tier in af.AFFIX_TIERS:
            assert affix.range_at(tier) == pytest.approx(twin.range_at(tier))
            low, high = affix.range_at(tier)
            if not affix.floor:
                assert low / high == pytest.approx(
                    1.0 - af.ROLL_BAND_FRACTION)


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

def test_the_damage_target_is_read_off_the_enemy_stats():
    """It is an OUTPUT of the enemy design, not an input to it.

    An earlier version imported a player damage figure that had been derived
    backwards from player-side targets, and that figure is what made the
    project owner's 125% increased damage affix impossible to fit.

    CHECKED AGAINST `enemy_stats`, NOT AGAINST A RESTATEMENT OF THE BODY. This
    used to assert `effective_health / HITS_TO_KILL_A_COMMON_ENEMY`, which is
    what the function did, so it could not catch the function doing the wrong
    thing -- and it did not: issue #511 was that the whole mitigation order was
    missing here. `player_damage_to_kill_in` is where that order lives, so this
    now checks the two files agree rather than checking one file agrees with
    itself.
    """
    common = es.stats_on_floor("Common", 8, "Cataclysm")
    assert af.damage_target(8) == pytest.approx(
        es.player_damage_to_kill_in(common, af.HITS_TO_KILL_A_COMMON_ENEMY))


def test_the_damage_target_goes_through_the_enemys_mitigation():
    """ISSUE #511. It divided health by hits and applied nothing, so it answered
    how much HEALTH had to be removed rather than how much DAMAGE had to be dealt
    to remove it. The average Common enemy carries 673 armour at tier 8, which
    stops 9.52% of a hit, so the figure was 10.5% low.

    Asserted as a strict inequality against the unmitigated figure, and as the
    arithmetic that produces the difference, so it fails if either the mitigation
    or the armour goes away.
    """
    common = es.stats_on_floor("Common", 8, "Cataclysm")
    unmitigated = common.effective_health / af.HITS_TO_KILL_A_COMMON_ENEMY

    assert common.armor > 0.0
    assert common.damage_taken_fraction() < 1.0
    assert af.damage_target(8) > unmitigated
    assert af.damage_target(8) == pytest.approx(
        unmitigated / common.damage_taken_fraction())


def test_the_target_tracks_enemy_health_rather_than_being_a_constant():
    """If the enemy numbers move, this must move with them. A hard-coded figure
    here is exactly the coupling that produced the conflict before."""
    assert af.damage_target(8) > 5 * af.damage_target(1)


def test_a_common_enemy_dies_in_the_range_the_project_owner_set():
    """One to three non-critical hits. The named Common enemies are checked as
    well as the average, because the average is not a creature anyone fights.

    COUNTED THROUGH EACH CREATURE'S OWN MITIGATION since issue #511. Dividing
    health by the target ignores the armour and evasion that creature has, which
    is the same mistake the target itself carried. The Imp and the Hellhound both
    evade, so this moves them: 0.6 and 1.4 swings become 0.8 and 1.9.
    """
    target = af.damage_target(8)
    for kind in ("Imp", "Hellhound"):
        e = es.stats_on_floor("Common", 8, "Cataclysm", kind=kind)
        hits = e.effective_health / (target * e.damage_taken_fraction())
        assert 0 < hits <= 3.0, f"a Common {kind} takes {hits:.1f} hits"


def test_the_rarest_enemy_is_a_long_fight_and_the_commonest_is_not():
    """Whatever the exact figures, the ordering has to survive."""
    target = af.damage_target(8)
    imp = es.stats_on_floor("Common", 8, "Cataclysm", kind="Imp")
    boss = es.stats_on_floor("Cataclysm Boss", 8, "Cataclysm", kind="Gatekeeper")
    assert boss.effective_health / target > 50 * (imp.effective_health / target)


def test_a_moderate_damage_build_still_needs_a_weapon():
    """The affixes must not be able to reach the target on their own at ordinary
    investment, or the weapon is decoration.

    Six slots each way is the reference build. It is not a cap: heavier
    investment overshoots the target, which is what heavy investment is for.
    """
    target = af.damage_target(8)
    for flat_slots, inc_slots in ((0, 0), (2, 2), (4, 4), (6, 6), (6, 8)):
        need = af.weapon_base_damage_needed(target, flat_slots, inc_slots)
        assert 0 < need <= target, (
            f"{flat_slots} flat and {inc_slots} increased slots need {need:.0f}")


def test_the_weapon_supplies_a_real_share_at_the_reference_build():
    """At six slots each way the weapon and skill together should carry a
    substantial part of the base, not a rounding error.

    This is what the flat damage value was set to achieve. At its previous value
    of 60 the flat affixes alone filled the whole base bracket and this fails.
    """
    target = af.damage_target(8)
    from_weapon = af.weapon_base_damage_needed(target, 6, 6)
    from_flat = af.FLAT_DAMAGE.value_at(7) * 6
    assert from_weapon > from_flat * 0.5, (
        f"weapon supplies {from_weapon:.0f} against {from_flat:.0f} from affixes")


def test_spending_no_slots_on_damage_needs_the_weapon_to_supply_everything():
    target = af.damage_target(8)
    assert af.weapon_base_damage_needed(target, 0, 0) == pytest.approx(target)


def test_more_damage_affixes_always_reduce_what_the_weapon_must_supply():
    target = af.damage_target(8)
    needs = [af.weapon_base_damage_needed(target, n, n) for n in range(0, 9)]
    # strict=False is required: the two lists differ in length by one, because
    # this pairs each investment level with the next.
    for bigger, smaller in zip(needs, needs[1:], strict=False):
        assert smaller < bigger, f"not strictly falling: {needs}"


def test_a_damage_build_crosses_from_flat_to_increased_partway_through():
    """Both kinds must get used. If the crossover sat outside the base a real
    character reaches, one kind would always win and the other would be dead
    content.

    Walked one flat affix at a time, which is how a build is actually assembled,
    rather than comparing the crossover to a range. At the previous flat damage
    value of 60 the crossover is 528 and no build reaches it, so flat wins
    always; at 12 it is 106, below where a build starts, so increased wins
    always. Both make this fail.
    """
    increases = (af.INCREASED_DAMAGE.value_at(7) / 100.0
                 * af.REFERENCE_INCREASED_DAMAGE_AFFIXES)
    # The weapon is a FIXED quantity a player adds affixes on top of. Starting
    # from the whole base a target implies would put the character at the target
    # before buying anything, which is not how a build is assembled.
    weapon = af.reference_weapon_base(8)

    winners = []
    for flat_affixes in range(0, 9):
        base = weapon + af.FLAT_DAMAGE.value_at(7) * flat_affixes
        winners.append(af.better_kind(af.DAMAGE_AFFIXES, base, increases).kind)

    assert winners[0] == "flat", (
        f"increased already wins with no flat affixes: {winners}")
    assert winners[-1] == "increased", (
        f"flat still wins after eight flat affixes: {winners}")
    # And it must switch exactly once, not oscillate.
    assert winners == sorted(winners, key=["flat", "increased"].index)


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


def test_increased_damage_is_the_value_the_project_owner_set():
    assert af.INCREASED_DAMAGE.top_value == 125.0


# A test named test_the_damage_affixes_overshoot_the_current_damage_target used
# to sit here. It recorded a conflict rather than asserting a desired state: at
# 125% per increased affix, a build spending four slots each way already exceeded
# the player damage figure then in use, so a weapon would have contributed
# nothing. It was written to fail once that was resolved, and it did.
#
# It was resolved by setting the enemy stats first and reading the damage target
# off them, and by lowering flat damage from 60 to 18. The test is deleted rather
# than adjusted, because the thing it existed to remember has happened.


# --------------------------------------------------------------------------
# Gear upgrade level multiplies every affix on the piece
# --------------------------------------------------------------------------

def test_the_gear_level_factor_is_no_longer_the_power_score_weight():
    """DECOUPLED ON 2026-09-03, and it used to be the same object. Issue #1179.

    This read `player_power.WEIGHTS["upgrade_factor"]` directly, so that the two
    could not drift. That was right while they answered the same question and
    wrong once they did not: easing the gear ladder for #1179 would have moved
    every Power Score in the game, and the Power Score is a separate model with
    its own anchors and no reason to change.

    They are two questions. How much an upgrade is WORTH to a rating is the
    Power Score's; how much it MULTIPLIES the numbers printed on the item is
    this one's. The Power Score weight is still imported, under a name that says
    what it is, so a reader can see both and see that they differ.
    """
    from cataclysm_sim import player_power as pp

    assert af.POWER_SCORE_UPGRADE_FACTOR is pp.WEIGHTS["upgrade_factor"]
    assert af.GEAR_LEVEL_FACTOR is not pp.WEIGHTS["upgrade_factor"]
    assert af.GEAR_LEVEL_FACTOR < af.POWER_SCORE_UPGRADE_FACTOR, (
        "the gear ladder was eased for #1179, so it should now be gentler than "
        "the Power Score's upgrade weight rather than equal to it")

    # The number of upgrade levels is still one fact, and is still shared.
    assert af.MAX_GEAR_LEVEL == pp.MAX_UPGRADE == 10


def test_a_fully_upgraded_piece_is_two_and_a_half_times_a_fresh_one():
    """WAS 3.5246 AND IS NOW EXACTLY 2.5. Issue #1179.

    The old figure was not a tidy number because it was derived from the Power
    Score's tier anchors. This one is chosen: 0.15 a level, ten levels, so a +10
    piece is 2.5 times a +0 one. Half of the pair of ladders the project owner
    kept, the other being TIER_LADDER_SPAN at 3.0; together about 10x, against
    32.9x before.
    """
    assert af.gear_level_multiplier(0) == pytest.approx(1.0)
    assert af.gear_level_multiplier(10) == pytest.approx(af.AFFIX_GEAR_LEVEL_SPAN)
    assert af.gear_level_multiplier(10) == pytest.approx(2.5)

    # Stated twice on purpose -- the span is what the design means and the
    # factor is what the arithmetic uses, so they must agree.
    assert (1.0 + af.GEAR_LEVEL_FACTOR * af.MAX_GEAR_LEVEL
            == pytest.approx(af.AFFIX_GEAR_LEVEL_SPAN))


def test_the_two_ladders_multiply_to_about_ten():
    """THE NUMBER THE WHOLE OF #1179 IS ABOUT, asserted where both halves are in
    scope.

    A tier 1 roll at its worst on an un-upgraded drop, against a tier 7 roll at
    its best on a fully upgraded one. It was 32.9x, which made a starting affix
    3.04% of a finished one against Last Epoch's 16.7%. Twelve affixes could not
    show more than three different numbers early in the game and three could
    only ever display 0.0.

    Neither ladder was deleted, which is the project owner's decision: two
    levers are the point, and the fault was the size of their product.
    """
    fresh = af.affix_value(120.0, 1, roll=0.0, gear_level=0)
    finished = af.affix_value(120.0, 7, roll=1.0, gear_level=af.MAX_GEAR_LEVEL)

    assert finished / fresh == pytest.approx(10.0, abs=0.05), (
        f"the two ladders now span {finished / fresh:.2f}x rather than about "
        f"10x. Tier ladder {af.TIER_LADDER_SPAN}x times gear ladder "
        f"{af.AFFIX_GEAR_LEVEL_SPAN}x, with the roll band inside the first.")

    # And the endgame value did not move, which is what made this safe to do
    # without retuning everything above it.
    assert finished == pytest.approx(120.0)


def test_a_gear_level_outside_the_designed_range_is_rejected():
    for bad in (-1, 11, 100):
        with pytest.raises(ValueError, match="outside 0-10"):
            af.gear_level_multiplier(bad)


def test_the_stated_top_values_are_what_a_fully_upgraded_piece_gives():
    """So the numbers in this module and in the design document are the ones a
    finished character reaches, and a fresh piece is a fraction of them."""
    for affix in af.HEALTH_AFFIXES + af.DAMAGE_AFFIXES:
        assert affix.value_at(7, gear_level=10) == pytest.approx(affix.top_value)
        assert affix.value_at(7) == pytest.approx(affix.top_value)


def test_an_unupgraded_piece_gives_the_stated_value_divided_by_the_multiplier():
    for affix in af.HEALTH_AFFIXES + af.DAMAGE_AFFIXES:
        assert affix.value_at(7, gear_level=0) == pytest.approx(
            affix.top_value / af.gear_level_multiplier(10))


@pytest.mark.parametrize("family", ["SINGLE_RESISTANCE", "HYBRID_RESISTANCE",
                                    "ALL_RESISTANCE"])
def test_gear_level_lifts_resistance_affixes_too(family):
    """EVERY affix on the piece, which is what the project owner chose. Not only
    the damage ones, and not only the flat ones."""
    f = getattr(af, family)
    assert f.value_at(7, gear_level=0) < f.value_at(7, gear_level=5) < \
        f.value_at(7, gear_level=10)


def test_gear_level_lifts_increased_affixes_as_well_as_flat_ones():
    """The project owner was shown that applying it to the flat bracket alone
    keeps damage growth in step with Power Score, and chose every affix anyway,
    to be tuned once the game is playable. This records which was chosen."""
    assert af.FLAT_DAMAGE.value_at(7, gear_level=0) < \
        af.FLAT_DAMAGE.value_at(7, gear_level=10)
    assert af.INCREASED_DAMAGE.value_at(7, gear_level=0) < \
        af.INCREASED_DAMAGE.value_at(7, gear_level=10)


@pytest.mark.parametrize("level", [0, 3, 7, 10])
def test_gear_level_scales_every_tier_by_the_same_proportion(level):
    """It multiplies the affix, so it must not distort the tier curve."""
    expected = af.gear_level_multiplier(level) / af.gear_level_multiplier(10)
    for tier in af.AFFIX_TIERS:
        at_level = af.FLAT_HEALTH.value_at(tier, gear_level=level)
        at_max = af.FLAT_HEALTH.value_at(tier, gear_level=10)
        assert at_level / at_max == pytest.approx(expected)


def test_gear_level_and_the_roll_are_independent():
    """A poor roll on an upgraded piece and a good roll on a fresh one are
    different things, and both have to be expressible."""
    poor_but_upgraded = af.FLAT_HEALTH.value_at(7, roll=0.0, gear_level=10)
    perfect_but_fresh = af.FLAT_HEALTH.value_at(7, roll=1.0, gear_level=0)
    assert poor_but_upgraded > perfect_but_fresh
    assert af.FLAT_HEALTH.value_at(7, roll=0.0, gear_level=0) < perfect_but_fresh


# --------------------------------------------------------------------------
# The pool, and the prefix and suffix split
# --------------------------------------------------------------------------

def test_the_pool_covers_most_of_the_character_sheet():
    """A pool of seven affixes is not a choice. This checks the pool actually
    reaches the sheet rather than only counting itself."""
    from cataclysm_sim.character import ALL_STATS
    covered = {a.stat for a in af.AFFIX_POOL} | set(af.RESISTANCE_STATS)
    assert len(af.AFFIX_POOL) >= 30
    assert len(covered & set(ALL_STATS)) / len(ALL_STATS) > 0.85


def test_no_stat_is_both_a_prefix_and_a_suffix():
    """The two are separate pools. A stat in both would let one item carry four
    of it, which is exactly what the split exists to prevent."""
    by_stat: dict[str, set[str]] = {}
    for affix in af.AFFIX_POOL:
        by_stat.setdefault(affix.stat, set()).add(affix.position)
    assert all(len(p) == 1 for p in by_stat.values()), by_stat


def test_that_separation_check_actually_fires():
    real = af.AFFIX_POOL
    af.AFFIX_POOL = real + (af.StatAffix("Straddler", "max_health", "flat",
                                         1.0, af.DEFENSIVE_SLOTS, af.SUFFIX),)
    try:
        with pytest.raises(ValueError, match="both a prefix and a suffix"):
            af._check_the_two_positions_are_separate_pools()
    finally:
        af.AFFIX_POOL = real


@pytest.mark.parametrize("slot", sorted(af.GEAR_SLOTS))
def test_every_slot_can_fill_all_four_of_its_affix_slots(slot):
    """Two prefixes and two suffixes. A slot short of either would roll
    duplicates or blanks. This is what caught Shoulders being left out of the
    defensive slot list, where it could roll nothing but resistance and shield.
    """
    assert len(af.pool_for(slot, af.PREFIX)) >= af.PREFIXES_PER_PIECE
    assert len(af.pool_for(slot, af.SUFFIX)) >= af.SUFFIXES_PER_PIECE


def test_that_coverage_check_actually_fires():
    real = af.AFFIX_POOL
    af.AFFIX_POOL = tuple(a for a in real if "Boots" not in a.allowed_slots)
    try:
        with pytest.raises(ValueError, match="slots to fill"):
            af._check_every_slot_can_fill_all_four_of_its_affixes()
    finally:
        af.AFFIX_POOL = real


def test_the_four_slots_per_piece_are_two_of_each():
    assert af.PREFIXES_PER_PIECE + af.SUFFIXES_PER_PIECE == \
        af.AFFIX_SLOTS_PER_PIECE == 4


def test_prefixes_carry_magnitude_and_suffixes_carry_rates():
    """Spot-checked against the convention, not asserted in the abstract. How
    big a number is goes in one pool; how often, how fast and how much gets
    through goes in the other."""
    positions = {a.stat: a.position for a in af.AFFIX_POOL}
    for magnitude in ("max_health", "max_mana", "max_energy_shield", "armor",
                      "evasion", "attack_damage", "spell_damage"):
        assert positions[magnitude] == af.PREFIX, magnitude
    for rate in ("attack_speed", "crit_chance", "movement_speed", "penetration",
                 "cooldown_reduction", "life_leech", "block_chance"):
        assert positions[rate] == af.SUFFIX, rate
    assert af.SINGLE_RESISTANCE.position == af.SUFFIX


def test_no_two_affixes_grant_the_same_thing():
    pairs = [(a.stat, a.kind) for a in af.AFFIX_POOL]
    assert len(pairs) == len(set(pairs))


def test_every_affix_names_a_stat_that_something_reads():
    """A typo would otherwise grant a stat nothing on the character sheet reads,
    and it would silently do nothing.

    AN ATTRIBUTE COUNTS AS READ. `character.py` keeps the eight in
    ATTRIBUTE_EFFECTS rather than in the stat groups, because an attribute holds
    no value of its own: it turns each point into increases on the two or three
    stats it drives. So an attribute name is not a stat and is still read.
    """
    from cataclysm_sim.character import ALL_STATS, ATTRIBUTE_NAMES
    readable = set(ALL_STATS) | af.OFF_SHEET_STATS | set(ATTRIBUTE_NAMES)
    for affix in af.AFFIX_POOL:
        assert affix.stat in readable, affix.name
    with pytest.raises(ValueError, match="neither on the character sheet"):
        af.StatAffix("Bad", "max_stamina", "flat", 1.0)


def test_only_stats_belonging_to_something_other_than_the_character_are_off_sheet():
    """`character.py` says attack damage belongs to the weapon rather than the
    sheet. The three minion stats were added with issue #337 for the same reason:
    they belong to the minion. Anything else off-sheet would be a stat nobody had
    designed.
    """
    assert af.OFF_SHEET_STATS == {"attack_damage", "minion_damage",
                                  "minion_health", "minion_attack_speed"}


def test_every_off_sheet_stat_is_named_by_at_least_one_affix():
    """An off-sheet stat nothing grants is a hole in the pool, not a rule."""
    named = {a.stat for a in af.AFFIX_POOL}
    missing = sorted(af.OFF_SHEET_STATS - named)
    assert not missing, (
        f"{missing} are allowed off the character sheet and no affix grants "
        "them, so nothing can ever raise them")


def test_every_attribute_has_exactly_one_affix():
    """Reversed on 2026-08-04. Gear used to grant no attributes at all; it now
    grants a percentage increase to each of the eight, and to no other."""
    from cataclysm_sim.character import ATTRIBUTE_NAMES
    granted = [a.stat for a in af.AFFIX_POOL if a.stat in set(ATTRIBUTE_NAMES)]
    assert sorted(granted) == sorted(ATTRIBUTE_NAMES)
    assert len(granted) == len(set(granted)), "an attribute has two affixes"


def test_an_attribute_affix_is_always_an_increase_and_never_flat():
    """The point of the design. Gear multiplies the attribute the character
    already has, so it is worth little when spread thin and a great deal when
    specialised. A flat version would hand everyone the same value."""
    for affix in af.ATTRIBUTE_AFFIXES:
        assert affix.kind == "increased", affix.name


def test_an_attribute_affix_is_always_a_suffix():
    for affix in af.ATTRIBUTE_AFFIXES:
        assert affix.position == af.SUFFIX, affix.name


def test_there_are_no_hybrid_attribute_affixes():
    """Decided by the project owner: one attribute per affix, never two."""
    for hybrid in af.HYBRID_AFFIXES:
        for part in hybrid.parts:
            assert part.stat not in af.ATTRIBUTE_STATS, hybrid.name


def test_an_attribute_affix_rolls_only_where_the_stats_it_drives_roll():
    """The slot lists are derived rather than chosen, which is what keeps a
    weapon offensive without needing a new rule. Ferocity drives critical strike
    and Efficacy drives area of effect, both of which already roll on a weapon.
    Vitality drives health and Constitution drives armour, which do not."""
    from cataclysm_sim.character import ATTRIBUTE_EFFECTS
    by_stat: dict[str, set[str]] = {}
    for affix in af.AFFIX_POOL:
        if affix.stat not in af.ATTRIBUTE_STATS:
            by_stat.setdefault(affix.stat, set()).update(affix.allowed_slots)
    for affix in af.ATTRIBUTE_AFFIXES:
        driven = ATTRIBUTE_EFFECTS[affix.stat]
        allowed = set().union(*(by_stat.get(s, set()) for s in driven))
        assert affix.allowed_slots == allowed, affix.name


def test_only_the_two_offensive_attributes_reach_a_weapon():
    """A consequence of the rule above, asserted directly because it is the part
    a reader is most likely to doubt."""
    on_weapon = {a.stat for a in af.ATTRIBUTE_AFFIXES
                 if "Weapon" in a.allowed_slots}
    assert on_weapon == {"ferocity", "efficacy"}


def test_an_unknown_position_is_rejected():
    with pytest.raises(ValueError, match="position must be one of"):
        af.StatAffix("Bad", "max_health", "flat", 1.0, af.DEFENSIVE_SLOTS, "infix")


def test_an_affix_that_can_appear_nowhere_is_rejected():
    with pytest.raises(ValueError, match="no slot at all"):
        af.StatAffix("Bad", "max_health", "flat", 1.0, frozenset())


def test_pool_for_filters_by_slot_and_by_position():
    weapon_prefixes = af.pool_for("Weapon", af.PREFIX)
    assert all(a.position == af.PREFIX for a in weapon_prefixes)
    assert all("Weapon" in a.allowed_slots for a in weapon_prefixes)
    assert set(af.pool_for("Weapon")) == set(weapon_prefixes) | \
        set(af.pool_for("Weapon", af.SUFFIX))
    with pytest.raises(ValueError, match="unknown gear slot"):
        af.pool_for("Cape")
    with pytest.raises(ValueError, match="unknown position"):
        af.pool_for("Ring", "infix")


def test_that_weapon_affix_check_actually_fires():
    real = af.AFFIX_POOL
    af.AFFIX_POOL = real + (af.StatAffix("Warding blade", "max_health", "flat",
                                         1.0, frozenset({"Weapon"})),)
    try:
        with pytest.raises(ValueError, match="and it defends"):
            af._check_no_weapon_rolls_a_defensive_affix()
    finally:
        af.AFFIX_POOL = real


def test_rings_take_the_widest_pool_of_any_slot():
    """The flexible slots a build uses to fix whatever it is short of, and there
    are eight of them."""
    widest = max(af.GEAR_SLOTS, key=lambda s: len(af.pool_for(s)))
    assert widest == "Ring"


# --------------------------------------------------------------------------
# Item bases, and the implicits that belong to them
# --------------------------------------------------------------------------

@pytest.mark.parametrize("slot", sorted(af.GEAR_SLOTS))
def test_every_slot_offers_at_least_three_bases(slot):
    """The implicit belongs to the BASE, not to the slot. One base in a slot is
    not a choice: the point is that picking one commits a character to a
    defensive layer or an offensive property before any affix is involved."""
    assert len(af.bases_for(slot)) >= 3


def test_that_base_choice_check_actually_fires():
    real = af.ITEM_BASES
    af.ITEM_BASES = tuple(b for b in real if b.slot != "Belt")[:1] + \
        tuple(b for b in real if b.slot == "Belt")[:1]
    af.BASES_BY_SLOT = {s: tuple(b for b in af.ITEM_BASES if b.slot == s)
                        for s in af.GEAR_SLOTS}
    try:
        with pytest.raises(ValueError, match="at least three"):
            af._check_every_slot_offers_a_real_choice_of_base()
    finally:
        af.ITEM_BASES = real
        af.BASES_BY_SLOT = {s: tuple(b for b in real if b.slot == s)
                            for s in af.GEAR_SLOTS}


@pytest.mark.parametrize("slot", sorted(af.GEAR_SLOTS))
def test_no_two_bases_in_a_slot_grant_the_same_thing(slot):
    """Two identical bases are one base written twice."""
    keys = [tuple(sorted((i.stat, i.kind, i.value) for i in b.implicits))
            for b in af.bases_for(slot)]
    assert len(keys) == len(set(keys))


def test_the_defensive_layers_each_have_a_base_to_come_from():
    """Armour, evasion and energy shield are the three defensive layers the
    design has, and a build committing to one should be able to find bases for
    it rather than waiting on affix rolls."""
    for layer in ("armor", "evasion", "max_energy_shield"):
        slots = {b.slot for b in af.ITEM_BASES
                 if any(i.stat == layer for i in b.implicits)}
        assert len(slots) >= 3, f"{layer} appears on only {sorted(slots)}"


def test_a_base_carries_one_to_three_implicits():
    for base in af.ITEM_BASES:
        assert 1 <= len(base.implicits) <= 3, base.name
    with pytest.raises(ValueError, match="one to three"):
        af.ItemBase("Empty", "Belt", ())


def test_a_base_in_a_slot_that_does_not_exist_is_rejected():
    with pytest.raises(ValueError, match="does not exist"):
        af.ItemBase("Cape", "Back", (af.Implicit("armor", "flat", 1.0),))


def test_an_implicit_does_not_roll():
    """It is fixed to the base. A tier and a roll are what an affix has and an
    implicit deliberately does not."""
    girdle = af.base_named("Girdle").implicits[0]
    assert not hasattr(girdle, "range_at")
    assert not hasattr(girdle, "top_value")
    assert girdle.value_at() == girdle.value


def test_gear_level_multiplies_an_implicit_the_same_way_it_does_an_affix():
    for base in af.ITEM_BASES:
        for implicit in base.implicits:
            assert implicit.value_at(10) == pytest.approx(implicit.value)
            assert implicit.value_at(0) == pytest.approx(
                implicit.value / af.gear_level_multiplier(10))


def test_bases_in_one_slot_pull_in_different_directions():
    """Spot-checked. A chest built for armour and a chest built for evasion are
    the same slot and different decisions, which is the whole point."""
    assert af.base_named("Cuirass").implicit_values().keys() == {"armor"}
    assert af.base_named("Jerkin").implicit_values().keys() == {"evasion"}
    assert af.base_named("Vestment").implicit_values().keys() == \
        {"max_energy_shield"}
    assert af.base_named("Hauberk").implicit_values().keys() == {"max_health"}


def test_an_implicit_naming_a_stat_that_does_not_exist_is_rejected():
    with pytest.raises(ValueError, match="neither on the character sheet"):
        af.Implicit("max_stamina", "flat", 1.0)
    with pytest.raises(ValueError, match="flat"):
        af.Implicit("max_health", "multiplicative", 1.0)


def test_an_unknown_base_name_is_rejected():
    with pytest.raises(ValueError, match="no item base named"):
        af.base_named("Bootstraps")


# --------------------------------------------------------------------------
# Weapon bases, which carry more than an armour base does
# --------------------------------------------------------------------------

def test_there_is_a_base_for_every_weapon_type_the_design_lists():
    """Eight one-handed and six two-handed, from section V."""
    one_handed = {b.weapon_type for b in af.WEAPON_BASES if b.hands == 1}
    two_handed = {b.weapon_type for b in af.WEAPON_BASES if b.hands == 2}
    assert one_handed == {"Sword", "Dagger", "Axe", "Fist", "Wand", "Whip",
                          "Shield", "Crossbow"}
    assert two_handed == {"Greatsword", "Greataxe", "Spear", "Staff",
                          "2H Crossbow", "Warhammer"}
    assert len(af.WEAPON_BASES) == 14


def test_that_weapon_type_check_actually_fires():
    real = af.WEAPON_BASES
    af.WEAPON_BASES = tuple(b for b in real if b.weapon_type != "Whip")
    try:
        with pytest.raises(ValueError, match="do not match the design"):
            af._check_the_weapon_types_match_the_design()
    finally:
        af.WEAPON_BASES = real


def test_every_weapon_has_one_of_the_four_sub_types():
    """The design's Weapon Sub-Types table: Piercing ignores armour, Slashing
    beats health, Blunt stuns, Magic strips energy shield."""
    assert set(af.WEAPON_SUB_TYPES) == {"Piercing", "Slashing", "Blunt", "Magic"}
    for base in af.WEAPON_BASES:
        assert base.sub_type in af.WEAPON_SUB_TYPES, base.name
    # And every sub-type is reachable, or one of the four would be dead.
    assert {b.sub_type for b in af.WEAPON_BASES} == set(af.WEAPON_SUB_TYPES)


def test_a_weapon_base_carries_a_limit_not_a_count():
    """The number on the base is the MOST damage types it can ever hold. A
    one-hander tops out at four and a two-hander at eight."""
    for base in af.WEAPON_BASES:
        expected = (af.DAMAGE_TYPES_ON_ONE_HANDED if base.hands == 1
                    else af.DAMAGE_TYPES_ON_TWO_HANDED)
        assert base.max_damage_types_on_base == expected, base.name
    assert af.DAMAGE_TYPES_ON_ONE_HANDED == 4
    assert af.DAMAGE_TYPES_ON_TWO_HANDED == len(af.DAMAGE_TYPES)


def test_the_difficulty_tier_caps_how_many_damage_types_a_weapon_rolls():
    """A two-hander gains one more possible type per tier until it reaches its
    own limit; a one-hander stops at four however deep the player goes."""
    assert [af.max_damage_types(2, t) for t in range(1, 9)] == [
        1, 2, 3, 4, 5, 6, 7, 8]
    assert [af.max_damage_types(1, t) for t in range(1, 9)] == [
        1, 2, 3, 4, 4, 4, 4, 4]
    # The two are identical up to tier 4 and diverge from tier 5, which is the
    # rule as the project owner stated it.
    for tier in range(1, 5):
        assert af.max_damage_types(1, tier) == af.max_damage_types(2, tier)
    for tier in range(5, 9):
        assert af.max_damage_types(1, tier) < af.max_damage_types(2, tier)


def test_no_weapon_can_roll_more_damage_types_than_exist():
    for hands in (1, 2):
        for tier in range(1, af.DIFFICULTY_TIERS + 1):
            assert 1 <= af.max_damage_types(hands, tier) <= len(af.DAMAGE_TYPES)


def test_max_damage_types_rejects_input_that_cannot_happen():
    with pytest.raises(ValueError, match="one- or two-handed"):
        af.max_damage_types(3, 1)
    with pytest.raises(ValueError, match="outside 1 to"):
        af.max_damage_types(1, 0)
    with pytest.raises(ValueError, match="outside 1 to"):
        af.max_damage_types(1, af.DIFFICULTY_TIERS + 1)


def test_dual_wielding_carries_more_damage_types_than_a_two_hander():
    """The design says dual wielding is the primary route to multiclassing
    BECAUSE it is how a player carries more damage types at once.

    COMPARING THE RAW LIMITS NO LONGER PROVES IT: both reach eight. The tier cap
    is what keeps it true, so this walks the tiers.
    """
    af._check_dual_wielding_carries_more_damage_types_than_a_two_hander()
    leads = [af.max_damage_types(1, t) * 2 - af.max_damage_types(2, t)
             for t in range(1, af.DIFFICULTY_TIERS + 1)]
    # Ahead at every tier but the last, widest at tier 4, level at tier 8.
    assert leads == [1, 2, 3, 4, 3, 2, 1, 0]


def test_that_the_multiclassing_check_actually_fires(monkeypatch):
    """Lowering the one-handed limit to two lets a two-hander draw level at tier
    4, three tiers before it is allowed to."""
    monkeypatch.setattr(af, "DAMAGE_TYPES_ON_ONE_HANDED", 2)
    with pytest.raises(ValueError, match="route to multiclassing"):
        af._check_dual_wielding_carries_more_damage_types_than_a_two_hander()


def test_the_multiclassing_check_allows_the_two_hander_to_catch_up_at_the_last_tier():
    """The shipped numbers tie at tier 8 and that is deliberate, so the check
    must pass rather than treating the tie as a failure."""
    assert (af.max_damage_types(1, af.DIFFICULTY_TIERS) * 2
            == af.max_damage_types(2, af.DIFFICULTY_TIERS))
    af._check_dual_wielding_carries_more_damage_types_than_a_two_hander()


def test_a_weapon_base_naming_a_sub_type_that_does_not_exist_is_rejected():
    with pytest.raises(ValueError, match="expected one of"):
        af.WeaponBase("Odd", "Weapon",
                      (af.Implicit("attack_damage", "flat", 1.0),),
                      sub_type="Explosive")


def test_no_weapon_can_roll_a_defensive_affix():
    """AFFIXES only. What a drop happened to roll on a weapon may not defend."""
    defensive = {"max_health", "max_energy_shield", "armor", "evasion",
                 "block_chance", "damage_reduction"}
    assert not {a.stat for a in af.pool_for("Weapon")} & defensive


def test_the_shield_is_the_one_weapon_whose_base_defends():
    """The design lists Shield among the one-handed weapon types and says there
    are no offhand items, so it is a weapon with nowhere else to be. Its block
    and armour are what the base IS, not something a drop rolled."""
    shield = af.base_named("Shield")
    assert shield.implicit_values().keys() == {"block_chance", "armor"}
    defensive = {"max_health", "max_energy_shield", "armor", "evasion",
                 "block_chance", "damage_reduction"}
    for base in af.WEAPON_BASES:
        if base.weapon_type == "Shield":
            continue
        assert not set(base.implicit_values()) & defensive, base.name


def test_every_weapon_but_the_shield_supplies_flat_attack_damage():
    """Every skill deals a percent of weapon damage, and spells have no separate
    path, so a weapon with no flat attack damage is one a character can hold and
    deal exactly zero with. The Wand and the Staff shipped that way: both gave
    only INCREASED spell damage, which multiplies a number they did not supply.
    Issue #146."""
    for base in af.WEAPON_BASES:
        supplies = any(i.stat == "attack_damage" and i.kind == "flat"
                       for i in base.implicits)
        if base.weapon_type == "Shield":
            assert not supplies, "the Shield is not meant to hit anything"
        else:
            assert supplies, base.name


def test_that_the_weapon_damage_check_actually_fires():
    real = af.WEAPON_BASES
    af.WEAPON_BASES = real + (af.WeaponBase(
        "Damageless Rod", "Weapon",
        (af.Implicit("spell_damage", "increased", 20.0),),
        weapon_type="Wand", sub_type="Magic", attack_speed=1.35),)
    try:
        with pytest.raises(ValueError, match="supplies no flat attack damage"):
            af._check_every_weapon_but_the_shield_supplies_damage()
    finally:
        af.WEAPON_BASES = real


def test_that_shield_exemption_check_actually_fires():
    real = af.WEAPON_BASES
    af.WEAPON_BASES = real + (af.WeaponBase(
        "Bulwark Blade", "Weapon", (af.Implicit("armor", "flat", 10.0),),
        weapon_type="Sword", attack_speed=1.3),)
    try:
        with pytest.raises(ValueError, match="only the Shield may defend"):
            af._check_only_the_shield_defends_among_weapon_bases()
    finally:
        af.WEAPON_BASES = real


# --------------------------------------------------------------------------
# Ailment affixes, from the effects the gems already apply
# --------------------------------------------------------------------------

def test_every_effect_a_gem_applies_is_reachable_as_an_affix():
    """`game/Data/Gems.csv` designs ten gems that apply an effect on hit. The
    project owner asked for the same effects to be reachable as affixes.

    Nine, not eight: the Of Wasting gem was added because Necrosis was the one
    effect in `game/Data/StatusEffects.csv` that nothing applied. Ten, not nine:
    the Of Embers gem was added for issue #152, because burn was the one
    player-applicable effect with neither a gem nor an affix, which meant a
    Demonic character's burn chance from gear was always zero.
    """
    from_gems = {"Void Splinter", "Poison", "Bleed", "Madness", "Disease",
                 "Necrosis", "Burn", "Cripple", "Weaken", "Shred"}
    covered = {a.ailment for a in af.AILMENT_AFFIXES}

    # ONE DIRECTION, SINCE 2026-08-16. This compared the two sets for equality,
    # which also required every AFFIX to have a gem -- never a stated rule, only
    # a fact about the ten that existed. The chance to stun added for issue #298
    # has no gem, deliberately: nothing in Gems.csv applies stun, which is what
    # makes the affix the only way to buy the chance.
    assert from_gems <= covered, sorted(from_gems - covered)


#: The one ailment affix with no gem, and why. Written out so adding a second
#: has to be deliberate rather than a typo in a gem name.
AFFIXES_WITH_NO_GEM = {"Chance to stun"}


def test_every_ailment_affix_names_the_gem_that_shares_its_effect():
    """So the two stay findable from each other when either is edited.

    THE CHANCE TO STUN IS THE EXCEPTION AND IS NAMED. Nothing in
    `game/Data/Gems.csv` applies stun, so there is no gem for it to name. That is
    also what makes the affix worth more than the same 15% is elsewhere: it is
    the only way to buy chance beyond what a Blunt weapon gives free.
    """
    for affix in af.AILMENT_AFFIXES:
        if affix.name in AFFIXES_WITH_NO_GEM:
            assert affix.gem == "", (
                f"{affix.name} is listed as having no gem and names {affix.gem!r}. "
                "If a gem now applies it, take it out of AFFIXES_WITH_NO_GEM.")
            continue
        assert affix.gem.startswith("Of "), affix.name


def test_no_gem_applies_the_effects_listed_as_having_none():
    """What stops AFFIXES_WITH_NO_GEM being a way to skip the check above. If a
    gem is ever added that applies stun, this fails and the exception goes."""
    import pathlib

    gems = (pathlib.Path(__file__).resolve().parents[2] / "game" / "Data"
            / "Gems.csv")
    if not gems.is_file():
        pytest.skip("game/Data/Gems.csv is not present")

    text = gems.read_text(encoding="utf-8-sig").lower()
    for name in AFFIXES_WITH_NO_GEM:
        effect = next(a.ailment for a in af.AILMENT_AFFIXES if a.name == name)
        assert effect.lower() not in text, (
            f"a gem in game/Data/Gems.csv now applies {effect}, so "
            f"{name!r} should name it rather than being listed as having none.")


def test_the_damage_over_time_effects_are_marked_as_such():
    """`game/Data/StatusEffects.csv` lists these six with EffectKind DoT.
    Damage over time matters separately because the design says it bypasses
    energy shield and holds it empty."""
    assert af.DAMAGE_OVER_TIME_AILMENTS == {"Bleed", "Poison", "Disease",
                                            "Void Splinter", "Necrosis", "Burn"}
    assert af.DAMAGE_OVER_TIME_AILMENTS <= {a.ailment
                                            for a in af.AILMENT_AFFIXES}


def test_every_ailment_can_be_found_on_a_weapon():
    """What the project owner asked for: on weapons at least."""
    assert set(af.ailments_for("Weapon")) == set(af.AILMENT_AFFIXES)


def test_no_ailment_rolls_on_armour():
    """They only make sense where a hit comes from."""
    for slot in ("Head", "Chest", "Shoulders", "Gloves", "Pants", "Boots",
                 "Belt"):
        assert af.ailments_for(slot) == ()


def test_that_ailment_placement_check_actually_fires():
    real = af.AILMENT_AFFIXES
    af.AILMENT_AFFIXES = real[:-1] + (
        af.AilmentAffix("Bad", "Bleed", 10.0, frozenset({"Boots", "Weapon"})),)
    try:
        with pytest.raises(ValueError, match="no hit comes from"):
            af._check_ailments_only_appear_where_a_hit_comes_from()
    finally:
        af.AILMENT_AFFIXES = real


def test_an_ailment_affix_uses_the_same_tier_curve_and_band_as_the_rest():
    for affix in af.AILMENT_AFFIXES:
        assert affix.range_at(7) == af.tier_band(affix.top_chance, 7)
        assert affix.chance_at(7) == pytest.approx(affix.top_chance)
        assert affix.chance_at(7, gear_level=0) < affix.chance_at(7)


def test_the_gem_stays_the_stronger_source_of_any_ailment():
    """A socket is a commitment and an affix is a roll. If the affix matched the
    gem, an ailment build would have no reason to spend a socket."""
    # The Of Rending gem reaches 150% chance at Cataclysmic rarity.
    assert af.BLEED.chance_at(7) < 150.0
    assert af.POISON.chance_at(7) < 180.0


# --------------------------------------------------------------------------
# Chance above 100% becomes magnitude
# --------------------------------------------------------------------------

def test_chance_to_apply_caps_at_one_hundred_percent():
    for chance in (100.0, 150.0, 800.0, 5000.0):
        applied, _ = af.ailment_application(chance)
        assert applied == pytest.approx(100.0), chance


def test_below_the_cap_chance_is_just_chance_and_magnitude_is_unchanged():
    for chance in (0.0, 15.0, 60.0, 99.9):
        applied, magnitude = af.ailment_application(chance)
        assert applied == pytest.approx(chance)
        assert magnitude == pytest.approx(1.0)


def test_the_project_owners_own_example_holds():
    """Stated 2026-08-03: at 800% chance the effect gets a 700% multiplier."""
    applied, magnitude = af.ailment_application(800.0)
    assert applied == pytest.approx(100.0)
    assert magnitude == pytest.approx(8.0)
    assert magnitude - 1.0 == pytest.approx(7.0)


def test_every_point_of_chance_past_the_cap_is_worth_the_same():
    """The whole reason for the rule. Without it an ailment build would stop
    progressing at exactly the point it was coming together, because every point
    past 100% would be dead."""
    def magnitude(chance: float) -> float:
        return af.ailment_application(chance)[1]

    first_hundred = magnitude(200.0) - magnitude(100.0)
    seventh_hundred = magnitude(800.0) - magnitude(700.0)
    assert first_hundred == pytest.approx(seventh_hundred)
    assert first_hundred == pytest.approx(1.0)


def test_an_enemy_carries_at_most_one_stack():
    """Which is what makes chance above the cap mean something rather than
    stacking, and keeps a screen full of enemies readable."""
    assert af.MAX_STACKS_ON_AN_ENEMY == 1


def test_a_negative_chance_is_rejected():
    with pytest.raises(ValueError, match="is not a chance"):
        af.ailment_application(-10.0)


def test_a_full_set_of_ailment_affixes_does_not_waste_itself():
    """Read against the real pool rather than in the abstract. There are 48
    slots an ailment affix can occupy, so a build that commits to one reaches
    well past the cap, and the rule is what stops that being wasted."""
    slots = af.BLEED.slots_available()
    total = af.BLEED.chance_at(7) * slots
    applied, magnitude = af.ailment_application(total)
    assert applied == pytest.approx(100.0)
    assert magnitude > 2.0, (
        f"{slots} slots of bleed reach {total:.0f}% and only {magnitude:.1f}x")


# --------------------------------------------------------------------------
# Hybrid affixes
# --------------------------------------------------------------------------

def test_a_hybrid_grants_less_of_each_stat_than_the_single_affix_does():
    for hybrid in af.HYBRID_AFFIXES:
        for part in hybrid.parts:
            assert hybrid.value_of(part) < part.value_at(7)


def test_a_hybrid_is_worth_more_than_one_affix_spread_over_two_stats():
    """What stops the single affix being strictly better.

    Measured as a SHARE of each part's own value, not as a sum of the two
    numbers. Summing them would be meaningless: the health and energy shield
    hybrid grants 84 and 35, and adding those treats a point of health and a
    point of shield as the same unit. An earlier version of this test did
    exactly that and failed, because 84 plus 35 is less than flat health's 120.
    """
    for hybrid in af.HYBRID_AFFIXES:
        share = sum(hybrid.value_of(p) / p.value_at(7) for p in hybrid.parts)
        assert share == pytest.approx(2 * af.HYBRID_FRACTION)
        assert share > 1.0, hybrid.name


def test_the_hybrid_reduction_is_read_off_the_resistance_families():
    """Not written twice. It is the ratio already set between the two-resistance
    affix and the single-resistance one, so the whole pool moves together."""
    assert af.HYBRID_FRACTION == pytest.approx(
        af.HYBRID_RESISTANCE.top_value / af.SINGLE_RESISTANCE.top_value)
    assert af.HYBRID_FRACTION == pytest.approx(0.70)


def test_a_hybrid_cannot_reach_a_slot_one_of_its_halves_could_not():
    for hybrid in af.HYBRID_AFFIXES:
        for part in hybrid.parts:
            assert hybrid.allowed_slots <= part.allowed_slots, hybrid.name


def test_a_hybrid_sits_in_one_pool():
    for hybrid in af.HYBRID_AFFIXES:
        assert all(p.position == hybrid.position for p in hybrid.parts)
    with pytest.raises(ValueError, match="one pool"):
        af.HybridAffix("Mixed", (af.FLAT_HEALTH, af.FLAT_PENETRATION),
                       frozenset({"Ring"}), af.PREFIX)


def test_a_hybrid_combining_a_stat_with_itself_is_rejected():
    with pytest.raises(ValueError, match="with itself"):
        af.HybridAffix("Doubled", (af.FLAT_HEALTH, af.FLAT_HEALTH),
                       frozenset({"Ring"}), af.PREFIX)


def test_asking_a_hybrid_for_a_stat_it_does_not_grant_is_rejected():
    with pytest.raises(ValueError, match="is not part of"):
        af.HYBRID_AFFIXES[0].value_of(af.FLAT_PENETRATION)


def test_the_pool_is_bigger_than_it_was():
    """The project owner called the first pass small. This counts everything a
    drop could roll, with each resistance family counted once."""
    assert af.total_pool_size() >= 55
    assert len(af.HYBRID_AFFIXES) >= 10


# --------------------------------------------------------------------------
# The two-handed multiplier, and what balances it against dual wielding
# --------------------------------------------------------------------------

def test_a_two_handed_weapon_doubles_its_own_implicits():
    greatsword = af.base_named("Greatsword")
    assert greatsword.value_multiplier == af.TWO_HANDED_MULTIPLIER
    # The stated base damage is 78. What the base actually supplies is double.
    assert greatsword.implicit_values()["attack_damage"] == pytest.approx(156.0)


def test_a_one_handed_weapon_and_armour_do_not():
    axe = af.base_named("Axe")
    helm = af.base_named("Helm")
    assert axe.value_multiplier == 1.0
    assert helm.value_multiplier == 1.0
    assert axe.implicit_values()["attack_damage"] == pytest.approx(46.0)


def test_an_affix_on_a_two_handed_weapon_is_worth_double():
    greatsword = af.base_named("Greatsword")
    axe = af.base_named("Axe")
    assert (greatsword.affix_value(af.FLAT_DAMAGE)
            == pytest.approx(2.0 * axe.affix_value(af.FLAT_DAMAGE)))


def test_the_two_loadouts_are_worth_the_same_in_affixes():
    """The reason the multiplier is 2.0 and not something else.

    Section VII requires it: two one-handed weapons count as one equipped piece
    for Power Score, so if the affix budgets differed, one side would carry
    power its rating does not count.
    """
    two_handed = af.AFFIX_SLOTS_PER_PIECE * af.TWO_HANDED_MULTIPLIER
    dual_wield = af.AFFIX_SLOTS_PER_PIECE * 2
    assert two_handed == pytest.approx(dual_wield)


def test_filling_the_offhand_adds_one_piece_and_four_slots():
    """True whether the offhand holds a second weapon or a Shield. The project
    owner settled on 2026-08-15 that a Shield is treated just like a second
    one-handed weapon."""
    assert af.GEAR_PIECES_WITH_AN_OFFHAND == af.GEAR_PIECES + 1
    assert af.TOTAL_AFFIX_SLOTS_WITH_AN_OFFHAND == af.TOTAL_AFFIX_SLOTS + 4


def test_only_a_two_handed_weapon_multiplies_anything():
    for base in af.ITEM_BASES:
        two_handed = isinstance(base, af.WeaponBase) and base.hands == 2
        expected = af.TWO_HANDED_MULTIPLIER if two_handed else 1.0
        assert base.value_multiplier == expected, base.name


def test_the_multiplier_helper_rejects_an_impossible_hand_count():
    assert af.two_handed_multiplier(1) == 1.0
    assert af.two_handed_multiplier(2) == af.TWO_HANDED_MULTIPLIER
    with pytest.raises(ValueError, match="1 or 2 hands"):
        af.two_handed_multiplier(3)


def test_the_two_handed_bonus_beats_summed_one_handed_base_damage():
    """The reason the multiplier has to reach the implicits and not only the
    affixes.

    Two one-handed bases SUM, which the project owner settled on 2026-08-03. An
    Axe and a Sword give 86 against a Greatsword's stated 78, so with the affix
    half alone the two-hander loses on damage while also holding one fewer
    damage type. Doubling the implicit is what reverses it.
    """
    summed_one_handed = sum(af.base_named(n).implicit_values()["attack_damage"]
                            for n in ("Axe", "Sword"))
    two_handed = af.base_named("Greatsword").implicit_values()["attack_damage"]

    assert summed_one_handed == pytest.approx(86.0)
    assert two_handed > summed_one_handed


# --------------------------------------------------------------------------
# Rarity is a label for what fills an item's four slots
# --------------------------------------------------------------------------

def test_rarity_is_decided_by_what_the_item_carries():
    """Stated by the project owner 2026-08-03: an item that drops with an
    enchantment is a Legendary; one that drops with three regular affixes is a
    Superb. Rarity is not a property the item carries."""
    assert af.rarity_of(0, 1) == "Everyday"
    assert af.rarity_of(0, 2) == "Quality"
    assert af.rarity_of(0, 3) == "Superb"
    assert af.rarity_of(0, 4) == "Masterful"
    assert af.rarity_of(1, 3) == "Legendary"
    assert af.rarity_of(2, 2) == "Mythical"
    assert af.rarity_of(3, 1) == "Ascendant"
    assert af.rarity_of(4, 0) == "Cataclysmic"


def test_every_rarity_is_produced_by_exactly_one_combination():
    combinations = [(af.enchantments_for(r), af.affix_slots_for(r))
                    for r in af.RARITIES]
    assert len(set(combinations)) == len(af.RARITIES)
    for rarity, combination in zip(af.RARITIES, combinations, strict=True):
        assert af.rarity_of(*combination) == rarity


def test_adding_an_affix_promotes_the_piece():
    """An Everyday item with an affix added becomes a Quality piece."""
    assert af.rarity_for_affix_count(1) == "Everyday"
    assert af.rarity_for_affix_count(2) == "Quality"
    assert af.rarity_for_affix_count(3) == "Superb"
    assert af.rarity_for_affix_count(4) == "Masterful"
    for count in range(1, af.AFFIX_SLOTS_PER_PIECE + 1):
        rarity = af.rarity_for_affix_count(count)
        assert af.affix_slots_for(rarity) == count
        assert af.enchantments_for(rarity) == 0


def test_an_enchantment_takes_an_affix_slot_rather_than_adding_one():
    """Applying an enchantment to a Masterful piece makes it Legendary, and it
    gives up a regular affix to do so. That is the trade the design describes."""
    assert af.affix_slots_for("Masterful") == 4
    assert af.affix_slots_for("Legendary") == 3
    assert af.enchantments_for("Legendary") == 1
    for rarity in af.RARITIES[4:]:
        filled = af.enchantments_for(rarity) + af.affix_slots_for(rarity)
        assert filled == af.AFFIX_SLOTS_PER_PIECE, rarity


def test_a_cataclysmic_item_has_no_regular_affixes():
    """All four of its slots hold enchantments. This is why the 72 affix budget
    belongs to Masterful gear and not to Cataclysmic gear. See issue #125."""
    assert af.enchantments_for("Cataclysmic") == af.AFFIX_SLOTS_PER_PIECE
    assert af.affix_slots_for("Cataclysmic") == 0


def test_the_affix_budget_belongs_to_masterful():
    best = max(af.affix_slots_for(r) for r in af.RARITIES)
    assert af.GEAR_PIECES * best == af.TOTAL_AFFIX_SLOTS == 72
    assert [r for r in af.RARITIES if af.affix_slots_for(r) == best] == ["Masterful"]


def test_enchantments_climb_as_rarity_rises():
    counts = [af.enchantments_for(r) for r in af.RARITIES]
    assert counts == sorted(counts)
    assert counts[:4] == [0, 0, 0, 0], "enchantments start at Legendary"
    assert counts[-1] == af.AFFIX_SLOTS_PER_PIECE


def test_an_unknown_rarity_is_rejected_rather_than_defaulting():
    with pytest.raises(ValueError, match="is not a rarity"):
        af.affix_slots_for("Uncommon")
    with pytest.raises(ValueError, match="is not a rarity"):
        af.enchantments_for("Uncommon")


def test_a_combination_that_names_no_rarity_is_rejected():
    """Below Legendary a piece fills only as many slots as it has affixes; from
    there it fills all four. Anything else is not an item this design makes."""
    with pytest.raises(ValueError, match="not a rarity"):
        af.rarity_of(1, 1)          # an enchantment with two slots empty
    with pytest.raises(ValueError, match="1 to 4 filled"):
        af.rarity_of(0, 0)          # nothing at all
    with pytest.raises(ValueError, match="1 to 4 filled"):
        af.rarity_of(3, 3)          # six slots
    with pytest.raises(ValueError, match="neither can be negative"):
        af.rarity_of(-1, 2)


def test_there_is_no_fifth_affix_to_add():
    with pytest.raises(ValueError, match="no fifth slot"):
        af.rarity_for_affix_count(af.AFFIX_SLOTS_PER_PIECE + 1)
    with pytest.raises(ValueError, match="not an item"):
        af.rarity_for_affix_count(0)


def test_a_split_adds_back_to_the_slot_count_and_respects_both_caps():
    for slots in range(af.AFFIX_SLOTS_PER_PIECE + 1):
        prefixes, suffixes = af.prefix_suffix_split(slots)
        assert prefixes + suffixes == slots
        assert prefixes <= af.PREFIXES_PER_PIECE
        assert suffixes <= af.SUFFIXES_PER_PIECE


def test_the_split_at_each_slot_count():
    assert af.prefix_suffix_split(1) == (1, 0)
    assert af.prefix_suffix_split(2) == (1, 1)
    assert af.prefix_suffix_split(3) == (2, 1)
    assert af.prefix_suffix_split(4) == (2, 2)


def test_a_split_beyond_the_ceiling_is_rejected():
    with pytest.raises(ValueError, match="outside"):
        af.prefix_suffix_split(af.AFFIX_SLOTS_PER_PIECE + 1)
