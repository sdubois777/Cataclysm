"""Clearing the Cataclysm boss dungeon wins the run. Issue #1324 slice 6e.

Ruled by the coordinating session on 2026-11-02 under the project owner's
delegation: clearing the Cataclysm-type dungeon, through
`UCataclysmEmpireRun::ClearDungeon` as every dungeon is cleared, is the win (R5's
half that is this slice's); a win at tier T pays the first-kill 10 passive points
for the boss of the T-th Cataclysm in the character's draw (R4); the walk's owed
days are not run after a win (R6); the next run begins at once, the same
character, tier+1, facing its draw plus one, with a win line (R7). And the
coordinating session's comment of 2026-11-01: the tier rises "before the
character record is written".

WHY THIS IS CHECKED FROM PYTHON AS WELL. Continuous integration compiles no C++,
so `Cataclysm.WinningTheRun.` and `Cataclysm.BossClearedWins.` never run on a
pull request. This file checks the order of the steps and the save fixtures from
the text, which is what can be checked without the engine; the automation tests
check what the steps do.

EVERY CHECK READS CODE WITH ITS COMMENTS REMOVED, so a comment naming a call
cannot pass for the call.
"""

from __future__ import annotations

import json
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
EMPIRE_RUN = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmEmpireRun.cpp"
GAME_INSTANCE = SOURCE / "Cataclysm" / "Player" / "CataclysmGameInstance.cpp"
DUNGEON_MODE = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"
TRIGGERS = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveTriggers.cpp"
SAVE_RECORDS_CPP = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.cpp"
SAVE_RECORDS_H = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.h"

WIN_KEYS = {
    "LastRunWonDay": 381,
    "LastRunWonTier": 3,
    "LastRunWonCataclysm": "War",
    "LastRunWonPassivePoints": 10,
}


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def body(path: pathlib.Path, signature: str) -> str:
    """The body of the function whose definition starts with `signature`, braces matched, comments removed."""
    text = code(path)
    start = text.find(signature)
    assert start != -1, f"{path.name} no longer defines {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    raise AssertionError(f"{signature!r} in {path.name} is not closed")


def position(text: str, needle: str, where: str) -> int:
    """Where `needle` occurs in `text`, which it must, once."""
    count = text.count(needle)
    assert count == 1, f"{where} holds {needle!r} {count} times, not once"
    return text.index(needle)


def test_a_won_run_raises_the_tier_before_it_writes_the_records() -> None:
    """Points, tier, next run, write: the order of the 2026-11-01 comment and R7."""
    won = body(GAME_INSTANCE, "UCataclysmEmpireRun* UCataclysmGameInstance::BeginTheRunAfterAWin(")
    where = "UCataclysmGameInstance::BeginTheRunAfterAWin"

    paid = position(won, "RecordCataclysmBossDefeat(", where)
    raised = position(won, "RaiseDifficultyTierForAWin()", where)
    begun = position(won, "BeginACharactersEmpireRun(", where)
    written = position(won, "ECataclysmSaveTrigger::RunWon", where)

    assert paid < raised, "the tier is raised before the first-kill points are paid"
    assert raised < written, (
        "the records are written before the tier is raised, so the character record holds the tier the run was won "
        "at; the comment of 2026-11-01 on #1324 says the tier rises before the record is written")
    assert begun < written, "the records are written before the next run begins, so the run record is the won run's"
    assert raised < begun, "the next run is begun before the tier is raised, so it is begun at the won run's tier"


def test_the_win_pays_for_the_tiers_cataclysm_in_the_characters_draw() -> None:
    """R4: `OrderFor(the character's seed)[T-1]`, T the won run's tier."""
    won = body(GAME_INSTANCE, "UCataclysmEmpireRun* UCataclysmGameInstance::BeginTheRunAfterAWin(")

    assert "UCataclysmRoster::OrderFor(OrderSeed)" in won, "the boss is not read from the character's draw"
    assert re.search(r"Order\[TierWon - 1\]", won), "the boss is not the T-th Cataclysm of the draw"
    assert re.search(r"RecordCataclysmBossDefeat\(UCataclysmRoster::NameFor\(Against\)\)", won), (
        "the boss defeated is not the one the draw puts at the tier")
    assert "UCataclysmPassivePoints::PerFirstBossKill : 0" in won, "a boss defeated before does not pay nothing"
    assert re.search(r"BeginACharactersEmpireRun\(Seed, OrderSeed, Mode, LethalityRung, TierNow\)", won), (
        "the next run is not begun from the character's seed at the new tier")


def test_clearing_the_cataclysm_kind_wins_last_and_drops_the_owed_days() -> None:
    """The clear is the win, set after everything else the clear does, as `Simulation._finish_current` sets `won`."""
    cleared = body(EMPIRE_RUN, "bool UCataclysmEmpireRun::ClearDungeon(")
    where = "UCataclysmEmpireRun::ClearDungeon"

    win = re.search(
        r"if \(Kind == ECataclysmDungeonType::Cataclysm\)\s*\{\s*bWon = true;\s*WonDay = Day\(\) \+ OwedDays;\s*"
        r"OwedDays = 0;\s*\}", cleared)
    assert win, "clearing a Cataclysm dungeon no longer sets the win, its day and drops the owed days, in that order"
    assert position(cleared, "OpenCataclysmDungeonIfEarned();", where) < win.start(), (
        "the win is set before the quest branch, not last")
    assert position(cleared, "Map->Retake(CityId);", where) < win.start(), "the win is set before the retake"


def test_the_game_mode_hands_a_win_on_after_leaving_the_walk() -> None:
    """Cleared first, left second -- which runs no owed day after a win -- and then the character's half."""
    cleared = body(DUNGEON_MODE, "bool ACataclysmDungeonGameMode::ClearEmpireDungeon(")
    where = "ACataclysmDungeonGameMode::ClearEmpireDungeon"

    clear = position(cleared, "Run->ClearDungeon(EmpireDungeonId)", where)
    leave = position(cleared, "LeaveEmpireDungeon();", where)
    hand = position(cleared, "BeginTheRunAfterAWin(", where)
    assert clear < leave < hand, "the win is not handed on after the clear and the leaving"
    assert re.search(r"if \(bCleared && Run->bWon\)", cleared), "the character's half does not wait for a win"


def test_the_win_trigger_writes_the_character_record_whatever_wrote_this_frame() -> None:
    """`RunWon` writes the character record, and the run record even after another write this frame."""
    character = body(TRIGGERS, "bool UCataclysmSaveTriggers::WritesTheCharacterRecord(")
    assert "case ECataclysmSaveTrigger::RunWon:" in character, "a won run does not write the character record"

    again = body(TRIGGERS, "bool UCataclysmSaveTriggers::WritesEvenIfTheRunWasWrittenThisFrame(")
    assert "ECataclysmSaveTrigger::RunWon" in again, (
        "a won run's write can be skipped by a write earlier in the frame, leaving the won run on disk")

    listed = body(TRIGGERS, "TArray<ECataclysmSaveTrigger> UCataclysmSaveTriggers::Every(")
    assert "ECataclysmSaveTrigger::RunWon" in listed, "the trigger is not in the list its tests walk"


def test_run_v5_is_run_v4_with_the_win_line_and_nothing_else() -> None:
    """The fixture rule: the new file is the old one at the new version, plus the new keys, none at its default."""
    before = json.loads((FIXTURES / "Run_v4.json").read_text(encoding="utf-8"))
    after = json.loads((FIXTURES / "Run_v5.json").read_text(encoding="utf-8"))

    assert before["SchemaVersion"] == 4
    assert after["SchemaVersion"] == 5
    for key, value in WIN_KEYS.items():
        assert key not in before, f"Run_v4.json holds {key}, which version 4 did not have"
        assert after.get(key) == value, f"Run_v5.json holds {after.get(key)!r} for {key}, not {value!r}"

    rest = {key: value for key, value in after.items() if key not in WIN_KEYS}
    rest["SchemaVersion"] = 4
    assert rest == before, "Run_v5.json differs from Run_v4.json beyond its version and the four keys"

    # NONE AT ITS DEFAULT, and the draw it describes is the run's own: at tier 4 facing four, the run before was at
    # tier 3, whose Cataclysm is the third of the list.
    assert after["DifficultyTier"] == after["LastRunWonTier"] + 1
    assert after["ActiveCataclysms"][after["LastRunWonTier"] - 1] == after["LastRunWonCataclysm"]


def test_the_run_record_is_at_version_5_and_migrate_4_to_5_writes_not_won() -> None:
    """The step is registered, one version wide, and writes the four defaults."""
    assert re.search(r"static constexpr int32 SchemaVersionNow = 5;", code(SAVE_RECORDS_H)), (
        "the run record is not at version 5")
    records = code(SAVE_RECORDS_CPP)
    assert '{ 4, TEXT("Migrate_4_to_5"), &Migrate_4_to_5 },\n\t};' in records.replace("\r\n", "\n"), (
        "Migrate_4_to_5 is not the run record's last step")

    step = body(SAVE_RECORDS_CPP, "bool Migrate_4_to_5(const TSharedRef<FJsonObject>& Record, FString& OutError)\n"
                                  "\t{\n\t\tOutError.Empty();\n\t\tRecord->SetNumberField(TEXT(\"LastRunWonDay\")")
    for written in ('SetNumberField(TEXT("LastRunWonDay"), 0)', 'SetNumberField(TEXT("LastRunWonTier"), 0)',
                    'SetStringField(TEXT("LastRunWonCataclysm"), TEXT("None"))',
                    'SetNumberField(TEXT("LastRunWonPassivePoints"), 0)'):
        assert written in step, f"Migrate_4_to_5 does not write {written}"
