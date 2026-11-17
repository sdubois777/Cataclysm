"""The affix table says which stats are measured in percentage points.

WHAT WENT WRONG. `game/Data/Affixes.csv` carried a `ValueKind` column saying
which bucket of the stat pipeline a modifier joins -- flat addition or increase
-- and the item tool tip printed a percent sign for one and not the other. That
is right for the value kind and wrong for the stat: a FLAT addition to a stat
that is itself measured in percentage points needs the sign as much as an
increase does. Eleven affix lines read as bare numbers, so "+0.2 to life leech"
did not say whether that was 0.2 per cent, 0.2 health, or 0.2 of something else.
Issue #1224.

The fix is a `Percent` column filled in on the Affixes sheet of
`docs/All_Things_Cataclysm.xlsx`. The fact it records belongs to the STAT rather
than to the affix, and several stats have two affixes, so the two tests at the
bottom of this file are what stop the repetition drifting.
"""

from __future__ import annotations

import csv
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

AFFIXES = REPO_ROOT / "game" / "Data" / "Affixes.csv"
ITEM_BASES = REPO_ROOT / "game" / "Data" / "ItemBases.csv"


@pytest.fixture(scope="module")
def rows() -> list[dict]:
    with AFFIXES.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def percent_of(rows: list[dict], stat: str) -> bool:
    for row in rows:
        if row["Stat"] == stat:
            return row["Percent"] == "True"
    raise AssertionError(f"no affix grants {stat!r}")


#: Stats the code applies by dividing by 100, or by rolling against a number
#: from 0 to 100. Each was read out of the file named beside it.
PERCENTAGE_STATS = [
    ("evasion", "rolled against FRandRange(0, 100) in CataclysmDamageCalculation"),
    ("block_chance", "rolled against FRandRange(0, 100)"),
    ("crit_chance", "rolled against FRandRange(0, 100)"),
    ("crit_multiplier", "Damage *= Hit.CritMultiplier / 100"),
    ("crowd_control_resistance", "Total *= 1 - Resisted / 100"),
    ("damage_reduction", "Damage *= 1 - EffectiveDamageReduction / 100"),
    ("retaliation", "DamageTaken * Percent / 100 in CataclysmRetaliation"),
    ("life_leech", "UCataclysmLeech::AmountFrom multiplies the damage"),
    ("mana_leech", "the same"),
    ("energy_shield_leech", "the same"),
    ("penetration", "subtracted from a target's resistance"),
    ("cooldown_reduction", "the attribute holds a percentage"),
    ("magic_find", "1 + MagicFind / 100 in CataclysmDropRoll"),
]

#: Stats that are pools, ratings, counts or amounts per second.
POINT_STATS = [
    ("max_health", "a pool of health"),
    ("max_mana", "a pool of mana"),
    ("armor", "a rating, fed to 100 x A / (A + K)"),
    ("attack_damage", "damage"),
    ("health_regen", "health per second"),
    ("mana_regen", "mana per second"),
    ("movement_speed", "metres per second"),
]


@pytest.mark.parametrize("stat,why", PERCENTAGE_STATS)
def test_a_percentage_stat_is_marked_as_one(rows, stat, why) -> None:
    assert percent_of(rows, stat), (
        f"{stat} is a percentage -- {why} -- and the affix granting it says it "
        "is not, so the item tool tip prints its number with no percent sign")


@pytest.mark.parametrize("stat,why", POINT_STATS)
def test_a_stat_that_is_not_a_percentage_is_not_marked(rows, stat, why) -> None:
    assert not percent_of(rows, stat), (
        f"{stat} is {why} and the affix granting it claims it is a percentage, "
        "so the item tool tip prints a percent sign that is not true")


def test_the_resistance_and_ailment_families_are_percentages(rows) -> None:
    """Neither names a stat, and both are percentages.

    A resistance is applied as `Damage *= 1 - Resist / 100`. An ailment chance is
    capped at 100 and anything past that raises the effect's magnitude instead.
    """
    for row in rows:
        if row["AffixKind"] in ("Resistance", "Ailment"):
            assert row["Percent"] == "True", (
                f"{row['Name']} is a {row['AffixKind']} affix and every one of "
                "them grants a percentage")


def test_a_hybrid_carries_no_answer_of_its_own(rows) -> None:
    """Its two halves each carry theirs, and they can disagree.

    `Magic find and loot quantity` grants one percentage and one increase. An
    answer on the hybrid row itself could only be wrong about one of them.
    """
    for row in rows:
        if row["AffixKind"] == "Hybrid":
            assert row["Percent"] == "False", (
                f"{row['Name']} is a hybrid and answers for its halves")


# --------------------------------------------------------------------------
# The two guards in the generator, shown failing
# --------------------------------------------------------------------------

def test_two_affixes_disagreeing_about_one_stat_is_refused() -> None:
    tables = {"Affixes": [
        {"Name": "Stat_Flat_critical_strike_chance", "Stat": "crit_chance",
         "Percent": True},
        {"Name": "Stat_Increased_critical_strike_chance", "Stat": "crit_chance",
         "Percent": False},
    ]}
    problems = gen.validate_affix_percent_agrees(tables)
    assert len(problems) == 1, problems
    assert "crit_chance" in problems[0]


def test_two_affixes_agreeing_about_one_stat_is_accepted() -> None:
    tables = {"Affixes": [
        {"Name": "Stat_Flat_critical_strike_chance", "Stat": "crit_chance",
         "Percent": True},
        {"Name": "Stat_Increased_critical_strike_chance", "Stat": "crit_chance",
         "Percent": True},
        {"Name": "Hybrid_Health_and_armor", "Stat": "", "Percent": False},
    ]}
    assert gen.validate_affix_percent_agrees(tables) == []


def test_an_implicit_naming_a_stat_no_affix_grants_is_refused() -> None:
    """The tool tip reads an implicit's unit off the affix rows.

    A stat that appears only as an implicit would print with no percent sign and
    nothing would say so, which is the same silence issue #1224 reports.
    """
    tables = {
        "Affixes": [{"Name": "Stat_Flat_armor", "Stat": "armor",
                     "Percent": False}],
        "ItemBases": [{"Name": "Head_Hood", "Implicit1Stat": "evasion",
                       "Implicit2Stat": ""}],
    }
    problems = gen.validate_implicit_stats_have_an_affix(tables)
    assert len(problems) == 1, problems
    assert "evasion" in problems[0]


def test_an_implicit_whose_stat_has_an_affix_is_accepted() -> None:
    tables = {
        "Affixes": [{"Name": "Stat_Flat_evasion", "Stat": "evasion",
                     "Percent": True}],
        "ItemBases": [{"Name": "Head_Hood", "Implicit1Stat": "evasion",
                       "Implicit2Stat": ""}],
    }
    assert gen.validate_implicit_stats_have_an_affix(tables) == []


def test_the_real_tables_pass_both_guards(rows) -> None:
    """Not a restatement of the two above: this runs them on the real data."""
    with ITEM_BASES.open(newline="", encoding="utf-8") as handle:
        bases = list(csv.DictReader(handle))

    tables = {
        "Affixes": [dict(row, Percent=row["Percent"] == "True") for row in rows],
        "ItemBases": bases,
    }
    assert gen.validate_affix_percent_agrees(tables) == []
    assert gen.validate_implicit_stats_have_an_affix(tables) == []
