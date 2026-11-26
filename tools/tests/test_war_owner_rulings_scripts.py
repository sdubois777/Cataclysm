"""Three owner rulings of 2026-11-25 on the War line, and the script that writes them.

`tools/apply_war_owner_rulings_2026_11_25.py`:
  1. Berserking at 1% a point ("yeah lower it to 1%");
  2. the 18 enchantments that say "gadget", 21 rows, requiring `Type.Gadget` instead of `Type.Deployable` ("agree with
     your rec"), the enchantment that says "deployables" left as it is;
  3. The Unbreakable Wall, "Every block negates the whole hit.", and its flag ("accepted").

These tests hold that the workbook, the generated tables and the tree files carry the rulings, with the "deployables"
row as the control, that the script writes each change once, and that it refuses text somebody wrote since. The game's
half is in `CataclysmStatExemptionTests.cpp`, `CataclysmSkillTrapTests.cpp` and `CataclysmEnchantmentEffectTests.cpp`.
"""
from __future__ import annotations

import csv
import json
import pathlib
import shutil
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import apply_war_owner_rulings_2026_11_25 as rulings  # noqa: E402
import generate_datatables as gen  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
DOCS = REPO_ROOT / "docs"
DATA = REPO_ROOT / "game" / "Data"


def _csv(name: str) -> list[dict]:
    with open(DATA / name, encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def test_everything_is_written():
    assert rulings.apply(WORKBOOK, DOCS, check_only=True)[-1] == "everything is already written; nothing written"


def test_berserking_is_one_percent_a_point():
    (row,) = [r for r in _csv("PassiveEffects.csv")
              if r["Node"] == "Berserker_trunk_009" and r["Stat"] == "berserking_damage_per_fervour"]
    assert float(row["ValuePerPoint"]) == 1.0
    data = json.loads((DOCS / "Berserker_Class_Tree_Final.json").read_text(encoding="utf-8"))
    (node,) = [n for n in data["nodes"] if n["id"] == "trunk_009"]
    assert "+1% bonus damage per point invested" in node["data"]["description"]


def test_the_gadget_rows_require_type_gadget_and_the_deployables_row_keeps_type_deployable():
    rows = _csv("EnchantmentEffects.csv")
    gadget = [r for r in rows if r["Enchantment"] in rulings.GADGET_ENCHANTMENTS]
    assert len(gadget) == rulings.GADGET_ROWS == 21
    assert len(set(r["Enchantment"] for r in gadget)) == 18
    assert all(r["RequiredTags"] == "Type.Gadget" for r in gadget), [r["Name"] for r in gadget
                                                                     if r["RequiredTags"] != "Type.Gadget"]
    still = [r for r in rows if "Type.Deployable" in r["RequiredTags"]]
    assert len(still) == 1 and "deployables" in still[0]["Enchantment"].lower(), [r["Name"] for r in still]
    assert gen.GADGET_ROW_TAG == "Type.Gadget"


def test_the_unbreakable_wall():
    data = json.loads((DOCS / "Bulwark_Class_Tree_Final.json").read_text(encoding="utf-8"))
    (node,) = [n for n in data["nodes"] if n["id"] == "capstone_200"]
    (wall,) = [o for o in node["data"]["options"] if o["name"] == "The Unbreakable Wall"]
    assert wall["description"] == "Every block negates the whole hit."
    (row,) = [r for r in _csv("PassiveEffects.csv") if r["Node"] == "Bulwark_capstone_200" and r["Option"] == "1"]
    assert (row["Stat"], row["ValueKind"], float(row["ValuePerPoint"])) == ("every_block_negates", "flat", 1.0)


def _copies(tmp_path) -> tuple[pathlib.Path, pathlib.Path]:
    """A workbook and tree files holding the text before the rulings."""
    book_path = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, book_path)
    folder = tmp_path / "docs"
    folder.mkdir()
    for tree in ("Berserker", "Bulwark"):
        shutil.copy(DOCS / f"{tree}_Class_Tree_Final.json", folder / f"{tree}_Class_Tree_Final.json")
    book = openpyxl.load_workbook(book_path)
    passive = book[rulings.base.SHEET]
    header = [c.value for c in passive[1]]
    for index in range(passive.max_row, 1, -1):
        node = passive.cell(index, header.index("Node") + 1).value
        stat = passive.cell(index, header.index("Stat") + 1).value
        if node == "Berserker_trunk_009" and stat == "berserking_damage_per_fervour":
            passive.cell(index, header.index("Value Per Point") + 1).value = 10
        if node == "Bulwark_capstone_200" and stat == "every_block_negates":
            passive.delete_rows(index, 1)
    enchantments = book["Enchantment Effects"]
    header = [c.value for c in enchantments[1]]
    for index in range(2, enchantments.max_row + 1):
        if enchantments.cell(index, header.index("Enchantment") + 1).value in rulings.GADGET_ENCHANTMENTS:
            enchantments.cell(index, header.index("Required Tags") + 1).value = "Type.Deployable"
    book.save(book_path)
    for each in rulings.SENTENCES:
        path = folder / f"{each.tree}_Class_Tree_Final.json"
        text = path.read_bytes().decode("utf-8")
        path.write_bytes(text.replace(json.dumps(each.after)[1:-1], json.dumps(each.before)[1:-1]).encode("utf-8"))
    return book_path, folder


def test_the_script_writes_everything_once(tmp_path):
    book, folder = _copies(tmp_path)
    assert rulings.main(["--check", "--workbook", str(book), "--docs", str(folder)]) == 1
    first = rulings.apply(book, folder)
    assert first[-1] == "written", first
    assert any("21 of 21 to Type.Gadget" in line for line in first), first
    assert rulings.apply(book, folder, check_only=True)[-1] == "everything is already written; nothing written"
    for tree in ("Berserker", "Bulwark"):
        name = f"{tree}_Class_Tree_Final.json"
        assert (folder / name).read_bytes() == (DOCS / name).read_bytes(), name


def test_the_script_refuses_a_tag_somebody_wrote_since(tmp_path):
    book, folder = _copies(tmp_path)
    loaded = openpyxl.load_workbook(book)
    sheet = loaded["Enchantment Effects"]
    header = [c.value for c in sheet[1]]
    for index in range(2, sheet.max_row + 1):
        if sheet.cell(index, header.index("Enchantment") + 1).value == rulings.GADGET_ENCHANTMENTS[0]:
            sheet.cell(index, header.index("Required Tags") + 1).value = "Type.Minion"
            break
    loaded.save(book)
    before = book.read_bytes()
    with pytest.raises(rulings.RulingError, match="neither Type.Deployable nor Type.Gadget"):
        rulings.apply(book, folder)
    assert book.read_bytes() == before


def test_the_script_refuses_a_sentence_somebody_wrote_since(tmp_path):
    book, folder = _copies(tmp_path)
    path = folder / "Bulwark_Class_Tree_Final.json"
    each = rulings.SENTENCES[1]
    text = path.read_bytes().decode("utf-8")
    path.write_bytes(text.replace(json.dumps(each.before)[1:-1], "Somebody wrote this since.").encode("utf-8"))
    before = book.read_bytes()
    with pytest.raises(rulings.RulingError, match="somebody has written it since"):
        rulings.apply(book, folder)
    assert book.read_bytes() == before, "the workbook was written before the sentence was refused"
