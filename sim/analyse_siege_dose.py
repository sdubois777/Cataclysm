"""The Siege dose-response curves the project owner ordered on issue #1349.

WHY THIS EXISTS. Modelling the Siege sub-type (issue #1345) took the Last Stand
from about a quarter of campaigns to nearly all of them and left the earned
Cataclysm dungeon -- the route the design calls the ordinary way to win --
happening in a few campaigns in a hundred. The owner was offered four answers and
chose the fourth, verbatim: **"Investigate before changing anything"**. So the
deliverable is a dose-response curve and a recommendation, and NOT a changed
constant. Nothing in this file writes to `cataclysm_sim/config.py`; every dose is
applied to a copy of the config for the length of one batch.

THE THREE AXES, WHICH ARE NOT INTERCHANGEABLE.

1. **How often a Siege appears** -- the spawn weight, 7.5 in 100 today and 15
   until the owner halved it on issue #1349 on 2026-09-06. The doses are the
   ones issue #1340 specifies: 0, 7.5, 15, 30, 50, with the other six sub-types
   taking up the slack IN PROPORTION so the weights still total 100. The 7.5
   row is now the shipped configuration and the 15 row is what it replaced,
   which is what makes this script a re-measurement rather than a sweep.

2. **How hard one bites** -- `siege_defence_bite_per_day`,
   `siege_population_bite_per_day` and `siege_damage_growth_per_day`, scaled
   together.

3. **How long it takes to kill a city** -- added by the owner on 2026-09-06,
   folding in issue #1351.

**AXIS 3 IS NOT A RESTATEMENT OF AXIS 2, AND THE ARITHMETIC IS WHY.** A Siege's
damage on the day it has stood for `k` days is `1% of maximum + 10k`, so the
total it has dealt after `n` days is quadratic in `n` and the growth term
dominates almost immediately. Time-to-fall therefore moves as roughly the INVERSE
SQUARE ROOT of the damage scale, not its inverse: halving every Siege constant
takes an Outpost from 14 days to 20, and it takes a QUARTER of today's damage to
reach 28. `days_to_fall` computes this exactly and `axis_3_table` prints it, so
the claim is checked against the model rather than argued.

That is why this file sweeps the growth term ON ITS OWN as well. Growth is the
half of a Siege that decides how fast it kills; the 1% share is the half that
gives the sub-type its identity -- "a siege does not care how thick your walls
are", which the owner ruled a deliberate exception on 2026-09-05. Setting growth
to zero leaves the identity intact and makes every city size take exactly 100
days, which is a different game from a Siege that is merely weaker.

**THE HEADLINE IS THE EARNED-ROUTE SHARE**, the share of campaigns that open the
earned Cataclysm dungeon, because that is the figure the owner reacted to. Last
Stand reach, wins per Last Stand reached, and cities lost are printed beside it.

WHAT IS HELD CONSTANT, AND IT IS STATED IN THE OUTPUT TOO. Difficulty tier 1, the
`No tree` preset, the `triage` policy, static surges every 120 days for 5
dungeons, resolve floor ratio 2.0, escalation 0.10 per 100 days, craft 12 days for
+4% of tier width. `surge_dungeon_count` is **5 and not the default of 4**:
`experiments.exp_calibrate` chooses 5 and the balance report runs at 5, while the
raw `TuningConfig` default of 4 is a value calibration rejects. A figure from this
file and a figure taken at 4 are not comparable.

TWO TRAPS, BOTH ALREADY PAID FOR ONCE ON THIS PROJECT.

**Campaigns are never grouped by how many Sieges happened.** A city that falls
absorbs every dungeon standing on it, so a SURVIVING empire accumulates Sieges and
grouping that way reads the survivors as the heavily-besieged ones. Every row here
is a separate batch at a fixed dose, which is the comparison that does not invert.

**A survivor count is not a frequency.** `sieges` below counts every Siege at the
moment it is CREATED, hooked in `_InstrumentedSimulation._make_dungeon`, not the
Sieges left standing at the end.

WHY THE POLICY IS INSTRUMENTED RATHER THAN THE DAY LOOP. Issue #1351 measured the
binding constraint on answering a Siege and it is the player's DECISION RATE, not
the city's health: the player is free to choose on about 8% of days. `triage` is
called exactly once per free day with every standing dungeon, so wrapping it
counts, per Siege, how many chances the player ever had to answer that Siege --
`answerable` further requires the dungeon to be inside `death_risk_tolerance`, so
a Siege the player could see but not survive is not counted as a chance. The
wrapper draws no random numbers (`Simulation.death_chance` is pure), so an
instrumented batch and a plain one are the same campaigns;
`sim/tests/test_siege_dose_curves.py` asserts that rather than assuming it.
"""

from __future__ import annotations

import os
import statistics
from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import TREE_NONE, CityTier, SurgeMode, TuningConfig
from cataclysm_sim.engine import Simulation

#: Campaigns per seed block. TWO DISJOINT BLOCKS ARE RUN AT EVERY DOSE, so a row
#: costs twice this, and both blocks are printed separately -- pooling two blocks
#: once changed an answer on this project from "not moved" to "moved".
#:
#: 3 is a compromise, exactly as `analyse_lethality_modes.py` documents for its
#: own 25: `sim/tests/test_analysis_scripts.py` runs every analysis script, and
#: this one sweeps eighteen doses over two blocks where that one sweeps five rows
#: once. **THE FILE RUNS IT TWICE** -- once for the parametrised "it runs at all"
#: test and once for the fixture the checks share -- so the number here is paid
#: twice. Measured 2026-09-06 on this machine:
#:
#:     TRIALS      3       4       5      12
#:     one run  3.95s   5.25s   6.29s   11.2s
#:
#: `sim/tests/test_analysis_scripts.py` takes 10.1 seconds without this section,
#: 23.3 with it at 3, and 27.7 at 5. **So the section costs 13.2 seconds and only
#: 7.9 of those are the two script runs**; the rest is the checks that run
#: campaigns of their own, and dropping TRIALS further buys little. That is still
#: the largest single cost in this file -- `analyse_lethality_modes.py` costs
#: about 8.8 -- and it is deliberate: five of the checks below are the only thing
#: standing between a wrong dose-response curve and a design decision made on it.
#:
#: **NOTHING IN THE CHECKS DEPENDS ON THE SIZE**: not one of them asserts a
#: campaign share, for the reason on the next line.
#:
#: **AT THE DEFAULT THE PRINTED SHARES ARE NOISE AND THE OUTPUT SAYS SO.**
#: The curves put to the owner on issue #1349 were taken at 1,000, which is
#: 2,000 campaigns a dose and 34,000 campaigns in all. That is tens of minutes,
#: and much longer on a machine you are also working on -- the run that produced
#: those curves shared the machine with two full `pytest` runs and six
#: `prove_guard` runs and took the better part of an hour. Run it in the
#: background, as `CLAUDE.md` says of `experiments.py` for the same reason.
#:
#: Set `CATACLYSM_SIEGE_DOSE_TRIALS` to re-run at that size. An environment
#: variable rather than a command line flag because
#: `sim/tests/test_analysis_scripts.py` executes this file with `runpy.run_path`,
#: which leaves `sys.argv` pointing at pytest's own arguments.
TRIALS = int(os.environ.get("CATACLYSM_SIEGE_DOSE_TRIALS", "3"))

#: The two blocks. Disjoint by construction at any `TRIALS`, and block A starts
#: at seed 0 so a run at 1,000 reproduces the block the baseline on issue #1349
#: was measured over.
BLOCKS = (("A", 0), ("B", TRIALS))

#: Axis 1. The doses issue #1340 specifies, and `SIEGE_WEIGHT_TODAY` is what
#: `development` ships -- read off the config rather than written out, so a
#: change to the shipped weight cannot leave this file claiming the wrong
#: baseline.
SIEGE_WEIGHTS = (0.0, 7.5, 15.0, 30.0, 50.0)

#: Axis 2. A multiplier on all three Siege constants at once.
DAMAGE_SCALES = (0.0, 0.125, 0.25, 0.5, 1.0, 2.0)

#: Axis 3. The growth term alone, in points per day stood, with the 1% share left
#: where the owner set it. 2.5 is today; 10.0 is what it replaced on 2026-09-06
#: and is kept on the ladder so the change this script was built to justify
#: stays visible as a row rather than only in the prose.
GROWTH_DOSES = (0.0, 2.5, 5.0, 10.0, 20.0)

CITY_SIZES = (CityTier.OUTPOST, CityTier.BULWARK,
              CityTier.SANCTUARY, CityTier.PILLAR)


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
SIEGE_WEIGHT_TODAY = BASE.SUBTYPE_SPAWN_WEIGHTS["Siege"]
GROWTH_TODAY = BASE.siege_damage_growth_per_day

#: COMBINATIONS, NOT POINTS ON A CURVE. Each of the three axes above moves one
#: lever and holds the rest; these are the settings actually worth putting to the
#: owner, measured the same way. `(label, Siege spawn weight, growth points a
#: day)`.
#:
#: WHY COMBINATIONS AT ALL. The three curves show the earned route is nearly
#: all-or-nothing against either lever on its own: to restore it with the damage
#: alone takes an eighth of today's numbers, which is not a weaker Siege but
#: almost no Siege, and it falsifies the design document's own sentence. Easing
#: BOTH ladders a little reaches the same place while leaving both rules
#: recognisable.
#:
#: THE FIRST ROW READS ITS GROWTH OFF THE CONFIG rather than writing 10.0 out.
#: It is the "rarer, unchanged bite" option, and it has to keep meaning that if
#: the shipped constant ever moves -- otherwise a later change to
#: `siege_damage_growth_per_day` would silently turn this row into a second dose
#: it does not claim to be.
CANDIDATES = (
    ("weight 5", 5.0, GROWTH_TODAY),
    ("w7.5 g2.5", 7.5, 2.5),
    ("w7.5 g5", 7.5, 5.0),
)


def weights_with_siege_at(weight: float) -> dict[str, float]:
    """The spawn table with Siege at `weight` and the rest sharing what is left.

    IN PROPORTION AND NOT EQUALLY, which is what issue #1340 specifies: a
    sub-type that is twice as common today stays twice as common. The total is
    held at 100 so a weight reads as a percentage, which is how the owner was
    shown it.

    Siege keeps its key at weight 0 rather than being deleted, so the draw in
    `Simulation._roll_subtype` sees the same list of names at every dose.
    """
    table = dict(BASE.SUBTYPE_SPAWN_WEIGHTS)
    slack = sum(v for k, v in table.items() if k != "Siege")
    scale = (100.0 - weight) / slack
    return {k: (weight if k == "Siege" else v * scale) for k, v in table.items()}


def days_to_fall(max_defense: float, scale: float = 1.0,
                 growth: float | None = None,
                 cfg: TuningConfig | None = None) -> int:
    """Days an unattended Siege takes to empty a city of `max_defense`.

    THE SAME ARITHMETIC `Simulation._apply_siege_damage` RUNS, in closed form:
    the flat share of the maximum every day, plus `growth` points for each day
    the Siege has already stood. At today's settings this returns 14, 23, 34 and
    47 for the four city sizes, which is what
    `game/Source/CataclysmEmpire/Empire/CataclysmEmpireRun.h` states and what
    `sim/tests/test_siege_subtype.py` asserts against the day loop itself.

    Returns 0 when the Siege deals no damage at all, meaning it never falls.
    """
    cfg = cfg or BASE
    share = cfg.siege_defence_bite_per_day * scale
    grow = (cfg.siege_damage_growth_per_day * scale
            if growth is None else growth)
    per_day_flat = max_defense * share * cfg.tree.city_damage_mult
    per_day_growth = grow * cfg.tree.city_damage_mult
    if per_day_flat <= 0.0 and per_day_growth <= 0.0:
        return 0
    dealt, day = 0.0, 0
    while dealt < max_defense and day < 100_000:
        dealt += per_day_flat + per_day_growth * day
        day += 1
    return day


class _InstrumentedSimulation(Simulation):
    """A campaign that also records what happened to every Siege it created.

    THE COUNT IS TAKEN AT CREATION and never from what is left standing. See the
    module docstring: a census of survivors under-counts whatever destroys its
    own host, and a Siege destroys its host.
    """

    def __init__(self, cfg: TuningConfig, seed: int = 0) -> None:
        super().__init__(cfg, seed=seed)
        #: WHICH DUNGEON THE WIN CAME FROM, and it is not a detail. `RunResult`
        #: carries `won` and `last_stand` and nothing joins them, so "won per
        #: Last Stand reached" reads two ways: every win in a campaign that
        #: reached one, or only the Last Stands actually cleared. **THE TWO
        #: DISAGREE BY A FACTOR APPROACHING TWO HERE.** At the settings this
        #: file runs, seeds 0-999, the first gives 1 in 16.0 and the second 1 in
        #: 26.6, because half the campaigns that open the earned boss go on to
        #: reach a Last Stand as well. Issue #1286's ruling was made on the
        #: second reading, so that is the one reported.
        self.won_by: str | None = None
        self.sieges_created = 0
        #: did -> free days on which this Siege was standing and offered to the
        #: policy at all.
        self.siege_chances: dict[int, int] = {}
        #: did -> those of them where entering it was also inside
        #: `death_risk_tolerance`, so the player could actually have taken it.
        self.siege_answerable: dict[int, int] = {}

    def _make_dungeon(self, *args, **kwargs):
        d = super()._make_dungeon(*args, **kwargs)
        if d.subtype == "Siege":
            self.sieges_created += 1
            self.siege_chances[d.did] = 0
            self.siege_answerable[d.did] = 0
        return d

    def _finish_current(self) -> None:
        """Note which dungeon was being cleared when the run turned into a win.

        Read BEFORE delegating, because `Simulation._finish_current` clears
        `self.current`. Draws nothing.
        """
        d = self.current
        was_last_stand = d is not None and d.is_last_stand
        super()._finish_current()
        if self.won and self.won_by is None:
            self.won_by = "last stand" if was_last_stand else "earned"


def instrumented(policy):
    """Wrap a policy so every free day is counted against the Sieges standing.

    Draws no random numbers, so the campaign is bit-for-bit the campaign the
    bare policy would have run.
    """
    def counted(sim, dungeons):
        tolerance = sim.cfg.death_risk_tolerance
        for d in dungeons:
            if d.subtype != "Siege":
                continue
            sim.siege_chances[d.did] = sim.siege_chances.get(d.did, 0) + 1
            if sim.death_chance(d) <= tolerance:
                sim.siege_answerable[d.did] = (
                    sim.siege_answerable.get(d.did, 0) + 1)
        return policy(sim, dungeons)
    return counted


def measure(cfg: TuningConfig, seed0: int, trials: int = TRIALS) -> dict:
    """One cell: `trials` campaigns from `seed0` upward at one dose."""
    policy = instrumented(policies.ALL["triage"])
    results, routes, chances, answerable, sieges = [], [], [], [], 0
    for i in range(trials):
        sim = _InstrumentedSimulation(cfg, seed=seed0 + i)
        results.append(sim.run(policy))
        routes.append(sim.won_by)
        sieges += sim.sieges_created
        chances.extend(sim.siege_chances.values())
        answerable.extend(sim.siege_answerable.values())

    opened = [r for r in results if r.cataclysm_floors > 0]
    reached = [r for r, w in zip(results, routes, strict=True) if r.last_stand]
    cleared_last_stand = sum(1 for r, w in zip(results, routes, strict=True)
                             if r.last_stand and w == "last stand")
    return {
        "n": trials,
        # THE HEADLINE. Share of campaigns that opened the earned Cataclysm
        # dungeon, which `RunResult.cataclysm_floors` records as non-zero and
        # only ever for the earned boss -- the Last Stand is `last_stand`.
        "earned%": 100.0 * len(opened) / trials,
        "wonEarned%": (100.0 * sum(1 for r in opened if r.won) / len(opened)
                       if opened else 0.0),
        "lastStand%": 100.0 * len(reached) / trials,
        # LAST STANDS ACTUALLY CLEARED, per Last Stand reached. This is the
        # reading issue #1286's ruling was made on, and NOT "campaigns that
        # reached a Last Stand and won", which counts the earned-boss wins that
        # went on to reach one too and is nearly twice as large. See
        # `_InstrumentedSimulation.won_by`.
        "clearedLS%": (100.0 * cleared_last_stand / len(reached)
                       if reached else 0.0),
        "wonAnyInLS%": (100.0 * sum(1 for r in reached if r.won) / len(reached)
                        if reached else 0.0),
        "won%": 100.0 * sum(1 for r in results if r.won) / trials,
        "cities": statistics.fmean([r.cities_lost for r in results]),
        "days": statistics.fmean([r.survived_days for r in results]),
        "free%": 100.0 * statistics.fmean(
            [r.free_days / max(1, r.survived_days) for r in results]),
        "sieges": sieges / trials,
        # AXIS 3'S RESPONSE VARIABLE. Of every Siege created, the share that
        # never gave the player a single day on which they were both free to
        # choose and able to survive the dungeon.
        "noChance%": (100.0 * sum(1 for c in answerable if c == 0)
                      / len(answerable) if answerable else 0.0),
        "chances": statistics.fmean(answerable) if answerable else 0.0,
        "seen": statistics.fmean(chances) if chances else 0.0,
    }


HEADER = (f"{'dose':>13} {'blk':>3} {'earned%':>8} {'wonE%':>6} {'LS%':>6} "
          f"{'clrLS%':>7} {'cities':>7} {'days':>6} {'sieges':>7} "
          f"{'noChance%':>10} {'chances':>8} {'seenDays':>9}")


def row(label: str, block: str, s: dict) -> str:
    return (f"{label:>13} {block:>3} {s['earned%']:>8.1f} "
            f"{s['wonEarned%']:>6.1f} {s['lastStand%']:>6.1f} "
            f"{s['clearedLS%']:>7.2f} {s['cities']:>7.2f} {s['days']:>6.0f} "
            f"{s['sieges']:>7.2f} {s['noChance%']:>10.1f} "
            f"{s['chances']:>8.2f} {s['seen']:>9.2f}")


def sweep(label_of, config_of, doses) -> dict:
    """Run every dose over both blocks. Returns dose -> block -> summary."""
    out = {}
    for dose in doses:
        cfg = config_of(dose)
        out[dose] = {name: measure(cfg, seed0) for name, seed0 in BLOCKS}
        for name, _ in BLOCKS:
            print(row(label_of(dose), name, out[dose][name]))
    return out


def both(cell: dict, key: str) -> tuple[float, float]:
    return cell["A"][key], cell["B"][key]


def worst(cell: dict, key: str) -> float:
    """The less favourable of the two blocks, so a claim rests on both."""
    return min(both(cell, key))


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
        f"  campaigns per block             {TRIALS}",
        f"  seed blocks                     A from {BLOCKS[0][1]}, "
        f"B from {BLOCKS[1][1]} (disjoint)",
    ]


def axis_3_table() -> dict:
    """Days an unattended Siege takes to empty each city size, per dose.

    Exact arithmetic, no campaigns. This is the part of axis 3 that does not
    need simulating, and it is what shows that axis 3 is not axis 2.
    """
    defence = {t: BASE.TIER_STATS[t].max_defense * BASE.tree.city_health_mult
               for t in CITY_SIZES}
    return {
        "scale": {s: {t: days_to_fall(defence[t], scale=s) for t in CITY_SIZES}
                  for s in DAMAGE_SCALES},
        "growth": {g: {t: days_to_fall(defence[t], growth=g) for t in CITY_SIZES}
                   for g in sorted({*GROWTH_DOSES, GROWTH_TODAY})},
    }


def main() -> None:
    print("=" * 108)
    print("SIEGE DOSE-RESPONSE -- issue #1349, the investigation the owner "
          "ordered on 2026-09-06")
    print("=" * 108)
    print("\nSETTINGS EVERY ROW BELOW RAN UNDER. A figure from this file "
          "carries them or it is not")
    print("a figure. Issue #1286 names them and the balance report uses them.")
    for line in settings_lines():
        print(line)
    if TRIALS < 250:
        print(f"\n  *** {TRIALS} CAMPAIGNS A BLOCK IS A SMOKE TEST AND THE "
              "SHARES BELOW ARE NOISE. ***")
        print("  Set CATACLYSM_SIEGE_DOSE_TRIALS=1000 for the size the curves "
              "on #1349 were taken at.")

    print("\n  earned%     share of campaigns that OPENED the earned Cataclysm "
          "dungeon -- the headline")
    print("  wonE%       of those, the share that won")
    print("  LS%         share of campaigns where the Cataclysm reached the "
          "Pillar")
    print("  clrLS%      Last Stands actually CLEARED, per Last Stand reached "
          "-- which is NOT")
    print("              the same as campaigns that reached one and won, "
          "because half the")
    print("              earned-boss runs reach one too. The second reading is "
          "nearly twice")
    print("              the first. Issue #1286's ruling rests on this one.")
    print("  cities      cities lost, mean per campaign")
    print("  sieges      Sieges CREATED per campaign, never a survivor count")
    print("  noChance%   share of those Sieges that never gave the player one "
          "day both free")
    print("              and safe enough to answer them -- axis 3's response "
          "variable")
    print("  chances     mean number of such days per Siege")
    print("  seenDays    mean free days it stood through, WITHOUT the "
          "survivability filter --")
    print("              the gap between this and chances is Sieges the player "
          "could see and")
    print("              could not have survived")

    print(f"\n{'=' * 108}")
    print("AXIS 1 -- HOW OFTEN A SIEGE APPEARS. Spawn weight out of 100, the "
          "other six sub-types")
    print("taking up the slack in proportion. Doses from issue #1340. "
          f"{SIEGE_WEIGHT_TODAY:g} is what development ships.")
    print("=" * 108)
    print(HEADER)
    weight_rows = sweep(lambda w: f"weight {w:g}",
                        lambda w: replace(
                            BASE, SUBTYPE_SPAWN_WEIGHTS=weights_with_siege_at(w)),
                        SIEGE_WEIGHTS)

    print(f"\n{'=' * 108}")
    print("AXIS 2 -- HOW HARD ONE BITES. A multiplier on all three Siege "
          "constants together.")
    print("1.0 is what development ships; 0.0 is the control with the "
          "sub-type still spawning.")
    print("=" * 108)
    print(HEADER)
    scale_rows = sweep(lambda s: f"damage {s:g}x",
                       lambda s: replace(
                           BASE,
                           siege_defence_bite_per_day=(
                               BASE.siege_defence_bite_per_day * s),
                           siege_population_bite_per_day=(
                               BASE.siege_population_bite_per_day * s),
                           siege_damage_growth_per_day=(
                               BASE.siege_damage_growth_per_day * s)),
                       DAMAGE_SCALES)

    print(f"\n{'=' * 108}")
    print("AXIS 3 -- HOW LONG IT TAKES TO KILL A CITY. The growth term alone, "
          "in points per day")
    print("stood, with the 1% share of maximum left exactly where the owner "
          "set it on 2026-09-05.")
    print("=" * 108)
    print(HEADER)
    growth_rows = sweep(lambda g: f"growth {g:g}",
                        lambda g: replace(BASE,
                                          siege_damage_growth_per_day=g),
                        GROWTH_DOSES)
    growth_rows[GROWTH_TODAY] = scale_rows[1.0]
    print(row(f"growth {GROWTH_TODAY:g}", "A", scale_rows[1.0]["A"])
          + "   <- today, from the damage 1x row above")
    print(row(f"growth {GROWTH_TODAY:g}", "B", scale_rows[1.0]["B"]))

    print(f"\n{'=' * 108}")
    print("CANDIDATES -- combinations rather than points on a curve. Each "
          "eases BOTH ladders, which")
    print("neither axis above does. `today` is repeated from the rows above "
          "for comparison.")
    print("=" * 108)
    print(HEADER)
    candidate_rows = {}
    for label, weight, growth in CANDIDATES:
        cfg = replace(BASE,
                      SUBTYPE_SPAWN_WEIGHTS=weights_with_siege_at(weight),
                      siege_damage_growth_per_day=growth)
        candidate_rows[label] = {name: measure(cfg, seed0)
                                 for name, seed0 in BLOCKS}
        for name, _ in BLOCKS:
            print(row(label, name, candidate_rows[label][name]))
    candidate_rows["today"] = scale_rows[1.0]
    for name, _ in BLOCKS:
        print(row("today", name, scale_rows[1.0][name])
              + ("   <- weight 15, growth 10" if name == "A" else ""))

    table = axis_3_table()
    print(f"\n{'=' * 108}")
    print("DAYS AN UNATTENDED SIEGE TAKES TO EMPTY A CITY. Exact arithmetic, "
          "no campaigns.")
    print("=" * 108)
    names = [t.value for t in CITY_SIZES]
    print(f"{'dose':>12} " + " ".join(f"{n:>10}" for n in names))
    for scale, days in table["scale"].items():
        if scale == 0.0:
            continue
        print(f"{f'damage {scale:g}x':>12} "
              + " ".join(f"{days[t]:>10}" for t in CITY_SIZES))
    for growth, days in table["growth"].items():
        mark = "  <- today" if growth == GROWTH_TODAY else ""
        shown = ["never" if d == 0 else str(d) for d in
                 (days[t] for t in CITY_SIZES)]
        print(f"{f'growth {growth:g}':>12} "
              + " ".join(f"{d:>10}" for d in shown) + mark)

    today = table["scale"][1.0]
    half = table["scale"][0.5]
    quarter = table["scale"][0.25]
    outpost = CityTier.OUTPOST
    ratio_half = half[outpost] / today[outpost]
    ratio_quarter = quarter[outpost] / today[outpost]

    print(f"\n{'=' * 108}")
    print("WHAT THE RUN SAYS")
    print("=" * 108)
    print("\n1. AXIS 3 IS NOT AXIS 2, AND THIS IS THE ARITHMETIC THAT SHOWS "
          "IT.")
    print(f"   Halving every Siege constant takes an Outpost from "
          f"{today[outpost]} days to {half[outpost]}, which is "
          f"{ratio_half:.2f}x the")
    print(f"   time and not 2x. Quartering it reaches {quarter[outpost]} days, "
          f"{ratio_quarter:.2f}x. The damage a Siege has")
    print("   dealt after n days is quadratic in n, so time-to-fall moves as "
          "the INVERSE SQUARE")
    print("   ROOT of the damage. **To buy the player twice as long you must "
          "quarter the damage.**")
    print("   Setting the growth term to zero instead leaves the 1% share "
          "alone and gives every")
    print(f"   city size exactly {table['growth'][0.0][outpost]} days, which "
          "is a different lever and a different game.")

    for number, (name, rows, label) in enumerate(
            (("AXIS 1", weight_rows, "weight"),
             ("AXIS 2", scale_rows, "damage"),
             ("AXIS 3", growth_rows, "growth")), start=2):
        doses = sorted(rows)
        earned = [(d, both(rows[d], "earned%")) for d in doses]
        best = max(doses, key=lambda d: worst(rows[d], "earned%"))
        print(f"\n{number}. {name} ({label}) -- earned-route share by dose, "
              "both blocks:")
        for dose, (a, b) in earned:
            print(f"   {label} {dose:<6g}  block A {a:>5.1f}%   "
                  f"block B {b:>5.1f}%")
        print(f"   Best dose on the less favourable block: {label} {best:g}, "
              f"at {worst(rows[best], 'earned%'):.1f}%.")

    print("\n5. CANDIDATES -- earned-route share and cities lost of 25, both "
          "blocks:")
    for label in [*(c[0] for c in CANDIDATES), "today"]:
        cell = candidate_rows[label]
        ea, eb = both(cell, "earned%")
        ca, cb = both(cell, "cities")
        print(f"   {label:<10}  earned A {ea:>5.1f}%  B {eb:>5.1f}%   "
              f"cities lost A {ca:>5.2f}  B {cb:>5.2f}")

    print("\n6. THE RECOMMENDATION IS NOT IN THIS FILE. The owner ruled that "
          "no constant changes")
    print("   on the strength of a curve alone. This prints the curves; issue "
          "#1349 carries the")
    print("   recommendation and the owner chooses.")
    print()


# Called unconditionally, like every other analyse_*.py here, because
# `sim/tests/test_analysis_scripts.py` executes this file with `runpy.run_path`
# and that does not set `__name__` to `"__main__"`.
main()
