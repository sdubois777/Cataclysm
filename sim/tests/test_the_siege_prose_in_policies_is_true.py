"""Every Siege figure `sim/cataclysm_sim/policies.py` states in prose is true.

WHY THIS FILE EXISTS. Issue #1364. `siege_urgency` justifies its shape with
measured numbers -- how long an unattended Siege leaves a city, and how long the
median walk to one is -- and nothing read those numbers. They went stale twice
in two days and the fast suite passed both times:

  * The four days-to-empty figures followed the owner's cut of
    `siege_damage_growth_per_day` from 10 to 2.5 on issue #1349, but only
    because a human hand carried them across. `test_siege_subtype.py` checks
    the ENGINE against 25 / 39 / 55 / 70; it has never looked at this
    docstring, so a constant moving would have failed there and left the
    sentence here saying the old four.
  * The median walk said 12 / 20 / 33 from the day it was written. Nobody could
    reproduce it: issue #1364 measured 14 / 22 / 33 and this file's own
    re-measurement gives 13 / 22 / 35. **None of the three was wrong by much
    and none was reproducible**, which is the actual defect -- see below.

WHY A MEDIAN WALK IS ALLOWED A DAY EITHER WAY AND A DAYS-TO-EMPTY IS NOT.
They are different kinds of number. Days-to-empty is exact arithmetic on
constants: at 1% of the maximum a day plus 2.5 points for each day already
stood, an Outpost's thousand points of defence is gone on day 25 and there is
nothing to average. The median walk is one order statistic over a mixture of
uniform floor ranges, and the mixture is nearly flat where the median sits --
45% of Outpost dungeons walk in 12 days or fewer and 50% in 13 or fewer -- so
two adjacent days are a coin flip and a block of 200 campaigns lands on either
side at random. A guard demanding an exact median would fail on an unrelated
change to the random stream and teach the next reader to widen it until it
stopped complaining. So this measures, allows one day, and separately checks
that the distribution really is flat enough to deserve that.

WHAT IS EXACT HERE, AND IS ASSERTED EXACTLY. That a Pillar Siege can never be
answered. A surge cannot target the Pillar, so the only dungeons that spawn
there are the Cataclysm and the Fallen City the Pillar leaves when it falls;
the shallower of those two floor ranges starts at 80, one floor is one day
without a tree, and both Cataclysm openers only add floors. Eighty days against
the seventy a fresh Siege leaves, at the best roll the model can produce.

HOW THE DAY OF TOLERANCE IS KEPT FROM SWALLOWING A REAL DRIFT. It would, on its
own: reverting the stated Outpost median from 13 to 12 leaves the
re-measurement passing, because 13 really is within a day of 12. What catches
that is the sentence beside it, which states the days a fresh Siege leaves AND
the difference. Twenty-five minus twelve is thirteen and the docstring says
twelve, so `test_the_slack_it_quotes_is_the_subtraction_it_claims` fails on a
one-day edit that the parametrised re-measurement waves through. Proved by
breaking exactly that on 2026-09-06; the whole set of breaks is in the pull
request that added this file.

WHAT STILL SLIPS THROUGH, STATED SO THAT NOBODY DISCOVERS IT AS A SURPRISE: a
consistent one-day error, where the walks and the slack are BOTH moved by a day
in the same direction. Nothing here can separate that from sampling noise,
because nothing can -- see the paragraph above on what a median over a flat
distribution is worth. The docstring in `policies.py` says so in the prose
rather than pretending otherwise.

THAT IS MEASURED AND NOT ASSUMED. Putting the Cow Level weight back to the 7.6
this file was first written against -- the very change whose reverse failed in
continuous integration -- leaves all fourteen of these passing. It should. The
share of Outpost dungeons walking in 13 days or fewer moves from 50.5% to 50.2%
between the two weights, which is a third of a percentage point: the
distribution barely moves and the reported integer flips because the median sat
on a boundary. **A guard that failed on that would be reporting the boundary and
not the game.** What broke in continuous integration was the asymmetry, not the
tolerance -- the figure then stated, 35, sat on the far side of the boundary
from what a 200-campaign block gives, so the gap was two days in one direction
and none in the other. Quoting what the large sample gives puts the block within
a day either way, which is what the tolerance is for.
"""

from __future__ import annotations

import inspect
import re
import statistics
from collections import Counter
from dataclasses import replace

import pytest

from cataclysm_sim import policies
from cataclysm_sim.config import (
    TREE_NONE, CityTier, DungeonType, SurgeMode, TuningConfig,
)
from cataclysm_sim.engine import Simulation

TIERS = [CityTier.OUTPOST, CityTier.BULWARK, CityTier.SANCTUARY,
         CityTier.PILLAR]

#: The settings the measured figures in `policies.py` carry, and the ones this
#: file re-measures at. Issue #1286 names the block and `analyse_siege_dose.py`
#: prints it above its own tables.
#:
#: FIVE DUNGEONS A SURGE IS THE BALANCE REPORT'S SETTING AND NOT
#: `TuningConfig.surge_dungeon_count`, which is 4. At four the Sanctuary median
#: is 37 rather than 34, measured over 1,000 campaigns at each, so quoting a
#: walk without its surge size says nothing.
REPORT_SETTINGS = dict(
    tier=1,
    surge_mode=SurgeMode.STATIC,
    surge_interval_days=120.0,
    surge_dungeon_count=5,
    resolve_floor_ratio=2.0,
    dungeon_power_escalation_per_100_days=0.10,
    craft_days=12,
    craft_power_gain_frac=0.04,
)

#: Campaigns the re-measurement runs. Chosen from evidence rather than taste:
#: over ten disjoint blocks of this size the median never strayed more than one
#: day from the 10,000-campaign answer on an Outpost, a Bulwark or a Sanctuary,
#: re-run at the Cow Level weight of 7.0 that #1369 landed. Ten blocks of 120
#: put a Sanctuary two days out, which is why it is not 120.
CAMPAIGNS = 200

#: How far a measured median may sit from the stated one. See the module
#: docstring: the walk distribution is flat where the median falls.
WALK_TOLERANCE_DAYS = 1

#: The Pillar's own median is noisier -- 200 campaigns produce only about 230
#: dungeons there against 16,000 on an Outpost -- and it is not the figure the
#: Pillar claim rests on. `TestAPillarSiegeCanNeverBeAnswered` carries that,
#: exactly and without campaigns. Ten blocks of 200 spread it over 120 to
#: 126.5 against a 10,000-campaign answer of 123, so four is what was measured
#: rather than what was convenient.
PILLAR_TOLERANCE_DAYS = 4


def report_config() -> TuningConfig:
    return replace(TuningConfig(), **REPORT_SETTINGS).with_tree(TREE_NONE)


def flat(doc: str) -> str:
    """A docstring with its hard line wrapping collapsed.

    Every phrase this file looks for is longer than one wrapped line, so
    searching the raw text would report a clean tree that is not clean.
    """
    return re.sub(r"\s+", " ", doc).strip()


DOC_URGENCY = flat(policies.siege_urgency.__doc__)
DOC_DAILY = flat(policies.siege_daily_damage.__doc__)


def says(doc: str, sentence: str, what: str) -> None:
    """Assert the docstring contains `sentence`, or say what to do about it."""
    assert sentence in doc, (
        f"the docstring should say {sentence!r}, and does not. {what}\n\n"
        "If the constants moved, the docstring is what is stale -- fix the\n"
        "prose. If the sentence was reworded, follow it here rather than\n"
        "deleting the check. Issue #1364.")


def days_to_empty(tier: CityTier, growth: float | None = None) -> int:
    """Days an unattended Siege takes to empty a city of that size.

    DRIVEN THROUGH THE DAY LOOP AND NOT RE-DERIVED. A second closed form here
    would pass while both copies were wrong, which is the failure mode this
    whole file exists to catch. `test_siege_subtype.py` takes the same route.
    """
    cfg = report_config()
    if growth is not None:
        cfg = replace(cfg, siege_damage_growth_per_day=growth)
    sim = Simulation(cfg, seed=0)
    city = next(c for c in sim.empire.cities.values() if c.tier is tier)
    d = sim._make_dungeon(DungeonType.BASIC, city)
    d.subtype = "Siege"
    d.spawned_day = sim.day

    n = 0
    while city.defense > 0 and n < 10_000:
        sim._apply_siege_damage()
        sim.day += 1
        n += 1
    assert n < 10_000, f"a Siege never empties a {tier.value}"
    return n


class _Recording(Simulation):
    """A campaign that records every dungeon at the moment it is created.

    AT CREATION AND NEVER FROM WHAT IS LEFT STANDING, for the reason
    `analyse_siege_dose.py` gives at length: a census of survivors under-counts
    whatever destroys its own host, and a Siege destroys its host.

    A Cataclysm dungeon is deepened after `_make_dungeon` returns -- by the
    ordinary dungeons cleared, and by the Last Stand's own bonuses -- and every
    one of those constants is positive. So the walk recorded here is a LOWER
    bound on the walk the player actually faces at the Pillar, which is the
    direction that makes the claim below safe.
    """

    def __init__(self, cfg: TuningConfig, seed: int = 0, log=None):
        self._log = [] if log is None else log
        super().__init__(cfg, seed=seed)

    def _make_dungeon(self, dtype, city, floors_mult: float = 1.0):
        d = super()._make_dungeon(dtype, city, floors_mult=floors_mult)
        self._log.append((city.tier, dtype, d.subtype, d.run_days))
        return d


@pytest.fixture(scope="module")
def made():
    """Every dungeon `CAMPAIGNS` campaigns created, at the report settings.

    Module scoped because it costs about ten seconds and every measurement in
    this file reads the same list.
    """
    cfg = report_config()
    log: list[tuple] = []
    for seed in range(CAMPAIGNS):
        _Recording(cfg, seed=seed, log=log).run(policies.triage)
    assert len(log) > 20_000, (
        f"only {len(log)} dungeons over {CAMPAIGNS} campaigns; the sample is "
        "too small for anything below to mean what it says")
    return log


def walks(made, tier: CityTier) -> list[int]:
    return [days for t, _, _, days in made if t is tier]


# ---------------------------------------------------------------------------
# What an unattended Siege costs -- exact, and asserted exactly
# ---------------------------------------------------------------------------

class TestTheDaysAnUnattendedSiegeLeaves:
    """`siege_urgency` opens by saying how long each city size survives one.

    Those four numbers are the premise of everything the function does: the
    decision it makes is "can the player still get there", and the answer is
    the walk against these. `test_siege_subtype.py` already drives the engine
    against 25 / 39 / 55 / 70 and against the C++ that states them. What it
    does not do is look at this docstring, so the two could disagree and only
    the docstring would be wrong. That is what happened on issue #1349.
    """

    def test_the_four_stated_days_are_what_the_day_loop_produces(self):
        d = {t: days_to_empty(t) for t in TIERS}
        says(
            DOC_URGENCY,
            f"{d[CityTier.OUTPOST]} days for an Outpost, "
            f"{d[CityTier.BULWARK]} for a Bulwark, "
            f"{d[CityTier.SANCTUARY]} for a Sanctuary, "
            f"{d[CityTier.PILLAR]} for the Pillar",
            "That is what the day loop empties each city size in today.")

    def test_the_growth_the_prose_names_is_the_constant(self):
        """`siege_daily_damage` calls itself a mirror of the engine and then
        names the growth in words. It said ten for two days after the owner cut
        it to 2.5, which is a mirror describing the wrong thing."""
        growth = report_config().siege_damage_growth_per_day
        says(DOC_DAILY, f"the growth of {growth:g} points a day",
             "That is `TuningConfig.siege_damage_growth_per_day`.")

    def test_the_old_four_are_a_control_and_the_derivation_reproduces_them(self):
        """THE CONTROL FOR THE TEST ABOVE. A guard whose expected value was
        tuned until it passed proves nothing, so the same day loop is run at
        the growth the owner cut FROM and has to return the four figures the
        docstring records as history. If it does not, the arithmetic changed
        underneath both sets and neither is evidence of anything."""
        old = {t: days_to_empty(t, growth=10.0) for t in TIERS}
        assert list(old.values()) == [14, 23, 34, 47], (
            f"at a growth of 10 points a day the day loop now returns "
            f"{list(old.values())} and not the 14 / 23 / 34 / 47 that held "
            "until issue #1349. Something other than that constant moved.")
        says(
            DOC_URGENCY,
            f"THOSE FOUR WERE {old[CityTier.OUTPOST]} / "
            f"{old[CityTier.BULWARK]} / {old[CityTier.SANCTUARY]} / "
            f"{old[CityTier.PILLAR]} until",
            "Those are what the same arithmetic gives at the old growth.")


# ---------------------------------------------------------------------------
# The median walk -- measured, and allowed a day
# ---------------------------------------------------------------------------

STATED_WALK = re.compile(
    r"the median walk is (\d+) days to an Outpost, (\d+) to a Bulwark, "
    r"(\d+) to a Sanctuary and (\d+) to the Pillar")

STATED_SLACK = re.compile(
    r"Against the (\d+) / (\d+) / (\d+) days a fresh Siege leaves those three "
    r"sizes, the slack is (\d+) / (\d+) / (\d+) days")


def stated_walks() -> dict[CityTier, int]:
    m = STATED_WALK.search(DOC_URGENCY)
    assert m is not None, (
        "`siege_urgency` no longer states the median walk in the sentence this "
        "guard reads. If it was reworded, follow it here rather than deleting "
        "the check. Issue #1364.")
    return dict(zip(TIERS, (int(g) for g in m.groups()), strict=True))


class TestTheMedianWalkTheProseStates:
    """The figure that sent issue #1364 up: 12 / 20 / 33, from nowhere anyone
    could reproduce, in a sentence whose whole point was the size of the gap
    between the walk and the Siege."""

    @pytest.mark.parametrize("tier", TIERS)
    def test_it_is_within_a_day_of_a_fresh_measurement(self, made, tier):
        stated = stated_walks()[tier]
        measured = statistics.median(walks(made, tier))
        allowed = (PILLAR_TOLERANCE_DAYS if tier is CityTier.PILLAR
                   else WALK_TOLERANCE_DAYS)
        assert abs(measured - stated) <= allowed, (
            f"`siege_urgency` says the median walk to a {tier.value} is "
            f"{stated} days; {CAMPAIGNS} campaigns at the settings it names "
            f"give {measured}. Re-measure and rewrite the sentence -- do not "
            f"widen this tolerance, which is already {allowed} day(s). "
            "Issue #1364.")

    def test_the_slack_it_quotes_is_the_subtraction_it_claims(self, made):
        """The sentence states the walks, the days a Siege leaves, and the
        difference. A hand edit that moves one and not the others reads
        perfectly and is wrong, which is exactly how it went stale before."""
        m = STATED_SLACK.search(DOC_URGENCY)
        assert m is not None, (
            "`siege_urgency` no longer states the slack in the sentence this "
            "guard reads. If it was reworded, follow it here.")
        answerable = [CityTier.OUTPOST, CityTier.BULWARK, CityTier.SANCTUARY]
        siege_days = [int(g) for g in m.groups()[:3]]
        slack = [int(g) for g in m.groups()[3:]]
        walk = stated_walks()

        assert siege_days == [days_to_empty(t) for t in answerable], (
            f"the slack sentence quotes {siege_days} days for a fresh Siege "
            f"and the day loop gives {[days_to_empty(t) for t in answerable]}")
        assert slack == [d - walk[t] for d, t in zip(siege_days, answerable, strict=True)], (
            f"the sentence states a slack of {slack} against walks of "
            f"{[walk[t] for t in answerable]} and Sieges of {siege_days}, "
            f"which subtracts to {[d - walk[t] for d, t in zip(siege_days, answerable, strict=True)]}")

    def test_the_distribution_really_is_flat_where_the_median_sits(self, made):
        """WHY THE TOLERANCE ABOVE IS A DAY RATHER THAN NOTHING, held in place
        so that it stays a measured property rather than a habit. If the walk
        lengths ever stopped being nearly uniform around the median, the median
        would become a stable number and this file should tighten."""
        v = walks(made, CityTier.OUTPOST)
        stated = stated_walks()[CityTier.OUTPOST]
        share = 100.0 * sum(1 for x in v if x <= stated) / len(v)
        assert 44.0 <= share <= 58.0, (
            f"{share:.1f}% of Outpost dungeons walk in {stated} days or fewer. "
            "The docstring's claim that the median is a coin flip between two "
            "adjacent days rests on that being near half.")
        says(DOC_URGENCY,
             "49.0% of Outpost dungeons walk in 13 days or fewer and 56.2% in "
             "14 or fewer",
             "Those are the shares from the 10,000-campaign measurement.")


# ---------------------------------------------------------------------------
# The Pillar -- exact, and without campaigns
# ---------------------------------------------------------------------------

class TestAPillarSiegeCanNeverBeAnswered:
    """The one claim in the docstring that is absolute rather than typical, so
    the one that must not rest on a median. It rests on which dungeon kinds can
    exist at the Pillar at all."""

    def test_a_surge_cannot_target_the_pillar(self):
        """The premise. `trigger_surge` drops the Pillar from the pool it draws
        a target from, and its surge weight is zero besides."""
        cfg = report_config()
        assert cfg.SURGE_TARGET_WEIGHT[CityTier.PILLAR] == 0.0
        source = inspect.getsource(Simulation.trigger_surge)
        assert "c.cid != self.empire.pillar_id" in source, (
            "`Simulation.trigger_surge` no longer removes the Pillar from the "
            "pool it draws a surge target from. If a surge can now put a Basic "
            "dungeon on the Pillar -- 40 floors, so 40 days -- then a Pillar "
            "Siege CAN be answered and the docstring is wrong.")

    def test_only_two_dungeon_kinds_ever_spawn_there(self, made):
        kinds = {dtype for tier, dtype, _, _ in made
                 if tier is CityTier.PILLAR}
        assert kinds == {DungeonType.CATACLYSM, DungeonType.FALLEN_CITY}, (
            f"{CAMPAIGNS} campaigns put {sorted(k.value for k in kinds)} on "
            "the Pillar. The docstring says the Cataclysm and the Fallen City "
            "and nothing else.")

    def test_the_shortest_possible_walk_outlasts_the_siege(self):
        """DERIVED FROM THE FLOOR RANGES AND NOT FROM A CAMPAIGN. The shallowest
        dungeon either kind can roll at the Pillar, walked with no tree."""
        cfg = report_config()
        sim = Simulation(cfg, seed=0)
        shortest = min(
            sim.run_days_for(cfg.spec(dtype, CityTier.PILLAR).floors[0])
            for dtype in (DungeonType.CATACLYSM, DungeonType.FALLEN_CITY))
        leaves = days_to_empty(CityTier.PILLAR)

        assert shortest > leaves, (
            f"the shallowest dungeon that can spawn at the Pillar walks in "
            f"{shortest} days and a fresh Siege leaves {leaves}. The "
            "docstring's 'can never be answered' has stopped being true.")
        says(DOC_URGENCY,
             f"is therefore {shortest} days against the {leaves} a fresh Siege "
             "leaves",
             "Those are the shallowest Pillar walk and the Pillar's "
             "days-to-empty.")

        cat = cfg.spec(DungeonType.CATACLYSM, CityTier.PILLAR).floors
        fallen = cfg.spec(DungeonType.FALLEN_CITY, CityTier.PILLAR).floors
        says(DOC_URGENCY,
             f"the Cataclysm at {cat[0]} to {cat[1]} floors",
             "That is the Cataclysm's floor range at the Pillar.")
        says(DOC_URGENCY,
             f"Fallen City at {fallen[0]} to {fallen[1]}",
             "That is the Fallen City's floor range at the Pillar.")

    def test_no_campaign_ever_produced_a_shorter_one(self, made):
        """The measured half of the same claim, which would catch a path into
        `_make_dungeon` that the floor ranges above do not describe."""
        shortest = min(walks(made, CityTier.PILLAR))
        leaves = days_to_empty(CityTier.PILLAR)
        assert shortest > leaves, (
            f"a Pillar dungeon walked in {shortest} days over {CAMPAIGNS} "
            f"campaigns, against the {leaves} a fresh Siege leaves")


# ---------------------------------------------------------------------------
# Which dungeon kinds carry a Siege
# ---------------------------------------------------------------------------

STATED_MIX = re.compile(
    r"the ([\d,]+) Sieges that reached the map were (\d+)% Basic, (\d+)% "
    r"Quest, (\d+)% Fallen City and (\d+)% Cataclysm, about ([\d.]+) a campaign")


class TestWhichKindsOfDungeonCarryASiege:
    """`siege_daily_damage` explains why the sub-type is applied in all three
    scoring branches by saying how often each dungeon kind carries one. The
    counts it used to give were taken before the owner halved the spawn weight
    and before a surge began rolling Quest dungeons, and had Fallen City at
    more than twice Quest when the two are level."""

    def test_the_shares_and_the_rate_are_what_a_campaign_produces(self, made):
        m = STATED_MIX.search(DOC_DAILY)
        assert m is not None, (
            "`siege_daily_damage` no longer states the Siege mix in the "
            "sentence this guard reads. If it was reworded, follow it here.")
        stated_total = int(m.group(1).replace(",", ""))
        stated = {DungeonType.BASIC: int(m.group(2)),
                  DungeonType.QUEST: int(m.group(3)),
                  DungeonType.FALLEN_CITY: int(m.group(4)),
                  DungeonType.CATACLYSM: int(m.group(5))}
        stated_rate = float(m.group(6))

        sieges = Counter(dtype for _, dtype, subtype, _ in made
                         if subtype == "Siege")
        total = sum(sieges.values())
        for dtype, share in stated.items():
            measured = 100.0 * sieges[dtype] / total
            assert abs(measured - share) <= 3.0, (
                f"the docstring says {share}% of Sieges sit on a "
                f"{dtype.value} dungeon; {CAMPAIGNS} campaigns give "
                f"{measured:.1f}%")

        rate = total / CAMPAIGNS
        assert abs(rate - stated_rate) <= 1.5, (
            f"the docstring says about {stated_rate} Sieges reach the map per "
            f"campaign; {CAMPAIGNS} campaigns give {rate:.2f}")
        assert abs(stated_total / 10_000 - stated_rate) <= 0.5, (
            f"the docstring's own two numbers disagree: {stated_total} Sieges "
            f"over 10,000 campaigns is {stated_total / 10_000:.2f} a campaign, "
            f"not {stated_rate}")
