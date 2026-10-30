"""`sacrifice_to_craft` lets a city fall to keep crafting, and only a city it can spare.

ISSUE #1454. The project owner, 2026-09-07: a player "will have to decide when
to sacrifice a city to craft and when not to". No policy made that trade:
`triage` stops crafting whenever any dungeon is due within a craft.

THE RULE. Do what `triage` does, and also craft when the player has the
materials and every dungeon due to hit during the craft stands on a city that,
if it fell, would still leave at least two more cities to fall before the
Cataclysm reaches the Pillar.

Every case is built by hand at difficulty tier 1 with no surges, so the only
thing on the board is the dungeon the case is about. THE CONTROL is the same
board with the dungeon on a city the rule says to defend.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import CRAFT, Simulation


def board() -> Simulation:
    cfg = replace(TuningConfig(), tier=1).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=0)
    sim.next_surge_day = float("inf")
    sim.dungeons.clear()
    sim.materials = cfg.craft_material_cost
    return sim


def about_to_fall(sim: Simulation, tier: CityTier):
    """A Basic dungeon due inside a craft, on a city its hit will fell."""
    city = next(c for c in sim.empire.cities.values()
                if c.tier is tier and not c.fallen)
    d = sim._make_dungeon(DungeonType.BASIC, city)
    d.subtype = "Timed"
    d.resolve_in = 1.0
    city.defense = d.defense_damage / 2.0
    assert d.resolves
    assert policies._resolve_fells(sim, d, city)
    assert sim.death_chance(d) <= sim.cfg.death_risk_tolerance, (
        "set-up: the dungeon must be safe to enter, or triage would craft "
        "because it is stuck rather than because of the rule")
    return city, d


def test_it_is_registered_with_every_other_policy():
    assert policies.ALL["sacrifice_to_craft"] is policies.sacrifice_to_craft


def test_it_crafts_while_a_city_it_can_spare_is_about_to_fall():
    """THE CASE. An Outpost at the start of a campaign: its fall leaves two
    more cities between the Cataclysm and the Pillar, so the rule spares it."""
    sim = board()
    city, d = about_to_fall(sim, CityTier.OUTPOST)
    left = (sim.empire.distance_to_defeat()
            - sim.empire.lane_criticality()[city.cid])
    assert left >= policies.SACRIFICE_KEEPS_DISTANCE
    assert policies.triage(sim, [d]) is d, (
        "set-up: triage answers the dungeon, so the craft below is the rule's "
        "own decision and not triage's")
    assert policies.sacrifice_to_craft(sim, [d]) is CRAFT, (
        "an Outpost whose fall leaves two cities to fall was about to fall, "
        "and the policy went to the dungeon instead of the forge. Issue #1454.")


def test_control_it_defends_a_city_it_cannot_spare():
    """THE CONTROL. A Bulwark at the start of a campaign: its fall would leave
    one city between the Cataclysm and the Pillar, so the rule defends it and
    the policy answers what triage answers."""
    sim = board()
    city, d = about_to_fall(sim, CityTier.BULWARK)
    left = (sim.empire.distance_to_defeat()
            - sim.empire.lane_criticality()[city.cid])
    assert left < policies.SACRIFICE_KEEPS_DISTANCE
    assert policies.sacrifice_to_craft(sim, [d]) is policies.triage(sim, [d]) is d


def test_without_the_materials_it_cannot_sacrifice_anything():
    """No craft to protect, no trade: it does what triage does."""
    sim = board()
    _, d = about_to_fall(sim, CityTier.OUTPOST)
    sim.materials = sim.cfg.craft_material_cost - 1.0
    assert policies.sacrifice_to_craft(sim, [d]) is d
