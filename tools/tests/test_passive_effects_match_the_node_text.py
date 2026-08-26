"""Every authored passive effect matches the words of the node it is about.

WHY THIS EXISTS. Issue #936. A passive tree node says what it does in a sentence
written for a player -- "+2% increased Life Leech per point" -- and carries no
stat name and no number anywhere a machine can read. The project owner chose on
2026-08-25 to author those numbers in the design workbook, in a `Passive Effects`
sheet, which `tools/generate_datatables.py` turns into
`game/Data/PassiveEffects.csv`.

**That is two statements of one fact in two files, and the second can be edited
without the first.** A number changed in the workbook and not in the tree, or a
node renamed in the tree and not in the workbook, gives a game that grants
something the design never said. Neither errors.

AN AUTOMATED TRANSCRIPTION WAS TRIED FIRST AND WAS WRONG. Deriving the sheet from
the descriptions with a parser produced 44 rows of which about fifteen were
wrong: eight melee-only bonuses were written as global increases, a node about
damage over time DURATION was mapped to damage over time DAMAGE, and a node about
how much Fervour a character gains was mapped to a damage stat because the words
"damage over time" appear in it. So the rows were selected by reading instead,
and this is what keeps them honest afterwards.

WHAT IS ASSERTED HERE.

    every effect names a node that exists
    every effect's value appears, as a number, in that node's own description
    an effect in the `more` bucket is on a node whose description says it
      multiplies, in either of the two wordings the trees use
    every stat named is a stat some class line or attribute supplies
    every required tag is one the workbook declares
    no node grants the same stat twice, though it may grant two different ones
    every row name is its node and the index of that node's effect
    the coverage is what it is measured to be, so it can only move deliberately

WHAT THIS CANNOT CHECK, and it is worth being plain about: whether the stat
chosen is the RIGHT stat. Nothing automatic can read "+2% increased Life Leech"
and know that `life_leech` rather than `health_regen` is meant. That is why the
sheet is written by hand and reviewed. This catches the mistakes a hand-written
table actually makes -- a wrong number, a renamed node, a stat that does not
exist -- rather than a wrong judgement.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
EFFECTS_CSV = DATA / "PassiveEffects.csv"
NODES_CSV = DATA / "PassiveNodes.csv"
CLASS_STATS_CSV = DATA / "ClassStats.csv"
ATTRIBUTES_CSV = DATA / "Attributes.csv"
ITEM_BASES_CSV = DATA / "ItemBases.csv"
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"

# WHICH STATS GEAR SUPPLIES IS THE GENERATOR'S RULE, imported rather than written
# out a second time here. `validate_passive_effects` in that file refuses exactly
# the rows this file refuses, and two spellings of one rule is the drift this
# whole file exists to catch.
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: The three buckets of the damage pipeline. Anything else is a typo.
BUCKETS = {"flat", "increased", "more"}

#: A description saying its number multiplies rather than joining the sum.
#:
#: TWO WORDINGS FOR ONE THING, AND BOTH TREES ARE RIGHT. Issue #977. The Bulwark
#: tree writes "+1.5% damage reduction per point (multiplicative)" on all ten of
#: its multiplying nodes. The Masochist tree writes "You gain 30% more Fervour
#: from health lost to damage, and 50% less Fervour from health spent as an
#: ability cost" and never uses the word "multiplicative" anywhere. Neither tree
#: uses the other's wording.
#:
#: `docs/DECISIONS.md` settles that they mean the same thing, twice. On
#: 2026-08-14: the wording "more" and "less" is "the multipliers that apply
#: separately instead of joining the additive bucket", and that entry names
#: Masochist keystones among the twelve keystones already using it. On
#: 2026-08-17 the project owner ruled that "multiplicative means more".
#:
#: THE SECOND PATTERN NEEDS A NUMBER AND A PERCENT SIGN IN FRONT OF THE WORD, or
#: ordinary English matches: "3 or more enemies", "more than 10 meters" and "5 or
#: more affixes" are not magnitudes. It is the same expression
#: `tools/tests/test_class_passive_trees.py` uses for the same reason, kept
#: separate rather than imported because that file pins counts against the tree
#: JSON while this one reads the generated CSV.
MULTIPLIES = re.compile(r"multiplicative|\d+\s*%\s+(?:more|less)\b",
                        re.IGNORECASE)

#: How many rows the effects file holds altogether.
#:
#: TWO NUMBERS RATHER THAN ONE SINCE ISSUE #953, because a node may now have
#: several rows. This is the row count and `AUTHORED_NODES` below is the count of
#: nodes with at least one, which is the number that measures the tree. They were
#: the same figure while a node could grant only one stat.
#:
#: IT WENT UP FROM 24 ON 2026-08-25, when Fervour was given a way of being
#: generated: ten rows on eight Masochist nodes. Three of the ten are on the
#: tree's starting node, which grants all three Fervour rates at once and could
#: not be written down at all before #953. Issue #954.
#:
#: AND UP AGAIN BY TWO THE SAME DAY, for Pain Tolerance, which grants a maximum
#: health increase and an armour increase in one sentence. That is the other node
#: #953 was opened for and it needed no code at all.
#:
#: AND BY TWO MORE, for the first two nodes whose bonus depends on where the
#: character's health is: Last Stand and Desperate Measures. Issue #959.
#:
#: AND BY TWO MORE AGAIN, both on one node. Living on the Edge reads "While at or
#: below 35% health, +2% increased damage per point", and the project owner
#: settled on 2026-08-25 that "increased damage" on a passive node means every
#: kind of damage the character DEALS -- attack damage and spell damage -- and
#: not damage over time. So it is two rows rather than one. Issue #958.
#:
#: AND BY TWO MORE, again both on one node. Blood Rush reads "+2% increased
#: damage per point for 2 seconds after you pay a health cost", so it is the same
#: two stats, under the first condition that depends on WHEN something happened
#: rather than on what is true now. Issue #962.
#:
#: AND BY ONE, for Vicious Onslaught: "+1% increased Attack Damage per point for
#: every 5% of your maximum health that is missing". One row rather than two
#: because the node names Attack Damage outright. It is the first bonus whose
#: SIZE grows with a state rather than switching on and off with it. Issue #968.
#:
#: AND BY ONE, for Deeper Cuts: "Your skills also cost 1% of your maximum
#: health per point, in addition to any other cost." A `flat` row, because the
#: stat it supplies is zero for every class and this node is its only source.
#: Issue #970.
#:
#: AND BY ONE, for The Catalyst: "While at or below 5% health, your skills
#: have a 5% chance per point not to go on cooldown." A `flat` row carrying
#: the health threshold, because nothing else supplies the stat. Issue #973.
#:
#: AND BY TWO, both on one node. Cataclysmic Resonance reads "+1% increased
#: damage per point for 5 seconds after you take damage of a Cataclysm type
#: other than Demonic", so it is the same two damage stats under the second
#: kind of timed window. Issue #975.
#:
#: AND BY FOUR, two on each of two keystones, and they are the first keystones
#: in the project with an authored effect at all. Flesh Craver and Blood Tithe
#: are mirror images: each grants "30% more" of one of the two ways Fervour is
#: gained and "50% less" of the other. Four `more` rows, which is what those
#: words mean. Issue #978.
#:
#: AND BY ONE, for Reciprocity: "Your Retaliation damage is increased by 1% for
#: each point of Fervour you currently hold." One row, and the second bonus in
#: the project whose SIZE grows with a state rather than switching on and off
#: with it. Issue #980.
#:
#: AND BY TWO, both on one node. Grand Tithe reads "A skill whose health
#: cost is above 10% of your maximum health deals 4% increased damage per
#: point", so it is the same two damage stats under the first condition that
#: asks about the SKILL rather than about the character. Issue #983.
#:
#: AND BY THREE, all on one node. Exsanguinate reads "Every skill costs an
#: additional 15% of your current health, and every skill deals 40% more
#: damage": a `flat` row for the cost, because that stat is zero for every
#: class and this node is its only source, and the two damage stats in the
#: `more` bucket. It is the first node to hold rows in two different
#: buckets. Issue #986.
#:
#: AND BY THREE MORE, all on one node again. Point of No Return reads "You
#: cannot be healed above 50% of your maximum health, but you deal 25% more
#: damage": a `flat` row reducing the ceiling healing may reach, and the two
#: damage stats in the `more` bucket. Issue #988.
#:
#: AND BY ONE, for Deferred Payment: "10% per point of the health a skill
#: costs is not taken when the skill is used. It is taken 3 seconds later."
#: A `flat` row, because the stat it supplies is zero for every class and
#: this node is its only source. Issue #991.
#:
#: AND BY SEVEN, FOR THE THREE NODES THAT FINISH THE BLOOD TITHE BRANCH.
#: Compound Interest is two damage rows whose size grows with what the character
#: owes, issue #994. Rolling Debt is one `flat` row, the seconds one further
#: payment pushes a debt out, issue #995. The Reckoning is four: a `flat` 100
#: deferring the whole cost, the two damage stats in the `more` bucket growing
#: with what is owed, and a `flat` flag for the rules that a debt is never taken
#: on a timer, is cleared by a kill, and kills a character it passes. Issue #997.
#:
#: AND BY THREE, ONE EACH FOR THE THREE NODES THAT NEEDED A STACK COUNT THAT
#: BUILDS AND EXPIRES. Sanguine Momentum grants attack speed per stack, Blood
#: Offering grants melee damage per stack, and Carnage multiplies melee damage
#: per stack. One row each rather than two, because each names a single stat:
#: this game has no cast speed at all (#1000), and melee damage is attack damage
#: rather than both damage stats. Issues #1002, #1003 and #1004.
AUTHORED_ROWS = 71

#: How many of the 293 nodes have an authored effect.
#:
#: PINNED RATHER THAN LEFT AS A FLOOR, and the reason is that this number is the
#: honest measure of how much of the passive tree actually does anything. Issue
#: #939 measures the gap and lists what each remaining group would need. A change
#: to this number should be somebody's decision, not a side effect.
#:
#: IT WENT DOWN FROM 26 TO 24 ON 2026-08-25, AND THAT WAS THE DECISION. Replacing
#: the Masochist's upper-right section removed three authored rows -- maximum
#: mana, mana regeneration and area of effect -- because the nodes holding them
#: are gone. One row arrived, for maximum health. The rest of the new section is
#: mechanics rather than modifiers: a cost taken late, a stack that expires, a
#: debt that has to be settled. None of those can be written as a row here, which
#: is the same finding issue #939 made about the other nodes.
#:
#: THEN UP TO 32 THE SAME DAY, from the eight Masochist nodes issue #954 made
#: work: the tree's starting node, five that increase one of the two ways Fervour
#: is gained, one scoped to damage over time, and one that reduces how much
#: health regeneration takes back out.
#:
#: AND TO 33, for Pain Tolerance, then 35 for Last Stand and Desperate Measures,
#: the first two nodes whose bonus depends on where the character's health is.
#: 35 of 293 altogether, and 31 of the Masochist tree's own 74. Issue #959.
#:
#: AND TO 36 FOR ONE NODE, Living on the Edge, which grants increased damage
#: below a health threshold. It is 36 of 293 altogether and 32 of the Masochist
#: tree's own 74. Issue #958.
#:
#: AND TO 37 FOR ONE MORE, Blood Rush, which grants increased damage for a short
#: window after a health cost is paid. 37 of 293 altogether and 33 of the
#: Masochist tree's own 74. Issue #962.
#:
#: AND TO 38 FOR ONE MORE, Vicious Onslaught, whose bonus grows with how much
#: health is missing. 38 of 293 altogether and 34 of the Masochist tree's own 74.
#: Issue #968.
#:
#: AND TO 39 FOR ONE MORE, Deeper Cuts, which adds a health cost to every
#: skill the character uses. 39 of 293 altogether and 35 of the Masochist
#: tree's own 74. Issue #970.
#:
#: AND TO 40 FOR ONE MORE, The Catalyst, whose skills sometimes do not go on
#: cooldown at all. 40 of 293 altogether and 36 of the Masochist tree's own
#: 74. Issue #973.
#:
#: AND TO 41 FOR ONE MORE, Cataclysmic Resonance. 41 of 293 altogether and 37
#: of the Masochist tree's own 74. Issue #975.
#:
#: AND TO 43 FOR TWO MORE, Flesh Craver and Blood Tithe, which trade one of the
#: two ways Fervour is gained against the other. 43 of 293 altogether and 39 of
#: the Masochist tree's own 74. They are the first keystones with an authored
#: effect: every node here before them was a basic node. Issue #978.
#:
#: AND TO 44 FOR ONE MORE, Reciprocity, whose retaliation damage grows with how
#: much Fervour the character is holding. 44 of 293 altogether and 40 of the
#: Masochist tree's own 74. Issue #980.
#:
#: AND TO 45 FOR ONE MORE, Grand Tithe, whose damage depends on what the
#: skill in hand cost. 45 of 293 altogether and 41 of the Masochist tree's
#: own 74. Issue #983.
#:
#: AND TO 46 FOR ONE MORE, Exsanguinate, which charges every skill a share
#: of current health and pays for it in damage. 46 of 293 altogether and 42
#: of the Masochist tree's own 74. Issue #986.
#:
#: AND TO 47 FOR ONE MORE, Point of No Return, which trades how far healing
#: may take the character for damage. 47 of 293 altogether and 43 of the
#: Masochist tree's own 74. Issue #988.
#:
#: AND TO 48 FOR ONE MORE, Deferred Payment, the first node built on a
#: character being able to owe health. 48 of 293 altogether and 44 of the
#: Masochist tree's own 74. Issue #991.
#:
#: AND TO 51 FOR THREE MORE, which finish the Blood Tithe branch: Compound
#: Interest, whose damage grows with what the character owes; Rolling Debt,
#: which pushes an outstanding debt further out every time another cost is
#: paid; and The Reckoning, whose debt is never taken on a timer, is cleared
#: only by a kill, and kills the character if it passes their health. 51 of
#: 293 altogether and 47 of the Masochist tree's own 74. Issues #994, #995
#: and #997.
#:
#: AND TO 54 FOR THREE MORE, the three nodes built on a count of stacks that
#: builds on an event and stops counting when the character goes long enough
#: without it: Sanguine Momentum, Blood Offering and Carnage. 54 of 293
#: altogether and 50 of the Masochist tree's own 74. Issues #1002, #1003 and
#: #1004.
#:
#: TWO OF THOSE THREE ARE SCOPED TO A TAG ALMOST NO SKILL CARRIES, and this
#: number does not know that. Issue #999: `Type.Melee` is on 6 weapon skill rows
#: of 398, so Blood Offering's and Carnage's bonuses reach six skills until that
#: is answered. Their triggers and their stacks work. This count measures whether
#: a node has an authored effect, which is not the same question as whether the
#: effect reaches much, and that gap is worth knowing about when reading it.
AUTHORED_NODES = 54

#: How many nodes there are altogether, so the share is visible in the failure
#: message rather than needing to be worked out.
TOTAL_NODES = 293


def rows_of(path: pathlib.Path) -> list[dict]:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present. Run "
                    "python tools/generate_datatables.py")
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def effects() -> list[dict]:
    return rows_of(EFFECTS_CSV)


@pytest.fixture(scope="module")
def nodes() -> dict[str, dict]:
    return {row["Name"]: row for row in rows_of(NODES_CSV)}


@pytest.fixture(scope="module")
def stats() -> set[str]:
    # A FLAT ROW IN THE EFFECTS FILE SUPPLIES A STAT TOO, and so does a flat
    # implicit on an item base. See `test_every_stat_is_one_the_game_supplies`
    # below for why, and for why a rolled affix does not.
    return ({row["Stat"] for row in rows_of(CLASS_STATS_CSV)}
            | {row["Stat"] for row in rows_of(ATTRIBUTES_CSV)}
            | {row["Stat"] for row in rows_of(EFFECTS_CSV)
               if row["ValueKind"].strip().lower() == "flat"}
            | gen.item_base_flat_stats(rows_of(ITEM_BASES_CSV))
            # AND A COLUMN ON AN ITEM BASE SUPPLIES ONE TOO, since issue #1002.
            # A swing rate is a column rather than an implicit, because two
            # weapons average theirs; see `item_base_column_stats` in
            # tools/generate_datatables.py for why that is a real base under an
            # increase all the same.
            | gen.item_base_column_stats(rows_of(ITEM_BASES_CSV)))


@pytest.fixture(scope="module")
def declared_tags() -> set[str]:
    openpyxl = pytest.importorskip("openpyxl")
    if not WORKBOOK.is_file():
        pytest.skip(f"{WORKBOOK.name} is not present")

    book = openpyxl.load_workbook(WORKBOOK, read_only=True)
    declared = {str(row[0]).strip()
                for row in book["Tags"].iter_rows(values_only=True)
                if row and row[0]} - {"Tag Name"}

    # THE PARENTS UNREAL CREATES IMPLICITLY COUNT TOO. Declaring `Type.AOE.Aura`
    # gives `Type` and `Type.AOE` as well, so a required tag naming one of those
    # is legitimate. `known_tags` in tools/generate_datatables.py does the same.
    known: set[str] = set()
    for tag in declared:
        parts = tag.split(".")
        for depth in range(1, len(parts) + 1):
            known.add(".".join(parts[:depth]))
    return known


def test_every_effect_is_about_a_node_that_exists(effects, nodes):
    missing = sorted({row["Node"] for row in effects} - set(nodes))
    assert not missing, (
        f"{EFFECTS_CSV.name} grants something to {missing}, and "
        f"{NODES_CSV.name} has no such node. The effect reaches nothing, and "
        "nothing at run time reports it."
    )


def test_every_row_name_is_its_node_and_an_index(effects):
    """The row name is the DataTable's key and nothing reads it.

    IT USED TO BE THE NODE NAME AND THAT IS WHY THIS EXISTS. Issue #953 gave a
    node the right to several rows, which a key cannot express, so the node moved
    into a column of its own and the row name became the node with `#1`, `#2` and
    so on after it. A row whose name no longer agrees with its `Node` column
    would still load and still apply, so nothing would report it; what it would
    break is a person reading the file.
    """
    counts: dict[str, int] = {}
    for row in effects:
        counts[row["Node"]] = counts.get(row["Node"], 0) + 1
        expected = f"{row['Node']}#{counts[row['Node']]}"
        assert row["Name"] == expected, (
            f"a row for {row['Node']} is named {row['Name']!r} and should be "
            f"{expected!r}. Regenerate with python tools/generate_datatables.py."
        )


def test_no_node_grants_the_same_stat_twice(effects):
    """Two rows for one node are for two different stats.

    A NODE MAY HAVE SEVERAL ROWS SINCE ISSUE #953, because several nodes grant
    two things at once: "+1% increased Maximum Health and +0.5% increased Armor",
    and the Masochist's starting node, which grants three Fervour rates. What is
    still a mistake is the SAME stat twice on one node, which is what copying a
    spreadsheet row and forgetting to change the stat produces. Both would apply
    and the node would be worth double what it says.
    """
    seen: dict[tuple[str, str, str, str], int] = {}
    for row in effects:
        # THE CONDITION IS PART OF WHAT MAKES A ROW DISTINCT since issue #959.
        # A node giving one amount always and more of it below a threshold is a
        # real shape, and it is two rows on the same node and the same stat.
        #
        # AND SO IS THE SCALE, since issue #968, for the same reason: a node
        # could give a fixed amount and a further amount that grows with a
        # state. `tools/generate_datatables.py` keys its own check the same way.
        key = (row["Node"], row["Stat"], row["Condition"],
               row["ConditionValue"], row["Scale"], row["ScaleStep"])
        seen[key] = seen.get(key, 0) + 1

    twice = sorted(key for key, count in seen.items() if count > 1)
    assert not twice, (
        f"{twice} appear more than once in {EFFECTS_CSV.name} as a node, stat, "
        "condition and scale. Two rows on one node are for two different stats, "
        "or for the same stat under different conditions or scales; anything "
        "else applies both and the node is worth double what it says."
    )


#: The states a bonus may depend on: the words the node must contain, and how
#: the condition's own value is written in that sentence.
#:
#: `tools/generate_datatables.py` holds the same set of names and refuses one it
#: does not know; this holds what the words have to say.
#:
#: THE VALUE'S UNITS ARE PART OF THE ENTRY, since issue #962. A health threshold
#: is written "20%" and a window is written "2 seconds", so one shared format
#: would have looked for "2%" in a sentence about seconds and failed on a
#: perfectly correct row.
#:
#: "second" WITHOUT THE PLURAL, so that "1 second" and "2 seconds" both match.
CONDITION_WORDS = {
    "health_at_or_below": ("at or below", "{value:g}%"),
    "seconds_after_health_cost": ("after you pay a health cost",
                                  "{value:g} second"),
    "seconds_after_foreign_damage": ("after you take damage of a cataclysm "
                                     "type",
                                     "{value:g} second"),
    # THE WORDS INCLUDE "above", WHICH IS THE POINT. Issue #983. This is the one
    # threshold in the tree written the other way round from every health
    # threshold, and a node reworded to "at or below" while the row kept saying
    # `skill_health_cost_above` would be worth the opposite of what a player
    # reads. Requiring the word here is what notices.
    "skill_health_cost_above": ("health cost is above", "{value:g}%"),
}


def test_a_condition_matches_the_words_of_the_node_it_is_on(effects, nodes):
    """A threshold in the workbook is the threshold the node's own words state.

    THE SAME DRIFT THE VALUE CHECK ABOVE EXISTS FOR, one column across. A row
    reading `health_at_or_below 35` on a node that says "at or below 20% health"
    is a bonus that arrives at the wrong time, and nothing at run time reports
    it: the arithmetic runs and the character simply gets it earlier or later
    than the sentence promises.

    Both halves are checked: that the words the condition implies are in the
    description at all, and that the number is.
    """
    for row in effects:
        condition = row["Condition"].strip()
        if not condition:
            continue

        assert condition in CONDITION_WORDS, (
            f"{row['Node']} names the condition {condition!r}, which this test "
            f"does not know. Known: {sorted(CONDITION_WORDS)}. Add it here and "
            "to CONDITIONS in tools/generate_datatables.py together."
        )

        expected_words, value_form = CONDITION_WORDS[condition]

        words = nodes[row["Node"]]["Description"].lower()
        assert expected_words in words, (
            f"{row['Node']} carries the condition {condition!r} and its "
            f"description does not say {expected_words!r}:\n"
            f"    {nodes[row['Node']]['Description']}"
        )

        value = float(row["ConditionValue"])
        printed = value_form.format(value=value)
        assert printed.lower() in words, (
            f"{row['Node']} carries {condition!r} with a value written "
            f"{printed!r}, and the node says:\n"
            f"    {nodes[row['Node']]['Description']}\n"
            "The two have to agree. Either the workbook is stale or the tree "
            "file changed."
        )


def test_a_condition_is_actually_used(effects):
    """The two condition columns are exercised rather than merely present.

    Without a row that uses one, the columns, the generator's refusal of an
    unknown condition, and the code that turns a condition into a modifier would
    all be untested, and the first node that needed one would find out whether it
    worked the hard way. The same argument as the required tag check below.
    """
    conditioned = [row for row in effects if row["Condition"].strip()]
    assert conditioned, (
        "No passive effect carries a condition, so nothing exercises the "
        "Condition columns or the code that reads them. If the last one was "
        "deliberately removed, delete these tests with it rather than leaving "
        "the columns untested."
    )


#: The states a bonus's SIZE may grow with: the words the node must contain, and
#: how the step is written in that sentence. Issue #968.
#:
#: A SEPARATE MAP FROM `CONDITION_WORDS`, because the two answer different
#: questions and a row may carry both. `tools/generate_datatables.py` holds the
#: same set of names in `SCALES` and refuses one it does not know.
#: A STEP FORM OF `None` MEANS THE SENTENCE NAMES NO STEP, so the step can only
#: be 1 and that is what is asserted instead. Issue #980. Reciprocity says "for
#: each point of Fervour you currently hold": one point is the step, and there is
#: no number in the sentence to match it against. Looking for "1" in that
#: description would find the "1%" of its own value and pass for the wrong
#: reason, which is worse than not checking.
#: THE WORDS ARE A TUPLE AND ALL OF THEM MUST APPEAR, since issue #994 added a
#: second scale about health. "For every 5% of your maximum health that is
#: MISSING" and "for every 5% of your maximum health you currently OWE" are two
#: different states of one character -- a character that deferred a cost owes
#: health it is still standing on -- and they share every word but the last. A
#: single phrase of "for every" would let either row sit on either node and pass,
#: which is the drift this whole file exists to catch.
SCALE_WORDS = {
    "health_missing": (("for every", "missing"), "{value:g}%"),
    "class_resource_held": (("for each point of fervour",), None),
    "health_owed": (("for every", "owe"), "{value:g}%"),

    # THREE STACK COUNTS, EACH NAMING ITS OWN STACK. Issues #1002, #1003 and
    # #1004. The words include the stack's name wherever the node gives it one,
    # which is what stops a row counting somebody else's stacks: the three are
    # granted by different events and last 3, 5 and 8 seconds, and nothing at run
    # time would report a row that named the wrong one.
    #
    # SANGUINE MOMENTUM'S SENTENCE NAMES NO STACK, so its words are the phrase
    # that describes when one is granted instead.
    #
    # A STEP FORM OF `None` FOR ALL THREE, because all three say "each stack" and
    # name no step. The step can only be 1 and that is what is asserted, the same
    # way `class_resource_held` handles "for each point of Fervour".
    "momentum_stacks": (("within 3 seconds of the last", "each stack"), None),
    "bloodlust_stacks": (("stack of bloodlust", "each stack"), None),
    "carnage_stacks": (("stack of carnage", "each stack"), None),
}


def test_a_scale_matches_the_words_of_the_node_it_is_on(effects, nodes):
    """A scaling step in the workbook is the step the node's own words state.

    THE SAME DRIFT THE CONDITION CHECK ABOVE EXISTS FOR, one column further
    across. A row reading `health_missing` with a step of 10 on a node that says
    "for every 5% of your maximum health that is missing" gives a character half
    the bonus the sentence promises, and nothing at run time reports it: the
    arithmetic runs and the number is simply smaller.

    IT ALSO CHECKS THE STAT IS ONE THE GAME ASKS FOR AT THE RIGHT MOMENT is NOT
    something this can check, and that is worth saying. A scaling bonus written
    onto a stat that is read off a gameplay attribute would be dropped, because
    a scaled value is never folded into an attribute. The engine-side tests are
    where that is caught.
    """
    for row in effects:
        scale = row["Scale"].strip()
        if not scale:
            continue

        assert scale in SCALE_WORDS, (
            f"{row['Node']} names the scale {scale!r}, which this test does not "
            f"know. Known: {sorted(SCALE_WORDS)}. Add it here and to SCALES in "
            "tools/generate_datatables.py together."
        )

        expected_words, value_form = SCALE_WORDS[scale]

        words = nodes[row["Node"]]["Description"].lower()
        for phrase in expected_words:
            assert phrase in words, (
                f"{row['Node']} carries the scale {scale!r} and its description "
                f"does not say {phrase!r}:\n"
                f"    {nodes[row['Node']]['Description']}\n"
                f"All of {list(expected_words)} have to be in it."
            )

        step = float(row["ScaleStep"])
        assert step > 0.0, (
            f"{row['Node']} carries the scale {scale!r} with a step of {step:g}. "
            "A step of nothing makes the bonus worth nothing at every state."
        )

        if value_form is None:
            # THE SENTENCE NAMES NO STEP, SO THE STEP CAN ONLY BE ONE. Issue
            # #980. "For each point of Fervour you currently hold" counts single
            # points; a step of 5 on that node would give a fifth of what the
            # sentence promises, and no number in the description could catch it.
            assert step == 1.0, (
                f"{row['Node']} carries the scale {scale!r} with a step of "
                f"{step:g}, and its description names no step:\n"
                f"    {nodes[row['Node']]['Description']}\n"
                "A sentence saying \"for each\" counts single units, so the step "
                "has to be 1. If the design now states a step, give this scale a "
                "step form in SCALE_WORDS instead."
            )
            continue

        printed = value_form.format(value=step)
        assert printed.lower() in words, (
            f"{row['Node']} scales in steps of {printed!r} and the node says:\n"
            f"    {nodes[row['Node']]['Description']}\n"
            "The two have to agree. Either the workbook is stale or the tree "
            "file changed."
        )


def test_a_scale_is_actually_used(effects):
    """The two scale columns are exercised rather than merely present.

    The same argument as the condition check above. Without a row that uses one,
    the columns, the generator's refusal of an unknown scale, and the code that
    turns a scale into a modifier would all be untested.
    """
    scaled = [row for row in effects if row["Scale"].strip()]
    assert scaled, (
        "No passive effect scales with a state, so nothing exercises the Scale "
        "columns or the code that reads them. If the last one was deliberately "
        "removed, delete these tests with it rather than leaving the columns "
        "untested."
    )


#: Stats whose value a node writes in units other than a percentage.
#:
#: EVERY OTHER VALUE IN THE SHEET IS A PERCENTAGE and was until issue #995, so
#: the check appended a percent sign to every number it looked for. Rolling Debt
#: is the first row that is not: "extends the delay on what is owed by 0.5
#: SECONDS per point". Looking for "0.5%" in that sentence fails on a perfectly
#: correct row, which is the same shape `CONDITION_WORDS` already carries units
#: for. Issue #990 is the general form of this and is not closed by it: a value
#: that is a plain count, like Low Life's "10 Fervour per second", still has
#: nowhere to say so.
VALUE_FORMS = {
    "health_debt_delay_extension": "{value:g} second",
}

#: Rows whose value the node states in WORDS instead of digits.
#:
#: THE ONLY WAY TO WRITE "NEVER" AS A NUMBER IS TO WRITE THE NUMBER. The
#: Reckoning reads "Health costs are never taken", which is a deferred share of
#: 100, and "the debt is cleared only by killing an enemy", which is a flag of 1.
#: Neither figure is in the sentence, so no search of the description can find
#: it, and refusing the row would leave the tree's last Blood Tithe keystone
#: unauthorable.
#:
#: THE EXEMPTION STILL CHECKS SOMETHING, AND THAT IS THE POINT OF ITS SHAPE. It
#: names the words that state the value AND the value those words mean, so a
#: workbook changed to defer half a cost, or a node reworded to take costs after
#: all, still fails. What it gives up is only the arithmetic tie between a digit
#: in the sheet and a digit in the sentence, because there is no digit.
#:
#: KEYED BY NODE AND STAT, so it exempts one row rather than a stat everywhere.
VALUE_IN_WORDS = {
    ("Masochist_keystone_bt_kA", "deferred_health_cost_share"):
        ("never taken", 100.0),
    ("Masochist_keystone_bt_kA", "health_debt_cleared_only_by_a_kill"):
        ("cleared only by killing an enemy", 1.0),
}


def test_every_value_appears_in_the_nodes_own_description(effects, nodes):
    """The number in the workbook is the number the design document states.

    THE ONE CHECK THAT TIES THE TWO FILES TOGETHER. Everything else here is
    about the sheet being well formed; this is about it being true.
    """
    for row in effects:
        node = nodes[row["Node"]]
        value = float(row["ValuePerPoint"])

        # A VALUE THE SENTENCE STATES IN WORDS IS CHECKED AGAINST THOSE WORDS.
        # See `VALUE_IN_WORDS` for why two rows are written that way and what
        # this still catches.
        stated = VALUE_IN_WORDS.get((row["Node"], row["Stat"]))
        if stated is not None:
            phrase, means = stated
            assert phrase in node["Description"].lower(), (
                f"{row['Node']}: the workbook states {row['Stat']} in words "
                f"rather than digits, as {phrase!r}, and the node says:\n"
                f"    {node['Description']}\n"
                "Either the node was reworded or the exemption is stale."
            )
            assert value == pytest.approx(means), (
                f"{row['Node']}: {phrase!r} means {means:g} of {row['Stat']} "
                f"and the workbook grants {value:g}."
            )
            continue

        # "3" and not "3.0", because a description writes 3% and 1.5%. `%g`
        # drops a trailing zero and keeps a real fraction.
        #
        # WITHOUT THE SIGN, because a description never writes one. A node that
        # takes something away says "reduced by 5% per point" while the sheet
        # writes -5: the pipeline sums increases, so a reduction is a negative
        # increase. The sign is checked by the test below instead.
        #
        # AND IN THE STAT'S OWN UNITS. Nearly every value is a percentage, and
        # `VALUE_FORMS` names the ones that are not.
        printed = VALUE_FORMS.get(row["Stat"], "{value:g}%").format(
            value=abs(value))
        assert printed in node["Description"], (
            f"{row['Node']}: the workbook grants {printed} of "
            f"{row['Stat']} per point, and the node says:\n"
            f"    {node['Description']}\n"
            "The two have to agree. Either the workbook is stale or the tree "
            "file changed."
        )


#: Words a node uses when its own effect takes something away.
TAKES_AWAY = ("reduce", "less", "fewer", "lower", "removed by")


def test_a_negative_value_is_on_a_node_that_says_it_takes_something_away(
        effects, nodes):
    """The number is matched without its sign, so the sign needs its own check.

    THE FAILURE THIS CATCHES. A row written -5 on a node that says "increased"
    would pass the number check above and make the node worth the opposite of
    what a player reads. Nothing at run time reports it: the arithmetic runs and
    the character is simply worse.

    ONLY ONE DIRECTION CAN BE CHECKED, and saying why matters more than the
    check. A POSITIVE value on a node that says "reduced" is often correct,
    because whether a bigger number is better belongs to the STAT rather than to
    the words: `damage_reduction` is a node saying "damage taken is reduced by
    1.5% per point" carrying a positive 1.5, and that is right. So a negative
    value must be on a node whose words take something away, and nothing is
    asserted the other way round.
    """
    for row in effects:
        value = float(row["ValuePerPoint"])
        if value >= 0:
            continue

        words = nodes[row["Node"]]["Description"].lower()
        assert any(term in words for term in TAKES_AWAY), (
            f"{row['Node']} grants {value:g} of {row['Stat']} per point, which "
            f"takes something away, and its description does not say so:\n"
            f"    {nodes[row['Node']]['Description']}\n"
            f"A description that takes something away uses one of {TAKES_AWAY}."
        )


def test_a_node_that_takes_something_away_is_actually_tested(effects):
    """The check above is worth nothing without a row that exercises it.

    ONE ROW HAS A NEGATIVE VALUE TODAY: the Masochist's Staunch node, which
    reduces the Fervour its own health regeneration removes by 5% per point.
    Without a negative row the test above passes over every row without
    asserting anything, which reads as a guard that holds.
    """
    negative = [row for row in effects if float(row["ValuePerPoint"]) < 0]
    assert negative, (
        "No passive effect has a negative value, so nothing exercises the check "
        "that a reduction is on a node whose words reduce something. If the "
        "last one was deliberately removed, delete that test with it rather "
        "than leaving it passing over nothing."
    )


def test_the_bucket_matches_the_nodes_own_wording(effects, nodes):
    """A description that says its number multiplies is the more bucket, and
    one that does not is not.

    WHY IT MATTERS RATHER THAN BEING TIDY. The three buckets are
    `(base + flat) x (1 + increases) x more1 x more2`. Putting a value in the
    wrong one changes what it is worth by a large factor on an invested
    character, and by nothing at all on a fresh one -- so it would look right
    in exactly the situation somebody is most likely to check it in.

    IT USED TO ASK FOR THE LITERAL WORD "multiplicative" AND THAT REFUSED HALF
    THE TREES. Issue #977. See `MULTIPLIES` for the two wordings and for the
    two entries in `docs/DECISIONS.md` that say they mean the same thing.

    THE SECOND DIRECTION GAVE WAY ON 2026-08-26, EXACTLY AS PREDICTED HERE.
    Issue #986. Exsanguinate reads "Every skill costs an additional 15% of your
    current health, and every skill deals 40% more damage": one sentence, one
    `flat` row for the cost and two `more` rows for the damage. The `flat` row
    sits on a node whose description says "40% more", so the strict form
    refused it.

    SO THE SECOND DIRECTION NOW ASKS ONLY OF A NODE WITH NO `more` ROW AT ALL.
    A node that has one has a description that legitimately says "more" about
    a different clause, and there is no way to tell from the sentence which
    clause a given row belongs to. A node with none is the case the check was
    really for: a description saying its number multiplies while every row on
    it lands in the additive sum.
    """
    #: Which nodes have at least one row in the more bucket.
    multiplying_nodes = {row["Node"] for row in effects
                         if row["ValueKind"].strip().lower() == "more"}

    for row in effects:
        kind = row["ValueKind"].strip().lower()
        assert kind in BUCKETS, (
            f"{row['Name']}: {kind!r} is not one of {sorted(BUCKETS)}"
        )

        multiplies = MULTIPLIES.search(nodes[row["Node"]]["Description"])
        if kind == "more":
            assert multiplies, (
                f"{row['Node']} is in the more bucket and its description does "
                f"not say so. It has to say \"multiplicative\", or write the "
                f"number as \"30% more\" or \"50% less\":\n    "
                f"{nodes[row['Node']]['Description']}"
            )
        elif row["Node"] not in multiplying_nodes:
            assert not multiplies, (
                f"{row['Node']} says {multiplies.group(0)!r}, which multiplies, "
                f"and its only rows are in the {kind} bucket. A multiplicative "
                "value in the increased bucket is added to a sum instead of "
                "multiplying, which is a different number."
            )


def test_every_stat_is_one_the_game_supplies(effects, stats):
    """An increase multiplies a base, so a stat nothing supplies is worth zero.

    That failure is silent: the modifier is applied, the arithmetic runs, and
    the result is the same as not having the node. Issue #120 is the same
    mistake on attack speed, which was worth nothing for some time and which
    nothing reported.

    A FLAT ROW IN THIS FILE COUNTS AS A SUPPLIER, since issue #954. The
    complaint above is about an INCREASE with no base under it; a flat row is
    the base. The three Fervour rates are the case: they are zero for every
    class deliberately, so no class stat line names them and none should -- a
    class stat row that is zero in both columns says nothing, and
    `test_class_sheets_match_the_model.py` refuses one. What supplies them is
    the Masochist's starting node, with a flat row here.

    A FLAT IMPLICIT ON AN ITEM BASE COUNTS TOO, since issue #958, and the case
    is `attack_damage`. No class stat line names it and none should: a
    character's damage comes from the weapon in its hands, which carries an
    `attack_damage` flat implicit, so the class base is correctly zero.
    `UCataclysmPlayerClassStats` states that rule in its own header. The node
    Living on the Edge grants increased damage and has a real base under it the
    moment a weapon is held.

    A ROLLED AFFIX DOES NOT COUNT, and the difference decides whether this check
    is worth anything. An implicit is on EVERY item of that base type, so a
    Sword always carries attack damage; an affix may never roll. A stat whose
    only supplier is an optional affix really is zero for most characters and an
    increase on it really is worth nothing, which is the complaint this test
    exists to make. Counting affixes would admit twelve more stats and blunt it.

    WHAT THIS CANNOT CATCH, AND WHAT DOES. A name misspelt the same way in both
    a flat row and an increased row would pass here. The engine-side test
    `Cataclysm.Passives.EveryStatAPassiveNodeGrantsHasAnAttributeBehindIt` reads
    the same file and fails when a stat has no gameplay attribute behind it,
    which is where a misspelling really stops working.
    """
    for row in effects:
        assert row["Stat"] in stats, (
            f"{row['Name']} grants {row['Stat']!r}, which is not a stat any "
            f"class stat line or attribute names, and no flat row here supplies "
            f"it either. The nearest are: "
            f"{sorted(s for s in stats if s[:4] == row['Stat'][:4]) or sorted(stats)[:6]}"
        )


def test_every_required_tag_is_declared(effects, declared_tags):
    """An undeclared tag matches nothing, so the modifier applies to nothing."""
    for row in effects:
        for tag in (part.strip() for part in row["RequiredTags"].split(",")):
            if not tag:
                continue
            assert tag in declared_tags, (
                f"{row['Name']} requires the tag {tag!r}, which the workbook's "
                "Tags sheet does not declare. A required tag nothing carries "
                "means the modifier applies to nothing at all."
            )


def test_the_coverage_is_what_it_was_measured_to_be(effects, nodes):
    """How much of the passive tree actually grants anything.

    THIS IS THE NUMBER THE WHOLE FEATURE SHOULD BE JUDGED ON, so it is pinned
    rather than left to drift. Issue #939 measures why the rest are not here:
    most nodes are not stat modifiers under any authoring scheme. They change a
    rule, generate a class resource, or apply only in a condition the three
    buckets cannot express.

    Raise both numbers deliberately when more are authored.
    """
    assert len(effects) == AUTHORED_ROWS, (
        f"{EFFECTS_CSV.name} holds {len(effects)} rows and this test expects "
        f"{AUTHORED_ROWS}. If that is deliberate, raise AUTHORED_ROWS here and "
        "say what changed in the pull request."
    )

    with_an_effect = {row["Node"] for row in effects}
    assert len(with_an_effect) == AUTHORED_NODES, (
        f"{len(with_an_effect)} of the {len(nodes)} passive nodes have an "
        f"authored effect and this test expects {AUTHORED_NODES}. This is the "
        "number the whole feature is judged on, so raise AUTHORED_NODES "
        "deliberately and say what changed in the pull request."
    )
    assert len(nodes) == TOTAL_NODES, (
        f"there are now {len(nodes)} passive nodes, not {TOTAL_NODES}. Raise "
        "TOTAL_NODES here; the share that does anything is what this file is "
        "about and it cannot be read without both numbers."
    )


def test_at_least_one_effect_is_scoped_by_a_tag(effects):
    """The Required Tags column is exercised rather than merely present.

    ONE ROW USES IT: the Saboteur's Bigger Traps, whose description scopes its
    area of effect "for traps". Without a row that uses it, the column and the
    code that reads it would both be untested, and the first node that needed
    scoping would find out whether it worked the hard way.
    """
    scoped = [row for row in effects if row["RequiredTags"].strip()]
    assert scoped, (
        "No passive effect requires a tag, so nothing exercises the Required "
        "Tags column or the code that turns it into a modifier's required "
        "tags. If the last scoped node was deliberately removed, delete this "
        "test with it rather than leaving the column untested."
    )
