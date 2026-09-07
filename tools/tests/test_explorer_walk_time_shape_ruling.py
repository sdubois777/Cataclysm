"""The 2026-09-06 rulings on the Explorer branch's walk time are recorded, and
the design document still matches what the record says about it.

WHY THIS EXISTS. Issue #1383. The project owner ruled two things on 2026-09-06:
that the Explorer branch's four unconditional walk-time nodes change from "-1 day
per point" to a percentage of the dungeon's run time, and that the 2026-09-05
sentence about "a 50 floor dungeon that only takes you a couple days" describes
the empire tree plus city upgrades plus the situational nodes together rather
than the tree alone. `CLAUDE.md`: a design decision is not real until it is in
`docs/`, so both are written into `docs/DECISIONS.md`.

**THE PERCENTAGES ARE NOW CHOSEN AND THE TREE CARRIES THEM.** When this file was
written the owner had ruled the shape and not the per-point values, so the four
nodes still read "-1 day per point" and the guard here asserted that state and
named what would have to change together. The values were chosen on 2026-09-07
by `sim/analyse_explorer_rate.py` -- -2.5% of run time per point on the three
basic nodes and -12% on the keystone, x0.2186 combined -- and all four node
descriptions in `docs/Empire_Development_Tree_Final.json` were rewritten. That
guard has done its job and is replaced by
`test_the_four_ruled_nodes_now_read_as_a_percentage`, which holds the new state
down the same way.

THAT IS WHAT MAKES THIS FILE WORTH HAVING. Four records say what these nodes do
-- the node graph, the decision log, the simulation preset and the analysis
script -- and a wrong one is invisible: a node description reading "-3% per
point" looks perfectly correct on its own.

WHAT IS CHECKED HERE AND NOT IN
`tools/tests/test_empire_tree_documents_agree.py`. That file compares the node
graph against the prose commentary and deliberately does not check what a node
*does* -- only that the names line up. This one checks what four specific nodes
say they do, against a decision log entry.

WHAT IS NOT CHECKED. Any number from the simulation. The measurements behind the
ruling are on issue #1383 and in `sim/analyse_explorer_shape.py`, which
`sim/tests/test_explorer_shape.py` covers.
"""

from __future__ import annotations

import ast
import json
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCS = REPO_ROOT / "docs"
GRAPH = DOCS / "Empire_Development_Tree_Final.json"
LOG = DOCS / "DECISIONS.md"
ANALYSIS = REPO_ROOT / "sim" / "analyse_explorer_shape.py"

TARGET_HEADING = ("## 2026-09-06 — The game is balanced around a player "
                  "fully invested in the Explorer tree")
SHAPE_HEADING = ("## 2026-09-06 — The Explorer branch's unconditional "
                 "walk-time nodes become a percentage, and \"a couple of "
                 "days\" is the whole stack")

#: The entry that chose the per-point values the shape ruling left open.
VALUE_HEADING = ("## 2026-09-07 — The Explorer branch's walk-time percentage "
                 "is -2.5% per point, and Speed Runner gains a cap")

#: The four the owner ruled change, and the fixed-day wording each still has.
#: They are checked by the word "day" rather than by the whole sentence, so
#: rewording a description without changing the shape does not fail this.
RULED_TO_CHANGE = ("Temporal Mastery", "Overclock", "Pacing", "Fleet Footed")

#: The six the owner ruled stay as a fixed number of days. Unlike the four
#: above, these are expected to keep saying "day" forever -- converting one of
#: them to a percentage would be going past what was ruled.
RULED_TO_STAY = ("Opportunist", "Sovereign's Haste", "The Delver",
                 "The Last Stand", "Rapid Descent", "Tactical Entry")


@pytest.fixture(scope="module")
def log_text() -> str:
    assert LOG.is_file(), "docs/DECISIONS.md is missing"
    return LOG.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def nodes() -> dict[str, dict]:
    """Every node and capstone option in the graph, by name."""
    graph = json.loads(GRAPH.read_text(encoding="utf-8"))
    out: dict[str, dict] = {}
    for node in graph["nodes"]:
        out[node["data"]["name"]] = node["data"]
        for option in node["data"].get("options") or []:
            out[option["name"]] = option
    return out


def flatten(markdown: str) -> str:
    r"""One line, with blockquote markers gone.

    **A PLAIN `re.sub(r"\s+", " ")` IS NOT ENOUGH HERE AND THAT IS NOT
    THEORETICAL** -- it is what the first version of this file did, and
    `test_the_balance_target_is_recorded_word_for_word` failed on its own
    decision log entry. Every file in `docs/` is hard-wrapped, and the owner's
    words are quoted in blockquotes, so a phrase that crosses a line comes back
    as "or at least to some > degree". Strip the marker first, then flatten.
    """
    lines = [re.sub(r"^\s*>\s?", "", line) for line in markdown.splitlines()]
    return re.sub(r"\s+", " ", " ".join(lines))


def entry(text: str, heading: str) -> str:
    """One entry out of the decision log, heading to the next rule.

    THE ENTRY AND NOT THE WHOLE FILE, for the reason
    `test_empire_tree_documents_agree.py` gives: a phrase can survive the record
    of what happened to it being deleted, because an earlier entry mentioned it
    while it was still an open question.
    """
    assert heading in text, (
        f"docs/DECISIONS.md has no entry headed {heading!r}. The rulings on "
        f"issue #1383 live only there.")
    body = text[text.index(heading):]
    end = body.find("\n---", 1)
    return body if end == -1 else body[:end]


# --------------------------------------------------------------------------
# The rulings are on the record
# --------------------------------------------------------------------------

def test_the_balance_target_is_recorded_word_for_word(log_text):
    """The owner's own sentence, not a paraphrase of it. It is the thing every
    future tuning decision is aimed at, and a paraphrase drifts."""
    body = entry(log_text, TARGET_HEADING)
    verbatim = ("how do we keep a player who is fully invested into the "
                "explorer tree engaged")
    flat = flatten(body)
    assert verbatim in flat, (
        "the balance target entry no longer quotes the owner's sentence. It is "
        "the whole content of that decision.")
    assert "or at least to some degree" in flat, (
        "the entry drops the qualifier the owner put on the target, which is "
        "what stops it licensing a change that only works for an invested "
        "player.")


def test_the_shape_ruling_and_the_stack_ruling_are_both_recorded(log_text):
    body = entry(log_text, SHAPE_HEADING)
    flat = flatten(body)
    assert '"Change to a percentage"' in flat, (
        "the entry no longer carries the owner's answer on the shape.")
    assert '"The whole stack"' in flat, (
        "the entry no longer carries the owner's answer on what 'a couple of "
        "days' covers.")
    assert "not ruled" in flat and "per-point" in flat, (
        "the entry must keep saying the per-point percentages are NOT ruled. "
        "Without that a reader takes the illustration in the analysis as the "
        "decision.")


def test_the_chosen_percentages_are_recorded_in_the_decision_log(log_text):
    """`CLAUDE.md`: a design decision is not real until it is in `docs/`.

    The owner ruled the shape and left the values open, and the values were
    chosen on 2026-09-07. **THE VALUES ARE THE PART THAT CAN BE OVERRULED**, so
    the entry has to carry them, has to carry the total they multiply to, and
    has to say plainly that they are not the owner's.
    """
    body = entry(log_text, VALUE_HEADING)
    # **THE HEADING ITSELF CARRIES "-2.5%", SO IT IS DROPPED BEFORE SEARCHING.**
    # Without that this test passes on the heading alone and cannot fail while
    # the entry exists, which is a check worth nothing. Proved with
    # `tools/prove_guard.py`: the first attempt at it did exactly that.
    flat = flatten(body.split(chr(10), 1)[1])
    for figure in ("-2.5%", "-12%", "x0.2186"):
        assert figure in flat, (
            f"the 2026-09-07 entry does not state {figure}. It is one of the "
            "three numbers the change turns on: the per-point value, the "
            "keystone, and what they multiply to at full investment.")
    assert "not the owner's" in flat, (
        "the entry must keep saying the per-point values are not the owner's "
        "ruling. The owner ruled the shape; a reader who takes the values as "
        "ruled cannot tell which part is open to revision.")


def test_the_entry_names_the_genre_sources(log_text):
    """`CLAUDE.md` requires the sources behind a formula in `docs/DECISIONS.md`
    and not only in the issue that produced it, so the next person can see why
    the shape was chosen and not only what was chosen."""
    flat = flatten(entry(log_text, VALUE_HEADING))
    for source in ("maxroll.gg", "pathofexile.fandom.com", "diablo"):
        assert source in flat.lower(), (
            f"the 2026-09-07 entry names no source containing {source!r}. The "
            "shape is a percentage combined multiplicatively because four "
            "shipped games in the genre do it that way, and that evidence has "
            "to be in docs/ rather than only on the issue.")


def test_the_entry_says_what_the_change_does_not_fix(log_text):
    """The 2026-09-06 ruling is explicit that the shape change does not make the
    Cataclysm a threat, and this project has had that read the wrong way once
    already. The entry that ships the numbers has to repeat it."""
    flat = flatten(entry(log_text, VALUE_HEADING))
    assert "Nothing here fixes the Cataclysm being harmless" in flat, (
        "the 2026-09-07 entry no longer says what the change does not fix. "
        "Every candidate measured on issue #1383 left the empire far safer "
        "than an untreed one, and the ruling says so in as many words.")


def test_the_entry_names_every_node_on_both_sides_of_the_ruling(log_text):
    """A ruling that says "the unconditional ones" and names none of them cannot
    be acted on by someone who was not in the conversation."""
    flat = flatten(entry(log_text, SHAPE_HEADING))
    for name in RULED_TO_CHANGE + RULED_TO_STAY:
        assert name in flat, (
            f"the decision log entry does not name {name!r}. The ruling splits "
            f"the branch's walk-time nodes into four that change and six that "
            f"do not, so both lists have to be in it.")


def test_every_node_the_entry_names_is_really_in_the_tree(nodes):
    """The entry could name a node that does not exist, which is the mistake
    issue #1288 found in `sim/cataclysm_sim/config.py`'s Architect comment."""
    for name in RULED_TO_CHANGE + RULED_TO_STAY:
        assert name in nodes, (
            f"{name!r} is named in the 2026-09-06 ruling and is not in "
            f"{GRAPH.name}")


# --------------------------------------------------------------------------
# The guard: the tree and the record are still in the state the record claims
# --------------------------------------------------------------------------

def test_the_four_ruled_nodes_now_read_as_a_percentage(nodes):
    """THE GUARD, and it now asserts the state the 2026-09-07 change created.

    Each of the four says a percentage of RUN TIME and says the percentages
    combine MULTIPLICATIVELY. The second half is not decoration: `Temporal
    Mastery` at -2.5% across 25 points is `0.975 ** 25` and not `1 - 25 * 0.025`,
    and the two differ by a factor of three at full investment. The owner ruled
    "combined multiplicatively" and a description that omits it is a description
    a reader will add up.
    """
    for name in RULED_TO_CHANGE:
        description = nodes[name].get("description", "")
        assert "%" in description, (
            f"{name!r} in {GRAPH.name} reads {description!r} and no longer "
            f"states a percentage. The project owner ruled on 2026-09-06 that "
            f"it is a percentage of the dungeon's run time, and the per-point "
            f"value was chosen on 2026-09-07. Four records have to move "
            f"together:\n"
            f"  1. the four node descriptions in {GRAPH.name}\n"
            f"  2. WALK_TIME_NODES in sim/analyse_explorer_shape.py\n"
            f"  3. TREE_EXPLORER_AS_DESIGNED in sim/cataclysm_sim/config.py, "
            f"whose `run_days_mult` is the product of these four\n"
            f"  4. the docs/DECISIONS.md entry headed\n"
            f"     {VALUE_HEADING}")
        assert "run time" in description.lower(), (
            f"{name!r} in {GRAPH.name} reads {description!r}. A percentage of "
            f"WHAT is the whole content of the ruling: of the dungeon's run "
            f"time, not of a day count and not of the floor count.")
        assert "multiplicative" in description.lower(), (
            f"{name!r} in {GRAPH.name} reads {description!r} and does not say "
            f"the percentages combine multiplicatively. 55 points at -2.5% is "
            f"0.975**55 = 0.248 and not 1 - 55*0.025, which is negative.")


def test_the_four_percentages_are_what_the_simulation_models(nodes):
    """The node graph and `TREE_EXPLORER_AS_DESIGNED.run_days_mult` agree.

    **READ OUT OF THE DESCRIPTIONS RATHER THAN RESTATED.** A guard that typed
    0.975 and 0.88 a second time would pass after the design document changed,
    which is the defect issue #1288 found in the Architect branch's modelled
    multiplier. The point counts come from the graph too.
    """
    from cataclysm_sim.config import TREE_EXPLORER_AS_DESIGNED

    product = 1.0
    for name in RULED_TO_CHANGE:
        node = nodes[name]
        found = re.search(r"-\s*([0-9]+(?:\.[0-9]+)?)\s*%", node["description"])
        assert found, (
            f"{name!r} in {GRAPH.name} reads {node['description']!r} and no "
            "percentage could be read out of it.")
        per_point = float(found.group(1)) / 100.0
        points = node.get("maxPoints", 1)
        product *= (1.0 - per_point) ** points

    assert abs(product - TREE_EXPLORER_AS_DESIGNED.run_days_mult) < 1e-12, (
        f"the four nodes in {GRAPH.name} multiply to {product:.6f} and "
        f"TREE_EXPLORER_AS_DESIGNED.run_days_mult is "
        f"{TREE_EXPLORER_AS_DESIGNED.run_days_mult:.6f}. The preset is what "
        "every campaign figure is measured through, so the two cannot differ.")


def test_the_preset_has_no_unconditional_flat_days_left():
    """**THE CONTROL FOR THE TEST ABOVE.**

    A preset that kept its old flat 60 AND gained the multiplier would satisfy
    every percentage check here while making an invested player far faster than
    the ruling intends. The four nodes moved from one term of `run_days_for` to
    another; they were not added to a second one.
    """
    from cataclysm_sim.config import TREE_EXPLORER_AS_DESIGNED

    assert TREE_EXPLORER_AS_DESIGNED.run_days_flat == 0.0, (
        f"TREE_EXPLORER_AS_DESIGNED still removes "
        f"{TREE_EXPLORER_AS_DESIGNED.run_days_flat:g} unconditional flat days. "
        "The four nodes that supplied them are the multiplier now, so the flat "
        "term is Sovereign's Haste alone and that one is per active type.")
    assert TREE_EXPLORER_AS_DESIGNED.run_days_flat_per_type > 0.0, (
        "Sovereign's Haste has gone from the preset. The 2026-09-06 ruling kept "
        "every conditional and situational node as a fixed number of days.")


def test_the_six_situational_nodes_still_read_as_fixed_days(nodes):
    """These six were ruled to stay as they are, so unlike the four above they
    are expected to keep saying days permanently. Converting one of them to a
    percentage would go past what was ruled, and it would take the one-day
    minimum out of reach, which is what makes the One-Day Specialist keystone
    worth anything."""
    for name in RULED_TO_STAY:
        description = nodes[name].get("description", "")
        assert "%" not in description, (
            f"{name!r} in {GRAPH.name} now reads {description!r}. The "
            f"2026-09-06 ruling kept the conditional and situational walk-time "
            f"nodes as a fixed number of days on purpose: they are what puts "
            f"the one-day minimum in reach in a particular situation rather "
            f"than by default. Changing this one needs its own ruling.")


def test_the_analysis_script_still_agrees_with_the_tree_about_these_nodes(nodes):
    """`sim/analyse_explorer_shape.py` carries a table of what each walk-time
    node is worth. It is read out of the source rather than by running the
    script, so a design document change fails here with a name attached instead
    of erroring at import."""
    source = ast.parse(ANALYSIS.read_text(encoding="utf-8"))
    table = None
    for statement in source.body:
        targets = getattr(statement, "targets", [])
        if targets and getattr(targets[0], "id", None) == "WALK_TIME_NODES":
            table = ast.literal_eval(statement.value)
            break
    assert table is not None, (
        "sim/analyse_explorer_shape.py has no WALK_TIME_NODES table. The "
        "2026-09-06 ruling names ten nodes and that table is where the "
        "simulation side records what each is worth.")

    listed = {row[0]: row for row in table}
    for name in RULED_TO_CHANGE + RULED_TO_STAY:
        assert name in listed, (
            f"{name!r} is named in the 2026-09-06 ruling and is missing from "
            f"WALK_TIME_NODES in sim/analyse_explorer_shape.py")
        assert listed[name][2] == nodes[name].get("maxPoints", 1), (
            f"{name!r} has {nodes[name].get('maxPoints', 1)} points in "
            f"{GRAPH.name} and {listed[name][2]} in "
            f"sim/analyse_explorer_shape.py")

    for name in RULED_TO_CHANGE:
        assert listed[name][3] is None, (
            f"WALK_TIME_NODES still gives {name!r} a flat-days figure of "
            f"{listed[name][3]}. It has removed a percentage of run time and "
            f"not a number of days since 2026-09-07.")
        assert listed[name][4] is not None, (
            f"WALK_TIME_NODES gives {name!r} no percentage. That column is "
            f"what the branch's multiplier is built from, and "
            f"`test_the_four_percentages_are_what_the_simulation_models` "
            f"checks it against the preset.")

    for name in RULED_TO_STAY:
        assert listed[name][4] is None, (
            f"WALK_TIME_NODES gives {name!r} a percentage. The 2026-09-06 "
            f"ruling kept every conditional and situational walk-time node as "
            f"a fixed number of days.")

    # **AND THE VALUE, NOT ONLY THAT THERE IS ONE.** Without this the script
    # could say -3% where the design document says -2.5% and every check above
    # would pass. The script asserts the same thing while it runs, but an
    # assertion at import time makes a run error rather than naming a test, so
    # the break cannot be demonstrated with `tools/prove_guard.py`.
    for name in RULED_TO_CHANGE:
        found = re.search(r"-\s*([0-9]+(?:\.[0-9]+)?)\s*%",
                          nodes[name]["description"])
        assert found and float(found.group(1)) / 100.0 == listed[name][4], (
            f"{name!r} removes {nodes[name]['description']!r} in {GRAPH.name} "
            f"and WALK_TIME_NODES in sim/analyse_explorer_shape.py records "
            f"{listed[name][4]}. That table is what the script prints the "
            "branch's multiplier from.")


# --------------------------------------------------------------------------
# Issue #1390: Speed Runner carries a cap
# --------------------------------------------------------------------------

#: The three Explorer nodes that pay Loot Quantity for days removed from a
#: dungeon's run time, and whether each states a ceiling. **THE SHAPE OF THE
#: WALK-TIME REDUCTION DECIDES WHAT ALL THREE ARE WORTH**, which is why issue
#: #1390 landed in the same change as #1383 rather than separately.
LOOT_FOR_DAYS_REMOVED = (
    ("Speed Runner", "cap 100%"),
    ("Efficiency Premium", "cap 50%"),
)


@pytest.mark.parametrize("name,cap", LOOT_FOR_DAYS_REMOVED)
def test_the_nodes_that_pay_for_removed_days_state_a_ceiling(nodes, name, cap):
    """**THE WHOLE OF ISSUE #1390.**

    `Efficiency Premium` has always said "cap 50%". `Speed Runner` said nothing,
    and it pays for "days under default run time", which the branch's own depth
    nodes raise without limit -- so it granted **+1600%** Loot Quantity on one
    40-floor dungeon at difficulty tier 1, sixteen times the largest single Loot
    Quantity effect anywhere else in the tree. It gained "(cap 100%)" on
    2026-09-07.

    The cap is a judgement rather than a measurement: `EmpireTree` has no loot
    field at all, so no campaign batch in this repository can measure it.
    `docs/DECISIONS.md` records what it was sized against and why.
    """
    assert name in nodes, f"{name!r} is no longer a node in {GRAPH.name}"
    description = nodes[name].get("description", "")
    assert cap in description, (
        f"{name!r} in {GRAPH.name} reads {description!r} and no longer states "
        f"{cap!r}. Both nodes pay Loot Quantity for days removed from a "
        f"dungeon's run time and that quantity has no natural ceiling: it "
        f"grows with the dungeon's depth, and the Explorer branch's own depth "
        f"nodes add +40 floors at difficulty tier 1 and +180 at tier 8. "
        f"Issue #1390.")


def test_the_cap_is_not_larger_than_the_biggest_loot_quantity_node(nodes):
    """**THE CONTROL FOR THE TEST ABOVE.** A cap of any size satisfies it, and a
    cap of 10,000% would be no cap at all.

    Sized against the two largest Loot Quantity effects in the design document:
    `Bounty`, 15 points at +5% each, and `The Hoarder`, a Tier 1 capstone option
    worth +100% on its own.
    """
    ceiling = 1.00
    for name, points, per_point in (("Bounty", 15, 0.05), ("The Hoarder", 1, 1.00)):
        assert name in nodes, f"{name!r} is no longer in {GRAPH.name}"
        assert nodes[name].get("maxPoints", 1) == points, (
            f"{name!r} has {nodes[name].get('maxPoints', 1)} points in "
            f"{GRAPH.name}, not {points}. Speed Runner's cap was sized against "
            "what it is worth.")
        ceiling = max(ceiling, points * per_point)

    found = re.search(r"cap\s+([0-9]+)\s*%", nodes["Speed Runner"]["description"])
    assert found, f"Speed Runner states no cap: {nodes['Speed Runner']!r}"
    assert float(found.group(1)) / 100.0 <= ceiling, (
        f"Speed Runner caps at {found.group(1)}% and the largest Loot Quantity "
        f"effect it was sized against is {ceiling:.0%}. A ten-point basic node "
        "worth more than every other source in the tree is what issue #1390 "
        "was opened about.")
