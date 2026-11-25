"""War's enemy modifiers, and Death's two new status rows, agree with the rows that state their figures.

WHY THIS EXISTS. Eight of War's nine enemy modifiers were written on 2026-11-25,
ahead of their build-and-test window, in `game/Source/Cataclysm/Character/
CataclysmEnemyModifiers.h` and beside it; and the same change added Death's
Mortal Reminder and Lingering Decay status rows to the design workbook's Debuffs
sheet. Each figure is written twice -- once in the C++ that applies it, once in
the generated row a player and a designer read -- so the two can drift. Continuous
integration never builds the C++, so nothing else on a pull request would see it.

WHAT IS HELD HERE, ROW BY ROW: the C++ constant against the number the row's own
sentence states, and the sentence against the wording the owner accepted where a
ruling reworded it (Shield Wall, 2026-11-24). And the re-apply script that carries the
workbook edit reports the workbook already carrying it, which is what keeps the
unmergeable binary and the recorded edit together.

READS SOURCE TEXT, AS `test_commander_buff_matches_the_design.py` DOES, for its
reason: the automation tests that run these rules need the editor.
"""

from __future__ import annotations

import csv
import pathlib
import re
import subprocess
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
MODIFIERS_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                    / "CataclysmEnemyModifiers.h")
ENEMY_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmEnemyCharacter.h")
DUNGEON_EFFECTS_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
                          / "CataclysmDungeonModifierEffects.h")
ENEMY_MODIFIERS = REPO_ROOT / "game" / "Data" / "EnemyModifiers.csv"
STATUS_EFFECTS = REPO_ROOT / "game" / "Data" / "StatusEffects.csv"
REAPPLY = REPO_ROOT / "tools" / "reapply_death_war_enemy_modifiers_workbook_edit.py"
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"

#: The eight resistances a player holds, which Battle Cry's row must name.
PLAYER_RESISTANCES = {
    "resistance_war", "resistance_demonic", "resistance_death",
    "resistance_pestilence", "resistance_famine", "resistance_celestial",
    "resistance_chaos", "resistance_void",
}


def text(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def constant(name: str, path: pathlib.Path) -> float:
    """A `static constexpr float Name = <number>f;` line's number."""
    match = re.search(
        rf"static\s+constexpr\s+float\s+{re.escape(name)}\s*=\s*(-?\d+(?:\.\d+)?)f\s*;",
        text(path))
    if match is None:
        pytest.fail(f"{path.name} has no 'static constexpr float {name} = <number>f;' line")
    return float(match.group(1))


def rows(path: pathlib.Path) -> dict[str, dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


@pytest.fixture(scope="module")
def modifiers() -> dict[str, dict[str, str]]:
    return rows(ENEMY_MODIFIERS)


@pytest.fixture(scope="module")
def statuses() -> dict[str, dict[str, str]]:
    return rows(STATUS_EFFECTS)


def described(table: dict[str, dict[str, str]], key: str) -> str:
    if key not in table:
        pytest.fail(f"no row {key!r}; it is generated from docs/All_Things_Cataclysm.xlsx")
    return table[key]["Description"]


def percent_before(phrase: str, sentence: str) -> float:
    match = re.search(rf"(\d+(?:\.\d+)?)%\s*{phrase}", sentence)
    assert match is not None, f"{sentence!r} states no percentage before {phrase!r}"
    return float(match.group(1))


def test_the_tables_are_readable(modifiers, statuses):
    """A positive control: every check below passes vacuously on an empty table."""
    assert sum(1 for row in modifiers.values() if row["CataclysmType"] == "War") == 9
    assert len(statuses) > 40


def test_shield_wall_is_the_owners_wording_and_its_share_is_a_quarter(modifiers):
    """Q10, accepted 2026-11-24: no positional weak points, so projectiles."""
    sentence = described(modifiers, "War_Shield_Wall")
    assert "front" not in sentence.lower(), (
        f"War_Shield_Wall reads {sentence!r}; the owner accepted on 2026-11-24 that it reads "
        f"'Take 75% less damage from projectiles' (no positional weak points)")
    less = percent_before(r"less damage from projectiles", sentence)
    share = constant("ShieldWallProjectileDamageShare", MODIFIERS_HEADER)
    assert share == pytest.approx(1.0 - less / 100.0), (
        f"the row says {less}% less and the C++ keeps {share} of a projectile")


def test_armored_juggernauts_multiplier_is_the_rows_percentage(modifiers):
    gained = percent_before("armor", described(modifiers, "War_Armored_Juggernaut"))
    multiplier = constant("ArmoredJuggernautArmourMultiplier", MODIFIERS_HEADER)
    assert multiplier == pytest.approx(1.0 + gained / 100.0)


def test_drums_of_war_is_fifty_percent_at_half_commanders_reach(modifiers, statuses):
    """Q12, ruled 2026-11-24: both stay, Drums of War at half Commander's radius."""
    row = described(modifiers, "War_Drums_of_War")
    buff = described(statuses, "Buff_Drums_of_War")
    increase = constant("DrumsOfWarIncreasePercent", ENEMY_HEADER)
    assert re.search(r"by\s+(\d+)%", row) and float(re.search(r"by\s+(\d+)%", row).group(1)) == increase
    assert f"{increase:g}% increased movement speed and attack speed" in buff

    assert re.search(r"DrumsOfWarReachCm\s*=\s*CommanderReachCm\s*\*\s*0\.5f\s*;",
                     text(MODIFIERS_HEADER)), (
        "DrumsOfWarReachCm is no longer written as half of CommanderReachCm")
    reach_m = constant("CommanderReachCm", MODIFIERS_HEADER) * 0.5 / 100.0
    assert f"within {reach_m:g} meters" in buff, (
        f"Buff_Drums_of_War reads {buff!r} and the reach is {reach_m:g} metres")


def test_commanders_reach_is_the_commander_buffs_own(modifiers):
    """One buff, one reach: the dungeon rule's and the Succubus's eight metres."""
    assert constant("CommanderReachCm", MODIFIERS_HEADER) == constant(
        "CommandersAuraRadiusCm", DUNGEON_EFFECTS_HEADER)
    row = described(modifiers, "War_Commander")
    assert float(re.search(r"(\d+)%", row).group(1)) == constant(
        "CommanderIncreasePercent", ENEMY_HEADER)


def test_phalanx_is_the_rows_ten_per_ally_within_five_metres(modifiers):
    row = described(modifiers, "War_Phalanx")
    match = re.search(r"(\d+)% damage reduction for every other enemy within (\d+) meters", row)
    assert match is not None, f"War_Phalanx reads {row!r}"
    assert constant("PhalanxDamageReductionPerAlly", MODIFIERS_HEADER) == float(match.group(1))
    assert constant("PhalanxReachCm", MODIFIERS_HEADER) == float(match.group(2)) * 100.0


def test_battle_cry_states_one_figure_in_the_code_and_the_row(modifiers, statuses):
    """The shout passes its own cut and seconds; the row says which stats move."""
    seconds = constant("BattleCrySeconds", MODIFIERS_HEADER)
    cut = constant("BattleCryResistanceCut", MODIFIERS_HEADER)
    assert f"for {seconds:g} seconds" in described(modifiers, "War_Battle_Cry")

    status = statuses["Debuff_Battle_Cry"]
    assert float(status["DurationSeconds"]) == seconds
    assert float(status["Strength"]) == cut
    assert f"by {cut:g} for {seconds:g} seconds" in status["Description"]
    assert "(planned)" not in status["Description"]
    moved = {name.strip() for name in status["MovesStat"].split(",") if name.strip()}
    assert moved == PLAYER_RESISTANCES, (
        f"Debuff_Battle_Cry moves {sorted(moved)}, not the player's eight resistances")


def test_deaths_two_new_status_rows_state_the_codes_figures(statuses):
    """Accepted by the owner on 2026-11-24 ("These are fine"), added 2026-11-25."""
    reminder = described(statuses, "Debuff_Mortal_Reminder")
    seconds = constant("MortalReminderSeconds", MODIFIERS_HEADER)
    assert f"For {seconds:g} seconds" in reminder

    decay = described(statuses, "Debuff_Lingering_Decay")
    cut = constant("LingeringDecayHealingLessPercent", MODIFIERS_HEADER)
    assert f"reduced by {cut:g}%" in decay


def test_the_reapply_script_finds_the_workbook_already_carrying_its_edit():
    """Its positive control: run against the workbook, it has nothing left to write."""
    if not WORKBOOK.is_file():
        pytest.skip("the design workbook is not present")
    pytest.importorskip("openpyxl")
    done = subprocess.run([sys.executable, str(REAPPLY), "--check"], cwd=REPO_ROOT,
                          capture_output=True, text=True, encoding="utf-8")
    assert done.returncode == 0, done.stdout + done.stderr
    assert "0 of 8 cells to write" in done.stdout, done.stdout
