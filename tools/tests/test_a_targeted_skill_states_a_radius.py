"""A skill that finds who it hits with its radius has to state one.

Issue #1519. Subjugate, the Staff's Demonic Ultimate, stated `Range=15` and no
`Radius`. `FCataclysmSkillShapeParams::RadiusCm` defaults to zero, every branch
of `UCataclysmSkillTemplate::ScaledRadiusCm` multiplies that zero, and
`UCataclysmSummonSkill::Possess` hands the result to a collision overlap, so the
skill searched a sphere of no size. It spent its cooldown, dealt no damage and
took nobody, eleven casts running.

NOTHING NOTICED FOR SEVEN DAYS, AND ONE TEST LOOKED AS THOUGH IT SHOULD HAVE.
`Cataclysm.Command.SubjugateTakesAnEnemyTheBlowLeftBelowHalfHealth` in
`game/Source/Cataclysm/Tests/CataclysmCommandTests.cpp` casts Subjugate and
checks it takes a wounded creature -- but it writes its own parameter string
with `Radius=15` in it, a figure that appears in no row of the real data. It
proved the mechanism and could not see the row. That is why the checks here read
the shipped data rather than a string written beside them.
"""

from __future__ import annotations

import csv
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import generate_datatables as gen  # noqa: E402


def skills() -> list[dict]:
    path = ROOT / "game" / "Data" / "WeaponSkills.csv"
    with path.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def params_of(row: dict) -> dict[str, str]:
    return gen.parse_shape_params(row["ShapeParams"], row["Shape"],
                                  f"WeaponSkills/{row['Name']}")


def as_table(rows: list[dict]) -> dict[str, list[dict]]:
    return {"WeaponSkills": rows}


def one_row(name: str, shape: str, params: str) -> dict:
    return {"Name": name, "Shape": shape, "ShapeParams": params}


# --------------------------------------------------------------------------
# The shipped data
# --------------------------------------------------------------------------

def test_the_shipped_rows_all_state_a_radius_where_one_is_read():
    """The bug itself, checked against the real file rather than a fixture."""
    assert gen.validate_targeted_shapes_state_a_radius(as_table(skills())) == []


def test_subjugate_states_a_radius_greater_than_zero():
    """Named on its own, so a failure says which skill rather than how many."""
    row = next(r for r in skills() if r["Name"] == "Demonic_Staff_Ultimate")
    assert row["SkillName"] == "Subjugate"
    written = params_of(row).get("Radius")
    assert written is not None, (
        "Subjugate states no Radius, so UCataclysmSummonSkill::Possess searches "
        "a sphere of size zero and can take nobody. Issue #1519.")
    assert float(written) > 0.0, f"Subjugate states Radius={written}"


# --------------------------------------------------------------------------
# The check refuses what it is for
#
# SYNTHETIC ROWS, BECAUSE A CHECK THAT PASSES ON GOOD DATA AND A CHECK THAT
# CANNOT FAIL LOOK IDENTICAL FROM THE OUTSIDE. These hand it the broken row the
# real file no longer has.
# --------------------------------------------------------------------------

def test_it_refuses_a_summon_that_possesses_and_states_no_radius():
    """Subjugate's row exactly as it was before this issue was fixed."""
    problems = gen.validate_targeted_shapes_state_a_radius(as_table([
        one_row("Demonic_Staff_Ultimate", "Summon",
                "Range=15; MaxTargets=1; Burn=1; Possess=1; "
                "FervourReserve=30; HealthThresholdPercent=50")]))
    assert len(problems) == 1
    assert "Demonic_Staff_Ultimate" in problems[0]
    assert "Radius" in problems[0]


def test_it_refuses_a_radius_written_as_zero():
    """A stated zero produces the same skill as a missing key."""
    problems = gen.validate_targeted_shapes_state_a_radius(as_table([
        one_row("Made_Up_Row", "Strike", "Radius=0; Angle=90")]))
    assert len(problems) == 1
    assert "Radius=0" in problems[0]


def test_it_refuses_every_shape_whose_search_reads_the_radius():
    """Strike, Projectile, Aura and Movement, each on its own."""
    rows = [
        one_row("A_Strike", "Strike", "Angle=90; MaxTargets=3"),
        one_row("A_Projectile", "Projectile", "Range=10; Speed=2000"),
        one_row("An_Aura", "Aura", "Duration=5"),
        one_row("A_Leap", "Movement", "Mode=Leap; Range=10"),
    ]
    problems = gen.validate_targeted_shapes_state_a_radius(as_table(rows))
    assert len(problems) == 4, problems
    for row in rows:
        assert any(row["Name"] in p for p in problems), row["Name"]


# --------------------------------------------------------------------------
# And stays quiet where a radius is not read
#
# THE HALF THAT KEEPS THE CHECK HONEST. Thirteen shipped rows state no radius --
# six self buffs, three deployables, three debuffs and one flickering movement --
# and all thirteen are correct, so a check that simply demanded one everywhere
# would have to be switched off the day it was written.
# --------------------------------------------------------------------------

def test_it_leaves_alone_the_shapes_that_never_read_a_radius():
    """Debuff searches Range, a Deployable searches nobody, a SelfBuff guards.

    `UCataclysmDebuffSkill::ActivateAbility` searches a sphere of `Range` around
    the caster and sorts what it finds by distance to the cursor; it never reads
    the radius. `UCataclysmSelfBuffSkill` guards both of its radius reads with
    `ScaledRadiusCm() > 0.0f`. A Deployable puts machines down and looks for
    nobody.
    """
    problems = gen.validate_targeted_shapes_state_a_radius(as_table([
        one_row("Demonic_Staff_Support", "Debuff",
                "Range=15; MaxTargets=1; EffectDuration=12; Effect=Quarry"),
        one_row("War_Spear_Special", "Deployable",
                "Count=1; Duration=8; Minions=Ballista:1; FervourReserve=5"),
        one_row("Demonic_Dagger_Support", "SelfBuff",
                "Duration=8; Requires=RearHit; RefundsCooldown=Movement"),
    ]))
    assert problems == []


def test_a_flickering_movement_needs_no_radius():
    """Everywhere at Once builds its circuit out of Range and returns early.

    `UCataclysmMovementSkill::ActivateAbility` handles `Mode=Flicker` in its own
    branch, which searches `Params.RangeCm` and then returns before the switch
    that reads the radius.
    """
    assert gen.validate_targeted_shapes_state_a_radius(as_table([
        one_row("Demonic_Dagger_Ultimate", "Movement",
                "Mode=Flicker; Range=10; Duration=4; Interval=0.33; "
                "Burn=1; RearHits=1; Untargetable=1")])) == []


def test_a_summon_that_makes_a_minion_needs_no_radius():
    """Only `Possess=1` searches. Summon Imp's radius is read by `Collapse`,
    which guards it with `ScaledRadiusCm() > 0.0f` the way the self buffs do."""
    assert gen.validate_targeted_shapes_state_a_radius(as_table([
        one_row("Made_Up_Summon", "Summon",
                "Count=1; MaxActive=3; Duration=20; Minions=Imp:1")])) == []


# --------------------------------------------------------------------------
# The rule matches the engine
# --------------------------------------------------------------------------

def test_every_shape_the_generator_knows_is_answered_one_way_or_the_other():
    """A ninth shape added to SHAPE_PARAMS must be classified deliberately.

    `shape_searches_with_the_radius` returns False for anything it does not
    recognise, which is the safe default and also a silent one: a new shape
    would be exempted by omission. This lists the eight answers so that adding a
    shape fails here and has to be thought about.
    """
    answers = {
        "Strike": True,
        "Projectile": True,
        "Aura": True,
        "Movement": True,
        "Summon": False,
        "Debuff": False,
        "Deployable": False,
        "SelfBuff": False,
    }
    assert set(answers) == set(gen.SHAPE_PARAMS), (
        "A shape was added or removed. Decide whether its template hands "
        "ScaledRadiusCm() to a targeting search, then say so here and in "
        "shape_searches_with_the_radius.")
    for shape, searches in answers.items():
        assert gen.shape_searches_with_the_radius(shape, {}) is searches, shape
