"""`tools/reapply_death_skills_workbook.py` writes the Death weapon skills into the design workbook, and only those cells.

WHY THIS EXISTS. Issue #2885: the owner approved the 40 drafted Death skills and five Death one-handed Crossbow skills
on 2026-11-24. The workbook is a binary git cannot merge, so the branch building them records its edit as a script that
redoes it on whatever workbook development holds (the coordinating session's instruction of 2026-11-24). These checks
hold the script to the generated table the game reads, run it on a copy of the workbook put back to the state before
the edit, and show it refusing a cell somebody else changed, so the real file is never touched.
"""

from __future__ import annotations

import csv
import pathlib
import shutil
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402
import reapply_death_skills_workbook as script  # noqa: E402

SKILLS_CSV = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
MINIONS_CSV = REPO_ROOT / "game" / "Data" / "MinionTypes.csv"


def csv_rows(path: pathlib.Path) -> dict[str, dict[str, str]]:
    with path.open(encoding="utf-8-sig", newline="") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


def number(cell: object) -> float:
    """A row's figure as the generator writes it: -1 for a figure the row does not state."""
    return gen.UNSTATED_SKILL_NUMBER if cell is None else float(cell)


def undo_the_edit(path: pathlib.Path) -> None:
    """Put a copy of the workbook back to the state before issue #2885: the 40 Death rows empty but for a Sword row's
    Tags, no Death Crossbow rows, and no Shade or Husk."""
    book = openpyxl.load_workbook(path)
    skills = book[script.SKILLS_SHEET]
    at = script.header_of(skills, script.SKILL_COLUMNS)
    appended = []
    for death in script.ROWS:
        for row in skills.iter_rows(min_row=2):
            if (row[at[script.WEAPON] - 1].value, row[at[script.DAMAGE] - 1].value,
                    row[at[script.SLOT] - 1].value) != (death.weapon, script.DAMAGE_TYPE, death.slot):
                continue
            if death.weapon in script.NEW_WEAPONS:
                appended.append(row[0].row)
            else:
                for column, before in death.cells_before().items():
                    skills.cell(row[0].row, at[column]).value = before
    for index in sorted(appended, reverse=True):
        skills.delete_rows(index)
    minions = book[script.MINIONS_SHEET]
    for index in sorted((row[0].row for row in minions.iter_rows(min_row=2)
                         if row[0].value in script.MINION_ROWS), reverse=True):
        minions.delete_rows(index)
    book.save(path)


@pytest.fixture()
def before_the_edit(tmp_path: pathlib.Path) -> pathlib.Path:
    copy = tmp_path / "All_Things_Cataclysm.xlsx"
    shutil.copyfile(script.WORKBOOK, copy)
    undo_the_edit(copy)
    return copy


# --------------------------------------------------------------------------
# The script's own data
# --------------------------------------------------------------------------

def test_the_rows_are_the_45_the_owner_approved():
    """40 for the nine Death weapons and five for the Crossbow, each weapon's five slots once."""
    script.check_rows()
    by_weapon: dict[str, set[str]] = {}
    for row in script.ROWS:
        by_weapon.setdefault(row.weapon, set()).add(row.slot)
    assert set(by_weapon) == {"Sword", "Greatsword", "Greataxe", "Staff", "Wand", "Fist", "Whip", "Spear",
                              "Crossbow"}
    assert all(slots == {"Support", "Heavy", "Special", "Movement", "Ultimate"} for slots in by_weapon.values())


def test_a_row_is_written_whole_exactly_when_what_it_needs_is_built():
    """`works` is the rule the whole script turns on, so it is checked against `BUILT` directly. Twelve rows need only
    N22, C3 or nothing; the other 33 wait, and say on what."""
    assert script.BUILT <= script.KNOWN_PIECES
    for row in script.ROWS:
        assert row.works == (set(row.needs) <= script.BUILT), row.csv_name
        assert row.waits_on == tuple(piece for piece in row.needs if piece not in script.BUILT), row.csv_name
    assert sum(row.works for row in script.ROWS) == 12
    assert {row.name for row in script.ROWS if row.works} == {
        "Grave Muster", "Wailing Host", "Shallow Graves", "Grasping Dead", "Nightshade", "Soul Lash",
        "Gallows Chain", "Soul Herd", "Reaping Arc", "Pale Stride", "Pale Vault", "Parting Shot"}


def test_every_row_the_script_writes_whole_is_accepted_by_the_generator():
    """THE SCRIPT'S SHAPES ARE THE GENERATOR'S TO JUDGE. A row written whole with a key the generator refuses would
    stop generation; each is parsed here with the generator's own reader, one row at a time."""
    for row in script.ROWS:
        if row.works:
            gen.parse_shape_params(row.params, row.shape, row.csv_name)


# --------------------------------------------------------------------------
# The committed data holds what the script writes
# --------------------------------------------------------------------------

def test_the_committed_workbook_holds_the_edit():
    assert script.check(), "docs/All_Things_Cataclysm.xlsx does not hold every cell the script writes; run it"


def test_the_generated_table_holds_every_row_as_the_script_writes_it():
    """The CSV the game reads, row by row: name, description, tags with the slot's added by the generator, the shape
    only for a row written whole, and every figure."""
    rows = csv_rows(SKILLS_CSV)
    for death in script.ROWS:
        row = rows.get(death.csv_name)
        assert row is not None, f"{death.csv_name} is not in {SKILLS_CSV.name}"
        assert row["SkillName"] == death.name
        assert row["SkillDescription"] == death.description
        assert row["Tags"] == f"{death.tags}, Slot.{death.slot}"
        assert row["Shape"] == (death.shape if death.works else ""), death.csv_name
        assert row["ShapeParams"] == (death.params if death.works else ""), death.csv_name
        for column, figure in (("CritChancePercent", death.crit), ("DamagePercent", death.damage),
                               ("Cooldown", death.cooldown), ("ManaCost", death.mana),
                               ("FervourCost", death.fervour)):
            assert float(row[column]) == number(figure), (death.csv_name, column)


def test_the_generated_minion_table_holds_the_kinds_the_script_writes():
    rows = csv_rows(MINIONS_CSV)
    header = {"Family": 1, "BaseHealth": 2, "HealthPerLevel": 3, "BaseDamage": 4, "DamagePerLevel": 5,
              "AttackIntervalSeconds": 6, "MoveSpeed": 7, "ThreatPercent": 8, "ReachCm": 9, "NoticeRadiusCm": 10,
              "TargetMode": 11, "Tags": 12, "ExplosionPercentOfOwnDamage": 13}
    for name, values in script.MINION_ROWS.items():
        row = rows.get(name)
        assert row is not None, f"{name} is not in {MINIONS_CSV.name}"
        for column, index in header.items():
            wanted = values[index]
            if isinstance(wanted, str):
                assert row[column] == wanted, (name, column)
            else:
                assert float(row[column]) == float(wanted), (name, column)


def test_the_shade_draws_no_attention_as_wailing_host_says():
    """A JUDGEMENT OF 2026-11-24, recorded in docs/DECISIONS.md: the draft gave the Shade a threat of 20, and since the
    owner's decision of 2026-11-01 (issue #2553) only nought makes the nearest-enemy search pass a minion over. Wailing
    Host's sentence says the enemies "pay them little attention"."""
    assert float(csv_rows(MINIONS_CSV)["Shade"]["ThreatPercent"]) == 0.0
    assert "pay them little attention" in csv_rows(SKILLS_CSV)["Death_Greatsword_Special"]["SkillDescription"]


# --------------------------------------------------------------------------
# The script, run on a copy
# --------------------------------------------------------------------------

def test_it_writes_the_edit_on_a_copy_put_back_to_before_it(before_the_edit: pathlib.Path):
    assert not script.check(before_the_edit), "set-up: the copy is back to the state before issue #2885"
    written = script.apply(before_the_edit)
    appended = sum(1 for row in script.ROWS if row.weapon in script.NEW_WEAPONS)
    assert appended == 5
    assert len({(w.row) for w in written if w.sheet == script.MINIONS_SHEET}) == len(script.MINION_ROWS)
    assert script.check(before_the_edit), "after one run the copy holds every cell"
    assert script.apply(before_the_edit) == [], "a second run must change nothing"

    # AND THE COPY THEN READS AS THE COMMITTED WORKBOOK DOES, through the generator's own reader.
    copied = {row["Name"]: row for row in gen.weapon_skills(openpyxl.load_workbook(before_the_edit, data_only=True))}
    committed = csv_rows(SKILLS_CSV)
    for death in script.ROWS:
        for column in ("SkillName", "SkillDescription", "Tags", "Shape", "ShapeParams"):
            assert copied[death.csv_name][column] == committed[death.csv_name][column], (death.csv_name, column)


def test_it_refuses_a_cell_somebody_else_changed(before_the_edit: pathlib.Path):
    """THE REFUSAL IS THE POINT. A Death cell holding neither its value before the edit nor the value the edit writes
    means another branch wrote it, and the script stops rather than overwrite it."""
    book = openpyxl.load_workbook(before_the_edit)
    skills = book[script.SKILLS_SHEET]
    at = script.header_of(skills, script.SKILL_COLUMNS)
    target = next(row[0].row for row in skills.iter_rows(min_row=2)
                  if (row[at[script.WEAPON] - 1].value, row[at[script.DAMAGE] - 1].value,
                      row[at[script.SLOT] - 1].value) == ("Sword", "Death", "Heavy"))
    skills.cell(target, at[script.NAME]).value = "Somebody Else's Stroke"
    book.save(before_the_edit)

    with pytest.raises(script.ApplyError, match="Somebody Else's Stroke"):
        script.apply(before_the_edit)
    after = openpyxl.load_workbook(before_the_edit)[script.SKILLS_SHEET]
    assert after.cell(target, at[script.NAME]).value == "Somebody Else's Stroke", "a refused run wrote nothing"


def test_it_refuses_a_minion_row_somebody_else_wrote_differently(before_the_edit: pathlib.Path):
    book = openpyxl.load_workbook(before_the_edit)
    minions = book[script.MINIONS_SHEET]
    minions.append(["Husk", "Creature", 999, 72, 10.5, 10.5, 1.5, 4.4, 150, 200, 1500, "Nearest",
                    "Type.Minion, Type.Summon, Minion.Creature, Minion.Melee", 300])
    book.save(before_the_edit)
    with pytest.raises(script.ApplyError, match="Husk"):
        script.apply(before_the_edit)
