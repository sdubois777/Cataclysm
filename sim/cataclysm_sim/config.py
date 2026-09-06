"""All tunable numbers in one place.

Anything tagged UNKNOWN is a number the design documents do not currently
specify. Deriving good values for these is the entire point of the rig.

Naming follows the canonical scheme: Outpost / Bulwark / Sanctuary / Pillar.
(GDD v0.3 still calls these Village / City / Metropolis / Capital. The JSON
trees use the newer names, and the newer names win.)
"""

from __future__ import annotations

from dataclasses import dataclass, field, replace
from enum import Enum


class CityTier(str, Enum):
    OUTPOST = "Outpost"
    BULWARK = "Bulwark"
    SANCTUARY = "Sanctuary"
    PILLAR = "Pillar"


class DungeonType(str, Enum):
    BASIC = "Basic"
    QUEST = "Quest"
    FALLEN_CITY = "FallenCity"
    CATACLYSM = "Cataclysm"


class SurgeMode(str, Enum):
    """How the Cataclysm escalates surge over surge.

    STATIC       fixed gap, fixed dungeon count (no escalation)
    ACCELERATING gap shrinks each surge, count fixed
    SWELLING     gap fixed, count grows each surge
    BOTH         gap shrinks and count grows
    """
    STATIC = "static"
    ACCELERATING = "accelerating"
    SWELLING = "swelling"
    BOTH = "both"


class LethalityMode(str, Enum):
    """Which of the three difficulty modes a character was created in.

    From the Difficulty Options section of `docs/Cataclysm_GDD_v2.md`. Locked at
    character creation and never changed, so one campaign is one mode throughout.
    Issue #289 asked for this, because the empire upgrade tree is partitioned by
    lethality mode (issue #277) and nothing here represented the modes at all.
    """
    STANDARD = "standard"
    HARDCORE = "hardcore"
    HERETIC = "heretic"


@dataclass(frozen=True)
class LethalityRules:
    """What one lethality mode changes about the strategy layer.

    ONLY THE PARTS THIS SIMULATION CAN REPRESENT. The design document gives each
    mode four columns and two of them have no model here:

      * Equipment lost on death -- 10% of 18 equipped pieces on Hardcore, 20%
        with a floor of 2 on Heretic. Player power in this simulation is one
        scalar accumulated from floors cleared and crafts, with no per-item
        model, so losing 1.8 of 18 pieces has no representation that would not be
        an invented conversion into a power fraction.
      * Heads-up display -- Hardcore shows the map overlay only and Heretic hides
        it. This simulation has no player perception model; its policies see the
        true state.

    AND ONE HERETIC RULE THAT DOES MATTER HERE IS STILL MISSING: cities get 2
    upgrade slots instead of 3. `world.City.upgrades` exists as a field and is
    read by nothing, so there is no city upgrade system to reduce. That is the
    half of issue #289 that cannot be measured, and it is issue #318.
    """

    #: Days lost when the player dies. Standard 5, Hardcore 10, Heretic 15.
    death_day_cost: int

    #: Multiplies the number of dungeons a surge spawns. Heretic is the only
    #: mode that changes it: "Surges spawn 25% more dungeons".
    surge_dungeon_multiplier: float

    #: City upgrade slots. Carried because the design gives it and a reader will
    #: look for it; NOTHING READS IT, because no city upgrade system exists.
    #: Issue #318.
    city_upgrade_slots: int


LETHALITY_RULES: dict[LethalityMode, LethalityRules] = {
    LethalityMode.STANDARD: LethalityRules(
        death_day_cost=5, surge_dungeon_multiplier=1.0, city_upgrade_slots=3),
    LethalityMode.HARDCORE: LethalityRules(
        death_day_cost=10, surge_dungeon_multiplier=1.0, city_upgrade_slots=3),
    LethalityMode.HERETIC: LethalityRules(
        death_day_cost=15, surge_dungeon_multiplier=1.25, city_upgrade_slots=2),
}


# Ordered weakest -> strongest. Used for "distance to the Pillar" reasoning.
TIER_ORDER = [CityTier.OUTPOST, CityTier.BULWARK, CityTier.SANCTUARY, CityTier.PILLAR]


@dataclass(frozen=True)
class TierStats:
    count: int
    max_defense: float
    max_population: float
    # UNKNOWN #4 -- base city defense / population per tier.
    # Anchored loosely to the Bulwark class tree, which gates nodes at
    # 5k/8k/10k/15k/20k/25k player Max HP, implying a 5-digit HP scale.


@dataclass(frozen=True)
class DungeonSpec:
    """Floor and timer ranges for one (DungeonType, CityTier) pair."""
    floors: tuple[int, int]
    resolve_days: tuple[int, int]

    #: Defence POINTS and PEOPLE destroyed each time this dungeon resolves
    #: undefeated, before scaling by relative floor count.
    #:
    #: ABSOLUTE, NOT A FRACTION OF THE CITY, and issue #1327 is why. These were
    #: fractions of the host city's own maximum, which meant the maximum divided
    #: out of "how many resolves does this city survive": a Pillar holding twenty
    #: times an Outpost's defence lasted 17 resolves against 10, and every
    #: upgrade in the game that raises a city's health was worth nothing. The
    #: project owner ruled on 2026-09-05, verbatim: "damage to cities shouldn't
    #: be a % of their hp. Instead, dungeons should have damage ranges that
    #: aren't % based, but should be flat damage numbers."
    #:
    #: The same shape is still in the game, at
    #: `UCataclysmEmpireMap::Bite`. Issue #1331 tracks porting this across.
    defense_damage: float
    population_damage: float


@dataclass
class EmpireTree:
    """The empire passive tree, collapsed to the handful of effects that
    actually touch the strategy layer.

    Every field is the *net* effect of a whole investment pattern, so we can
    compare "no tree" against "Explorer maxed" against a proposed fix without
    simulating 1,248 individual nodes.
    """
    name: str = "None"

    # --- dungeon run time -------------------------------------------------
    run_days_flat: float = 0.0      # total flat days removed (the current design)
    run_days_mult: float = 1.0      # multiplicative scalar

    # Floors added/removed. This model charges one day a floor, so this lever
    # changes run time AND reward together: it cannot buy speed without paying
    # loot. `run_days_flat` above is the lever that can, and
    # TREE_EXPLORER_VIA_FLOORS exists to contrast the two. In the game a city
    # upgrade shortens the walk without touching the floor count at all.
    floor_delta: float = 0.0

    # --- city survivability ----------------------------------------------
    city_damage_mult: float = 1.0   # product of every damage-reduction node

    #: How much damage a city can absorb, as a multiple of its tier's base.
    #: The sum of every city-health increase in the tree, because increases are
    #: summed into one bucket in this project rather than multiplied.
    #:
    #: A SEPARATE LEVER FROM `city_damage_mult` AND NOT INTERCHANGEABLE WITH IT,
    #: which is why issue #1288 refused to fold the two together and issue #1319
    #: built this instead. Halving the damage and doubling the health look the
    #: same on a full-health city and diverge immediately afterwards:
    #:
    #:   * `engine._resolve` takes a bite of `max_defense * bite * mult`, so
    #:     raising `max_defense` raises the absolute size of every bite as well
    #:     as the pool it comes out of. The two do not cancel for a city that
    #:     has already taken damage.
    #:   * `engine._retake` restores a fraction of `max_defense`, so a larger
    #:     pool means a larger restore. Damage reduction does nothing for a city
    #:     that has already fallen.
    #:
    #: IT SCALES DEFENCE AND NOT POPULATION. Four of the seven nodes say
    #: "Defense" or "Max Health" and not population, and `Imperial Decree`
    #: explicitly trades "-10% to population" for its +20% health, so the tree
    #: treats the two apart. `world.build_empire` applies it to `max_defense`
    #: only.
    city_health_mult: float = 1.0

    # --- timers -----------------------------------------------------------
    resolve_bonus_days: float = 0.0
    surge_bonus_days: float = 0.0

    def describe(self) -> str:
        return (f"{self.name}: run -{self.run_days_flat:g}d x{self.run_days_mult:.2f}, "
                f"floors {self.floor_delta:+g}, city dmg x{self.city_damage_mult:.3f}, "
                f"city hp x{self.city_health_mult:.2f}, "
                f"resolve +{self.resolve_bonus_days:g}d, surge +{self.surge_bonus_days:g}d")


@dataclass
class TuningConfig:
    # =====================================================================
    # THE FIVE UNKNOWNS
    # =====================================================================

    # RESOLVED (was UNKNOWN #1) -- one floor costs one day as a STARTING RATE.
    #
    # THIS MODEL NEVER CHANGES IT, and that is a fact about the model rather
    # than about the design. Nothing here has city upgrades or an empire tree,
    # so there is nothing to lower the rate with and no sweep moves it.
    # sim/README.md lists it among the rules fixed by design rather than swept
    # for exactly that reason.
    #
    # IT IS NOT AN INVARIANT OF THE GAME. In the game, city upgrades and the
    # empire tree lower the days a dungeon takes to walk WHILE ITS FLOOR COUNT
    # STAYS WHERE IT IS, so an invested player runs a fifty floor dungeon in a
    # couple of days and it is still fifty floors deep and still worth that.
    # Depth and reward are the same axis; depth and time are not, once a player
    # has invested. docs/DECISIONS.md carries the 2026-09-05 correction, and an
    # earlier version of this comment stated the stronger rule as law.
    #
    # SO A FLOOR-COUNT NODE AND A WALK-TIME NODE ARE DIFFERENT UPGRADES. Here
    # they cannot be told apart because only one of them exists; in the game the
    # first gives up depth, reward and resolve time to buy speed and the second
    # gives up nothing.
    days_per_floor: float = 1.0
    run_days_min: int = 1
    run_days_max: int = 400

    # UNKNOWN #2 -- surge cadence, and how it escalates.
    surge_mode: SurgeMode = SurgeMode.STATIC
    surge_interval_days: float = 120.0
    surge_dungeon_count: int = 4

    # Heretic spawns 25% more dungeons per surge. Set by the lethality mode.
    surge_dungeon_multiplier: float = 1.0

    # ACCELERATING / BOTH: each surge multiplies the gap by this and floors it.
    surge_interval_decay: float = 0.88
    surge_interval_min: float = 25.0

    # SWELLING / BOTH: each surge adds this many dungeons, capped.
    surge_count_growth: float = 0.5
    surge_count_max: int = 14

    # Wave volume across multiple Cataclysms. Each one brings its own pattern's
    # count_mult, and the totals are SUMMED (not averaged -- averaging meant a
    # fourth Cataclysm could dilute the wave and make the run easier) then
    # raised to this exponent so eight simultaneous threats are brutal without
    # being arithmetically impossible.
    cataclysm_volume_exponent: float = 0.7

    # A surge also fires immediately when a city falls.
    surge_on_city_fall: bool = True
    # Does a fall-triggered surge also advance the escalation counter? If it
    # does, losing a city permanently speeds the game up -- a death spiral.
    city_fall_advances_escalation: bool = True

    # UNKNOWN #3 -- resolve timers.
    # A flat timer table cannot work at one day a floor: a 40-floor dungeon
    # with a 30-day timer is unsavable no matter how well the player plays.
    # The timer scales with depth, and with depth only -- in the game a
    # shortened walk does not move it.
    #
    #     resolve_days = resolve_base_days + floors * resolve_floor_ratio
    #
    # The ratio is the single most important number in the strategy layer. At
    # 1.0 every dungeon is exactly barely savable and nothing else can be. Above
    # roughly 1.5 the player can save any ONE dungeon comfortably but still
    # cannot save all of them -- which is where triage lives.
    resolve_base_days: float = 10.0
    resolve_floor_ratio: float = 1.6
    resolve_jitter: float = 0.15    # +/- fraction, rolled per dungeon

    # UNKNOWN #4 -- base city defense / population. Held in TIER_STATS.

    # UNKNOWN #5 -- empire points earned per dungeon cleared.
    # Not consumed by the strategy sim; recorded so the meta-progression
    # curve can be fitted from the same runs.
    empire_points_per_dungeon: dict[DungeonType, float] = field(default_factory=lambda: {
        DungeonType.BASIC: 1.0,
        DungeonType.QUEST: 3.0,
        DungeonType.FALLEN_CITY: 5.0,
        DungeonType.CATACLYSM: 25.0,
    })

    # =====================================================================
    # Structural rules
    # =====================================================================

    # The dungeon you are standing in has its timer PAUSED -- its residents are
    # busy fighting you rather than marching on the city. Every other dungeon
    # in the world keeps ticking.
    #
    # This makes entering a dungeon a guaranteed save rather than a gamble, so
    # the decision is pure opportunity cost: 30 days in here is 30 days the
    # other timers advance without you.
    timer_ticks_while_running: bool = False

    # After resolving undefeated, does the dungeon stay (with a refreshed
    # timer) or disappear? Staying is what lets an ignored city actually die.
    dungeon_persists_after_resolve: bool = True

    # =====================================================================
    # Power and the forge
    # =====================================================================
    # Time and floors are separate currencies. A floor costs a day to begin
    # with, but a day can also be spent at the forge instead -- and the forge
    # does not defend anything. That is the decision the whole game is built on: make the item
    # and let a city burn, or keep slogging with the gear you have.

    # Which difficulty tier this run is played at. Sets the whole power scale
    # via scoring.PLAYER_MAX_SCORES.
    tier: int = 1
    # Moving up a tier adds a Cataclysm, and dungeons then draw from the
    # combined modifier pool of every ACTIVE Cataclysm. Two active types means
    # ~26 modifiers to roll from, which is what stops deep tiers running dry.
    CATACLYSM_ROSTER: tuple[str, ...] = ("Demonic", "Death", "War", "Pestilence",
                                         "Famine", "Celestial", "Chaos", "Void")

    # A dungeon carries one modifier per tier. Sacrificial dungeons start with
    # double that -- the player may sacrifice materials to shed the extra ones,
    # or keep them for bonus rewards.
    modifiers_per_tier: int = 1
    sacrificial_modifier_multiplier: int = 2

    # Spawn distribution over dungeon subtypes. Every dungeon gets one: there
    # is deliberately no "None" entry here, because the owner ruled on
    # 2026-09-05 that every dungeon should have a sub-type. See
    # `scoring.SUBTYPE_WEIGHTS`, which is a DIFFERENT quantity -- how much
    # harder each sub-type makes a dungeon -- and which keeps its "None" entry,
    # because a dungeon entered outside a surge still has no sub-type.
    SUBTYPE_SPAWN_WEIGHTS: dict[str, float] = field(default_factory=lambda: {
        "Timed": 18.0, "Horde": 18.0, "Elite": 15.0,
        "Volatile": 15.0, "Siege": 15.0, "Sacrificial": 12.0, "Cow Level": 7.0,
    })

    # --- the Siege sub-type ----------------------------------------------
    #
    # THE ONE PLACE IN THIS MODEL WHERE DAMAGE IS STILL A SHARE OF A CITY'S
    # MAXIMUM, AND IT IS DELIBERATE. Issue #1327 turned every other city damage
    # number into points, because a share of the maximum divided out of how long
    # a city survived and made every city-health upgrade worthless. The project
    # owner was asked whether the Siege should follow and answered on
    # 2026-09-05, verbatim: "Keep it as a deliberate exception (Recommended)".
    #
    # THE REASON, AS IT WAS PUT TO THEM: a siege does not care how thick your
    # walls are. It makes the Siege the one threat that city-health investment
    # does not protect against, which gives the sub-type a situation of its own
    # rather than only a number of its own. The cost -- that a fully invested
    # city is still helpless against a siege -- was stated and accepted.
    #
    # SO A READER WHO FINDS A PERCENTAGE HERE HAS NOT FOUND AN OVERSIGHT.
    # `docs/DECISIONS.md` carries the same warning for the same reason.
    #
    # WHERE THESE NUMBERS COME FROM. `docs/Cataclysm_GDD_v2.md` line 3744:
    # "Deals 1% damage to city defenses and population per day while active.
    # Increases in power by 10 points per day. Pauses city upgrades. Max 1 per
    # city." The game reads it the same way in
    # `CataclysmEmpireRun.h`; this model follows the game rather than the other
    # way round, which is the reverse of this project's usual direction.
    siege_defence_bite_per_day: float = 0.01
    siege_population_bite_per_day: float = 0.01

    # "Increases in power by 10 points per day", where the owner settled on
    # 2026-09-05 that its power is "the damage it does to the city/population".
    #
    # POINTS AND NOT A SHARE, so the growth bites hardest where the empire is
    # thinnest -- ten points is 1% of an Outpost's defence and 0.05% of the
    # Pillar's. It is also the half of a Siege that city health DOES protect
    # against, because a bigger pool absorbs the same points for longer.
    siege_damage_growth_per_day: float = 10.0

    #: "Max 1 per city". A refused roll is redistributed across the other
    #: sub-types, which is what makes the Siege share of dungeons that reach
    #: the map lower than its share of dungeons rolled.
    siege_max_per_city: int = 1

    # NOT MODELLED: "Pauses city upgrades." The model has no city upgrades to
    # pause. `LethalityRules.city_upgrade_slots` exists and nothing reads it,
    # which issue #318 already records. Stated here so the omission is a known
    # gap rather than a silent one.

    # Overwhelm -- see combat.py.
    overwhelm_rate: float = 0.25
    overwhelm_cap: float = 0.50
    # Per-floor death risk at 2x incoming damage. Small; it compounds by depth.
    per_floor_risk: float = 0.010
    boss_risk_multiplier: float = 6.0

    # Player power, expressed as fractions of the tier's power gap so the same
    # numbers work at every tier. Dungeon difficulty comes from scoring.py.
    power_start_frac: float = 0.35

    # The Cataclysm gets stronger as the run goes on. Without this, power is
    # never scarce, looting alone always suffices, and the forge is a trap that
    # only ever costs you cities. This is the treadmill that makes gear matter.
    #
    # Deliberately keyed to ELAPSED DAYS, not surge count. Keyed to surges, any
    # change to surge cadence silently rescales difficulty -- accelerating
    # surges would multiply the treadmill as well as the pressure, and the two
    # escalation modes stop being comparable.
    dungeon_power_escalation_per_100_days: float = 0.22

    # Power gained from equipping what drops, as a fraction of tier width per
    # floor. Deliberately NOT enough on its own to stay ahead of the treadmill.
    power_gain_per_floor_frac: float = 0.00032
    # Crafting materials banked per floor cleared.
    material_per_floor: float = 1.0

    # The forge. Far better power-per-day than looting, but it defends nothing
    # and it burns materials that only dungeon time can supply.
    craft_days: int = 12
    craft_material_cost: float = 40.0
    craft_power_gain_frac: float = 0.05

    # Which difficulty mode this campaign is played in. A record of what the
    # numbers below were set to; `with_lethality` is what sets them together.
    # A test asserts the defaults here equal LETHALITY_RULES[STANDARD], so the
    # two cannot drift apart silently. Issue #289.
    lethality_mode: LethalityMode = LethalityMode.STANDARD

    # Dying costs days and the dungeon is not cleared. Set by the lethality
    # mode: Standard 5, Hardcore 10, Heretic 15.
    death_day_cost: int = 5
    # Policies refuse dungeons riskier than this.
    death_risk_tolerance: float = 0.35

    # Number of simultaneous Cataclysms this run.
    active_cataclysms: int = 1

    # The win condition. Every Cataclysm quest mechanic in GDD XI reduces to
    # "clear N quest dungeons, then the enemy capital opens": 10 Rifts, 5 Seeds
    # of Undeath, 10 Essences of War, 8 Pillars of Order. 8 is the midpoint.
    quest_objectives_required: int = 8
    # A quest dungeon that reaches the end of its timer relocates instead of
    # detonating (GDD VIII: "does not resolve -- refreshes and may move").
    quest_relocates: bool = True

    # The Last Stand. When a Sanctuary falls the Cataclysm can reach the Pillar
    # and comes to you: a stronger Cataclysm dungeon that absorbs every dungeon
    # still standing on the map as extra floors. There is no escaping it --
    # either you beat the Cataclysm or the run is over.
    last_stand_floor_bonus: int = 25
    last_stand_floors_per_absorbed: int = 5
    # The Last Stand is as strong as the ruin it grew out of. Arriving there
    # having thrown away sixteen cities should be a materially worse fight than
    # arriving having lost two -- otherwise the Last Stand launders bad play.
    last_stand_power_per_fallen_city: float = 0.05
    last_stand_floors_per_fallen_city: int = 4

    # How much deeper the EARNED Cataclysm dungeon gets for each ordinary
    # dungeon the player has cleared. The design document: "Every dungeon
    # defeated adds one floor to the Cataclysm boss dungeon."
    #
    # ONLY ORDINARY DUNGEONS COUNT, which the owner settled on 2026-09-06:
    # "Only ordinary dungeons count (Recommended)". A Quest dungeon is the win
    # condition itself and retaking a fallen city is recovery rather than
    # progress, so neither makes the final fight harder. The consequence is
    # deliberate: PURSUING THE WIN CONDITION NEVER DEEPENS THE BOSS.
    #
    # IT DOES NOT APPLY TO THE LAST STAND. That fight is built from its own
    # bonuses above and nothing else; the owner settled that the same day: "No
    # -- the last stand replaces it". It is won 1 time in 54 on purpose, and
    # adding earned growth on top would have made a chosen number worse by
    # accident.
    cataclysm_floors_per_dungeon_cleared: int = 1

    # Runs are long now that a 40-floor dungeon costs 40 days.
    max_days: int = 2500

    # =====================================================================
    # Tables
    # =====================================================================

    TIER_STATS: dict[CityTier, TierStats] = field(default_factory=lambda: {
        CityTier.OUTPOST:   TierStats(count=12, max_defense=1_000,  max_population=5_000),
        CityTier.BULWARK:   TierStats(count=8,  max_defense=3_000,  max_population=20_000),
        CityTier.SANCTUARY: TierStats(count=4,  max_defense=8_000,  max_population=60_000),
        CityTier.PILLAR:    TierStats(count=1,  max_defense=20_000, max_population=150_000),
    })

    # Floor ranges follow the stated intent: randomised within a range that
    # depends on both dungeon type and the tier of the host city.
    #
    # THE DAMAGE COLUMNS ARE POINTS AND PEOPLE, NOT FRACTIONS. Issue #1327. Each
    # one here is the fraction it replaced multiplied by that tier's base
    # maximum, so this table reproduces the old arithmetic exactly for a city at
    # its base size, and the change of shape can be measured on its own before
    # any number is chosen:
    #
    #   Outpost    10% of 1,000 =   100 defence,  5% of   5,000 =   250 people
    #   Bulwark     9% of 3,000 =   270 defence,  5% of  20,000 = 1,000 people
    #   Sanctuary   8% of 8,000 =   640 defence,  4% of  60,000 = 2,400 people
    #   Pillar      6% of 20,000 = 1,200 defence, 3% of 150,000 = 4,500 people
    DUNGEON_SPECS: dict[tuple[DungeonType, CityTier], DungeonSpec] = field(
        default_factory=lambda: {
            (DungeonType.BASIC, CityTier.OUTPOST):
                DungeonSpec((8, 15),   (10, 20), 100.0, 250.0),
            (DungeonType.BASIC, CityTier.BULWARK):
                DungeonSpec((15, 25),  (14, 26), 270.0, 1_000.0),
            (DungeonType.BASIC, CityTier.SANCTUARY):
                DungeonSpec((25, 40),  (20, 34), 640.0, 2_400.0),
            (DungeonType.BASIC, CityTier.PILLAR):
                DungeonSpec((40, 60),  (30, 50), 1_200.0, 4_500.0),

            # Quest dungeons never resolve (GDD VIII) -- they refresh and may
            # move. Bites are zero; the timer is a relocation clock.
            (DungeonType.QUEST, CityTier.OUTPOST):
                DungeonSpec((20, 30),  (25, 40), 0.0, 0.0),
            (DungeonType.QUEST, CityTier.BULWARK):
                DungeonSpec((30, 45),  (25, 40), 0.0, 0.0),
            (DungeonType.QUEST, CityTier.SANCTUARY):
                DungeonSpec((30, 50),  (25, 40), 0.0, 0.0),
            (DungeonType.QUEST, CityTier.PILLAR):
                DungeonSpec((50, 70),  (25, 40), 0.0, 0.0),

            # Fallen City floor minimums come straight from the GDD: 20/40/60.
            (DungeonType.FALLEN_CITY, CityTier.OUTPOST):
                DungeonSpec((20, 35),  (999, 999), 0.0, 0.0),
            (DungeonType.FALLEN_CITY, CityTier.BULWARK):
                DungeonSpec((40, 60),  (999, 999), 0.0, 0.0),
            (DungeonType.FALLEN_CITY, CityTier.SANCTUARY):
                DungeonSpec((60, 85),  (999, 999), 0.0, 0.0),
            (DungeonType.FALLEN_CITY, CityTier.PILLAR):
                DungeonSpec((80, 120), (999, 999), 0.0, 0.0),

            (DungeonType.CATACLYSM, CityTier.PILLAR):
                DungeonSpec((100, 150), (999, 999), 0.0, 0.0),
        })

    # Which tier a surge prefers to hit. Higher weight = more likely.
    SURGE_TARGET_WEIGHT: dict[CityTier, float] = field(default_factory=lambda: {
        CityTier.OUTPOST: 5.0,
        CityTier.BULWARK: 3.0,
        CityTier.SANCTUARY: 1.5,
        CityTier.PILLAR: 0.0,   # the Pillar is only attacked in the Last Stand
    })

    # Chance a surge-spawned dungeon is a Quest rather than a Basic.
    quest_dungeon_chance: float = 0.12

    tree: EmpireTree = field(default_factory=EmpireTree)

    # ---------------------------------------------------------------------

    def spec(self, dtype: DungeonType, tier: CityTier) -> DungeonSpec:
        return self.DUNGEON_SPECS[(dtype, tier)]

    def with_tree(self, tree: EmpireTree) -> "TuningConfig":
        return replace(self, tree=tree)

    def with_lethality(self, mode: LethalityMode) -> "TuningConfig":
        """This config played in one of the three difficulty modes.

        Sets the mode AND every number the mode owns, together, because setting
        `lethality_mode` alone would label a config Heretic while it ran on
        Standard numbers. See `LethalityRules` for the two mode effects that
        have no model here and the one that is issue #318.
        """
        rules = LETHALITY_RULES[mode]
        return replace(self, lethality_mode=mode,
                       death_day_cost=rules.death_day_cost,
                       surge_dungeon_multiplier=rules.surge_dungeon_multiplier)


# =========================================================================
# Empire tree presets
# =========================================================================

TREE_NONE = EmpireTree(name="No tree")

# Every unconditional flat run-time reduction in Empire_Development_Tree_Final:
#   Pacing 10 + Overclock 20 + Temporal Mastery 25 + Opportunist 5
#   + Fleet Footed 5 + The Delver 5  = 70 days
# (Rapid Descent and Sovereign's Haste are excluded -- they scale with floors
#  and active Cataclysm count and would only make this worse.)
TREE_EXPLORER_AS_DESIGNED = EmpireTree(
    name="Explorer maxed (as designed)",
    run_days_flat=70.0,
)

# Every multiplicative city damage-reduction node in the Architect branch of
# `docs/Empire_Development_Tree_Final.json`, at full investment, for a Sanctuary
# next to the Pillar holding 2+ upgrades. THE NODE NAMES ARE HERE ON PURPOSE:
# the previous list was bare numbers, and checking it against the graph is what
# found that one factor matched no node at all. Issue #1288.
#
#   Urban Fortification    0.96^15    0.5421   -4% per point, 15 points
#   Imperial Command       flat       0.6000   40% less surge damage, 2+ upgrades
#   Sovereign's Might      0.99^25    0.7778   -1% per point, 25 points
#   Siege Resistance     0.9867^15    0.8180   -1.33% per point, 15 points
#   Fortified Pillar       0.98^10    0.8171   -2% population loss, within 2 rings
#   Global Vigilance       capped     0.8500   -1% per dungeon cleared, 15% cap
#   Civil Defense         0.985^10    0.8597   -1.5% per point, 15 points
#   Structural Integrity  0.985^10    0.8597   -1.5% per point, 10 points
#   Supply Lines          0.985^10    0.8597   -1.5% per point, adjacent to Pillar
#   Iron Will              0.99^15    0.8601   -1% per point, 15 points
#   Unyielding Defense    0.995^5     0.9752   -0.5% per point per active type
#                                     ------
#                                     0.0766
#
# `Reclaimer's Resolve` is excluded: +1% per city reclaimed has no value at full
# investment, because it depends on how the run went rather than on the tree.
#
# WHAT THIS DELIBERATELY DOES NOT INCLUDE. Seven further Architect nodes raise
# how much damage a city can ABSORB -- 6.54x for this same Sanctuary. The model
# has no field for that and this lever does not stand in for it: the project
# owner ruled on 2026-09-05, verbatim "Damage reduction only, 0.0766". Issue
# #1319 is the gap. The previous value of 0.023 carried an untraceable 0.25
# factor that may have been an attempt to fold that axis in here; folding two
# axes into one number is what made it untraceable.
# The city-health nodes, for the SAME scenario as the multiplier above -- a
# Sanctuary next to the Pillar. Issue #1319. Summed as increases, which is this
# project's convention for an increase.
#
#   Masonry Techniques    +150%   +10% per point x15, Bulwarks and Sanctuaries
#   Reinforced Walls      +120%   +8% per point x15, all cities
#   Monument Building     +100%   +10% per point x10, Sanctuaries
#   Beacon of Hope         +50%   within 2 rings of the Pillar; a Sanctuary is
#                                 in ring 1
#   Foundation             +50%   +5% per point x10, unrestricted
#   Imperial Decree        +20%   also -10% population, which is not modelled
#                          ----
#                          5.90x
#
# `Fortified Gates` is EXCLUDED and is the reason this number is not 6.54. It
# grants +8% per point across 8 points, but only "for Outposts", and this
# scenario is a Sanctuary. A first count of these nodes matched descriptions for
# a percentage without reading the tier they apply to and produced 6.54; the
# same class of mistake as the untraceable factor issue #1288 removed.
#
# ONE NUMBER WHERE THE TREE HAS PER-TIER RULES. Three of these six apply only to
# some tiers, and this lever multiplies every tier alike, so an Outpost is
# credited with Masonry, Monument Building and Beacon of Hope that it does not
# get, and is not credited with Fortified Gates that it does. `city_damage_mult`
# already makes the same simplification for the same reason. Making both levers
# per-tier is a larger change and is not part of #1319.
TREE_ARCHITECT_AS_DESIGNED = EmpireTree(
    name="Architect maxed (as designed)",
    city_damage_mult=0.0766,
    city_health_mult=5.90,
    resolve_bonus_days=13.0,
)

# Alternative: delete every flat day-reduction node and move that power onto
# floor count instead. Because one floor is one day, this still speeds runs up
# -- but it costs reward, so it cannot be a free win.
TREE_EXPLORER_VIA_FLOORS = EmpireTree(
    name="Explorer via floors (-25 floors)",
    floor_delta=-25.0,
)

# The same branch pushed the other way: buy depth, pay time.
TREE_EXPLORER_DEEP = EmpireTree(
    name="Explorer via floors (+30 floors)",
    floor_delta=+30.0,
)

# A conservative whole-tree budget: modest multiplicative speed, clamped city
# damage reduction, small timer padding.
TREE_PROPOSED_FIX = EmpireTree(
    name="Proposed budget (x0.85 time, x0.55 dmg)",
    run_days_mult=0.85,
    city_damage_mult=0.55,
    resolve_bonus_days=5.0,
    surge_bonus_days=10.0,
)

TREE_PRESETS = [
    TREE_NONE,
    TREE_EXPLORER_AS_DESIGNED,
    TREE_EXPLORER_VIA_FLOORS,
    TREE_EXPLORER_DEEP,
    TREE_ARCHITECT_AS_DESIGNED,
    TREE_PROPOSED_FIX,
]
