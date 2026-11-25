"""The empire upgrade tree is kept on the account record -- and a Solo Self-Found
character's own on its character record -- the committed files hold trees play
can reach, and every change to a tree is saved in the record that holds it.

WHY THIS EXISTS. Issue #2741, slice 1b. The account record went to version 4 to
hold `EmpireTree`, what the partition's empire upgrade tree has bought, and the
game instance buys, chooses and respecs through `EmpireTreeHeldBy`. The Unreal
tests in `game/Source/Cataclysm/Tests/CataclysmEmpireTreeAccountTests.cpp` cover
all of it, and continuous integration compiles no C++, so the parts that can be
read as text are checked here as well.

SLICE 1C (the same issue) gave a Solo Self-Found character its own bank and tree,
on its character record at version 9 (the owner's reversal of C8, 2026-11-18), so
`Character_v16.json`, the committed character file since issue #2849 (`Character_v15.json` from issue #2848,
`Character_v14.json` from issue #2741 slice 9,
`Character_v13.json` from issue #46, `Character_v12.json` from issue #2741 slice 8), is checked the same way as the
account file.

WHAT EACH CHECK HOLDS, and what would go wrong without it:

  the committed file is at the     `Account_v6.json` is the file
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
    record that holds the tree       next session does not have; a Solo
                                     Self-Found character's own tree is in its
                                     character record, which `AccountChanged`
                                     does not write (slice 1c)
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
# `Account_v7.json` SINCE ISSUE #2741 SLICE 9 AND `Character_v16.json` SINCE ISSUE #2849 (`Character_v15.json` from issue
# #2848, `Character_v14.json` from slice 9), `Account_v6.json` and `Character_v13.json` from issue #46 and slice 8.
ACCOUNT_FILE = REPO_ROOT / "game" / "Tests" / "SaveFixtures" / "Account_v7.json"
CHARACTER_FILE = REPO_ROOT / "game" / "Tests" / "SaveFixtures" / "Character_v16.json"

#: `class CATACLYSM_API UCataclysmAccountSave ... SchemaVersionNow = 7;` (6 until issue #2741 slice 9, 5 until slice 8,
#: 4 until slice 6e)
ACCOUNT_VERSION = re.compile(
    r"class\s+CATACLYSM_API\s+UCataclysmAccountSave\s*:.*?static\s+constexpr\s+int32\s+SchemaVersionNow\s*=\s*(\d+)\s*;",
    re.DOTALL)

#: `class CATACLYSM_API UCataclysmCharacterSave ... SchemaVersionNow = 16;` (15 until issue #2849, 14 until issue #2848, 13 until issue #2741
#: slice 9, 12 until
#: issue #46, 11 until slice 8, 10 until slice 6e, 9 until issue #2764)
CHARACTER_VERSION = re.compile(
    r"class\s+CATACLYSM_API\s+UCataclysmCharacterSave\s*:.*?static\s+constexpr\s+int32\s+SchemaVersionNow\s*=\s*(\d+)\s*;",
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
    assert "NoteTheEmpireTreeChanged(" in respec, "a respec no longer writes the record that holds the tree"
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
    assert "EmpireTreeChangeRefusal(" in respec, "a respec is no longer refused while a dungeon is being walked"
    refusal = body_of(code, "FString UCataclysmGameInstance::EmpireTreeChangeRefusal(")
    assert "IsAWalkUnderWay()" in refusal, "the tree's one walk refusal no longer asks whether a walk is under way"


@pytest.mark.parametrize("signature", [
    "bool UCataclysmGameInstance::BuyEmpireTreePoint(",
    "bool UCataclysmGameInstance::ChooseEmpireTreeOption(",
])
def test_a_buy_and_a_choice_write_the_record_that_holds_the_tree(signature):
    code = without_comments(GAME_INSTANCE.read_text(encoding="utf-8"))
    assert "NoteTheEmpireTreeChanged(" in body_of(code, signature), (
        f"{signature.split('::')[1].rstrip('(')} no longer writes the record that holds the tree")
    # AND IS REFUSED DURING A WALK, the one rule buying, choosing and respec share since slice 2.
    assert "EmpireTreeChangeRefusal(" in body_of(code, signature), (
        f"{signature.split('::')[1].rstrip('(')} is no longer refused while a dungeon is being walked")


def test_the_record_that_holds_the_tree_is_the_one_written():
    """The account record for a lethality mode's tree, a Solo Self-Found character's own record for its own. Issue
    #2741 slice 1c: `AccountChanged` writes only the account record, and such a character has none."""
    code = without_comments(GAME_INSTANCE.read_text(encoding="utf-8"))
    noted = body_of(code, "void UCataclysmGameInstance::NoteTheEmpireTreeChanged(")
    assert "bTheCharactersOwn" in noted, "the trigger no longer depends on which record holds the tree"
    assert "ECataclysmSaveTrigger::AccountChanged" in noted, "a lethality mode's tree no longer writes the account record"
    assert "ECataclysmSaveTrigger::OwnEmpireTreeChanged" in noted, (
        "a Solo Self-Found character's own tree no longer writes its character record")
    triggers = without_comments(TRIGGERS.read_text(encoding="utf-8"))
    character = body_of(triggers, "bool UCataclysmSaveTriggers::WritesTheCharacterRecord(")
    assert "case ECataclysmSaveTrigger::OwnEmpireTreeChanged:" in character, (
        "the trigger for a Solo Self-Found character's own tree does not write the character record")
    listed = body_of(triggers, "TArray<ECataclysmSaveTrigger> UCataclysmSaveTriggers::Every(")
    assert "ECataclysmSaveTrigger::OwnEmpireTreeChanged" in listed, "the trigger is not in the list its tests walk"


def test_the_committed_character_file_holds_its_own_tree_at_the_records_version(node_rows, edges_into):
    """`Character_v16.json` (`Character_v15.json` until issue #2849, `Character_v14.json` until issue #2848, `Character_v13.json` until issue #2741 slice 9, `Character_v12.json` until issue #46,
    `Character_v11.json` until issue #2741 slice 8, `Character_v10.json` until slice 6e,
    `Character_v9.json` until issue #2764):
    at the character record's version, with
    private points and a tree of its own whose every node is a row within its points, waits on nothing, takes no
    option, and is open. Slice 1c."""
    character = json.loads(CHARACTER_FILE.read_text(encoding="utf-8"))
    match = CHARACTER_VERSION.search(RECORDS_H.read_text(encoding="utf-8"))
    assert match, f"no SchemaVersionNow found for UCataclysmCharacterSave in {RECORDS_H.relative_to(REPO_ROOT)}"
    assert character["SchemaVersion"] == int(match.group(1)), (
        f"{CHARACTER_FILE.name} is at version {character['SchemaVersion']} and the character record at "
        f"{match.group(1)}. A version bump commits a new fixture: game/Tests/SaveFixtures/README.md.")
    points = character["PrivateEmpireUpgradePoints"]
    assert points != 0 and points != int(points), (
        f"{CHARACTER_FILE.name}'s private points are {points}; a fraction other than 0 shows the double is read")
    nodes = character["PrivateEmpireTree"]["Nodes"]
    assert nodes, f"{CHARACTER_FILE.name} holds an empty tree of its own, which shows nothing about reading one"
    spent = {}
    for entry in nodes:
        row = node_rows.get(entry["Node"])
        assert row is not None, f"{CHARACTER_FILE.name} names {entry['Node']}, which EmpireTreeNodes.csv does not hold"
        assert 1 <= entry["Points"] <= int(row["MaxPoints"]) and row["WaitsOn"] == "" and entry["ChosenOption"] == 0, (
            f"{entry} is not a state its own tree can hold")
        spent[entry["Node"]] = entry["Points"]
    assert shut_nodes(spent, node_rows, edges_into) == [], f"{CHARACTER_FILE.name} holds nodes no player could open"


def test_a_solo_self_found_characters_own_tree_is_gathered_restored_and_read_in_one_place():
    """The save gather writes the character's own bank and tree, the load puts them back, and `EmpireTreeHeldBy` hands
    them out for a Solo Self-Found character. Slice 1c; without the first two a save loses them, without the third
    nothing spends them."""
    gather = without_comments((REPO_ROOT / "game" / "Source" / "Cataclysm" / "Save" / "CataclysmSaveGather.cpp")
                              .read_text(encoding="utf-8"))
    apply = without_comments((REPO_ROOT / "game" / "Source" / "Cataclysm" / "Save" / "CataclysmSaveApply.cpp")
                             .read_text(encoding="utf-8"))
    assert "Record.PrivateEmpireUpgradePoints = State->GetOwnEmpireUpgradePoints();" in gather, (
        "the save gather no longer writes a Solo Self-Found character's own empire upgrade points")
    assert "Record.PrivateEmpireTree = State->GetOwnEmpireTree();" in gather, (
        "the save gather no longer writes a Solo Self-Found character's own tree")
    assert "State->SetOwnEmpireTree(Record.PrivateEmpireUpgradePoints, Record.PrivateEmpireTree);" in apply, (
        "a load no longer puts a Solo Self-Found character's own bank and tree back")
    code = without_comments(GAME_INSTANCE.read_text(encoding="utf-8"))
    held = body_of(code, "bool UCataclysmGameInstance::EmpireTreeHeldBy(")
    assert "EditOwnEmpireUpgradePoints()" in held and "EditOwnEmpireTree()" in held, (
        "a Solo Self-Found character's own bank and tree are no longer what the one read hands out")
