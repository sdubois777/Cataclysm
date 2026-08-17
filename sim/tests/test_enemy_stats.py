"""Tests for the enemy stat blocks.

The structural rule this file is mostly guarding: RARITY scales magnitude and
ARCHETYPE supplies the profile. If those two layers ever start reaching into each
other, a Legendary Imp stops being a bigger Imp and starts being a different
creature, which is the thing the split exists to prevent.
"""

from __future__ import annotations

import dataclasses

import pytest

from cataclysm_sim import combat, damage as dmg, enemy_stats as es, scoring
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
    small = es.stats_for("Boss", 1000.0, "Succubus", tier=8)
    big = es.stats_for("Boss", 2000.0, "Succubus", tier=8)
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
    assert es.stats_for("Common", 0.0, tier=8).health >= 1.0


def test_a_negative_score_is_treated_as_zero():
    assert es.stats_for("Common", -500.0, tier=8).damage_per_hit == 0.0


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
    small = es.stats_for("Elite", 100.0, "Hellhound", tier=8)
    big = es.stats_for("Elite", 10_000.0, "Hellhound", tier=8)
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

def test_the_damage_ladder_does_not_flatten():
    """A cheap guard that rarity still separates enemies at all.

    THIS IS NOT THE MEASURE OF WHETHER THE RAREST ENEMIES ARE FRIGHTENING, and
    it used to claim to be, at a threshold of 6. That was wrong. When issue #108
    fitted enemy damage to what a geared character survives, this ratio FELL
    from 9.7 to 5.8 while the danger rose sharply: the Cataclysm Boss went from
    killing the reference build in 8 hits to killing it in 3. A raw damage ratio
    says nothing about that, because mitigation and health sit between the two.

    `sim/tests/test_survivability.py` measures the real thing. This only catches
    the ladder collapsing.
    """
    common, boss = at_tier_eight("Common"), at_tier_eight("Cataclysm Boss")
    assert boss.damage_per_hit / common.damage_per_hit > 4.0


def test_damage_per_second_rises_with_rarity_too():
    """It used to rise only 1.2x across the whole ladder, because attack interval
    rose with rarity and cancelled the damage growth out. Attack interval is the
    archetype's now, so nothing cancels it."""
    common, boss = at_tier_eight("Common"), at_tier_eight("Cataclysm Boss")
    assert boss.damage_per_second / common.damage_per_second > 4.0


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

    assert stripped("Common") == pytest.approx(8.4, abs=0.05)
    assert stripped("Cataclysm Boss") == pytest.approx(20.9, abs=0.05)


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
        es.stats_for("Common", 1000.0, "Skeleton", tier=8)


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


def test_every_declared_resistance_is_worth_what_it_says():
    """The failure this guards against is a figure that reads as smaller than it
    is written. `damage.effective_resistance` caps at 70%, so an archetype at 95%
    would behave exactly as one at 70% and the extra 25 points would appear in a
    table and change nothing. Asserted by resolving the figure rather than by
    repeating the constant, so it fails if the cap moves."""
    for kind in es.ARCHETYPES.values():
        assert dmg.effective_resistance(kind.resistance, 0.0) == kind.resistance, \
            f"{kind.name}'s {kind.resistance}% resistance is not worth {kind.resistance}%"


def test_the_resistance_cap_check_actually_fires():
    real = dict(es.ARCHETYPES)
    es.ARCHETYPES["Tainted"] = es.Archetype(name="Tainted", role="test",
                                            resistance=95.0)
    try:
        with pytest.raises(AssertionError, match="the 70%"):
            es._check_no_enemy_resists_more_than_the_cap_allows()
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
    without_resistance = dmg.armor_reduction(warden.armor, warden.tier)
    assert warden.damage_taken_fraction() == pytest.approx(
        (1.0 - without_resistance / 100.0) * 0.65)


def test_player_penetration_cuts_into_enemy_resistance():
    """Which is what gives the player's resistance penetration stat a target on
    the enemy side. Measured as a RATIO so it tests the resistance layer alone:
    the creature's armour is unchanged by resistance penetration, so it divides
    out and the ratio is exactly (100 - 15) / (100 - 35)."""
    warden = at_tier_eight("Herald", "Abyssal Warden")
    assert warden.damage_taken_fraction(20.0) / warden.damage_taken_fraction(
        0.0) == pytest.approx(0.85 / 0.65)
    assert es.player_damage_to_kill_in(warden, 30.0, penetration=20.0) < \
        es.player_damage_to_kill_in(warden, 30.0)


def test_penetration_beyond_an_enemys_resistance_does_not_grant_bonus_damage():
    """Otherwise over-stacking penetration would turn into a damage multiplier
    against the enemies that need it least.

    ASSERTED AGAINST THE CREATURE'S OWN UNRESISTED FRACTION rather than against
    1.0, because armour is applied now too and the Abyssal Warden's removes 48%
    of the hit whatever the player's penetration is. What must not happen is
    penetration pushing the fraction ABOVE what an identical creature with no
    resistance at all would take, and that is what this checks.

    `damage.effective_resistance` is what stops the overshoot, since issue #482.
    This file used to clamp penetration itself before handing it over, because
    the shared function let penetration run on into negative resistance; that
    local clamp is gone and this test now checks the shared rule reaches an
    enemy rather than checking a copy of it.
    """
    warden = at_tier_eight("Herald", "Abyssal Warden")
    unresisted = 1.0 - dmg.armor_reduction(warden.armor, warden.tier) / 100.0
    assert warden.damage_taken_fraction(35.0) == pytest.approx(unresisted)
    assert warden.damage_taken_fraction(200.0) == pytest.approx(unresisted)
    assert warden.damage_taken_fraction(200.0) <= 1.0
    imp = at_tier_eight("Common", "Imp")
    assert imp.damage_taken_fraction(50.0) == pytest.approx(
        imp.damage_taken_fraction(0.0))


# --------------------------------------------------------------------------
# Every defensive layer reaches the arithmetic. Issue #481.
# --------------------------------------------------------------------------

def test_armour_is_applied_to_what_a_player_needs_to_kill_an_enemy():
    """THE DEFECT IN ISSUE #481. `player_damage_to_kill_in` applied the enemy's
    resistance and nothing else, so every figure it produced was too low, worst
    against the most armoured creatures in the slice.

    Pinned to the arithmetic rather than to a stored number: armour removes
    `armor / (armor + 800 x tier)` capped at 75%, and it multiplies with
    resistance rather than adding to it.
    """
    warden = at_tier_eight("Herald", "Abyssal Warden")
    assert warden.armor > 0.0

    removed = dmg.armor_reduction(warden.armor, warden.tier)
    together = (1.0 - removed / 100.0) * (1.0 - warden.resistance / 100.0)
    assert warden.damage_taken_fraction() == pytest.approx(together)

    needed = es.player_damage_to_kill_in(warden, 30.0)
    assert needed == pytest.approx(warden.effective_health / 30.0 / together)

    # What the resistance-only answer was, and how far out it was.
    resistance_only = (warden.effective_health / 30.0
                       / (1.0 - warden.resistance / 100.0))
    assert needed > 1.9 * resistance_only


def test_the_two_most_armoured_creatures_are_the_ones_this_moved_most():
    """Armour is the layer that was missing, so the size of the correction has
    to track the armour share and nothing else. The Imp carries no armour at
    all, so its figure must be unchanged by armour."""
    imp = at_tier_eight("Common", "Imp")
    assert imp.armor == 0.0
    assert dmg.armor_reduction(imp.armor, imp.tier) == 0.0

    ranked = sorted(
        (at_tier_eight(rarity, name) for name, rarity in
         (("Imp", "Common"), ("Hellhound", "Common"), ("Succubus", "Elite"),
          ("Brute", "Elite"), ("Corrupted Sentinel", "Legendary"),
          ("Abyssal Warden", "Herald"), ("Gatekeeper", "Cataclysm Boss"))),
        key=lambda e: dmg.armor_reduction(e.armor, e.tier))
    assert ranked[0].archetype.name == "Imp"
    assert ranked[-1].archetype.name == "Gatekeeper"


def test_an_enemys_armour_is_worth_less_at_a_higher_tier():
    """The reason the tier is carried on the stat block. The same creature with
    the same armour figure takes markedly more damage at tier 8 than at tier 1,
    because the armour constant rises 800 a tier."""
    low = es.stats_on_floor("Herald", 1, "Cataclysm", kind="Abyssal Warden")
    high = es.stats_on_floor("Herald", 8, "Cataclysm", kind="Abyssal Warden")
    same_armour = es.stats_for("Herald", low.score, "Abyssal Warden", tier=8)

    assert same_armour.armor == pytest.approx(low.armor)
    assert same_armour.damage_taken_fraction() > low.damage_taken_fraction()
    assert high.tier == 8 and low.tier == 1


def test_the_tier_has_to_be_stated_because_a_wrong_one_is_wrong_by_half():
    """A defaulted tier would be a silent error rather than a loud one. At tier
    1 the Abyssal Warden's armour hits its 75% cap; at tier 8 it removes 48%.
    That is more than a factor of two in what gets through."""
    with pytest.raises(TypeError, match="tier"):
        es.stats_for("Herald", 1000.0, "Abyssal Warden")

    warden = at_tier_eight("Herald", "Abyssal Warden")
    at_one = es.stats_for("Herald", warden.score, "Abyssal Warden", tier=1)
    assert dmg.armor_reduction(at_one.armor, 1) == pytest.approx(75.0)
    assert warden.damage_taken_fraction() > 2 * at_one.damage_taken_fraction()


def test_evasion_is_counted_and_only_against_a_direct_attack():
    """Evasion is the Imp's and the Hellhound's designed defence. Folded in as
    its expectation, which is what `damage.average_damage_taken` does on the
    player's side. Area damage lands regardless, which is the design's rule."""
    imp = at_tier_eight("Common", "Imp")
    assert imp.evasion == 25.0
    assert imp.damage_taken_fraction() == pytest.approx(0.75)
    assert imp.damage_taken_fraction(is_area=True) == pytest.approx(1.0)

    direct = es.player_damage_to_kill_in(imp, 30.0)
    area = es.player_damage_to_kill_in(imp, 30.0, is_area=True)
    assert direct == pytest.approx(area / 0.75)


def test_armour_penetration_cuts_into_armour_and_nothing_else():
    """Armour is a live layer now, so the enchantment family that ignores it --
    'Your skills ignore 10%-25% of enemy armor' -- has somewhere to land. It
    must not touch resistance: ignoring all of the armour leaves exactly the
    resistance behind."""
    warden = at_tier_eight("Herald", "Abyssal Warden")
    none = warden.damage_taken_fraction()
    some = warden.damage_taken_fraction(armor_penetration=25.0)
    total = warden.damage_taken_fraction(armor_penetration=100.0)

    assert none < some < total
    assert total == pytest.approx(1.0 - warden.resistance / 100.0)


def test_the_energy_shield_is_counted_once_and_not_twice():
    """A shield sits after every mitigation layer and is part of the pool a
    player chews through, so it belongs to `effective_health` and must not also
    reduce the per-hit fraction. Counting it in both places would make the
    Corrupted Sentinel look far tougher than it is."""
    sentinel = at_tier_eight("Legendary", "Corrupted Sentinel")
    assert sentinel.energy_shield > 0.0

    bare = es.stats_for("Legendary", sentinel.score, "Brute", tier=8)
    assert bare.energy_shield == 0.0

    # The fraction ignores the shield entirely: it is the same whether the
    # creature has one or not, once armour and resistance are matched.
    probe = sentinel.defender_for()
    assert probe.energy_shield == sentinel.energy_shield
    assert es.player_damage_to_kill_in(sentinel, 30.0) == pytest.approx(
        sentinel.effective_health / 30.0 / sentinel.damage_taken_fraction())


# --------------------------------------------------------------------------
# The handoff into the damage model
# --------------------------------------------------------------------------

def test_the_defender_carries_every_layer_the_stat_block_has():
    """`defender_for` is the ONLY route from this file into `damage.py`. A stat
    a defender does not carry is a stat nothing applies, which is the whole of
    issue #481."""
    warden = at_tier_eight("Herald", "Abyssal Warden")
    d = warden.defender_for()

    assert d.health == warden.health
    assert d.energy_shield == warden.energy_shield
    assert d.armor == warden.armor
    assert d.evasion == warden.evasion
    assert d.tier == warden.tier


def test_one_enemy_resistance_becomes_the_same_figure_for_all_eight_types():
    """An enemy has ONE resistance applied to all incoming damage, and
    `damage.Defender` holds one per type because the player needs eight. The
    mapping is the same number in every slot, so a player switching damage type
    changes nothing about how hard an enemy is to kill."""
    warden = at_tier_eight("Herald", "Abyssal Warden")
    d = warden.defender_for()
    assert set(d.resistances) == set(DAMAGE_TYPES)
    for kind in DAMAGE_TYPES:
        assert d.resistance_to(kind) == pytest.approx(warden.resistance)


def test_the_defender_knows_whether_it_is_a_boss():
    """`is_boss_rarity` exists to feed `Defender.is_boss`, which is what makes a
    boss immune to stun. Before `defender_for` there was nothing to feed."""
    assert at_tier_eight("Cataclysm Boss", "Gatekeeper").defender_for().is_boss
    assert at_tier_eight("Boss", "Gatekeeper").defender_for().is_boss
    assert not at_tier_eight("Herald", "Abyssal Warden").defender_for().is_boss


def test_an_enemy_has_four_defensive_layers_and_not_six():
    """An enemy has evasion, armour, resistance and an energy shield. No block
    chance and no flat damage reduction, and that is the decision rather than a
    gap.

    IT USED TO SAY "yet". Issue #488 asked for the player's two remaining layers
    on every archetype and was answered no on 2026-08-17, so this stopped being a
    note about unbuilt work and became the guard that holds the answer. The
    reasoning is in `docs/DECISIONS.md` and the short form is two sentences:

    * An enemy holds ONE untyped resistance, so step 4 already multiplies every
      hit by `(1 - resistance/100)`. A flat damage reduction at step 5 is the
      same arithmetic with the player's penetration no longer biting it — a
      second copy of the mechanic with the counterplay removed. This file has
      already deleted one mechanic for that exact reason, per-rarity enemy
      penetration.
    * Block applies to area damage, and area damage is the answer the design
      names for enemy evasion. So enemy block would be the first enemy layer
      with no player answer anywhere, and no affix or enchantment table holds a
      block-reduction stat to give it one.

    WHERE AN ENEMY SHOULD STOP MORE, raise its resistance or its armour share.
    Where it should stop more only sometimes, that is a modifier, which is
    issue #674.
    """
    for name, rarity in (("Imp", "Common"), ("Abyssal Warden", "Herald"),
                         ("Gatekeeper", "Cataclysm Boss")):
        defender = at_tier_eight(rarity, name).defender_for()
        assert defender.block_chance == 0.0, (
            f"{name} has a block chance. Issue #488 decided an enemy has none; "
            "if that is being reversed, the decision in docs/DECISIONS.md has "
            "to be reversed with it and this test rewritten rather than deleted")
        assert defender.damage_reduction == 0.0, (
            f"{name} has flat damage reduction. Issue #488 decided an enemy has "
            "none, and a conditional modifier is issue #674 rather than a "
            "figure on the archetype")

    # AND THE FOUR IT DOES HAVE ARE STILL CARRIED, so this cannot pass by an
    # enemy quietly losing every layer it has.
    warden = at_tier_eight("Herald", "Abyssal Warden").defender_for()
    assert warden.armor > 0.0
    assert warden.resistances["Demonic"] > 0.0
    assert at_tier_eight("Common", "Imp").defender_for().evasion > 0.0


def test_the_probe_hit_is_never_clamped_by_the_probe_pool():
    """`damage.resolve` clamps a hit to the health remaining, and this is
    reading a ratio rather than killing anything. A creature whose real health
    is far below the probe hit must still report its true fraction."""
    tiny = es.stats_for("Common", 1.0, "Imp", tier=8)
    assert tiny.health < es._PROBE_DAMAGE
    assert tiny.damage_taken_fraction() == pytest.approx(0.75)


def test_the_fraction_matches_resolving_a_hit_against_the_enemy_directly():
    """The fraction must be what the damage model itself says, not a second
    implementation of the same arithmetic that could drift from it. Resolved
    here against the creature's own defender with its real health, which is
    what the engine does."""
    for name, rarity in (("Imp", "Common"), ("Hellhound", "Common"),
                         ("Brute", "Elite"), ("Abyssal Warden", "Herald"),
                         ("Gatekeeper", "Cataclysm Boss")):
        e = at_tier_eight(rarity, name)
        probe = 1.0  # far below any of these creatures' health, so no clamp
        # Emptied of shield, because the fraction is a pre-pool figure and a
        # shield would absorb the whole probe where there is one.
        against = dataclasses.replace(e.defender_for(), energy_shield=0.0)
        landed = dmg.average_damage_taken(
            dmg.Attacker(damage=probe, damage_type=e.damage_type), against)
        assert landed / probe == pytest.approx(e.damage_taken_fraction())


# --------------------------------------------------------------------------
# The ceiling on the combination. Issue #483
# --------------------------------------------------------------------------

def test_no_enemys_layers_combine_past_what_the_player_stops():
    """The rule the design document states is about the COMBINATION: "No
    combination of these layers reaches immunity." Every archetype is inside
    every per-field cap and that is not the same statement."""
    for name in es.ARCHETYPES:
        for rarity in ORDER:
            most = es.most_damage_stopped(name, rarity)
            assert most < es.ENEMY_MITIGATION_CEILING, f"{name} at {rarity}"


def test_the_per_field_caps_alone_do_not_enforce_the_ceiling():
    """The reason the check had to change. Armour caps at 75% and resistance at
    70%, so a creature with both at their caps stops 92.5% of a hit with neither
    field over its own limit. No per-field check can see that."""
    both_at_their_caps = 100.0 * (
        1.0 - (1.0 - dmg.ARMOR_REDUCTION_CAP / 100.0)
        * (1.0 - dmg.RESISTANCE_CAP / 100.0))
    assert both_at_their_caps == pytest.approx(92.5)
    assert both_at_their_caps > es.ENEMY_MITIGATION_CEILING


def test_the_ceiling_check_actually_fires():
    """Broken with an archetype every per-field check passes: 60% resistance is
    under the 70% cap, and armour has no per-archetype limit at all. Only the
    combination is out of bounds, so this is the case the old check could not
    have caught."""
    real = dict(es.ARCHETYPES)
    es.ARCHETYPES["Tainted"] = es.Archetype(name="Tainted", role="test",
                                            resistance=60.0, armor_share=1.0)
    try:
        es._check_no_enemy_resists_more_than_the_cap_allows()   # this one passes
        with pytest.raises(AssertionError, match="could stop"):
            es._check_no_enemy_can_become_immune()
    finally:
        es.ARCHETYPES.clear()
        es.ARCHETYPES.update(real)


def test_the_two_checks_are_different_rules_and_neither_implies_the_other():
    """One archetype passes each check and fails the other, so keeping both is
    not belt and braces."""
    resists_past_the_cap = es.Archetype(name="Resister", role="test",
                                        resistance=95.0, armor_share=0.0)
    stops_too_much = es.Archetype(name="Wall", role="test",
                                  resistance=60.0, armor_share=1.0)

    assert (dmg.effective_resistance(resists_past_the_cap.resistance, 0.0)
            != resists_past_the_cap.resistance)
    assert es.most_damage_stopped(resists_past_the_cap, "Common") < \
        es.ENEMY_MITIGATION_CEILING

    assert (dmg.effective_resistance(stops_too_much.resistance, 0.0)
            == stops_too_much.resistance)
    assert es.most_damage_stopped(stops_too_much, "Common") >= \
        es.ENEMY_MITIGATION_CEILING


@pytest.mark.parametrize("name", sorted(es.ARCHETYPES))
def test_the_bound_is_never_below_what_a_real_creature_stops(name):
    """`most_damage_stopped` is an upper bound and the guard is only worth
    anything if it really bounds. Checked against real stat blocks at every tier
    and rarity, whose armour comes from the score rather than from the cap."""
    bound = es.most_damage_stopped(name, "Common")
    for tier in range(1, 9):
        for rarity in ORDER:
            e = es.stats_on_floor(rarity, tier, "Cataclysm", kind=name)
            real = 100.0 * (1.0 - e.damage_taken_fraction())
            assert real <= bound + 1e-9, f"{name} {rarity} tier {tier}"


def test_the_bound_is_reached_rather_than_merely_approached():
    """An upper bound nothing gets near would say nothing. A score large enough
    lands within a hundredth of a point of it, which is what makes the bound the
    right thing to check rather than a sampled tier."""
    huge = es.stats_for("Cataclysm Boss", 1e7, "Abyssal Warden", tier=1)
    assert 100.0 * (1.0 - huge.damage_taken_fraction()) == pytest.approx(
        es.most_damage_stopped("Abyssal Warden", "Cataclysm Boss"), abs=0.01)


def test_an_archetype_that_never_gets_armour_is_bounded_without_it():
    """The Imp's armour share is zero, so no score gives it any armour and its
    bound is its evasion alone. Reading it at the armour cap would say 81.25%
    and forbid a creature the design has no problem with."""
    assert es.archetype("Imp").armor_share == 0.0
    assert es.most_damage_stopped("Imp", "Common") == pytest.approx(25.0)


def test_the_worst_creature_in_the_slice_is_the_one_the_design_calls_resistant():
    """The Abyssal Warden is the design's deliberate maximum, so it should be
    what sits closest to the ceiling, and it should still fit under it."""
    by_bound = sorted(es.ARCHETYPES,
                      key=lambda n: es.most_damage_stopped(n, "Herald"))
    assert by_bound[-1] == "Abyssal Warden"
    assert es.most_damage_stopped("Abyssal Warden", "Herald") == pytest.approx(
        83.75)


def test_the_ceiling_is_not_reached_by_anything_the_slice_actually_fights():
    """Reported so the headroom is a number rather than an impression. If this
    ever gets tight, the enemy design has moved and the ceiling is the thing to
    argue about rather than to quietly raise."""
    met_at = (("Imp", "Common"), ("Hellhound", "Common"), ("Succubus", "Elite"),
              ("Brute", "Elite"), ("Corrupted Sentinel", "Legendary"),
              ("Abyssal Warden", "Herald"), ("Gatekeeper", "Cataclysm Boss"))
    worst = max(100.0 * (1.0 - at_tier_eight(rarity, name).damage_taken_fraction())
                for name, rarity in met_at)
    assert worst < es.ENEMY_MITIGATION_CEILING - 10.0, (
        f"the hardest creature met stops {worst:.1f}%")


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
