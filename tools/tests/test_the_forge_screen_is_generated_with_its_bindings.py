"""The forge screen's generator builds every widget its C++ class binds.

WHY THIS EXISTS. The forge (the owner's ruling of 2026-11-19, "build it next")
adds `UCataclysmForgeWidget` and `make_forge` in
`tools/generate_interface_assets.py`, which writes the first `WBP_Forge`. The
generator runs only inside the editor, under the machine's lock, after a
compile. If it leaves out one `BindWidget` name, the Blueprint does not compile
and the editor run stops there -- a whole window spent on a missing line. This
finds that on a pull request instead, as
`test_the_empire_tree_screen_is_generated_with_its_bindings.py` does for the
empire upgrade tree's screen; the reading helpers are the skill screen's,
imported rather than copied.

WHAT IS CHECKED, AND ITS SCOPE: THIS ONE SCREEN ONLY. That `main()` calls
`make_forge` after `make_choice_button`, whose class the screen's piece and
action buttons are copies of; that every name the header declares
`UPROPERTY(meta = (BindWidget))` is added by an `add(blueprint, unreal.<Type>,
"<Name>", ...)` call inside `make_forge`; and that the controller's soft path and
the generator name the same asset.

WHAT IS NOT. That the generator was run, or that a widget's TYPE suits its
binding. The editor's own compile checks the type when the asset is made.
"""
from __future__ import annotations

import ast
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "tests"))

from test_the_skill_assignment_screen_is_generated_with_its_bindings import (  # noqa: E402
    BIND_WIDGET,
    CONTROLLER_HEADER,
    generator_constant,
    generator_function,
    names_added_by,
    without_comments,
)

HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Interface"
          / "CataclysmForgeWidget.h")

SCREEN = "make_forge"


def bound_names() -> list[str]:
    """Every name the forge screen's header binds with `BindWidget`, in order."""
    assert HEADER.is_file(), f"{HEADER.relative_to(REPO_ROOT)} does not exist."
    return BIND_WIDGET.findall(without_comments(HEADER.read_text(encoding="utf-8")))


def test_main_makes_the_forge_screen_after_the_choice_button() -> None:
    calls = [
        node.func.id for node in ast.walk(generator_function("main"))
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    ]
    assert "make_choice_button" in calls and SCREEN in calls, (
        f"main() in tools/generate_interface_assets.py calls {calls}; it must call "
        f"make_choice_button and {SCREEN}.")
    assert calls.index("make_choice_button") < calls.index(SCREEN), (
        f"main() calls {SCREEN} before make_choice_button, so the forge screen's "
        "ChoiceButtonClass points at an asset that does not exist yet.")


def test_every_bound_widget_of_the_forge_screen_is_added_by_its_generator() -> None:
    bound = bound_names()
    # A FLOOR, so a header whose declarations changed shape cannot pass with
    # nothing to compare: the screen binds seven today.
    assert len(bound) >= 7, (
        f"Only {len(bound)} BindWidget names were read from "
        f"{HEADER.relative_to(REPO_ROOT)} ({bound}); the forge screen binds "
        "seven. If the declarations changed shape, update BIND_WIDGET.")
    added = names_added_by(generator_function(SCREEN))
    missing = [name for name in bound if name not in added]
    assert not missing, (
        f"The forge screen (only this screen is checked here) binds "
        f"{len(bound)} widgets: {bound}. {SCREEN}() adds {len(added)}: {added}. "
        f"Missing: {missing}. The editor would refuse to compile WBP_Forge.")


def test_the_controller_opens_the_asset_the_generator_makes() -> None:
    asset = generator_constant("FORGE_ASSET")
    parent = generator_constant("FORGE_PARENT")
    controller = CONTROLLER_HEADER.read_text(encoding="utf-8")
    path = f"/Game/Interface/{asset}.{asset}_C"
    assert controller.count(path) == 1, (
        f"CataclysmPlayerController.h names {path} {controller.count(path)} times; "
        "the forge screen's soft path must name the asset the generator makes, "
        "once.")
    assert f"class CATACLYSM_API U{parent} : public UUserWidget" in HEADER.read_text(
        encoding="utf-8"), (
        f"FORGE_PARENT is {parent!r}, but {HEADER.name} does not declare "
        f"U{parent} as a UUserWidget.")
