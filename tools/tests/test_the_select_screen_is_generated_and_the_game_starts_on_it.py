"""The game starts on the character select screen's level, and the generator makes that level and the screen.

WHY THIS EXISTS. Issue #753, select-screen step 3b. `game/Config/DefaultEngine.ini`
names `L_FrontEnd` as the game's start map; `tools/generate_interface_assets.py`
makes `L_FrontEnd` and `WBP_CharacterSelect`. Both run only inside the editor,
under the machine's lock, so a mistake in either is found here, on a pull
request, rather than a window later.

WHAT IS CHECKED, AND ITS SCOPE: THIS ONE SCREEN AND THIS ONE LEVEL. That the
start map names `L_FrontEnd` and the editor still opens on `L_Sandbox`; that
`main()` makes the screen after `make_choice_button`, whose class its rows are
copies of, and the level after both; that every name
`CataclysmCharacterSelectWidget.h` binds with `BindWidget` is added by
`make_character_select`; that the front end's controller opens the asset the
generator makes; and that the level the generator makes names the front end's
game mode, which the C++ declares.

WHAT IS NOT. That the generator was run, or that the level loads: the Unreal
tests `Cataclysm.CharacterSelect.TheSelectScreenAssetLoadsAndHoldsItsBoundWidgets`
and `Cataclysm.CharacterSelect.TheGameStartsOnTheFrontEndMap` read the assets.

HOW THE GENERATOR IS READ. With `ast`, not by importing it: the module imports
`unreal`, which only exists inside the editor. The helpers are this file's own
copies of those in `test_the_skill_assignment_screen_is_generated_with_its_bindings.py`.
"""

from __future__ import annotations

import ast
import configparser
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = REPO_ROOT / "tools" / "generate_interface_assets.py"
ENGINE_INI = REPO_ROOT / "game" / "Config" / "DefaultEngine.ini"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
HEADER = SOURCE / "Interface" / "CataclysmCharacterSelectWidget.h"
FRONT_END_HEADER = SOURCE / "Player" / "CataclysmFrontEndGameMode.h"

SCREEN = "make_character_select"
LEVEL = "make_front_end_map"
FRONT_END_MAP = "/Game/Maps/L_FrontEnd.L_FrontEnd"
EDITOR_MAP = "/Game/Maps/L_Sandbox.L_Sandbox"

#: `UPROPERTY(meta = (BindWidget))` then, on the next code line, `TObjectPtr<UTextBlock> RefusalLabel;`.
BIND_WIDGET = re.compile(
    r"UPROPERTY\(\s*meta\s*=\s*\(\s*BindWidget\s*\)\s*\)\s*TObjectPtr<\s*\w+\s*>\s+(\w+)\s*;")


def without_comments(text: str) -> str:
    """The C++ with `//` comments and `/* ... */` blocks removed, not by a line's first character."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def generator_tree() -> ast.Module:
    return ast.parse(GENERATOR.read_text(encoding="utf-8"))


def generator_function(name: str) -> ast.FunctionDef:
    for node in generator_tree().body:
        if isinstance(node, ast.FunctionDef) and node.name == name:
            return node
    raise AssertionError(f"tools/generate_interface_assets.py has no function {name}().")


def generator_constant(name: str) -> str:
    for node in generator_tree().body:
        if isinstance(node, ast.Assign) and name in [
                t.id for t in node.targets if isinstance(t, ast.Name)]:
            return ast.literal_eval(node.value)
    raise AssertionError(f"tools/generate_interface_assets.py has no constant {name}.")


def calls_in(function: ast.FunctionDef) -> list[str]:
    return [node.func.id for node in ast.walk(function)
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)]


def engine_maps() -> configparser.SectionProxy:
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read(ENGINE_INI, encoding="utf-8")
    section = "/Script/EngineSettings.GameMapsSettings"
    assert parser.has_section(section), f"{ENGINE_INI.name} has no [{section}] section."
    return parser[section]


def test_the_game_starts_on_the_front_end_and_the_editor_on_the_sandbox() -> None:
    maps = engine_maps()
    assert maps.get("GameDefaultMap") == FRONT_END_MAP, (
        f"DefaultEngine.ini's GameDefaultMap is {maps.get('GameDefaultMap')!r}; the game "
        f"opens on the character select screen's level, {FRONT_END_MAP} (#753, step 3b).")
    assert maps.get("EditorStartupMap") == EDITOR_MAP, (
        f"DefaultEngine.ini's EditorStartupMap is {maps.get('EditorStartupMap')!r}; the "
        f"editor still opens on {EDITOR_MAP}, ruled 2026-10-31, so Play-in-editor on a "
        "level works as before.")


def test_main_makes_the_screen_after_the_button_and_the_level_after_both() -> None:
    calls = calls_in(generator_function("main"))
    for name in ("make_choice_button", SCREEN, LEVEL):
        assert name in calls, f"main() in tools/generate_interface_assets.py does not call {name}: {calls}."
    assert calls.index("make_choice_button") < calls.index(SCREEN), (
        f"main() calls {SCREEN} before make_choice_button, so the screen's rows point at "
        "an asset that does not exist yet.")
    assert calls.index(SCREEN) < calls.index(LEVEL), (
        f"main() calls {LEVEL} before {SCREEN}; the level is made last, so its save is its own.")


def test_every_bound_widget_of_the_select_screen_is_added_by_its_generator() -> None:
    bound = BIND_WIDGET.findall(without_comments(HEADER.read_text(encoding="utf-8")))
    # A FLOOR, so a header whose declarations changed shape cannot pass with nothing to compare.
    assert len(bound) >= 3, (
        f"Only {len(bound)} BindWidget names were read from {HEADER.name} ({bound}); the "
        "select screen binds three.")
    added = [node.args[2].value for node in ast.walk(generator_function(SCREEN))
             if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
             and node.func.id == "add" and len(node.args) >= 3
             and isinstance(node.args[2], ast.Constant)]
    missing = [name for name in bound if name not in added]
    assert not missing, (
        f"The select screen binds {bound}; {SCREEN}() adds {added}. Missing: {missing}. "
        "The editor would refuse to compile WBP_CharacterSelect.")


def test_the_front_end_opens_the_screen_and_the_level_names_its_game_mode() -> None:
    asset = generator_constant("CHARACTER_SELECT_ASSET")
    front_end = FRONT_END_HEADER.read_text(encoding="utf-8")
    path = f"/Game/Interface/{asset}.{asset}_C"
    assert front_end.count(path) == 1, (
        f"CataclysmFrontEndGameMode.h names {path} {front_end.count(path)} times; the "
        "controller's soft path must name the asset the generator makes, once.")
    game_mode = generator_constant("FRONT_END_GAME_MODE")
    assert game_mode == "/Script/Cataclysm.CataclysmFrontEndGameMode", (
        f"The generator gives L_FrontEnd the game mode {game_mode!r}.")
    assert "class CATACLYSM_API ACataclysmFrontEndGameMode : public AGameModeBase" in front_end, (
        "CataclysmFrontEndGameMode.h does not declare ACataclysmFrontEndGameMode, the "
        "game mode the generator gives L_FrontEnd.")


#: The widgets deleting a character added to the select screen, select-screen step 5.
DELETE_WIDGETS = ("DeleteButton", "ConfirmPanel", "ConfirmText", "ConfirmNameBox",
                  "ConfirmDeleteButton", "CancelButton")
REMAKE = "remake_character_select_without_delete"


def test_the_select_screen_binds_and_its_generator_adds_the_delete_confirmation() -> None:
    """Step 5's six widgets are bound by the screen's C++ and added by its generator."""
    bound = BIND_WIDGET.findall(without_comments(HEADER.read_text(encoding="utf-8")))
    added = [node.args[2].value for node in ast.walk(generator_function(SCREEN))
             if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
             and node.func.id == "add" and len(node.args) >= 3
             and isinstance(node.args[2], ast.Constant)]
    for name in DELETE_WIDGETS:
        assert name in bound, f"{HEADER.name} does not bind {name}, which deleting a character needs (step 5)."
        assert name in added, f"{SCREEN}() does not add {name}, which {HEADER.name} binds (step 5)."


def test_the_generator_remakes_only_the_select_screen_and_only_one_made_before_delete() -> None:
    """The one asset the generator deletes is WBP_CharacterSelect, and only when it lacks step 5's widget.

    Ruled 2026-11-01: the remake is explicit for this one asset, never a general
    "remake everything". So every `delete_asset` in the generator is inside
    remake_character_select_without_delete, that function is called by
    make_character_select alone, and the widget it looks for is one the screen binds.
    """
    tree = generator_tree()
    deleting = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                and any(isinstance(call, ast.Call) and isinstance(call.func, ast.Attribute)
                        and call.func.attr == "delete_asset" for call in ast.walk(node))]
    assert [node.name for node in deleting] == [REMAKE], (
        f"delete_asset is called in {[node.name for node in deleting]}; only {REMAKE} may delete an asset.")
    callers = [node.name for node in tree.body if isinstance(node, ast.FunctionDef) and REMAKE in calls_in(node)]
    assert callers == [SCREEN], f"{REMAKE} is called by {callers}; only {SCREEN} may call it."
    remake_source = ast.get_source_segment(GENERATOR.read_text(encoding="utf-8"), generator_function(REMAKE))
    assert "CHARACTER_SELECT_ASSET" in remake_source and "CHARACTER_SELECT_DELETE_WIDGET" in remake_source, (
        f"{REMAKE} must name the select screen's asset and the widget step 5 added.")
    marker = generator_constant("CHARACTER_SELECT_DELETE_WIDGET")
    bound = BIND_WIDGET.findall(without_comments(HEADER.read_text(encoding="utf-8")))
    assert marker in bound, (
        f"The remake looks for {marker!r}, which {HEADER.name} does not bind, so it would remake the screen every run.")
