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


def test_blunt_is_better_against_an_armoured_target_only():
    armoured = plain(armor=2000.0, tier=1)
    bare = plain(armor=0.0)
    assert taken(hit(subtype="Blunt"), armoured) > taken(hit(), armoured)
    assert taken(hit(subtype="Blunt"), bare) == pytest.approx(taken(hit(), bare))


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
    meaning anything."""
    d = plain(armor=1_000_000.0, damage_reduction=90.0,
              resistances={"Demonic": 300.0}, tier=1)
    assert taken(hit(damage=1_000_000.0), d) > 0.0


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
