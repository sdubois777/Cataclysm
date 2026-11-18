"""The capital level, and Play opening the front end. Issue #2733 slice 2.

The project owner's rulings of 2026-11-17 on #2733: "Play opens the front end (character select and creation). This
replaces the 2026-11-01 choice to keep the editor on L_Sandbox." and "The empire map's home is a plain capital level,
`L_Capital`. It is a new, nearly empty level that opens the empire map as soon as the player arrives. Character
select's Start and Continue go there." The coordinating session's rulings of 2026-11-17: a record saved on the
sandbox's floor loads in the capital without that floor (Q1); Play on the capital with nothing pending begins a run
(Q2).

WHY THIS IS CHECKED FROM PYTHON AS WELL. The group `Cataclysm.Capital.` and the updated select-screen tests measure the
behaviour in Unreal; these hold, in the fast suite, the pieces it depends on: the level's generator and the game mode
it names, the one path every routing uses, the engine's start maps, and the shape of the sandbox-floor exception.

HOW THE GENERATOR IS READ: as text and with `ast`, not by importing it, because it imports `unreal`, which exists only
inside the editor.
"""

from __future__ import annotations

import ast
import configparser
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
GENERATOR = REPO_ROOT / "tools" / "generate_capital_map.py"
ENGINE_INI = REPO_ROOT / "game" / "Config" / "DefaultEngine.ini"
CAPITAL_H = SOURCE / "Player" / "CataclysmCapitalGameMode.h"
CAPITAL_CPP = SOURCE / "Player" / "CataclysmCapitalGameMode.cpp"
SELECT_H = SOURCE / "Interface" / "CataclysmCharacterSelectWidget.h"
SELECT_CPP = SOURCE / "Interface" / "CataclysmCharacterSelectWidget.cpp"
NEW_CPP = SOURCE / "Interface" / "CataclysmNewCharacterWidget.cpp"
SAVE_LOAD_CPP = SOURCE / "Save" / "CataclysmSaveLoad.cpp"

CAPITAL_MAP = "/Game/Maps/L_Capital"
FRONT_END = "/Game/Maps/L_FrontEnd.L_FrontEnd"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed and its white space made single spaces."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", " ", text)
    return re.sub(r"\s+", " ", text)


def body(path: pathlib.Path, signature: str) -> str:
    """The braces of the one function whose definition starts with `signature`, comments removed."""
    text = code(path)
    assert text.count(signature) == 1, f"{path.name} holds {signature!r} {text.count(signature)} times, not once"
    opening = text.index("{", text.index(signature))
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    raise AssertionError(f"{signature!r} in {path.name} is not closed")


def generator_constants() -> dict[str, object]:
    tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))
    found = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            try:
                found[node.targets[0].id] = ast.literal_eval(node.value)
            except ValueError:
                continue
    return found


def test_the_generator_makes_l_capital_with_the_capital_game_mode_and_nothing_to_bake() -> None:
    constants = generator_constants()
    assert f"{constants.get('MAPS_DIR')}/{constants.get('CAPITAL_LEVEL')}" == CAPITAL_MAP, (
        f"tools/generate_capital_map.py makes {constants.get('MAPS_DIR')}/{constants.get('CAPITAL_LEVEL')}, and the "
        f"select screen opens {CAPITAL_MAP}")
    assert constants.get("GAME_MODE_CLASS") == "/Script/Cataclysm.CataclysmCapitalGameMode", (
        "L_Capital's world settings must name the capital's own game mode, so the level loads the chosen character "
        "and opens the empire map")

    text = GENERATOR.read_text(encoding="utf-8")
    # MOVABLE LIGHTS AND NOTHING ELSE TO BAKE, so the editor writes no L_Capital_BuiltData beside the map.
    assert text.count("set_mobility(unreal.ComponentMobility.MOVABLE)") == 2, (
        "both of L_Capital's lights must be movable, or the editor asks for a lighting build and writes _BuiltData")
    assert "STATIONARY" not in text, "a stationary light in L_Capital would make the editor write _BuiltData"
    # NO NAVIGATION: nothing in the capital walks by it.
    assert "add_nav_mesh_bounds" not in text and "NavMeshBounds" not in text, (
        "L_Capital has no creatures and needs no navigation bounds")
    assert "settings[0].set_editor_property(\"default_game_mode\", game_mode)" in text, (
        "the generator no longer sets the level's own game mode")


def test_the_capital_game_mode_turns_the_sandbox_off_records_no_floor_and_opens_the_map() -> None:
    header = code(CAPITAL_H)
    assert "class CATACLYSM_API ACataclysmCapitalGameMode : public ACataclysmGameMode" in header, (
        "the capital's game mode must be an ACataclysmGameMode, for the arrival the sandbox's does: the writer begun "
        "and the chosen character made, loaded or refused")
    assert re.search(r"virtual int32 RunFloorNumber\(\) const override \{ return 0; \}", header), (
        "the capital must record floor 0, no floor, so a character saved there reloads with no floor to match")

    constructed = body(CAPITAL_CPP, "ACataclysmCapitalGameMode::ACataclysmCapitalGameMode(")
    assert "bSpawnsSandboxCreatures = false;" in constructed, "the capital spawns the sandbox's creatures"

    for signature in ("void ACataclysmCapitalGameMode::StartPlay(",
                      "void ACataclysmCapitalGameMode::HandleStartingNewPlayer_Implementation("):
        arrival = body(CAPITAL_CPP, signature)
        assert "Super::" in arrival and "OpenTheEmpireMapIfReady();" in arrival, (
            f"{signature.split('::')[1]} no longer runs the sandbox's arrival and then asks to open the map")
        assert arrival.index("Super::") < arrival.index("OpenTheEmpireMapIfReady();"), (
            f"{signature.split('::')[1]} asks to open the map before the load it waits for has had its chance")

    opening = body(CAPITAL_CPP, "bool ACataclysmCapitalGameMode::OpenTheEmpireMapIfReady(")
    for condition, why in (
            ("Instance->PendingCreation.bPending || Instance->PendingCharacterId.IsValid()",
             "the map would open before the chosen character is made or loaded"),
            ("!FrontEndRequested().IsEmpty()", "the map would open after a refusal sent the player back"),
            ("EmpireRunFor(this, true)", "Play on the capital with nothing pending would show an empty map (Q2)")):
        assert condition in opening, f"OpenTheEmpireMapIfReady no longer asks `{condition}`: {why}"
    assert "->OpenEmpireMap()" in opening, "the capital no longer opens the map on the player's screen"


def test_start_and_every_continue_not_inside_a_dungeon_go_to_the_capital() -> None:
    header = code(SELECT_H)
    assert f'CapitalMap = TEXT("{CAPITAL_MAP}");' in header, f"the select screen's CapitalMap is not {CAPITAL_MAP}"

    routed = body(SELECT_CPP, "bool UCataclysmCharacterSelectWidget::MapToContinue(")
    assert "OutMap = bInADungeon ? DungeonMap : CapitalMap;" in routed, (
        "Continue no longer sends a run not saved inside a dungeon to the capital")
    assert "SandboxMap" not in routed, "Continue still names the sandbox"

    confirmed = body(NEW_CPP, "bool UCataclysmNewCharacterWidget::ConfirmInto(")
    assert "Requested = UCataclysmCharacterSelectWidget::CapitalMap;" in confirmed, (
        "Start no longer opens the capital")

    # AND NOTHING ELSE OPENS THE SANDBOX: it is a test level, opened by hand (ruled 2026-11-17).
    openers = sorted(
        path.relative_to(REPO_ROOT).as_posix()
        for path in SOURCE.rglob("*.cpp")
        if "/Tests/" not in path.as_posix() and re.search(r"OpenLevel\([^;]*SandboxMap", code(path)))
    assert not openers, f"these open L_Sandbox, which is now reached only by opening it in the editor: {openers}"


def test_the_game_and_the_editor_start_on_the_front_end() -> None:
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read(ENGINE_INI, encoding="utf-8")
    maps = parser["/Script/EngineSettings.GameMapsSettings"]
    assert maps.get("GameDefaultMap") == FRONT_END and maps.get("EditorStartupMap") == FRONT_END, (
        f"DefaultEngine.ini starts the game on {maps.get('GameDefaultMap')!r} and the editor on "
        f"{maps.get('EditorStartupMap')!r}; both must be {FRONT_END}, so pressing Play opens the front end (#2733)")


def test_only_the_sandboxs_floor_is_dropped_in_the_capital_and_every_other_mismatch_is_refused() -> None:
    loaded = body(SAVE_LOAD_CPP, "FCataclysmLoadReport FCataclysmSaveLoad::CharacterAndRun(")
    drop = re.search(r"const bool bDropsTheSandboxFloor = (.*?);", loaded)
    assert drop, "CharacterAndRun no longer decides whether to drop the sandbox's floor"
    rule = drop.group(1)
    for part, why in (("SavedRun->CurrentDungeonId == INDEX_NONE", "a run saved mid-walk would be dropped"),
                      ("FName(ACataclysmGameMode::SandboxFloorName)", "a floor other than the sandbox's would be dropped"),
                      ("Writer->HoldsTheFloor() == 0", "the sandbox's floor would be dropped in a level that has one")):
        assert part in rule, f"the drop no longer asks `{part}`: {why}"

    chain = loaded.index("if (SavedRun && SavedRun->CurrentDungeonId != INDEX_NONE)")
    dropped = loaded.index("else if (bDropsTheSandboxFloor)")
    refused = loaded.index("else if (SavedRun && SavedRun->Floor.IsOccupied() && (SavedRun->Floor.Dungeon != "
                           "Writer->HoldsTheDungeon() || SavedRun->Floor.Floor != Writer->HoldsTheFloor()))")
    assert chain < dropped < refused, (
        "the sandbox's floor must be one more branch of the one chain -- a walk's resume, then the drop, then the "
        "refusal of every other floor mismatch -- so neither the resume nor the refusal changes")
    for guarded in ("else if (SavedRun->Floor.IsOccupied() && !bDropsTheSandboxFloor)",
                    "SavedRun->Floor.IsOccupied() && !bDropsTheSandboxFloor;",
                    "if (SavedRun && !bDropsTheSandboxFloor)"):
        assert guarded in loaded, (
            f"`{guarded}` is gone: a dropped floor's creatures, floor or place on it would come back in the capital")
