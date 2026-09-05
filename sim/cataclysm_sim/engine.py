"""The day loop.

One tick == one in-game day. The player is a single actor who is either idle
at the Pillar or committed to a dungeon for a fixed number of days. Everything
the strategy layer does happens in `Simulation.step`.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass

from . import combat, scoring
from .config import CityTier, DungeonType, SurgeMode, TuningConfig
from .modifiers import pool_for
from .patterns import DEFAULT as PATTERN_DEFAULT, PATTERNS
from .world import City, Empire, build_empire

# DungeonType -> the string keys scoring.py uses.
SCORING_TYPE = {
    DungeonType.BASIC: "Basic",
    DungeonType.QUEST: "Quest",
    DungeonType.FALLEN_CITY: "Fallen City",
    DungeonType.CATACLYSM: "Cataclysm",
}


class _Craft:
    """Sentinel action: spend days at the forge instead of in a dungeon."""
    def __repr__(self):
        return "CRAFT"


CRAFT = _Craft()


@dataclass
class Dungeon:
    did: int
    dtype: DungeonType
    city_id: int
    city_tier: CityTier
    floors: int
    run_days: int
    resolve_max: int
    resolve_in: float
    defense_bite: float
    population_bite: float
    spawned_day: int
    subtype: str = "None"
    modifier_names: tuple[str, ...] = ()
    modifier_score: float = 0.0
    power_scale: float = 1.0
    is_last_stand: bool = False
    source: str = ""            # which Cataclysm sent it
    times_resolved: int = 0

    @property
    def resolves(self) -> bool:
        """Quest dungeons refresh instead of resolving; Fallen City and
        Cataclysm dungeons have already done their damage."""
        return self.dtype is DungeonType.BASIC


@dataclass
class RunResult:
    survived_days: int
    won: bool
    lost: bool
    cities_lost: int
    outposts_lost: int
    bulwarks_lost: int
    sanctuaries_lost: int
    dungeons_cleared: int
    dungeons_resolved: int          # times a dungeon detonated undefeated
    objectives: int                 # quest dungeons cleared toward the win
    floors_cleared: int             # loot proxy -- reward scales with depth
    surges: int
    final_surge_gap: float
    final_surge_count: int
    empire_points: float
    power: float                    # final player power
    crafts: int
    craft_days: int
    deaths: int
    min_distance_to_defeat: int   # closest the Cataclysm ever came (3 = untouched)
    last_stand: bool              # did the Cataclysm reach the Pillar?
    final_population_frac: float
    idle_days: int                  # days with nothing worth doing
    decision_days: int              # free days facing 2+ urgent dungeons
    free_days: int                  # days the player was choosing at all
    overcommitted: int              # dungeons entered that could not finish in time

    @property
    def triage_pressure(self) -> float:
        """Fraction of the player's decision points that were real triage --
        two or more dungeons about to detonate and only time for one.

        This is the health metric for the whole strategy layer. If it is near
        zero the empire game is decoration.
        """
        return 0.0 if self.free_days == 0 else self.decision_days / self.free_days


class Simulation:
    def __init__(self, cfg: TuningConfig, seed: int = 0):
        self.cfg = cfg
        self.rng = random.Random(seed)
        self.empire: Empire = build_empire(cfg)
        self.dungeons: dict[int, Dungeon] = {}
        self._next_did = 0

        self.day = 0
        self.last_stand: Dungeon | None = None
        self.next_surge_day = 0.0
        self.surge_index = 0        # how many surges have happened
        self.surge_log: list[tuple[int, float, int]] = []  # (day, gap, count)

        self.current: Dungeon | None = None
        self.crafting = False
        self.dying = False
        self.days_remaining = 0

        # Active Cataclysms pool their modifiers together, plus the Generic
        # ones, which every Cataclysm can draw. `modifiers.pool_for` owns that
        # rule so this and `analyse_dungeons.py` cannot disagree about it.
        self.active_types = cfg.CATACLYSM_ROSTER[:max(1, cfg.active_cataclysms)]
        self.modifier_pool = pool_for(self.active_types)

        self.tier_min, self.tier_max = scoring.tier_bounds(cfg.tier)
        self.tier_width = self.tier_max - self.tier_min
        self.power = self.tier_min + self.tier_width * cfg.power_start_frac
        self.materials = 0.0
        self.crafts_done = 0
        self.craft_days_spent = 0
        self.deaths = 0
        self.min_d2d = 3

        # tallies
        self.objectives = 0
        self.cleared = 0
        self.resolved = 0
        self.floors_cleared = 0     # loot proxy: reward scales with depth
        self.empire_points = 0.0
        self.idle_days = 0
        self.decision_days = 0
        self.free_days = 0
        self.overcommitted = 0
        self.lost = False
        self.won = False

    # -- construction ----------------------------------------------------

    def _make_dungeon(self, dtype: DungeonType, city: City,
                      floors_mult: float = 1.0) -> Dungeon:
        cfg = self.cfg
        spec = cfg.spec(dtype, city.tier)
        lo, hi = spec.floors
        floors = max(1, int(round(self.rng.randint(lo, hi) * floors_mult)))

        # Floor deltas from the tree change depth, and because one floor costs
        # one day they change run time and reward at the same time.
        floors = max(1, int(round(floors + cfg.tree.floor_delta)))

        # Subtype and modifiers. One modifier per tier; Sacrificial starts with
        # double, which is exactly what makes it a gamble worth taking.
        subtype = self._roll_subtype(dtype)
        n_mods = cfg.modifiers_per_tier * cfg.tier
        if subtype == "Sacrificial":
            n_mods *= cfg.sacrificial_modifier_multiplier
        pool = self.modifier_pool
        picked: list[tuple[str, float]] = []
        if pool and n_mods > 0:
            k = min(n_mods, len(pool))
            picked = self.rng.sample(pool, k)
        mod_score = sum(s for _, s in picked)

        if dtype is DungeonType.BASIC:
            # Timer scales with depth, so a deep dungeon is a big commitment
            # rather than an automatic loss.
            base = cfg.resolve_base_days + floors * cfg.resolve_floor_ratio
            jitter = 1.0 + self.rng.uniform(-cfg.resolve_jitter, cfg.resolve_jitter)
            resolve = base * jitter + cfg.tree.resolve_bonus_days
        else:
            # Quest dungeons relocate, Fallen Cities and the Cataclysm have
            # already done their damage. None of them detonate.
            resolve = spec.resolve_days[0]

        d = Dungeon(
            did=self._next_did,
            dtype=dtype,
            city_id=city.cid,
            city_tier=city.tier,
            floors=floors,
            # Cow Level: "time to complete is doubled and cannot be reduced".
            run_days=(self.run_days_for(floors) if subtype != "Cow Level"
                      else max(1, int(math.ceil(floors * cfg.days_per_floor)) * 2)),
            resolve_max=int(resolve),
            resolve_in=float(resolve),
            defense_bite=spec.defense_bite,
            population_bite=spec.population_bite,
            spawned_day=self.day,
            subtype=subtype,
            modifier_names=tuple(n for n, _ in picked),
            modifier_score=mod_score,
            power_scale=1.0 + cfg.dungeon_power_escalation_per_100_days * (self.day / 100.0),
        )
        self._next_did += 1
        self.dungeons[d.did] = d
        return d

    def run_days_for(self, floors: int) -> int:
        """UNKNOWN #1, and the single most consequential formula in the game.

        Flat reduction is applied first (that is how the tree reads today),
        then the multiplicative scalar (the proposed replacement), then the
        floor. Setting run_days_flat=0 and run_days_mult<1 models the fix.
        """
        cfg = self.cfg
        base = floors * cfg.days_per_floor
        days = base - cfg.tree.run_days_flat
        days *= cfg.tree.run_days_mult
        days = max(cfg.run_days_min, min(cfg.run_days_max, days))
        return int(math.ceil(days))

    # -- power -----------------------------------------------------------

    def dungeon_power(self, d: Dungeon) -> float:
        """The dungeon's headline difficulty -- its middle floor, rarity-mixed,
        including its modifiers."""
        cfg = self.cfg
        base = scoring.dungeon_score(cfg.tier, SCORING_TYPE[d.dtype], d.subtype,
                                     d.floors, d.modifier_score)
        return base * d.power_scale

    def death_chance(self, d: Dungeon) -> float:
        """Overwhelm, evaluated across the dungeon's whole floor range.

        Not a gate. A player who out-powers every floor takes no risk; one who
        is behind takes progressively more, and the last floor is where it
        actually bites.
        """
        cfg = self.cfg
        # power_scale represents the Cataclysm strengthening over the run; fold
        # it in by treating the player as correspondingly weaker.
        effective_power = self.power / max(1e-9, d.power_scale)
        return combat.death_chance(
            player_power=effective_power,
            tier=cfg.tier,
            dtype=SCORING_TYPE[d.dtype],
            subtype=d.subtype,
            total_floors=d.floors,
            modifier_score=d.modifier_score,
            per_floor_risk=cfg.per_floor_risk,
            boss_risk_multiplier=cfg.boss_risk_multiplier,
            boss_rarity=("Cataclysm Boss" if d.dtype is DungeonType.CATACLYSM
                         else "Boss"),
            rate=cfg.overwhelm_rate,
            cap=cfg.overwhelm_cap,
        )

    def _roll_subtype(self, dtype: DungeonType) -> str:
        w = self.cfg.SUBTYPE_SPAWN_WEIGHTS
        names = list(w)
        return self.rng.choices(names, weights=[w[n] for n in names], k=1)[0]

    def can_craft(self) -> bool:
        return self.materials >= self.cfg.craft_material_cost

    # -- surges ----------------------------------------------------------

    def surge_count(self) -> int:
        """Dungeons in the next surge.

        Grows with the surge index under SWELLING/BOTH, and with the number of
        simultaneous Cataclysms in every mode.

        The lethality mode multiplies the result LAST, after the cap. Heretic's
        rule is stated without qualification -- "Surges spawn 25% more dungeons"
        -- while `surge_count_max` bounds how far the Cataclysm's own escalation
        runs, which is a different thing. Applying the multiplier before the cap
        would make Heretic identical to Standard at every surge that reaches it,
        which is where the extra dungeons would hurt most. Issue #289.
        """
        cfg = self.cfg
        n = float(cfg.surge_dungeon_count)
        if cfg.surge_mode in (SurgeMode.SWELLING, SurgeMode.BOTH):
            n += cfg.surge_count_growth * self.surge_index
        n = min(n, float(cfg.surge_count_max))
        return max(1, int(n * cfg.surge_dungeon_multiplier))

    def surge_gap(self) -> float:
        """Days until the surge after this one.

        Shrinks with the surge index under ACCELERATING/BOTH.
        """
        cfg = self.cfg
        gap = cfg.surge_interval_days
        if cfg.surge_mode in (SurgeMode.ACCELERATING, SurgeMode.BOTH):
            gap *= cfg.surge_interval_decay ** self.surge_index
        gap = max(cfg.surge_interval_min, gap)
        return gap + cfg.tree.surge_bonus_days

    def trigger_surge(self, from_city_fall: bool = False) -> None:
        cfg = self.cfg
        # Only the exposed frontier can be attacked. Everything behind a
        # standing city is sealed until that city falls.
        targets = self.empire.exposed_cities()
        if not targets:
            return
        weights = [cfg.SURGE_TARGET_WEIGHT[c.tier] for c in targets]
        if sum(weights) <= 0:
            return

        # The wave is split between the active Cataclysms, each attacking in
        # its own way. Three Cataclysms are three different problems, not one
        # problem three times over. Volume is weighted by each pattern's
        # count_mult, so a Death wave really is a swarm and a Celestial one
        # really is a handful of judgements.
        pats = [PATTERNS.get(t, PATTERN_DEFAULT) for t in self.active_types]
        cmults = [p.count_mult for p in pats]
        volume = sum(cmults) ** cfg.cataclysm_volume_exponent
        count = max(1, int(round(self.surge_count() * volume)))

        for _ in range(count):
            idx = self.rng.choices(range(len(self.active_types)),
                                   weights=cmults, k=1)[0]
            ctype = self.active_types[idx]
            pat = pats[idx]

            pool = (self.empire.alive_cities() if pat.ignores_frontier
                    else targets)
            pool = [c for c in pool if c.cid != self.empire.pillar_id]
            if not pool:
                continue
            w = [max(0.0, pat.weight(self.empire, c)) for c in pool]
            if sum(w) <= 0:
                continue

            city = self.rng.choices(pool, weights=w, k=1)[0]
            dtype = (DungeonType.QUEST
                     if self.rng.random() < cfg.quest_dungeon_chance
                     else DungeonType.BASIC)
            d = self._make_dungeon(dtype, city, floors_mult=pat.floors_mult)
            d.source = ctype
            if pat.erases_cities:
                city.doomed = True

        gap = self.surge_gap()
        self.surge_log.append((self.day, gap, count))
        self.next_surge_day = self.day + gap

        # A city falling is itself an escalation, not just an extra wave.
        if not from_city_fall or cfg.city_fall_advances_escalation:
            self.surge_index += 1

    # -- consequences ----------------------------------------------------

    def _resolve(self, d: Dungeon) -> None:
        """A dungeon reached the end of its timer undefeated."""
        cfg = self.cfg
        city = self.empire.cities[d.city_id]

        # A quest dungeon does not detonate; it picks up and moves.
        if d.dtype is DungeonType.QUEST:
            d.resolve_in = float(d.resolve_max)
            if cfg.quest_relocates:
                targets = self.empire.exposed_cities()
                if targets:
                    new_city = self.rng.choice(targets)
                    d.city_id = new_city.cid
                    d.city_tier = new_city.tier
            return

        d.times_resolved += 1
        self.resolved += 1

        if not d.resolves or city.fallen:
            d.resolve_in = float(d.resolve_max)
            return

        # Bigger dungeons hit harder. Scale the bite by how deep this one is
        # relative to a typical dungeon of its type on this tier.
        lo, hi = cfg.spec(d.dtype, d.city_tier).floors
        typical = (lo + hi) / 2.0
        scale = d.floors / typical

        city.defense -= city.max_defense * d.defense_bite * scale * cfg.tree.city_damage_mult
        city.population -= city.max_population * d.population_bite * scale * cfg.tree.city_damage_mult
        city.population = max(0.0, city.population)

        if city.defense <= 0:
            self._fall(city)

        if cfg.dungeon_persists_after_resolve:
            d.resolve_in = float(d.resolve_max)
        else:
            self.dungeons.pop(d.did, None)

    def _fall(self, city: City) -> None:
        cfg = self.cfg
        city.defense = 0.0
        city.fallen = True
        if city.doomed:
            city.erased = True     # Void: never coming back
        self.empire._invalidate()

        # Every dungeon sitting on the city is absorbed into the Fallen City.
        absorbed = [d for d in self.dungeons.values() if d.city_id == city.cid]
        for d in absorbed:
            if self.current is not None and d.did == self.current.did:
                continue
            self.dungeons.pop(d.did, None)

        # An erased city leaves no Fallen City dungeon -- there is nothing left
        # to retake, and the lane stays open for the rest of the run.
        if not city.erased:
            self._make_dungeon(DungeonType.FALLEN_CITY, city)

        if cfg.surge_on_city_fall:
            self.trigger_surge(from_city_fall=True)

    def _retake(self, city: City) -> None:
        city.fallen = False
        self.empire._invalidate()
        city.defense = city.max_defense * 0.5
        city.population = city.max_population * 0.5

    # -- the player ------------------------------------------------------

    def _urgent(self, d: Dungeon) -> bool:
        """A dungeon the player cannot safely postpone: it will detonate
        before they could finish it."""
        return d.resolves and d.resolve_in <= d.run_days

    def _finish_craft(self) -> None:
        cfg = self.cfg
        self.materials -= cfg.craft_material_cost
        self.power += self.tier_width * cfg.craft_power_gain_frac
        self.crafts_done += 1
        self.craft_days_spent += cfg.craft_days
        self.crafting = False

    def _finish_current(self) -> None:
        d = self.current
        assert d is not None
        cfg = self.cfg

        # The attempt can still kill you if you went in underpowered.
        if self.rng.random() < self.death_chance(d):
            self.deaths += 1
            self.current = None
            # Dying to the Cataclysm -- whether you went to it or it came to
            # you in the Last Stand -- ends the run.
            if d.dtype is DungeonType.CATACLYSM:
                self.lost = True
                return
            self.days_remaining = cfg.death_day_cost
            self.dying = True
            return

        self.cleared += 1
        self.floors_cleared += d.floors
        self.power += d.floors * self.tier_width * cfg.power_gain_per_floor_frac
        self.materials += d.floors * cfg.material_per_floor
        self.empire_points += self.cfg.empire_points_per_dungeon.get(d.dtype, 1.0)

        city = self.empire.cities[d.city_id]
        if d.dtype is DungeonType.FALLEN_CITY and city.fallen:
            self._retake(city)

        if d.dtype is DungeonType.QUEST:
            self.objectives += 1
            self._maybe_open_cataclysm()

        if d.dtype is DungeonType.CATACLYSM:
            self.won = True

        self.dungeons.pop(d.did, None)
        self.current = None

    def _open_last_stand(self) -> None:
        """The Cataclysm assaults the Pillar, absorbing everything still standing.

        NEAR-FATAL BY DESIGN, AND THE 2% WIN RATE IS NOT A BUG. Letting the
        empire collapse is meant to be close to a loss; the project owner
        settled that on 2026-09-05 and `docs/DECISIONS.md` records it. Measured
        over 400 campaigns at tier 1: 54 Last Stands, 1 won, averaging about 440
        floors, against about 126 floors and a 57% win rate for a Cataclysm
        dungeon opened by clearing quest objectives instead. Issue #1286.

        WHY IT COMES OUT SO LOPSIDED. Every term below grows with how badly the
        run has already gone, and a player only ever reaches this by having lost
        cities: a flat bonus, five floors for each dungeon still standing when
        the map is swallowed, four more per city already lost, and a power
        multiplier that also scales with the cities lost.

        THE FOUR CONSTANTS ARE NOT TO BE TUNED TOWARDS A FAIRER FIGHT. That is
        the outcome the design wants, and anyone reading the 2% cold will read
        it as broken. It is not.
        """
        cfg = self.cfg
        if self.last_stand is not None:
            return
        absorbed = [d for d in self.dungeons.values()
                    if d.dtype is not DungeonType.CATACLYSM
                    and (self.current is None or d.did != self.current.did)]

        ruin = len(self.empire.fallen_cities())

        d = self._make_dungeon(DungeonType.CATACLYSM,
                               self.empire.cities[self.empire.pillar_id])
        d.floors += (cfg.last_stand_floor_bonus
                     + len(absorbed) * cfg.last_stand_floors_per_absorbed
                     + ruin * cfg.last_stand_floors_per_fallen_city)
        d.run_days = self.run_days_for(d.floors)
        d.power_scale *= 1.0 + ruin * cfg.last_stand_power_per_fallen_city
        d.is_last_stand = True

        for a in absorbed:
            self.dungeons.pop(a.did, None)

        self.last_stand = d

    def _maybe_open_cataclysm(self) -> None:
        """Once the quest objectives are met, the enemy capital opens."""
        if self.objectives < self.cfg.quest_objectives_required:
            return
        if any(x.dtype is DungeonType.CATACLYSM for x in self.dungeons.values()):
            return
        self._make_dungeon(DungeonType.CATACLYSM, self.empire.cities[self.empire.pillar_id])

    # -- main loop -------------------------------------------------------

    def step(self, policy) -> None:
        cfg = self.cfg
        self.day += 1

        if self.day >= self.next_surge_day:
            self.trigger_surge()

        # Timers tick. Whether the dungeon the player is currently inside also
        # ticks is a rules question with real design weight -- see config.
        for d in list(self.dungeons.values()):
            inside = self.current is not None and d.did == self.current.did
            if inside and not cfg.timer_ticks_while_running:
                continue
            d.resolve_in -= 1.0

        for d in list(self.dungeons.values()):
            if d.resolve_in <= 0:
                self._resolve(d)

        # A Sanctuary has fallen: the Cataclysm reaches the Pillar and comes to
        # the player. In practice this is the end of the run. The fight is
        # winnable in principle and almost never in practice -- measured at 2%
        # over 400 campaigns at tier 1, against 57% for a Cataclysm dungeon the
        # player opened by clearing quest objectives. See `_open_last_stand`.
        self.min_d2d = min(self.min_d2d, self.empire.distance_to_defeat())
        if self.empire.pillar_exposed():
            self._open_last_stand()

        # -- player action ------------------------------------------------

        # Respawning after a death.
        if self.dying:
            self.days_remaining -= 1
            if self.days_remaining <= 0:
                self.dying = False
            return

        # At the forge.
        if self.crafting:
            self.days_remaining -= 1
            if self.days_remaining <= 0:
                self._finish_craft()
            return

        # Inside a dungeon.
        if self.current is not None:
            self.days_remaining -= 1
            if self.days_remaining <= 0:
                self._finish_current()
            return

        self.free_days += 1
        candidates = list(self.dungeons.values())

        urgent = [d for d in candidates if self._urgent(d)]
        if len(urgent) >= 2:
            self.decision_days += 1

        choice = policy(self, candidates)

        if choice is CRAFT:
            self.crafting = True
            self.days_remaining = cfg.craft_days
            return

        if choice is None:
            self.idle_days += 1
            return

        if choice.resolves and choice.resolve_in < choice.run_days:
            self.overcommitted += 1

        self.current = choice
        self.days_remaining = choice.run_days

    def run(self, policy) -> RunResult:
        while self.day < self.cfg.max_days and not self.lost and not self.won:
            self.step(policy)

        lost_by_tier = {t: 0 for t in CityTier}
        for c in self.empire.fallen_cities():
            lost_by_tier[c.tier] += 1

        return RunResult(
            survived_days=self.day,
            won=self.won,
            lost=self.lost,
            cities_lost=sum(lost_by_tier.values()),
            outposts_lost=lost_by_tier[CityTier.OUTPOST],
            bulwarks_lost=lost_by_tier[CityTier.BULWARK],
            sanctuaries_lost=lost_by_tier[CityTier.SANCTUARY],
            dungeons_cleared=self.cleared,
            dungeons_resolved=self.resolved,
            objectives=self.objectives,
            floors_cleared=self.floors_cleared,
            surges=len(self.surge_log),
            final_surge_gap=(self.surge_log[-1][1] if self.surge_log else 0.0),
            final_surge_count=(self.surge_log[-1][2] if self.surge_log else 0),
            empire_points=self.empire_points,
            power=self.power,
            crafts=self.crafts_done,
            craft_days=self.craft_days_spent,
            deaths=self.deaths,
            min_distance_to_defeat=self.min_d2d,
            last_stand=self.last_stand is not None,
            final_population_frac=(self.empire.total_population()
                                   / max(1.0, self.empire.max_population())),
            idle_days=self.idle_days,
            decision_days=self.decision_days,
            free_days=self.free_days,
            overcommitted=self.overcommitted,
        )
