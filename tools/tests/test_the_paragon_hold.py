"""The Paragon packs' objects are held before every garbage collection (issue #2968).

WHY THIS EXISTS. The packaged game crashed when a collection freed Paragon objects a cooked Paragon material still
needed at draw time. `UCataclysmParagonHold` holds them. `Cataclysm.ParagonHold.` tests the holding at run time; this
checks, without a build, that the hold is wired to every collection, holds through a property the collector follows,
and does not write the Paragon prefix as one string, which tools/package_game.py would read as a load path the cook
must cover.
"""
from __future__ import annotations

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PLAYER = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
HEADER = PLAYER / "CataclysmParagonHold.h"
SOURCE = PLAYER / "CataclysmParagonHold.cpp"

sys.path.insert(0, str(REPO_ROOT / "tools"))

import package_game  # noqa: E402


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


def test_the_hold_runs_before_every_collection_and_holds_the_paragon_prefix() -> None:
    source = re.sub(r"\s+", " ", code(SOURCE))
    initialize = body(source, "void UCataclysmParagonHold::Initialize(")
    assert ("FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddUObject(this, "
            "&UCataclysmParagonHold::HoldBeforeCollecting)") in initialize, "the hold is not run before every collection"
    assert "GetPreGarbageCollectDelegate().Remove(" in body(source, "void UCataclysmParagonHold::Deinitialize(")
    assert "HoldObjectsUnder(ParagonPrefix(), Held)" in body(source, "void UCataclysmParagonHold::HoldBeforeCollecting(")


def test_what_is_held_is_a_property_the_collector_follows() -> None:
    header = re.sub(r"\s+", " ", code(HEADER))
    assert re.search(r"UPROPERTY\([^)]*\) TSet<TObjectPtr<UObject>> Held;", header), (
        "Held is not a UPROPERTY, so the collector would not see what it holds")


def test_the_prefix_is_not_a_path_the_packager_must_cook() -> None:
    # THE CONTROL: the packager's own pattern finds a content path in the line it would have read.
    assert package_game.CONTENT_PATH.search('TEXT("/Game/Paragon")')
    for path in (HEADER, SOURCE):
        assert not package_game.CONTENT_PATH.search(path.read_text(encoding="utf-8")), (
            f"{path.name} writes a content path the cook list would have to cover")
