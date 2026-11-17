"""The design document's count of multiplicative damage reduction nodes is real.

WHAT WENT WRONG, TWICE. `docs/Cataclysm_GDD_v2.md` states how many passive tree
nodes grant damage reduction and say "(multiplicative)", and which trees they are
in. The number was twelve across two trees when the data held nine in one, which
issue #1232 corrected by hand. Nothing then held the corrected number to the
data, so adding "(multiplicative)" to one Saboteur node on 2026-09-04 made it
wrong again in the same way.

A sentence that counts rows is a number to keep in step, and this is what keeps
it. The counts below are read out of `game/Data/PassiveNodes.csv` and compared
against the sentence, so neither can move without the other.
"""

from __future__ import annotations

import collections
import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
NODES = REPO_ROOT / "game" / "Data" / "PassiveNodes.csv"
DESIGN = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: The words the design document uses, in the sentence that counts them.
SENTENCE = re.compile(
    r'\*\*"Multiplicative" and "more" are the same word\.\*\* '
    r"(?P<total>\w+) passive tree nodes grant damage reduction and say "
    r'"\(multiplicative\)": (?P<first_count>\w+) in the (?P<first_tree>\w+) '
    r"tree and (?P<second_count>\w+) in the (?P<second_tree>\w+) tree\.")

#: Only as far as the counts actually reach, so a wrong word fails rather than
#: raising a KeyError that says nothing about the design.
WORDS = {"zero": 0, "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
         "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11,
         "twelve": 12, "thirteen": 13, "fourteen": 14, "fifteen": 15}


@pytest.fixture(scope="module")
def counted() -> collections.Counter:
    """Nodes granting damage reduction and saying "(multiplicative)", by tree."""
    with NODES.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    return collections.Counter(
        row["Tree"] for row in rows
        if "(multiplicative)" in row["Description"]
        and "damage reduction" in row["Description"].lower())


@pytest.fixture(scope="module")
def stated() -> dict:
    text = DESIGN.read_text(encoding="utf-8")
    found = SENTENCE.search(text)
    assert found is not None, (
        "docs/Cataclysm_GDD_v2.md no longer contains the sentence counting the "
        "passive nodes that grant multiplicative damage reduction, in the shape "
        "this test reads. Either it was reworded, in which case update the "
        "pattern here, or the design changed, in which case the counts below "
        "have to be checked by hand.")
    return found.groupdict()


def word(value: str) -> int:
    assert value.lower() in WORDS, (
        f"the design document writes the count as {value!r}, which is not a "
        "number word this test knows")
    return WORDS[value.lower()]


def test_the_stated_total_matches_the_data(counted, stated) -> None:
    assert word(stated["total"]) == sum(counted.values()), (
        f"the design document says {stated['total']} nodes grant multiplicative "
        f"damage reduction and game/Data/PassiveNodes.csv holds "
        f"{sum(counted.values())}: {dict(counted)}")


def test_the_stated_trees_match_the_data(counted, stated) -> None:
    said = {stated["first_tree"]: word(stated["first_count"]),
            stated["second_tree"]: word(stated["second_count"])}
    assert said == dict(counted), (
        f"the design document says {said} and game/Data/PassiveNodes.csv holds "
        f"{dict(counted)}")


def test_the_saboteur_node_is_one_of_them(counted) -> None:
    """The node the project owner ruled on, named so the ruling is not lost.

    `Saboteur_basic_trunk_014`, Durable Modifications, granted damage reduction
    without saying "(multiplicative)", so as written it joined the capped
    additive pool. The design document had named it as one of the multiplicative
    nodes and #1232 corrected the count without touching the node. The project
    owner settled it on 2026-09-04: it is meant to be multiplicative.
    """
    with NODES.open(newline="", encoding="utf-8") as handle:
        rows = {row["Name"]: row for row in csv.DictReader(handle)}

    node = rows.get("Saboteur_basic_trunk_014")
    assert node is not None, "Saboteur_basic_trunk_014 is no longer in the data"
    assert "(multiplicative)" in node["Description"], (
        "Saboteur_basic_trunk_014 is Durable Modifications, and the project "
        "owner ruled on 2026-09-04 that its damage reduction is its own "
        "multiplier rather than joining the capped additive pool. Without the "
        "word it silently joins the pool.")
    assert counted["Saboteur"] >= 1
