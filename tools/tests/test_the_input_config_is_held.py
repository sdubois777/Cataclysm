"""The player controller reads its input configuration through a held reference, never the soft pointer alone.

WHY THIS EXISTS. Until 2026-11-27 `ACataclysmPlayerController::SetupInputComponent` loaded the configuration into a
local, and `KeyForAbilitySlot` and `LeftButtonAlsoMoves` read it back with `InputConfig.Get()`. `InputConfig` is a
`TSoftObjectPtr`, which keeps nothing loaded. The editor keeps the asset loaded, so nothing showed; in the packaged game
the first garbage collection, about a minute in, freed it, and from then a right click over the inventory went to the
Heavy skill instead of equipping (the owner: "The right clicking an item is what stops working. I have to drag and drop
them after that"), and the skill bar's key letters went blank.

`Cataclysm.Input.TheControllerHoldsTheInputConfigItLoaded` checks the held property at run time; this checks, without a
build, that every reader uses it.
"""
from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PLAYER = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
HEADER = PLAYER / "CataclysmPlayerController.h"
SOURCE = PLAYER / "CataclysmPlayerController.cpp"


def code(path: pathlib.Path) -> str:
    """The file with its comments removed, so a name written only in a comment does not count."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def body(text: str, signature: str) -> str:
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    depth, at = 0, text.index("{", start)
    for index in range(at, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[at:index + 1]
    raise AssertionError(f"{signature} is never closed")


def test_the_held_configuration_is_a_property_garbage_collection_follows() -> None:
    header = re.sub(r"\s+", " ", code(HEADER))
    assert re.search(r"UPROPERTY\([^)]*\) TObjectPtr<const UCataclysmInputConfig> HeldInputConfig", header), (
        "HeldInputConfig is not a UPROPERTY object pointer, so garbage collection would not follow it")


def test_setup_holds_what_it_loads() -> None:
    source = code(SOURCE)
    hold = body(source, "const UCataclysmInputConfig* ACataclysmPlayerController::HoldTheInputConfig(")
    assert "HeldInputConfig = InputConfig.LoadSynchronous();" in hold
    setup = body(source, "void ACataclysmPlayerController::SetupInputComponent(")
    assert "HoldTheInputConfig()" in setup, "setting up input does not hold the configuration"
    assert "InputConfig.LoadSynchronous()" not in setup, "setting up input loads the configuration without holding it"


def test_no_reader_goes_back_to_the_soft_pointer() -> None:
    source = code(SOURCE)
    # THE CONTROL: the check finds the soft pointer where it is meant to be, in the one function that loads it.
    assert source.count("InputConfig.LoadSynchronous()") == 1
    assert "InputConfig.Get()" not in source, "a reader asks the soft pointer, which answers null once the asset is freed"
    for signature in ("FKey ACataclysmPlayerController::KeyForAbilitySlot(",
                      "bool ACataclysmPlayerController::LeftButtonAlsoMoves("):
        assert "HeldInputConfig" in body(source, signature), f"{signature} does not read the held configuration"
