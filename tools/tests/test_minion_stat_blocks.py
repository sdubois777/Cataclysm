"""The minion type table: what is in it, and whether it plays the way it was set.

`tools/tests/test_minion_damage.py` checks that `docs/Cataclysm_GDD_v2.md` states
the minion rules and that the data agrees with the prose. This file checks the
stat blocks themselves -- the numbers in `game/Data/MinionTypes.csv`, the table
generated from the "Minion Types" sheet of `docs/All_Things_Cataclysm.xlsx`.

Issue #336.
"""

from __future__ import annotations

import csv
import pathlib
import sys

import openpyxl
import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "sim"))

import generate_datatables as gen  # noqa: E402
from cataclysm_sim import player_damage as pd  # noqa: E402
from cataclysm_sim import player_power as pp  # noqa: E402

#: Summon Imp and Open the Rift are both Staff skills, so the summoner holds a
#: Staff. Read from the skill table below rather than assumed.
SUMMONER_WEAPON = "Staff"

#: From the Shape Params of the skills that produce them.
IMP_MAX_ACTIVE = 3
MOTE_MAX_ACTIVE = 4


def minion_rows() -> dict[str, dict]:
    path = ROOT / "game" / "Data" / "MinionTypes.csv"
    with path.open(encoding="utf-8") as handle:
        return {r["Name"]: r for r in csv.DictReader(handle)}


def skill_rows() -> list[dict]:
    path = ROOT / "game" / "Data" / "WeaponSkills.csv"
    with path.open(encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def damage_at(row: dict, level: int) -> float:
    return float(row["BaseDamage"]) + float(row["DamagePerLevel"]) * level


def health_at(row: dict, level: int) -> float:
    return float(row["BaseHealth"]) + float(row["HealthPerLevel"]) * level


def squad_per_second(row: dict, level: int, count: int) -> float:
    return count * damage_at(row, level) / float(row["AttackIntervalSeconds"])


# --------------------------------------------------------------------------
# What is in the table
# --------------------------------------------------------------------------

def test_the_table_holds_the_two_demonic_minion_types():
    """Only the vertical slice is designed, and the vertical slice is Demonic.

    The three War deployables -- bolt turret, ballista and spike trap -- sit on
    Crossbow and Spear and are deliberately absent.
    """
    assert set(minion_rows()) == {"Imp", "Mote"}


def test_every_demonic_minion_skill_produces_a_type_the_table_defines():
    """The whole reason this table exists: a skill names a creature and the
    creature's numbers live in one place, so two skills making the same creature
    cannot disagree about it."""
    produced = {"Summon Imp": "Imp", "Open the Rift": "Imp",
                "Cinder Swarm": "Mote"}
    demonic_minion_skills = {
        r["SkillName"] for r in skill_rows()
        if "Type.Minion" in r["Tags"] and r["DamageType"] == "Demonic"}

    assert demonic_minion_skills == set(produced), (
        "the Demonic minion skills in game/Data/WeaponSkills.csv changed, so "
        "the mapping this test uses is stale")
    for skill, minion_type in produced.items():
        assert minion_type in minion_rows(), (
            f"{skill} produces a {minion_type} and no such row exists")


def test_two_skills_making_the_same_creature_share_one_stat_block():
    """Summon Imp and Open the Rift both make a lesser imp. There is one Imp row,
    so the numbers cannot exist twice and drift apart."""
    staff_minion_skills = [
        r["SkillName"] for r in skill_rows()
        if "Type.Minion" in r["Tags"] and r["WeaponType"] == "Staff"]
    assert sorted(staff_minion_skills) == ["Open the Rift", "Summon Imp"]
    assert len([n for n in minion_rows() if n == "Imp"]) == 1


def test_a_creature_scales_from_spirit():
    """Settled in issue #335: Spirit for creatures, Agility for machines."""
    for name, row in minion_rows().items():
        assert row["Family"] == "Creature", name
        assert row["ScalingAttribute"] == "spirit", name


def test_both_types_attack_the_nearest_enemy():
    """Both skill descriptions say nearest. The column exists because the
    Ballista, which is not in this table yet, deliberately picks the furthest."""
    for name, row in minion_rows().items():
        assert row["TargetMode"] == "Nearest", name


# --------------------------------------------------------------------------
# How it plays
# --------------------------------------------------------------------------

def test_three_imps_out_damage_the_summoners_own_basic_attack_at_every_tier():
    """DECIDED BY THE PROJECT OWNER on 2026-08-15, and it reverses the floor
    issue #336 originally proposed, which was that an uninvested summoner should
    get LESS from three imps than from their basic attack.

    Three imps are permanent in normal play: Summon Imp holds 3 for 20 seconds
    against a 5 second cooldown.
    """
    imp = minion_rows()["Imp"]
    for tier in range(1, 9):
        level = pp.reference_character(tier).level
        squad = squad_per_second(imp, level, IMP_MAX_ACTIVE)
        player = pd.damage_per_second(tier, SUMMONER_WEAPON)
        assert squad > player, (
            f"at difficulty tier {tier} three imps deal {squad:,.0f} a second "
            f"and the summoner's own basic attack deals {player:,.0f}")


def test_a_mote_is_the_weaker_body():
    """The project owner's words on 2026-08-15: a mote is a weaker body in
    larger numbers. So one mote hits for less than one imp, and there are more
    of them."""
    imp, mote = minion_rows()["Imp"], minion_rows()["Mote"]
    for level in (1, 25, 50, 100):
        assert damage_at(mote, level) < damage_at(imp, level), level
        assert health_at(mote, level) < health_at(imp, level), level
    assert MOTE_MAX_ACTIVE > IMP_MAX_ACTIVE


def test_a_full_swarm_of_motes_deals_less_than_a_full_squad_of_imps():
    """The project owner left the size of the difference open and it is set at
    about three quarters. The swarm is paid back by the burst each mote leaves
    when it expires, which an imp only gives when a fourth is summoned over the
    cap."""
    imp, mote = minion_rows()["Imp"], minion_rows()["Mote"]
    for tier in range(1, 9):
        level = pp.reference_character(tier).level
        motes = squad_per_second(mote, level, MOTE_MAX_ACTIVE)
        imps = squad_per_second(imp, level, IMP_MAX_ACTIVE)
        assert 0.70 <= motes / imps <= 0.80, (
            f"at difficulty tier {tier} four motes deal {motes / imps:.2f}x "
            f"what three imps deal")


def test_a_minion_falls_behind_a_geared_summoner_rather_than_keeping_pace():
    """RECORDS A CONSEQUENCE, not a defect.

    A minion's damage rises with the summoner's LEVEL only. The summoner's own
    damage rises with level AND with affix tier AND with gear upgrade level, so
    it climbs faster. The imp squad is therefore worth most against an
    under-geared character and least at the level cap.

    That gap is what minion affixes (issue #337) and points in Spirit exist to
    close, and it is why an uninvested summoner is not the balance case.
    """
    imp = minion_rows()["Imp"]
    ratios = []
    for tier in (1, 8):
        level = pp.reference_character(tier).level
        ratios.append(squad_per_second(imp, level, IMP_MAX_ACTIVE)
                      / pd.damage_per_second(tier, SUMMONER_WEAPON))
    assert ratios[0] > ratios[1], (
        f"three imps are worth {ratios[0]:.2f}x the basic attack at tier 1 and "
        f"{ratios[1]:.2f}x at tier 8; the fall-off is expected")


def test_a_minion_is_fragile_enough_to_be_worth_killing():
    """A minion inherits no armour and no resistance, so it takes hits whole.
    Both should die to a handful of Common hits rather than being ignorable."""
    from cataclysm_sim import enemy_stats as es

    for name, row in minion_rows().items():
        level = pp.reference_character(8).level
        hit = es.stats_on_floor("Common", 8, "Cataclysm").average_damage_per_hit
        survives = health_at(row, level) / hit
        assert survives < 5.0, (
            f"a {name} survives {survives:.1f} Common hits at tier 8")


# --------------------------------------------------------------------------
# The generator's guards
# --------------------------------------------------------------------------

HEADERS = ["Minion Type", "Family", "Base Health", "Health Per Level",
           "Base Damage", "Damage Per Level", "Attack Interval Seconds",
           "Move Speed", "Threat Percent", "Reach Cm", "Notice Radius Cm",
           "Target Mode", "Scaling Attribute"]

GOOD = ["Imp", "Creature", 200, 90, 10, 10.0, 1.0, 4.4, 100, 200, 1500,
        "Nearest", "spirit"]


def book_with(tmp_path, row):
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheet = book.create_sheet("Minion Types")
    sheet.append(HEADERS)
    sheet.append(row)
    path = tmp_path / "w.xlsx"
    book.save(path)
    return openpyxl.load_workbook(path)


def test_the_good_row_is_accepted(tmp_path):
    """A guard that rejects everything proves nothing, so check the control."""
    rows = gen.minion_types(book_with(tmp_path, list(GOOD)))
    assert rows[0]["Name"] == "Imp"
    assert rows[0]["ScalingAttribute"] == "spirit"


def test_a_creature_that_scales_from_the_wrong_attribute_is_refused(tmp_path):
    row = list(GOOD)
    row[HEADERS.index("Scaling Attribute")] = "agility"
    with pytest.raises(gen.DataError, match="issue #335"):
        gen.minion_types(book_with(tmp_path, row))


def test_an_unknown_family_is_refused(tmp_path):
    row = list(GOOD)
    row[HEADERS.index("Family")] = "Elemental"
    with pytest.raises(gen.DataError, match="family"):
        gen.minion_types(book_with(tmp_path, row))


def test_an_unknown_target_mode_is_refused(tmp_path):
    row = list(GOOD)
    row[HEADERS.index("Target Mode")] = "Weakest"
    with pytest.raises(gen.DataError, match="targets"):
        gen.minion_types(book_with(tmp_path, row))


def test_an_attack_interval_of_zero_is_refused(tmp_path):
    row = list(GOOD)
    row[HEADERS.index("Attack Interval Seconds")] = 0
    with pytest.raises(gen.DataError, match="rather than at a rate"):
        gen.minion_types(book_with(tmp_path, row))


def test_no_health_at_all_is_refused(tmp_path):
    row = list(GOOD)
    row[HEADERS.index("Base Health")] = 0
    with pytest.raises(gen.DataError, match="base health"):
        gen.minion_types(book_with(tmp_path, row))
