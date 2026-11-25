"""A Rift at difficulty tier 1 never draws the Demonic Guide; above tier 1 it may (issue #51).

THE OWNER, 2026-11-25, verbatim: "Make demonic guide not spawn on the Rifts. It would be really annoying if you're
trying to destroy a pillar and you can't because that guy will kill you if you let him get out of range.", and a moment
later, "at least not on t1". A "pillar" is a Rift's altar. `TuningConfig.rift_without_the_guide_up_to_tier` is 1, and
the game's port is `UCataclysmDungeonModifierRules::RiftWithoutTheGuideUpToTier`.

EACH TEST HAS A CONTROL: the same draws with the switch at 0 hold the Guide, a tier 2 Rift can hold it, and an
ordinary Demonic dungeon at tier 1 can hold it.
"""
from __future__ import annotations

from dataclasses import replace

from cataclysm_sim.config import TREE_NONE, CityTier, DungeonType, TuningConfig
from cataclysm_sim.engine import Simulation
from cataclysm_sim.modifiers import DEMONIC_GUIDE, MODIFIERS, pool_for

SEEDS = range(200)


def simulation(seed: int, tier: int = 1, active: tuple[str, ...] = ("Demonic",), **over) -> Simulation:
    cfg = replace(TuningConfig(), tier=tier,
                  **{"demonic_rifts": True, "surge_on_empty_board": False, **over}).with_tree(TREE_NONE)
    sim = Simulation(cfg, seed=seed)
    # THE POOL OF THE CATACLYSMS NAMED, set after the seed built its own.
    sim.active_types = active
    sim.modifier_pool = pool_for(active)
    sim.dungeons.clear()
    sim.next_surge_day = float("inf")
    return sim


def made(sim: Simulation, dtype: DungeonType, source: str = "Demonic"):
    city = next(c for c in sim.empire.cities.values() if c.tier is CityTier.OUTPOST)
    sim.making_for = source
    try:
        d = sim._make_dungeon(dtype, city)
    finally:
        sim.making_for = ""
    d.source = source
    return d


def slotted(d) -> list[str]:
    """The modifiers in a dungeon's slots: the Corrupted Stalker is granted outside them."""
    return [n for n in d.modifier_names if n != "Corrupted Stalker"]


def test_the_switch_defaults_to_the_ruling() -> None:
    assert TuningConfig().rift_without_the_guide_up_to_tier == 1


def test_the_guide_is_a_row_of_demonics_column() -> None:
    assert DEMONIC_GUIDE in {name for name, _ in MODIFIERS["Demonic"]}


def test_no_tier_one_rift_holds_the_guide_and_each_still_holds_eight_different() -> None:
    for seed in SEEDS:
        names = slotted(made(simulation(seed), DungeonType.QUEST))
        assert DEMONIC_GUIDE not in names, (seed, names)
        assert len(names) == 8 and len(set(names)) == 8, (seed, names)


def test_control_with_the_switch_at_nought_a_tier_one_rift_draws_the_guide() -> None:
    held = sum(DEMONIC_GUIDE in slotted(made(simulation(seed, rift_without_the_guide_up_to_tier=0), DungeonType.QUEST))
               for seed in SEEDS)
    assert held > 0, "the control drew no Guide, so the test above could not have seen one"


def test_control_a_tier_two_rift_can_draw_the_guide() -> None:
    held = 0
    for seed in SEEDS:
        names = slotted(made(simulation(seed, tier=2, active=("Demonic", "Death")), DungeonType.QUEST))
        assert len(names) == 8 and len(set(names)) == 8, (seed, names)
        held += DEMONIC_GUIDE in names
    assert held > 0


def test_control_an_ordinary_demonic_dungeon_at_tier_one_can_draw_the_guide() -> None:
    held = sum(DEMONIC_GUIDE in slotted(made(simulation(seed), DungeonType.BASIC)) for seed in SEEDS)
    assert held > 0
