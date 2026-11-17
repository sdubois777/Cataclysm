"""An enemy's ground marker must be the area its attack actually covers.

WHY THIS EXISTS. Issue #396. Nothing in the project drew a ground marker of any
kind, so the design's wind-up rule --

    Wind-up seconds = 0.4 + Radius / 3.5

where 0.4 is a reaction allowance and 3.5 metres per second is the slowest
class's walk speed -- was being kept on the timing side and broken on the seeing
side. The Brute's Stomp waited the designed 1.4 seconds for its 3.5 metre ring
and the player had to judge that ring from an animation.

THE FAULT THIS GUARDS AGAINST IS NOT "NO MARKER". It is a marker that shows a
different area from the one that hurts, which is worse than no marker, because
the player would have learnt to trust it. So every check below compares the
figure the marker is drawn from against the figure the damage uses, and both
against `sim/cataclysm_sim/enemy_abilities.py`, which is authoritative.

WHY IT IS A PYTHON TEST. Continuous integration never builds the C++ --
`.github/workflows/ci.yml` is a single Linux job. The automation tests in
`game/Source/Cataclysm/Tests/CataclysmTelegraphMarkerTests.cpp` check the same
things against live actors and only ever run on a developer's machine.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
MARKER_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                 / "CataclysmTelegraphMarker.h")
BRUTE_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
             / "CataclysmBruteCharacter.cpp")
BRUTE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmBruteCharacter.h")
CONTROLLER_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                  / "CataclysmEnemyController.cpp")

sys.path.insert(0, str(REPO_ROOT / "sim"))

#: Metres to centimetres. The design and the model work in metres; Unreal's
#: world unit is the centimetre.
CM_PER_METRE = 100.0

#: Which C++ constant each telegraphed Brute ability draws its marker from, and
#: which shape it uses. The point of the mapping is that the marker constant is
#: the SAME constant the ability's own damage uses, so the two cannot drift.
BRUTE_MARKERS: tuple[tuple[str, str, str], ...] = (
    # model ability name, C++ shape, C++ constant the marker is filled from
    ("Stomp", "Strike", "StompRadiusCm"),
    ("Rip and Toss", "Projectile", "RockThrowRadiusCm"),
)


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def constant(header: pathlib.Path, name: str) -> float:
    """The value of a `static constexpr float <name> = <number>f;` line."""
    match = re.search(
        rf"static\s+constexpr\s+float\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?)f\s*;",
        source(header),
    )
    if match is None:
        pytest.fail(
            f"{header.relative_to(REPO_ROOT)} has no "
            f"'static constexpr float {name} = <number>f;' line."
        )
    return float(match.group(1))


def brute_ability(name: str):
    """One entry of ABILITIES['Brute'] by name."""
    from cataclysm_sim.enemy_abilities import abilities

    for ability in abilities("Brute"):
        if ability.name == name:
            return ability
    raise AssertionError(
        f"sim/cataclysm_sim/enemy_abilities.py has no Brute ability named "
        f"{name!r}. It holds: {[a.name for a in abilities('Brute')]}"
    )


def test_the_one_metre_floor_matches_the_model() -> None:
    """A marker smaller than the creature standing in it is not a telegraph.

    The design document's wording, quoted in `is_telegraphed`, is that such a
    marker "is smaller than the creature standing in it, so there is nowhere to
    walk". The C++ refuses to draw one and the model refuses to call the ability
    telegraphed; if the two figures ever disagreed, an ability the model treats
    as telegraphed would silently draw nothing.
    """
    from cataclysm_sim.enemy_abilities import SMALLEST_USEFUL_MARKER_METRES

    floor_cm = constant(MARKER_HEADER, "SmallestUsefulRadiusCm")

    assert floor_cm == pytest.approx(
        SMALLEST_USEFUL_MARKER_METRES * CM_PER_METRE), (
        f"ACataclysmTelegraphMarker refuses to draw below {floor_cm:.0f} cm but "
        f"SMALLEST_USEFUL_MARKER_METRES in sim/cataclysm_sim/enemy_abilities.py "
        f"is {SMALLEST_USEFUL_MARKER_METRES} metres. An ability between the two "
        f"would be telegraphed as far as the model is concerned and draw "
        f"nothing in the game."
    )


@pytest.mark.parametrize("ability_name,shape,cpp_constant", BRUTE_MARKERS)
def test_each_marked_area_is_the_designed_one(
        ability_name, shape, cpp_constant) -> None:
    """The constant the marker is drawn from is the model's own radius."""
    designed_metres = float(brute_ability(ability_name).params["Radius"])
    cpp_cm = constant(BRUTE_HEADER, cpp_constant)

    assert cpp_cm == pytest.approx(designed_metres * CM_PER_METRE), (
        f"{cpp_constant} in CataclysmBruteCharacter.h is {cpp_cm:.0f} cm but "
        f"the {ability_name}'s Radius in sim/cataclysm_sim/enemy_abilities.py "
        f"is {designed_metres} metres. The Python is authoritative."
    )


@pytest.mark.parametrize("ability_name,shape,cpp_constant", BRUTE_MARKERS)
def test_the_marker_is_filled_from_the_constant_the_damage_uses(
        ability_name, shape, cpp_constant) -> None:
    """Not from a literal, and not from a second constant of its own.

    THIS IS THE ONE THAT MATTERS. Everything else here checks numbers against
    numbers. This checks that the marker and the attack read the SAME name, so
    that changing the attack's size changes the marker's size and there is no
    way to move one without the other.
    """
    text = source(BRUTE_CPP)

    assert re.search(rf"MarkerRadiusCm\s*=\s*{re.escape(cpp_constant)}\s*;", text), (
        f"CataclysmBruteCharacter.cpp does not fill MarkerRadiusCm from "
        f"{cpp_constant} for the {ability_name}. If it now holds a literal or a "
        f"constant of its own, the marker and the attack are two numbers that "
        f"can disagree, and a marker that lies about where the damage lands is "
        f"worse than no marker at all."
    )


@pytest.mark.parametrize("ability_name,shape,cpp_constant", BRUTE_MARKERS)
def test_the_shape_matches_the_model(ability_name, shape, cpp_constant) -> None:
    """A Strike marks a circle and a Projectile marks a lane.

    Drawing the wrong one is not cosmetic: a circle where a lane belongs tells
    the player to walk out of the wrong ground entirely.
    """
    assert brute_ability(ability_name).shape == shape, (
        f"The {ability_name} is a {brute_ability(ability_name).shape} in "
        f"sim/cataclysm_sim/enemy_abilities.py, and the C++ is checked below "
        f"for {shape}. Update this mapping and the C++ together."
    )

    text = source(BRUTE_CPP)
    assert re.search(
        rf"Shape\s*=\s*ECataclysmSkillShape::{re.escape(shape)}\s*;", text), (
        f"CataclysmBruteCharacter.cpp does not give any ability the "
        f"{shape} shape, so the {ability_name} draws no marker of that kind."
    )


def test_every_telegraphed_brute_ability_has_a_marker() -> None:
    """Nothing the model calls telegraphed may be missing from the C++.

    THE REGRESSION THIS EXISTS FOR is an ability being added to the model with a
    marker and to the C++ without one. It would look finished, run correctly,
    and quietly give the player no warning.
    """
    from cataclysm_sim.enemy_abilities import abilities, is_telegraphed

    telegraphed = {a.name for a in abilities("Brute") if is_telegraphed(a, "Brute")}
    covered = {name for name, _, _ in BRUTE_MARKERS}

    assert telegraphed == covered, (
        f"The model says these Brute abilities are telegraphed: "
        f"{sorted(telegraphed)}. This test covers: {sorted(covered)}. "
        f"An ability in the first list and not the second draws no ground "
        f"marker and nothing reports it."
    )


def test_the_slam_is_deliberately_not_one_of_them() -> None:
    """The case the one metre floor exists for, named so nobody 'fixes' it.

    The Brute's ordinary slam reaches 0.90 metres, which is under the floor, so
    it draws nothing and is read off the creature instead. That is the design
    working rather than an omission.
    """
    from cataclysm_sim.enemy_abilities import (
        SMALLEST_USEFUL_MARKER_METRES, is_telegraphed)

    slam = brute_ability("Slam")

    assert not is_telegraphed(slam, "Brute")
    assert float(slam.params["Radius"]) < SMALLEST_USEFUL_MARKER_METRES
    assert "Slam" not in {name for name, _, _ in BRUTE_MARKERS}


def controller_function_body(name: str) -> str:
    """The body of one ACataclysmEnemyController member function.

    WHY NOT JUST COUNT THE CALLS. Counting was the first version of the test
    below and it could not fail usefully: `ShowWindUpMarker` clears any previous
    marker itself, so there are five calls and a threshold of four passed with
    one of the four real removals deleted. Reading each function separately is
    what makes deleting any single one of them fail.

    Relies on this repository's formatting, where a member function's closing
    brace is the first `}` at the start of a line.
    """
    text = source(CONTROLLER_CPP)
    start = re.search(rf"^\w[\w:<>*&\s]*?ACataclysmEnemyController::{re.escape(name)}\s*\(",
                      text, re.M)
    if start is None:
        pytest.fail(
            f"CataclysmEnemyController.cpp has no ACataclysmEnemyController::"
            f"{name}. If it was renamed, rename it here; if it was deleted, "
            f"this guard is no longer checking anything."
        )
    end = re.search(r"^\}", text[start.end():], re.M)
    if end is None:
        pytest.fail(f"could not find the end of {name}")
    return text[start.end():start.end() + end.start()]


def test_the_controller_draws_the_marker_when_a_wind_up_starts() -> None:
    """Otherwise no enemy telegraphs anything at all."""
    body = controller_function_body("UseAbilitiesOn")

    assert "ShowWindUpMarker(Driven, Abilities[Chosen])" in body, (
        "CataclysmEnemyController::UseAbilitiesOn does not draw a marker when "
        "it starts a wind-up, so nothing in the game telegraphs anything and "
        "issue #396 is back."
    )


#: Every place a wind-up can end, and what leaving a marker behind would mean.
#: A marker still on the floor after its attack is over tells the player to walk
#: out of ground where nothing is going to happen; do that a few times and they
#: stop reading the next one, which is worse than never having drawn any.
REMOVAL_SITES: tuple[tuple[str, str], ...] = (
    ("ContinueWindUp", "the attack lands"),
    ("Think", "a stun abandons the wind-up"),
    ("OnUnPossess", "the pawn it belonged to goes away"),
    ("EndPlay", "the controller ends play"),
)


@pytest.mark.parametrize("function,when", REMOVAL_SITES)
def test_the_marker_is_removed_at_every_place_a_wind_up_ends(
        function, when) -> None:
    assert "DismissWindUpMarker();" in controller_function_body(function), (
        f"CataclysmEnemyController::{function} does not remove the wind-up "
        f"marker, so when {when} the warning stays on the floor after there is "
        f"nothing left to walk out of."
    )
