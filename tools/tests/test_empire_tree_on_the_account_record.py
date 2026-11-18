"""The empire upgrade tree is kept on the account record, the committed account
file holds a tree play can reach, and every change to the tree is saved.

WHY THIS EXISTS. Issue #2741, slice 1b. The account record went to version 4 to
hold `EmpireTree`, what the partition's empire upgrade tree has bought, and the
game instance buys, chooses and respecs through `EmpireTreeHeldBy`. The Unreal
tests in `game/Source/Cataclysm/Tests/CataclysmEmpireTreeAccountTests.cpp` cover
all of it, and continuous integration compiles no C++, so the parts that can be
read as text are checked here as well.

WHAT EACH CHECK HOLDS, and what would go wrong without it:

  the committed file is at the     `Account_v4.json` is the file
    record's version                 `Cataclysm.SaveRecords.EveryFixtureHoldsEveryFieldItsRecordWrites`
                                     compares the record against; a stale one
                                     compares the record with an older shape
  every node in its tree is a      a fixture naming a node the tree file lost,
    row, within its points           or more points than the node takes, is a
                                     state no player can be in
  its tree is reachable by the     the same, for a node bought with nothing
    edges and the thresholds         leading to it, or a capstone below its
                                     threshold
  a respec writes both records,    with the account record alone, a player who
    the run record by a trigger      quit before the next run write would come
    no earlier write can skip        back to a respec that cost no days; and the
                                     clock's trigger is skipped when another
                                     write wrote the run record in the frame
  a buy and a choice write the     a point bought and not written is a point the
    account record                   next session does not have
"""

from __future__ import annotations

import csv
import json
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
NODES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv"
EDGES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeEdges.csv"
RECORDS_H = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Save" / "CataclysmSaveRecords.h"
GAME_INSTANCE = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player" / "CataclysmGameInstance.cpp"
TRIGGERS = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Save" / "CataclysmSaveTriggers.cpp"
ACCOUNT_FILE = REPO_ROOT / "game" / "Tests" / "SaveFixtures" / "Account_v4.json"

#: `class CATACLYSM_API UCataclysmAccountSave ... SchemaVersionNow = 4;`
ACCOUNT_VERSION = re.compile(
    r"class\s+CATACLYSM_API\s+UCataclysmAccountSave\s*:.*?static\s+constexpr\s+int32\s+SchemaVersionNow\s*=\s*(\d+)\s*;",
    re.DOTALL)


def without_comments(text: str) -> str:
    """The code with its comments removed, so a trigger named only in a comment does not count."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def body_of(text: str, signature: str) -> str:
    """One function's body, from its signature to the line that closes it."""
    start = text.index(signature)
    return text[start:text.index("\n}", start)]


@pytest.fixture(scope="module")
def node_rows() -> dict[str, dict]:
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


@pytest.fixture(scope="module")
def edges_into() -> dict[str, list[dict]]:
    into: dict[str, list[dict]] = {}
    with EDGES_CSV.open(encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            into.setdefault(row["Target"], []).append(row)
    return into


@pytest.fixture(scope="module")
def account_file() -> dict:
    return json.loads(ACCOUNT_FILE.read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def committed_tree(account_file) -> dict[str, dict]:
    nodes = account_file["EmpireTree"]["Nodes"]
    return {entry["Node"]: entry for entry in nodes}


def test_the_committed_account_file_is_at_the_records_version(account_file, committed_tree):
    match = ACCOUNT_VERSION.search(RECORDS_H.read_text(encoding="utf-8"))
    assert match, f"no SchemaVersionNow found for UCataclysmAccountSave in {RECORDS_H.relative_to(REPO_ROOT)}"
    assert account_file["SchemaVersion"] == int(match.group(1)), (
        f"{ACCOUNT_FILE.name} is at version {account_file['SchemaVersion']} and the account record at "
        f"{match.group(1)}. A version bump commits a new fixture: game/Tests/SaveFixtures/README.md.")
    assert committed_tree, f"{ACCOUNT_FILE.name} holds an empty tree, which shows nothing about reading one"


def test_every_node_in_the_committed_tree_is_a_row_within_its_points(committed_tree, node_rows):
    for name, entry in committed_tree.items():
        assert name in node_rows, f"{ACCOUNT_FILE.name} names {name}, which EmpireTreeNodes.csv does not hold"
        row = node_rows[name]
        assert 1 <= entry["Points"] <= int(row["MaxPoints"]), (
            f"{name} ({row['NodeName']}) holds {entry['Points']} of its {row['MaxPoints']}")
        assert row["WaitsOn"] == "", f"{name} ({row['NodeName']}) waits on {row['WaitsOn']} and cannot be bought"
        option = entry["ChosenOption"]
        if row["Kind"] == "capstone" and row["Branch"] == "Centre":
            assert 1 <= option <= 3, f"{name} ({row['NodeName']}) holds a point with no option chosen"
            assert row[f"Option{option}WaitsOn"] == "", (
                f"{name}'s option {option}, {row[f'Option{option}Name']}, waits on a system and cannot be chosen")
        else:
            assert option == 0, f"{name} ({row['NodeName']}) is not a centre capstone and has an option chosen"


def shut_nodes(spent: dict[str, int], node_rows: dict[str, dict], edges_into: dict[str, list[dict]]) -> list[str]:
    """Every node holding points that the rest of the tree does not open, each with why; empty when all are open.

    A node is open by an edge any one parent satisfies, and a capstone by its threshold counted without its own
    point: in the whole tree for a centre capstone, in its branch for a branch capstone. The tree has no cycle, so a
    state where every node is open is one a player can buy in some order.
    """
    total = sum(spent.values())
    shut: list[str] = []
    for name, points in spent.items():
        row = node_rows[name]
        if row["Kind"] == "capstone":
            counted = total if row["Branch"] == "Centre" else sum(
                held for other, held in spent.items() if node_rows[other]["Branch"] == row["Branch"])
            if counted - points < int(row["Threshold"]):
                shut.append(f"{name} ({row['NodeName']}) opens at {row['Threshold']} and the rest holds "
                            f"{counted - points}")
            continue
        incoming = edges_into.get(name, [])
        if incoming and not any(spent.get(edge["Source"], 0) >= int(edge["RequiredPoints"]) for edge in incoming):
            shut.append(f"{name} ({row['NodeName']}): "
                        + ", ".join(f"{edge['Source']} needs {edge['RequiredPoints']}, holds "
                                    f"{spent.get(edge['Source'], 0)}" for edge in incoming))
    return shut


def test_the_committed_tree_is_reachable_by_its_edges_and_thresholds(committed_tree, node_rows, edges_into):
    spent = {name: entry["Points"] for name, entry in committed_tree.items()}
    assert shut_nodes(spent, node_rows, edges_into) == [], f"{ACCOUNT_FILE.name} holds nodes no player could open"


@pytest.mark.parametrize("spent", [
    {"basic_00000010": 7},                                            # Masonry Techniques, nothing leading to it
    {"basic_0000000w": 10, "basic_arch_t1d": 5, "basic_00000010": 7},  # Fortified Gates short of its 6
    {"basic_0000000w": 10, "basic_arch_t1d": 8, "capstone_0000000m": 1},  # the capstone at 18 of its 25
])
def test_the_reachability_check_can_say_no(spent, node_rows, edges_into):
    """CONTROL: a tree no player could buy is reported, by the same function the committed file passes."""
    assert shut_nodes(spent, node_rows, edges_into), f"{spent} was reported reachable"


def test_a_respec_writes_the_account_record_and_the_run_record():
    code = without_comments(GAME_INSTANCE.read_text(encoding="utf-8"))
    respec = body_of(code, "bool UCataclysmGameInstance::RespecEmpireTree(")
    assert "ECataclysmSaveTrigger::AccountChanged" in respec, "a respec no longer writes the account record"
    assert "ECataclysmSaveTrigger::EmpireTreeRespecced" in respec, (
        "a respec no longer writes the run record by its own trigger. With the account record alone, a player who quits "
        "before the next run write comes back to a respec that cost no days (#2741, slice 1b).")
    triggers = without_comments(TRIGGERS.read_text(encoding="utf-8"))
    again = body_of(triggers, "bool UCataclysmSaveTriggers::WritesEvenIfTheRunWasWrittenThisFrame(")
    assert "ECataclysmSaveTrigger::EmpireTreeRespecced" in again, (
        "a respec's run write can be skipped by a write earlier in its frame, leaving its days off the disk until the "
        "next write. The coordinating session's ruling of 2026-11-18.")
    listed = body_of(triggers, "TArray<ECataclysmSaveTrigger> UCataclysmSaveTriggers::Every(")
    assert "ECataclysmSaveTrigger::EmpireTreeRespecced" in listed, "the respec's trigger is not in the list its tests walk"
    assert "AdvanceDays(" in respec, "a respec no longer passes its days through the run's AdvanceDays"
    assert "IsAWalkUnderWay()" in respec, "a respec is no longer refused while a dungeon is being walked"


@pytest.mark.parametrize("signature", [
    "bool UCataclysmGameInstance::BuyEmpireTreePoint(",
    "bool UCataclysmGameInstance::ChooseEmpireTreeOption(",
])
def test_a_buy_and_a_choice_write_the_account_record(signature):
    code = without_comments(GAME_INSTANCE.read_text(encoding="utf-8"))
    assert "ECataclysmSaveTrigger::AccountChanged" in body_of(code, signature), (
        f"{signature.split('::')[1].rstrip('(')} no longer writes the account record")
