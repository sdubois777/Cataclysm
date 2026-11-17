"""The Unreal experience curve must match the simulation's.

WHY THIS EXISTS. `UCataclysmExperience` in
`game/Source/Cataclysm/Character/CataclysmExperience.cpp` is a port of the curve
decided in `sim/analyse_experience_curve.py`. Two copies of a number are two
numbers, and this repository has already learned what happens next: the power
model in `sim/cataclysm_sim/scoring.py` silently drifted from its own source
twice, which is why `CLAUDE.md` carries a rule about it.

WHAT MAKES THIS ONE WORSE THAN THE USUAL DRIFT. The curve decides what a save
record holds. `FCataclysmCharacterRecord` stores a character's level beside its
progress into that level, so a curve that disagreed with itself by a single unit
would put a character at a different level on the two sides. That is why the
rounding rule is compared here and not just the constants.

WHAT IT COMPARES. The three constants, and the rounding rule, and that the two
curves inside the Python model have not parted company.

WHAT IT DOES NOT CHECK, AND WHERE THAT IS CHECKED INSTEAD. That the C++ produces
those numbers when it runs, and that granting experience raises a level and
carries the remainder. Those are Unreal automation tests under the
`Cataclysm.Experience` prefix, in
`game/Source/Cataclysm/Tests/CataclysmExperienceTests.cpp`, and they compute the
expected costs from the same two constants rather than from a typed table. A
test written against a constant cannot notice that the code ignores it, and a
test that reads the constant out of the source cannot notice that the constant
is wrong, so both halves are needed.

Issue #50.
"""

from __future__ import annotations

import contextlib
import io
import pathlib
import re
import runpy

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
          / "CataclysmExperience.h")
SOURCE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
          / "CataclysmExperience.cpp")
SCRIPT = REPO_ROOT / "sim" / "analyse_experience_curve.py"

#: Levels compared one by one. The two ends, the two the design document quotes,
#: and three with nothing special about them, because a rate applied to the
#: wrong exponent agrees at level 2 and nowhere else.
SAMPLE_LEVELS = (2, 3, 10, 25, 37, 50, 64, 75, 90, 99, 100)


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def without_comments(text: str) -> str:
    """The C++ with everything after a `//` on each line removed.

    NEEDED, NOT TIDINESS. The port carries a comment saying it deliberately does
    NOT use `FMath::RoundToInt64`, and a test searching the raw file for that
    name finds the comment and fails on the explanation for the very thing it is
    checking. Reading comments as if they were code makes a well-documented file
    look broken.

    Split rather than a regular expression, because the pattern for "up to the
    end of the line" needs a backslash escape and this file is edited through
    shells that eat them.
    """
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def constant(text: str, name: str, pattern: str) -> str:
    """One `static constexpr` value out of the header, by name."""
    match = re.search(
        rf"static\s+constexpr\s+[\w:]+\s+{name}\s*=\s*({pattern})\s*;", text)
    if not match:
        pytest.fail(f"could not find {name} in {HEADER.name}; has it been renamed?")
    return match.group(1)


@pytest.fixture(scope="module")
def model() -> dict:
    """The analysis script's globals, with its printed report thrown away."""
    with contextlib.redirect_stdout(io.StringIO()):
        return runpy.run_path(str(SCRIPT))


@pytest.fixture(scope="module")
def header() -> str:
    return read(HEADER)


@pytest.fixture(scope="module")
def source() -> str:
    return read(SOURCE)


def test_the_maximum_level_matches(model, header):
    assert int(constant(header, "MaxLevel", r"\d+")) == model["MAX_LEVEL"]


def test_the_growth_per_level_matches(model, header):
    """8.2%. The one number the whole shape rests on."""
    stated = float(constant(header, "GrowthPerLevel", r"[\d.]+"))
    assert stated == pytest.approx(model["DECIDED_RATE"]), (
        f"the Unreal curve grows {stated:.4%} a level and the model "
        f"{model['DECIDED_RATE']:.4%}")


def test_the_cost_of_level_two_matches(model, header):
    """230,000. The one number the whole size rests on, and it has to be the
    model's level 2 cost rather than merely the same literal, because the model
    rounds it and this does not."""
    stated = int(constant(header, "SecondLevelCost", r"\d+"))
    assert stated == int(model["DECIDED_LEVEL_2_COST"])
    assert stated == model["whole_level_cost"](2)


def test_the_two_curves_the_port_could_have_copied_still_differ(model):
    """The Python model has a float curve and a whole-number one, and the port
    had to pick the whole-number one. If they ever became the same thing this
    test file would be checking nothing, so the difference is asserted here as
    well as in the model's own tests."""
    total = model["whole_total_to_reach"](model["MAX_LEVEL"])
    closed = model["total_experience"](model["DECIDED_RATE"],
                                       model["DECIDED_LEVEL_2_COST"])
    assert total != int(closed + 0.5)
    for level in SAMPLE_LEVELS:
        assert model["whole_level_cost"](level) > 0, (
            f"the model says level {level} costs nothing")


def test_the_source_rounds_the_way_the_model_does(source):
    """floor(x + 0.5), and NOT `FMath::RoundToInt64`.

    This is the one part of the port a constant comparison cannot see. The
    engine's rounding helper does not promise floor-of-x-plus-a-half on every
    platform, and C's `round` sends a half away from zero, which is the same
    thing for a positive number but says something different. The model rounds
    like `_js_round` in `sim/cataclysm_sim/scoring.py`, which is floor(x + 0.5),
    so the C++ has to say that outright.
    """
    code = without_comments(source)
    assert "FloorToDouble(Raw + 0.5)" in code, (
        "the port no longer rounds a level's cost with floor(x + 0.5). Whatever "
        "it does instead has to agree with _js_round in "
        "sim/cataclysm_sim/scoring.py on a half, or the two curves will differ "
        "by one at some level and put a character at a different level on each "
        "side.")
    assert "FMath::RoundToInt" not in code, (
        "the port now uses the engine's rounding helper, which does not promise "
        "floor(x + 0.5) on every platform")


def test_the_total_is_summed_from_rounded_costs_and_says_why(source):
    """The distinction that is easy to collapse and costs 5 over the climb: the
    sum of 99 roundings is not the rounding of a geometric sum. The Python model
    has a test asserting the two differ; this checks the C++ took the same side
    and left the reason behind for whoever wants to replace it with a one-liner."""
    parts = source.split("int64 UCataclysmExperience::TotalToReach", 1)
    assert len(parts) == 2, "TotalToReach has been renamed or removed"
    body = parts[1].split("\nint32 ", 1)[0]

    assert "LevelCosts()[Step]" in without_comments(body), (
        "TotalToReach no longer sums the per-level costs. A closed-form "
        "geometric sum gives a number 5 lower over the whole climb.")
    assert "closed form" in body or "closed-form" in body, (
        "the reason for summing rather than using a closed form is no longer "
        "written down, so the next person will replace it with the one-liner")


def test_the_port_names_what_it_cannot_do_yet(header):
    """The design says an enemy's Enemy Score IS the experience it grants, and
    Enemy Score has no port at all. A header that did not say so would read as
    though the levelling model were finished."""
    assert "Enemy Score" in header
    assert "#926" in header, (
        "the header no longer names the issue that owns the Enemy Score port, "
        "so a reader has no way to find out why nothing feeds this curve")
    assert "#41" in header, (
        "the header no longer names the issue that owns a dungeon's floor "
        "count, which is one of the three inputs an Enemy Score port needs and "
        "which the game does not have")


def test_the_port_keeps_progress_into_a_level_rather_than_a_running_total(header):
    """The save record stores a level beside progress into it. That is a choice
    with a reason -- a running total would make the level derivable and a retune
    of the curve would silently move every character -- and the header has to
    carry the reason or the next person will 'simplify' it."""
    assert "PROGRESS INTO THE CURRENT LEVEL, NOT A RUNNING TOTAL" in header
    assert "retune" in header
