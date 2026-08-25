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
    an effect in the `more` bucket is on a node whose description says
      "multiplicative", and one in `increased` is on a node that does not
    every stat named is a stat some class line or attribute supplies
    every required tag is one the workbook declares
    no node has two effects
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

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
EFFECTS_CSV = DATA / "PassiveEffects.csv"
NODES_CSV = DATA / "PassiveNodes.csv"
CLASS_STATS_CSV = DATA / "ClassStats.csv"
ATTRIBUTES_CSV = DATA / "Attributes.csv"
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"

#: The three buckets of the damage pipeline. Anything else is a typo.
BUCKETS = {"flat", "increased", "more"}

#: How many of the 293 nodes have an authored effect.
#:
#: PINNED RATHER THAN LEFT AS A FLOOR, and the reason is that this number is the
#: honest measure of how much of the passive tree actually does anything. Issue
#: #939 measures the gap and lists what each remaining group would need. A change
#: to this number should be somebody's decision, not a side effect.
#:
#: IT WENT DOWN FROM 26 ON 2026-08-25, AND THAT WAS THE DECISION. Replacing the
#: Masochist's upper-right section removed three authored rows -- maximum mana,
#: mana regeneration and area of effect -- because the nodes holding them are
#: gone. One row arrived, for maximum health. The rest of the new section is
#: mechanics rather than modifiers: a cost taken late, a stack that expires, a
#: debt that has to be settled. None of those can be written as a row here, which
#: is the same finding issue #939 made about the other 267 nodes.
AUTHORED = 24

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
    return ({row["Stat"] for row in rows_of(CLASS_STATS_CSV)}
            | {row["Stat"] for row in rows_of(ATTRIBUTES_CSV)})


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
    missing = sorted({row["Name"] for row in effects} - set(nodes))
    assert not missing, (
        f"{EFFECTS_CSV.name} grants something to {missing}, and "
        f"{NODES_CSV.name} has no such node. The effect reaches nothing, and "
        "nothing at run time reports it."
    )


def test_no_node_has_two_effects(effects):
    seen: dict[str, int] = {}
    for row in effects:
        seen[row["Name"]] = seen.get(row["Name"], 0) + 1

    twice = sorted(name for name, count in seen.items() if count > 1)
    assert not twice, (
        f"{twice} have more than one row in {EFFECTS_CSV.name}. A node grants "
        "one effect; two rows would silently apply both."
    )


def test_every_value_appears_in_the_nodes_own_description(effects, nodes):
    """The number in the workbook is the number the design document states.

    THE ONE CHECK THAT TIES THE TWO FILES TOGETHER. Everything else here is
    about the sheet being well formed; this is about it being true.
    """
    for row in effects:
        node = nodes[row["Name"]]
        value = float(row["ValuePerPoint"])

        # "3" and not "3.0", because a description writes 3% and 1.5%. `%g`
        # drops a trailing zero and keeps a real fraction.
        printed = f"{value:g}%"
        assert printed in node["Description"], (
            f"{row['Name']}: the workbook grants {printed} of "
            f"{row['Stat']} per point, and the node says:\n"
            f"    {node['Description']}\n"
            "The two have to agree. Either the workbook is stale or the tree "
            "file changed."
        )


def test_the_bucket_matches_the_nodes_own_wording(effects, nodes):
    """A description that says "multiplicative" is the more bucket, and one
    that does not is not.

    WHY IT MATTERS RATHER THAN BEING TIDY. The three buckets are
    `(base + flat) x (1 + increases) x more1 x more2`. Putting a value in the
    wrong one changes what it is worth by a large factor on an invested
    character, and by nothing at all on a fresh one -- so it would look right
    in exactly the situation somebody is most likely to check it in.
    """
    for row in effects:
        kind = row["ValueKind"].strip().lower()
        assert kind in BUCKETS, (
            f"{row['Name']}: {kind!r} is not one of {sorted(BUCKETS)}"
        )

        says_multiplicative = "multiplicative" in nodes[row["Name"]]["Description"].lower()
        if kind == "more":
            assert says_multiplicative, (
                f"{row['Name']} is in the more bucket and its description does "
                f"not say multiplicative:\n    "
                f"{nodes[row['Name']]['Description']}"
            )
        else:
            assert not says_multiplicative, (
                f"{row['Name']} says multiplicative and is in the {kind} "
                "bucket. A multiplicative value in the increased bucket is "
                "added to a sum instead of multiplying, which is a different "
                "number."
            )


def test_every_stat_is_one_the_game_supplies(effects, stats):
    """An increase multiplies a base, so a stat nothing supplies is worth zero.

    That failure is silent: the modifier is applied, the arithmetic runs, and
    the result is the same as not having the node. Issue #120 is the same
    mistake on attack speed, which was worth nothing for some time and which
    nothing reported.
    """
    for row in effects:
        assert row["Stat"] in stats, (
            f"{row['Name']} grants {row['Stat']!r}, which is not a stat any "
            f"class stat line or attribute names. The nearest are: "
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
    assert len(effects) == AUTHORED, (
        f"{len(effects)} passive nodes have an authored effect and this test "
        f"expects {AUTHORED}. If that is deliberate, raise AUTHORED here and "
        "say what changed in the pull request."
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
