"""The skill-choice screen's generator builds every widget its C++ class binds.

WHY THIS EXISTS. Issue #837 layer 4 adds `UCataclysmSkillAssignmentWidget` and
`make_skill_assignment` in `tools/generate_interface_assets.py`, which writes the
first `WBP_SkillAssignment`. The generator runs only inside the editor, under the
machine's lock, after a compile. If it leaves out one `BindWidget` name, the
Blueprint does not compile and the editor run stops there -- a whole window spent
on a missing line. This finds that on a pull request instead.

WHAT IS CHECKED, AND ITS SCOPE: THIS ONE SCREEN ONLY. That `main()` calls
`make_skill_assignment` after `make_choice_button`, whose class the screen's
buttons are copies of; that every name the header declares
`UPROPERTY(meta = (BindWidget))` is added by an `add(blueprint, unreal.<Type>,
"<Name>", ...)` call inside `make_skill_assignment`; and that the controller's
soft path and the generator name the same asset. The same check could cover all
eight screens the generator makes. It does not: it was written for layer 4, and
each assertion message names the one screen it read.

WHAT IS NOT. That the generator was run, or that a widget's TYPE suits its
binding (a `UPanelWidget` binding accepts any panel). The editor's own compile
checks the type when the asset is made.

HOW THE GENERATOR IS READ. With `ast`, not by importing it: the module imports
`unreal`, which only exists inside the editor.
"""

from __future__ import annotations

import ast
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = REPO_ROOT / "tools" / "generate_interface_assets.py"
INTERFACE = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Interface"
HEADER = INTERFACE / "CataclysmSkillAssignmentWidget.h"
CONTROLLER_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                     / "CataclysmPlayerController.h")

SCREEN = "make_skill_assignment"

#: `UPROPERTY(meta = (BindWidget))` then, on the next code line, `TObjectPtr<UTextBlock> StatusLabel;`.
BIND_WIDGET = re.compile(
    r"UPROPERTY\(\s*meta\s*=\s*\(\s*BindWidget\s*\)\s*\)\s*TObjectPtr<\s*\w+\s*>\s+(\w+)\s*;")


def without_comments(text: str) -> str:
    """The C++ with `//` comments and `/* ... */` blocks removed.

    NOT BY A LINE'S FIRST CHARACTER: a wrapped expression can continue on a line
    opening with `*`, and a stripper that drops such lines drops code (memory note
    `a-code-line-opening-with-a-star-is-not-a-comment`).
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def generator_function(name: str) -> ast.FunctionDef:
    tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name == name:
            return node
    raise AssertionError(f"tools/generate_interface_assets.py has no function {name}().")


def generator_constant(name: str) -> str:
    tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign) and name in [
                t.id for t in node.targets if isinstance(t, ast.Name)]:
            return ast.literal_eval(node.value)
    raise AssertionError(f"tools/generate_interface_assets.py has no constant {name}.")


def names_added_by(function: ast.FunctionDef) -> list[str]:
    """Every widget name an `add(blueprint, unreal.<Type>, "<Name>", ...)` call adds, in order."""
    added: list[str] = []
    for node in ast.walk(function):
        if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                and node.func.id == "add" and len(node.args) >= 3
                and isinstance(node.args[2], ast.Constant)
                and isinstance(node.args[2].value, str)):
            added.append(node.args[2].value)
    return added


def bound_names() -> list[str]:
    """Every name the screen's header binds with `BindWidget` (not `BindWidgetOptional`), in order."""
    assert HEADER.is_file(), f"{HEADER.relative_to(REPO_ROOT)} does not exist."
    return BIND_WIDGET.findall(without_comments(HEADER.read_text(encoding="utf-8")))


def test_main_makes_the_skill_screen_after_the_choice_button() -> None:
    calls = [
        node.func.id for node in ast.walk(generator_function("main"))
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    ]
    assert "make_choice_button" in calls and SCREEN in calls, (
        f"main() in tools/generate_interface_assets.py calls {calls}; it must call "
        f"make_choice_button and {SCREEN}.")
    assert calls.index("make_choice_button") < calls.index(SCREEN), (
        f"main() calls {SCREEN} before make_choice_button, so the skill screen's "
        "ChoiceButtonClass points at an asset that does not exist yet.")


def test_every_bound_widget_of_the_skill_screen_is_added_by_its_generator() -> None:
    bound = bound_names()
    # A FLOOR, so a header whose declarations changed shape cannot pass with
    # nothing to compare: the screen binds seven today.
    assert len(bound) >= 7, (
        f"Only {len(bound)} BindWidget names were read from "
        f"{HEADER.relative_to(REPO_ROOT)} ({bound}); the skill screen binds seven. "
        "If the declarations changed shape, update BIND_WIDGET.")
    added = names_added_by(generator_function(SCREEN))
    missing = [name for name in bound if name not in added]
    assert not missing, (
        f"The skill-choice screen (only this screen is checked here) binds "
        f"{len(bound)} widgets: {bound}. {SCREEN}() adds {len(added)}: {added}. "
        f"Missing: {missing}. The editor would refuse to compile WBP_SkillAssignment.")


def test_the_controller_opens_the_asset_the_generator_makes() -> None:
    asset = generator_constant("SKILL_ASSIGNMENT_ASSET")
    parent = generator_constant("SKILL_ASSIGNMENT_PARENT")
    controller = CONTROLLER_HEADER.read_text(encoding="utf-8")
    path = f"/Game/Interface/{asset}.{asset}_C"
    assert controller.count(path) == 1, (
        f"CataclysmPlayerController.h names {path} {controller.count(path)} times; "
        "the skill screen's soft path must name the asset the generator makes, once.")
    assert f"class CATACLYSM_API U{parent} : public UUserWidget" in HEADER.read_text(
        encoding="utf-8"), (
        f"SKILL_ASSIGNMENT_PARENT is {parent!r}, but {HEADER.name} does not declare "
        f"U{parent} as a UUserWidget.")
