"""A trip between the capital and a dungeon is a save and a load, and the run carries what the map shows after one.

WHY THIS EXISTS. Issue #2733 slice 3, ruled by the coordinating session on
2026-11-17. The capital's Walk takes the character to `L_Dungeon`, and the end of
a walk takes it back to `L_Capital`; the departing level writes both records and
the arriving one loads them, as a Continue does. So whatever the run record does
not hold, a trip loses: the run record went to version 6 to carry the last
defeat's empire points (Q3), the last clear and what the days away did (Q6). And
the day loop records what each hit took (`FCataclysmDayReport::Hits`), ADDITIVE
ONLY, at the three places a city is damaged.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the
`Cataclysm.Travel.` and `Cataclysm.EmpireRun.` automation tests never run on a
pull request. Reading the source as text does.

WHAT IS CHECKED, with comments removed:

  1. the run record is at version 6 or later, and `Migrate_5_to_6` is one of its
     steps and writes the five fields' "nothing yet" values;
  2. the gather writes and the restore reads all five;
  3. each of the three `Map->Damage` calls in the day loop records its hit, and
     there are exactly three, so a new place a city is damaged must record too;
  4. recording a hit changes nothing but the report's list of hits;
  5. `TravelTo` writes the records before it carries the character, and opens
     no level in an automation test.

WHAT IS NOT. That the records load, that the hits add up to what a city lost,
or what the map's line says; the automation tests above do.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
RECORDS_H = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.h"
RECORDS_CPP = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.cpp"
GATHER_CPP = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveGather.cpp"
APPLY_CPP = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveApply.cpp"
RUN_CPP = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmEmpireRun.cpp"
GAME_MODE_CPP = SOURCE / "Cataclysm" / "Player" / "CataclysmGameMode.cpp"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"

#: The run record's version 6 fields, and what `Migrate_5_to_6` writes for each.
VERSION_6 = {
    "LastEmpirePointsAwarded": 'SetNumberField(TEXT("LastEmpirePointsAwarded"), 0.0)',
    "LastClearDay": 'SetNumberField(TEXT("LastClearDay"), INDEX_NONE)',
    "LastClearDungeonName": 'SetStringField(TEXT("LastClearDungeonName"), FString())',
    "LastClearWalkDays": 'SetNumberField(TEXT("LastClearWalkDays"), 0)',
    "LastDaysAway": 'SetObjectField(TEXT("LastDaysAway"), NoneAway)',
}


def strip_comments(text: str) -> str:
    """C++ with its `//` and `/* */` comments removed, string literals kept."""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j == -1 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j == -1 else j + 2
            out.append(" ")
        else:
            out.append(c)
            i += 1
    return "".join(out)


def squeeze(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def code(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. If it moved, move it here too.")
    return squeeze(strip_comments(path.read_text(encoding="utf-8")))


def braced_from(text: str, start: int, what: str) -> str:
    open_at = text.find("{", start)
    assert open_at != -1, f"{what} has no opening brace"
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1 : i]
    raise AssertionError(f"{what} is never closed")


def function_body(text: str, signature: str) -> str:
    assert text.count(signature) == 1, f"{signature} should be defined once; it appears {text.count(signature)} times"
    return braced_from(text, text.find(signature), signature)


def test_the_run_record_is_at_version_6_or_later_and_migrate_5_to_6_writes_nothing_yet() -> None:
    """AT VERSION 6 OR LATER, and the step one of the run record's: until issue #2742 took the run record to version 7
    (2026-11-18) this asked for version 6 and for `Migrate_5_to_6` to be the last step."""
    run_record = code(RECORDS_H).split("class CATACLYSM_API UCataclysmRunSave")[1]
    version = re.search(r"static constexpr int32 SchemaVersionNow = (\d+);", run_record)
    assert version and int(version.group(1)) >= 6, "the run record is not at version 6 or later"
    records = code(RECORDS_CPP)
    assert '{ 5, TEXT("Migrate_5_to_6"), &Migrate_5_to_6 },' in records, (
        "Migrate_5_to_6 is not one of the run record's steps")
    # THE RUN RECORD'S STEP: the character record has a `Migrate_5_to_6` of its own, in its own namespace.
    run_steps = records.split("namespace CataclysmRunSaveMigration")[1]
    step = function_body(run_steps, "bool Migrate_5_to_6(const TSharedRef<FJsonObject>& Record, FString& OutError)")
    for field, written in VERSION_6.items():
        assert written in step, f"Migrate_5_to_6 does not write {field} as {written}"
    assert (FIXTURES / "Run_v6.json").is_file(), "there is no committed Run_v6.json for the run record's version 6"


@pytest.mark.parametrize("field", sorted(VERSION_6))
def test_the_gather_writes_and_the_restore_reads_each_version_6_field(field: str) -> None:
    gather = function_body(code(GATHER_CPP),
                           "bool FCataclysmSaveGather::ProgressFrom(const UCataclysmEmpireRun& Run, UCataclysmRunSave& Record)")
    assert f"Record.{field} = Run.{field};" in gather, f"the run record's gather does not write {field}"
    restore = code(APPLY_CPP)
    reads = f"Run.{field} = Record.{field};" in restore or (
        field.startswith("LastClear") and "Run.NoteClear(Record.LastClearDay, Record.LastClearDungeonName, "
                                          "Record.LastClearWalkDays);" in restore)
    assert reads, f"the run's restore does not read {field}"


def test_every_place_a_city_is_damaged_records_its_hit() -> None:
    run = code(RUN_CPP)
    assert run.count("Map->Damage(") == 3, (
        f"the day loop damages a city in {run.count('Map->Damage(')} places, not the 3 that record their hits. A new "
        "one must record what it takes in FCataclysmDayReport::Hits.")
    assert run.count("const FHitRecordedOnLeaving RecordTheHit(OutReport, Map.Get(), HostId, "
                     "ECataclysmCityHitKind::QuestTimer);") == 1, "the Quest timer's hit is not recorded"
    assert run.count("NoteHitIn(OutReport, Map.Get(), BeforeTheHit, ECataclysmCityHitKind::Ordinary);") == 1, (
        "the ordinary dungeon's hit is not recorded")
    assert run.count("NoteHitIn(OutReport, Map.Get(), BeforeTheHit, ECataclysmCityHitKind::SiegeShare);") == 1, (
        "the Siege's daily share is not recorded")
    assert run.count("const FCityBeforeAHit BeforeTheHit = BeforeAHit(Map.Get(), CityId);") == 2, (
        "the ordinary hit and the Siege's share do not read their city before the damage")


def test_recording_a_hit_changes_nothing_but_the_list_of_hits() -> None:
    body = function_body(code(RUN_CPP), "void NoteHitIn(")
    calls = set(re.findall(r"(?:->|\.)(\w+)\(", body))
    # `DefenceOf` AND `PopulationOf` ONLY READ: the city as the game reads it, never more than its maximum with the
    # empire upgrade tree, so a hit after a respec lowered a maximum records what it took. Issue #2741 slice 3a.
    assert calls <= {"Find", "AddDefaulted_GetRef", "DefenceOf", "PopulationOf"}, (
        f"NoteHitIn calls {sorted(calls)}; recording a hit is additive only (ruled 2026-11-17), so it may only read "
        "the city and add to the report's hits.")
    # EVERY ASSIGNMENT is to the new hit's own fields, or declares the city read or the hit added.
    allowed = ("Hit.", "const FCataclysmCity* After =", "FCataclysmCityHit& Hit = OutReport.Hits.AddDefaulted_GetRef()")
    for statement in (s.strip().lstrip("{} ") for s in body.split(";")):
        if re.search(r"[^=!<>]=[^=]", statement):
            assert statement.startswith(allowed), f"NoteHitIn assigns something other than the new hit: {statement!r}"


def test_travel_writes_the_records_before_it_carries_the_character() -> None:
    body = function_body(code(GAME_MODE_CPP), "bool ACataclysmGameMode::TravelTo(const TCHAR* Map, int32 WalkDungeonId)")
    handed = body.index("const FGuid Leaving = Writer->HandOver();")
    carried = body.index("Instance->PendingCharacterId = Leaving;")
    assert handed < carried, "TravelTo carries the character before its records are written"
    assert "if (!Leaving.IsValid()) {" in body[handed:carried], (
        "TravelTo does not stop when the records could not be written, so the next level would load older ones")
    assert "if (!GIsAutomationTesting) { UGameplayStatics::OpenLevel(this, FName(Map)); }" in body, (
        "TravelTo opens a level in an automation test, or opens none in play")


def test_a_change_of_floor_writes_even_after_another_write_this_frame() -> None:
    """A change of floor replaces what an earlier write in its frame gathered, and an arrival raises it once.

    WINDOW TRAVEL-W1'S FIRST RUN, 2026-11-18: the walk an arrival begins was written as `ChangedFloor`, and the run
    record's once-a-frame rule skipped it behind an earlier write in the same frame, so the record on disk still said
    the capital. Ruled that day: `ChangedFloor` writes whatever else wrote the run record that frame. And the arrival
    raises it only when entering the dungeon did not already, so it is raised once.
    """
    triggers = code(SOURCE / "Cataclysm" / "Save" / "CataclysmSaveTriggers.cpp")
    body = function_body(triggers, "bool UCataclysmSaveTriggers::WritesEvenIfTheRunWasWrittenThisFrame(")
    assert "ECataclysmSaveTrigger::ChangedFloor" in body, (
        "a change of floor obeys the once-a-frame rule again, and the walk an arrival begins can go unwritten")
    dungeon = code(SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp")
    arrival = function_body(dungeon, "bool ACataclysmDungeonGameMode::EnterThePendingWalkIfReady()")
    assert ("if (Writer->FloorChangesNoted() == FloorChangesBefore) { "
            "Writer->NoteTrigger(ECataclysmSaveTrigger::ChangedFloor); }") in arrival, (
        "the arrival raises the change of floor whether or not entering the dungeon already did")
