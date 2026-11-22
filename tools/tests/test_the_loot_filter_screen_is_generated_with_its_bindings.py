"""The loot filter screen's generator builds every widget its C++ class binds.

WHY THIS EXISTS. Issue #2846, slice LF2, adds `UCataclysmLootFilterWidget` and `make_loot_filter` in
`tools/generate_interface_assets.py`, which writes the first `WBP_LootFilter`. The generator runs only inside the
editor, under the machine's lock, after a compile; a `BindWidget` name it leaves out stops the Blueprint compiling and
the editor run with it. This finds that on a pull request instead, as the forge screen's check does; the reading
helpers are the skill screen's, imported rather than copied.

WHAT IS CHECKED, AND ITS SCOPE: THIS ONE SCREEN ONLY. That `main()` calls `make_loot_filter` after
`make_choice_button`, whose class the screen's buttons are copies of; that every name the header binds with
`BindWidget` is added inside `make_loot_filter`; and that the controller's soft path and the generator name the same
asset.

WHAT IS NOT. That the generator was run, or that a widget's TYPE suits its binding: the editor's compile checks that.
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
          / "CataclysmLootFilterWidget.h")

SCREEN = "make_loot_filter"


def bound_names() -> list[str]:
    """Every name the loot filter screen's header binds with `BindWidget`, in order."""
    assert HEADER.is_file(), f"{HEADER.relative_to(REPO_ROOT)} does not exist."
    return BIND_WIDGET.findall(without_comments(HEADER.read_text(encoding="utf-8")))


def test_main_makes_the_loot_filter_screen_after_the_choice_button() -> None:
    calls = [
        node.func.id for node in ast.walk(generator_function("main"))
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    ]
    assert "make_choice_button" in calls and SCREEN in calls, (
        f"main() in tools/generate_interface_assets.py calls {calls}; it must call "
        f"make_choice_button and {SCREEN}.")
    assert calls.index("make_choice_button") < calls.index(SCREEN), (
        f"main() calls {SCREEN} before make_choice_button, so the screen's "
        "ChoiceButtonClass points at an asset that does not exist yet.")


def test_every_bound_widget_of_the_loot_filter_screen_is_added_by_its_generator() -> None:
    bound = bound_names()
    # A FLOOR, so a header whose declarations changed shape cannot pass with nothing to compare: it binds six today.
    assert len(bound) >= 6, (
        f"Only {len(bound)} BindWidget names were read from {HEADER.relative_to(REPO_ROOT)} ({bound}); "
        "the loot filter screen binds six. If the declarations changed shape, update BIND_WIDGET.")
    added = names_added_by(generator_function(SCREEN))
    missing = [name for name in bound if name not in added]
    assert not missing, (
        f"The loot filter screen binds {len(bound)} widgets: {bound}. {SCREEN}() adds {len(added)}: {added}. "
        f"Missing: {missing}. The editor would refuse to compile WBP_LootFilter.")


def test_the_controller_opens_the_asset_the_generator_makes() -> None:
    asset = generator_constant("LOOT_FILTER_ASSET")
    parent = generator_constant("LOOT_FILTER_PARENT")
    controller = CONTROLLER_HEADER.read_text(encoding="utf-8")
    path = f"/Game/Interface/{asset}.{asset}_C"
    assert controller.count(path) == 1, (
        f"CataclysmPlayerController.h names {path} {controller.count(path)} times; the loot filter screen's soft "
        "path must name the asset the generator makes, once.")
    assert f"class CATACLYSM_API U{parent} : public UUserWidget" in HEADER.read_text(encoding="utf-8"), (
        f"LOOT_FILTER_PARENT is {parent!r}, but {HEADER.name} does not declare U{parent} as a UUserWidget.")
