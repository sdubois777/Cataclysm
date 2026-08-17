"""Tests for the proposed damage calculation."""

from __future__ import annotations

import pytest

from cataclysm_sim import damage as dm


def plain(**kwargs) -> dm.Defender:
    return dm.Defender(health=10_000.0, **kwargs)


def hit(damage: float = 1000.0, **kwargs) -> dm.Attacker:
    return dm.Attacker(damage=damage, **kwargs)


def taken(attacker, defender) -> float:
    return dm.resolve(attacker, defender,
                      force_evade=False, force_block=False).dealt_to_health


# --------------------------------------------------------------------------
# Evasion and block, which the design already settled
# --------------------------------------------------------------------------

def test_evasion_avoids_a_direct_attack_completely():
    d = plain(evasion=100.0)
    r = dm.resolve(hit(), d, force_block=False)
    assert r.evaded
    assert r.dealt_to_health == 0.0


def test_evasion_does_nothing_against_area_damage():
    """Area damage lands regardless of evasion. This is why evasion's cap can be
    soft: even at 100% a character is not immune."""
    d = plain(evasion=100.0)
    r = dm.resolve(hit(is_area=True), d, force_block=False)
    assert not r.evaded
    assert r.dealt_to_health == pytest.approx(1000.0)


def test_a_block_removes_exactly_half_the_hit():
    d = plain()
    blocked = dm.resolve(hit(), d, force_evade=False, force_block=True)
    assert blocked.blocked
    assert blocked.dealt_to_health == pytest.approx(500.0)


def test_block_applies_to_area_damage():
    """Unlike evasion. A raised shield helps against an explosion."""
    d = plain()
    r = dm.resolve(hit(is_area=True), d, force_block=True)
    assert r.dealt_to_health == pytest.approx(500.0)


def test_full_block_chance_is_not_immunity():
    """A block halves a hit rather than preventing it, which is why block chance
    needs no cap."""
    d = plain(block_chance=100.0)
    assert dm.average_damage_taken(hit(), d) == pytest.approx(500.0)


# --------------------------------------------------------------------------
# Armor
# --------------------------------------------------------------------------

def test_armor_never_reaches_full_immunity():
    for armor in (1_000, 100_000, 10_000_000):
        assert dm.armor_reduction(armor, tier=1) <= dm.ARMOR_REDUCTION_CAP
        assert dm.armor_reduction(armor, tier=1) < 100.0


def test_armor_has_diminishing_returns():
    """The first points of armor are worth the most. Doubling armor must not
    double the reduction, or armor stacking runs away."""
    first = dm.armor_reduction(500, tier=1)
    second = dm.armor_reduction(1000, tier=1)
    assert second > first
    assert second < 2 * first


def test_the_same_armor_is_worth_less_at_a_higher_tier():
    """Otherwise armor earned early keeps its value forever and gear stops
    mattering."""
    values = [dm.armor_reduction(371, tier=t) for t in range(1, 9)]
    assert values == sorted(values, reverse=True)
    assert values[0] > 4 * values[-1]


def test_zero_and_negative_armor_reduce_nothing():
    assert dm.armor_reduction(0, tier=1) == 0.0
    assert dm.armor_reduction(-500, tier=1) == 0.0


# --------------------------------------------------------------------------
# Resistance and penetration, the load-bearing choice
# --------------------------------------------------------------------------

def test_penetration_is_applied_before_the_cap_so_over_capping_is_worth_something():
    """The single most important rule here. A defender at exactly the cap and
    one over-capped must NOT end up identical after penetration, or every point
    above 70 is wasted and the design's own allowance for over-capping via
    affixes means nothing."""
    at_cap = dm.effective_resistance(70.0, penetration=30.0)
    over_capped = dm.effective_resistance(100.0, penetration=30.0)
    assert at_cap == pytest.approx(40.0)
    assert over_capped == pytest.approx(70.0)
    assert over_capped > at_cap


def test_resistance_is_capped_at_seventy_without_penetration():
    assert dm.effective_resistance(200.0, penetration=0.0) == pytest.approx(70.0)
    assert dm.effective_resistance(45.0, penetration=0.0) == pytest.approx(45.0)


def test_negative_resistance_means_taking_extra_damage():
    """Several enchantments inflict this deliberately."""
    d = plain(resistances={"Demonic": -50.0})
    assert taken(hit(), d) == pytest.approx(1500.0)


def test_negative_resistance_is_bounded():
    assert dm.effective_resistance(-9999.0, 0.0) == dm.RESISTANCE_FLOOR


def test_penetration_past_a_targets_resistance_grants_nothing():
    """ISSUE #482. The design document forbids over-stacked penetration becoming
    a damage multiplier: "Penetration beyond an enemy's resistance grants no
    bonus, so over-stacking it does not become a damage multiplier against the
    enemies that need it least."

    Written against the Abyssal Warden's 35%, which is the worked example the
    document itself uses and the highest resistance in the vertical slice. Before
    the fix, 50 penetration gave -15% effective resistance and 115% of a hit."""
    for penetration in (35.0, 50.0, 80.0, 200.0):
        assert dm.effective_resistance(35.0, penetration) == pytest.approx(0.0)

    d = plain(resistances={"Demonic": 35.0})
    assert taken(hit(penetration=35.0), d) == pytest.approx(1000.0)
    assert taken(hit(penetration=200.0), d) == pytest.approx(1000.0)


def test_penetration_is_worth_less_once_it_passes_the_target():
    """The property that makes it a defence-stripping stat rather than a scaling
    one: each point up to the target's resistance is worth the same, and every
    point past it is worth nothing."""
    steps = [dm.effective_resistance(35.0, p) for p in (0.0, 10.0, 20.0, 30.0)]
    gaps = [steps[i] - steps[i + 1] for i in range(len(steps) - 1)]
    assert all(gap == pytest.approx(10.0) for gap in gaps)
    assert dm.effective_resistance(35.0, 40.0) == dm.effective_resistance(35.0, 35.0)


def test_penetration_does_not_deepen_a_natively_negative_resistance():
    """The case that stops this being a clamp at zero. An enchantment can push a
    target's resistance below zero and that target should take extra damage; what
    the rule forbids is penetration MANUFACTURING that state. So a target already
    at -25% stays at -25% however much penetration is thrown at it, and the floor
    still bounds how far a native negative can go."""
    assert dm.effective_resistance(-25.0, 0.0) == pytest.approx(-25.0)
    assert dm.effective_resistance(-25.0, 60.0) == pytest.approx(-25.0)

    d = plain(resistances={"Demonic": -25.0})
    assert taken(hit(), d) == pytest.approx(1250.0)
    assert taken(hit(penetration=60.0), d) == pytest.approx(1250.0)


def test_penetration_only_affects_the_matching_damage_type():
    d = plain(resistances={"Demonic": 70.0, "Void": 70.0})
    demonic = taken(hit(damage_type="Demonic", penetration=70.0), d)
    void = taken(hit(damage_type="Void"), d)
    assert demonic == pytest.approx(1000.0)
    assert void == pytest.approx(300.0)


# --------------------------------------------------------------------------
# Energy shield
# --------------------------------------------------------------------------

def test_energy_shield_absorbs_before_health():
    d = plain(energy_shield=400.0)
    r = dm.resolve(hit(), d, force_evade=False, force_block=False)
    assert r.absorbed_by_shield == pytest.approx(400.0)
    assert r.dealt_to_health == pytest.approx(600.0)


def test_a_shield_larger_than_the_hit_stops_all_of_it():
    d = plain(energy_shield=5000.0)
    r = dm.resolve(hit(), d, force_evade=False, force_block=False)
    assert r.dealt_to_health == 0.0
    assert r.absorbed_by_shield == pytest.approx(1000.0)


def test_hits_to_kill_depletes_the_shield_instead_of_reusing_it():
    """A shield is spent once. Dividing the combined pool by one hit's damage
    treats it as absorbing its whole value again on every hit, which made the
    Ritualist look three times tougher than it is."""
    d = dm.Defender(health=1000.0, energy_shield=1000.0)
    actual = dm.hits_to_kill(hit(damage=500.0), d)

    # The naive figure: pool 2000, per-hit damage after a 1000 shield absorbs
    # all 500, which is zero, so it would never die at all.
    assert actual == pytest.approx(4.0)


def test_a_shield_only_delays_death_it_does_not_prevent_it():
    d = dm.Defender(health=1000.0, energy_shield=100_000.0)
    assert dm.hits_to_kill(hit(damage=500.0), d) > 100


# --------------------------------------------------------------------------
# Weapon sub-types
# --------------------------------------------------------------------------

def test_piercing_ignores_a_share_of_armor():
    d = plain(armor=2000.0, tier=1)
    normal = taken(hit(), d)
    piercing = taken(hit(subtype="Piercing"), d)
    assert piercing > normal

    # Specifically, it faces 80% of the armor.
    expected = 1000.0 * (1 - dm.armor_reduction(2000.0 * 0.8, 1) / 100.0)
    assert piercing == pytest.approx(expected)


def test_slashing_is_better_against_health():
    d = plain()
    assert taken(hit(subtype="Slashing"), d) == pytest.approx(1100.0)


def test_magic_is_better_against_energy_shield():
    """It strips more shield per hit than an ordinary weapon does."""
    d = plain(energy_shield=10_000.0)
    ordinary = dm.resolve(hit(), d, force_evade=False, force_block=False)
    magic = dm.resolve(hit(subtype="Magic"), d, force_evade=False, force_block=False)
    assert magic.absorbed_by_shield > ordinary.absorbed_by_shield
    assert magic.absorbed_by_shield == pytest.approx(1100.0)


def test_magic_does_not_destroy_more_raw_damage_than_the_hit_contained():
    """The shield bonus must not let a 1,000 damage hit remove 1,100 of a
    defender's effective pool while also dealing damage to health."""
    d = plain(energy_shield=200.0)
    r = dm.resolve(hit(subtype="Magic"), d, force_evade=False, force_block=False)
    consumed_raw = 200.0 / 1.1
    assert r.dealt_to_health == pytest.approx(1000.0 - consumed_raw)


def test_blunt_deals_no_bonus_damage_at_all():
    """Blunt used to do 10% more damage versus armor, which put it in direct
    competition with piercing while having no affix family to scale it. It now
    stuns instead, and does ordinary damage."""
    armoured = plain(armor=2000.0, tier=1)
    bare = plain(armor=0.0)
    assert taken(hit(subtype="Blunt"), armoured) == pytest.approx(taken(hit(), armoured))
    assert taken(hit(subtype="Blunt"), bare) == pytest.approx(taken(hit(), bare))


def test_only_blunt_carries_a_stun_chance():
    assert hit(subtype="Blunt").stun_chance() == pytest.approx(dm.BLUNT_STUN_CHANCE)
    for subtype in ("Piercing", "Slashing", "Magic", "None"):
        assert hit(subtype=subtype).stun_chance() == 0.0


def test_a_blunt_stun_uses_the_shortest_designed_duration():
    """Several War skills stun, for between 0.75 and 3 seconds. A weapon
    sub-type stunning on every hit must not outclass the skills whose entire
    purpose is stunning, so it uses the shortest of those durations."""
    assert dm.INCIDENTAL_STUN_SECONDS == 0.75
    r = dm.resolve(hit(subtype="Blunt"), plain(),
                   force_evade=False, force_block=False, force_stun=True)
    assert r.stunned
    assert r.stun_seconds == pytest.approx(0.75)


def test_a_hit_that_does_not_stun_records_no_duration():
    r = dm.resolve(hit(subtype="Blunt"), plain(),
                   force_evade=False, force_block=False, force_stun=False)
    assert not r.stunned
    assert r.stun_seconds == 0.0


def test_crowd_control_resistance_reduces_the_stun_chance_proportionally():
    """Proportionally rather than by subtraction, so a character at 100
    resistance cannot be stunned at all whatever the incoming chance."""
    blunt = hit(subtype="Blunt")
    assert dm.effective_stun_chance(blunt, plain()) == pytest.approx(10.0)
    assert dm.effective_stun_chance(
        blunt, plain(crowd_control_resistance=50.0)) == pytest.approx(5.0)
    assert dm.effective_stun_chance(
        blunt, plain(crowd_control_resistance=100.0)) == 0.0


def test_stun_chance_scales_with_gear():
    """The affix family blunt needs in order to scale. It does not exist yet;
    this is the hook for it. See issue #79."""
    assert hit(subtype="Blunt",
               bonus_stun_chance=15.0).stun_chance() == pytest.approx(25.0)
    # And gear alone can grant it to a weapon that is not blunt.
    assert hit(subtype="Slashing",
               bonus_stun_chance=15.0).stun_chance() == pytest.approx(15.0)


def test_stun_chance_cannot_exceed_certainty():
    assert hit(subtype="Blunt",
               bonus_stun_chance=500.0).stun_chance() == pytest.approx(100.0)


def test_an_evaded_hit_cannot_stun():
    """Nothing made contact."""
    r = dm.resolve(hit(subtype="Blunt"), plain(evasion=100.0), force_block=False)
    assert r.evaded
    assert not r.stunned


def test_a_blocked_hit_can_still_stun():
    """A block reduces damage rather than preventing contact, so the impact is
    still delivered.

    Deliberately does NOT pin the stun roll: forcing it would bypass the chance
    calculation and the test could not tell whether blocking suppresses stun.
    A certain stun chance is used instead, so the roll must succeed on its own.

    THE HIT IS LARGE ON PURPOSE, and it did not used to be. Until issue #216 any
    blocked hit could stun. The anti-stun-lock rule added a second condition --
    the damage that got through has to be at least
    `damage.STUN_DAMAGE_THRESHOLD` per cent of the defender's maximum health --
    and a block halves the damage, so a hit that only just cleared the threshold
    falls below it once blocked. That is the rule working, not blocking
    suppressing stun. This test keeps its original point by using a hit that is
    still above the threshold after being halved.
    """
    certain = hit(damage=4_000.0, subtype="Blunt", bonus_stun_chance=100.0)
    r = dm.resolve(certain, plain(), force_evade=False, force_block=True)
    assert r.blocked
    assert r.dealt_to_health >= 10_000.0 * dm.STUN_DAMAGE_THRESHOLD / 100.0
    assert r.stunned


def test_blocking_does_not_by_itself_stop_a_stun():
    """The claim the test above is really making, isolated from the threshold.

    Same attacker, same defender, blocked and unblocked. If blocking suppressed
    stun outright rather than only by reducing damage, these two would differ.
    """
    certain = hit(damage=4_000.0, subtype="Blunt", bonus_stun_chance=100.0)
    blocked = dm.resolve(certain, plain(), force_evade=False, force_block=True)
    clean = dm.resolve(certain, plain(), force_evade=False, force_block=False)
    assert blocked.blocked and not clean.blocked
    assert blocked.stunned == clean.stunned is True


def test_the_stun_roll_respects_the_chance_without_being_forced():
    """Guards the same gap the other way round: a zero chance must never stun,
    and a certain one must always stun, with no roll pinned."""
    always = hit(subtype="Blunt", bonus_stun_chance=100.0)
    never = hit(subtype="Slashing")
    for _ in range(50):
        assert dm.resolve(always, plain(), force_evade=False,
                          force_block=False).stunned
        assert not dm.resolve(never, plain(), force_evade=False,
                              force_block=False).stunned


def test_an_unknown_weapon_subtype_is_rejected():
    with pytest.raises(ValueError, match="unknown weapon sub-type"):
        dm.Attacker(damage=100.0, subtype="Explosive")


def test_negative_damage_is_rejected():
    with pytest.raises(ValueError):
        dm.Attacker(damage=-5.0)


# --------------------------------------------------------------------------
# Order of operations
# --------------------------------------------------------------------------

def test_block_is_applied_before_armor_not_after():
    """Halving first and then reducing gives the same result as reducing then
    halving, so this checks the recorded intermediate values instead, which is
    what makes the order inspectable."""
    d = plain(armor=1000.0, tier=1)
    r = dm.resolve(hit(), d, force_evade=False, force_block=True)
    assert r.after_block == pytest.approx(500.0)
    assert r.after_armor < r.after_block


def test_each_layer_only_ever_reduces_damage_except_negative_resistance():
    d = plain(armor=1000.0, damage_reduction=20.0,
              resistances={"Demonic": 50.0}, tier=1)
    r = dm.resolve(hit(), d, force_evade=False, force_block=True)
    assert r.incoming >= r.after_block >= r.after_armor
    assert r.after_armor >= r.after_resistance >= r.after_reduction


def test_stacking_every_defence_still_lets_damage_through():
    """No combination of layers should reach immunity. If it does, a character
    that reaches the caps becomes unkillable and the difficulty system stops
    meaning anything.

    IT ASKS AT AND ABOVE 100 FLAT REDUCTION, WHICH IT DID NOT UNTIL ISSUE #644.
    This test pinned `damage_reduction` at 90 and passed, one step below the
    value that broke it: at exactly 100 the layer removed the whole hit and this
    assertion was False. So the one guard aimed at immunity passed because of the
    number it happened to choose rather than because anything stopped it.
    """
    for reduction in (90.0, 100.0, 1_000.0):
        d = plain(armor=1_000_000.0, damage_reduction=reduction,
                  resistances={"Demonic": 300.0}, tier=1)
        assert taken(hit(damage=1_000_000.0), d) > 0.0, (
            f"{reduction}% flat damage reduction with armour and resistance at "
            "their caps let nothing through, which is immunity")


def test_flat_damage_reduction_stops_rising_at_its_cap():
    """Past the cap, more of the stat buys nothing at all.

    That is what makes it a HARD cap in the design's caps table, as against
    resistance's soft one: over-capped resistance is worth having because
    penetration is subtracted before the cap, and nothing penetrates this layer.
    """
    at_cap = plain(damage_reduction=dm.DAMAGE_REDUCTION_CAP)
    over = plain(damage_reduction=dm.DAMAGE_REDUCTION_CAP + 25.0)
    far_over = plain(damage_reduction=1_000.0)

    assert taken(hit(damage=1_000.0), at_cap) == pytest.approx(250.0)
    assert taken(hit(damage=1_000.0), over) == pytest.approx(250.0)
    assert taken(hit(damage=1_000.0), far_over) == pytest.approx(250.0)

    # AND BELOW THE CAP IT STILL DOES EVERYTHING IT DID. A cap that also changed
    # the ordinary case would be a nerf wearing a cap's clothes; nothing
    # reachable from gear and a class base goes near 75.
    under = plain(damage_reduction=35.95)
    assert taken(hit(damage=1_000.0), under) == pytest.approx(640.5)


def test_the_cap_applies_to_the_layer_and_not_to_the_stat():
    """A character may hold more than the cap; the excess simply does nothing.

    The same shape as armour and resistance, whose caps sit on the percentage
    the layer removes rather than on the number the character carries. The
    engine says so in as many words in `CataclysmCombatAttributeSet.cpp`:
    "where they need bounding is in the damage calculation, against the final
    number, not against each contributing stat".
    """
    assert dm.effective_damage_reduction(20.0) == pytest.approx(20.0)
    assert dm.effective_damage_reduction(75.0) == pytest.approx(75.0)
    assert dm.effective_damage_reduction(99.0) == pytest.approx(75.0)
    assert dm.effective_damage_reduction(100.0) == pytest.approx(75.0)

    # Negative is floored rather than allowed to add damage. Unlike resistance,
    # which reaches -100 on purpose, nothing in the design grants a negative
    # here.
    assert dm.effective_damage_reduction(-40.0) == pytest.approx(0.0)


def test_the_full_order_is_recorded_step_by_step():
    r = dm.resolve(hit(), plain(armor=500.0, tier=1), force_evade=False,
                   force_block=False)
    for field in ("incoming", "after_block", "after_armor", "after_resistance",
                  "after_reduction", "absorbed_by_shield", "dealt_to_health"):
        assert hasattr(r, field), f"{field} is not recorded"
    assert r.total_mitigated == pytest.approx(r.incoming - r.dealt_to_health)


# --------------------------------------------------------------------------
# Against the real class stat lines
# --------------------------------------------------------------------------

def test_the_ritualist_is_the_frailest_of_the_three_demonic_classes():
    """The classes were designed with the Ritualist frailest. If the damage
    calculation reverses that, one of the two is wrong."""
    from cataclysm_sim.character import Attributes, Character
    from cataclysm_sim.classes import DEMONIC_CLASSES

    survival = {}
    for name, definition in DEMONIC_CLASSES.items():
        c = Character(definition, level=100, attributes=Attributes(vitality=100))
        d = dm.Defender(
            health=c.stat("max_health"),
            energy_shield=c.stat("max_energy_shield"),
            armor=c.stat("armor"),
            evasion=c.stat("evasion"),
            block_chance=c.stat("block_chance"),
            damage_reduction=c.stat("damage_reduction"),
            resistances={"Demonic": c.stat("resistance_demonic")},
            tier=8)
        survival[name] = dm.hits_to_kill(hit(), d)

    assert min(survival, key=survival.get) == "Ritualist"


def test_armor_and_energy_shield_both_change_the_outcome():
    """The whole point of issue #93: in the game today both stats are declared
    and inert, because nothing reads them."""
    bare = plain()
    armoured = plain(armor=2000.0, tier=1)
    shielded = plain(energy_shield=500.0)
    assert taken(hit(), armoured) < taken(hit(), bare)
    assert taken(hit(), shielded) < taken(hit(), bare)


# --------------------------------------------------------------------------
# What makes energy shield a distinct defence, read out of the enchantment data
# --------------------------------------------------------------------------

def test_energy_shield_ignores_damage_over_time_by_default():
    """`EnchantmentsNegative.csv` line 165 is "Energy shield can now be effected
    by bleed". It is a NEGATIVE enchantment, which is only a drawback if the
    shield normally is not affected. That proves the default."""
    d = plain(energy_shield=5000.0)
    r = dm.resolve(hit(is_damage_over_time=True), d,
                   force_evade=False, force_block=False)
    assert r.absorbed_by_shield == 0.0
    assert r.dealt_to_health == pytest.approx(1000.0)


def test_an_enchantment_can_take_that_immunity_away():
    d = plain(energy_shield=5000.0, shield_absorbs_damage_over_time=True)
    r = dm.resolve(hit(is_damage_over_time=True), d,
                   force_evade=False, force_block=False)
    assert r.absorbed_by_shield == pytest.approx(1000.0)
    assert r.dealt_to_health == 0.0


def test_energy_shield_still_absorbs_ordinary_hits():
    """The immunity is specific to damage over time, not to everything."""
    d = plain(energy_shield=5000.0)
    r = dm.resolve(hit(is_damage_over_time=False), d,
                   force_evade=False, force_block=False)
    assert r.absorbed_by_shield == pytest.approx(1000.0)


def test_energy_shield_is_not_simply_extra_health():
    """The point of the whole rule. A shielded character and one with the same
    total in health alone must behave differently against damage over time."""
    shielded = dm.Defender(health=1000.0, energy_shield=1000.0)
    healthy = dm.Defender(health=2000.0)
    dot = hit(damage=500.0, is_damage_over_time=True)
    assert dm.hits_to_kill(dot, shielded) < dm.hits_to_kill(dot, healthy)


# --------------------------------------------------------------------------
# Mana as a damage pool, which is a build choice rather than a default
# --------------------------------------------------------------------------

def test_mana_does_not_absorb_damage_by_default():
    d = plain(mana=5000.0)
    r = dm.resolve(hit(is_damage_over_time=True), d,
                   force_evade=False, force_block=False)
    assert r.absorbed_by_mana == 0.0
    assert r.dealt_to_health == pytest.approx(1000.0)


def test_mana_absorbs_damage_over_time_when_the_character_has_built_for_it():
    """`EnchantmentsPositive.csv` line 202: "DoTs deal damage to your mana pool
    first". A positive enchantment, so it is off by default."""
    d = plain(mana=5000.0, mana_absorbs_damage_over_time=True)
    r = dm.resolve(hit(is_damage_over_time=True), d,
                   force_evade=False, force_block=False)
    assert r.absorbed_by_mana == pytest.approx(1000.0)
    assert r.dealt_to_health == 0.0


def test_mana_only_absorbs_damage_over_time_not_ordinary_hits():
    """The enchantment names damage over time specifically."""
    d = plain(mana=5000.0, mana_absorbs_damage_over_time=True)
    r = dm.resolve(hit(is_damage_over_time=False), d,
                   force_evade=False, force_block=False)
    assert r.absorbed_by_mana == 0.0
    assert r.dealt_to_health == pytest.approx(1000.0)


def test_mana_absorbs_before_energy_shield():
    """Both can be active at once. Mana is the earlier step, so it takes the
    damage over time first and the shield only sees what is left."""
    d = plain(mana=400.0, energy_shield=5000.0,
              mana_absorbs_damage_over_time=True,
              shield_absorbs_damage_over_time=True)
    r = dm.resolve(hit(is_damage_over_time=True), d,
                   force_evade=False, force_block=False)
    assert r.absorbed_by_mana == pytest.approx(400.0)
    assert r.absorbed_by_shield == pytest.approx(600.0)
    assert r.dealt_to_health == 0.0


# --------------------------------------------------------------------------
# Armor penetration is a stat, separate from the weapon sub-type
# --------------------------------------------------------------------------

def test_armor_penetration_from_gear_stacks_with_piercing():
    """The enchantment tables treat ignoring armor and ignoring resistances as
    two different things, and grant armor-ignoring on skills, critical hits,
    traps and first hits. Piercing adds its 20% on top of whatever gear gives."""
    assert hit(armor_penetration=25.0).total_armor_ignored() == pytest.approx(25.0)
    assert hit(subtype="Piercing").total_armor_ignored() == pytest.approx(20.0)
    assert hit(subtype="Piercing",
               armor_penetration=25.0).total_armor_ignored() == pytest.approx(45.0)


def test_armor_penetration_cannot_exceed_all_of_the_armor():
    """"Your first hit against each enemy ignores all armor" is 100%, not more."""
    assert hit(subtype="Piercing",
               armor_penetration=200.0).total_armor_ignored() == pytest.approx(100.0)
    d = plain(armor=5000.0, tier=1)
    full = taken(hit(armor_penetration=100.0), d)
    bare = taken(hit(), plain(armor=0.0))
    assert full == pytest.approx(bare)


def test_armor_penetration_and_resistance_penetration_are_separate():
    """An attacker that ignores armor must not thereby ignore resistance."""
    d = plain(armor=3000.0, resistances={"Demonic": 70.0}, tier=1)
    r = dm.resolve(hit(armor_penetration=100.0), d,
                   force_evade=False, force_block=False)
    # Armor did nothing, but resistance still removed 70%.
    assert r.after_armor == pytest.approx(1000.0)
    assert r.after_resistance == pytest.approx(300.0)


# --------------------------------------------------------------------------
# Energy shield recharge: 3 seconds, restarted by any damage
# --------------------------------------------------------------------------

def test_the_shield_does_not_recharge_during_the_delay():
    assert dm.ENERGY_SHIELD_RECHARGE_DELAY == 3.0
    for elapsed in (0.0, 1.0, 2.9, 3.0):
        assert dm.shield_after_quiet_period(
            current_shield=0.0, max_shield=1000.0, regen_per_second=100.0,
            seconds_since_damage=elapsed) == 0.0


def test_the_shield_recharges_once_the_delay_has_passed():
    """Only the time beyond the delay counts, not the whole quiet period."""
    after = dm.shield_after_quiet_period(
        current_shield=0.0, max_shield=1000.0, regen_per_second=100.0,
        seconds_since_damage=5.0)
    assert after == pytest.approx(200.0)


def test_the_shield_never_recharges_above_its_maximum():
    assert dm.shield_after_quiet_period(
        current_shield=900.0, max_shield=1000.0, regen_per_second=100.0,
        seconds_since_damage=1000.0) == pytest.approx(1000.0)


def test_damage_inside_the_window_restarts_the_wait():
    """The rule that matters. A character hit every 2 seconds never recharges,
    because the wait restarts before it ever elapses."""
    steady = [(t, 100.0) for t in (0.0, 2.0, 4.0, 6.0, 8.0)]
    remaining = dm.shield_over_timeline(
        max_shield=1000.0, regen_per_second=100.0, events=steady, ends_at=8.0)
    assert remaining == pytest.approx(500.0)


def test_the_same_damage_spaced_out_does_recharge():
    """Same five hits, same total damage, spaced 4 seconds apart instead of 2.
    Each gap now clears the 3 second wait with 1 second of recharge left over,
    which at 100 per second exactly replaces each 100 damage hit."""
    spaced = [(t, 100.0) for t in (0.0, 4.0, 8.0, 12.0, 16.0)]
    remaining = dm.shield_over_timeline(
        max_shield=1000.0, regen_per_second=100.0, events=spaced, ends_at=16.0)
    steady = [(t, 100.0) for t in (0.0, 2.0, 4.0, 6.0, 8.0)]
    steady_remaining = dm.shield_over_timeline(
        max_shield=1000.0, regen_per_second=100.0, events=steady, ends_at=8.0)

    # 900, not 1000: the timeline ends at the instant of the last hit, so there
    # is no quiet period after it in which to recover that hit.
    assert remaining == pytest.approx(900.0)
    assert steady_remaining == pytest.approx(500.0)
    assert remaining > steady_remaining


def test_a_quiet_tail_after_the_last_hit_refills_the_shield():
    spaced = [(t, 100.0) for t in (0.0, 4.0, 8.0, 12.0, 16.0)]
    assert dm.shield_over_timeline(
        max_shield=1000.0, regen_per_second=100.0, events=spaced,
        ends_at=30.0) == pytest.approx(1000.0)


def test_a_single_hit_then_quiet_recharges_fully():
    remaining = dm.shield_over_timeline(
        max_shield=1000.0, regen_per_second=100.0,
        events=[(0.0, 600.0)], ends_at=20.0)
    assert remaining == pytest.approx(1000.0)


def test_damage_over_time_restarts_the_wait_even_though_the_shield_ignores_it():
    """The two rules together are what makes damage over time the answer to
    shield stacking: it bypasses the shield's protection and holds it empty.

    Without this, a bleeding character would refill their shield while the bleed
    ran, and shields would be strongest against exactly the damage they ignore.
    """
    assert dm.DAMAGE_OVER_TIME_DELAYS_RECHARGE is True
    assert dm.restarts_recharge(hit(is_damage_over_time=True))
    assert dm.restarts_recharge(hit(is_damage_over_time=False))


def test_a_shield_that_absorbed_the_whole_hit_still_waits_to_recharge():
    """Taking damage restarts the wait whether or not any of it reached health."""
    remaining = dm.shield_over_timeline(
        max_shield=1000.0, regen_per_second=100.0,
        events=[(0.0, 50.0), (2.0, 50.0)], ends_at=2.0)
    assert remaining == pytest.approx(900.0)


# --------------------------------------------------------------------------
# Chance to stun above certainty. Issues #298 and #639.
# --------------------------------------------------------------------------

def test_chance_to_stun_caps_at_certainty_and_the_rest_becomes_duration():
    """THE RULE, extended to stun by the project owner on 2026-08-16. It is the
    one they set for damage over time on 2026-08-03: chance caps at 100% and
    everything past it multiplies the magnitude. A stun has no damage, so its
    magnitude is its duration."""
    assert dm.stun_application(0.0) == (0.0, dm.INCIDENTAL_STUN_SECONDS)
    assert dm.stun_application(50.0) == (50.0, dm.INCIDENTAL_STUN_SECONDS)
    assert dm.stun_application(100.0) == (100.0, dm.INCIDENTAL_STUN_SECONDS)

    chance, seconds = dm.stun_application(200.0)
    assert chance == 100.0
    assert seconds == pytest.approx(dm.INCIDENTAL_STUN_SECONDS * 2)


def test_a_stun_never_lasts_as_long_as_the_immunity_window():
    """THE PROPERTY THE CAP EXISTS FOR, and it is arithmetic rather than taste.
    A stun lasting as long as the window means the window expires while the
    target is still held, so the next hit re-stuns and the target never acts --
    which is exactly the chain-stunning the window was added to stop."""
    for chance in (100.0, 400.0, 800.0, 100_000.0):
        _, seconds = dm.stun_application(chance)
        assert seconds <= dm.LONGEST_STUN_SECONDS
        assert seconds < dm.STUN_IMMUNITY_SECONDS

    # AND THE GAP IS REAL, not a rounding margin. A stunned target always gets at
    # least two seconds to act.
    assert dm.STUN_IMMUNITY_SECONDS - dm.LONGEST_STUN_SECONDS >= 2.0


def test_the_cap_is_the_longest_stun_the_design_already_has():
    """Three seconds is not a new number: it is the Brute's Heart ten-piece set
    bonus, the most expensive thing in the game to assemble. Reaching it needs
    400% chance."""
    assert dm.LONGEST_STUN_SECONDS == 3.0
    _, at_cap = dm.stun_application(400.0)
    assert at_cap == pytest.approx(dm.LONGEST_STUN_SECONDS)


def test_scaling_stops_dead_at_the_cap_and_that_is_deliberate():
    """Every other effect rolls over into duration once its magnitude caps, so
    its scaling never dies. A stun's magnitude IS its duration, so there is
    nothing to roll over into, and letting it run would break the window."""
    _, at_cap = dm.stun_application(400.0)
    _, far_past = dm.stun_application(4000.0)
    assert far_past == at_cap


def test_a_blunt_weapon_brings_its_own_chance_and_nothing_else_does():
    assert dm.Attacker(damage=100.0, subtype="Blunt").total_stun_chance() ==         dm.BLUNT_STUN_CHANCE
    assert dm.Attacker(damage=100.0, subtype="Slashing").total_stun_chance() == 0.0

    # AN AFFIX ADDS TO IT RATHER THAN REPLACING IT, which is what the project
    # owner meant by the affix including the effect of blunt.
    both = dm.Attacker(damage=100.0, subtype="Blunt", bonus_stun_chance=90.0)
    assert both.total_stun_chance() == 100.0


def test_crowd_control_resistance_bites_into_the_overflow_too():
    """A defender at 50 resistance facing 400% chance sees 200%, which is still
    certainty and still doubles the duration -- but from twice the investment.
    Reducing only the capped 100 would make resistance worth nothing at all
    against a heavy stun build, which is the opposite of what it is for."""
    attacker = dm.Attacker(damage=100.0, subtype="Blunt", bonus_stun_chance=390.0)
    tough = plain(crowd_control_resistance=50.0)

    chance, seconds = dm.stun_against(attacker, tough)
    assert chance == 100.0
    assert seconds == pytest.approx(dm.INCIDENTAL_STUN_SECONDS * 2)

    # And full resistance still means no stun at all, whatever is stacked.
    immune = plain(crowd_control_resistance=100.0)
    assert dm.stun_against(attacker, immune)[0] == 0.0


def test_a_stunning_hit_reports_the_scaled_duration():
    """The duration has to reach the Resolution, or the rule computes a number
    nothing uses."""
    # 290 FROM THE AFFIX PLUS BLUNT'S OWN 10 IS 300, so the duration trebles.
    # Blunt's chance is part of the total rather than separate from it, which is
    # what the project owner meant by the affix including the effect of blunt.
    attacker = dm.Attacker(damage=100_000.0, subtype="Blunt",
                           bonus_stun_chance=290.0)
    d = plain()
    r = dm.resolve(attacker, d, force_evade=False, force_block=False,
                   force_stun=True)
    assert r.stunned
    assert r.stun_seconds == pytest.approx(dm.INCIDENTAL_STUN_SECONDS * 3)


def test_a_hit_that_does_not_stun_reports_no_duration():
    attacker = dm.Attacker(damage=100_000.0, subtype="Blunt")
    r = dm.resolve(attacker, plain(), force_evade=False, force_block=False,
                   force_stun=False)
    assert not r.stunned
    assert r.stun_seconds == 0.0
