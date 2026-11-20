"""Every Cataclysm's wave lands on the exposed front line, Demonic included.

**THE RULING.** Issue [#1085](https://github.com/sdubois777/Cataclysm/issues/1085), 2026-09-14: "the vertical
slice's Demonic wave does not ignore the empire map's lane rule. The generic frontier pattern the scheduler already
ports stays; the GDD's Demonic quest mechanic stays a Demonic-only mechanic and is not the general surge rule." The
game's `UCataclysmSurgeScheduler` draws every wave slot, whichever Cataclysm sent it, from
`UCataclysmEmpireMap::ExposedCities`. Until 2026-11-19 the model's Demonic drew from every standing city, sealed ones
included, through a `Pattern.ignores_frontier` field that only it set. Issue #2749 removed the field, and this file is
what fails if any Cataclysm lands behind the line again.

**EACH CHECK SAYS FIRST THAT IT COULD FAIL.** A wave on an untouched map can only reach the rim, and a check that the
rim is where it landed proves nothing unless the Cataclysm would have gone further: so each one first asserts that the
Cataclysm's own weight wants at least one sealed city, and that the campaigns reached cities behind the rim at all.
"""

from __future__ import annotations

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import CityTier, TuningConfig
from cataclysm_sim.engine import Simulation
from cataclysm_sim.patterns import PATTERNS

ROSTER = TuningConfig().CATACLYSM_ROSTER

#: The seeds whose tier 1 character faces each Cataclysm alone are found among these. Every one of the eight is the
#: only active one for at least `LEAST_SEEDS` of them.
SEED_RANGE = range(300)

#: How many seeds each Cataclysm's first-wave check must have, so a roster draw that stops reaching one cannot leave
#: its check passing on nothing. Demonic had 36 of the 300 on 2026-11-19.
LEAST_SEEDS = 20

#: The tier 8 campaigns the whole-campaign check runs: every one of the eight is active in each.
CAMPAIGN_SEEDS = range(12)


def alone_seeds(cataclysm: str) -> list[int]:
    """The seeds in `SEED_RANGE` whose tier 1 character faces `cataclysm` and no other."""
    return [seed for seed in SEED_RANGE if list(Simulation(TuningConfig(), seed).active_types) == [cataclysm]]


def sealed_cities(sim: Simulation) -> list:
    """Every standing city the front line does not yet expose, the Pillar left out."""
    exposed = {c.cid for c in sim.empire.exposed_cities()}
    return [c for c in sim.empire.alive_cities() if c.cid not in exposed and c.cid != sim.empire.pillar_id]


@pytest.mark.parametrize("cataclysm", ROSTER)
def test_the_first_wave_of_each_cataclysm_alone_lands_only_on_the_front_line(cataclysm):
    seeds = alone_seeds(cataclysm)
    assert len(seeds) >= LEAST_SEEDS, (
        f"only {len(seeds)} of seeds {SEED_RANGE.start} to {SEED_RANGE.stop - 1} face {cataclysm} alone at tier 1; "
        f"this check needs {LEAST_SEEDS}. Widen SEED_RANGE rather than lowering the floor.")

    pattern = PATTERNS[cataclysm]
    landed, behind = 0, []
    for seed in seeds:
        sim = Simulation(TuningConfig(), seed)
        sealed = sealed_cities(sim)
        # SET-UP: the Cataclysm's own weight wants a city the line keeps it from, so the line is what is tested.
        assert any(pattern.weight(sim.empire, c) > 0 for c in sealed), (
            f"seed {seed}: {cataclysm}'s weight gives no sealed city a share, so landing on the rim proves nothing")
        sealed_ids = {c.cid for c in sealed}
        sim.trigger_surge()
        for dungeon in sim.dungeons.values():
            landed += 1
            if dungeon.city_id in sealed_ids:
                behind.append((seed, dungeon.city_id, dungeon.city_tier.value))
    assert landed > 0
    assert not behind, (
        f"{len(behind)} of {landed} first-wave {cataclysm} dungeons landed behind the front line "
        f"(seed, city, ring), the first ten: {behind[:10]}. The ruling on #1085: no Cataclysm's wave ignores "
        "the lane rule.")


def test_a_demonic_wave_reaches_a_bulwark_once_its_lane_opens_and_no_sealed_one():
    """THE OTHER HALF. Following the line must not mean staying on the rim: a fallen Outpost opens the Bulwark it
    shielded, and a Demonic wave then lands there -- and still on no Bulwark whose lane is closed."""
    seed = alone_seeds("Demonic")[0]
    sim = Simulation(TuningConfig(), seed)
    bulwark = next(c for c in sim.empire.cities.values() if c.tier is CityTier.BULWARK)
    sim.empire.cities[bulwark.outward[0]].fallen = True

    opened = {c.cid for c in sim.empire.exposed_cities() if c.tier is CityTier.BULWARK}
    sealed = {c.cid for c in sealed_cities(sim)}
    assert bulwark.cid in opened, "set-up: the Bulwark behind the fallen Outpost is not exposed"
    assert any(sim.empire.cities[k].tier is CityTier.BULWARK for k in sealed), "set-up: no Bulwark is still sealed"

    for _ in range(30):
        sim.trigger_surge()
    on_opened = [d for d in sim.dungeons.values() if d.city_id in opened]
    on_sealed = [(d.city_id, d.city_tier.value) for d in sim.dungeons.values() if d.city_id in sealed]
    assert on_opened, f"thirty Demonic waves and none reached the opened Bulwark {bulwark.cid}"
    assert not on_sealed, f"{len(on_sealed)} Demonic dungeons landed on sealed cities: {on_sealed[:10]}"


class _Watched(Simulation):
    """A campaign that notes, for every dungeon a wave makes, whether its city was exposed at that moment."""

    def __init__(self, cfg, seed, log):
        self._log, self._in_wave = log, False
        super().__init__(cfg, seed=seed)

    def trigger_surge(self, from_city_fall: bool = False) -> None:
        self._in_wave = True
        try:
            super().trigger_surge(from_city_fall)
        finally:
            self._in_wave = False

    def _make_dungeon(self, dtype, city, floors_mult: float = 1.0):
        if self._in_wave:
            self._log.append((self.making_for, city.tier, self.empire.is_exposed(city)))
        return super()._make_dungeon(dtype, city, floors_mult=floors_mult)


def test_every_wave_dungeon_of_a_tier_8_campaign_stands_on_the_front_line():
    """Whole campaigns, every Cataclysm active, so the line has moved inward and every pattern's weight has had
    fallen and damaged cities to read. Every dungeon a wave makes stands, when it is made, on a city the line exposes."""
    log: list = []
    for seed in CAMPAIGN_SEEDS:
        sim = _Watched(TuningConfig(tier=8), seed, log)
        assert sorted(sim.active_types) == sorted(ROSTER), "set-up: tier 8 does not face all eight"
        sim.run(policies.triage)

    senders = {sender for sender, _, _ in log}
    assert senders == set(ROSTER), f"set-up: these sent no wave dungeon: {sorted(set(ROSTER) - senders)}"
    inner = sum(1 for _, tier, _ in log if tier is not CityTier.OUTPOST)
    assert inner > 0, "set-up: no wave reached a city behind the rim, so the line never moved"
    behind = [(sender, tier.value) for sender, tier, exposed in log if not exposed]
    assert not behind, (
        f"{len(behind)} of {len(log)} wave dungeons were made on a city the front line did not expose "
        f"(sender, ring), the first ten: {behind[:10]}")
