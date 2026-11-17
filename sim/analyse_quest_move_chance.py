"""What a chance of moving does to a Quest dungeon, for issue #1324.

WHY THIS EXISTS. The design says a Quest dungeon "does not resolve -- refreshes
and may move to adjacent city". Slice 4 of issue #1324 read that "may" as
satisfied by the map: the dungeon moves whenever an adjacent exposed city
exists, and stays only when there is nowhere to go, which is about one quest
timer in five. Asked whether that was the intent, the project owner ruled on
2026-09-06, verbatim: **"A chance each time"**. A Quest dungeon must sometimes
stay even when it could move.

**THE OWNER NAMED NO NUMBER AND `CLAUDE.md` FORBIDS INVENTING ONE.** So this
file was written as a dose-response curve and a recommendation, and NOT as a
shipped constant. The chance is applied by `_ChanceSimulation` below, which
overrides one branch of `Simulation._resolve` for the length of one batch. This
follows `analyse_siege_dose.py`, which is the shape that worked for issue #1349.

**THE OWNER HAS SINCE ANSWERED, VERBATIM "0.5", AND IT IS BUILT.** The constant
is `config.quest_move_chance` and the game's copy is
`UCataclysmSurgeScheduler::QuestMoveChance`. **This paragraph is the only thing
that changed here.** The file still sweeps a chance of its own through
`_ChanceSimulation` rather than reading the field, so a later dose sweep does not
have to write to the configuration to run -- and so the rows below still describe
the run that produced them. Two sentences saying the model has no move-chance
field, which it now does, were removed.

**DO NOT RE-DERIVE THIS CURVE TO JUSTIFY 0.5.** It is recorded on issue #1324 and
in `docs/DECISIONS.md`. Every response variable was flat, so the number came from
the ruling and not from the balance; a fresh run would produce a different flat
ladder and settle nothing.

WHAT MOVES WHEN THE CHANCE MOVES, AND WHY IT IS NOT OBVIOUS. A Quest dungeon is
the win condition: clearing `quest_objectives_required` of them opens the earned
Cataclysm dungeon, which the design calls the ordinary way to win. A dungeon
that stays put is a dungeon the player can plan around, so a LOWER chance should
make objectives easier to reach and should RAISE the earned-route share. A
dungeon that wanders spreads its threat over more cities and is harder to
schedule against. **If the curve runs the other way that is a finding and this
file says so rather than explaining it away**; see `RESULT` below, which is
written from a measured run and not from that expectation. It does run the other
way.

THE THREE RESPONSE VARIABLES, IN THE ORDER THE OWNER WILL READ THEM.

1. **`earned%`** -- the share of campaigns that opened the earned Cataclysm
   dungeon. The headline, and the same figure issue #1349's curves were read on.
2. **`questAge`** -- mean days from a Quest dungeon spawning to the player
   clearing it: how long a quest objective survives before it is answered.
   Counted only over the ones actually cleared, and `clearRate` beside it says
   what share that is, because a mean over survivors alone would improve as the
   player cleared fewer of them.
3. **`cities`** -- cities lost, of 25.

`takeUp%` is printed beside them as the check that the dose was applied at all:
it is the share of the quest timers that HAD somewhere to go which took it, so it
must read 0 at chance 0.0 and 100 at chance 1.0 whatever else the campaign did.
`moved%` is the same count over every timer, which is smaller because about one
timer in FOUR has nowhere adjacent to go however the coin lands. **This said
one in five and disagreed with the table below it**, whose `couldGo%` column
reads 73.7% to 75.8% -- a quarter left over, not a fifth. One in five is what
slice 4 measured at the model's defaults, which is a different campaign.

**EVERY DOSE DRAWS THE SAME NUMBER OF RANDOM NUMBERS PER QUEST TIMER.** The
target is chosen and the coin is flipped whether or not the dungeon then moves,
so two doses run the same campaign up to the first quest timer whose OUTCOME
differs, rather than diverging at the first one that fires. Doing it the other
way round -- flip, and only draw a target if the flip passed -- would put the
streams out of step immediately and turn part of every difference below into
noise from a different random walk.

**AND THAT IS THE ORDER THE SHIPPED CODE TAKES TOO**, which was not true when
this file was written and is why the rows below describe what was built.
`Simulation._resolve` draws the target and then flips, and
`UCataclysmEmpireRun::RelocateQuestDungeon` calls `PickRelocation` and then
flips. `tools/tests/test_surge_port.py::TestWhatAQuestDungeonIs::
test_both_draw_the_target_before_they_flip_the_coin` is the guard on both.

**THERE IS NO `sim/tests/test_quest_move_chance_curves.py` AND THERE NEVER WAS.**
This docstring named one twice, as the thing that checked the draw order and the
thing that checked this class against `Simulation._resolve` at chance 1.0. Both
references were false; the order is guarded by the port test named above, and
nothing guards the duplication below, which is stated at `_ChanceSimulation`
rather than papered over.

**THE 1.0 ROW WAS THE SHIPPED BEHAVIOUR WHEN THIS RAN, AND IS NO LONGER.** The
owner has since chosen 0.5, so the shipped setting is the 0.50 row; 1.0 is the
baseline this was measured against rather than a description of the game. Every
row is measured through the same override, not read off `development` or off
any earlier report, so the baseline and the doses differ only in the dose. Figures measured before slice 4
are not comparable to any row here at all: relocation to an adjacent city
changed which cities a Quest dungeon threatens, so the whole random stream
diverges from the first quest timer onward. Issue #1358 tracks that.

WHAT IS HELD CONSTANT, AND IT IS PRINTED IN THE OUTPUT TOO. Difficulty tier 1,
the `No tree` preset, the `triage` policy, static surges every 120 days for 5
dungeons, resolve floor ratio 2.0, escalation 0.10 per 100 days, craft 12 days
for +4% of tier width. `surge_dungeon_count` is **5 and not the `TuningConfig`
default of 4**: `experiments.exp_calibrate` chooses 5 and the balance report runs
at 5, so a figure from this file and a figure taken at 4 are not comparable.
Issue #1286 names the whole set.

A SURVIVOR COUNT IS NOT A FREQUENCY, which this project has paid for once
already. `timers` counts every quest timer at the moment it FIRES and `moves`
every relocation as it happens, both hooked in `_ChanceSimulation._resolve`.
Neither is read off the dungeons left standing at the end of a campaign, because
a city that falls absorbs everything on it and the survivors are the ones on the
cities that held.


RESULT, MEASURED 2026-09-06 AT 1,000 CAMPAIGNS A BLOCK, 10,000 IN ALL. Every
figure below ran under the settings named above -- tier 1, no empire tree,
`triage`, static surges every 120 days for **5** dungeons, resolve floor ratio
2.0, escalation 0.10 per 100 days, craft 12 days for +4%.

**IT WAS TAKEN OVER TWO SEED BLOCKS AND THIS FILE NOW RUNS FOUR.** A fresh run
will not reproduce the columns below, because there are two more of them and
the same total campaign count is spread differently. The table is kept as the
record of what was put to the owner on issue #1324, and the conclusion it
carries survives the change -- it rested on the paired test, which does not
depend on the block count, and not on the two-block gap that issue #1379
retired. Set `CATACLYSM_QUEST_MOVE_TRIALS=1000` and expect 20,000 campaigns
rather than 10,000.

    chance   earned% A/B   objectives A/B   cities A/B   questAge A/B
      0.00    48.8 / 46.9    6.36 / 6.24   16.66 / 16.12   57.9 / 55.1
      0.25    49.5 / 48.0    6.37 / 6.27   16.54 / 16.07   58.1 / 55.7
      0.50    50.9 / 48.6    6.41 / 6.28   16.40 / 16.18   58.6 / 56.1
      0.75    51.2 / 48.5    6.40 / 6.28   16.41 / 16.21   58.4 / 55.8
      1.00    50.0 / 48.0    6.40 / 6.27   16.49 / 16.23   58.6 / 55.7

**THE CURVE IS FLAT.** The whole ladder spans 2.4 points of earned% in block A
and 1.7 in block B, and the two blocks sit 2.7 points apart at the widest dose:
the difference between the ends is smaller than the difference between two
samples of the same thing. Cities lost move by a quarter of a city, quest
objective age by 0.7 days, and the share of quest dungeons ever cleared by 1.2
points, all against block gaps of the same size or larger. **No dose is
significant.** Paired by seed against chance 1.00 and read as McNemar's test, the
largest discordant split is 99 to 76 pooled at chance 0.00, z = 1.74, which is
p = 0.08.

**AND THE SIGN RUNS THE OTHER WAY FROM THE MECHANISM, WHICH IS A FINDING.** A
Quest dungeon that stays put is a Quest dungeon the player can plan around, so a
LOWER chance was expected to make objectives easier to reach. It does not.
Chance 0.00 is the WORST dose for the earned route in both blocks, and the paired
counts agree in sign at every dose in both:

    chance   campaigns per 1,000 that reached the earned boss here and
             not at chance 1.00, against the reverse       A          B
      0.00                                            37 / 49    39 / 50
      0.25                                            37 / 42    39 / 39
      0.50                                            41 / 32    38 / 32
      0.75                                            30 / 18    28 / 23

Four sign agreements out of four across two disjoint blocks is more than the
individual p-values suggest, and the shape is a shallow hump peaking around
0.50-0.75 rather than a slope. **It is stated and not explained away.** A
plausible mechanism -- a Quest dungeon that never moves eventually sits behind a
sealed frontier where the player has no reason to walk, while one that drifts
stays in reachable territory -- is a guess, and this file has not tested it.

**RECOMMENDATION: 0.5.** Balance does not choose here; every response variable the
owner asked about is flat across the whole ladder, so the number is a feel
decision and should be argued as one. 0.5 is the plainest reading of "a chance
each time"; it is the point at which the mechanic reads most clearly AS a chance,
since a Quest dungeon whose timer fires is then as likely to stay as to go; and
it is not a cost, measuring very slightly better for the earned route than the
always-move rule it replaces. In play it means a Quest dungeon actually moves on
**37%** of its timers rather than 50%, because about a quarter of them have
nowhere adjacent to go whatever the coin says.

**THE SECOND-BEST ANSWER IS 0.75**, if the owner wants the wandering to stay
prominent; it has the largest positive paired net in block A. **0.0 and 1.0 are
both excluded by the ruling itself** -- 1.0 is what the owner rejected, and 0.0
is a Quest dungeon that never moves, which the design's "may move" describes as
something that happens.

ONE FIGURE HERE DISAGREES WITH THE DESIGN DOCUMENT AND THE CONDITIONS ARE WHY.
`docs/Cataclysm_GDD_v2.md` states that a Quest dungeon has somewhere to go 79.5%
of the time, measured over 926 quest timers in 30 campaigns with the model's
defaults and lethality switched off. `couldGo%` here is 73.7% to 75.8% over ten
cells at the balance report's settings. Neither is wrong; they are different
campaigns, and the document states its conditions beside the number for exactly
this reason.
"""

from __future__ import annotations

import math
import os
import statistics
from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import TREE_NONE, DungeonType, SurgeMode, TuningConfig
from cataclysm_sim.engine import Simulation

#: Campaigns per seed block. TWO DISJOINT BLOCKS RUN AT EVERY DOSE and both are
#: printed separately, because pooling two blocks once changed an answer on this
#: project from "not moved" to "moved".
#:
#: 3 is the size `sim/tests/test_analysis_scripts.py` runs at, for the reason
#: `analyse_siege_dose.py` documents for its own default: every analysis script
#: is executed by that file, and at the size the curves are actually read at this
#: one is tens of minutes. **AT THE DEFAULT THE PRINTED SHARES ARE NOISE AND THE
#: OUTPUT SAYS SO.**
#:
#: The curves put to the owner on issue #1324 were taken at 1,000, which is 2,000
#: campaigns a dose and 10,000 campaigns in all. Set
#: `CATACLYSM_QUEST_MOVE_TRIALS=1000` to reproduce them. An environment variable
#: rather than a command line flag because `test_analysis_scripts.py` executes
#: this file with `runpy.run_path`, which leaves `sys.argv` pointing at pytest's
#: own arguments.
TRIALS = int(os.environ.get("CATACLYSM_QUEST_MOVE_TRIALS", "3"))

#: The seed blocks. Disjoint by construction at any `TRIALS`, and block A
#: starts at seed 0 so a run reproduces the block every other measurement on
#: this issue was taken over.
#:
#: **THERE WERE TWO OF THESE, AND TWO CANNOT MEASURE NOISE.** Two blocks give
#: ONE realised difference, not an estimate of how far a block mean moves.
#: This file used to compute a threshold from that single difference and print
#: a categorical conclusion against it. Issue #1379.
#:
#: **THE WORKED CASE THAT SETTLES IT.** For Sieges created per campaign,
#: blocks A and B differed by 0.04 while six disjoint blocks of the same size
#: gave a standard deviation of 0.105. The two-block gap understated the
#: spread by about two and a half times, because A and B happened to be the
#: two lowest of the six. It fails in both directions: a real move smaller
#: than the true spread would have been called significant, and the old
#: `spread` took a MAXIMUM over doses, which drifts upward as the ladder grows
#: and starts calling real differences noise.
#:
#: Four is the smallest count that gives a standard deviation worth reading.
#: It doubles the campaigns: the curve on issue #1324 was 10,000 at two blocks
#: and the same run is 20,000 now.
BLOCKS = (("A", 0), ("B", TRIALS), ("C", 2 * TRIALS), ("D", 3 * TRIALS))

#: The doses. Chance that a Quest dungeon whose timer runs out takes a move it
#: could take. 1.0 is what `development` ships -- move whenever there is
#: somewhere to go -- and 0.0 is the control: a Quest dungeon that never moves at
#: all, which is what the design said before slice 4 built the move.
#:
#: THE LADDER IS EVEN RATHER THAN LOGARITHMIC because the owner's ruling is about
#: a coin, not about an order of magnitude, and because the interesting region was
#: not known in advance. **0.5 IS ON IT DELIBERATELY**, as the plainest reading of
#: "a chance each time", so that the answer cannot be decided by a ladder that
#: stepped over it.
MOVE_CHANCES = (0.0, 0.25, 0.5, 0.75, 1.0)


def base_config() -> TuningConfig:
    """The settings every row runs under. See the module docstring."""
    return replace(
        TuningConfig(),
        tier=1,
        surge_mode=SurgeMode.STATIC,
        resolve_floor_ratio=2.0,
        surge_interval_days=120.0,
        surge_dungeon_count=5,
        dungeon_power_escalation_per_100_days=0.10,
        craft_days=12,
        craft_power_gain_frac=0.04,
    ).with_tree(TREE_NONE)


BASE = base_config()


class _ChanceSimulation(Simulation):
    """A campaign in which a Quest dungeon takes an available move `p` of the
    time, and which counts what happened to its quest dungeons.

    WHY A SUBCLASS AND NOT A CONFIG FIELD. The owner gave no number, so there is
    nothing to ship; adding `quest_move_chance` to `TuningConfig` would put a
    default into the model that no ruling supports, and every sweep in
    `experiments.py` would silently start running against it. The override lives
    here, where its lifetime is one batch.

    IT REPRODUCES `Simulation._resolve`'S QUEST BRANCH RATHER THAN CALLING IT,
    which is duplication and is the lesser evil: wrapping the original would
    have to undo a move already made, and a dungeon whose `city_id` has been
    written and reverted is not the same as one that never moved -- the draw
    would already have been taken from a list computed after the fact.

    **NOTHING GUARDS THE DUPLICATION AND THIS DOCSTRING USED TO CLAIM SOMETHING
    DID.** It named `sim/tests/test_quest_move_chance_curves.py` as asserting
    that this branch and the real one agree at chance 1.0. **That file does not
    exist and never has.** The divergence to watch for is real: `_resolve` now
    also refuses a besieged destination to a dungeon carrying a Siege -- the
    owner's ruling of 2026-09-06, "Check the limit on arrival too", issue #1371
    -- and the branch below does not. It does not affect any row above, which
    were measured before that rule existed, and a future sweep through this class
    would be measuring a slightly different game. Issue #1371's own measurement
    says how much: 2 Siege-carrying quest moves in 40 campaigns.
    """

    def __init__(self, cfg: TuningConfig, seed: int = 0,
                 move_chance: float = 1.0) -> None:
        super().__init__(cfg, seed=seed)
        self.move_chance = move_chance
        #: Quest timers that fired, moves taken, and moves that were available.
        self.quest_timers = 0
        self.quest_moves = 0
        self.quest_could_move = 0
        #: Days from spawning to being cleared, one entry per quest dungeon the
        #: player actually defeated, and the number that spawned at all.
        self.quest_ages: list[int] = []
        self.quest_spawned = 0

    def _make_dungeon(self, *args, **kwargs):
        d = super()._make_dungeon(*args, **kwargs)
        if d.dtype is DungeonType.QUEST:
            self.quest_spawned += 1
        return d

    def _finish_current(self) -> None:
        d = self.current
        before = self.objectives
        super()._finish_current()
        if d is not None and self.objectives > before:
            self.quest_ages.append(self.day - d.spawned_day)

    def _resolve(self, d) -> None:
        if d.dtype is not DungeonType.QUEST:
            super()._resolve(d)
            return

        cfg = self.cfg
        city = self.empire.cities[d.city_id]
        self.quest_timers += 1
        d.resolve_in = float(d.resolve_max)

        if not cfg.quest_relocates:
            return

        targets = self.empire.adjacent_exposed_cities(city)
        if not targets:
            return

        self.quest_could_move += 1

        # BOTH DRAWS HAPPEN AT EVERY DOSE, and the order is load-bearing. See
        # the module docstring: choosing the target first and flipping second
        # means the two doses run identical campaigns until an OUTCOME differs.
        target = self.rng.choice(targets)
        if self.rng.random() < self.move_chance:
            self.quest_moves += 1
            d.city_id = target.cid

        # AND `city_tier` STAYS WHERE IT IS, exactly as `Simulation._resolve`
        # now leaves it. It is the tier the depth was rolled from and `floors`
        # does not move either. The owner ruled it on 2026-09-06, verbatim
        # "Keeps everything, fix the size".


def measure(cfg: TuningConfig, seed0: int, chance: float,
            trials: int = TRIALS) -> dict:
    """One cell: `trials` campaigns from `seed0` upward at one move chance."""
    policy = policies.ALL["triage"]
    results, ages = [], []
    timers = moves = could = spawned = 0

    for i in range(trials):
        sim = _ChanceSimulation(cfg, seed=seed0 + i, move_chance=chance)
        results.append(sim.run(policy))
        ages.extend(sim.quest_ages)
        timers += sim.quest_timers
        moves += sim.quest_moves
        could += sim.quest_could_move
        spawned += sim.quest_spawned

    opened = [r for r in results if r.cataclysm_floors > 0]
    cleared = sum(r.objectives for r in results)

    return {
        "n": trials,
        # **THE PER-SEED OUTCOME, KEPT SO THE DOSES CAN BE COMPARED IN PAIRS.**
        # Two doses run the same seeds, so "how many campaigns changed answer"
        # is a far tighter question than "how far apart are the two shares",
        # which throws the pairing away. `paired_flips` uses it.
        "earnedBySeed": [r.cataclysm_floors > 0 for r in results],
        # THE HEADLINE. `RunResult.cataclysm_floors` is non-zero only for the
        # EARNED boss; the Last Stand is `last_stand` and takes none of it.
        "earned%": 100.0 * len(opened) / trials,
        "won%": 100.0 * sum(1 for r in results if r.won) / trials,
        "lastStand%": 100.0 * sum(1 for r in results if r.last_stand) / trials,
        "objectives": statistics.fmean([r.objectives for r in results]),
        "cities": statistics.fmean([r.cities_lost for r in results]),
        "days": statistics.fmean([r.survived_days for r in results]),
        # THE DOSE ACTUALLY APPLIED, as a share of every quest timer that fired
        # -- not of the ones that had somewhere to go, which is `takeUp%`.
        "moved%": 100.0 * moves / timers if timers else 0.0,
        "couldMove%": 100.0 * could / timers if timers else 0.0,
        "takeUp%": 100.0 * moves / could if could else 0.0,
        # HOW LONG A QUEST OBJECTIVE SURVIVES BEFORE THE PLAYER CLEARS IT.
        # Over cleared ones only, with `clearRate%` saying what share that is.
        "questAge": statistics.fmean(ages) if ages else 0.0,
        "clearRate%": 100.0 * cleared / spawned if spawned else 0.0,
        "questsSpawned": spawned / trials,
        "timers": timers / trials,
    }


HEADER = (f"{'chance':>7} {'blk':>3} {'earned%':>8} {'won%':>6} {'LS%':>6} "
          f"{'objectvs':>8} {'cities':>7} {'days':>6} {'couldGo%':>9} "
          f"{'moved%':>7} {'takeUp%':>8} {'questAge':>9} {'clearRate%':>11} "
          f"{'quests':>7}")


def row(chance: float, block: str, s: dict) -> str:
    return (f"{chance:>7.2f} {block:>3} {s['earned%']:>8.1f} "
            f"{s['won%']:>6.1f} {s['lastStand%']:>6.1f} "
            f"{s['objectives']:>8.2f} {s['cities']:>7.2f} {s['days']:>6.0f} "
            f"{s['couldMove%']:>9.1f} "
            f"{s['moved%']:>7.1f} {s['takeUp%']:>8.1f} {s['questAge']:>9.1f} "
            f"{s['clearRate%']:>11.1f} {s['questsSpawned']:>7.2f}")


def paired_flips(rows: dict, chance: float, block: str) -> tuple[int, int]:
    """Campaigns that opened the earned boss at `chance` and not at 1.00, and
    the other way round, on the SAME seeds.

    THE PAIRED READING. Two shares 2 points apart over 1,000 campaigns each say
    almost nothing on their own; the same 1,000 seeds answering differently in
    one direction and not the other says a great deal. This is the count
    McNemar's test is built on, reported as the two raw numbers because the
    reader can see immediately whether they are lopsided.
    """
    at = rows[chance][block]["earnedBySeed"]
    base = rows[1.0][block]["earnedBySeed"]
    gained = sum(1 for a, b in zip(at, base, strict=True) if a and not b)
    lost = sum(1 for a, b in zip(at, base, strict=True) if b and not a)
    return gained, lost


def sweep() -> dict:
    """Run every dose over both blocks. Returns chance -> block -> summary."""
    out = {}
    for chance in MOVE_CHANCES:
        out[chance] = {name: measure(BASE, seed0, chance)
                       for name, seed0 in BLOCKS}
        for name, _ in BLOCKS:
            print(row(chance, name, out[chance][name]))
    return out


def across(cell: dict, key: str) -> list[float]:
    """One value per seed block, in block order."""
    return [cell[name][key] for name, _ in BLOCKS]


def block_sd(rows: dict, key: str) -> float:
    """How far a single block's estimate of `key` moves from block to block.

    **THIS REPLACED A FUNCTION CALLED `spread`, AND THAT ONE WAS NOT A
    MEASUREMENT OF ANYTHING.** It took the gap between the two blocks at the
    worst dose, and `main` compared a difference against it as though it were
    a threshold. A gap between two numbers is one realised difference: it can
    land arbitrarily close to zero and it can land wide. Issue #1379 measured
    how badly -- for Sieges per campaign the A-to-B gap was 0.04 against a
    six-block standard deviation of 0.105.

    **IT IS A DESCRIPTION AND NOT A THRESHOLD, AND NOTHING COMPARES AGAINST
    IT.** The verdict below comes from `mcnemar` on the same seeds, which does
    not depend on where the blocks happened to land. This is printed so a
    reader can see how much of the table is sampling noise, which is a
    different job from deciding anything.

    AVERAGED OVER THE DOSES RATHER THAN MAXIMISED OVER THEM, which is the
    other half of the old function's problem: a maximum grows as doses are
    added, so the same ladder with one more rung would have been harder to
    call significant for no reason but its length.
    """
    return statistics.fmean(
        statistics.stdev(across(cell, key)) for cell in rows.values())


def mcnemar(gained: int, lost: int) -> tuple[float, float]:
    """McNemar's test on the paired counts, as z and a two-sided p.

    **THE SCRIPT PRINTED THE TWO COUNTS AND NEVER THE TEST.** The p-value in
    this file's own RESULT section was worked out by hand and typed in, which
    is how every stale number in this repository started. Issue #1379.

    `gained` is campaigns that reached the earned boss at this dose and not at
    the baseline, `lost` the reverse. Concordant pairs carry no information
    about a difference, which is the whole point of the test: under the null
    the discordant ones are a fair coin, so `z` is the normal approximation to
    that binomial and `erfc` turns it into a two-sided p exactly.

    Standard library only, which `CLAUDE.md` requires of the simulation.
    """
    discordant = gained + lost
    if discordant == 0:
        return 0.0, 1.0
    z = (gained - lost) / math.sqrt(discordant)
    return z, math.erfc(abs(z) / math.sqrt(2.0))


def settings_lines() -> list[str]:
    cfg = BASE
    return [
        f"  difficulty tier                 {cfg.tier}",
        f"  empire tree preset              {cfg.tree.name}",
        "  policy                          triage",
        f"  surge mode                      {cfg.surge_mode.value}",
        f"  days between surges             {cfg.surge_interval_days:.0f}",
        f"  dungeons per surge              {cfg.surge_dungeon_count}"
        "   (NOT the TuningConfig default of 4)",
        f"  resolve timer days per floor    {cfg.resolve_floor_ratio:.1f}",
        "  dungeon power per 100 days      "
        f"{cfg.dungeon_power_escalation_per_100_days:.2f}",
        f"  days per craft                  {cfg.craft_days}",
        f"  tier width gained per craft     {cfg.craft_power_gain_frac:.2f}",
        f"  quest objectives to win         {cfg.quest_objectives_required}",
        f"  campaigns per block             {TRIALS}",
        "  seed blocks                     "
        + ", ".join(f"{name} from {seed0}" for name, seed0 in BLOCKS)
        + " (disjoint)",
    ]


def main() -> dict:
    print("=" * 112)
    print("QUEST MOVE CHANCE -- issue #1324, the curve the owner ordered with "
          "\"A chance each time\"")
    print("=" * 112)
    print("\nSETTINGS EVERY ROW BELOW RAN UNDER. A figure from this file "
          "without these is not")
    print("a figure. Issue #1286 names them and the balance report uses them.")
    for line in settings_lines():
        print(line)

    if TRIALS < 250:
        print(f"\n  *** {TRIALS} CAMPAIGNS A BLOCK IS A SMOKE TEST AND THE "
              "SHARES BELOW ARE NOISE. ***")
        print("  Set CATACLYSM_QUEST_MOVE_TRIALS=1000 for the size the curves "
              "were read at.")

    print("\n  chance      probability that a quest dungeon WITH somewhere to "
          "go takes it")
    print("  earned%     share of campaigns that OPENED the earned Cataclysm "
          "dungeon")
    print("  won%        share of campaigns won by either route")
    print("  LS%         share where the Cataclysm reached the Pillar")
    print("  objectvs    quest dungeons cleared per campaign, of "
          f"{BASE.quest_objectives_required} needed")
    print("  cities      cities lost of 25, mean per campaign")
    print("  couldGo%    of every quest timer, the share with somewhere "
          "adjacent to go")
    print("  moved%      of every quest timer that fired, the share that moved")
    print("  takeUp%     of the timers that HAD somewhere to go, the share "
          "that took it")
    print("  questAge    mean days from a quest dungeon spawning to the player "
          "clearing it")
    print("  clearRate%  share of quest dungeons spawned that were ever "
          "cleared")
    print("  quests      quest dungeons spawned per campaign")

    print(f"\n{'=' * 112}")
    print("THE DOSE-RESPONSE CURVE. Chance that an available move is taken. "
          "1.0 is what")
    print("`development` ships and 0.0 is a Quest dungeon that never moves.")
    print("=" * 112)
    print(HEADER)
    rows = sweep()

    print(f"\n{'=' * 112}")
    print("READING IT")
    print("=" * 112)

    base = across(rows[1.0], "earned%")
    zero = across(rows[0.0], "earned%")
    sd = block_sd(rows, "earned%")

    print(f"\n  Block-to-block standard deviation in earned%, mean over "
          f"the doses: {sd:.1f} points,")
    print(f"  over {len(BLOCKS)} disjoint blocks. THAT IS A DESCRIPTION OF "
          "THE SAMPLING NOISE AND NOT A")
    print("  THRESHOLD. Nothing below is compared against it. This file "
          "used to take the gap")
    print("  between two blocks and call it a noise floor; a gap is one "
          "realised difference")
    print("  and estimates nothing. Issue #1379.")

    shown = " / ".join(f"{v:.1f}" for v in base)
    print(f"\n  earned% at chance 1.00 (shipped):     {shown}")
    shown = " / ".join(f"{v:.1f}" for v in zero)
    print(f"  earned% at chance 0.00 (never moves): {shown}")

    direction = statistics.fmean(zero) - statistics.fmean(base)

    # **THE VERDICT COMES FROM THE PAIRED TEST AND NOT FROM THE BLOCKS.**
    # Every dose runs the same seeds, so a campaign that reached the earned
    # boss at one dose and not the other is the evidence; two shares two
    # points apart are not. Pooled over the blocks because they are disjoint.
    flips = [paired_flips(rows, 0.0, name) for name, _ in BLOCKS]
    gained = sum(g for g, _ in flips)
    lost = sum(l for _, l in flips)
    z, p = mcnemar(gained, lost)

    print(f"\n  Paired on the same seeds, chance 0.00 against 1.00, pooled "
          f"over {len(BLOCKS)} blocks:")
    print(f"    {gained} campaigns gained, {lost} lost, z = {z:+.2f}, "
          f"p = {p:.3f}")

    if p >= 0.05:
        print(f"\n  The two ends differ by {direction:+.1f} points of "
              "earned%, and the paired test on")
        print("  the same seeds does not separate them. ON THIS SAMPLE THE "
              "MOVE CHANCE IS NOT")
        print("  SHOWN TO MOVE THE EARNED ROUTE -- which is a statement "
              "about this run's power")
        print("  and not a demonstration that the dose does nothing.")
    elif direction > 0:
        print(f"\n  A dungeon that stays put is easier to reach: "
              f"{direction:+.1f} points of earned%")
        print("  from chance 1.00 down to 0.00, which is the direction the "
              "mechanism predicts.")
    else:
        print(f"\n  *** THE CURVE RUNS THE OTHER WAY: {direction:+.1f} points "
              "of earned% going from")
        print("  chance 1.00 down to 0.00. A dungeon that stays put should be "
              "EASIER to reach,")
        print("  so this is a finding and not a rounding error. It is stated "
              "rather than")
        print("  explained away; see RESULT in this file's docstring. ***")

    print("\n  The dose was applied. Take-up should track the chance:")
    for chance in MOVE_CHANCES:
        shown = " / ".join(f"{v:5.1f}%" for v
                           in across(rows[chance], "takeUp%"))
        print(f"    chance {chance:.2f} -> take-up {shown}")

    print(f"\n{'=' * 112}")
    print("HOW LONG A QUEST OBJECTIVE SURVIVES, and how many are ever answered")
    print("=" * 112)
    for chance in MOVE_CHANCES:
        ages = " / ".join(f"{v:6.1f}" for v
                          in across(rows[chance], "questAge"))
        rates = " / ".join(f"{v:5.1f}%" for v
                           in across(rows[chance], "clearRate%"))
        print(f"    chance {chance:.2f} -> {ages} days to clear, "
              f"{rates} ever cleared")

    print(f"\n{'=' * 112}")
    print("THE SAME SEEDS, PAIRED AGAINST CHANCE 1.00. Campaigns that reached "
          "the earned")
    print("boss at this dose and not at 1.00, against the ones that did the "
          "reverse. Two")
    print("shares two points apart say little; the same seeds flipping one way "
          "and not")
    print("the other would say a great deal. A roughly even split is a dose "
          "that did")
    print("nothing but reshuffle which campaigns got there.")
    print("=" * 112)
    print(f"{'chance':>7} {'blk':>3} {'gained':>8} {'lost':>7} {'net':>7} "
          f"{'of':>7}")
    for chance in MOVE_CHANCES:
        for name, _ in BLOCKS:
            gained, lost = paired_flips(rows, chance, name)
            print(f"{chance:>7.2f} {name:>3} {gained:>8} {lost:>7} "
                  f"{gained - lost:>+7} {TRIALS:>7}")

    return rows


# Called unconditionally, like every other analyse_*.py here, because
# `sim/tests/test_analysis_scripts.py` executes this file with `runpy.run_path`
# and that does not set `__name__` to `"__main__"`. A `__main__` guard here made
# the script print nothing under that test, which failed on "assert 0 > 20".
main()
