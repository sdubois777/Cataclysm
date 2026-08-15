"""Every legal loadout is worth the same Power Score and the same sockets.

There are four: one two-handed weapon, two one-handed weapons, a single
one-handed weapon, and a one-handed weapon with a Shield in the offhand. The
project owner settled on 2026-08-15 that a Shield counts "just like a second
one-handed weapon", which is issue #612.

The rule these guard is the one the design document states under Power Score: no
loadout may be worth free Power Score.
"""

from __future__ import annotations

import pytest

from cataclysm_sim import affixes as af
from cataclysm_sim import player_damage as pd
from cataclysm_sim import player_power as pp


# --------------------------------------------------------------------------
# Sockets and pieces
# --------------------------------------------------------------------------

def test_two_one_handed_items_give_the_sockets_one_two_hander_gives():
    assert pp.SOCKETS_PER_ONE_HANDED_ITEM * 2 == pp.SOCKETS_IN_BOTH_HANDS == 6


def test_what_the_hands_hold_is_one_piece_for_power_score():
    """All four loadouts count once, so none is worth free Power Score."""
    assert pp.GEAR_PIECES == 18


def test_filling_the_offhand_adds_a_piece_and_four_affix_slots():
    """A second weapon and a Shield are both real pieces, not exemptions."""
    assert af.GEAR_PIECES_WITH_AN_OFFHAND == af.GEAR_PIECES + 1 == 19
    assert af.TOTAL_AFFIX_SLOTS_WITH_AN_OFFHAND == af.TOTAL_AFFIX_SLOTS + 4 == 76


def test_the_constants_are_no_longer_named_only_for_dual_wielding():
    """They cover a Shield too, so a name mentioning only dual wielding
    described half of what they are. Renamed with issue #612."""
    assert not hasattr(af, "DUAL_WIELD_GEAR_PIECES")
    assert not hasattr(af, "DUAL_WIELD_TOTAL_AFFIX_SLOTS")


# --------------------------------------------------------------------------
# What each loadout is worth in a fight
# --------------------------------------------------------------------------

def test_a_shield_buys_defence_by_giving_up_a_second_weapons_damage():
    """The trade, stated as a number. A Shield adds no attack damage, so a
    one-hander with a Shield hits exactly as hard as that one-hander alone and
    strictly less hard than a pair."""
    alone = pd.damage_per_hit(8, "Axe")
    with_shield = pd.damage_per_hit(8, ("Axe", pd.OFFHAND))
    paired = pd.damage_per_hit(8, ("Axe", "Sword"))

    assert with_shield == pytest.approx(alone)
    assert with_shield < paired


def test_a_shield_is_the_only_held_item_that_grants_no_attack_damage():
    """Which is why it is the one thing a hand can hold that is bought for
    something other than a hit.

    Read off the bases rather than through `weapon_base_damage`, because a
    Shield on its own is not a legal loadout and that function refuses one.
    """
    def attack_damage(base):
        return sum(i.value for i in base.implicits if i.stat == "attack_damage")

    no_damage = [b.name for b in af.ITEM_BASES
                 if isinstance(b, af.WeaponBase) and attack_damage(b) == 0.0]
    assert no_damage == [pd.OFFHAND]


def test_a_shield_on_its_own_is_not_a_loadout():
    with pytest.raises(ValueError, match="holds no weapon"):
        pd.damage_per_hit(8, (pd.OFFHAND,))


def test_a_shield_grants_block_and_armor_and_no_weapon_does():
    shield = af.base_named(pd.OFFHAND).implicit_values()
    assert shield["block_chance"] > 0
    assert shield["armor"] > 0

    for base in af.ITEM_BASES:
        if not isinstance(base, af.WeaponBase) or base.name == pd.OFFHAND:
            continue
        granted = base.implicit_values()
        assert "block_chance" not in granted, base.name
        assert "armor" not in granted, base.name


def test_the_offhand_never_changes_how_fast_the_weapon_swings():
    """A shield is held, not swung, so it stays out of the rate average."""
    for weapon in ("Axe", "Dagger", "Sword", "Wand"):
        assert pd.attack_rate((weapon, pd.OFFHAND)) == \
            pytest.approx(pd.attack_rate(weapon))
