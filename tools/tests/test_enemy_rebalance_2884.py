"""The enemy rebalance measurement of issue #2884, on its analysis branch.

Three things are held here. The simulation's copy of the approved affix ranges (`sim/cataclysm_sim/affix_ranges_2883.py`)
is the proposal's own table, value for value, so the measurement cannot be of a different proposal. The re-fitted
reference build places every affix on a piece that can roll it, and the check that says so fires. And the proposed
enemy factors give the re-fitted character today's figures: the design's survival table at tier 8, the hits an average
Common takes at every tier, and the seconds a Boss takes to kill the tier's character, with tier 1 held where it is.

TODAY'S FIGURES were measured on `24c1c3810` (development with #2853's two branches and the proposal's files, before
this branch's change) by `python -u sim/analyse_enemy_rebalance_2884.py --sim-root <a copy of 24c1c3810>/sim`.
"""

from __future__ import annotations

import math
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
for path in (REPO_ROOT / "sim", REPO_ROOT / "tools"):
    if str(path) not in sys.path:
        sys.path.insert(0, str(path))

import affix_range_proposal as proposal  # noqa: E402
from cataclysm_sim import affix_ranges_2883 as ranges  # noqa: E402
from cataclysm_sim import affixes as af  # noqa: E402
from cataclysm_sim import damage as dmg  # noqa: E402
from cataclysm_sim import enemy_stats as es  # noqa: E402
from cataclysm_sim import fight as fg  # noqa: E402
from cataclysm_sim import reference_build as rb  # noqa: E402

#: The design's "How Long a Geared Character Survives", tier 8: hits to kill the reference character.
DESIGN_SURVIVAL_HITS = {"Imp": 60, "Hellhound": 26, "Succubus": 29, "Brute": 11, "Corrupted Sentinel": 25,
                        "Abyssal Warden": 5, "Gatekeeper": 2}
MET_AT = {"Imp": "Common", "Hellhound": "Common", "Succubus": "Elite", "Brute": "Elite",
          "Corrupted Sentinel": "Legendary", "Abyssal Warden": "Herald", "Gatekeeper": "Cataclysm Boss"}

#: Today, tiers 1 to 8: non-critical hits the reference loadout takes to kill an average Common (2 / gap), and the
#: seconds the game's Boss on floor 25 of a 50-floor Basic dungeon takes to kill the tier's reference character.
TODAY_COMMON_HITS = (1.20, 1.75, 2.00, 1.98, 1.85, 1.64, 1.82, 2.01)
TODAY_BOSS_KILLS_IN = (328.9, 37.5, 30.0, 22.8, 20.0, 19.6, 16.6, 14.5)


# ---------------------------------------------------------------------------
# The simulation's copy is the approved proposal
# ---------------------------------------------------------------------------

def test_the_simulations_ranges_are_the_approved_proposals():
    table = proposal.proposal()
    approved = {name: a.tiers for name, a in table.items() if a.kind != "Hybrid"}
    assert ranges.TIER_RANGES == approved
    narrowed = {name: tuple(sorted(a.slots)) for name, a in table.items()
                if a.kind != "Hybrid" and a.slots != a.old_slots}
    assert {name: tuple(sorted(s)) for name, s in ranges.SLOTS.items()} == narrowed


@pytest.mark.parametrize("two_handed", [False, True])
def test_every_value_the_simulation_reads_is_the_proposals(two_handed):
    table = proposal.proposal()
    for affix in af.AFFIX_POOL:
        for tier in af.AFFIX_TIERS:
            for level in (0, 3, 7, 10):
                for roll in (0.0, 0.5, 1.0):
                    assert affix.value_at(tier, roll, level, two_handed) == proposal.value(
                        table[affix.name], tier, roll, level, two_handed), (affix.name, tier, level, roll)
    for family in af.RESISTANCE_FAMILIES:
        assert family.value_at(7, 1.0, 10) == proposal.value(table[family.name], 7, 1.0, 10)


def test_a_hybrid_grants_each_half_two_tiers_down_and_nothing_below_tier_three():
    table = proposal.proposal()
    for hybrid in af.HYBRID_AFFIXES:
        for part in hybrid.parts:
            assert hybrid.value_of(part, 7) == proposal.hybrid_half(table[part.name], 7)
            assert hybrid.value_of(part, 3) == proposal.hybrid_half(table[part.name], 3)
            assert hybrid.value_of(part, 2) == 0.0


def test_every_implicit_is_the_proposals_whole_plus_zero_value():
    by_base = {(i.base, i.stat): i for i in proposal.implicit_proposal()}
    checked = 0
    for base in af.ITEM_BASES:
        for implicit in base.implicits:
            wanted = by_base.get((base.name, implicit.stat))
            if wanted is None:
                continue
            assert ranges.implicit_at_zero(implicit.value) == wanted.at_zero, (base.name, implicit.stat)
            checked += 1
    assert checked > 60, f"only {checked} implicits matched a proposal row by base and stat"


def test_the_narrowed_slots_reach_the_simulations_affixes_and_their_hybrids():
    assert af.FLAT_BLOCK_CHANCE.allowed_slots == frozenset({"Shoulders", "Belt"})
    assert af.FLAT_DAMAGE_REDUCTION.allowed_slots == frozenset({"Pants"})
    assert af.FLAT_PENETRATION.allowed_slots == frozenset({"Weapon"})
    hybrid = next(h for h in af.HYBRID_AFFIXES if h.name == "Block chance and crowd control resistance")
    assert hybrid.allowed_slots == frozenset({"Belt"})


# ---------------------------------------------------------------------------
# The re-fitted reference build
# ---------------------------------------------------------------------------

def test_the_reference_build_spends_every_slot_and_keeps_its_six_and_six_damage_affixes():
    assert rb.spent_slots() == (36, 36)
    assert rb.PREFIX_SPEND["FLAT_DAMAGE"] == 6 and rb.PREFIX_SPEND["INCREASED_DAMAGE"] == 6
    assert rb.RESISTANCE_AFFIXES == math.ceil(af.slots_to_cap(af.ALL_RESISTANCE, 8, 8))


def test_the_reference_build_caps_every_resistance_at_tier_8():
    assert all(v >= dmg.resistance_needed_to_cap(8) for v in rb.defender(8).resistances.values())


@pytest.mark.parametrize("piece, change, message", [
    (0, ("FLAT_BLOCK_CHANCE", "INCREASED_VITALITY"), "cannot roll there"),
    (0, ("INCREASED_VITALITY", "INCREASED_VITALITY"), "repeats an affix group"),
    (0, ("FLAT_HEALTH", "INCREASED_VITALITY"), "is not a suffix"),
])
def test_the_layout_check_refuses_what_a_piece_cannot_hold(monkeypatch, piece, change, message):
    """The check stated without its condition would pass anything; each break here is one it must refuse."""
    layout = list(rb.LAYOUT)
    base, prefixes, _ = layout[piece]
    layout[piece] = (base, prefixes, change)
    monkeypatch.setattr(rb, "LAYOUT", tuple(layout))
    with pytest.raises(ValueError, match=message):
        rb._check_every_affix_sits_on_a_piece_that_can_roll_it()


# ---------------------------------------------------------------------------
# The proposed enemy factors
# ---------------------------------------------------------------------------

def test_tier_one_is_held_where_the_starting_character_was_fitted():
    assert es.rebalance_factors_2884(1) == (1.0, 1.0)


def test_the_factors_never_fall_from_one_tier_to_the_next():
    for table in (es.HEALTH_SCALE_BY_TIER_2884, es.DAMAGE_SCALE_BY_TIER_2884):
        assert list(table) == sorted(table) and len(table) == 8


@pytest.mark.parametrize("name", sorted(DESIGN_SURVIVAL_HITS))
def test_the_designs_survival_table_holds_against_the_re_fitted_character(name):
    enemy = es.stats_on_floor(MET_AT[name], 8, "Cataclysm", kind=name)
    hits = dmg.hits_to_kill(dmg.Attacker(damage=enemy.average_damage_per_hit, damage_type=enemy.damage_type),
                            rb.defender(8))
    assert hits == DESIGN_SURVIVAL_HITS[name]


@pytest.mark.parametrize("tier", range(2, 9))
def test_an_average_common_takes_todays_hits_at_every_tier_after_the_first(tier):
    from cataclysm_sim import player_damage as pd
    hits = af.HITS_TO_KILL_A_COMMON_ENEMY / pd.gap_against_target(tier)
    assert hits == pytest.approx(TODAY_COMMON_HITS[tier - 1], abs=0.03)


@pytest.mark.parametrize("tier", range(2, 9))
def test_the_boss_kills_the_tiers_character_as_fast_as_today_after_the_first(tier):
    assert fg.fight(tier).boss_seconds_to_kill_the_player == pytest.approx(TODAY_BOSS_KILLS_IN[tier - 1], rel=0.01)


def test_control_todays_enemies_do_not_restore_the_survival_table(monkeypatch):
    """The tests above would pass for nothing if the factors changed no figure. With them at 1.00 the Gatekeeper
    takes 28 hits to kill the re-fitted character, not 2."""
    monkeypatch.setattr(es, "DAMAGE_SCALE_BY_TIER_2884", (1.0,) * 8)
    gatekeeper = es.stats_on_floor("Cataclysm Boss", 8, "Cataclysm", kind="Gatekeeper")
    hits = dmg.hits_to_kill(dmg.Attacker(damage=gatekeeper.average_damage_per_hit,
                                         damage_type=gatekeeper.damage_type), rb.defender(8))
    assert hits > 10 * DESIGN_SURVIVAL_HITS["Gatekeeper"]
