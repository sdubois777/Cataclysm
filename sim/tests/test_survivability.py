"""How long a geared character survives, measured rather than assumed.

Issue #108. Enemy damage only means something against what a player can actually
survive. These run real enemy hits through the real damage model against the
reference build in `cataclysm_sim/reference_build.py`, so the two enemy damage
constants cannot drift from the mitigation they were fitted against.

Every earlier figure on this assumed a flat "70% mitigation" rather than
computing one, and was wrong by a factor of twenty.
"""

from __future__ import annotations

import pytest

from cataclysm_sim import affixes as af
from cataclysm_sim import damage as dmg
from cataclysm_sim import enemy_stats as es
from cataclysm_sim import reference_build as rb
from cataclysm_sim.character import Attributes, Character
from cataclysm_sim.classes import DEMONIC_CLASSES

TIER = 8

#: The enemies a player actually meets, at the rarity each is normally met at.
MET_AT = (("Imp", "Common"), ("Hellhound", "Common"), ("Succubus", "Elite"),
          ("Brute", "Elite"), ("Corrupted Sentinel", "Legendary"),
          ("Abyssal Warden", "Herald"), ("Gatekeeper", "Cataclysm Boss"))


def enemy(name: str, rarity: str) -> es.EnemyStats:
    return es.stats_on_floor(rarity, TIER, "Cataclysm", kind=name)


def hits_to_kill_the_reference_build(e: es.EnemyStats) -> float:
    return dmg.hits_to_kill(
        dmg.Attacker(damage=e.average_damage_per_hit, damage_type=e.damage_type),
        rb.defender(TIER))


# --------------------------------------------------------------------------
# The build itself
# --------------------------------------------------------------------------

def test_the_reference_build_fits_in_the_slots_a_character_has():
    prefixes, suffixes = rb.spent_slots()
    available = af.GEAR_PIECES * af.PREFIXES_PER_PIECE
    assert prefixes <= available
    assert suffixes <= available


def test_the_slot_check_actually_fires():
    """A build quietly spending more slots than exist would flatter every number
    below it."""
    real = dict(rb.PREFIX_SPEND)
    rb.PREFIX_SPEND["FLAT_HEALTH"] = 90
    try:
        with pytest.raises(ValueError, match="prefix slots"):
            rb._check_the_build_fits_in_the_slots_it_has()
    finally:
        rb.PREFIX_SPEND.clear()
        rb.PREFIX_SPEND.update(real)


def test_the_build_spends_on_offence_as_well_as_defence():
    """A character that spent everything on staying alive would make these
    numbers meaningless, because no real build does that."""
    offensive = {"FLAT_DAMAGE", "INCREASED_DAMAGE"}
    assert offensive <= set(rb.PREFIX_SPEND)
    assert sum(rb.PREFIX_SPEND[k] for k in offensive) >= 8


def test_it_has_one_base_for_every_equipped_piece():
    assert len(rb.BASES) == af.GEAR_PIECES
    for name in rb.BASES:
        af.base_named(name)


# --------------------------------------------------------------------------
# Mitigation, which is the thing every earlier figure got wrong
# --------------------------------------------------------------------------

def test_four_mitigation_layers_multiply_to_roughly_a_tenth():
    """The measurement that made issue #108 answerable. Armour, resistance,
    block and flat reduction each look modest and compound to something that
    is not."""
    landed = rb.damage_taken_fraction(TIER)
    assert 0.05 < landed < 0.18, f"a hit lands for {landed:.1%}"


def test_every_mitigation_layer_actually_contributes():
    """If any one of them did nothing, the fitted enemy damage would be wrong by
    however much that layer was worth."""
    full = rb.defender(TIER)
    probe = dmg.Attacker(damage=10_000.0, damage_type="Demonic")
    baseline = dmg.average_damage_taken(probe, full)

    from dataclasses import replace
    for layer, off in (("armor", {"armor": 0.0}),
                       ("resistance", {"resistances": {}}),
                       ("block chance", {"block_chance": 0.0}),
                       ("damage reduction", {"damage_reduction": 0.0})):
        weaker = dmg.average_damage_taken(probe, replace(full, **off))
        assert weaker > baseline, f"{layer} contributes nothing"


def test_no_amount_of_gear_makes_the_reference_build_immune():
    """The design says no combination of layers reaches zero damage taken."""
    assert rb.damage_taken_fraction(TIER) > 0.0


# --------------------------------------------------------------------------
# The enemy side is held below the player side. Issue #483
# --------------------------------------------------------------------------
#
# `enemy_stats.ENEMY_MITIGATION_CEILING` states what the player stops, because
# that module cannot measure it: `reference_build` imports `affixes` and
# `affixes` imports `enemy_stats`, so importing it there would be a cycle. This
# file can import both, which is why the pin lives here rather than beside the
# constant.

def test_the_enemy_mitigation_ceiling_is_what_the_player_actually_stops():
    """Both directions matter. Above what the player stops and the rule "no
    enemy stops more than the player does" is not what is enforced; far below it
    and the ceiling has quietly become a different, stricter rule that nobody
    decided."""
    player_stops = 100.0 * (1.0 - rb.damage_taken_fraction(TIER))

    assert es.ENEMY_MITIGATION_CEILING < player_stops, (
        f"the ceiling is {es.ENEMY_MITIGATION_CEILING}% and the reference "
        f"character stops {player_stops:.2f}%, so an enemy at the ceiling would "
        "stop more than the player does")
    assert es.ENEMY_MITIGATION_CEILING > player_stops - 2.0, (
        f"the ceiling is {es.ENEMY_MITIGATION_CEILING}% and the reference "
        f"character stops {player_stops:.2f}%; the ceiling is meant to be that "
        "figure rounded down to a whole percent, not a stricter rule")


def test_the_player_stops_least_at_the_last_tier():
    """Which is why the ceiling is taken from tier 8. Armour is divided by
    800 x tier, so the same gear stops less as the tier rises, and taking the
    player's weakest is what makes the rule hold at every tier."""
    stopped = [100.0 * (1.0 - rb.damage_taken_fraction(t)) for t in range(1, 9)]
    assert stopped[-1] == min(stopped)
    assert stopped[0] > stopped[-1], "the tier would then not matter here"


def test_every_enemy_stops_less_than_the_reference_character_does():
    """The rule stated directly against the two live numbers rather than through
    the constant, so it still holds if the constant is wrong."""
    player_stops = 100.0 * (1.0 - rb.damage_taken_fraction(TIER))
    for name, rarity in MET_AT:
        stops = 100.0 * (1.0 - enemy(name, rarity).damage_taken_fraction())
        assert stops < player_stops, f"{rarity} {name} stops {stops:.1f}%"


# --------------------------------------------------------------------------
# What each enemy does to that character
# --------------------------------------------------------------------------

def test_the_rarest_enemy_kills_fastest_and_the_commonest_slowest():
    """Whatever the exact figures, the ordering has to hold."""
    imp = hits_to_kill_the_reference_build(enemy("Imp", "Common"))
    warden = hits_to_kill_the_reference_build(enemy("Abyssal Warden", "Herald"))
    boss = hits_to_kill_the_reference_build(enemy("Gatekeeper", "Cataclysm Boss"))
    assert boss < warden < imp


def test_the_last_boss_kills_a_geared_character_in_a_few_hits():
    """The project owner asked for the rarest enemies to be frightening. Before
    issue #108 this took 8 hits against a build that had spent half its slots on
    staying alive, which is not frightening."""
    hits = hits_to_kill_the_reference_build(
        enemy("Gatekeeper", "Cataclysm Boss"))
    assert 1 < hits <= 5, f"the Gatekeeper takes {hits:.0f} hits"


def test_the_mini_boss_is_dangerous_without_being_the_boss():
    hits = hits_to_kill_the_reference_build(enemy("Abyssal Warden", "Herald"))
    assert 3 <= hits <= 10, f"the Abyssal Warden takes {hits:.0f} hits"


def test_nothing_one_shots_a_geared_character():
    """A geared character should always get to react. Being one-shot is what the
    project owner asked for against a GEARLESS one, which is a different case."""
    for name, rarity in MET_AT:
        hits = hits_to_kill_the_reference_build(enemy(name, rarity))
        assert hits > 1, f"a {rarity} {name} one-shots a geared character"


def test_the_gearless_character_is_still_one_shot_by_the_last_boss():
    """Stated by the project owner. It has to stay true after damage was refitted
    to a geared character, or the refit went too far the other way."""
    bare = Character(DEMONIC_CLASSES["Ravager"], level=100,
                     attributes=Attributes(vitality=100))
    pool = bare.stat("max_health") + bare.stat("max_energy_shield")
    boss = enemy("Gatekeeper", "Cataclysm Boss")
    assert boss.average_damage_per_hit > pool


def test_a_single_common_enemy_is_not_the_threat():
    """Trash at tier 8 should not kill a geared character on its own. The 8-to-10
    hit target the project owner set early on is a PACK target: one Imp cannot
    both be trivial alone and lethal in a group of twenty."""
    for name in ("Imp", "Hellhound"):
        hits = hits_to_kill_the_reference_build(enemy(name, "Common"))
        assert hits > 15, f"one {name} takes only {hits:.0f} hits"


def test_a_pack_of_common_enemies_very_much_is_the_threat():
    """Ten Imps should kill a geared character in seconds, which is what makes
    the design's description of them -- weak individually, overwhelming in packs
    -- true rather than flavour."""
    imp = enemy("Imp", "Common")
    against = rb.defender(TIER)
    per_hit = dmg.average_damage_taken(
        dmg.Attacker(damage=imp.average_damage_per_hit,
                     damage_type=imp.damage_type), against)
    for pack, longest in ((10, 10.0), (20, 5.0)):
        seconds = against.health / (per_hit * pack / imp.attack_interval)
        assert seconds < longest, (
            f"{pack} Imps take {seconds:.1f} seconds to kill a geared character")


def test_an_elite_is_between_trash_and_a_mini_boss():
    for name in ("Succubus", "Brute"):
        hits = hits_to_kill_the_reference_build(enemy(name, "Elite"))
        assert 6 <= hits <= 20, f"an Elite {name} takes {hits:.0f} hits"


# --------------------------------------------------------------------------
# The fitting itself
# --------------------------------------------------------------------------

def test_enemy_damage_is_fitted_to_the_reference_build_and_says_so():
    """These two constants are the one place in `enemy_stats.py` set against the
    player rather than on the enemy's own terms. If that stops being written
    down, the next person to change them will not know what they were fitted to.
    """
    assert es.DAMAGE_AT_COMMON == pytest.approx(0.65)
    assert es.DAMAGE_PER_STEP == pytest.approx(1.40)
    assert "reference_build" in es.__doc__ or "108" in es.__doc__


def test_halving_enemy_damage_roughly_doubles_how_long_a_character_lives():
    """So the two constants are load-bearing rather than decorative.

    Compared against the real figure rather than a fixed threshold, because a
    fixed one only says what the numbers happen to be today.
    """
    boss = enemy("Gatekeeper", "Cataclysm Boss")
    full = hits_to_kill_the_reference_build(boss)
    halved = dmg.hits_to_kill(
        dmg.Attacker(damage=boss.average_damage_per_hit / 2,
                     damage_type=boss.damage_type),
        rb.defender(TIER))
    assert halved >= 2 * full - 1, (
        f"halving the hit took survival from {full:.0f} hits to {halved:.0f}")
