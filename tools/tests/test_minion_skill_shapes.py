"""Every minion skill says, in data, what it produces and how many.

Issue #338. Three of the six skills tagged `Type.Minion` had an empty `Shape`
and an empty `ShapeParams`, so every number they stated lived only in the prose
of their description where no code could read it.
"""

from __future__ import annotations

import csv
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import generate_datatables as gen  # noqa: E402


def skills() -> list[dict]:
    path = ROOT / "game" / "Data" / "WeaponSkills.csv"
    with path.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def minion_skills() -> list[dict]:
    return [r for r in skills() if "Type.Minion" in r["Tags"]]


def minion_types() -> set[str]:
    path = ROOT / "game" / "Data" / "MinionTypes.csv"
    with path.open(encoding="utf-8") as handle:
        return {r["Name"] for r in csv.DictReader(handle)}


def params_of(row: dict) -> dict[str, str]:
    return gen.parse_shape_params(row["ShapeParams"], row["Shape"],
                                  f"WeaponSkills/{row['Name']}")


# --------------------------------------------------------------------------
# Nothing is left in prose
# --------------------------------------------------------------------------

def test_every_minion_skill_has_a_shape_and_parameters():
    """The state issue #338 was opened about: three of the six had neither."""
    for row in minion_skills():
        assert row["Shape"], f"{row['SkillName']} has no Shape"
        assert row["ShapeParams"], f"{row['SkillName']} has no ShapeParams"


#: Skills that produce a minion without naming a type, and why.
#:
#: A THRALL'S TYPE IS THE ENEMY IT WAS. Subjugate does not create a creature, it
#: takes one that is already standing there, so there is no row in the Minion
#: Types sheet for what it produces and there cannot be: the stat block is
#: whatever the possessed enemy already had. `Possess=1` is what says so, and a
#: skill setting it is exempt from naming a type rather than being allowed to
#: forget to.
def possesses(row) -> bool:
    return params_of(row).get("Possess") == "1"


def test_every_minion_skill_names_what_it_produces():
    for row in minion_skills():
        if possesses(row):
            continue
        produced = params_of(row).get("Minions")
        assert produced, (
            f"{row['SkillName']} does not say which minion type it produces, "
            "so nothing can find its stat block")


def test_every_type_a_skill_names_has_a_stat_block():
    known = minion_types()
    for row in minion_skills():
        if possesses(row):
            continue
        for name in gen.parse_minions(params_of(row)["Minions"], row["Name"]):
            assert name in known, (
                f"{row['SkillName']} produces {name!r} and no such row exists "
                "in the minion type table")


def test_the_shape_agrees_with_the_tag_the_skill_already_carried():
    """The summon-versus-deployable split was already in the data as a tag
    before issue #338 gave it a Shape. The two must not disagree."""
    for row in minion_skills():
        if row["Shape"] == "Deployable":
            assert "Type.Deployable" in row["Tags"], row["SkillName"]
        elif row["Shape"] == "Summon":
            assert "Type.Summon" in row["Tags"], row["SkillName"]
        else:
            pytest.fail(f"{row['SkillName']} has shape {row['Shape']!r}")


def test_the_three_deployables_are_the_three_that_were_empty():
    deployables = sorted(r["SkillName"] for r in minion_skills()
                         if r["Shape"] == "Deployable")
    assert deployables == ["Ballista", "Bolt Turret", "Iron Fortress"]


def test_one_skill_produces_two_kinds_and_that_is_why_counts_are_in_the_list():
    """Iron Fortress is the reason `Minions` carries counts at all. A single
    `Count` cannot say two ballistae AND three spike traps."""
    row = next(r for r in minion_skills() if r["SkillName"] == "Iron Fortress")
    produced = gen.parse_minions(params_of(row)["Minions"], "Iron Fortress")
    assert produced == {"Ballista": 2, "SpikeTrap": 3}
    assert "Count" not in params_of(row)


def test_iron_fortress_states_its_health_override():
    """Its description says its gadgets "have 50% increased HP", which is a
    per-skill override of the type's own health."""
    row = next(r for r in minion_skills() if r["SkillName"] == "Iron Fortress")
    assert float(params_of(row)["HealthPercent"]) == 150.0


@pytest.mark.parametrize("skill,seconds", [
    ("Bolt Turret", 5), ("Ballista", 8), ("Iron Fortress", 20),
])
def test_the_durations_that_were_only_in_prose_are_now_in_the_data(skill, seconds):
    row = next(r for r in minion_skills() if r["SkillName"] == skill)
    assert float(params_of(row)["Duration"]) == float(seconds)
    assert f"{seconds} second" in row["SkillDescription"], (
        "the description no longer states this duration, so the two have "
        "drifted apart")


# --------------------------------------------------------------------------
# The generator's guards
# --------------------------------------------------------------------------

def test_deployable_is_the_eighth_shape():
    assert len(gen.SHAPE_PARAMS) == 8
    assert "Deployable" in gen.SHAPE_PARAMS


@pytest.mark.parametrize("written,expected", [
    ("Imp:1", {"Imp": 1}),
    ("Ballista:2, SpikeTrap:3", {"Ballista": 2, "SpikeTrap": 3}),
    ("  Mote:2  ", {"Mote": 2}),
])
def test_a_well_formed_minion_list_is_read(written, expected):
    assert gen.parse_minions(written, "test") == expected


@pytest.mark.parametrize("written,message", [
    ("Imp", "not Type:Count"),
    ("Imp:", "not Type:Count"),
    ("Imp:0", "says nothing"),
    ("Imp:1, Imp:2", "listed twice"),
    ("", "names nothing"),
])
def test_a_malformed_minion_list_is_refused(written, message):
    with pytest.raises(gen.DataError, match=message):
        gen.parse_minions(written, "test")


def _tables(skill_params, types=("Imp",)):
    """The two tables `validate_minion_references` reads, built by hand."""
    return {"WeaponSkills": [{"Name": "Test", "Shape": "Summon",
                              "ShapeParams": skill_params}],
            "MinionTypes": [{"Name": n} for n in types]}


def test_a_skill_producing_a_type_with_no_stat_block_is_refused():
    problems = gen.validate_minion_references(
        _tables("Count=1; Minions=Wraith:1"))
    assert problems and "Wraith" in problems[0]


def test_a_count_disagreeing_with_the_minion_list_is_refused():
    problems = gen.validate_minion_references(
        _tables("Count=3; Minions=Imp:1"))
    assert problems and "One of the two is wrong" in problems[0]


def test_a_count_agreeing_with_the_minion_list_is_accepted():
    """A guard that rejects everything proves nothing."""
    assert gen.validate_minion_references(
        _tables("Count=1; Minions=Imp:1")) == []


def test_the_real_workbook_has_no_dangling_minion_reference():
    """The same check the generator runs, against the committed data."""
    tables = {
        "WeaponSkills": skills(),
        "MinionTypes": [{"Name": n} for n in minion_types()],
    }
    assert gen.validate_minion_references(tables) == []
