"""Tests for the three Demonic class stat lines."""

from __future__ import annotations

import pytest

from cataclysm_sim import character as ch
from cataclysm_sim import classes as cl


# --------------------------------------------------------------------------
# The set of classes
# --------------------------------------------------------------------------

def test_the_three_demonic_classes_are_defined():
    """The vertical slice needs all three, because a damage type unlocks all
    three of its class trees."""
    assert set(cl.DEMONIC_CLASSES) == {"Ravager", "Ritualist", "Masochist"}


def test_no_class_overrides_a_stat_that_is_not_on_the_character_sheet():
    for definition in cl.DEMONIC_CLASSES.values():
        assert set(definition.overrides) <= set(ch.ALL_STATS)


def test_no_class_overrides_a_stat_whose_base_is_not_its_to_give():
    """A class cannot set critical strike chance or attack speed: those bases
    come from the skill and the weapon. An override there would silently do
    nothing, which is worse than being rejected."""
    for name, definition in cl.DEMONIC_CLASSES.items():
        for stat in definition.overrides:
            assert ch.BASE_SOURCE[stat] == "class", (
                f"{name} overrides {stat}, whose base comes from the "
                f"{ch.BASE_SOURCE[stat]}")


def test_every_class_leaves_most_of_the_sheet_at_the_default():
    """The three designed War trees each commit to a handful of stats and ignore
    the rest. A class that overrode everything would have no identity."""
    for name, definition in cl.DEMONIC_CLASSES.items():
        assert len(definition.overrides) <= 12, (
            f"{name} overrides {len(definition.overrides)} of "
            f"{len(ch.ALL_STATS)} stats, which is not a identity but a rewrite")
        assert len(definition.overrides) >= 4, f"{name} barely differs"


# --------------------------------------------------------------------------
# Each class is distinguishable from the other two
# --------------------------------------------------------------------------

def _at_100(name, **kwargs):
    return ch.Character(cl.DEMONIC_CLASSES[name], level=100, **kwargs)


def test_no_two_classes_have_the_same_stat_line():
    lines = {}
    for name, definition in cl.DEMONIC_CLASSES.items():
        lines[name] = tuple(sorted(
            (s, definition.scaling(s).base, definition.scaling(s).per_level)
            for s in ch.ALL_STATS))
    assert len(set(lines.values())) == len(lines)


def test_the_ritualist_is_the_only_one_with_an_energy_shield():
    """The project owner's rule: energy shield goes to classes that thematically
    warrant it, such as casters. The Ritualist is the caster of the three."""
    assert _at_100("Ritualist").stat("max_energy_shield") > 0
    assert _at_100("Ravager").stat("max_energy_shield") == 0
    assert _at_100("Masochist").stat("max_energy_shield") == 0


def test_the_ritualist_has_the_largest_mana_pool_and_the_least_health():
    mana = {n: _at_100(n).stat("max_mana") for n in cl.DEMONIC_CLASSES}
    health = {n: _at_100(n).stat("max_health") for n in cl.DEMONIC_CLASSES}
    assert max(mana, key=mana.get) == "Ritualist"
    assert min(health, key=health.get) == "Ritualist"


def test_the_ravager_is_the_most_armoured_and_the_fastest():
    armor = {n: _at_100(n).stat("armor") for n in cl.DEMONIC_CLASSES}
    speed = {n: _at_100(n).stat("movement_speed") for n in cl.DEMONIC_CLASSES}
    assert max(armor, key=armor.get) == "Ravager"
    assert max(speed, key=speed.get) == "Ravager"


def test_the_ravager_is_the_only_one_that_leeches():
    """Sustain is how a frontline class holds a line without a tank's defences."""
    assert _at_100("Ravager").stat("life_leech") > 0
    assert _at_100("Ritualist").stat("life_leech") == 0
    assert _at_100("Masochist").stat("life_leech") == 0


def test_the_masochist_has_the_most_health_and_the_most_regeneration():
    health = {n: _at_100(n).stat("max_health") for n in cl.DEMONIC_CLASSES}
    regen = {n: _at_100(n).stat("health_regen") for n in cl.DEMONIC_CLASSES}
    assert max(health, key=health.get) == "Masochist"
    assert max(regen, key=regen.get) == "Masochist"


def test_the_masochist_is_the_only_one_that_retaliates():
    """The counterattack half of 'converts received damage into buffs and
    counterattacks'."""
    assert _at_100("Masochist").stat("retaliation") > 0
    assert _at_100("Ravager").stat("retaliation") == 0
    assert _at_100("Ritualist").stat("retaliation") == 0


def test_no_demonic_class_has_any_evasion():
    """None of the three avoids damage. The Ravager absorbs it, the Masochist
    wants it, and the Ritualist should not be in range of it."""
    for name in cl.DEMONIC_CLASSES:
        assert _at_100(name).stat("evasion") == 0, f"{name} evades"


# --------------------------------------------------------------------------
# The Masochist's identity, which is the one the design states mechanically
# --------------------------------------------------------------------------

def test_the_masochist_keeps_a_normal_mana_pool():
    """'Uses HP instead of mana' is delivered by a passive tree node converting
    mana into health, not by the class starting without mana. Until that node is
    taken it is an ordinary mana user."""
    masochist = _at_100("Masochist")
    generic = ch.Character(ch.GENERIC, level=100)
    assert masochist.stat("max_mana") == pytest.approx(generic.stat("max_mana"))
    assert masochist.stat("max_mana") > 0


def test_the_masochist_regenerates_enough_health_to_act_on_it():
    """Health doubles as the resource after the tree's conversion, so
    regeneration has to be large enough to pay for abilities. At level 100 it
    should restore at least 1% of the class's own base health per second."""
    definition = cl.DEMONIC_CLASSES["Masochist"]
    health = definition.base_at("max_health", 100)
    regen = definition.base_at("health_regen", 100)
    assert regen / health >= 0.01


def test_the_masochist_takes_no_energy_shield_deliberately():
    """A shield absorbs damage before health, and this class converts damage it
    receives. The zero is a decision, so it is asserted rather than assumed."""
    assert "max_energy_shield" not in cl.DEMONIC_CLASSES["Masochist"].overrides
    assert ch.DEFAULT_STAT_LINE["max_energy_shield"].base == 0.0


# --------------------------------------------------------------------------
# Class resources
# --------------------------------------------------------------------------

def test_every_class_has_a_resource_pool_but_none_defines_behaviour():
    """What a resource does belongs with the passive trees in #63. Only the pool
    size is set here."""
    for name in cl.DEMONIC_CLASSES:
        assert _at_100(name).stat("class_resource") > 0


def test_resource_pools_stay_in_the_range_the_one_designed_resource_uses():
    """Resolve, the only resource with designed values, never exceeds 100 in the
    Bulwark tree. Nothing here should be wildly outside that."""
    for name in cl.DEMONIC_CLASSES:
        assert 50 <= _at_100(name).stat("class_resource") <= 200


# --------------------------------------------------------------------------
# Attributes reach what each class cares about
# --------------------------------------------------------------------------

@pytest.mark.parametrize("name, attribute, stat", [
    ("Ravager", "constitution", "armor"),
    ("Ravager", "vitality", "max_health"),
    ("Ravager", "agility", "movement_speed"),
    ("Ritualist", "mind", "max_mana"),
    ("Ritualist", "spirit", "max_energy_shield"),
    ("Ritualist", "efficacy", "area_of_effect"),
    ("Masochist", "vitality", "max_health"),
    ("Masochist", "vitality", "health_regen"),
])
def test_each_class_has_a_base_for_the_stats_its_attributes_should_scale(
        name, attribute, stat):
    """An attribute only scales, so a class that wants an attribute to matter
    must give that stat a base. This checks the intended pairings actually
    produce a difference."""
    plain = _at_100(name)
    invested = _at_100(name, attributes=ch.Attributes(**{attribute: 100}))
    assert invested.stat(stat) > plain.stat(stat), (
        f"{attribute} does nothing for {name}'s {stat}; the class needs a "
        "non-zero base for it")


def test_spirit_does_nothing_for_the_two_classes_without_shields():
    """The other side of the same rule, and deliberate rather than an oversight:
    a Ravager or Masochist putting points into Spirit is wasting them."""
    for name in ("Ravager", "Masochist"):
        invested = _at_100(name, attributes=ch.Attributes(spirit=100))
        assert invested.stat("max_energy_shield") == 0


# --------------------------------------------------------------------------
# Nothing here is calibrated against the Bulwark
# --------------------------------------------------------------------------

def test_no_class_here_approaches_the_bulwark_tree_thresholds():
    """The project owner's direction: the Bulwark is a tank intended to be
    extremely tanky, and its numbers are not a scale for other classes. None of
    these three should be near its 20,000 endgame threshold from base values,
    gear-free and tree-free."""
    for name in cl.DEMONIC_CLASSES:
        c = _at_100(name, attributes=ch.Attributes(vitality=100))
        assert c.stat("max_health") < 10_000
