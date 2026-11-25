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
import reapply_death_skills_workbook as death_rows  # noqa: E402


def skills() -> list[dict]:
    path = ROOT / "game" / "Data" / "WeaponSkills.csv"
    with path.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def waiting_on_code() -> set[str]:
    """The Death rows entered with no shape because the code they need is not built yet. Issue #2885.

    NOT PROSE, WHICH IS WHAT ISSUE #338 WAS ABOUT. Each one's whole shape is data in
    `tools/reapply_death_skills_workbook.py`, beside the code piece it waits on, and
    the script writes it into the workbook the moment that piece is built. Until then
    the game grants the row as the designed placeholder that does nothing.
    """
    return {row.csv_name for row in death_rows.ROWS if not row.works}


def minion_skills() -> list[dict]:
    """Every row tagged `Type.Minion`, less the Death rows waiting on code."""
    waiting = waiting_on_code()
    return [r for r in skills() if "Type.Minion" in r["Tags"] and r["Name"] not in waiting]


def test_a_minion_skill_waiting_on_code_states_no_half_of_a_shape():
    """THE EXEMPTION ABOVE IS NARROW, and this is what keeps it so. A waiting row is
    left out of every check below only while it has neither a Shape nor Shape Params,
    which is how the re-apply script enters it; a row given half a shape by hand, or a
    row the script now writes whole, is checked like any other."""
    waiting = waiting_on_code()
    tagged = [r for r in skills() if "Type.Minion" in r["Tags"] and r["Name"] in waiting]
    assert tagged, "no Death minion row waits on code, so the exemption guards nothing and can go"
    for row in tagged:
        assert not row["Shape"] and not row["ShapeParams"], (
            f"{row['Name']} ({row['SkillName']}) is listed as waiting on "
            f"{death_rows.ROWS[[r.csv_name for r in death_rows.ROWS].index(row['Name'])].waits_on} and has "
            f"Shape {row['Shape']!r} and Shape Params {row['ShapeParams']!r}")


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


#: A CURSE UNDER WHICH THE DYING RISE PRODUCES A MINION AND IS NO SUMMON. Issue #2885, the Death draft's N5, slice
#: DS3: the Greataxe's Death Rattle is a Debuff tagged `Type.Minion`, whose rattled dead rise as husks. It names the
#: kind in `RisesAs` beside `OnDeath=Rise`, not in `Minions`, because it raises one per death rather than a count per
#: cast; every figure of what rises is read from the Summon row raising that kind (`validate_risen_kinds`).
def rises(row) -> bool:
    return params_of(row).get("OnDeath") == "Rise"


def produced_by(row) -> dict[str, int]:
    """What a minion skill produces: its `Minions`, or the one kind a rising curse's `RisesAs` names."""
    params = params_of(row)
    if rises(row):
        return {params["RisesAs"]: 1}
    return gen.parse_minions(params["Minions"], row["Name"])


def test_every_minion_skill_names_what_it_produces():
    for row in minion_skills():
        if possesses(row):
            continue
        produced = params_of(row).get("RisesAs" if rises(row) else "Minions")
        assert produced, (
            f"{row['SkillName']} does not say which minion type it produces, "
            "so nothing can find its stat block")


def test_every_type_a_skill_names_has_a_stat_block():
    known = minion_types()
    for row in minion_skills():
        if possesses(row):
            continue
        for name in produced_by(row):
            assert name in known, (
                f"{row['SkillName']} produces {name!r} and no such row exists "
                "in the minion type table")


def test_the_shape_agrees_with_the_tag_the_skill_already_carried():
    """The summon-versus-deployable split was already in the data as a tag
    before issue #338 gave it a Shape. The two must not disagree. A rising
    curse (N5) is a Debuff, and carries that tag."""
    for row in minion_skills():
        if row["Shape"] == "Deployable":
            assert "Type.Deployable" in row["Tags"], row["SkillName"]
        elif row["Shape"] == "Summon":
            assert "Type.Summon" in row["Tags"], row["SkillName"]
        elif row["Shape"] == "Debuff" and rises(row):
            assert "Type.Debuff" in row["Tags"], row["SkillName"]
        else:
            pytest.fail(f"{row['SkillName']} has shape {row['Shape']!r}")


def test_the_two_rising_curses_are_death_rattles_and_soul_snares_and_each_kind_has_a_stat_block():
    """THE CONTROL that the branch above is reached: two written rows rise, Death Rattle, raising a husk, and since slice
    DS4 the Wand's Soul Snare, raising a wisp, the same wisp Grave Mouth tears free."""
    rising = {r["SkillName"]: produced_by(r) for r in minion_skills() if r["Shape"] == "Debuff" and rises(r)}
    assert rising == {"Death Rattle": {"Husk": 1}, "Soul Snare": {"Wisp": 1}}
    assert {"Husk", "Wisp"} <= minion_types()


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


def test_a_taunting_row_is_read_on_a_summon_and_a_deployable():
    """THE CONTROL for the two refusals below: issue #2885's C3, `Taunts=1`, on
    Shallow Graves' own cell and on a placed machine's, parses."""
    assert gen.parse_shape_params(
        "Range=10; Radius=4; Count=4; Duration=8; Minions=Husk:4; FervourReserve=3; Taunts=1",
        "Summon", "Shallow Graves")["Taunts"] == "1"
    assert gen.parse_shape_params(
        "Radius=8; Count=1; Duration=5; Interval=1; Minions=BoltTurret:1; Taunts=1",
        "Deployable", "a taunting machine")["Taunts"] == "1"


def test_a_taunting_row_with_no_radius_is_refused():
    """A taunt reaches the enemies within the row's Radius of each minion, so
    without one it would taunt nobody and nothing would say so."""
    with pytest.raises(gen.DataError, match="no Radius above nought"):
        gen.parse_shape_params("Count=1; Duration=8; Minions=Imp:1; Taunts=1",
                               "Summon", "a taunt with no radius")


def test_a_taunting_row_that_possesses_is_refused():
    """A thrall is taken, not raised, so it never passes through `SummonOne`
    and nothing would be told to taunt."""
    with pytest.raises(gen.DataError, match="possesses"):
        gen.parse_shape_params("Range=15; Radius=1.5; MaxTargets=1; Possess=1; Taunts=1",
                               "Summon", "a taunting take")


def test_a_taunt_is_not_a_key_of_any_other_shape():
    """Only what a row raises or places can taunt; a strike stating it is refused
    as an unknown parameter, as every other misplaced key is."""
    with pytest.raises(gen.DataError, match="has no parameter 'Taunts'"):
        gen.parse_shape_params("Radius=4; Angle=90; Taunts=1", "Strike", "a taunting swing")


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


def test_a_summon_stating_a_count_cap_is_refused():
    """Issue #2787, branch B: nothing that reserves the class resource has a
    count cap, by the owner's ruling of 2026-11-20, so the reserve is the only
    limit and `UCataclysmSummonSkill::SummonOne` reads no `MaxActive`. Summon
    Imp's own cell as it stood before that turn."""
    problems = gen.refuse_a_count_cap_on_a_summon(
        _tables("Count=1; MaxActive=3; Duration=20; Radius=3; Minions=Imp:1"))
    assert len(problems) == 1 and "MaxActive=3" in problems[0], problems


def test_a_summon_with_no_cap_a_rift_and_a_deployable_are_left_alone():
    """THE CONTROLS, so the refusal above is about the cap and nothing else:
    Summon Imp's cell after branch B; a rift's pattern, a spawn every Interval,
    where `SpawnTick` reads MaxActive as how many one cast makes; and a War
    deployable, which keeps its cap until issue #1934."""
    assert gen.refuse_a_count_cap_on_a_summon(
        _tables("Count=1; Duration=20; Radius=3; Minions=Imp:1")) == []
    assert gen.refuse_a_count_cap_on_a_summon(
        _tables("Count=1; MaxActive=5; Duration=8; Interval=1; Minions=Imp:1")) == []
    deployable = {"WeaponSkills": [{"Name": "Turret", "Shape": "Deployable",
                                    "ShapeParams": "Count=1; MaxActive=1; Duration=20; "
                                                   "Minions=BoltTurret:1"}]}
    assert gen.refuse_a_count_cap_on_a_summon(deployable) == []


def test_no_shipped_summon_states_a_count_cap():
    """The same check the generator runs, against the committed data. It
    fails until branch B's workbook turn takes `MaxActive=3` off Summon Imp's
    row in the Weapon Skills sheet and `game/Data/WeaponSkills.csv` is
    generated again."""
    assert gen.refuse_a_count_cap_on_a_summon({"WeaponSkills": skills()}) == []


def test_the_real_workbook_has_no_dangling_minion_reference():
    """The same check the generator runs, against the committed data."""
    tables = {
        "WeaponSkills": skills(),
        "MinionTypes": [{"Name": n} for n in minion_types()],
    }
    assert gen.validate_minion_references(tables) == []
