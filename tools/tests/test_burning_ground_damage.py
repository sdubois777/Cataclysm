"""Burning ground states what it deals, and the rule that sets it.

WHY THIS EXISTS. Issue #361. `ACataclysmGroundZone::SpawnRound` and `SpawnLine`
in `game/Source/Cataclysm/AbilitySystem/CataclysmGroundZone.cpp` take a
`DamagePerTick` parameter and the zone ticks once a second. **Nothing supplied
that number from data.** Every skill leaving burning ground stated only
`GroundRadius` and `GroundDuration`, so a skill that says it "leaves a pool of
lava" left a pool dealing whatever the caller happened to pass, and two skills
with the same radius and duration could silently deal different amounts.

WHAT WAS DECIDED, by the project owner on 2026-08-14: **standing in burning
ground for its whole life costs one hit of the skill that left it.** So the new
`GroundPercent` rider, the percent of the skill's damage the ground deals per
second, is 100 divided by `GroundDuration`.

WHY THE RULE IS CHECKED AND NOT JUST THE FIELD. A rider that every row carries
with an arbitrary number is barely better than no rider: the point of the
decision is the relationship, which is what stops a longer patch being
automatically a bigger one. So this file asserts the arithmetic on every row
rather than pinning 22 separate values.

ISSUE #361 SAYS EIGHT SKILLS LEAVE GROUND. It is 22, counted on 2026-08-14.
`test_the_rule_covers_a_realistic_number_of_skills` holds that, so a change that
quietly dropped most of them could not leave this file passing over a handful.

WHAT IS ASSERTED HERE.

    every skill with GroundRadius also has GroundDuration and GroundPercent
    on every one of them, GroundPercent times GroundDuration is 100
    the rider is declared in the generator, in the simulation's copy of that
      list, and in the design document's rider sentence
    the design document states the rule, not only the field
    the Hellhound's fire trail follows the same rule, so there is one rule
      rather than one for players and one for enemies
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
WEAPON_SKILLS = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"

sys.path.insert(0, str(REPO_ROOT / "sim"))
sys.path.insert(0, str(REPO_ROOT / "tools"))

#: The rider this issue added.
RIDER = "GroundPercent"

#: One hit, spread over the ground's whole life. Percent per second times
#: seconds.
ONE_HIT_PERCENT = 100.0

#: Rounding slack. The values are written to one decimal place, so a 3 second
#: patch is 33.3 rather than 33.333..., which totals 99.9.
TOLERANCE = 0.5

#: How many skills left ground when the rule was decided. A floor rather than an
#: exact pin: adding a skill that leaves ground is ordinary, and losing most of
#: them is not.
# TWELVE SINCE 2026-09-01, NOT TWENTY-TWO. The Demonic verb rewrite made
# burning ground the Greataxe's verb rather than the whole element's habit: the
# Sword consumes fire instead of leaving it, the Warhammer leaves geometry, the
# Whip drags people, and ten skills that trailed a patch because everything else
# did stopped doing so. This floor guards against the parser silently matching
# nothing, so it moves with the design rather than pinning a count the design no
# longer has.
FEWEST_SKILLS_LEAVING_GROUND = 10


def parse_params(text: str) -> dict[str, str]:
    return dict(pair.split("=", 1)
                for pair in (part.strip() for part in text.split(";"))
                if "=" in pair)


@pytest.fixture(scope="module")
def leaving_ground() -> list[tuple[str, dict[str, str]]]:
    if not WEAPON_SKILLS.is_file():
        pytest.skip("game/Data/WeaponSkills.csv has not been generated")
    with WEAPON_SKILLS.open(encoding="utf-8-sig", newline="") as handle:
        rows = list(csv.DictReader(handle))
    return [(row["SkillName"], parse_params(row["ShapeParams"]))
            for row in rows if "GroundRadius" in row["ShapeParams"]]


@pytest.fixture(scope="module")
def document() -> str:
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    return " ".join(GDD.read_text(encoding="utf-8").split())


# --------------------------------------------------------------------------
# The rule, on every row that it governs
# --------------------------------------------------------------------------

def test_the_rule_covers_a_realistic_number_of_skills(leaving_ground) -> None:
    """Guards everything below against passing over an empty or tiny list."""
    assert len(leaving_ground) >= FEWEST_SKILLS_LEAVING_GROUND, (
        f"only {len(leaving_ground)} skill(s) leave burning ground, where 22 "
        f"did when the rule was decided on 2026-08-14. Either the parser here "
        f"stopped matching ShapeParams, or most of them were removed. Every "
        f"check below would otherwise pass having compared almost nothing.")


def test_every_skill_leaving_ground_says_what_it_deals(leaving_ground) -> None:
    """The whole of issue #361. Before this, none of them did."""
    missing = sorted(name for name, params in leaving_ground
                     if RIDER not in params)
    assert not missing, (
        f"these skills leave burning ground without stating what it deals per "
        f"second: {missing}. Add {RIDER} to their Shape Params in "
        f"docs/All_Things_Cataclysm.xlsx. Issue #361.")

    no_duration = sorted(name for name, params in leaving_ground
                         if "GroundDuration" not in params)
    assert not no_duration, (
        f"these skills leave burning ground with no GroundDuration: "
        f"{no_duration}. {RIDER} is meaningless without it, because the rule "
        f"is stated in terms of it.")


def test_the_ground_is_worth_exactly_one_hit_over_its_life(
        leaving_ground) -> None:
    """The decision itself, as arithmetic rather than as 22 pinned numbers.

    This is what stops a longer patch being automatically a bigger one, which
    is the reason the rule was chosen over a flat percent per second.
    """
    wrong = []
    for name, params in leaving_ground:
        if RIDER not in params or "GroundDuration" not in params:
            continue  # reported by the test above
        total = float(params[RIDER]) * float(params["GroundDuration"])
        if abs(total - ONE_HIT_PERCENT) > TOLERANCE:
            wrong.append(f"{name}: {params[RIDER]}%/s over "
                         f"{params['GroundDuration']}s is {total:.1f}%")
    assert not wrong, (
        "these skills' burning ground is not worth one hit of the skill over "
        "its full life: " + "; ".join(wrong) + f". The rule decided on "
        f"2026-08-14 is that {RIDER} equals 100 divided by GroundDuration. "
        f"Issue #361.")


# --------------------------------------------------------------------------
# The rider is declared everywhere it has to be
# --------------------------------------------------------------------------

def test_the_generator_declares_the_rider() -> None:
    """`SHAPE_RIDERS` is the authoritative list and refuses anything not in it,
    so a rider missing there cannot be written to a row at all."""
    from generate_datatables import SHAPE_RIDERS

    assert RIDER in SHAPE_RIDERS, (
        f"tools/generate_datatables.py does not declare {RIDER} in "
        f"SHAPE_RIDERS, so no row could carry it. Issue #361.")


def test_the_simulation_carries_the_same_rider_list() -> None:
    """`RIDERS` in sim/cataclysm_sim/enemy_abilities.py is a copy of the
    generator's list. The two drifting is the failure it exists to prevent."""
    from cataclysm_sim.enemy_abilities import RIDERS

    assert RIDER in RIDERS, (
        f"sim/cataclysm_sim/enemy_abilities.py does not list {RIDER} in "
        f"RIDERS, so an enemy ability could not carry it and the two rider "
        f"lists disagree. Issue #361.")


def test_the_design_document_states_the_rule_and_not_only_the_field(
        document) -> None:
    """A field with no rule behind it is a number somebody will choose freshly
    for each new skill, which is the state issue #361 describes."""
    assert (f"Any shape may carry `GroundRadius`, `GroundDuration` and "
            f"`{RIDER}`") in document, (
        f"section V's rider sentence does not list {RIDER} among the riders any "
        f"shape may carry. Issue #361.")
    assert ("Standing in burning ground for its whole life costs one hit of the "
            "skill that left it") in document, (
        "the design document states the GroundPercent field without the rule "
        "that sets its value. The rule is what stops a longer patch being "
        "automatically a bigger one, and it is the thing that was decided. "
        "Issue #361.")
    assert "100 divided by `GroundDuration`" in document, (
        "the design document states the rule in words without the arithmetic, "
        "so a reader adding a skill has to derive the figure. Issue #361.")


def test_the_hellhound_trail_follows_the_same_rule() -> None:
    """One rule rather than one for players and one for enemies.

    The Hellhound's fire trail already worked this way and said so in prose --
    "a quarter of one of its bites per tick, so that standing in it for the
    whole 4 seconds costs exactly one bite". That is exactly 100 / 4.
    """
    from cataclysm_sim.enemy_abilities import ABILITIES

    trails = [(name, ability)
              for name, entries in ABILITIES.items()
              for ability in entries
              if "GroundRadius" in ability.params]
    assert trails, (
        "no enemy ability leaves burning ground. The Hellhound's Hellrush "
        "does, and this check compares it against the same rule the player "
        "skills follow.")

    for name, ability in trails:
        params = ability.params
        assert RIDER in params, (
            f"the {name}'s {ability.name} leaves burning ground without "
            f"stating {RIDER}. The rule applies to enemy abilities too. "
            f"Issue #361.")
        total = float(params[RIDER]) * float(params["GroundDuration"])
        assert abs(total - ONE_HIT_PERCENT) <= TOLERANCE, (
            f"the {name}'s {ability.name} ground is worth {total:.1f}% of a "
            f"hit over its life, not {ONE_HIT_PERCENT}%. Issue #361.")


def test_the_document_no_longer_says_the_figure_has_nowhere_to_live(
        document) -> None:
    """The sentence that recorded the gap. Leaving it would tell a reader the
    field does not exist while 22 rows carry it."""
    assert "That last figure has nowhere to live in data yet" not in document, (
        "the Hellhound section still says the burning ground figure has "
        "nowhere to live in data. Issue #361 added GroundPercent on "
        "2026-08-14 and the trail carries it.")


def test_the_regex_used_here_would_notice_a_missing_semicolon(
        leaving_ground) -> None:
    """The parser in this file splits on ';' and '='. A row whose ShapeParams
    lost a separator would parse into one giant key and every check above would
    skip it as having no GroundPercent -- which the first test would catch, but
    only if it dropped below the floor. This asserts the shape directly."""
    for name, params in leaving_ground:
        assert all(re.fullmatch(r"[A-Za-z]+", key) for key in params), (
            f"{name}'s ShapeParams parsed into keys that are not plain names: "
            f"{sorted(params)}. A separator is probably missing in "
            f"docs/All_Things_Cataclysm.xlsx.")
