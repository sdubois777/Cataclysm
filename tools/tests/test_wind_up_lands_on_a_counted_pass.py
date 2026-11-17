"""A telegraph is rounded up to a whole thinking pass, and must still be legal.

WHY THIS EXISTS. Issue #413. An enemy's ability used to land on "the first
thinking pass whose clock has gone past the deadline". That sounds exact and is
not: a timer callback runs on the first FRAME past its deadline, so every pass
carries up to a frame of overshoot, and the overshoot on the pass that starts a
wind-up is not the overshoot on the pass that should land it. Where a telegraph
sits on a pass boundary, a difference of a few milliseconds decided a whole
quarter of a second.

The Brute's rock throw sits exactly there: a 1.000 second telegraph against a
0.250 second pass. Simulating the engine's own timer arithmetic over 500 jittery
frames landed it on the later pass 246 times out of 500.

WHAT THE FIX DOES, AND WHAT IT COSTS. The telegraph is turned into a number of
passes once, when the wind-up begins, and counted down. That is immune to frame
timing. The cost is that a telegraph which is not a whole number of passes is
rounded UP to the next one, so the attack lands slightly later than the designed
figure -- the Brute's 1.4 second stomp lands at 1.5, which is what it already did
before the change.

WHAT THIS FILE CHECKS. That rounding up never pushes an ability outside what the
design allows. Landing later than designed is safe for the player, who is being
given more time to walk clear than promised, but it is not free: the design also
requires a telegraph to fit inside half the ability's cycle, and that bound is
against the real telegraph rather than the designed one.

WHY IT IS A PYTHON TEST. Continuous integration never builds the C++ and never
opens the editor. The automation tests in
`game/Source/Cataclysm/Tests/CataclysmWindUpTimingTests.cpp` check the counting
against a live controller and only ever run on a developer's machine.
"""

from __future__ import annotations

import math
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CONTROLLER_H = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmEnemyController.h")
CONTROLLER_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                  / "CataclysmEnemyController.cpp")
BRUTE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmBruteCharacter.h")

sys.path.insert(0, str(REPO_ROOT / "sim"))

#: Which C++ constant holds each Brute ability's telegraph, against the name the
#: model knows the ability by.
BRUTE_TELEGRAPHS: tuple[tuple[str, str], ...] = (
    ("Stomp", "StompWindUpSeconds"),
    ("Rip and Toss", "RockThrowWindUpSeconds"),
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


@pytest.fixture(scope="module")
def pass_seconds() -> float:
    return constant(CONTROLLER_H, "ThinkIntervalSeconds")


def effective_telegraph(wind_up: float, pass_seconds: float) -> float:
    """What the telegraph really lasts once rounded up to whole passes.

    The same arithmetic as ACataclysmEnemyController::PassesForWindUp, repeated
    here because continuous integration cannot run the C++. The automation tests
    check the C++ against a live controller.
    """
    if wind_up <= 0.0:
        return 0.0
    return max(1, math.ceil(wind_up / pass_seconds)) * pass_seconds


def test_the_landing_is_counted_in_passes_not_compared_against_a_clock() -> None:
    """The mechanism itself, so it cannot quietly go back to what it was.

    A clock comparison is what issue #413 is about. If the count is removed, the
    coin toss returns and nothing errors: the ability still lands, just a quarter
    of a second later than promised about half the time.
    """
    text = source(CONTROLLER_CPP)

    assert "WindUpPassesLeft = PassesForWindUp(" in text, (
        "CataclysmEnemyController.cpp does not count a wind-up into thinking "
        "passes when it starts one, so landing is decided by comparing clocks "
        "again and issue #413 is back."
    )

    assert re.search(r"if\s*\(\s*WindUpPassesLeft\s*>\s*0\s*&&\s*Now\s*<\s*"
                     r"WindUpLandsAt\s*\)", text), (
        "CataclysmEnemyController.cpp no longer lands an ability on its counted "
        "pass. The clock comparison on its own carries up to a frame of "
        "overshoot per pass, which for a telegraph sitting on a pass boundary "
        "decides a whole quarter of a second."
    )


def test_the_cpp_takes_the_ceiling_rather_than_rounding() -> None:
    """The player's guarantee is a floor, so the count must round up.

    THIS CHECKS THE C++ RATHER THAN RECOMPUTING IT. An earlier version of this
    test worked the rounding out in Python with `math.ceil` and asserted the
    result was never below the designed telegraph. That is true of `math.ceil`
    whatever the C++ does, so it could not fail and was worth nothing. What
    matters is which way the engine rounds, and that is only visible in the
    source text here.

    Rounding rather than taking the ceiling would land the Brute's 1.4 second
    stomp at 1.25, a tenth of a second sooner than the player was told they had.
    """
    body = re.search(
        r"int32 ACataclysmEnemyController::PassesForWindUp\(.*?\n\}",
        source(CONTROLLER_CPP), re.S)
    if body is None:
        pytest.fail(
            "CataclysmEnemyController.cpp has no PassesForWindUp. If it was "
            "renamed, rename it here; if it was deleted, nothing turns a "
            "telegraph into a number of passes and issue #413 is back."
        )

    assert "CeilToInt" in body.group(0), (
        "ACataclysmEnemyController::PassesForWindUp does not take the ceiling "
        "of the telegraph in thinking passes. Rounding or truncating would let "
        "an attack land before its own telegraph was over, which breaks the one "
        "promise a telegraph makes."
    )

    assert "RoundToInt" not in body.group(0), (
        "ACataclysmEnemyController::PassesForWindUp rounds the telegraph to the "
        "nearest whole pass. The Brute's 1.4 second stomp would then land at "
        "1.25 seconds."
    )


@pytest.mark.parametrize("ability_name,cpp_constant", BRUTE_TELEGRAPHS)
def test_the_real_telegraph_still_fits_inside_half_its_cycle(
        ability_name, cpp_constant, pass_seconds) -> None:
    """Rounding up must not push a telegraph past what the design allows.

    THE BOUND THIS CHECKS. The Attack Telegraphs subsection requires a marker's
    wind-up to fit inside half the ability's cycle, so that a creature is not
    telegraphing for most of the time between its attacks.
    `largest_telegraphed_radius` in sim/cataclysm_sim/enemy_abilities.py is that
    rule rearranged, and it is stated against the designed telegraph. Rounding up
    makes the real one longer, so the bound has to be re-checked against the real
    one -- which is what this does and nothing else did.
    """
    from cataclysm_sim.enemy_abilities import REACTION_ALLOWANCE, WALK_OUT_SPEED

    ability = brute_ability(ability_name)
    real = effective_telegraph(
        constant(BRUTE_HEADER, cpp_constant), pass_seconds)

    # The cycle an ability is telegraphed against: its own cooldown, or the
    # archetype's attack interval for a basic attack. Neither Brute ability here
    # is basic, so both use the cooldown.
    half_cycle = ability.cooldown / 2.0

    assert real <= half_cycle, (
        f"The {ability_name} really telegraphs for {real} s once rounded up to "
        f"whole thinking passes, and its cycle is {ability.cooldown} s, so it "
        f"spends more than half of it winding up. The design's own bound is "
        f"half the cycle."
    )

    # And the radius the rule allows at that cycle still covers what it marks,
    # which is the same bound stated the way the model states it.
    allowed_radius = WALK_OUT_SPEED * (half_cycle - REACTION_ALLOWANCE)
    assert float(ability.params["Radius"]) <= allowed_radius, (
        f"The {ability_name} marks {ability.params['Radius']} m and its "
        f"{ability.cooldown} s cycle allows at most {allowed_radius:.2f} m."
    )


def test_the_rock_throw_is_the_one_that_sat_on_a_pass_boundary(
        pass_seconds) -> None:
    """Named so the reason this work happened is not lost.

    A telegraph that is an exact multiple of the thinking pass is where the old
    clock comparison tossed a coin. The rock throw's 1.0 second telegraph against
    a 0.25 second pass is exactly that; the stomp's 1.4 sits 0.1 clear of the 1.5
    boundary, which is far more than a frame, and it never moved.

    This is not a rule that telegraphs must avoid boundaries -- the fix removed
    the need for that. It records which ability showed the fault, so that if the
    counting is ever removed, the failure below says where to look.
    """
    throw = constant(BRUTE_HEADER, "RockThrowWindUpSeconds")
    stomp = constant(BRUTE_HEADER, "StompWindUpSeconds")

    def sits_on_a_boundary(seconds: float) -> bool:
        passes = seconds / pass_seconds
        return abs(passes - round(passes)) < 1e-6

    assert sits_on_a_boundary(throw), (
        f"The rock throw's telegraph is no longer an exact multiple of the "
        f"{pass_seconds} s thinking pass. That is not a fault -- it is the "
        f"condition that made issue #413 visible. If it moved on purpose, this "
        f"test has nothing left to record and should be deleted along with the "
        f"note above."
    )

    assert not sits_on_a_boundary(stomp), (
        "The stomp's telegraph now sits on a pass boundary too. That is not a "
        "fault either, since landings are counted rather than compared, but the "
        "note above says the stomp is the ability that never moved and it would "
        "no longer be the useful contrast."
    )
