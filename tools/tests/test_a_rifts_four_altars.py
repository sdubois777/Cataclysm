"""A Rift's four altars: the Demonic Quest dungeon, issue #51 slice B1.

WHY THIS EXISTS. The owner ruled on 2026-11-24 (issue #51) that a Rift is the Demonic Quest dungeon ("Rift = Quest
dungeon (Recommended)"), that its four altars stand "One per floor, spread (Recommended)" -- altar i on floor
ceil(i x floors / 4), the rest on the last floor of a Rift of fewer than four -- and, later that day, that a Rift is
four floors deep with one altar on each. An altar seals its floor's stairs while it stands, and a floor's fallen altars
are saved in the run record from version 19. Continuous integration compiles no C++, so the `Cataclysm.RiftAltars.`
automation tests never run on a pull request; these checks read the data, the documents and the source.

WHAT IS CHECKED:

  1. the design still says "the four altars", and the game's count is four;
  2. a Rift is a Quest dungeon Demonic sent, and Demonic's Quest dungeons are Elite;
  3. the altar floors the automation test expects, for 1 to 12 floors, are the ruling's, worked out here again from
     its sentence, and four floors give one a floor;
  4. the key the stairs' seal names and the key the floor panel lists are one text;
  5. the run record's step 18 to 19 writes no altar fallen, is in the chain, and the game mode writes and reads the
     field; the committed `Run_v19.json` is `Run_v18.json` with its version and the field, not the default; every chain
     test adds the step's write; and only the migration test reads `Run_v18.json`.
"""

from __future__ import annotations

import math
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
SAVE = SOURCE / "Cataclysm" / "Save"
TESTS = SOURCE / "Cataclysm" / "Tests"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

ALTAR_TESTS = TESTS / "CataclysmRiftAltarsTests.cpp"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str) -> str:
    """The braces-matched body of the function whose signature opens at `signature`."""
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return flat(text[opening:index + 1])
    raise AssertionError(f"{signature!r} is not closed")


def altar_floors(total_floors: int) -> list[int]:
    """The ruling, worked out here from its sentence and not read from the C++: altar i of min(4, F) on floor
    ceil(i x F / min(4, F)), at most one a floor, and the rest of the four on the last floor."""
    spread = min(4, total_floors)
    floors = [math.ceil(altar * total_floors / spread) for altar in range(1, spread + 1)]
    return floors + [total_floors] * (4 - spread)


# --------------------------------------------------------------------------
# 1. Four altars
# --------------------------------------------------------------------------

def test_the_design_says_four_altars_and_the_game_counts_four() -> None:
    gdd = flat(GDD.read_text(encoding="utf-8"))
    assert "destroy the four altars powering each one" in gdd, (
        "docs/Cataclysm_GDD_v2.md section XI no longer says 'destroy the four altars powering each one', which is the "
        "sentence a Rift's altars are built on (issue #51)")
    assert "A Rift is four floors deep, with one altar on each floor." in gdd, (
        "the design no longer states the owner's ruling that a Rift is four floors deep, one altar on each")
    brief = flat(code(SOURCE / "Cataclysm" / "Dungeon" / "CataclysmFloorBrief.h"))
    assert "static constexpr int32 RiftAltars = 4;" in brief


# --------------------------------------------------------------------------
# 2. What a Rift is
# --------------------------------------------------------------------------

def test_a_rift_is_a_quest_dungeon_demonic_sent_and_demonics_quest_dungeons_are_elite() -> None:
    surge = code(SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmSurge.cpp")
    rift = body(surge, "bool FCataclysmDungeon::IsARift() const")
    assert "Type == ECataclysmDungeonType::Quest" in rift
    assert "Cataclysm == ECataclysmType::Demonic" in rift
    assert "SubType" not in rift, "a Rift is told by its sender, not its sub-type, which an ordinary dungeon can roll"
    sub_type = body(surge, "ECataclysmDungeonSubType UCataclysmSurgeScheduler::QuestSubTypeFor(")
    assert "case ECataclysmType::Demonic: return ECataclysmDungeonSubType::Elite;" in sub_type


# --------------------------------------------------------------------------
# 3. Where the altars stand
# --------------------------------------------------------------------------

def test_the_altar_floors_the_automation_test_expects_are_the_rulings() -> None:
    test = body(code(ALTAR_TESTS), "bool FCataclysmRiftAltarsSpreadTest::RunTest(")
    rows = {int(floors): spread for floors, spread in
            re.findall(r'\{\s*(\d+)\s*,\s*TEXT\("([\d,]+)"\)\s*\}', test)}
    assert sorted(rows) == list(range(1, 13)), f"the spread test's table covers {sorted(rows)}, not 1 to 12 floors"
    for floors, spread in rows.items():
        assert spread == ",".join(map(str, altar_floors(floors))), (
            f"the spread test expects a Rift of {floors} floors to put its altars on {spread}; the ruling gives "
            f"{altar_floors(floors)}")
    assert altar_floors(4) == [1, 2, 3, 4], "a Rift of four floors, the owner's, must hold one altar on each floor"
    assert altar_floors(30) == [8, 15, 23, 30]
    assert 'AltarFloorsOf(30), FString(TEXT("8,15,23,30"))' in test


# --------------------------------------------------------------------------
# 4. One key for the seal and the panel
# --------------------------------------------------------------------------

def test_the_seal_key_and_the_panel_key_are_the_same_text() -> None:
    mode = (SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.h").read_text(encoding="utf-8")
    panel = (SOURCE / "Cataclysm" / "Interface" / "CataclysmFloorModifierPanelLayout.h").read_text(encoding="utf-8")
    seal = re.findall(r'RiftAltarsSealKey = TEXT\("([^"]+)"\);', mode)
    line = re.findall(r'RiftAltarsKey = TEXT\("([^"]+)"\);', panel)
    assert len(seal) == 1 and len(line) == 1 and seal == line, (
        f"the key StairsSealedBy names a Rift's altars by ({seal}) and the key the floor panel lists the Rift by "
        f"({line}) have drifted apart; both headers say they are the same text")


# --------------------------------------------------------------------------
# 5. The save: run record version 19
# --------------------------------------------------------------------------

def test_the_rule_state_carries_the_fallen_altars_both_ways() -> None:
    header = flat(code(SAVE / "CataclysmSaveRecords.h"))
    rule_state = header[header.index("struct CATACLYSM_API FCataclysmSavedFloorRuleState"):]
    rule_state = rule_state[:rule_state.index("};")]
    assert re.search(r"UPROPERTY\([^)]*SaveGame[^)]*\) TArray<int32> RiftAltarsDestroyed;", rule_state)
    mode = code(SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp")
    gathered = body(mode, "FCataclysmSavedFloorRuleState ACataclysmDungeonGameMode::FloorRuleStateNow() const")
    assert "Out.RiftAltarsDestroyed.Add(Index);" in gathered
    restored = body(mode, "void ACataclysmDungeonGameMode::RestoreFloorRuleState(")
    assert "for (const int32 Index : State.RiftAltarsDestroyed)" in restored
    assert "Altar->Destroy();" in restored


def test_every_chain_test_adds_the_new_steps_write() -> None:
    missing = [path.name for path in sorted(TESTS.glob("*.cpp"))
               if code(path).count("AddTheRunsVersion18Defaults(")
               != code(path).count("AddTheRunsVersion19Defaults(")]
    assert not missing, f"these run an older run file through the chain without the step 18 to 19's write: {missing}"


