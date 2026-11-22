"""The empire upgrade tree's slice 8: Imperial Prowess.

WHY THIS EXISTS. Issue #2741 slice 8. The project owner, 2026-11-21, verbatim: "Keystones are tier gated behind point
thresholds. Imperial prowess lets you pick an additional keystone from a previous tier." The keystones with point
thresholds are the four centre milestones' options, so Imperial Prowess lets one more option of an earlier milestone be
taken beside the one chosen there. The coordinating session's J1 to J5 the same day: the earlier milestone's own option
chosen and its point bought first; one extra option in total; the account and character records move on; the two
crafting options stay refused; the extra option stands until a respec.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.EmpireTreeProwess.` automation
tests never run on a pull request; and the save chain's steps and the screen's routing are where they reach least.

WHAT IS CHECKED:

  1. the reading the slice rests on: the centre milestones carry the point thresholds, the branch keystones none; and
     Imperial Prowess is built in slice 8 with one extra option, no node waiting on a ruling about it;
  2. an option row counts while it is chosen or taken as the extra option, and its milestone's point is bought;
  3. the extra option's refusals come in their order;
  4. the allocation saves the two fields and a clear forgets them;
  5. the account record's step from 5 and the character record's newest step write no extra option into each
     allocation, keeping a value of the right kind and refusing anything else; neither record's number is written into a
     test beyond the migration and its fixtures;
  6. the committed files are the previous ones with the two fields and nothing else, and the README lists them;
  7. every test that runs an older file through the chain adds what the new steps write;
  8. the screen offers the extra option and takes it through its own call, and the game instance saves it.
"""

from __future__ import annotations

import csv
import json
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
NODES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv"
TREE_CPP = SOURCE / "Cataclysm" / "Player" / "CataclysmEmpireTree.cpp"
PASSIVE_H = SOURCE / "Cataclysm" / "Character" / "CataclysmPassiveTree.h"
RECORDS_H = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.h"
RECORDS_CPP = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.cpp"
INSTANCE = SOURCE / "Cataclysm" / "Player" / "CataclysmGameInstance.cpp"
WIDGET = SOURCE / "Cataclysm" / "Interface" / "CataclysmEmpireTreeWidget.cpp"
TESTS = SOURCE / "Cataclysm" / "Tests"
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def body(text: str, signature: str, where: str) -> str:
    """The braces-matched body of the function or namespace whose signature opens at `signature`."""
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[opening:index + 1]
    raise AssertionError(f"{signature!r} in {where} is not closed")


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def version_of(record: str) -> int:
    found = re.search(rf"class\s+CATACLYSM_API\s+{record}\s*:.*?SchemaVersionNow\s*=\s*(\d+)\s*;", code(RECORDS_H),
                      re.DOTALL)
    assert found, f"{record} states no version"
    return int(found.group(1))


def test_the_centre_milestones_carry_the_thresholds_and_imperial_prowess_is_built() -> None:
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    centre = sorted(int(row["Threshold"]) for row in rows if row["Kind"] == "capstone" and row["Branch"] == "Centre")
    keystones = {int(row["Threshold"]) for row in rows if row["Kind"] == "keystone"}
    assert centre == [25, 50, 100, 200], f"the centre milestones' thresholds are {centre}"
    assert keystones == {0}, (
        f"a branch keystone has a point threshold ({keystones}); the reading that the owner's 'keystones ... tier gated "
        "behind point thresholds' are the centre options no longer holds")
    assert gen.EMPIRE_TREE_BUILT.get("Imperial Prowess") == 8
    assert gen.EMPIRE_TREE_EFFECTS.get("Imperial Prowess") == [("ExtraEarlierMilestoneOption", 1.0)], (
        "Imperial Prowess does not give exactly one extra option (J2)")
    assert "prowess ruling" not in gen.EMPIRE_TREE_SYSTEMS and "prowess ruling" not in gen.EMPIRE_TREE_REFUSED.values()
    zenith = next(row for row in rows if row["Option1Name"] == "Imperial Prowess")
    assert "an additional keystone" in zenith["Option1Description"], zenith["Option1Description"]
    assert zenith["Option1WaitsOn"] == "", f"Imperial Prowess still waits on {zenith['Option1WaitsOn']!r}"


def test_an_option_counts_while_chosen_or_taken_as_the_extra_one() -> None:
    points = flat(body(code(TREE_CPP), "int32 PointsFor(const FCataclysmEmpireTreeEffectRow& Row,", "CataclysmEmpireTree.cpp"))
    assert ("Allocation.ChosenOptionIn(Node) == Row.Option || Allocation.ExtraOptionIn(Node) == Row.Option") in points, (
        "an option row does not count while it is the extra option")
    assert "return Held > 0 && bTaken ? 1 : 0;" in points, "an option counts without its milestone's point bought"


def test_the_extra_options_refusals_come_in_their_order() -> None:
    refusal = body(code(TREE_CPP), "FString UCataclysmEmpireTree::RefusalForChoosingExtraOption(", "CataclysmEmpireTree.cpp")
    order = ["is not a node of the empire upgrade tree", "can be taken only with", "offers no choice",
             "Choose 1, 2 or 3", "takes an option from a tier before", "own option must be chosen",
             "is already %s's choice", "cannot be chosen yet", "extra option is already taken"]
    found = [refusal.find(words) for words in order]
    assert all(at >= 0 for at in found), dict(zip(order, found, strict=True))
    assert found == sorted(found), f"the refusals are not in their order: {dict(zip(order, found, strict=True))}"
    assert "Row->Threshold >= GrantingRow->Threshold" in refusal, "an earlier tier is not one with a lower threshold"
    assert "Allocation.ExtraOption > 0" in refusal, "a second extra option is not refused (J2)"


def test_the_allocation_saves_the_two_fields_and_a_clear_forgets_them() -> None:
    header = code(PASSIVE_H)
    allocation = body(header, "struct CATACLYSM_API FCataclysmPassiveAllocation", "CataclysmPassiveTree.h")
    assert re.search(r"UPROPERTY\([^)]*SaveGame[^)]*\)\s*FName\s+ExtraOptionNode\s*;", allocation)
    assert re.search(r"UPROPERTY\([^)]*SaveGame[^)]*\)\s*int32\s+ExtraOption\s*=\s*0\s*;", allocation)
    clear = flat(body(allocation, "void Clear()", "FCataclysmPassiveAllocation"))
    assert "Nodes.Reset();" in clear and "ExtraOptionNode = NAME_None;" in clear and "ExtraOption = 0;" in clear, (
        "a clear, which a respec is, leaves the extra option behind (J5)")


def test_the_steps_write_no_extra_option_into_each_allocation() -> None:
    records = code(RECORDS_CPP)
    helper = flat(body(records, "bool WriteTheExtraOption(", "CataclysmSaveRecords.cpp"))
    for part in ('FString Node = TEXT("None");', "double Option = 0.0;", 'SetStringField(TEXT("ExtraOptionNode"), Node);',
                 'SetNumberField(TEXT("ExtraOption"), Option);'):
        assert part in helper, f"WriteTheExtraOption does not hold {part!r}"
    assert helper.count("return false;") == 3, "WriteTheExtraOption does not refuse each wrong kind of value"

    account_version = version_of("UCataclysmAccountSave")
    assert account_version >= 6, "the account record is below version 6"
    account = body(records, "namespace CataclysmAccountSaveMigration", "CataclysmSaveRecords.cpp")
    assert '{ 5, TEXT("Migrate_5_to_6"), &Migrate_5_to_6 },' in account
    assert 'WriteTheExtraOption(Record, TEXT("EmpireTree"), OutError)' in body(
        account, "bool Migrate_5_to_6(", "the account record's chain")

    # THE CHARACTER RECORD'S STEP IS `Migrate_11_to_12`, SETTLED ONCE THE GEMS WINDOW MERGED: this slice merged first, the
    # gems window then took the record to 13 and issue #2741 slice 9 to 14. Until then this check left the number out and
    # asked for the current version or one before it, which stopped holding at 14.
    character_version = version_of("UCataclysmCharacterSave")
    character = body(records, "namespace CataclysmCharacterSaveMigration", "CataclysmSaveRecords.cpp")
    steps = re.findall(r"bool (Migrate_(\d+)_to_(\d+))\(const TSharedRef<FJsonObject>& Record, FString& OutError\)\s*\{"
                       r"(?:(?!bool Migrate_).)*?WriteTheExtraOption", character, re.DOTALL)
    assert len(steps) == 1, f"the character record has {len(steps)} steps writing the extra option"
    name, before, after = steps[0]
    assert name == "Migrate_11_to_12" and int(after) <= character_version, (name, character_version)
    step = flat(body(character, f"bool {name}(", "the character record's chain"))
    for key in ("PassiveAllocation", "PrivateEmpireTree"):
        assert f'WriteTheExtraOption(Record, TEXT("{key}"), OutError)' in step, f"{name} leaves {key} out"
    assert 'TEXT("ConsumptionBuild")' in step and 'TEXT("PassiveAllocation"), OutError)' in step, (
        f"{name} leaves the held build's allocation out")
    assert f'{{ {before}, TEXT("{name}"), &{name} }},' in character, f"the character chain does not list {name}"


def test_no_test_writes_the_character_records_new_number() -> None:
    version = version_of("UCataclysmCharacterSave")
    # ONE TEST AT A TIME, AND ONLY ONE READING A CHARACTER RECORD: the run record is at 12 as well, and its tests state it.
    written = sorted(f"{path.name}: {test.split(',')[0].strip()}" for path in TESTS.glob("*.cpp")
                     for test in code(path).split("IMPLEMENT_SIMPLE_AUTOMATION_TEST(")[1:]
                     if "Cast<UCataclysmCharacterSave>" in test
                     and re.search(rf"Read->SchemaVersion,\s*{version}\)", test))
    assert not written, (f"{written} write the character record's version, {version}, as a number; gems slice G2 also "
                         "moves it, so read UCataclysmCharacterSave::SchemaVersionNow")


def test_the_committed_files_are_the_previous_ones_with_the_two_fields() -> None:
    for old, new, paths in (("Account_v5.json", "Account_v6.json", [["EmpireTree"]]),
                            ("Character_v11.json", "Character_v12.json",
                             [["PassiveAllocation"], ["PrivateEmpireTree"], ["ConsumptionBuild", "PassiveAllocation"]])):
        before = json.loads((FIXTURES / old).read_text(encoding="utf-8"))
        after = json.loads((FIXTURES / new).read_text(encoding="utf-8"))
        assert after["SchemaVersion"] == before["SchemaVersion"] + 1
        held = []
        for path in paths:
            allocation = after
            for key in path:
                allocation = allocation[key]
            held.append((allocation.pop("ExtraOptionNode"), allocation.pop("ExtraOption")))
        empire = held[0] if new.startswith("Account") else held[1]
        assert empire[0] != "None" and empire[1] > 0, (
            f"{new}'s empire tree holds {empire}: None and 0 are the defaults and the migration's, so they prove nothing read")
        after["SchemaVersion"] = before["SchemaVersion"]
        assert after == before, f"{new} differs from {old} beyond its version and the extra options"
    readme = (FIXTURES / "README.md").read_text(encoding="utf-8")
    assert "| `Account_v6.json` |" in readme and "| `Character_v12.json` |" in readme


def test_every_chain_test_adds_what_the_new_steps_write() -> None:
    missing = []
    for path in sorted(TESTS.glob("*.cpp")):
        text = code(path)
        for old, new in (("AddTheCharactersVersion11Defaults", "AddTheCharactersVersion12Defaults"),
                         ("AddTheAccountsVersion5Defaults", "AddTheAccountsVersion6Defaults")):
            if text.count(f"{old}(*Expected)") != text.count(f"{new}(*Expected)"):
                missing.append(f"{path.name}: {old} {text.count(old)}, {new} {text.count(new)}")
    assert not missing, "a test runs an older file through the chain without the new step's defaults:\n" + "\n".join(missing)


def test_the_screen_offers_the_extra_option_and_the_game_instance_saves_it() -> None:
    widget = code(WIDGET)
    clicked = flat(body(widget, "void UCataclysmEmpireTreeWidget::HandleBranchClicked(FName Value)", "the tree screen"))
    assert ("if (bChoosingExtraOption) { ChooseExtraOption(ChoosingOptionFor, Option); } else { ChooseOption(ChoosingOptionFor, "
            "Option); }") in clicked, "a click in the list does not take the extra option through its own call"
    touched = flat(body(widget, "bool UCataclysmEmpireTreeWidget::TouchNode(FName Node)", "the tree screen"))
    assert "UCataclysmEmpireTree::OffersAnExtraOption(" in touched and "bChoosingExtraOption = true;" in touched, (
        "a click on an earlier milestone does not offer the extra option")
    chosen = flat(body(widget, "bool UCataclysmEmpireTreeWidget::ChooseExtraOption(FName Node, int32 Option)", "the tree screen"))
    assert "UCataclysmGameInstance::ChooseEmpireTreeExtraOption(" in chosen
    instance = flat(body(code(INSTANCE), "bool UCataclysmGameInstance::ChooseEmpireTreeExtraOption(", "the game instance"))
    for part in ("EmpireTreeChangeRefusal(WorldContext)", "UCataclysmEmpireTree::ChooseExtraOption(",
                 "RefreshEmpireTreeBonuses(WorldContext);", "NoteTheEmpireTreeChanged(WorldContext, Held);"):
        assert part in instance, f"the game instance's extra option does not hold {part!r}"
