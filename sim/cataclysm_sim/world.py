"""The empire map.

A diamond lattice: the taxicab (L1) ball of radius 3. A cell exists at grid
coordinate (r, c) when |r| + |c| <= 3, and its ring -- |r| + |c| -- is its tier.

        ring 0   1 cell    Pillar        (Capital in the older docs)
        ring 1   4 cells   Sanctuary     (Metropolis)
        ring 2   8 cells   Bulwark       (City)
        ring 3  12 cells   Outpost       (Village)
                ---------
                25 cells

Ring N holds exactly 4N cells, which is where 12/8/4/1 comes from -- the counts
are a property of the geometry, not a separate design decision.

                          V
                      V   C   V
                  V   C   M   C   V
              V   C   M   P   M   C   V
                  V   C   M   C   V
                      V   C   V
                          V

LANES. Adjacency is orthogonal in lattice space: (r+-1, c) and (r, c+-1). Every
orthogonal step changes the ring by one, so the neighbours of a cell are
strictly the cells one step further out and one step further in. That is what
makes the frontier rule exact: a cell is attackable once ANY of the cells
shielding it from outside has fallen, and retaking that cell seals the lane
again.

Outposts on the rim are also linked to their neighbours along the perimeter
(the curved edges in the design sketch). Those are same-ring links, so they
carry no lanes inward -- they exist for adjacency effects, not for exposure.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .config import CityTier, TuningConfig

RADIUS = 3

RING_TIER = {
    0: CityTier.PILLAR,
    1: CityTier.SANCTUARY,
    2: CityTier.BULWARK,
    3: CityTier.OUTPOST,
}

TIER_LABEL = {
    CityTier.PILLAR: "Pillar",
    CityTier.SANCTUARY: "Sanctuary",
    CityTier.BULWARK: "Bulwark",
    CityTier.OUTPOST: "Outpost",
}


@dataclass
class City:
    cid: int
    name: str
    tier: CityTier
    r: int
    c: int

    # Orthogonal lattice neighbours, split by which way they lie.
    outward: list[int] = field(default_factory=list)   # ring + 1, shields this cell
    inward: list[int] = field(default_factory=list)    # ring - 1, this cell shields it
    perimeter: list[int] = field(default_factory=list)  # same ring, rim only

    max_defense: float = 0.0
    max_population: float = 0.0
    defense: float = 0.0
    population: float = 0.0

    fallen: bool = False
    # Marked by the Void: if this city falls it is erased, not merely lost, and
    # the lane it was sealing can never be closed again.
    doomed: bool = False
    erased: bool = False
    upgrades: int = 0

    @property
    def ring(self) -> int:
        return abs(self.r) + abs(self.c)

    @property
    def depth(self) -> int:
        """Steps from the Pillar. Same as the ring."""
        return self.ring

    @property
    def alive(self) -> bool:
        return not self.fallen

    @property
    def defense_frac(self) -> float:
        return 0.0 if self.max_defense <= 0 else self.defense / self.max_defense

    def reset(self) -> None:
        self.defense = self.max_defense
        self.population = self.max_population
        self.fallen = False


@dataclass
class Empire:
    cities: dict[int, City]
    pillar_id: int
    by_coord: dict[tuple[int, int], int] = field(default_factory=dict)
    _cache: dict = field(default_factory=dict)
    _cache_key: int = 0

    def __iter__(self):
        return iter(self.cities.values())

    def alive_cities(self, exclude_pillar: bool = True) -> list[City]:
        return [c for c in self.cities.values()
                if c.alive and not (exclude_pillar and c.cid == self.pillar_id)]

    def fallen_cities(self) -> list[City]:
        return [c for c in self.cities.values() if c.fallen]

    def total_population(self) -> float:
        return sum(c.population for c in self.cities.values() if c.alive)

    def max_population(self) -> float:
        return sum(c.max_population for c in self.cities.values())

    def population_frac(self) -> float:
        """Living population over what the empire would hold intact.

        THE DENOMINATOR COUNTS EVERY CITY, FALLEN OR NOT, and that is the whole
        point of it: a fallen city drops out of `total_population` and stays in
        `max_population`, so losing one moves the fraction down instead of
        quietly rebasing it. A denominator that shrank with the empire would
        report a wiped-out empire as 100% intact.

        The game module made the same choice explicitly and left a comment
        saying why -- `UCataclysmEmpireMap::TotalMaxPopulation` in
        `game/Source/CataclysmEmpire/Empire/CataclysmEmpireMap.cpp`: "EVERY
        CITY, FALLEN OR NOT. This is what the empire would hold intact, so it
        is the denominator the fraction lost is measured against and it must
        not shrink as cities are lost."

        An erased city -- one the Void took -- is still counted. `erased` is a
        flag and the city stays in `self.cities`, so the denominator never
        shrinks even then. Issue #1348.
        """
        return self.total_population() / max(1.0, self.max_population())

    # -- lanes -----------------------------------------------------------

    def is_exposed(self, c: City) -> bool:
        """Can dungeons spawn here yet?

        The rim is permanently exposed. Everything inside it is sealed until a
        lane opens -- that is, until one of the cells shielding it falls. Retake
        that cell and the lane closes again.
        """
        if c.fallen:
            return False
        if c.ring == RADIUS:
            return True
        return any(self.cities[k].fallen for k in c.outward)

    def exposed_cities(self, exclude_pillar: bool = True) -> list[City]:
        return [c for c in self.cities.values()
                if self.is_exposed(c)
                and not (exclude_pillar and c.cid == self.pillar_id)]

    def neighbours(self, c: City) -> list[int]:
        """Every city the map links this one to, whichever way the link runs.

        THREE KINDS OF LINK AND ALL THREE COUNT. `outward` and `inward` are the
        orthogonal lanes, and `perimeter` joins rim Outposts along the edge of
        the diamond. The perimeter links carry no lane and take no part in
        exposure, which is why `is_exposed` reads only `outward`; they exist
        for adjacency effects, and this is one.

        THAT IS A READING AND IT IS RECORDED IN `docs/DECISIONS.md`. The design
        never defines adjacency. Taking the orthogonal links alone would leave a
        Quest dungeon somewhere to go 69.1% of the time against 79.5% with the
        perimeter, measured over 926 quest timers in 30 campaigns, and would
        make the curved edges of the design's own sketch mean nothing.
        """
        return list(c.outward) + list(c.inward) + list(c.perimeter)

    def adjacent_exposed_cities(self, c: City,
                                exclude_pillar: bool = True) -> list[City]:
        """Where a dungeon standing on `c` could move to.

        THE SAME FILTER AS `exposed_cities`, NARROWED TO NEIGHBOURS. A dungeon
        may only stand where a surge could have put one, so a sealed city is no
        more a relocation target than it is a spawn target, and the Pillar is
        left out for the same reason it is left out there.

        WHY THIS EXISTS. `Simulation._resolve` moved a quest dungeon to a
        uniformly random exposed city ANYWHERE, which the design contradicts:
        `docs/Cataclysm_GDD_v2.md` section VIII says a Quest dungeon "does not
        resolve -- refreshes and may move to adjacent city". Asked which was
        intended the project owner answered on 2026-09-06, verbatim "Adjacent,
        and fix the simulation". Issue #1324 slice 4.
        """
        return [self.cities[k] for k in self.neighbours(c)
                if self.is_exposed(self.cities[k])
                and not (exclude_pillar and k == self.pillar_id)]

    def pillar_exposed(self) -> bool:
        """A Sanctuary has fallen -- the Cataclysm can reach the Pillar."""
        return self.is_exposed(self.cities[self.pillar_id])

    # -- lane analysis ---------------------------------------------------

    def _invalidate(self) -> None:
        self._cache_key += 1
        self._cache = {}

    def fall_cost(self, extra_fallen: int | None = None) -> dict[int, int]:
        """For each cell, the fewest cities that must fall -- itself included --
        for that cell to be lost, given the map as it stands.

        The rim costs 1 (already exposed, just has to break). Anything inner
        costs one more than the cheapest cell shielding it.
        """
        cost: dict[int, int] = {}
        for ring in range(RADIUS, -1, -1):
            for c in self.cities.values():
                if c.ring != ring:
                    continue
                if c.fallen or c.cid == extra_fallen:
                    cost[c.cid] = 0
                elif ring == RADIUS:
                    cost[c.cid] = 1
                else:
                    cost[c.cid] = 1 + min(cost[k] for k in c.outward)
        return cost

    def distance_to_defeat(self, extra_fallen: int | None = None) -> int:
        """How many more cities must fall before the Cataclysm reaches the
        Pillar. Starts at 3; the Last Stand fires at 0.

        This -- not the raw count of cities lost -- is the number that decides
        the run. Twenty Outposts scattered around the rim cost less than three
        in a line.
        """
        cost = self.fall_cost(extra_fallen)
        return min(cost[c.cid] for c in self.cities.values() if c.ring == 1)

    def lane_criticality(self) -> dict[int, int]:
        """Per city: how much closer to defeat its loss would put you.

        Cities off every shortest lane score 0 and are, structurally, free to
        lose. Cities on one score 1 or more and are what actually needs saving.
        """
        if self._cache.get("crit_key") == self._cache_key:
            return self._cache["crit"]
        base = self.distance_to_defeat()
        out: dict[int, int] = {}
        for c in self.cities.values():
            if c.fallen or c.cid == self.pillar_id:
                out[c.cid] = 0
                continue
            out[c.cid] = max(0, base - self.distance_to_defeat(extra_fallen=c.cid))
        self._cache["crit_key"] = self._cache_key
        self._cache["crit"] = out
        return out

    def open_lanes(self) -> int:
        """How many sealed cells currently have a lane open into them."""
        return sum(1 for c in self.cities.values()
                   if c.ring < RADIUS and self.is_exposed(c))

    def breach_depth(self) -> int:
        """How far in the deepest breach reaches, 0 (intact) .. 3 (a Sanctuary)."""
        depth = 0
        for c in self.cities.values():
            if c.fallen:
                depth = max(depth, RADIUS + 1 - c.ring)
        return depth

    def render(self) -> str:
        """The map as text. '.' is intact, 'x' fallen, '!' exposed."""
        out = []
        for r in range(-RADIUS, RADIUS + 1):
            row = [" " * abs(r)]
            for c in range(-RADIUS, RADIUS + 1):
                cid = self.by_coord.get((r, c))
                if cid is None:
                    continue
                city = self.cities[cid]
                mark = ("x" if city.fallen
                        else "!" if self.is_exposed(city) else ".")
                row.append(f"{TIER_LABEL[city.tier][0]}{mark}")
            out.append(" ".join(row))
        return "\n".join(out)


def build_empire(cfg: TuningConfig) -> Empire:
    cities: dict[int, City] = {}
    by_coord: dict[tuple[int, int], int] = {}
    nid = 0
    pillar = -1

    for r in range(-RADIUS, RADIUS + 1):
        for c in range(-RADIUS, RADIUS + 1):
            ring = abs(r) + abs(c)
            if ring > RADIUS:
                continue
            tier = RING_TIER[ring]
            stats = cfg.TIER_STATS[tier]
            # The empire tree's city-health nodes raise how much damage a
            # city can absorb. Applied here rather than at the bite, because
            # `_retake` restores a fraction of `max_defense` too and a larger
            # pool has to mean a larger restore. Issue #1319. Population is
            # deliberately not scaled -- see `EmpireTree.city_health_mult`.
            city = City(cid=nid, name=f"{TIER_LABEL[tier]} ({r},{c})",
                        tier=tier, r=r, c=c,
                        max_defense=stats.max_defense * cfg.tree.city_health_mult,
                        max_population=stats.max_population)
            city.reset()
            cities[nid] = city
            by_coord[(r, c)] = nid
            if ring == 0:
                pillar = nid
            nid += 1

    # Orthogonal lanes. Every step changes the ring by exactly one.
    for city in cities.values():
        for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            k = by_coord.get((city.r + dr, city.c + dc))
            if k is None:
                continue
            other = cities[k]
            if other.ring > city.ring:
                city.outward.append(k)
            else:
                city.inward.append(k)

    # Perimeter links between rim Outposts (the curved edges in the sketch).
    for city in cities.values():
        if city.ring != RADIUS:
            continue
        for dr, dc in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
            k = by_coord.get((city.r + dr, city.c + dc))
            if k is not None and cities[k].ring == RADIUS:
                city.perimeter.append(k)

    return Empire(cities=cities, pillar_id=pillar, by_coord=by_coord)
