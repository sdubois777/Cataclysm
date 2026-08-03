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
                k.crit_multiplier, k.move_speed, k.evasion, k.resistance)

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
    assert es.BASELINE.resistance == 0.0


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

def test_an_enemy_has_one_resistance_and_not_one_per_damage_type():
    """Player damage is adaptive: a weapon deals one damage number rather than
    eight separate pools, so a per-type enemy profile would change no outcome.

    A version of this model gave every enemy eight figures. The project owner
    removed it for that reason.
    """
    for kind in es.ARCHETYPES.values():
        assert isinstance(kind.resistance, float), kind.name
        assert not hasattr(kind, "resistances"), (
            f"{kind.name} still carries a per-damage-type profile")
    assert not hasattr(at_tier_eight("Elite", "Brute"), "resistance_to")


def test_the_player_still_has_all_eight_resistances():
    """Only the ENEMY side collapsed to one figure. Eight Cataclysms attack the
    player, so the player's own eight are untouched by this."""
    from cataclysm_sim.character import RESISTANCE_STATS
    assert len(RESISTANCE_STATS) == len(DAMAGE_TYPES) == 8


@pytest.mark.parametrize("kind", sorted(es.ARCHETYPES))
def test_an_enemys_own_damage_type_is_one_of_the_eight(kind):
    """It says which of the player's eight resistances applies when this enemy
    hits them, so a typo would silently bypass every one of them."""
    assert es.archetype(kind).damage_type in DAMAGE_TYPES
    assert at_tier_eight("Elite", kind).damage_type in DAMAGE_TYPES


def test_the_import_time_check_on_that_actually_fires():
    """A guard nobody has seen fail is a guard nobody should trust."""
    real = dict(es.ARCHETYPES)
    es.ARCHETYPES["Tainted"] = es.Archetype(name="Tainted", role="test",
                                            cataclysm="Fire")
    try:
        with pytest.raises(AssertionError, match="not one of the eight"):
            es._check_every_archetype_deals_a_real_damage_type()
    finally:
        es.ARCHETYPES.clear()
        es.ARCHETYPES.update(real)


def test_no_enemy_can_resist_its_way_to_immunity():
    """The design says no combination of defensive layers reaches immunity.
    Enemy resistance is one unbounded number now, so nothing else caps it."""
    for kind in es.ARCHETYPES.values():
        assert kind.resistance < 70.0, kind.name


def test_that_immunity_check_actually_fires():
    real = dict(es.ARCHETYPES)
    es.ARCHETYPES["Tainted"] = es.Archetype(name="Tainted", role="test",
                                            resistance=95.0)
    try:
        with pytest.raises(AssertionError, match="the 70%"):
            es._check_no_enemy_can_become_immune()
    finally:
        es.ARCHETYPES.clear()
        es.ARCHETYPES.update(real)


def test_swarm_fodder_resists_nothing_at_all():
    """The Imp is described as weak individually. It should die to whatever the
    player happens to have brought."""
    assert es.archetype("Imp").resistance == 0.0


def test_the_enemy_the_design_calls_highly_resistant_is_the_most_resistant():
    """The design describes the Abyssal Warden, and only the Abyssal Warden, as
    having high damage resistance."""
    warden = es.archetype("Abyssal Warden")
    others = [k for k in es.ARCHETYPES.values() if k.name != "Abyssal Warden"]
    assert all(warden.resistance > k.resistance for k in others)


def test_resistance_is_counted_when_reporting_what_gear_has_to_deliver():
    """Otherwise the figure would understate what a player needs against
    anything that resists."""
    warden = at_tier_eight("Herald", "Abyssal Warden")
    raw = warden.effective_health / 30.0
    assert es.player_damage_to_kill_in(warden, 30.0) == pytest.approx(
        raw / 0.65)
    unresisted = at_tier_eight("Common", "Imp")
    assert es.player_damage_to_kill_in(unresisted, 30.0) == pytest.approx(
        unresisted.effective_health / 30.0)


def test_player_penetration_cuts_into_enemy_resistance():
    """Which is what gives the player's resistance penetration stat a target on
    the enemy side."""
    warden = at_tier_eight("Herald", "Abyssal Warden")
    assert warden.damage_taken_fraction(0.0) == pytest.approx(0.65)
    assert warden.damage_taken_fraction(20.0) == pytest.approx(0.85)
    assert es.player_damage_to_kill_in(warden, 30.0, penetration=20.0) < \
        es.player_damage_to_kill_in(warden, 30.0)


def test_penetration_beyond_an_enemys_resistance_does_not_grant_bonus_damage():
    """Otherwise over-stacking penetration would turn into a damage multiplier
    against the enemies that need it least."""
    warden = at_tier_eight("Herald", "Abyssal Warden")
    assert warden.damage_taken_fraction(35.0) == pytest.approx(1.0)
    assert warden.damage_taken_fraction(200.0) == pytest.approx(1.0)
    imp = at_tier_eight("Common", "Imp")
    assert imp.damage_taken_fraction(50.0) == pytest.approx(1.0)


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
    assert sentinel.energy_shield > 0
    needed = es.player_damage_to_kill_in(sentinel, 30.0)
    # What the same enemy would need if its shield absorbed nothing.
    ignoring_shield = sentinel.health / 30.0 / sentinel.damage_taken_fraction()
    assert needed > ignoring_shield
