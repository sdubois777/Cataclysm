"""A class stat line's zero is a starting value, and gear can still supply it.

WHY THIS EXISTS. Issue #345. `docs/Masochist_Class_Tree_Final.json` has a node,
"Rupture Focus", that pays out when the character's energy shield breaks. The
Masochist has 0 base Maximum Energy Shield and the design document says that is
deliberate: "a shield absorbs the damage the class needs to convert." So the node
looked as though it could never fire, and the issue asked whether a class with a
zero in its stat line can gain that stat from gear at all.

IT WAS ALREADY ANSWERED, three sections further down the same document, under
Stat Calculation: "Health, mana and energy shield come from three places: the
class's base value, its per-level scaling, and flat values from gear." Yes, gear
can. Nothing needed deciding.

**The defect was the distance between the two.** A reader at the stat table saw a
0 with nothing saying whether it was a floor or a wall, and the sentence that
answers it is not near enough to find by accident. The rule is now stated at the
table as well, and this file holds the two together.

WHAT IS ASSERTED HERE.

    the stat table still shows the zeros this rule is about, so the rule is not
      being held for a table that no longer has any
    the class stat data agrees with the table: only the Ritualist has an energy
      shield stat line at all
    the stat table says plainly that a zero is a starting value and not a gate
    it says gear supplies it, and names a source that really exists in the
      generated item data
    it says what the zero does cost, which is that nothing compounds
    the Stat Calculation rule it depends on is still there
    the tree and the document agree about the node that prompted the question

THE NODE THAT PROMPTED IT IS GONE, SINCE 2026-08-25. The project owner replaced
the whole branch Rupture Focus sat in, so a Masochist wearing an energy shield
now gets nothing from the tree for it. The answer above is unaffected -- a zero
is still a starting value and gear can still supply the stat -- and the last
assertion now holds the tree and the document to the same story rather than
requiring the node to exist. `docs/DECISIONS.md`, 2026-08-25, has the reasoning.

WHAT IS NOT ASSERTED. Whether a Masochist energy shield build is any good. It
buys resource generation away for nothing at all now, which is a real cost the
document records, and whether that matters needs play rather than a test.
"""

from __future__ import annotations

import csv
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
CLASS_STATS = REPO_ROOT / "game" / "Data" / "ClassStats.csv"
ITEM_BASES = REPO_ROOT / "game" / "Data" / "ItemBases.csv"
MASOCHIST_TREE = REPO_ROOT / "docs" / "Masochist_Class_Tree_Final.json"

#: The section holding the three class stat lines.
STAT_SECTION = "### **The Three Demonic Class Stat Lines**"

#: The stat the question was asked about, as the generated data spells it.
STAT = "max_energy_shield"

#: The one class that has it as a base value.
CLASS_WITH_A_SHIELD = "Ritualist"


def unwrapped(text: str) -> str:
    """One long line, so a matched sentence survives being re-wrapped."""
    return " ".join(text.split())


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return GDD.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def stat_lines(document: str) -> str:
    """The stat table section alone, so a sentence elsewhere cannot satisfy it.

    That is the whole point of this issue: the answer already existed three
    sections away and was not findable from the table. A check run over the
    whole document would have passed before the change and proved nothing.
    """
    start = document.find(STAT_SECTION)
    assert start != -1, (
        f"the design document has no {STAT_SECTION} section. It holds the three "
        f"class stat lines the vertical slice needs. Issue #345.")
    after = start + len(STAT_SECTION)
    ends = [document.find(marker, after) for marker in ("\n### ", "\n## ", "\n# ")]
    ends = [e for e in ends if e != -1]
    return unwrapped(document[start:min(ends)] if ends else document[start:])


# --------------------------------------------------------------------------
# The table still has the zeros this rule is about
# --------------------------------------------------------------------------

def test_the_table_still_shows_a_class_with_no_energy_shield(stat_lines) -> None:
    """A rule about zeros is worth nothing if the table has stopped having any.
    This is the guard that stops every check below being vacuous."""
    assert "| Maximum Energy Shield | 0 | 832 | 0 |" in stat_lines, (
        "the class stat table no longer shows the Ravager and Masochist with 0 "
        "Maximum Energy Shield and the Ritualist with 832. If the values "
        "changed, update this file; if the row is gone, the rule below is "
        "being held for a table that no longer needs it. Issue #345.")


def test_the_generated_class_data_agrees_with_the_table(stat_lines) -> None:
    """The table is prose and `game/Data/ClassStats.csv` is what the game loads.
    A zero in one and a value in the other would make this whole question
    meaningless in the direction nobody would check."""
    del stat_lines  # read for its fixture's assertion that the section exists
    if not CLASS_STATS.is_file():
        pytest.skip("game/Data/ClassStats.csv has not been generated")

    with CLASS_STATS.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))

    with_shield = sorted(r["ClassName"] for r in rows if r["Stat"] == STAT)
    assert with_shield == [CLASS_WITH_A_SHIELD], (
        f"game/Data/ClassStats.csv gives a {STAT} base to {with_shield}, and "
        f"the design document's table gives one to {CLASS_WITH_A_SHIELD} alone. "
        f"A class with no row has a base of 0 by absence. Issue #345.")


# --------------------------------------------------------------------------
# The answer is stated where the question is asked
# --------------------------------------------------------------------------

def test_the_table_says_a_zero_is_not_a_gate(stat_lines) -> None:
    """The sentence issue #345 existed for. Before 2026-08-14 a reader at this
    table had nothing telling them whether a 0 was a floor or a wall."""
    assert "A zero in this table is a starting value, not a gate" in stat_lines, (
        "the class stat lines section does not say whether a 0 in the table is "
        "a floor or a wall. It is a floor: gear supplies the stat regardless. "
        "The rule was three sections away under Stat Calculation and was not "
        "findable from here. Issue #345.")


def test_it_names_a_gear_source_that_actually_exists(stat_lines) -> None:
    """Naming the rule is half of it. A reader wants to know what to equip, and
    a named source is checkable, which an abstract claim is not."""
    assert "Vestment body armour grants 120 maximum energy shield" in stat_lines, (
        "the section says gear can supply a stat with a 0 base without naming "
        "anything that does. Issue #345.")

    if not ITEM_BASES.is_file():
        pytest.skip("game/Data/ItemBases.csv has not been generated")
    bases = ITEM_BASES.read_text(encoding="utf-8")
    assert "Vestment" in bases, (
        "the design document names the Vestment as the body armour that grants "
        "energy shield, and game/Data/ItemBases.csv has no such base. One of "
        "the two is wrong. Issue #345.")


def test_it_says_what_the_zero_does_cost(stat_lines) -> None:
    """Half an answer is worse than none here. "Gear can supply it" read alone
    says a Masochist and a Ritualist are equivalent with the same gear, and they
    are not: nothing compounds from a base of nothing."""
    assert "the class gets no help scaling it" in stat_lines, (
        "the section says a 0 base can be filled by gear without saying what "
        "the 0 still costs. The per-level scaling is zero, there is no matching "
        "regeneration, and 'increased' sources multiply only what gear "
        "supplied. Issue #345.")


def test_it_says_why_the_masochist_case_is_worse_than_the_general_one(
        stat_lines) -> None:
    """The class-specific half, and the one a reader would otherwise have to
    derive from how the Masochist fills Fervour, which is from health lost. A
    shield absorbs damage before health does."""
    assert "every point of energy shield is a point of resource the class does not generate" in stat_lines, (
        "the section does not say that an energy shield costs a Masochist its "
        "resource generation. It fills Fervour from health lost and a shield "
        "absorbs damage before health does. Without this the trade looks free. "
        "Issue #345.")
    assert "Rupture Focus" in stat_lines, (
        "the section describes the trade without naming the one passive node "
        "that buys something with it. Issue #345.")


# --------------------------------------------------------------------------
# What it rests on, and what prompted it
# --------------------------------------------------------------------------

def test_the_rule_it_depends_on_is_still_in_the_document(document) -> None:
    """The sentence above is a restatement. If the Stat Calculation rule were
    reworded or removed, the restatement would be the only place the rule
    existed and nothing would say it had become load-bearing."""
    assert ("Health, mana and energy shield come from three places: the class's "
            "base value, its per-level scaling, and flat values from gear"
            in unwrapped(document)), (
        "the Stat Calculation section no longer says where health, mana and "
        "energy shield come from. That sentence is what makes a 0 base a "
        "starting value rather than a gate, and the class stat table now "
        "restates it. Issue #345.")


def test_the_document_and_the_tree_agree_about_the_node_that_prompted_it(
        document) -> None:
    """The node this whole file was written about no longer exists.

    WHAT THIS USED TO ASSERT, and why it changed. Until 2026-08-25 it required
    Rupture Focus to still be in the Masochist tree, so that the design
    document's account of why a Masochist would take an energy shield was not
    being held for a node that had gone. It fired the day the node went, which
    is what it was for.

    The project owner replaced the whole branch it sat in, because that branch
    grew a mana pool on a class whose 25-point capstone option deletes one. The
    node went with it and nothing took its place. See `docs/DECISIONS.md`,
    2026-08-25.

    SO THE GUARD IS THE OTHER WAY ROUND NOW, and it is still worth having: the
    tree and the document have to agree that the node is gone. Restoring the
    node without restoring the document's account of it, or the reverse, is the
    thing that would otherwise pass unnoticed.

    EVERYTHING ELSE IN THIS FILE IS UNCHANGED. A zero in a class stat line is
    still a starting value rather than a gate, gear can still supply the stat,
    and the table still says so. Only the example is gone.
    """
    if not MASOCHIST_TREE.is_file():
        pytest.skip("the Masochist class tree is not present")
    tree = MASOCHIST_TREE.read_text(encoding="utf-8")
    text = unwrapped(document)

    in_tree = "Rupture Focus" in tree
    claimed_live = "its passive tree has one node that buys something specific" in text

    assert not in_tree or claimed_live, (
        "docs/Masochist_Class_Tree_Final.json has a Rupture Focus node again, "
        "but docs/Cataclysm_GDD_v2.md still records it as removed. If the node "
        "is back, the document's account of what a Masochist gets for wearing "
        "an energy shield has to come back with it. Issue #345.")
    assert in_tree or not claimed_live, (
        "docs/Cataclysm_GDD_v2.md says the Masochist's passive tree has a node "
        "that pays out for wearing an energy shield, and the tree has no "
        "Rupture Focus node. One of the two is stale. Issue #345.")
