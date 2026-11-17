"""Whether the empire upgrade tree can be respecced, and who owns its four
tier-milestone capstone choices.

WHY THIS EXISTS. Issue #288. `docs/Cataclysm_GDD_v2.md`, the main design
document, listed exactly one respec service — "Trainer | Respec passive skill
points" — and everywhere else in that document "passive skill point" means the
class trees, because the progression section says "Per level: 1 passive skill
point". The empire tree is spent with empire upgrade points, a different currency
with a different name. So nothing said the empire tree could be respecced.

The second half of the same issue. `docs/Empire_Development_Tree_Final.json`, the
empire tree node graph, holds four decision capstones at 25, 50, 100 and 200
points, each one choice from three. The tree belongs to the lethality mode, so the
second character in a mode inherits all four choices already made and never faces
the decisions. Whether that was intended was unstated.

WHY THEY ARE ONE ISSUE. Respec is the mechanism that would let a player revisit an
inherited capstone. Without respec, the first character in a mode fixes four
decisions permanently for every character after it.

WHAT WAS DECIDED, 2026-08-05, by the project owner on issue #288: "Your
recommendation is correct." The tree can be respecced at the Trainer for a cost
in days, the number of days is left as a tuning value, and the four capstone
choices belong to the tree and therefore to the lethality mode.

WHAT IS ASSERTED HERE.

    the Capital Services table says the Trainer respecs the empire tree as well
      as class passive skill points, and that both cost days
    the Empire-Wide Upgrades section states the respec rule and says a respec is
      not a route between lethality modes
    it states that the four capstone choices are inherited already made, that
      this is intended, and why
    the four capstones the design document names match the node graph, which is
      authoritative for the tree
    the decision log records both answers, the owner's words, and the case
      against
"""

from __future__ import annotations

import json
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

#: The node graph. `docs/Empire_Skill_Tree_Keystones.md` is prose commentary and
#: is older; where the two disagree the graph is right. Issue #25.
TREE = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"

OWNERSHIP_SECTION = "## **Empire-Wide Upgrades**"
SERVICES_SECTION = "## **Capital Services**"

DECISION_HEADING = ("## 2026-08-05 — The empire tree can be respecced in days, "
                    "and its four capstone choices are inherited")


def unwrapped(text: str) -> str:
    """One long line, so a matched sentence survives being re-wrapped."""
    return " ".join(text.split())


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


def section_of(document: str, heading: str) -> str:
    """One `## ` section, from its heading to the next heading of any level."""
    start = document.find(heading)
    assert start != -1, f"the design document has no {heading} section"
    after = start + len(heading)
    ends = [document.find(marker, after) for marker in ("\n## ", "\n# ")]
    ends = [e for e in ends if e != -1]
    return document[start:min(ends)] if ends else document[start:]


@pytest.fixture(scope="module")
def ownership(document: str) -> str:
    return unwrapped(section_of(document, OWNERSHIP_SECTION))


@pytest.fixture(scope="module")
def services(document: str) -> str:
    return unwrapped(section_of(document, SERVICES_SECTION))


@pytest.fixture(scope="module")
def decision_entry() -> str:
    """This decision's entry alone, not the whole 5,000-line log.

    The same trap `test_empire_tree_ownership.py` documents: a name asserted
    against the whole file passes because some unrelated entry mentions it.
    """
    assert DECISIONS.is_file(), "docs/DECISIONS.md is missing"
    text = DECISIONS.read_text(encoding="utf-8")
    start = text.find(DECISION_HEADING)
    assert start != -1, (
        f"docs/DECISIONS.md has no entry headed {DECISION_HEADING!r}. That entry "
        f"holds the owner's answer on issue #288 and the reasoning for it.")
    end = text.find("\n---", start)
    return unwrapped(text[start:end if end != -1 else len(text)])


@pytest.fixture(scope="module")
def capstones() -> list[dict]:
    """The four decision capstones, from the node graph, in threshold order."""
    assert TREE.is_file(), f"{TREE.name} is missing"
    graph = json.loads(TREE.read_text(encoding="utf-8"))
    found = [node["data"] for node in graph["nodes"]
             if node["data"].get("kind") == "capstone"
             and "threshold" in node["data"]]
    return sorted(found, key=lambda data: data["threshold"])


# --------------------------------------------------------------------------
# Respec
# --------------------------------------------------------------------------

def test_the_trainer_row_no_longer_says_only_passive_skill_points(services):
    """WHAT THIS USED TO BE. The row read "Trainer | Respec passive skill
    points", which in this document means the class trees and nothing else.

    That is the whole reason issue #288 existed: a reader could not tell whether
    the one respec service covered the empire tree, and the natural reading was
    that it did not.
    """
    assert "Respec passive skill points |" not in services, (
        "the Capital Services table is back to 'Respec passive skill points', "
        "which names the class trees only. The empire tree is spent with empire "
        "upgrade points, a different currency. Issue #288.")
    assert "Respec class passive skill points" in services, (
        "the Trainer row no longer distinguishes class passive skill points "
        "from empire upgrade points. Issue #288.")


def test_the_trainer_respecs_the_empire_tree(services):
    assert "respec the empire upgrade tree" in services, (
        "the Capital Services table does not say the empire upgrade tree can be "
        "respecced. Issue #288 settled that it can, at the Trainer.")


def test_the_respec_is_priced_in_days_in_the_services_table(services):
    """Where a reader looking for the service will actually land. The section
    already says every service costs time; the row says it too, because a reader
    scanning the table may not read the sentence above it."""
    assert "Both cost days" in services, (
        "the Trainer row does not say a respec is paid for in days. Every "
        "capital service is priced in time rather than gold, and that is what "
        "keeps the choice real: a day at the capital is a day not defending the "
        "empire. Issue #288.")


def test_the_ownership_section_states_the_respec_rule(ownership):
    """The services table says the service exists. The section that describes the
    tree has to say the tree can be respecced, because that is where someone
    reading about the tree will look."""
    assert "The tree can be respecced, at a cost in days" in ownership, (
        "the Empire-Wide Upgrades section does not say the tree can be "
        "respecced. Issue #288.")


def test_the_day_cost_is_left_as_a_tuning_value_rather_than_invented(ownership):
    """The owner approved respec priced in days, not a number of days. A number
    written here would read as decided when nothing decided it, and the decision
    is compatible with 1 day and with 60."""
    assert "How many days is a tuning value and is not fixed here" in ownership, (
        "the section either invented a day cost or dropped the statement that "
        "the cost is unfixed. A respec costing 1 day is effectively free and "
        "one costing 60 is effectively permanent, and issue #288 decided "
        "neither. Issue #288.")


def test_a_respec_is_not_a_route_between_lethality_modes(ownership):
    """The one thing a respec must NOT be allowed to become. Issue #277 scoped
    the points to the lethality mode that earned them, precisely so a player
    cannot farm on Standard and spend into Heretic. A respec moves an allocation
    inside one tree and does not touch that."""
    assert "not a route between trees" in ownership, (
        "the respec rule does not say a respec stays inside one tree. Issue "
        "#277 scoped empire upgrade points to the lethality mode that earned "
        "them, and a respec that could move points between trees would defeat "
        "it. Issues #277 and #288.")


# --------------------------------------------------------------------------
# The four capstone choices
# --------------------------------------------------------------------------

def test_it_says_the_capstone_choices_belong_to_the_tree(ownership):
    assert ("The four tier-milestone capstones belong to the tree, not to the "
            "character") in ownership, (
        "the Empire-Wide Upgrades section does not say who owns the four "
        "capstone choices. They are allocations in a tree that belongs to the "
        "lethality mode. Issue #288.")


def test_it_says_the_second_character_inherits_them_already_made(ownership):
    """The consequence, stated rather than left to be worked out. It is the part
    a player would otherwise discover by making a second character and finding
    four decisions missing."""
    assert ("the second character in a mode inherits all four choices already "
            "made") in ownership, (
        "the section does not state the consequence of the capstones belonging "
        "to the tree: a second character in the same lethality mode never faces "
        "the four decisions. Issue #288.")


def test_it_says_the_inheritance_is_intended(ownership):
    """Issue #288 asked whether this was intended or an accident of the
    ownership rule. An unanswered question that looks like a bug gets 'fixed' by
    the next reader."""
    assert "intended rather than an oversight" in ownership, (
        "the section states that the capstone choices are inherited without "
        "saying it is intended. Issue #288 asked exactly that, and a rule that "
        "looks like an oversight gets removed by whoever reads it next.")


def test_the_respec_is_named_as_what_stops_it_being_permanent(ownership):
    """The two halves of issue #288 hold each other up. Inheriting four fixed
    choices is only acceptable because they can be changed; a respec is only
    worth having because something is inherited. If one is ever removed the
    other has to be reconsidered, so the document says they are connected."""
    assert "Respec is what stops it being permanent" in ownership, (
        "the section states the inheritance and the respec rule without saying "
        "the second is what makes the first acceptable. Remove respec and the "
        "first character in a lethality mode fixes four decisions for every "
        "character after it. Issue #288.")


def test_the_document_names_the_four_capstones_the_node_graph_holds(
        ownership, capstones):
    """The design document now names all four and their thresholds, so it can go
    stale against `Empire_Development_Tree_Final.json`, which is authoritative
    for the tree. Issue #25. This is the check that notices."""
    assert len(capstones) == 4, (
        f"{TREE.name} holds {len(capstones)} decision capstones, not 4. The "
        f"Empire-Wide Upgrades section names four with their thresholds and is "
        f"now wrong. Issues #25 and #288.")
    for data in capstones:
        assert data["name"] in ownership, (
            f"the design document does not name the capstone {data['name']!r}, "
            f"which is in {TREE.name}. The two have drifted. Issue #25.")
        assert str(data["threshold"]) in ownership, (
            f"the design document does not state the threshold "
            f"{data['threshold']} for {data['name']!r}. Issue #25.")


def test_each_capstone_is_one_choice_from_three(ownership, capstones):
    """The document says "each is one choice from three". If the graph ever
    holds a capstone with two options or four, that sentence is false."""
    for data in capstones:
        assert len(data["options"]) == 3, (
            f"the capstone {data['name']!r} in {TREE.name} offers "
            f"{len(data['options'])} options, not 3. The design document says "
            f"each is one choice from three. Issues #25 and #288.")
    assert "each is one choice from three" in ownership


# --------------------------------------------------------------------------
# The decision log
# --------------------------------------------------------------------------

def test_the_decision_log_quotes_the_owner(decision_entry):
    assert "Your recommendation is correct" in decision_entry, (
        "the docs/DECISIONS.md entry does not quote the owner's answer on issue "
        "#288. The answer approved a recommendation rather than restating it, "
        "so the entry has to carry both.")


def test_the_decision_log_says_why_days_rather_than_gold_or_free(decision_entry):
    assert "All services cost time" in decision_entry, (
        "the entry does not say why a respec is priced in days. It is priced "
        "that way because the design document already prices every capital "
        "service in time, so it needs no new currency. Issue #288.")
    assert "would make the capstones not decisions" in decision_entry, (
        "the entry does not record why a free respec was rejected. The node "
        "graph calls these decision capstones, and a free respec makes them "
        "not decisions. Issue #288.")


def test_the_decision_log_records_what_argues_against_it(decision_entry):
    """Three things do, and the second is the one most likely to bite: the
    decision is compatible with a day cost that makes respec free and with one
    that makes it permanent."""
    assert "What argues against it" in decision_entry, (
        "the docs/DECISIONS.md entry for issue #288 records no case against.")
    assert "only half-priced" in decision_entry, (
        "the entry does not record that leaving the day cost unspecified means "
        "the decision does not yet determine how permanent a capstone choice "
        "is. Issue #288.")


def test_the_decision_log_reports_both_questions_as_answered(decision_entry):
    """Issue #288 was two questions. An entry that answered one and let the
    other pass would leave the document half-updated with nothing saying so."""
    for phrase in ("can be respecced", "inherited"):
        assert phrase in decision_entry, (
            f"the docs/DECISIONS.md entry does not cover {phrase!r}. Issue #288 "
            f"asked two questions and both were answered together.")


def test_the_open_questions_table_no_longer_lists_288_as_open():
    """The entry for issue #277 lists five questions it deliberately did not
    settle. Two of them are now answered, and a reader arriving at that table
    would otherwise be told they are still open."""
    text = unwrapped(DECISIONS.read_text(encoding="utf-8"))
    for issue in ("#286", "#288"):
        marker = f"| {issue} |"
        start = text.find(marker)
        assert start != -1, (
            f"the open-questions table in docs/DECISIONS.md no longer has a row "
            f"for {issue}. It records what the lethality-mode scope left open "
            f"and should be annotated rather than deleted.")
        row = text[start:text.find("|", text.find("|", start + len(marker)) + 1)]
        assert "Answered 2026-08-05" in row, (
            f"the open-questions table still lists {issue} as unanswered. It "
            f"was answered on 2026-08-05 and has its own entry near the top of "
            f"docs/DECISIONS.md.")
