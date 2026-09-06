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


def cataclysm_order_for(cfg: TuningConfig, seed: int) -> tuple[str, ...]:
    """The order this campaign's character adds Cataclysms in, all eight of them.

    A CAMPAIGN IN THIS MODEL IS ONE RUN BY ONE CHARACTER, and that is what
    decides how this is seeded. `Simulation` plays a single run at a single
    `cfg.tier`: it ends the first time a Cataclysm dungeon is cleared or lost
    in, it never advances a tier, and nothing loops. The character it belongs
    to is implicit -- it drew this order when it was created, climbed to
    `cfg.tier`, and so faces the first `cfg.tier` entries.

    THE SEED IS THE CHARACTER. That one line is what makes the model obey the
    project owner's ruling of 2026-09-06 -- "if they are on t3 with
    demonic/war/death they restart with those same cataclysms" -- rather than
    merely not contradicting it:

    - Replaying a failed run is re-running the same seed, and the same seed
      draws the same order, so the replay meets the same Cataclysms. The design
      calls that a property of the character; here it is a property of the
      seed, and they are the same thing.
    - The same seed at tier N+1 draws the same order, so its active set is the
      tier N set plus one. That is one character climbing, not two unrelated
      worlds.
    - Different seeds are different characters, which is why averaging over
      campaigns now measures what a population of players meets rather than
      what one fixed world does.

    IT IS DRAWN FROM ITS OWN GENERATOR, not from `Simulation.rng`. Two reasons,
    and the second matters more. Taking these draws from the main stream would
    shift every later draw in the campaign, so a config change that altered the
    count would silently re-roll the whole run. And sweeps compare cells over a
    shared block of seeds; a private generator keyed only on the seed means two
    cells that differ in policy or empire tree still face the same 250
    characters, so the draw is common to both and cancels in the difference.

    THAT IS WHY THE NOISE FLOOR DOES NOT MOVE, and it was measured rather than
    argued. Issue #1338's decision expected sweeps to get noisier. Over 16
    disjoint blocks of 250 campaigns per cell -- 4,000 campaigns per condition
    -- at difficulty tier 1, the `triage` policy, no tree against the Architect
    preset, the floor for a difference between two cells sharing a seed block is
    3.0 points under this draw against 2.9 under the old fixed one at surge size
    5, the calibrated value the balance report uses, and 2.3 against 2.2 at the
    raw default of 4. Unpaired -- two cells on different seeds -- it is 1.7
    against 1.7 at surge 5 and 2.7 against 1.9 at surge 4. So only the unpaired
    floor moves, only at the surge size `exp_calibrate` rejected.

    `random.Random` seeds from a string via SHA-512, so this is stable across
    processes and Python versions in a way `hash()` is not.

    UNIFORM OVER ORDERINGS IS THIS MODEL'S ASSUMPTION AND NOT A DESIGN
    DECISION. The owner's ruling says the order is drawn per character and no
    more. Whether every ordering is equally likely, and whether any pairing is
    constrained, are both explicitly open -- `docs/DECISIONS.md` lists them and
    issue #1338 carries them. A uniform shuffle is the assumption that adds
    nothing the owner did not say; it is not evidence about what the game will
    do, and this docstring is where that is recorded so a later reader does not
    mistake the shuffle for a decision.
    """
    order = list(cfg.CATACLYSM_ROSTER)
    random.Random(f"cataclysm-order:{seed}").shuffle(order)
    return tuple(order)


def active_cataclysms_for(cfg: TuningConfig, seed: int) -> tuple[str, ...]:
    """The Cataclysms active in this campaign: the first N of its draw."""
    return cataclysm_order_for(cfg, seed)[:cfg.active_cataclysm_count()]


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
    defense_damage: float
    population_damage: float
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
    # THE EARNED BOSS ONLY, never the Last Stand: the two are built by different
    # rules and the owner ruled on 2026-09-06 that the Last Stand takes none of
    # the earned growth. Whether a Last Stand happened is `last_stand` above.
    cataclysm_floors: int         # how deep it was, 0 if it never opened
    cataclysm_floors_earned: int  # of those, how many came from clearing
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
        #: The Cataclysm dungeon opened by meeting the quest objectives, kept
        #: because clearing it removes it from everywhere else. A run can hold
        #: this AND a Last Stand: the Cataclysm can still reach the Pillar after
        #: the objectives were met.
        self.cataclysm: Dungeon | None = None
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
        #
        # HOW MANY comes from the difficulty tier and WHICH ONES from this
        # campaign's character. Both were fixed before issue #1338: the count
        # was 1 whatever the tier, and the set was always the first N of a
        # tuple beginning with Demonic -- the one Cataclysm of the eight that
        # ignores the frontier, so every lane-based figure this model has ever
        # produced was measured against the one that does not respect lanes.
        # `cataclysm_order_for` explains why the seed is the character.
        self.active_types = active_cataclysms_for(cfg, seed)
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
        #: Ordinary dungeons cleared, which is what deepens the earned boss.
        #: NOT `cleared`, which counts every kind. See
        #: `cataclysm_floors_per_dungeon_cleared`.
        self.basic_cleared = 0
        #: Floors the earned boss gained from `basic_cleared` when it opened.
        self.cataclysm_floors_earned = 0
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

        # Floor deltas from the tree change depth, and because this model charges
        # one day a floor they change run time and reward at the same time. The
        # game can separate the two; this model cannot.
        floors = max(1, int(round(floors + cfg.tree.floor_delta)))

        # Subtype and modifiers. One modifier per tier; Sacrificial starts with
        # double, which is exactly what makes it a gamble worth taking.
        subtype = self._roll_subtype(dtype, city)
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
            run_days=self._walk_days(floors, subtype),
            resolve_max=int(resolve),
            resolve_in=float(resolve),
            defense_damage=spec.defense_damage,
            population_damage=spec.population_damage,
            spawned_day=self.day,
            subtype=subtype,
            modifier_names=tuple(n for n, _ in picked),
            modifier_score=mod_score,
            power_scale=1.0 + cfg.dungeon_power_escalation_per_100_days * (self.day / 100.0),
        )
        self._next_did += 1
        self.dungeons[d.did] = d
        return d

    def _walk_days(self, floors: int, subtype: str) -> int:
        """How many days walking a dungeon of this depth and sub-type costs.

        WHY THIS IS NOT SIMPLY `run_days_for`. A Cow Level's time "is doubled
        and cannot be reduced", which is two rules: the doubling, and that the
        tree's reduction does not apply. `run_days_for` applies the reduction,
        so a Cow Level must not go through it.

        WHAT IT IS FOR is the callers that change a dungeon's depth AFTER
        `_make_dungeon` built it and have to work the days out again.
        `_maybe_open_cataclysm` and `_open_last_stand` both do that.

        **BOTH OF THOSE BUILD A CATACLYSM DUNGEON, AND A CATACLYSM DUNGEON CAN
        NO LONGER BE A COW LEVEL**, so neither can reach the branch below. The
        project owner ruled that on 2026-09-06 -- see `subtypes_allowed_on` --
        and it is what closes issue [#1333], which was about a Cow Level Last
        Stand losing its doubling to a bare `run_days_for`. They are called
        through this helper anyway, so that a later dungeon kind which CAN be a
        Cow Level and CAN change its depth gets the rule without anyone
        remembering to add it.
        """
        if subtype == "Cow Level":
            return max(1, int(math.ceil(floors * self.cfg.days_per_floor)) * 2)
        return self.run_days_for(floors)

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

    def sieges_on(self, city_id: int) -> int:
        """How many Siege dungeons are standing on this city."""
        return sum(1 for d in self.dungeons.values()
                   if d.subtype == "Siege" and d.city_id == city_id)

    def subtypes_allowed_on(self, dtype: DungeonType) -> list[str]:
        """The sub-types this kind of dungeon may roll, in weight order.

        **A CATACLYSM DUNGEON MAY NOT BE A COW LEVEL, AND THAT IS THE ONLY
        EXCLUSION THERE IS.** The project owner ruled on 2026-09-06, verbatim:
        "Last stand is a cataclysm dungeon and should not be allowed to roll as
        a cow level sub type." Asked how far to take it they answered "Only the
        one you ruled", so the other 27 pairs stay legal and nothing here may be
        read as a statement about any of them. Issue #1333;
        `config.SUBTYPES_FORBIDDEN_ON` carries the rule and the reasoning.

        WHY THE RULING WAS MADE THAT WAY RATHER THAN AS A REPAIR. A Cow Level's
        time "is doubled and cannot be reduced", and `_open_last_stand` recomputed
        the walk after adding its floor bonuses, dropping the doubling. Offered
        the repair, the owner removed the situation instead. The Last Stand's
        construction is therefore still left alone and the defect can no longer
        occur, which is why `_open_last_stand` needs no special case.
        """
        forbidden = self.cfg.SUBTYPES_FORBIDDEN_ON.get(dtype, ())
        return [n for n in self.cfg.SUBTYPE_SPAWN_WEIGHTS
                if n not in forbidden]

    def _roll_subtype(self, dtype: DungeonType, city: City) -> str:
        """One sub-type, respecting "Max 1 per city" for the Siege and the one
        dungeon-type exclusion in `subtypes_allowed_on`.

        A REFUSED SIEGE IS REDISTRIBUTED RATHER THAN DROPPED, so the dungeon
        still gets a sub-type -- every dungeon has one since issue #1293. That
        is what makes the Siege share of dungeons REACHING THE MAP lower than
        its share of dungeons ROLLED: 15 in 100 rolled, and about 12.7 in 100
        arriving, measured over twenty campaigns of the C++ implementation.
        Issue #1329.

        **A SUB-TYPE THE DUNGEON'S KIND FORBIDS IS REDISTRIBUTED THE SAME WAY,
        AND FOR THE SAME REASON**, but it is done by shortening the list the
        draw is taken from rather than by refusing afterwards. Both give each
        remaining sub-type a share in proportion to its weight; taking the
        shorter list costs no second draw, which the C++ port
        (`UCataclysmSurgeScheduler::RollSubType`) requires -- the wave rolls
        every dungeon from one stream, so a roll whose cost varied would make
        each dungeon depend on what the one before it drew.

        THE SIEGE REFUSAL STILL COSTS A SECOND DRAW HERE and does not in the
        game; that difference predates this and is issue #1329's territory, not
        this one's.
        """
        w = self.cfg.SUBTYPE_SPAWN_WEIGHTS
        names = self.subtypes_allowed_on(dtype)
        picked = self.rng.choices(names, weights=[w[n] for n in names], k=1)[0]

        if picked != "Siege":
            return picked
        if self.sieges_on(city.cid) < self.cfg.siege_max_per_city:
            return picked

        others = [n for n in names if n != "Siege"]
        return self.rng.choices(
            others, weights=[w[n] for n in others], k=1)[0]

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

        # ABSOLUTE POINTS. `city.max_defense` is deliberately absent: while the
        # damage was a fraction of it, it divided out of how many resolves the
        # city survived and every city-health upgrade in the design was worth
        # nothing. Issue #1327.
        city.defense -= d.defense_damage * scale * cfg.tree.city_damage_mult
        city.population -= d.population_damage * scale * cfg.tree.city_damage_mult
        city.population = max(0.0, city.population)

        if city.defense <= 0:
            self._fall(city)

        if cfg.dungeon_persists_after_resolve:
            d.resolve_in = float(d.resolve_max)
        else:
            self.dungeons.pop(d.did, None)

    def _apply_siege_damage(self) -> None:
        """Every standing Siege takes its daily share of the city it sits on.

        A SIEGE BITES EVERY DAY AND NOT ONLY WHEN ITS TIMER RUNS OUT. That is
        what makes it the dungeon a player cannot postpone, and it is the whole
        reason the sub-type is worth modelling: it is the one threat the empire
        tree's city-health nodes do not protect against. See
        `TuningConfig.siege_defence_bite_per_day` for why a share survives here
        when issue #1327 turned every other city damage into points.

        THE HOSTS ARE COLLECTED BEFORE ANYTHING IS BITTEN. `_fall` removes
        dungeons from `self.dungeons`, and walking that dictionary while it is
        being emptied is how a crash gets written. The C++ beside this takes
        the same care for the same reason.
        """
        cfg = self.cfg
        hosts: list[tuple[int, int]] = []
        for d in self.dungeons.values():
            if d.subtype != "Siege":
                continue
            # NEVER NEGATIVE. A dungeon built by hand in a test may carry a
            # spawn day later than the clock, and a negative age would heal
            # the city rather than hurt it.
            hosts.append((d.city_id, max(0, self.day - d.spawned_day)))

        for city_id, days_stood in hosts:
            city = self.empire.cities.get(city_id)
            # CHECKED AGAIN FOR EACH ONE. A city that fell to an earlier Siege
            # in this same loop must not be bitten a second time.
            if city is None or city.fallen:
                continue

            grown = cfg.siege_damage_growth_per_day * days_stood

            # THE EMPIRE TREE'S DAMAGE REDUCTION STILL APPLIES, AND THAT IS
            # A RULING RATHER THAN A READING. It was written here as a reading:
            # the owner's exception is that city HEALTH does not protect
            # against a siege -- "a siege does not care how thick your walls
            # are" -- and reducing the damage is a different claim from
            # thickening the wall. Put to the owner as a confirm-or-overturn
            # against two alternatives, that a siege ignores both defensive
            # lines and that reduction applies at a reduced rate. They answered
            # on 2026-09-06, verbatim: "Yes — damage reduction still applies
            # (Recommended)".
            #
            # SO A SIEGE IGNORES HOW MUCH A CITY CAN ABSORB AND IS STILL
            # BLUNTED BY WHAT REDUCES DAMAGE. Both defensive lines stay
            # meaningful, which is also why the combined defensive ceiling
            # still has two things to multiply. Issue #1329.
            city.defense -= (city.max_defense * cfg.siege_defence_bite_per_day
                             + grown) * cfg.tree.city_damage_mult
            city.population -= (
                city.max_population * cfg.siege_population_bite_per_day
                + grown) * cfg.tree.city_damage_mult
            city.population = max(0.0, city.population)

            if city.defense <= 0:
                self._fall(city)

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

        # ONLY ORDINARY DUNGEONS DEEPEN THE BOSS. `cleared` above counts every
        # kind and is the wrong number for this; see
        # `cataclysm_floors_per_dungeon_cleared`.
        if d.dtype is DungeonType.BASIC:
            self.basic_cleared += 1

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

        NEAR-FATAL BY DESIGN, AND LOSING IT IS NOT A BUG. Letting the empire
        collapse is meant to be close to a loss; the project owner settled that
        on 2026-09-05 and `docs/DECISIONS.md` records it.

        **THE FIGURE THE RULING WAS MADE ON IS NO LONGER THE FIGURE.** Measured
        on 2026-09-06 over 10,000 campaigns in two disjoint seed blocks, with
        every setting held at what issue #1286 names:

          - **About 1 in 84 PER LAST STAND REACHED**, 33 wins in 2,768, against
            the 1 in 54 #1286 reported. The 95% range is 1 in 63 to 1 in 127, so
            1 in 54 is outside it.
          - **About 1 in 70 per Last Stand ENTERED**, 33 wins in 2,322, and on
            THAT denominator 1 in 54 is not excluded. A quarter of Last Stands
            are never entered because the day cap arrives first, so the two
            denominators give different verdicts on the same data. #1286 counted
            reached, so reached is the comparison its ruling rests on.
          - **Reached in 27.7% of campaigns**, against 13.5%. The fight is about
            twice as common as when the ruling was made.
          - Averaging 438 and 440 floors, which is #1286's "about 440" unchanged.
          - The earned Cataclysm dungeon is won **40.4%** of the time, against
            57%.

        WHAT MOVED IT, AND WHAT DID NOT. Of the 12.7 point rise in how often the
        fight happens, the dungeon sub-type distribution change accounts for
        about 6.6 points and the boss growing with dungeons cleared for about
        2.4. **The remaining 4.5 points are not attributable to any known
        change** -- with both undone the rate is 18.0%, with two blocks agreeing
        exactly. That is issue #1343. The change in the WIN RATE is not
        attributed at all.

        **THE RULING IS UNAFFECTED.** The owner ruled that a collapse should be
        near-fatal. Both figures moved in the direction that satisfies it, so
        nothing here reopens the decision -- only the numbers under it.

        **AND THEN #1345 MOVED THEM AGAIN, MUCH FURTHER.** Teaching the model
        what a Siege does to a city took the Last Stand from 26% of campaigns to
        **99%**, measured the same way. The figures above are with the three
        Siege damage settings zeroed, which is the only way to compare with what
        #1286 recorded; with them on, current behaviour is:

          - Last Stand reached in **99.1%** of campaigns.
          - Won about **1 in 19** of those reached -- easier, because it is now
            met with the empire in better shape and at fewer floors, 386 against
            438.
          - **The earned Cataclysm dungeon opens in 2.4% of campaigns**, against
            74.7%. The route the design treats as the ordinary win condition has
            almost stopped happening.

        THAT IS ATTRIBUTED AND ISOLATED. Zeroing `siege_defence_bite_per_day`,
        `siege_population_bite_per_day` and `siege_damage_growth_per_day`
        reproduces 25.8% and 26.0% against the 26.2% measured before #1345, so
        nothing else in it is responsible. **Whether a Siege should be that
        decisive is a design question and has not been put to the owner.**

        **RE-MEASURED AGAIN ON 2026-09-06 FOR ISSUE #1333**, which barred a
        Cataclysm dungeon from rolling Cow Level and so changed the draw this
        fight is built from. 2,000 campaigns in two disjoint blocks of 1,000
        seeds, at tier 1, `No tree`, `triage`, STATIC surges every 120 days x5
        (`surge_dungeon_count=5`, not the default of 4), resolve ratio 2.0,
        escalation 0.10 per 100 days, craft 12 days +4%, **with the Siege damage
        settings live**:

          - Last Stand **reached in 96.3% of campaigns** and entered in 1,836 of
            them.
          - Won **1 in 26.4 of those reached** -- 73 wins in 1,926 -- and 1 in
            25.2 of those entered.
          - Averaging **407 floors**.
          - The earned Cataclysm dungeon **opens in 8.1% of campaigns** and is
            won 41.6% of the time.

        **THE SAME 2,000 SEEDS WITH THE RULE UNDONE give 1 in 28.8 reached, 67
        wins in 1,926, and the same 96.3% and 407 floors.** So barring one
        sub-type of seven moved the win rate by 0.31 of a percentage point on a
        base of 3.5, which is well inside the noise at this sample: six wins
        against a standard deviation of about eight. **The honest reading is
        that the ruling did not measurably change this fight**, not that it made
        it easier. 115 of the 1,926 Last Stands were Cow Levels before the rule
        and none after, so the comparison is between two different things and
        the nil result is a result.

        WHY 96.3% RATHER THAN THE 99.1% ABOVE. Both were measured the same way;
        what changed between them is commit `b196cd9`, which draws the active
        Cataclysms from the campaign seed and ties their count to the tier. A
        seeded campaign does not replay a figure measured before it.

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

        # THROUGH `_walk_days` RATHER THAN `run_days_for`, WHICH CHANGES NOTHING
        # TODAY AND IS THE POINT. A bare `run_days_for` knows nothing about
        # sub-types, so it dropped a Cow Level Last Stand's doubled walk -- issue
        # #1333. The owner removed the situation instead of repairing it: a
        # Cataclysm dungeon can no longer roll Cow Level, so the two calls give
        # the same answer for every dungeon that can reach this line. This one
        # is the one that stays right if that ever changes.
        d.run_days = self._walk_days(d.floors, d.subtype)
        d.power_scale *= 1.0 + ruin * cfg.last_stand_power_per_fallen_city
        d.is_last_stand = True

        for a in absorbed:
            self.dungeons.pop(a.did, None)

        self.last_stand = d

    def _maybe_open_cataclysm(self) -> None:
        """Once the quest objectives are met, the enemy capital opens.

        IT IS AS DEEP AS THE WORK THE PLAYER CHOSE TO DO. The design document
        says "every dungeon defeated adds one floor to the Cataclysm boss
        dungeon", and until issue #1315 nothing added any: a campaign that
        cleared thirty dungeons met the same boss as one that cleared five.

        ONLY ORDINARY DUNGEONS COUNT, so beelining the objectives gives a
        smaller boss than clearing the map. See
        `cataclysm_floors_per_dungeon_cleared` for the ruling and the reasons.

        THIS IS NOT WHERE THE LAST STAND IS BUILT. `_open_last_stand` has its
        own bonuses and takes none of this; the two are deliberately separate.
        """
        cfg = self.cfg
        if self.objectives < cfg.quest_objectives_required:
            return
        if any(x.dtype is DungeonType.CATACLYSM for x in self.dungeons.values()):
            return

        d = self._make_dungeon(DungeonType.CATACLYSM,
                               self.empire.cities[self.empire.pillar_id])

        d.floors += self.basic_cleared * cfg.cataclysm_floors_per_dungeon_cleared
        d.run_days = self._walk_days(d.floors, d.subtype)

        #: HOW MANY OF ITS FLOORS WERE EARNED, recorded now rather than worked
        #: out at the end: more ordinary dungeons can be cleared after the boss
        #: opens, and they do not deepen a dungeon that already exists.
        self.cataclysm_floors_earned = (
            self.basic_cleared * cfg.cataclysm_floors_per_dungeon_cleared)
        self.cataclysm = d

    # -- main loop -------------------------------------------------------

    def step(self, policy) -> None:
        cfg = self.cfg
        self.day += 1

        if self.day >= self.next_surge_day:
            self.trigger_surge()

        # A SIEGE TAKES ITS SHARE BEFORE ANY TIMER IS ACTED ON, which is the
        # order `UCataclysmEmpireRun::AdvanceDay` uses. A Siege that arrived
        # today deals the flat share alone; each later day adds its growth.
        self._apply_siege_damage()

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
        # winnable in principle and almost never in practice -- measured on
        # 2026-09-06 at about 1 in 84 per Last Stand REACHED, against 40.4% for
        # a Cataclysm dungeon the player opened by clearing quest objectives.
        # See `_open_last_stand`, which carries the denominators and what moved
        # them.
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
            cataclysm_floors=(0 if self.cataclysm is None
                              else self.cataclysm.floors),
            cataclysm_floors_earned=self.cataclysm_floors_earned,
            final_population_frac=(self.empire.total_population()
                                   / max(1.0, self.empire.max_population())),
            idle_days=self.idle_days,
            decision_days=self.decision_days,
            free_days=self.free_days,
            overcommitted=self.overcommitted,
        )
