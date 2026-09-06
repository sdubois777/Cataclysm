"""What shape the Explorer branch's walk-time reduction should have. Issue #1383.

WHY THIS EXISTS. The project owner set a balance target on 2026-09-06, verbatim:
**"how do we keep a player who is fully invested into the explorer tree engaged?
That is what the game should be mostly balanced around, or at least to some
degree."** and directed, verbatim: **"adjust the explorer tree so it doesn't just
completely null the threat of the cataclysm"**.

**NOTHING HERE CHANGES A CONSTANT.** Every candidate is built by passing a
different `EmpireTree` and `run_days_min` to a `TuningConfig` for the length of
one batch. `cataclysm_sim/config.py` and `cataclysm_sim/engine.py` are not
written to, and no shape below is installed as a preset.

THE ENGINE ALREADY SUPPORTS EVERY SHAPE UNDER CONSIDERATION, which is the first
thing worth saying. `Simulation.run_days_for` computes

    days = max(run_days_min, min(run_days_max, (floors * days_per_floor
                                                - tree.run_days_flat)
                                               * tree.run_days_mult))

so a flat subtraction, a rate multiplier, a higher floor, and any combination of
them are already expressible in fields that exist. No candidate here needs a new
mechanism; the question is purely which of those three terms the design document
loads. That is why this file overrides no engine method: every row runs through
the shipped `run_days_for`, and so inherits the Cow Level exemption in
`_walk_days` -- "time is doubled and cannot be reduced" -- rather than having to
reproduce it.

SECTION 1 READS THE DESIGN DOCUMENT RATHER THAN TRUSTING THE MODEL. Issue #1288
found that one factor in the Architect branch's modelled multiplier matched no
node in `docs/Empire_Development_Tree_Final.json` at all. Section 1 therefore
re-derives the Explorer branch's totals from that file at run time and asserts
every node it names is present with the points and wording it expects, so a
change to the design document breaks this script instead of silently making it
wrong.

**IT FOUND ONE.** `TREE_EXPLORER_AS_DESIGNED` removes 70 flat days where the
Explorer branch's own unconditional nodes remove 60, and it credits the branch
with none of the +50 floors its depth nodes add at tier 1 -- issue #1386. It is
not repaired here; issue #1383 is under an instruction to change no tuning
constant.

**WHY THIS FILE READS THE GRAPH AND NOT THE PROSE.**
`docs/Empire_Skill_Tree_Keystones.md` describes the same tree and lists neither
Opportunist nor Rapid Descent. That is not a disagreement to resolve: issue #25
settled that the graph is the tree and the prose is commentary on it, which
`docs/README.md` and the prose file's own first paragraph both state and
`tools/tests/test_empire_tree_documents_agree.py` guards. The graph has 68 names
the prose does not mention, because it is about three weeks newer, and those two
are among them. An earlier version of this comment reported it as a second
finding and opened issue #1387 for it; that issue is closed as not planned and
the mistake was not searching `tools/tests/` for a test naming either file.

WHAT IS HELD FIXED, AND THE SURGE CADENCE IS THE HALF THIS FILE DOES NOT MOVE.
Difficulty tier 1, the `triage` policy, **static surges every 120 days for 5
dungeons**, resolve floor ratio 2.0, escalation 0.10 per 100 days, craft 12 days
for +4% of tier width. Those are `experiments.exp_calibrate`'s choices and what
the balance report runs at; the bare `TuningConfig` default of 4 dungeons a surge
is a value calibration rejects, so a figure here and a figure at 4 are not
comparable. **The owner's other directive was to raise the cadence**, and that is
being measured separately. Every campaign figure below therefore describes a
player on the cadence the owner called "incredibly low", and a shape chosen here
must be re-measured once the cadence moves.

HOW UNCERTAINTY IS REPORTED, AND WHY NOT AS A GAP BETWEEN TWO BLOCKS. Issue #1379
records the mistake: measurements on this project have treated the difference
between two seed blocks as a noise floor, and two blocks give one difference
rather than a spread. Every cell here is run as **eight disjoint seed blocks**.
The two halves issue #1383 asks for are the first four blocks and the last four,
printed separately as A and B; underneath them the standard deviation across the
eight block means is printed beside the standard error taken over the individual
campaigns, so the two can be read against each other rather than either being
taken on trust.
"""

from __future__ import annotations

import json
import math
import os
import pathlib
import statistics
from dataclasses import replace

from cataclysm_sim import policies
from cataclysm_sim.config import (
    TREE_EXPLORER_AS_DESIGNED,
    EmpireTree,
    SurgeMode,
    TuningConfig,
)
from cataclysm_sim.engine import Simulation

SIM_ROOT = pathlib.Path(__file__).resolve().parent
TREE_JSON = SIM_ROOT.parent / "docs" / "Empire_Development_Tree_Final.json"

#: Campaigns per seed block, eight blocks a row. `sim/tests/test_analysis_
#: scripts.py` executes this file, so the default has to be a smoke test; the
#: figures put to the owner on issue #1383 were taken with
#: `CATACLYSM_EXPLORER_TRIALS=500`, which is 4,000 campaigns a row and gives the
#: two 2,000-campaign halves that issue asks for. An environment variable rather
#: than a command line flag because the test executes this file with
#: `runpy.run_path`, which leaves `sys.argv` pointing at pytest's arguments;
#: `analyse_siege_dose.py` documents the same choice.
TRIALS = int(os.environ.get("CATACLYSM_EXPLORER_TRIALS", "1"))

BLOCK_COUNT = 8
BLOCKS = tuple((chr(ord("a") + i), i * TRIALS) for i in range(BLOCK_COUNT))
HALVES = (("A", BLOCKS[:BLOCK_COUNT // 2]), ("B", BLOCKS[BLOCK_COUNT // 2:]))


# =========================================================================
# 1. What the Explorer branch actually gives, read off the design document
# =========================================================================

#: The Explorer branch is the south-east quadrant. `metadata.description` in the
#: design document says so: "Branches: Architect (NE), Explorer (SE), Treasury
#: (SW), Artisan (NW)". Capstones sit on the central axis and are not part of any
#: branch, which is the distinction that matters below.
QUADRANTS = {("S", "E"): "Explorer", ("N", "E"): "Architect",
             ("S", "W"): "Treasury", ("N", "W"): "Artisan"}

#: Every node whose text changes how long a dungeon takes to walk, wherever it
#: sits in the tree, with what it is worth at full investment and what it is
#: conditional on. Found by sweeping every node and capstone option in the
#: document for time words and reading each hit; `assert_document_matches`
#: re-runs that sweep and fails if it finds one this list does not name.
#:
#: `days` is the flat days removed at full investment. `None` means the node is
#: not a flat subtraction at all and is described in `note` instead.
WALK_TIME_NODES = (
    # name, branch, points, days, condition
    ("Temporal Mastery", "Explorer", 25, 25.0, ""),
    ("Overclock", "Explorer", 20, 20.0, ""),
    ("Pacing", "Explorer", 10, 10.0, ""),
    ("Fleet Footed", "Explorer", 1, 5.0, ""),
    ("Opportunist", "Explorer", 5, 5.0,
     "only in a city with no other active dungeon"),
    ("Rapid Descent", "Explorer", 10, None,
     "-0.1 days of REMAINING run time per floor cleared per point"),
    ("Sovereign's Haste", "Explorer", 10, None,
     "-1 day per point per active Cataclysm type, capped at -30"),
    ("Tactical Entry", "Explorer", 1, None,
     "run days HALVED above 50 floors -- a multiplier, not a subtraction"),
    ("The Delver", "CENTRAL", 1, 5.0,
     "one of three exclusive options at the Tier 1 capstone"),
    ("The Last Stand", "CENTRAL", 1, None,
     "run time set to one day, in a city within 7 days of falling"),
    ("Imperial Roads", "Architect", 10, None,
     "-1 day per point, Fallen City dungeons only"),
)

#: Every Explorer node that changes how deep a dungeon is. `TREE_EXPLORER_AS_
#: DESIGNED` sets `floor_delta=0.0`, so the model credits the branch with its
#: day-removal nodes and none of these. `floors` is at full investment; the two
#: that depend on something outside the branch carry it in `note`.
FLOOR_NODES = (
    ("Architect of Greed", 20, 20.0, ""),
    ("Deep Boring", 10, 10.0, ""),
    ("Infinite Depths", 10, 20.0,
     "+2 floors per active Cataclysm type per point; 20 at tier 1, 160 at tier 8"),
    ("Architectural Insight", 10, 0.0,
     "+1 floor per 10 Architect points per point; 0 for a pure Explorer build"),
    ("Exclusionary Mapping", 10, -10.0,
     "the only node that removes floors, and it works against the four above"),
)

#: Nodes that pay the player for having removed days, or for reaching the one-day
#: floor. They are why the shape of the reduction is not only a pacing question:
#: change the shape and these three change what they are worth.
REWARD_NODES = (
    ("Speed Runner", 10, "+5% Loot Quantity per 2 days under default run time per point"),
    ("Efficiency Premium", 1, "+5% Loot Quantity per day removed, capped at 50%"),
    ("One-Day Specialist", 1,
     "if run time reaches the 1-day minimum, all Explorer loot modifiers doubled"),
)

#: Words that make a node's description worth reading for a time effect. The
#: sweep below is deliberately wider than the nodes it has to find, so that a
#: node phrased differently still surfaces. `assert_document_matches` runs a
#: positive and a negative control on it before believing either answer.
TIME_WORDS = ("day", "time", "halv", "faster", "speed", "duration", "timer",
              "instant", "remove")


def load_tree() -> list[dict]:
    with open(TREE_JSON, encoding="utf-8") as handle:
        return json.load(handle)["nodes"]


def branch_of(node: dict) -> str:
    """Which branch a node belongs to, from its position on the radial tree."""
    if node["data"].get("kind") == "capstone":
        return "CENTRAL"
    x, y = node["position"]["x"], node["position"]["y"]
    return QUADRANTS[("S" if y > 0 else "N", "E" if x > 0 else "W")]


def named(nodes: list[dict], name: str) -> tuple[dict, dict]:
    """The node or capstone option called `name`, and the node carrying it."""
    for node in nodes:
        if node["data"].get("name") == name:
            return node, node["data"]
        for option in node["data"].get("options") or []:
            if option["name"] == name:
                return node, option
    raise AssertionError(
        f"'{name}' is not in {TREE_JSON.name}. This script names it as an "
        f"Explorer-branch effect; either the node was renamed or removed, or "
        f"this list is wrong. Do not quote any total below until it is fixed.")


def assert_document_matches(nodes: list[dict]) -> None:
    """Every node this file names exists, with the branch and points it claims,
    and no time-affecting node exists that this file does not name.

    THE SECOND HALF IS THE ONE THAT MATTERS. A list of nodes typed out of a
    document is exactly what issue #1288 found had gone wrong for the Architect
    branch, and a check that only confirms the nodes you already wrote down
    cannot catch a node you never wrote down.
    """
    for name, branch, points, _days, _note in WALK_TIME_NODES:
        node, data = named(nodes, name)
        assert branch_of(node) == branch, (
            f"{name} is in the {branch_of(node)} branch, not {branch}")
        got = data.get("maxPoints", 1)
        assert got == points, f"{name} has {got} points, not {points}"
    for name, points, _floors, _note in FLOOR_NODES:
        node, data = named(nodes, name)
        assert branch_of(node) == "Explorer", f"{name} is not an Explorer node"
        assert data["maxPoints"] == points, f"{name} points moved"
    for name, points, _note in REWARD_NODES:
        node, data = named(nodes, name)
        assert branch_of(node) == "Explorer", f"{name} is not an Explorer node"
        assert data["maxPoints"] == points, f"{name} points moved"

    # The sweep. Controls first, because a search that finds nothing proves
    # nothing until the same search has been shown to find something.
    def mentions_time(text: str) -> bool:
        low = (text or "").lower()
        return any(word in low for word in TIME_WORDS)

    assert mentions_time("-1 day from dungeon run time per point."), (
        "the time-word sweep does not match Overclock's own description")
    assert not mentions_time("+5% Loot Quantity per point."), (
        "the time-word sweep matches a pure loot node")

    known = {name for name, *_ in WALK_TIME_NODES}
    # Nodes that mention time but do not change a dungeon's walk: resolution
    # timers, crafting time, city construction, gold ticks, movement speed
    # inside a floor, and the three reward nodes above. Named individually so a
    # NEW node cannot hide behind a category.
    allowed = {
        "Bastion Spirit", "Border Patrol", "Civil Order", "Enduring Foundations",
        "Rapid Renovation", "Strategic Reserve", "Emergency Shelters",
        "Martial Law", "Scorched Earth", "The Mason's Guild",
        "Ancient Techniques", "Industrial Speed", "Master Craftsman",
        "Sovereign's Assembly", "The Flood Barrier", "The Master Tinkerer",
        "The Warlord", "Gamble", "Pathfinding", "Sovereign Loans",
        "Tax Collection", "Trade Routes", "Wealth Interest", "Bribery",
        "Pax Imperialis", "Rapid Funding", "Quality over Quantity",
    } | {name for name, *_ in REWARD_NODES}
    found = set()
    for node in nodes:
        entries = [(node["data"].get("name"), node["data"].get("description"))]
        entries += [(o["name"], o.get("description"))
                    for o in node["data"].get("options") or []]
        for name, description in entries:
            if mentions_time(description or ""):
                found.add(name)
    unaccounted = found - known - allowed
    assert not unaccounted, (
        f"these nodes mention time and this script classifies none of them: "
        f"{sorted(unaccounted)}. Read each one and add it to WALK_TIME_NODES or "
        f"to the allowed list with a reason.")


def section_one(nodes: list[dict]) -> dict:
    print("=" * 100)
    print("THE EXPLORER BRANCH'S WALK-TIME SHAPE -- issue #1383")
    print("=" * 100)
    print()
    print("1. WHAT THE BRANCH GIVES, node by node, read off "
          f"docs/{TREE_JSON.name} at run time.")
    print("-" * 100)

    explorer = [n for n in nodes if branch_of(n) == "Explorer"]
    branch_points = sum(n["data"].get("maxPoints") or 0 for n in explorer)

    print(f"{'node':26} {'branch':10} {'pts':>4} {'days':>6}  condition")
    unconditional = 0.0
    unconditional_points = 0
    for name, branch, points, days, note in WALK_TIME_NODES:
        shown = f"-{days:g}" if days is not None else "see note"
        print(f"{name:26} {branch:10} {points:>4} {shown:>6}  "
              f"{note or 'always, every dungeon'}")
        if days is not None and not note and branch == "Explorer":
            unconditional += days
            unconditional_points += points
    print()
    print(f"  Explorer-branch unconditional flat total        "
          f"-{unconditional:g} days, bought with {unconditional_points} points "
          f"of the branch's {branch_points}")
    modelled = TREE_EXPLORER_AS_DESIGNED.run_days_flat
    print(f"  TREE_EXPLORER_AS_DESIGNED.run_days_flat         -{modelled:g} days")
    gap = modelled - unconditional
    print(f"  difference                                      {gap:g} days, and "
          f"it is Opportunist (conditional) plus The Delver (not an")
    print(f"{'':50}Explorer node -- one of three exclusive options at the "
          f"Tier 1 capstone)")
    print()
    print("  The config comment above that constant calls its six terms 'every "
          "unconditional flat run-time")
    print("  reduction in Empire_Development_Tree_Final'. Two of the six are "
          "neither unconditional nor in")
    print("  the Explorer branch. It names Rapid Descent and Sovereign's Haste "
          "as its only exclusions; it")
    print("  does not mention Tactical Entry, which halves run days above 50 "
          "floors and is the one")
    print("  multiplicative walk-time node the tree already has.")
    print()
    print(f"  IT TAKES {unconditional_points} POINTS TO REMOVE "
          f"{unconditional:g} DAYS. The deepest dungeon a surge can put on the "
          "board is 50 floors")
    print(f"  (a Quest at a Sanctuary), so the collapse is complete at "
          f"{unconditional_points} of the branch's {branch_points} points, well "
          "short of the")
    print("  full investment the owner's question is about.")
    print()

    print("  FLOOR-COUNT NODES IN THE SAME BRANCH, WHICH THE MODEL CREDITS THE "
          "BRANCH WITH NONE OF:")
    print(f"{'node':26} {'pts':>4} {'floors':>7}  note")
    floors_total = 0.0
    for name, points, floors, note in FLOOR_NODES:
        print(f"{name:26} {points:>4} {floors:>+7g}  {note}")
        if floors > 0:
            floors_total += floors
    print(f"{'':26} {'':>4} {floors_total:>+7g}  taking Exclusionary Mapping's "
          f"-10 as not taken")
    print(f"  `TREE_EXPLORER_AS_DESIGNED.floor_delta` is "
          f"{TREE_EXPLORER_AS_DESIGNED.floor_delta:+g}. A maxed Explorer at "
          f"tier 1 adds {floors_total:+g} floors to every")
    print("  dungeon, which changes both what the flat subtraction collapses "
          "and what the dungeon is worth.")
    print("  Rows marked '+50f' below are run with that included.")
    print()

    print("  NODES THAT PAY FOR HAVING REMOVED DAYS -- change the shape and "
          "these change what they are worth:")
    for name, points, note in REWARD_NODES:
        print(f"{name:26} {points:>4}         {note}")
    print()
    return {"unconditional": unconditional, "modelled": modelled,
            "unconditional_points": unconditional_points,
            "branch_points": branch_points, "floors_total": floors_total}


# =========================================================================
# 2. The shapes, before any campaign is run
# =========================================================================

#: The depths every shape is quoted at. 11 / 20 / 32 are the midpoints of the
#: Basic dungeon at the three city sizes a surge can target -- `SURGE_TARGET_
#: WEIGHT` gives the Pillar 0.0 -- 50 is the deepest a surge can reach (a Quest
#: at a Sanctuary) and the depth the owner used in the sentence this question
#: comes from, verbatim "you could have a 50 floor dungeon that only takes you a
#: couple days in world time to beat", and 125 is the midpoint of the earned
#: Cataclysm dungeon.
QUOTED_DEPTHS = (11, 20, 32, 50, 125)

#: The floors an ordinary surge-spawned Basic dungeon can have, across the three
#: city sizes a surge targets: 8-15, 15-25, 25-40 from `DUNGEON_SPECS`. This is
#: the range issue #1383's own table uses and the common case.
ORDINARY = range(8, 41)


def surge_reachable() -> range:
    """Every floor count a surge can put on the board, read off the config.

    **WIDER THAN `ORDINARY`, AND THE DIFFERENCE MATTERS.** Two things issue
    #1383's table leaves out. A surge rolls a Quest dungeon 12% of the time
    (`quest_dungeon_chance`) and a Quest at a Sanctuary is 30-50 floors where a
    Basic is 25-40. And every surge-spawned dungeon is scaled by the active
    Cataclysm's `floors_mult`, which runs from 0.55 for Death to 1.60 for
    Celestial. Together they take the reachable range from 8-40 to 4-80, so
    **a flat 70 does not put literally every surge dungeon on the one-day
    floor**: a Celestial Quest at a Sanctuary can be 80 floors and still walk
    in 10 days. How rare that is, is what the `at floor%` column in section 3
    measures on the dungeons campaigns actually contain.
    """
    from cataclysm_sim.config import DungeonType
    from cataclysm_sim.patterns import PATTERNS
    mults = [p.floors_mult for p in PATTERNS.values()]
    los, his = [], []
    for (dtype, tier), spec in BASE.DUNGEON_SPECS.items():
        if BASE.SURGE_TARGET_WEIGHT.get(tier, 0.0) <= 0.0:
            continue
        if dtype not in (DungeonType.BASIC, DungeonType.QUEST):
            continue
        los.append(max(1, int(round(spec.floors[0] * min(mults)))))
        his.append(max(1, int(round(spec.floors[1] * max(mults)))))
    return range(min(los), max(his) + 1)


class Shape:
    """One candidate, expressed in the three fields `run_days_for` already has."""

    def __init__(self, label: str, flat: float = 0.0, mult: float = 1.0,
                 minimum: int = 1, floor_delta: float = 0.0) -> None:
        self.label = label
        self.flat, self.mult = flat, mult
        self.minimum, self.floor_delta = minimum, floor_delta

    def config(self, base: TuningConfig) -> TuningConfig:
        return replace(base, run_days_min=self.minimum).with_tree(
            EmpireTree(name=self.label, run_days_flat=self.flat,
                       run_days_mult=self.mult, floor_delta=self.floor_delta))

    def days(self, floors: int, base: TuningConfig) -> int:
        """What the engine returns for a dungeon this deep under this shape.

        Recomputed here rather than called on a `Simulation`, so section 2 needs
        no campaigns; `test_the_shape_table_matches_the_engine` in
        `sim/tests/test_explorer_shape.py` checks the two agree.
        """
        cfg = self.config(base)
        floors = max(1, int(round(floors + cfg.tree.floor_delta)))
        value = floors * cfg.days_per_floor - cfg.tree.run_days_flat
        value *= cfg.tree.run_days_mult
        value = max(cfg.run_days_min, min(cfg.run_days_max, value))
        return int(math.ceil(value))


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
    )


BASE = base_config()
FLAT_TODAY = TREE_EXPLORER_AS_DESIGNED.run_days_flat

#: The candidates, in the order they are printed. Read off the config rather
#: than written out where possible, so a change to `TREE_EXPLORER_AS_DESIGNED`
#: cannot leave this file claiming the wrong baseline.
SHAPES = (
    Shape("none"),
    Shape(f"flat {FLAT_TODAY:g}", flat=FLAT_TODAY),
    Shape("flat 60", flat=60.0),
    Shape(f"flat {FLAT_TODAY:g} min10", flat=FLAT_TODAY, minimum=10),
    Shape("x0.30", mult=0.30),
    Shape("x0.20", mult=0.20),
    Shape("x0.15", mult=0.15),
    Shape("x0.10", mult=0.10),
    Shape("flat5 x0.15", flat=5.0, mult=0.15),
    Shape("flat5 x0.10", flat=5.0, mult=0.10),
    Shape("flat 60 +50f", flat=60.0, floor_delta=50.0),
    Shape("flat5 x0.15 +50f", flat=5.0, mult=0.15, floor_delta=50.0),
)


def gradient(shape: Shape, depths=None) -> tuple[float, int]:
    """How much floor count still changes the walk, over a range of depths.

    Returns the ratio of the deepest walk to the shallowest, and how many
    DISTINCT walk lengths the range produces. **The count is the better of the
    two.** A ratio of 1.00 and a count of 1 both say the gradient is gone, but a
    ratio can be flattered by a single step at one end while a count of 2 says
    plainly that floor count buys the player two answers.
    """
    days = [shape.days(f, BASE) for f in (ORDINARY if depths is None else depths)]
    return days[-1] / days[0], len(set(days))


def on_the_floor(shape: Shape, depths=None) -> float:
    """Share of a depth range this shape puts on its own minimum, where floor
    count buys the player nothing at all."""
    depths = ORDINARY if depths is None else depths
    days = [shape.days(f, BASE) for f in depths]
    return 100.0 * sum(1 for d in days if d == shape.minimum) / len(days)


def section_two() -> dict:
    print("-" * 100)
    print("2. THE SHAPES THEMSELVES, before any campaign is run. Walk days at "
          "each depth.")
    print("-" * 100)
    wide = surge_reachable()
    head = "  ".join(f"{f:>4}f" for f in QUOTED_DEPTHS)
    print(f"   Basic dungeons at the three surge-target city sizes are "
          f"{ORDINARY[0]}-{ORDINARY[-1]} floors. Counting Quest")
    print(f"   dungeons and the active Cataclysm's depth multiplier, a surge "
          f"can reach {wide[0]}-{wide[-1]} floors -- see")
    print("   `surge_reachable`. Both are reported, because the second is "
          "wider than issue #1383's own table.")
    print()
    print(f"{'shape':>17}  {head}  "
          f"{'answers':>8} {'on floor':>9} | "
          f"{'answers':>8} {'on floor':>9}")
    print(f"{'':>17}  {'':<{len(head)}}  "
          f"{f'--- {ORDINARY[0]}-{ORDINARY[-1]}f ---':>18} | "
          f"{f'--- {wide[0]}-{wide[-1]}f ---':>18}")
    table = {}
    for shape in SHAPES:
        cells = "  ".join(f"{shape.days(f, BASE):>5}" for f in QUOTED_DEPTHS)
        ratio, answers = gradient(shape)
        _wr, wide_answers = gradient(shape, wide)
        table[shape.label] = {
            "ratio": ratio, "answers": answers,
            "clamped": on_the_floor(shape),
            "wide_answers": wide_answers,
            "wide_clamped": on_the_floor(shape, wide),
            "days": {f: shape.days(f, BASE) for f in QUOTED_DEPTHS}}
        t = table[shape.label]
        print(f"{shape.label:>17}  {cells}  "
              f"{answers:>8} {t['clamped']:>8.0f}% | "
              f"{wide_answers:>8} {t['wide_clamped']:>8.0f}%")
    print()
    print("  'answers' is how many different walk lengths the range produces "
          "and 'on floor' is the share of")
    print("  it that lands on the shape's own minimum. One answer means floor "
          "count has stopped being a")
    print("  reason to pick one dungeon over another.")
    print()
    shipped = table[f"flat {FLAT_TODAY:g}"]
    print("  A CORRECTION TO ISSUE #1383, WHICH SAYS EVERY SURGE DUNGEON "
          "COSTS ONE DAY. Over the Basic range")
    print(f"  that is right -- {shipped['clamped']:.0f}% of it is on the "
          f"floor and there is {shipped['answers']} walk length left. Over the "
          f"wider range it is")
    print(f"  {shipped['wide_clamped']:.0f}%, with "
          f"{shipped['wide_answers']} lengths left, all of them on dungeons "
          f"deeper than 71 floors: a Quest at a")
    print("  Sanctuary under a Cataclysm with a high depth multiplier. "
          "Section 3's 'at floor%' column measures")
    print("  how often that actually happens in a campaign.")
    print()

    print("  A FLAT SUBTRACTION AND A RATE, SET TO THE SAME SPEED ON THE "
          "DEEPEST ORDINARY DUNGEON.")
    print("  Each row is a pair that walks a 40-floor dungeon in the same "
          "number of days: a flat amount of")
    print("  40-d, and a rate of d/40. What they do to that one dungeon is "
          "identical. What they do to")
    print("  everything shallower is not.")
    print(f"{'40f walk':>10} | {'flat':>6} {'8f':>4} {'on floor':>9} "
          f"{'answers':>8} | {'rate':>6} {'8f':>4} {'on floor':>9} "
          f"{'answers':>8}")
    matched = {}
    for target in (20, 12, 8, 6, 4, 3, 2, 1):
        by_flat = Shape("f", flat=40.0 - target)
        by_rate = Shape("r", mult=target / 40.0)
        cells = []
        for probe in (by_flat, by_rate):
            days = [probe.days(f, BASE) for f in ORDINARY]
            cells.append((on_the_floor(probe), len(set(days))))
        matched[target] = {"flat": 40.0 - target, "mult": target / 40.0,
                           "flat_on_floor": cells[0][0],
                           "rate_on_floor": cells[1][0],
                           "flat_answers": cells[0][1],
                           "rate_answers": cells[1][1]}
        print(f"{target:>10} | {40.0 - target:>6g} "
              f"{by_flat.days(8, BASE):>4} {cells[0][0]:>8.0f}% "
              f"{cells[0][1]:>8} | {target / 40.0:>6.3f} "
              f"{by_rate.days(8, BASE):>4} {cells[1][0]:>8.0f}% "
              f"{cells[1][1]:>8}")
    print()
    print("  'on floor' is the share of the 8-to-40 floor range that lands on "
          "the one-day minimum, where")
    print("  floor count buys the player nothing. A FLAT SUBTRACTION FLATTENS "
          "EVERYTHING SHALLOWER THAN THE")
    print("  AMOUNT IT REMOVES, so the faster it makes the deep dungeon the "
          "more of the range it erases. A")
    print("  rate flattens only what falls under one day, which is the "
          "shallow tail and nothing else.")
    print()
    fast = matched[4]
    print("  At a 40-floor walk of 4 days the two are the same speed there "
          "and nowhere else: the flat")
    print(f"  {fast['flat']:g} puts {fast['flat_on_floor']:.0f}% of the range "
          f"on the floor and the rate {fast['mult']:.3f} puts "
          f"{fast['rate_on_floor']:.0f}%.")
    print()
    print("  THE LIMIT THIS PUTS ON HOW FAST AN INVESTED PLAYER CAN BE, and it "
          "is a constraint on any shape:")
    slow = matched[1]
    print(f"  at a 40-floor walk of 1 day even the rate puts "
          f"{slow['rate_on_floor']:.0f}% of the range on the floor. Walk time "
          f"is a whole")
    print("  number of days, so a gradient needs somewhere to go. The deepest "
          "ordinary dungeon has to stay")
    print(f"  at roughly {min(t for t in matched if matched[t]['rate_on_floor'] == 0):g} "
          f"days or more for the rate to leave the whole range distinguishable.")
    print()
    print("  RAISING THE FLOOR MOVES WHERE THE COLLAPSE LANDS WITHOUT REMOVING "
          "IT. Under a flat 70 every")
    print("  dungeon in the range still costs the same number of days at any "
          "floor height; the number is")
    print("  just larger. The 'answers' column for `flat 70 min10` above is "
          "1, the same as `flat 70`.")
    print()
    return table


# =========================================================================
# 3. What each shape does to a campaign
# =========================================================================

class _Recording(Simulation):
    """A campaign that keeps every dungeon it built. Changes no behaviour."""

    def __init__(self, cfg: TuningConfig, seed: int = 0) -> None:
        super().__init__(cfg, seed=seed)
        self.made: list[tuple[int, int, str]] = []

    def _make_dungeon(self, *args, **kwargs):
        d = super()._make_dungeon(*args, **kwargs)
        self.made.append((d.floors, d.run_days, d.subtype))
        return d


def empty_board_counter(policy):
    """Wrap a policy so free days facing an empty board are counted.

    **THIS IS THE OWNER'S COMPLAINT, AND `RunResult.idle_days` IS NOT IT.**
    `idle_days` counts every free day on which the policy declined, which
    includes days with dungeons standing that the player judged not worth
    entering. A day with nothing on the board at all is a different thing:
    there is no decision to make and no risk being taken, and it is what "4
    dungeons every 120 days is incredibly low, and boring either way"
    describes. Both are reported.

    Draws no random numbers, so an instrumented batch runs the same campaigns a
    bare one would.
    """
    def counted(sim, dungeons):
        if not dungeons:
            sim.empty_days = getattr(sim, "empty_days", 0) + 1
        return policy(sim, dungeons)
    return counted


def measure(shape: Shape, seed0: int, trials: int = TRIALS) -> dict:
    """One block: `trials` campaigns from `seed0` upward under one shape."""
    cfg = shape.config(BASE)
    policy = empty_board_counter(policies.ALL["triage"])
    results, empties, made = [], [], []
    for i in range(trials):
        sim = _Recording(cfg, seed=seed0 + i)
        results.append(sim.run(policy))
        empties.append(getattr(sim, "empty_days", 0))
        made.extend(sim.made)

    days = [max(1, r.survived_days) for r in results]
    at_floor = sum(1 for _f, d, _s in made if d <= shape.minimum)
    return {
        "n": trials,
        "_earned": [1.0 if r.cataclysm_floors > 0 else 0.0 for r in results],
        "_won": [1.0 if r.won else 0.0 for r in results],
        "_ls": [1.0 if r.last_stand else 0.0 for r in results],
        "_cities": [float(r.cities_lost) for r in results],
        "_days": [float(d) for d in days],
        "_empty": [100.0 * e / d for e, d in zip(empties, days, strict=True)],
        "_idle": [100.0 * r.idle_days / d
                  for r, d in zip(results, days, strict=True)],
        "_cleared": [float(r.dungeons_cleared) for r in results],
        "at_floor%": 100.0 * at_floor / max(1, len(made)),
        "dungeons": len(made),
    }


SAMPLES = (("earned%", "_earned", 100.0), ("won%", "_won", 100.0),
           ("LS%", "_ls", 100.0), ("cities", "_cities", 1.0),
           ("days", "_days", 1.0), ("empty%", "_empty", 1.0),
           ("idle%", "_idle", 1.0), ("cleared", "_cleared", 1.0))


def pool(cells: list[dict]) -> dict:
    """Several blocks read as one sample, with standard errors over campaigns."""
    n = sum(c["n"] for c in cells)
    out = {"n": n,
           "at_floor%": sum(c["at_floor%"] * c["dungeons"] for c in cells)
                        / max(1, sum(c["dungeons"] for c in cells)),
           "dungeons": sum(c["dungeons"] for c in cells)}
    for key, sample, scale in SAMPLES:
        values = [v * scale for c in cells for v in c[sample]]
        out[key] = statistics.fmean(values)
        out[key + "_se"] = (statistics.stdev(values) / math.sqrt(n)
                            if n > 1 else 0.0)
    return out


HEADER = (f"{'shape':>17} {'blk':>4} {'earned%':>8} {'won%':>6} {'LS%':>5} "
          f"{'cities':>7} {'days':>6} {'empty%':>7} {'idle%':>6} "
          f"{'cleared':>8} {'at floor%':>10}")


def row(label: str, block: str, s: dict) -> str:
    return (f"{label:>17} {block:>4} {s['earned%']:>8.1f} {s['won%']:>6.1f} "
            f"{s['LS%']:>5.1f} {s['cities']:>7.2f} {s['days']:>6.0f} "
            f"{s['empty%']:>7.1f} {s['idle%']:>6.1f} {s['cleared']:>8.1f} "
            f"{s['at_floor%']:>10.1f}")


def section_three() -> dict:
    print("-" * 100)
    print("3. WHAT EACH SHAPE DOES TO A CAMPAIGN. A and B are the two disjoint "
          "halves issue #1383 asks")
    print("   for; a to h are the eight blocks they are made of.")
    print("-" * 100)
    print(f"tier {BASE.tier}, policy triage, "
          f"{BASE.surge_mode.name.lower()} surges every "
          f"{BASE.surge_interval_days:g} days for {BASE.surge_dungeon_count} "
          f"dungeons, resolve floor ratio {BASE.resolve_floor_ratio:g},")
    print(f"{TRIALS} campaigns x {BLOCK_COUNT} blocks = "
          f"{TRIALS * BLOCK_COUNT} per shape, so each half is "
          f"{TRIALS * BLOCK_COUNT // 2}.")
    if TRIALS < 250:
        print()
        print(f"  *** {TRIALS} CAMPAIGNS A BLOCK IS A SMOKE TEST AND THE "
              f"FIGURES BELOW ARE NOISE. ***")
        print("  Set CATACLYSM_EXPLORER_TRIALS=500 for the size the "
              "conclusions were taken at.")
    print()
    print(HEADER)
    measured = {}
    for shape in SHAPES:
        cells = {name: measure(shape, seed0) for name, seed0 in BLOCKS}
        measured[shape.label] = cells
        for half, group in HALVES:
            print(row(shape.label, half, pool([cells[n] for n, _ in group])))
        for name, _ in BLOCKS:
            print(row("", name, pool([cells[name]])))
    print()
    return measured


def section_four(measured: dict, table: dict) -> dict:
    print("-" * 100)
    print("4. POOLED OVER ALL EIGHT BLOCKS, WITH TWO ESTIMATES OF THE ERROR.")
    print("   'se' is the standard error over the individual campaigns. 'sd8' "
          "is the standard deviation")
    print("   of the eight block means. Issue #1379: a gap between two blocks "
          "is one difference, not a")
    print("   spread. If sd8 is far larger than se x sqrt(8) the blocks "
          "disagree more than sampling explains.")
    print("-" * 100)
    print(f"{'shape':>17} {'earned%':>22} {'cities lost of 25':>24} "
          f"{'empty board days%':>22} {'answers':>8}")
    pooled = {}
    for shape in SHAPES:
        cells = [measured[shape.label][n] for n, _ in BLOCKS]
        p = pool(cells)
        for key, sample, scale in SAMPLES:
            block_means = [statistics.fmean([v * scale for v in c[sample]])
                           for c in cells]
            p[key + "_sd8"] = (statistics.stdev(block_means)
                               if len(block_means) > 1 else 0.0)
        pooled[shape.label] = p
        print(f"{shape.label:>17} "
              f"{p['earned%']:>10.1f} +-{p['earned%_se']:<4.1f} "
              f"/{p['earned%_sd8']:<5.1f} "
              f"{p['cities']:>12.2f} +-{p['cities_se']:<4.2f} "
              f"/{p['cities_sd8']:<5.2f} "
              f"{p['empty%']:>10.1f} +-{p['empty%_se']:<4.1f} "
              f"/{p['empty%_sd8']:<5.1f} "
              f"{table[shape.label]['answers']:>8}")
    print()
    return pooled


def section_five(pooled: dict, table: dict, facts: dict) -> None:
    print("-" * 100)
    print("5. WHAT THE UNINVESTED PLAYER PAYS, AND WHAT THE RECOMMENDATION "
          "COSTS THEM. Issue #1383 item 4.")
    print("-" * 100)
    control = pooled["none"]
    today = pooled[f"flat {FLAT_TODAY:g}"]
    print(f"  An untreed player reaches the earned Cataclysm dungeon in "
          f"{control['earned%']:.1f}% of campaigns, loses "
          f"{control['cities']:.2f}")
    print(f"  of 25 cities, clears {control['cleared']:.1f} dungeons and "
          f"spends {control['empty%']:.1f}% of the campaign with an empty "
          f"board.")
    print(f"  The shipped branch takes those to {today['earned%']:.1f}%, "
          f"{today['cities']:.2f}, {today['cleared']:.1f} and "
          f"{today['empty%']:.1f}%, with")
    print(f"  {today['at_floor%']:.0f}% of every dungeon it meets walked at the "
          f"one-day floor and "
          f"{table[f'flat {FLAT_TODAY:g}']['answers']} distinct walk lengths "
          f"left across 8-40 floors.")
    print()
    print("  A candidate has to leave the invested player ahead of the "
          "uninvested one -- investment must")
    print("  still be worth something -- while leaving floor count a reason to "
          "choose one dungeon over")
    print("  another, and it must not make the untreed run harder than it is "
          "today. Every candidate that")
    print("  changes only the tree leaves the untreed row exactly where it is, "
          "because a player with no tree")
    print("  has `run_days_flat` 0 and `run_days_mult` 1.00 whatever the "
          "branch's nodes say.")
    print()
    print("  THE ONE EXCEPTION, AND IT IS THE ONLY CANDIDATE THAT COSTS THE "
          "UNINVESTED PLAYER ANYTHING: raising")
    print("  `run_days_min` from 1 to 10 is a change to `TuningConfig`, not to "
          "the tree, so it applies to")
    print("  everyone. It makes an 8-floor dungeon cost an untreed player 10 "
          "days instead of 8. That is a")
    raised = table[f"flat {FLAT_TODAY:g} min10"]
    print(f"  second reason to reject it beyond the {raised['answers']} "
          f"distinct walk lengths it leaves.")
    print()
    print(f"  ON THE NUMBERS ABOVE BEING A LOWER BOUND: the modelled flat "
          f"{facts['modelled']:g} is "
          f"{facts['modelled'] - facts['unconditional']:g} days more than the")
    print(f"  Explorer branch's own unconditional total of "
          f"{facts['unconditional']:g}, and at the same time the model credits "
          f"the branch")
    print(f"  with none of its {facts['floors_total']:+g} floors, none of "
          f"Rapid Descent, none of Sovereign's Haste and none of")
    print("  Tactical Entry. Every one of those makes an invested player "
          "faster than the '+50f' rows show.")
    print()


def main() -> dict:
    nodes = load_tree()
    assert_document_matches(nodes)
    facts = section_one(nodes)
    table = section_two()
    measured = section_three()
    pooled = section_four(measured, table)
    section_five(pooled, table, facts)
    return {"facts": facts, "table": table, "pooled": pooled,
            "measured": measured}


RESULT = main()
