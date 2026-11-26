"""`tools/reapply_death_finish_soul_collector_workbook.py`, the Death finish slice A's rows and tag, and the data they
rest on: Soul Fuel's skip of Cold Blood and Requiem (issue #2937), Spirit Volley's row (issue #2908) and the Effigy's
`SiphonsAround` key.

THE WORKBOOK IN `docs/` HOLDS THE ROWS, the first test says. The rest work on copies: a second run writes nothing, a
copy with this slice's rows taken out is put back exactly, and a row or a tag somebody changed is refused with nothing
written.
"""

from __future__ import annotations

import csv
import pathlib
import re
import shutil
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402
import reapply_death_finish_soul_collector_workbook as script  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
DATA = REPO_ROOT / "game" / "Data"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"


@pytest.fixture
def copy_of_the_workbook(tmp_path) -> pathlib.Path:
    target = tmp_path / "workbook.xlsx"
    shutil.copyfile(WORKBOOK, target)
    return target


def _rows(name: str) -> list[dict]:
    with (DATA / f"{name}.csv").open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def _ours(row_node: str, option: int) -> bool:
    return any(node.key == row_node and node.option == option for node in script.NODES)


def _take_ours_out(path: pathlib.Path) -> None:
    """Delete this slice's Passive Effects rows and its tag row from the workbook at `path`."""
    book = openpyxl.load_workbook(path)
    sheet = book[script.base.EFFECTS_SHEET]
    header = [c.value for c in sheet[1]]
    node_at, option_at = header.index("Node") + 1, header.index("Option") + 1
    doomed = [r for r in range(2, sheet.max_row + 1)
              if _ours(sheet.cell(r, node_at).value, int(sheet.cell(r, option_at).value or 0))]
    assert len(doomed) == sum(len(node.rows) for node in script.NODES), doomed
    for row in reversed(doomed):
        sheet.delete_rows(row, 1)
    tags = book[script.TAGS_SHEET]
    declared = [r for r in range(2, tags.max_row + 1) if tags.cell(r, 1).value == script.SPENT_FERVOUR_TAG[0]]
    assert len(declared) == 1
    tags.delete_rows(declared[0], 1)
    book.save(path)


def _sheet(path: pathlib.Path, title: str) -> list[tuple]:
    rows = [tuple(r) for r in openpyxl.load_workbook(path, read_only=True)[title].iter_rows(values_only=True)]
    while rows and all(v is None for v in rows[-1]):
        rows.pop()
    return rows


# ---------------------------------------------------------------------------
# The script
# ---------------------------------------------------------------------------

def test_the_committed_workbook_holds_this_slices_rows_and_tag():
    book = openpyxl.load_workbook(WORKBOOK)
    plan = script.base.plan_effects(book[script.base.EFFECTS_SHEET], script.NODES)
    assert plan.changed == 0, "\n".join(plan.report)
    assert script.plan_tag(book) == []


def test_a_second_run_writes_nothing(copy_of_the_workbook):
    before = copy_of_the_workbook.read_bytes()
    assert script.apply(copy_of_the_workbook) == [
        "every row and tag of the Death finish slice A is already written; nothing written"]
    assert copy_of_the_workbook.read_bytes() == before


def test_the_rows_and_the_tag_taken_out_are_put_back_as_committed(copy_of_the_workbook):
    """THE CONTROL is the first assertion: with them taken out, the two sheets differ from the committed ones."""
    _take_ours_out(copy_of_the_workbook)
    for title in (script.base.EFFECTS_SHEET, script.TAGS_SHEET):
        assert _sheet(copy_of_the_workbook, title) != _sheet(WORKBOOK, title)
    report = script.apply(copy_of_the_workbook)
    assert report[-1].startswith(f"wrote {len(script.NODES) + 2} change(s)"), report
    # THE SAME ROWS, IN ANY ORDER, since the Death finish bundle's assembly (2026-11-26): the script appends its rows,
    # and the committed sheets hold slice B's rows after this slice's, so put back they land after those. The heading
    # row is compared in place.
    for title in (script.base.EFFECTS_SHEET, script.TAGS_SHEET):
        put_back, committed = _sheet(copy_of_the_workbook, title), _sheet(WORKBOOK, title)
        assert put_back[0] == committed[0], title
        assert sorted(put_back[1:], key=repr) == sorted(committed[1:], key=repr), title


def test_check_writes_nothing_and_exits_one(copy_of_the_workbook):
    _take_ours_out(copy_of_the_workbook)
    before = copy_of_the_workbook.read_bytes()
    assert script.main(["--workbook", str(copy_of_the_workbook), "--check"]) == 1
    assert copy_of_the_workbook.read_bytes() == before


def test_a_row_somebody_changed_is_refused_and_nothing_is_written(copy_of_the_workbook):
    book = openpyxl.load_workbook(copy_of_the_workbook)
    sheet = book[script.base.EFFECTS_SHEET]
    header = [c.value for c in sheet[1]]
    changed = next(r for r in range(2, sheet.max_row + 1)
                   if sheet.cell(r, header.index("Node") + 1).value == "SoulCollector_basic_d_a0")
    sheet.cell(changed, header.index("Value Per Point") + 1).value = 2
    book.save(copy_of_the_workbook)
    before = copy_of_the_workbook.read_bytes()
    with pytest.raises(script.ReapplyError, match="SoulCollector_basic_d_a0"):
        script.apply(copy_of_the_workbook)
    assert copy_of_the_workbook.read_bytes() == before


def test_a_tag_somebody_reworded_is_refused(copy_of_the_workbook):
    book = openpyxl.load_workbook(copy_of_the_workbook)
    tags = book[script.TAGS_SHEET]
    row = next(r for r in range(2, tags.max_row + 1) if tags.cell(r, 1).value == script.SPENT_FERVOUR_TAG[0])
    tags.cell(row, 2).value = "something else"
    book.save(copy_of_the_workbook)
    with pytest.raises(script.ReapplyError, match="Use.SpentFervour"):
        script.apply(copy_of_the_workbook)


def test_it_writes_soul_collector_rows_only():
    assert all(node.key.startswith("SoulCollector_") for node in script.NODES)


# ---------------------------------------------------------------------------
# What the rows rest on
# ---------------------------------------------------------------------------

def test_the_tag_the_rows_name_is_declared_for_unreal():
    ini = (REPO_ROOT / "game" / "Config" / "Tags" / "CataclysmTags.ini").read_text(encoding="utf-8")
    assert f'Tag="{script.SPENT_FERVOUR_TAG[0]}"' in ini
    named = {row["RequiredTags"] for row in _rows("PassiveEffects") if row["Node"].startswith("SoulCollector_basic_d_a")}
    assert script.SPENT_FERVOUR_TAG[0] in named


def test_the_tag_the_code_puts_on_a_use_is_the_one_the_rows_name():
    text = (SOURCE / "CataclysmSkillTemplate.cpp").read_text(encoding="utf-8")
    assert f'RequestGameplayTag(FName(TEXT("{script.SPENT_FERVOUR_TAG[0]}"))' in text


def _code_of(text: str, signature: str) -> str:
    """The body of the function whose definition starts with `signature`, with every comment line taken out."""
    start = text.index(signature)
    body = text[start:text.index("\n}", start)]
    return "\n".join(line for line in body.splitlines() if not line.strip().startswith("//"))


def test_soul_fuel_skips_the_two_keys_cold_blood_and_requiem_carry():
    """ISSUE #2937, RULING 2: Soul Fuel skips a skill whose own row holds or spends Fervour. Read from the generated
    rows: Cold Blood states `HoldsFervour=1` and Requiem `SpendsAllFervour=1`, and they are the only rows stating
    either; and the rule's code tests those two keys (comments taken out, so a comment naming them does not pass)."""
    rows = {row["SkillName"]: gen.parse_shape_params(row["ShapeParams"], row["Shape"], row["Name"])
            for row in _rows("WeaponSkills") if row["Shape"]}
    holding = {name for name, params in rows.items() if float(params.get("HoldsFervour", 0)) != 0}
    spending = {name for name, params in rows.items() if float(params.get("SpendsAllFervour", 0)) != 0}
    assert holding == {"Cold Blood"} and spending == {"Requiem"}
    code = _code_of((SOURCE / "CataclysmSkillTemplate.cpp").read_text(encoding="utf-8"),
                    "bool UCataclysmSkillTemplate::SoulFuelReaches() const")
    assert re.search(r"Params\.bHoldsFervour\s*\|\|\s*Params\.bSpendsAllFervour", code), code
    assert "ECataclysmAbilitySlot::Movement" in code and "ECataclysmAbilitySlot::Ultimate" in code


def test_spirit_volley_is_option_two_of_the_vow_at_fifty_at_thirty_percent():
    rows = [row for row in _rows("PassiveEffects") if row["Stat"] == "spirit_volley_weapon_percent"]
    assert [(row["Node"], row["Option"], float(row["ValuePerPoint"])) for row in rows] == [
        ("SoulCollector_capstone_50", "2", 30.0)]


def test_the_effigy_is_written_whole_with_its_siphon_and_its_reserve():
    effigy = next(row for row in _rows("WeaponSkills") if row["Name"] == "Death_Crossbow_Special")
    params = gen.parse_shape_params(effigy["ShapeParams"], effigy["Shape"], effigy["Name"])
    assert effigy["Shape"] == "Deployable"
    assert params["SiphonsAround"] == "1" and params["Taunts"] == "1" and params["FervourReserve"] == "5"
    assert params["Minions"] == "Effigy:1" and "MaxActive" not in params
    kinds = {row["Name"]: row for row in _rows("MinionTypes")}
    assert "Type.Gadget" in kinds["Effigy"]["Tags"] and float(kinds["Effigy"]["BaseDamage"]) == 0.0


def test_siphons_around_is_refused_on_a_row_that_places_nothing():
    """THE CONTROL is the same cell with a machine named, which passes."""
    with pytest.raises(gen.DataError, match="places nothing"):
        gen.parse_shape_params("Count=1; Duration=5; SiphonsAround=1", "Deployable", "a test row")
    gen.parse_shape_params("Count=1; Duration=5; Minions=Effigy:1; SiphonsAround=1", "Deployable", "a test row")


def test_siphons_around_is_refused_on_any_shape_but_a_deployable():
    with pytest.raises(gen.DataError, match="SiphonsAround"):
        gen.parse_shape_params("Count=1; Duration=5; Minions=Husk:1; SiphonsAround=1", "Summon", "a test row")
