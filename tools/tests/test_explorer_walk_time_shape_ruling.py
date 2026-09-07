"""The 2026-09-06 rulings on the Explorer branch's walk time are recorded, and
the design document still matches what the record says about it.

WHY THIS EXISTS. Issue #1383. The project owner ruled two things on 2026-09-06:
that the Explorer branch's four unconditional walk-time nodes change from "-1 day
per point" to a percentage of the dungeon's run time, and that the 2026-09-05
sentence about "a 50 floor dungeon that only takes you a couple days" describes
the empire tree plus city upgrades plus the situational nodes together rather
than the tree alone. `CLAUDE.md`: a design decision is not real until it is in
`docs/`, so both are written into `docs/DECISIONS.md`.

**THE RULING AND THE DESIGN DOCUMENT DO NOT AGREE RIGHT NOW, ON PURPOSE.** The
owner ruled the shape and not the per-point percentages, and writing a percentage
into `docs/Empire_Development_Tree_Final.json` needs a percentage. So the four
nodes there still read "-1 day per point" and "-5 days", and the decision log
entry says so and says what has to change together when a value is ruled.

THAT IS WHAT MAKES THIS FILE WORTH HAVING. Two records that disagree by
arrangement will drift apart the moment somebody edits one of them, and the
disagreement is invisible: a node description reading "-3% per point" looks
perfectly correct on its own. `test_the_four_ruled_nodes_still_read_as_fixed_days`
fails the moment the tree is changed, and its message names the other two things
that have to change in the same commit.

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

def test_the_four_ruled_nodes_still_read_as_fixed_days(nodes):
    """THE GUARD, and it asserts the state the decision log says exists rather
    than the state the ruling asks for. That is deliberate.

    The owner ruled the shape and not the per-point percentages, so the tree has
    not been changed and the log entry says so. When somebody does change it,
    this test fails and its message is the checklist of what else has to move in
    the same commit. Without it the three records drift silently, because a node
    description reading "-3% per point" looks correct on its own.
    """
    for name in RULED_TO_CHANGE:
        description = nodes[name].get("description", "")
        assert "%" not in description, (
            f"{name!r} in {GRAPH.name} now reads {description!r}.\n"
            f"\n"
            f"That is the shape the project owner ruled on 2026-09-06, so this "
            f"is expected work rather than a mistake -- but three records have "
            f"to change together and this test exists because they will not "
            f"otherwise:\n"
            f"  1. the four node descriptions in {GRAPH.name} (this one, plus "
            f"{', '.join(n for n in RULED_TO_CHANGE if n != name)})\n"
            f"  2. WALK_TIME_NODES in sim/analyse_explorer_shape.py, which "
            f"records a flat-days figure for each of them and would then "
            f"describe a design that no longer exists\n"
            f"  3. the docs/DECISIONS.md entry headed\n"
            f"     {SHAPE_HEADING}\n"
            f"     whose closing section says the tree still reads in days, "
            f"and what value was chosen\n"
            f"\n"
            f"Then delete this test. It has done its job.")
        assert "day" in description.lower(), (
            f"{name!r} in {GRAPH.name} no longer mentions days and does not "
            f"mention a percentage either: {description!r}. The 2026-09-06 "
            f"ruling covers this node, so whatever it now does needs recording "
            f"in docs/DECISIONS.md.")


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
        assert listed[name][3] is not None, (
            f"WALK_TIME_NODES gives {name!r} no flat-days figure. While "
            f"{GRAPH.name} still describes it in days that figure is what the "
            f"branch's total is built from.")
