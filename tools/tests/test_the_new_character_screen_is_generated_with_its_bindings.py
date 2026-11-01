"""The generator makes the new-character screen with every widget its C++ binds, after the button it copies.

WHY THIS EXISTS. Issue #753, select-screen step 4b. `tools/generate_interface_assets.py`
makes `WBP_NewCharacter`, the screen New on the character select screen opens. It
runs only inside the editor, under the machine's lock, so a mistake in it is found
here, on a pull request, rather than a window later.

WHAT IS CHECKED, AND ITS SCOPE: THIS ONE SCREEN. That every name
`CataclysmCharacterCreationWidget.h` and `CataclysmNewCharacterWidget.h` bind with
`BindWidget` -- the creator's six and the new screen's six -- is added by
`make_new_character`; that `main()` makes the screen after `make_choice_button`,
whose class its lethality and population buttons are copies of, and before
`make_front_end_map`, which stays last; and that the front end's controller opens
the asset the generator makes.

WHAT IS NOT. That the generator was run, or that the asset holds the widgets: the
Unreal test `Cataclysm.CharacterSelect.TheNewCharacterScreenAssetLoadsAndHoldsItsBoundWidgets`
reads the asset.

HOW THE GENERATOR IS READ. With `ast`, not by importing it: the module imports
`unreal`, which only exists inside the editor. The helpers are this file's own
copies of those in `test_the_select_screen_is_generated_and_the_game_starts_on_it.py`.
"""

from __future__ import annotations

import ast
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = REPO_ROOT / "tools" / "generate_interface_assets.py"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
HEADERS = (SOURCE / "Interface" / "CataclysmCharacterCreationWidget.h",
           SOURCE / "Interface" / "CataclysmNewCharacterWidget.h")
FRONT_END_HEADER = SOURCE / "Player" / "CataclysmFrontEndGameMode.h"

SCREEN = "make_new_character"

#: `UPROPERTY(meta = (BindWidget))` then, on the next code line, `TObjectPtr<UButton> BackButton;`.
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


def test_every_bound_widget_of_the_new_character_screen_is_added_by_its_generator() -> None:
    bound = [name for header in HEADERS
             for name in BIND_WIDGET.findall(without_comments(header.read_text(encoding="utf-8")))]
    # A FLOOR, so headers whose declarations changed shape cannot pass with nothing to compare.
    assert len(bound) >= 12, (
        f"Only {len(bound)} BindWidget names were read from {[h.name for h in HEADERS]} ({bound}); "
        "the new-character screen binds the creator's six and its own six.")
    added = [node.args[2].value for node in ast.walk(generator_function(SCREEN))
             if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
             and node.func.id == "add" and len(node.args) >= 3
             and isinstance(node.args[2], ast.Constant)]
    missing = [name for name in bound if name not in added]
    assert not missing, (
        f"The new-character screen binds {bound}; {SCREEN}() adds {added}. Missing: {missing}. "
        "The editor would refuse to compile WBP_NewCharacter.")


def test_main_makes_the_screen_after_the_button_and_before_the_level() -> None:
    calls = calls_in(generator_function("main"))
    for name in ("make_choice_button", SCREEN, "make_front_end_map"):
        assert name in calls, f"main() in tools/generate_interface_assets.py does not call {name}: {calls}."
    assert calls.index("make_choice_button") < calls.index(SCREEN), (
        f"main() calls {SCREEN} before make_choice_button, so the screen's lethality and population "
        "buttons point at an asset that does not exist yet.")
    assert calls.index(SCREEN) < calls.index("make_front_end_map"), (
        f"main() calls make_front_end_map before {SCREEN}; the level is made last, so its save is its own.")


def test_the_front_end_opens_the_asset_the_generator_makes() -> None:
    asset = generator_constant("NEW_CHARACTER_ASSET")
    front_end = FRONT_END_HEADER.read_text(encoding="utf-8")
    path = f"/Game/Interface/{asset}.{asset}_C"
    assert front_end.count(path) == 1, (
        f"CataclysmFrontEndGameMode.h names {path} {front_end.count(path)} times; the "
        "controller's soft path must name the asset the generator makes, once.")
