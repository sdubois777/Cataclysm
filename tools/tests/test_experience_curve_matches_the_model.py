"""The experience curve in the design document, against the model that produced it.

WHAT THIS FILE IS ABOUT. `docs/Cataclysm_GDD_v2.md` section XII, under "The
Experience Curve", states a formula, a table of eight levels and their cumulative
costs, the level a character reaches at the end of each difficulty tier, and the
size of the whole climb in dungeons and hours. Every one of those was computed by
`sim/analyse_experience_curve.py` and then typed into a document by hand.

A HAND-TYPED NUMBER UNDER A COMPUTED TABLE IS THE FAILURE THIS PROJECT KEEPS
HAVING. Issue #6 is the same shape in `sim/cataclysm_sim/scoring.py`, where a
docstring quoted four scores that had been wrong for five months. The numbers
here depend on the Enemy Score model, the dungeon floor ranges in
`sim/cataclysm_sim/config.py`, the creature density in
`game/Source/Cataclysm/Dungeon/CataclysmFloorPopulation.h` and the spawn weights
in `game/Data/EnemyRarities.csv`. Any of those moving silently invalidates the
document, and none of them has any reason to think about the experience curve
when it moves.

So this file recomputes the document's figures and fails when they disagree.

WHAT IT DELIBERATELY DOES NOT CHECK. Whether the curve is a good one. That was
decided by the project owner on 2026-08-24 and the reasoning is in
`docs/DECISIONS.md`. This file only checks that the document still says what the
model computes.

WHAT IS GUARDED HERE AND WHAT IS GUARDED NEXT DOOR. Eight deliberate breaks were
run against this file. Seven fail here: any figure edited in the document, and
the growth rate, the cost of level 2 or the campaign length edited in the model.
The eighth -- the creature density moving -- passes here and fails in
`sim/tests/test_analysis_scripts.py`, in four tests including
`test_eight_campaigns_pay_for_the_decided_climb`. That is the right split rather
than a gap: the document's stated size is eight campaigns of 26 dungeons at two
minutes a floor, which the density does not enter, while whether those campaigns
still PAY for the climb is exactly what the density moves. Do not add a density
check here without deleting that one.

THE DECIDED CONSTANTS ARE THE ONE THING ALLOWED TO BE TYPED, in
`analyse_experience_curve.DECIDED_RATE` and `DECIDED_LEVEL_2_COST`. They are a
choice rather than a measurement, so there is nothing to derive them from.
`sim/tests/test_analysis_scripts.py` checks that they are the fitted rate rounded
and that eight campaigns still pay for the climb they produce.

Issue #50.
"""

from __future__ import annotations

import contextlib
import io
import pathlib
import runpy

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
SCRIPT = REPO_ROOT / "sim" / "analyse_experience_curve.py"

#: The heading the curve lives under. Named here so a rename fails loudly rather
#: than quietly making every check below pass against an empty string.
SECTION = "## **The Experience Curve**"


@pytest.fixture(scope="module")
def model() -> dict:
    """The analysis script's globals, with its printed report thrown away.

    Run through `runpy` rather than imported, because `sim/` is not a package
    this test's own path can reach and the script prints its whole report at
    import time by design.
    """
    with contextlib.redirect_stdout(io.StringIO()):
        return runpy.run_path(str(SCRIPT))


@pytest.fixture(scope="module")
def section() -> str:
    """The experience curve section of the design document, alone.

    Bounded at the next heading so a figure that moves out of the section is
    noticed rather than found elsewhere in a 6,500 line file.
    """
    text = GDD.read_text(encoding="utf-8")
    assert SECTION in text, (
        f"docs/Cataclysm_GDD_v2.md no longer has a {SECTION!r} heading. Either "
        f"it was renamed, in which case rename SECTION here too, or the "
        f"experience curve has been removed from the design.")
    after = text.split(SECTION, 1)[1]
    return after.split("\n## ", 1)[0]


def test_the_stated_formula_is_the_decided_one(model, section):
    """The formula is what an implementer types in. It is checked first."""
    rate, scale = model["DECIDED_RATE"], model["DECIDED_LEVEL_2_COST"]
    assert f"cost of level L = {scale:,.0f} × {1 + rate:g} ^ (L − 2)" in section, (
        f"the document's formula no longer matches DECIDED_RATE {rate} and "
        f"DECIDED_LEVEL_2_COST {scale:,.0f}")
    assert f"A level costs {rate:.1%} more than the level below it" in section
    assert f"Level 2 costs {scale:,.0f}." in section


def test_every_row_of_the_stated_cost_table_is_recomputed(model, section):
    """Eight rows of level, cost and cumulative cost, each checked against the
    curve rather than against the row above it."""
    level_cost, total = model["level_cost"], model["total_experience"]
    rate, scale = model["DECIDED_RATE"], model["DECIDED_LEVEL_2_COST"]

    checked = 0
    for level in (2, 10, 25, 50, 75, 90, 100):
        row = (f"| {level} | {level_cost(level, rate, scale):,.0f} | "
               f"{total(rate, scale, level):,.0f} |")
        assert row in section, (
            f"the cost table row for level {level} should read {row!r} and does "
            f"not. The curve moved, or the document did.")
        checked += 1
    assert checked == 7, "the loop above stopped checking rows"


def test_the_stated_level_at_the_end_of_each_tier_is_recomputed(model, section):
    """The row that says what level the character is when each difficulty tier's
    campaign ends. This is the figure the growth rate was chosen to control, so
    it is the one most worth pinning."""
    levels = model["levels_at_tier_ends"](model["DECIDED_RATE"], model["PER_DUNGEON"])
    row = "| Character level | " + " | ".join(str(level) for level in levels) + " |"
    assert row in section, (
        f"the design document should say the character reaches {levels} at the "
        f"end of each difficulty tier's campaign, as a row reading {row!r}")
    assert levels[-1] == model["MAX_LEVEL"], (
        "the character no longer reaches the maximum level at the end of the "
        "last campaign, which is what the size of the climb was derived to do")


def test_the_stated_size_of_the_climb_is_recomputed(model, section):
    """208 dungeons and 347 hours. Both are derived -- eight campaigns of 26
    dungeons, at two minutes a floor -- so both move if the campaign length,
    the floor count or the minutes move."""
    dungeons = model["CLIMB_DUNGEONS"]
    played = model["hours"](dungeons, model["WHOLE_FLOORS"])
    assert f"**{dungeons} dungeons and {played:,.0f} hours**" in section, (
        f"the design document should say the climb is {dungeons} dungeons and "
        f"{played:,.0f} hours")
    assert f"A campaign is about {model['CAMPAIGN_DUNGEONS']} dungeons" in section
    assert f"a dungeon averages {model['WHOLE_FLOORS']} floors" in section
    assert f"at {model['MINUTES_PER_FLOOR']:g} minutes a floor" in section


def test_the_stated_creature_values_are_recomputed(model, section):
    """The two Enemy Score figures quoted to show what a kill is worth. They come
    straight out of the power model, which is a port that has drifted twice."""
    weights, floors = model["WEIGHTS"], model["WHOLE_FLOORS"]
    # THE SPAWN-WEIGHTED AVERAGE, NOT A COMMON. The document said "a Common
    # enemy" over these two numbers when it was first written, and a Common on
    # that floor is 407 rather than 420. Averaging over the rarities that
    # actually spawn is what produces both figures, and this test caught it.
    tier_1 = model["creature_experience"](1, floors, floors, weights)
    tier_8 = model["creature_experience"](8, floors, floors, weights)

    assert f"worth {tier_1:,.0f} at difficulty tier 1" in section
    assert f"{tier_8:,.0f} at tier 8" in section, (
        f"the design document quotes a tier 8 creature at a value the Enemy "
        f"Score model no longer produces. It should be {tier_8:,.0f}.")


def test_the_stated_path_of_exile_checkpoints_are_recomputed(model, section):
    """The three checkpoints are the whole argument for the rate. Two of them are
    unfitted agreements, and the argument dies quietly if they stop agreeing."""
    rate = model["DECIDED_RATE"]
    whole = model["total_experience"](rate, 1.0)

    by_50 = model["total_experience"](rate, 1.0, 50) / whole
    last = model["level_cost"](model["MAX_LEVEL"], rate, 1.0) / whole
    assert f"{by_50:.2%} of the climb is spent by level 50" in section
    assert f"the last level alone is {last:.2%}" in section

    assert f"against their {model['POE_SHARE_BY_50']:.2%}" in section
    assert f"against their {model['POE_SHARE_LAST_LEVEL']:.2%}" in section
    assert f"which is {model['POE_SHARE_BY_90']:.2%}" in section


def test_the_stated_out_levelling_shares_are_recomputed(model, section):
    """The row showing how far the character runs ahead of each difficulty tier.
    It is what makes the chosen rate defensible, so it has to stay true."""
    levels = model["levels_at_tier_ends"](model["DECIDED_RATE"], model["PER_DUNGEON"])
    weight = model["LEVEL_WEIGHT"]
    anchors = model["scoring"].PLAYER_MAX_SCORES

    shares = [weight * levels[tier - 2] / anchors[tier - 1] for tier in range(2, 9)]
    row = ("| share of the tier's starting power carried in from level alone | "
           + " | ".join(f"{share:.0%}" for share in shares) + " |")
    assert row in section, f"the out-levelling row should read {row!r}"

    entering = "| at character level | " + " | ".join(
        str(levels[tier - 2]) for tier in range(2, 9)) + " |"
    assert entering in section, f"the level-on-entering row should read {entering!r}"


def test_the_section_says_the_dungeon_counts_assume_a_full_clear(section):
    """Every figure in the section is the optimistic end, and the section has to
    say so or a reader takes it as measured. Issue #925."""
    assert "clears every floor" in section
    assert "runs for the stairs takes longer" in section


def test_the_section_points_at_where_the_reasoning_lives(section):
    """A design document states decisions and not arguments. The two files that
    carry the working are named so the next person can find them."""
    assert "sim/analyse_experience_curve.py" in section
    assert "docs/DECISIONS.md" in section
