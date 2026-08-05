"""One affix per group: the rule that stops a piece rolling the same stat twice.

WHY THIS EXISTS. Issue #128. Affixes were restricted against the gear slot and
against the prefix or suffix pool, and against nothing else. Nothing stopped a
four-affix Masterful piece rolling "Flat maximum health" four times over.

THE RULE. An affix belongs to a group for every stat it grants, named by the stat
and the kind together. One piece holds at most one affix from any group. The
group is derived from what the affix grants rather than written on it, so a new
affix cannot arrive without one.

The three cases the issue left open are each pinned below:

    flat against increased      different groups, so both may sit on one piece
    a hybrid against its half   same group, so they may not
    prefix against suffix       cannot collide; no stat appears in both pools
"""

from __future__ import annotations

import random

import pytest

from cataclysm_sim import affixes as af


# --------------------------------------------------------------------------
# What a group is
# --------------------------------------------------------------------------

def test_a_group_is_the_stat_and_the_kind_together():
    assert af.groups_of(af.FLAT_HEALTH) == {af.stat_group("max_health", "flat")}


def test_flat_and_increased_of_one_stat_are_different_groups():
    """The design says neither kind is strictly better and that is the reason
    for having both, so a piece carrying one of each is the design working
    rather than a duplicate that slipped through."""
    assert af.FLAT_HEALTH.stat == af.INCREASED_HEALTH.stat
    assert af.groups_of(af.FLAT_HEALTH) != af.groups_of(af.INCREASED_HEALTH)
    assert af.may_join(af.INCREASED_HEALTH, af.groups_of(af.FLAT_HEALTH))


def test_no_two_affixes_in_the_pool_share_a_group():
    """Two stat affixes sharing a group would be the same affix written twice,
    and one of them could never roll beside the other."""
    seen: dict[str, str] = {}
    for affix in af.AFFIX_POOL:
        for group in af.groups_of(affix):
            assert group not in seen, (
                f"{affix.name} and {seen[group]} are both in group {group}")
            seen[group] = affix.name


def test_every_stat_affix_has_exactly_one_group():
    for affix in af.AFFIX_POOL:
        assert len(af.groups_of(affix)) == 1, affix.name


def test_an_ailment_affix_is_grouped_by_the_effect_it_applies():
    """It grants no stat, so it cannot be grouped by one. Two rolls of the same
    chance on one piece is still the duplicate the rule exists to stop."""
    assert af.groups_of(af.BLEED) == {af.ailment_group("Bleed")}
    assert af.groups_of(af.BLEED) != af.groups_of(af.POISON)
    assert af.may_join(af.POISON, af.groups_of(af.BLEED))
    assert not af.may_join(af.BLEED, af.groups_of(af.BLEED))


def test_every_ailment_affix_has_its_own_group():
    groups = [next(iter(af.groups_of(a))) for a in af.AILMENT_AFFIXES]
    assert len(set(groups)) == len(af.AILMENT_AFFIXES)


def test_an_ailment_group_can_never_collide_with_a_stat_group():
    """A stat named 'Bleed' would break the rule silently. The two key spaces
    are kept apart by construction, and this is what says so."""
    stat_groups = {g for a in af.AFFIX_POOL for g in af.groups_of(a)}
    ailment_groups = {g for a in af.AILMENT_AFFIXES for g in af.groups_of(a)}
    assert not (stat_groups & ailment_groups)


def test_an_object_that_is_not_an_affix_is_rejected():
    with pytest.raises(TypeError):
        af.groups_of("Flat maximum health")


# --------------------------------------------------------------------------
# A hybrid occupies the group of each of its halves
# --------------------------------------------------------------------------

def test_a_hybrid_occupies_the_group_of_both_its_parts():
    health_and_armor = next(h for h in af.HYBRID_AFFIXES
                            if h.name == "Health and armor")
    assert af.groups_of(health_and_armor) == (
        af.groups_of(af.FLAT_HEALTH) | af.groups_of(af.FLAT_ARMOR))


def test_a_hybrid_cannot_sit_beside_either_of_its_halves():
    """The judgement made on issue #128, and the one place this design differs
    from Path of Exile 2, which gives a hybrid its own group. A hybrid grants
    each half at 70%, so allowing it beside its own half puts the same stat on
    one piece twice."""
    for hybrid in af.HYBRID_AFFIXES:
        taken = af.groups_of(hybrid)
        for part in hybrid.parts:
            assert not af.may_join(part, taken), (
                f"{part.name} may still roll beside {hybrid.name}")


def test_a_hybrid_may_sit_beside_an_affix_it_shares_no_stat_with():
    health_and_armor = next(h for h in af.HYBRID_AFFIXES
                            if h.name == "Health and armor")
    assert af.may_join(af.FLAT_EVASION, af.groups_of(health_and_armor))


def test_two_hybrids_sharing_a_stat_cannot_sit_on_one_piece():
    """`Health and armor` and `Armor and evasion` both grant flat armor."""
    first = next(h for h in af.HYBRID_AFFIXES if h.name == "Health and armor")
    second = next(h for h in af.HYBRID_AFFIXES if h.name == "Armor and evasion")
    assert af.groups_of(first) & af.groups_of(second)
    assert not af.may_join(second, af.groups_of(first))


def test_two_hybrids_sharing_no_stat_may_sit_on_one_piece():
    first = next(h for h in af.HYBRID_AFFIXES if h.name == "Health and armor")
    second = next(h for h in af.HYBRID_AFFIXES
                  if h.name == "Mana and energy shield")
    assert af.may_join(second, af.groups_of(first))


# --------------------------------------------------------------------------
# Resistance is grouped by damage type, not by family
# --------------------------------------------------------------------------

def test_a_single_resistance_roll_occupies_one_group():
    assert af.groups_of(af.SINGLE_RESISTANCE, ("War",)) == {
        af.resistance_group("War")}


def test_two_single_resistance_rolls_of_different_types_may_share_a_piece():
    """They are two stats, so they are two groups. That falls out of the rule
    rather than needing one of its own."""
    taken = af.groups_of(af.SINGLE_RESISTANCE, ("War",))
    assert af.may_join(af.SINGLE_RESISTANCE, taken, ("Demonic",))
    assert not af.may_join(af.SINGLE_RESISTANCE, taken, ("War",))


def test_an_all_resistance_roll_excludes_every_other_resistance_affix():
    taken = af.groups_of(af.ALL_RESISTANCE, af.DAMAGE_TYPES)
    assert len(taken) == len(af.DAMAGE_TYPES)
    for damage_type in af.DAMAGE_TYPES:
        assert not af.may_join(af.SINGLE_RESISTANCE, taken, (damage_type,))


def test_a_two_resistance_roll_excludes_only_the_types_it_covers():
    taken = af.groups_of(af.HYBRID_RESISTANCE, ("War", "Demonic"))
    assert not af.may_join(af.SINGLE_RESISTANCE, taken, ("War",))
    assert af.may_join(af.SINGLE_RESISTANCE, taken, ("Death",))


def test_a_resistance_family_needs_as_many_types_as_its_breadth():
    with pytest.raises(ValueError):
        af.groups_of(af.HYBRID_RESISTANCE, ("War",))
    with pytest.raises(ValueError):
        af.groups_of(af.SINGLE_RESISTANCE, ())
    with pytest.raises(ValueError):
        af.groups_of(af.HYBRID_RESISTANCE, ("War", "War"))


def test_an_unknown_damage_type_is_rejected_rather_than_becoming_its_own_group():
    with pytest.raises(ValueError):
        af.resistance_group("Fire")


def test_only_a_resistance_family_takes_covered_damage_types():
    with pytest.raises(ValueError):
        af.groups_of(af.FLAT_HEALTH, ("War",))


def test_a_resistance_group_is_a_stat_on_the_character_sheet():
    """Grouping resistance by damage type only works because the eight
    resistances really are eight separate stats."""
    from cataclysm_sim.character import RESISTANCE_STATS
    for damage_type in af.DAMAGE_TYPES:
        stat = af.resistance_group(damage_type).split(".")[0]
        assert stat in RESISTANCE_STATS


# --------------------------------------------------------------------------
# Prefixes and suffixes cannot collide
# --------------------------------------------------------------------------

def test_the_two_pools_share_no_group():
    """Answered already by the rule that a stat appearing as a prefix never
    appears as a suffix. Pinned here because the group rule relies on it."""
    prefixes = {g for a in af.AFFIX_POOL if a.position == af.PREFIX
                for g in af.groups_of(a)}
    suffixes = {g for a in af.AFFIX_POOL if a.position == af.SUFFIX
                for g in af.groups_of(a)}
    assert not (prefixes & suffixes)


def test_hybrids_do_not_straddle_the_two_pools_either():
    for hybrid in af.HYBRID_AFFIXES:
        positions = {p.position for p in hybrid.parts}
        assert positions == {hybrid.position}, hybrid.name


# --------------------------------------------------------------------------
# Drawing a piece's affixes
# --------------------------------------------------------------------------

def _no_families(pool):
    """A resistance family needs its covered types decided before it has a
    group, which is the drop roll's job rather than this helper's."""
    return [a for a in pool if not isinstance(a, af.AffixFamily)]


def test_a_draw_never_takes_two_affixes_from_one_group():
    rng = random.Random(20260805)
    for slot in af.GEAR_SLOTS:
        for position, count in ((af.PREFIX, af.PREFIXES_PER_PIECE),
                                (af.SUFFIX, af.SUFFIXES_PER_PIECE)):
            candidates = _no_families(af.everything_for(slot, position))
            for _ in range(50):
                drawn = af.draw_without_repeating_a_group(
                    candidates, count, rng)
                taken: set[str] = set()
                for affix in drawn:
                    groups = af.groups_of(affix)
                    assert not (groups & taken), (
                        f"{slot} {position}: {affix.name} repeats a group")
                    taken |= groups


def test_a_draw_returns_the_number_asked_for():
    rng = random.Random(1)
    candidates = _no_families(af.everything_for("Chest", af.PREFIX))
    assert len(af.draw_without_repeating_a_group(candidates, 2, rng)) == 2


def test_a_draw_of_nothing_is_allowed():
    """An Everyday item has one affix and a piece can carry zero of a position,
    so zero has to be a legal ask rather than an error."""
    rng = random.Random(1)
    candidates = _no_families(af.everything_for("Chest", af.PREFIX))
    assert af.draw_without_repeating_a_group(candidates, 0, rng) == ()


def test_a_draw_that_cannot_be_filled_raises_rather_than_returning_short():
    """Returning fewer than asked would give an item silently missing an affix,
    which reads as an unlucky drop rather than a fault in the pool."""
    rng = random.Random(1)
    health_and_armor = next(h for h in af.HYBRID_AFFIXES
                            if h.name == "Health and armor")
    one_group_only = [af.FLAT_HEALTH, af.FLAT_ARMOR, health_and_armor]
    with pytest.raises(ValueError):
        af.draw_without_repeating_a_group(one_group_only, 3, rng)


def test_a_negative_draw_is_rejected():
    with pytest.raises(ValueError):
        af.draw_without_repeating_a_group([af.FLAT_HEALTH], -1, random.Random(1))


def test_a_draw_does_not_consume_its_candidate_list():
    rng = random.Random(1)
    candidates = _no_families(af.everything_for("Chest", af.PREFIX))
    before = list(candidates)
    af.draw_without_repeating_a_group(candidates, 2, rng)
    assert candidates == before


# --------------------------------------------------------------------------
# The rule has to stay satisfiable as the pool grows
# --------------------------------------------------------------------------

def test_every_slot_offers_enough_distinct_groups_to_fill_its_slots():
    """Counting affixes cannot see this. A slot could offer four prefixes with
    one group between them, and the second prefix slot would be unfillable."""
    for slot in af.GEAR_SLOTS:
        assert af.distinct_groups_for(slot, af.PREFIX) >= af.PREFIXES_PER_PIECE
        assert af.distinct_groups_for(slot, af.SUFFIX) >= af.SUFFIXES_PER_PIECE


def test_everything_for_a_slot_includes_all_four_kinds_of_affix():
    """A ring suffix can be a stat affix, a hybrid, an ailment or a resistance
    family. The group rule has to hold across all four rather than within each."""
    pool = af.everything_for("Ring", af.SUFFIX)
    kinds = {type(a) for a in pool}
    assert af.StatAffix in kinds
    assert af.HybridAffix in kinds
    assert af.AilmentAffix in kinds
    assert af.AffixFamily in kinds


def test_a_weapon_offers_no_resistance_affix():
    """Resistance does not roll on weapons, so a weapon suffix draw must not be
    offered one."""
    pool = af.everything_for("Weapon", af.SUFFIX)
    assert not any(isinstance(a, af.AffixFamily) for a in pool)


def test_no_prefix_position_is_offered_an_ailment_or_a_resistance():
    for slot in af.GEAR_SLOTS:
        for affix in af.everything_for(slot, af.PREFIX):
            assert not isinstance(affix, (af.AilmentAffix, af.AffixFamily)), (
                f"{slot} offers {affix.name} as a prefix")


def test_everything_for_rejects_an_unknown_slot_or_position():
    with pytest.raises(ValueError):
        af.everything_for("Cape", af.PREFIX)
    with pytest.raises(ValueError):
        af.everything_for("Chest", "infix")
