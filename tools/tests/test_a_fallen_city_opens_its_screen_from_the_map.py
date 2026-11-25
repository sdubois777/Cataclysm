"""A fallen city's box on the empire map can be pressed, so its Fallen City dungeon can be walked and the city retaken.

WHY THIS EXISTS. Issue #2866, the owner: "I'm unable to take it back, it won't
let me attack it." Since 2026-08-31 `UCataclysmEmpireMapWidget::PlaceCities`
made a fallen city's box unavailable (`bAvailable` false), and an unavailable
`UCataclysmChoiceButton` cannot be pressed, so the city screen -- where the
Fallen City dungeon standing on the city is listed with its Walk button -- never
opened. The decision now lives in `UCataclysmEmpireMapLayout::CityOpensItsScreen`,
which an automation test reads; but no automation test can see a button, since
the test command runs with `-nullrhi` and a headless widget makes none. So this
checks, in the widget's source with comments taken out, that the box's
availability IS that function's answer and that a fallen city is told apart by
being dimmed rather than by being unavailable.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
INTERFACE = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Interface"
WIDGET = INTERFACE / "CataclysmEmpireMapWidget.cpp"
LAYOUT = INTERFACE / "CataclysmEmpireMapLayout.cpp"
CITY_SCREEN = INTERFACE / "CataclysmCityScreenLayout.cpp"


def code(path: pathlib.Path) -> str:
    """The C++ with `/* ... */` blocks and `//` comments removed and every run of whitespace one space."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
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
            return text[opening:index + 1]
    raise AssertionError(f"{signature!r} is not closed")


def test_a_citys_box_is_available_exactly_when_the_layout_says_it_opens_its_screen() -> None:
    place = body(code(WIDGET), "void UCataclysmEmpireMapWidget::PlaceCities(")
    assert "/*bAvailable*/" not in place, "comments were not taken out"
    assert re.search(r"Button->SetChoice\( ButtonNameFor\(Index\), .*?, Mark == ECataclysmCityMark::Exposed, "
                     r"UCataclysmEmpireMapLayout::CityOpensItsScreen\(Run->Map, Index\)\);", place), (
        "a city's box is not made available by CityOpensItsScreen, so a fallen city's box may not be pressable and "
        "its Fallen City dungeon cannot be walked (issue #2866)")
    assert "Mark != ECataclysmCityMark::Fallen" not in place, (
        "a fallen city's box is made unavailable again, which stops it being pressed (issue #2866)")
    assert "Button->SetDimmed(UCataclysmEmpireMapLayout::IsCityDimmed(Run->Map, Index));" in place, (
        "a fallen city is no longer drawn dimmed, so nothing on the map tells it apart from a standing one")


def test_every_city_on_the_map_opens_its_screen_a_fallen_one_included() -> None:
    opens = body(code(LAYOUT), "bool UCataclysmEmpireMapLayout::CityOpensItsScreen(")
    assert "return Map != nullptr && Map->Find(CityId) != nullptr;" in opens, (
        "CityOpensItsScreen no longer answers yes for every city on the map")
    assert "bFallen" not in opens, "CityOpensItsScreen asks whether the city has fallen; a fallen one must open (#2866)"
    dimmed = body(code(LAYOUT), "bool UCataclysmEmpireMapLayout::IsCityDimmed(")
    assert "return MarkFor(Map, CityId) == ECataclysmCityMark::Fallen;" in dimmed


def test_the_map_and_the_city_screen_say_how_a_fallen_city_is_retaken() -> None:
    hovered = body(code(WIDGET), "void UCataclysmEmpireMapWidget::HandleCityHovered(")
    assert "Parts.Add(UCataclysmEmpireMapLayout::FallenCityDetail(Run, CityId));" in hovered, (
        "the line under the map no longer says how a fallen city is retaken")
    status = body(code(CITY_SCREEN), "FString UCataclysmCityScreenLayout::StatusTextFor(")
    assert "UCataclysmEmpireMapLayout::FallenCityDungeonOn(Run, CityId) != INDEX_NONE" in status, (
        "the city screen of a fallen city no longer says its Fallen City dungeon is walked to retake it")
