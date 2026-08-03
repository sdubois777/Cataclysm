"""Tests for the enemy stat blocks.

The structural rule this file is mostly guarding: RARITY scales magnitude and
ARCHETYPE supplies the profile. If those two layers ever start reaching into each
other, a Legendary Imp stops being a bigger Imp and starts being a different
creature, which is the thing the split exists to prevent.
"""

from __future__ import annotations

import pytest

from cataclysm_sim import combat, enemy_stats as es, scoring
from cataclysm_sim.character import DAMAGE_TYPES

ORDER = es.RARITY_ORDER

#: Stats that rarity is allowed to change.
MAGNITUDE = ("health", "damage_per_hit", "armor", "energy_shield")

#: Stats that rarity must NOT change. These come from the archetype.
PROFILE = ("attack_interval", "crit_chance", "crit_multiplier", "move_speed",
           "evasion")


def at_tier_eight(rarity: str, kind: str = "Baseline") -> es.EnemyStats:
    return es.stats_on_floor(rarity, 8, "Cataclysm", kind=kind)


# --------------------------------------------------------------------------
# The rarity ladder
# --------------------------------------------------------------------------

def test_the_rarity_list_matches_the_authoritative_one():
    """`scoring.RARITY_WEIGHTS` is a verified port. If a rarity were added or
    renamed there and not here, an enemy would have a score and no stats."""
    assert set(ORDER) == set(scoring.RARITY_WEIGHTS)
    assert len(ORDER) == 6


def test_the_rarity_step_is_the_distance_above_common():
    assert es.rarity_step("Common") == 0
    assert es.rarity_step("Cataclysm Boss") == 5
    assert [es.rarity_step(r) for r in ORDER] == list(range(6))


def test_an_unknown_rarity_is_rejected():
    with pytest.raises(ValueError, match="unknown rarity"):
        es.rarity_step("Rare")


def test_the_superseded_rare_tier_is_not_present():
    """The design document lists Rare; the authoritative model does not."""
    assert "Rare" not in ORDER


def test_magnitude_scales_with_the_score():
    small = es.stats_for("Boss", 1000.0, "Succubus")
    big = es.stats_for("Boss", 2000.0, "Succubus")
    for stat in MAGNITUDE:
        assert getattr(big, stat) == pytest.approx(2 * getattr(small, stat)), stat


@pytest.mark.parametrize("stat", MAGNITUDE)
def test_every_magnitude_stat_rises_strictly_with_rarity(stat):
    """Checked as a STRICT change between every neighbouring pair.

    Comparing against a sorted list allows ties, so a stat flattened to the same
    value at every rarity would pass a test named 'rises with rarity' -- and a
    flat stat is one that stopped distinguishing the rarities at all, which is
    exactly the failure worth catching.
    """
    # The baseline archetype has no shield, so use one that does.
    values = [getattr(at_tier_eight(r, "Succubus"), stat) for r in ORDER]
    # strict=False is required: the two lists differ in length by one, because
    # this pairs each rarity with the next.
    for lower, higher in zip(values, values[1:], strict=False):
        assert higher > lower, f"{stat} does not rise: {values}"


def test_an_enemy_always_has_at_least_one_health():
    """Guards against a zero-score enemy being unkillable through division."""
    assert es.stats_for("Common", 0.0).health >= 1.0


def test_a_negative_score_is_treated_as_zero():
    assert es.stats_for("Common", -500.0).damage_per_hit == 0.0


# --------------------------------------------------------------------------
# Rarity scales magnitude AND NOTHING ELSE
# --------------------------------------------------------------------------

@pytest.mark.parametrize("stat", PROFILE)
@pytest.mark.parametrize("kind", sorted(es.ARCHETYPES))
def test_rarity_does_not_touch_the_profile(stat, kind):
    """The whole point of the split. A Legendary Imp is a bigger Imp: it does
    not start critting more or moving differently because it is rarer."""
    values = {r: getattr(at_tier_eight(r, kind), stat) for r in ORDER}
    assert len(set(values.values())) == 1, f"{kind}'s {stat} varies: {values}"


@pytest.mark.parametrize("stat", PROFILE)
def test_the_profile_does_not_scale_with_the_score_either(stat):
    small = es.stats_for("Elite", 100.0, "Hellhound")
    big = es.stats_for("Elite", 10_000.0, "Hellhound")
    assert getattr(small, stat) == getattr(big, stat)


def test_a_caster_and_a_brawler_of_the_same_rarity_share_only_their_score():
    """The other half of the split: if archetype stopped mattering, every Elite
    enemy would be the same thing wearing a different name.

    Crit multiplier is deliberately not in this list. Two archetypes are allowed
    to agree on any single value -- the Succubus and the Brute both hit for 200%
    on a critical -- and demanding they differ everywhere would be testing an
    accident rather than the design.
    """
    succubus = at_tier_eight("Elite", "Succubus")
    brute = at_tier_eight("Elite", "Brute")
    assert succubus.score == brute.score
    for stat in MAGNITUDE + ("attack_interval", "crit_chance", "move_speed",
                             "evasion"):
        assert getattr(succubus, stat) != getattr(brute, stat), stat


def test_no_two_archetypes_are_the_same_creature():
    """The anti-collapse guard. Individual values may coincide; whole archetypes
    may not, or one of them is a duplicate nobody noticed."""
    def fingerprint(k: es.Archetype) -> tuple:
        return (k.health_share, k.damage_share, k.armor_share,
                k.energy_shield_fraction, k.attack_interval, k.crit_chance,
                k.crit_multiplier, k.move_speed, k.evasion,
                tuple(sorted(k.resistances.items())))

    seen: dict[tuple, str] = {}
    for kind in es.ARCHETYPES.values():
        key = fingerprint(kind)
        assert key not in seen, f"{kind.name} is identical to {seen.get(key)}"
        seen[key] = kind.name


# --------------------------------------------------------------------------
# The rarest things are frightening
# --------------------------------------------------------------------------

def test_damage_grows_enough_that_the_rarest_enemies_are_dangerous():
    """The project owner's correction: at 1.21 per step a Cataclysm Boss hit was
    2.8x a Common enemy's, which is not frightening. Nine is."""
    common, boss = at_tier_eight("Common"), at_tier_eight("Cataclysm Boss")
    assert boss.damage_per_hit / common.damage_per_hit > 6.0


def test_damage_per_second_rises_with_rarity_now():
    """It used to rise only 1.2x across the whole ladder, because attack interval
    rose with rarity and cancelled the damage growth out. Attack interval is the
    archetype's now, so nothing cancels it."""
    common, boss = at_tier_eight("Common"), at_tier_eight("Cataclysm Boss")
    assert boss.damage_per_second / common.damage_per_second > 6.0


def test_health_still_grows_faster_than_damage():
    """If both grew together the rarest enemies would be unkillable and lethal at
    once, which is a wall rather than a fight."""
    common, boss = at_tier_eight("Common"), at_tier_eight("Cataclysm Boss")
    health_ratio = boss.health / common.health
    damage_ratio = boss.damage_per_hit / common.damage_per_hit
    assert health_ratio > 2 * damage_ratio


def test_criticals_raise_average_damage_above_the_per_hit_figure():
    assert at_tier_eight("Boss", "Gatekeeper").average_damage_per_hit > \
        at_tier_eight("Boss", "Gatekeeper").damage_per_hit


def test_an_archetype_with_no_criticals_configured_still_crits_sometimes():
    """Every archetype carries a nonzero base, so `average_damage_per_hit` is
    never just `damage_per_hit` in disguise."""
    for kind in es.ARCHETYPES.values():
        assert kind.crit_chance > 0.0, kind.name
        assert kind.crit_multiplier > 100.0, kind.name


# --------------------------------------------------------------------------
# Overwhelm replaces the per-rarity penetration this file used to carry
# --------------------------------------------------------------------------

def test_the_stat_block_carries_no_penetration_of_its_own():
    """It used to. `combat.overwhelm` already strips the player's mitigation, so
    a second per-rarity number was the same mechanic written twice, at roughly
    double the size and disagreeing with the first."""
    assert not hasattr(at_tier_eight("Boss"), "penetration")


def test_overwhelm_produces_a_rarity_ladder_by_itself():
    """Which is why no per-rarity penetration is needed: `RARITY_WEIGHTS` already
    spaces the rarities apart in score, and Overwhelm reads the score gap."""
    width = scoring.tier_width(8)
    player = scoring.PLAYER_MAX_SCORES[8]
    stripped = [combat.overwhelm(player, at_tier_eight(r).score, width)
                for r in ORDER]
    for lower, higher in zip(stripped, stripped[1:], strict=False):
        assert higher > lower, f"Overwhelm does not rise with rarity: {stripped}"


def test_the_overwhelm_figures_quoted_in_the_design_documents_are_still_true():
    """`Cataclysm_GDD_v2.md` section IV and the 2026-08-03 entry in
    `DECISIONS.md` both quote these two numbers as the argument for why no
    per-rarity enemy penetration is needed. If `OVERWHELM_RATE` moved, that
    argument would quietly stop holding and the documents would be wrong."""
    width = scoring.tier_width(8)
    player = scoring.PLAYER_MAX_SCORES[8]

    def stripped(rarity: str) -> float:
        return combat.overwhelm(player, at_tier_eight(rarity).score, width) * 100

    assert stripped("Common") == pytest.approx(8.9, abs=0.05)
    assert stripped("Cataclysm Boss") == pytest.approx(21.4, abs=0.05)


def test_overwhelm_shrinks_as_the_player_out_powers_the_enemy():
    """The reason it is better than a fixed per-rarity figure, which punished a
    player forever no matter how well geared."""
    boss = at_tier_eight("Cataclysm Boss")
    width = scoring.tier_width(8)
    under = combat.overwhelm(boss.score - 500, boss.score, width)
    over = combat.overwhelm(boss.score + 500, boss.score, width)
    assert under > 0
    assert over == 0


# --------------------------------------------------------------------------
# Archetypes
# --------------------------------------------------------------------------

def test_every_enemy_the_design_document_names_is_here():
    """The vertical slice list in the game design document, section X."""
    named = {"Imp", "Succubus", "Hellhound", "Brute", "Corrupted Sentinel",
             "Abyssal Warden", "Gatekeeper"}
    assert named <= set(es.ARCHETYPES)


def test_an_unknown_archetype_is_rejected():
    with pytest.raises(ValueError, match="unknown archetype"):
        es.stats_for("Common", 1000.0, "Skeleton")


def test_the_baseline_is_an_average_enemy_and_not_a_creature():
    """It exists so the rarity ladder can be read on its own. If any multiplier
    drifted off 1 it would stop showing what rarity alone does."""
    assert es.BASELINE.health_share == 1.0
    assert es.BASELINE.damage_share == 1.0
    assert es.BASELINE.armor_share == 1.0
    assert es.BASELINE.resistances == {}


def test_an_enemy_is_named_by_its_rarity_and_its_archetype():
    assert at_tier_eight("Elite", "Brute").name == "Elite Brute"
    assert at_tier_eight("Elite").name == "Elite"


@pytest.mark.parametrize("faster, slower", [
    ("Hellhound", "Brute"), ("Imp", "Succubus"), ("Imp", "Brute"),
])
def test_the_design_documents_movement_descriptions_hold(faster, slower):
    """The design calls the Imp and the Hellhound fast and the Brute slow."""
    assert es.archetype(faster).move_speed > es.archetype(slower).move_speed


def test_the_corrupted_sentinel_does_not_move_at_all():
    """The design calls it stationary, which is a real stat and not flavour."""
    assert es.archetype("Corrupted Sentinel").move_speed == 0.0


def test_the_brute_is_the_armoured_one_and_the_imp_is_not():
    """The design calls the Brute heavily armored and the Imp weak."""
    brute = at_tier_eight("Elite", "Brute")
    imp = at_tier_eight("Elite", "Imp")
    assert brute.armor > imp.armor
    assert imp.armor == 0.0


def test_only_the_archetypes_meant_to_have_a_shield_have_one():
    """The design says most classes have no energy shield and it is given to
    those that thematically warrant it. The same applies to enemies: the caster
    and the construct have one, the animals and the brawlers do not."""
    with_shield = {k.name for k in es.ARCHETYPES.values()
                   if k.energy_shield_fraction > 0}
    assert with_shield == {"Succubus", "Corrupted Sentinel"}


def test_the_shield_is_a_fraction_of_that_enemys_own_health():
    succubus = at_tier_eight("Elite", "Succubus")
    assert succubus.energy_shield == pytest.approx(succubus.health * 0.50)
    assert succubus.effective_health == pytest.approx(
        succubus.health + succubus.energy_shield)


def test_an_enemy_without_a_shield_has_an_effective_health_of_just_health():
    brute = at_tier_eight("Elite", "Brute")
    assert brute.energy_shield == 0.0
    assert brute.effective_health == brute.health


# --------------------------------------------------------------------------
# Resistances
# --------------------------------------------------------------------------

def test_every_resistance_names_a_real_damage_type():
    """A typo would otherwise sit there silently resisting nothing."""
    for kind in es.ARCHETYPES.values():
        for damage_type in kind.resistances:
            assert damage_type in DAMAGE_TYPES, f"{kind.name}: {damage_type}"


def test_an_unknown_damage_type_is_rejected_when_asked_about():
    with pytest.raises(ValueError, match="unknown damage type"):
        at_tier_eight("Common", "Imp").resistance_to("Fire")


def test_a_damage_type_an_archetype_never_mentions_is_simply_unresisted():
    assert at_tier_eight("Common", "Imp").resistance_to("Void") == 0.0


@pytest.mark.parametrize("kind", sorted(es.ARCHETYPES))
def test_no_enemy_resists_or_is_weak_to_its_own_cataclysms_damage_type(kind):
    """The one hard rule about resistances.

    The design hands the player the damage type of the Cataclysm they are
    fighting, and in the first run they cannot obtain another. An enemy that
    resists that type is an unavoidable tax; one that is weak to it is an
    unmissable bonus. Neither is a decision, so the profile must not mention it
    in either direction.

    An earlier version gave every Demonic enemy 40% Demonic resistance, which
    was a flat 40% damage loss against every enemy in the first run.
    """
    e = at_tier_eight("Elite", kind)
    assert e.resistance_to(e.archetype.cataclysm) == 0.0


def test_the_import_time_check_on_that_rule_actually_fires():
    """A guard nobody has seen fail is a guard nobody should trust."""
    tainted = es.Archetype(name="Tainted", role="test",
                           resistances={"Demonic": 40.0})
    real = dict(es.ARCHETYPES)
    es.ARCHETYPES["Tainted"] = tainted
    try:
        with pytest.raises(AssertionError, match="own Cataclysm's damage type"):
            es._check_no_archetype_mentions_its_own_cataclysms_damage_type()
    finally:
        es.ARCHETYPES.clear()
        es.ARCHETYPES.update(real)


def test_what_an_enemy_resists_says_what_it_is_made_of():
    """The replacement rule. Spot-checked against the design's own descriptions
    rather than asserted in the abstract."""
    # Not alive, so what kills and sickens living things does little.
    sentinel = es.archetype("Corrupted Sentinel")
    assert sentinel.resistance_to("Death") > 0
    assert sentinel.resistance_to("Pestilence") > 0
    # "Heavily armored" turns blades. "Can be outmaneuvered" is a slow mind.
    brute = es.archetype("Brute")
    assert brute.resistance_to("War") > 0
    assert brute.resistance_to("Chaos") < 0
    # A creature of the mind is the exact inverse of that.
    succubus = es.archetype("Succubus")
    assert succubus.resistance_to("Chaos") > 0
    assert succubus.resistance_to("War") < 0


def test_swarm_fodder_resists_nothing_at_all():
    """The Imp is described as weak individually. It should die to whatever the
    player happens to have brought."""
    assert es.archetype("Imp").resistances == {}


def test_the_last_boss_has_no_weakness_so_there_is_no_cheap_answer_to_it():
    """And it is the one enemy in the vertical slice that gives the player's
    resistance penetration stat a target, since nothing else resists the damage
    type the player is given."""
    gatekeeper = es.archetype("Gatekeeper")
    assert all(r > 0 for r in gatekeeper.resistances.values())
    others = [k for k in es.ARCHETYPES.values() if k.name != "Gatekeeper"]
    assert all("Demonic" not in k.resistances for k in others)


def test_resistance_is_counted_when_reporting_what_gear_has_to_deliver():
    """Otherwise the figure would understate what a player needs against
    everything that resists their damage type."""
    gk = at_tier_eight("Cataclysm Boss", "Gatekeeper")
    raw = gk.effective_health / 30.0
    # The Gatekeeper resists every type except the one the player is given.
    assert es.player_damage_to_kill_in(gk, 30.0, "Demonic") == pytest.approx(raw)
    assert es.player_damage_to_kill_in(gk, 30.0, "Celestial") == pytest.approx(
        raw / 0.75)


def test_the_reported_figure_defaults_to_the_damage_type_the_player_is_given():
    """Rather than to whichever type happens to flatter the number."""
    warden = at_tier_eight("Herald", "Abyssal Warden")
    assert es.player_damage_to_kill_in(warden, 30.0) == pytest.approx(
        es.player_damage_to_kill_in(warden, 30.0, "Demonic"))


# --------------------------------------------------------------------------
# Placing an enemy on a floor
# --------------------------------------------------------------------------

def test_a_deeper_floor_produces_a_stronger_enemy():
    shallow = es.stats_on_floor("Common", 8, "Cataclysm", floor=5)
    deep = es.stats_on_floor("Common", 8, "Cataclysm", floor=50)
    assert deep.health > shallow.health


def test_a_higher_tier_produces_a_stronger_enemy():
    low = es.stats_on_floor("Common", 1, "Cataclysm")
    high = es.stats_on_floor("Common", 8, "Cataclysm")
    assert high.health > 10 * low.health


def test_dungeon_modifiers_make_an_enemy_harder():
    """Dungeon modifiers carry a weight that is summed into the score, which is
    how they reach the enemies inside. Enemy modifiers deliberately do not."""
    plain = es.stats_on_floor("Common", 8, "Cataclysm", modifier_score=0.0)
    modified = es.stats_on_floor("Common", 8, "Cataclysm", modifier_score=200.0)
    assert modified.health > plain.health


# --------------------------------------------------------------------------
# The player side is reported, not asserted
# --------------------------------------------------------------------------

def test_the_player_helpers_are_reporting_tools_with_no_targets_baked_in():
    """The enemy side is set on its own terms and gear is fitted to it, so
    nothing here should constrain a player number. An earlier version of this
    file asserted player survival targets directly, which is what kept producing
    conflicts with the gear work.
    """
    boss = at_tier_eight("Cataclysm Boss", "Gatekeeper")
    assert es.hits_to_kill_player(boss, 6330.0) > 0
    assert es.player_damage_to_kill_in(boss, 30.0) > 0


def test_mitigation_reduces_how_hard_an_enemy_hits_a_player():
    boss = at_tier_eight("Cataclysm Boss", "Gatekeeper")
    bare = es.hits_to_kill_player(boss, 6330.0, mitigation_fraction=0.0)
    armoured = es.hits_to_kill_player(boss, 6330.0, mitigation_fraction=0.5)
    assert armoured == pytest.approx(2 * bare)


def test_killing_an_enemy_faster_needs_proportionally_more_damage():
    boss = at_tier_eight("Cataclysm Boss", "Gatekeeper")
    assert es.player_damage_to_kill_in(boss, 10.0) == pytest.approx(
        3 * es.player_damage_to_kill_in(boss, 30.0))


def test_a_shielded_enemy_takes_more_damage_to_kill_than_its_health_alone():
    sentinel = at_tier_eight("Legendary", "Corrupted Sentinel")
    needed = es.player_damage_to_kill_in(sentinel, 30.0, "Celestial")
    ignoring_shield = sentinel.health / 30.0 / 1.25
    assert needed > ignoring_shield
