"""The game's Dungeon Score is the simulation's, and the player's Power Score is shown where the design asks.

WHY THIS EXISTS. Issue #2824, slice a, and the owner's "Dungeons also need their power scores as well" (2026-11-22).
`UCataclysmEnemyScore::DungeonScoreFor` (`game/Source/Cataclysm/Dungeon/CataclysmEnemyScore.cpp`) is a port of
`sim/cataclysm_sim/scoring.py`'s `dungeon_score`, the design's "Dungeon Score Formula". Continuous integration compiles
no C++, so the automation test `Cataclysm.PowerScoreLive.TheDungeonScoreIsTheSimulationsOnSixDungeons` never runs on a
pull request; it holds six figures written out, and this check holds those figures to the simulation. Two copies of a
number are two numbers.

WHAT IS CHECKED:

  1. each row of the automation test's table is `scoring.dungeon_score` for the same dungeon, and the table holds the
     simulation's own self-test figure, 201;
  2. the port reads its weights from the rarity table's `SpawnWeight`, and those weights are the simulation's
     `DUNGEON_SCORE_MIX`, Cataclysm Boss at 0;
  3. every screen that offers a dungeon calls the score functions, which no automation test can see (-nullrhi, #650):
     the character sheet's status line and its tooltip, the empire map's city detail, the city screen's dungeon rows,
     and the floor panel while an empire dungeon is walked.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
TEST = SOURCE / "Tests" / "CataclysmPowerScoreLiveTests.cpp"

if str(REPO_ROOT / "sim") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "sim"))

from cataclysm_sim import scoring  # noqa: E402

#: The C++ enum names, as the simulation spells the same kinds.
TYPES = {"Basic": "Basic", "Quest": "Quest", "FallenCity": "Fallen City", "Cataclysm": "Cataclysm"}
SUBTYPES = {"None": "None", "Timed": "Timed", "Horde": "Horde", "Siege": "Siege", "CowLevel": "Cow Level",
            "Elite": "Elite", "Volatile": "Volatile", "Sacrificial": "Sacrificial"}

ROW = re.compile(r"\{\s*(\d+),\s*ECataclysmDungeonType::(\w+),\s*ECataclysmDungeonSubType::(\w+),\s*(\d+),\s*"
                 r"([\d.]+)f,\s*(\d+)\s*\}")


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


def test_the_automation_tests_figures_are_the_simulations() -> None:
    rows = ROW.findall(code(TEST))
    assert len(rows) == 6, f"the automation test's table holds {len(rows)} rows, not 6"
    wrong = []
    for tier, kind, subtype, floors, modifiers, expected in rows:
        got = scoring.dungeon_score(int(tier), TYPES[kind], SUBTYPES[subtype], int(floors), float(modifiers))
        if got != int(expected):
            wrong.append((tier, kind, subtype, floors, modifiers, expected, got))
    assert not wrong, f"rows whose figure is not the simulation's (row..., written, simulation): {wrong}"
    assert ("1", "Basic", "Timed", "50", "0.0", "201") in rows, "the table no longer holds the simulation's self-test"


def test_the_port_reads_the_rarity_tables_weights_which_are_the_simulations() -> None:
    port = body(code(SOURCE / "Dungeon" / "CataclysmEnemyScore.cpp"), "int32 UCataclysmEnemyScore::DungeonScoreFor(")
    assert "UCataclysmEnemyRarity::SpawnWeightForStep(Rarities, Step)" in port
    assert "ScoreFor(Middle, Step)" in port
    assert "FMath::FloorToDouble(Sum + 0.5)" in port, "the total is not rounded half up as the model rounds it"
    with (REPO_ROOT / "game" / "Data" / "EnemyRarities.csv").open(encoding="utf-8", newline="") as handle:
        table = {row["Name"]: float(row["SpawnWeight"]) for row in csv.DictReader(handle)}
    mix = dict(scoring.DUNGEON_SCORE_MIX)
    for name, weight in mix.items():
        assert table.get(name) == weight, f"{name}: the rarity table weighs {table.get(name)}, the simulation {weight}"
    assert all(weight == 0.0 for name, weight in table.items() if name not in mix), (
        f"a rarity outside the simulation's mix carries a weight: {table}")


def test_every_screen_that_offers_a_dungeon_shows_both_scores() -> None:
    interface = SOURCE / "Interface"
    sheet = body(code(interface / "CataclysmCharacterSheetWidget.cpp"), "void UCataclysmCharacterSheetWidget::WriteStatus(")
    assert "UCataclysmPowerScore::LiveBreakdownOf(" in sheet
    assert "UCataclysmCharacterSheetLayout::PowerScoreLine(Power.Total)" in sheet
    assert "SetToolTipText(FText::FromString(UCataclysmCharacterSheetLayout::PowerScoreTooltip(Power)))" in sheet
    for widget in ("CataclysmEmpireMapWidget.cpp", "CataclysmCityScreenWidget.cpp"):
        text = flat(code(interface / widget))
        assert "UCataclysmPowerScore::LiveScoreOf(" in text, widget
        assert "UCataclysmEmpireMapLayout::DungeonScoreLine(Run, " in text, widget
    walked = body(code(SOURCE / "Dungeon" / "CataclysmDungeonGameMode.cpp"),
                  "void ACataclysmDungeonGameMode::RefreshFloorModifierPanel(")
    assert "UCataclysmEnemyScore::DungeonScoreFor(" in walked
    assert "LiveCountsForTheFloor(), ScoreLine)" in walked
