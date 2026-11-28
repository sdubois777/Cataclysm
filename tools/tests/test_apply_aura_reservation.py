"""`tools/apply_aura_reservation_2026_11_27.py`, run on copies of the real workbook and tree files.

WHAT IS HELD. The script is idempotent and refusing, and finds every row by its key: so on a copy of the workbook as it
stood BEFORE the ruling, `--check` reports work pending; a run writes it; a second run writes nothing; and a cell holding
some third value is refused with nothing saved. The copy is put back into its unruled state here, from the script's own
old values, so the test exercises the transformation whatever state `docs/` is in by the time it runs.

AND A WORKBOOK ANOTHER BRANCH HAS CHANGED. The Soul Collector rework rewrites the same workbook, so rows move; the
script is run on a copy with rows inserted above every row it edits, and must still find each by its key.
"""

from __future__ import annotations

import json
import pathlib
import shutil
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import apply_aura_reservation_2026_11_27 as ruling  # noqa: E402

sys.path.insert(0, str(REPO_ROOT / "tools" / "tests"))
import without_the_war_trees_rows  # noqa: E402

TREES = sorted({f"{each.tree}_Class_Tree_Final.json" for each in ruling.NODE_ROWS})


def _headers(sheet) -> dict[str, int]:
    return {str(c.value).strip(): c.column for c in sheet[1] if c.value is not None}


def _unrule_workbook(path: pathlib.Path) -> None:
    """Put a copy of the workbook back as it stood before the ruling, whether or not the ruling is in it."""
    book = openpyxl.load_workbook(path)

    slots = book["Skill Slots"]
    h = _headers(slots)
    if "Mana Reserved" in h:
        column = h["Mana Reserved"]
        for row in range(1, slots.max_row + 1):
            slots.cell(row, column).value = None
    for row in range(2, slots.max_row + 1):
        if slots.cell(row, h["Slot"]).value == "Aura":
            slots.cell(row, h["Mana Cost"]).value = ruling.AURA_MANA_COST_BEFORE
            slots.cell(row, h["Note"]).value = ruling.OLD_AURA_NOTE

    sheet = book["Enchantments"]
    new_benefits = {b.sentence for b in ruling.NEW_BENEFITS}
    for row in range(2, sheet.max_row + 1):
        if sheet.cell(row, 1).value == ruling.NEW_POSITIVE_TEXT:
            sheet.cell(row, 1).value = ruling.OLD_POSITIVE_TEXT
        if sheet.cell(row, 6).value == ruling.NEW_NEGATIVE_TEXT:
            sheet.cell(row, 6).value = ruling.OLD_NEGATIVE_TEXT
        if sheet.cell(row, 1).value in new_benefits:
            for column in (1, 2, 3, 4, 5, 11):
                sheet.cell(row, column).value = None

    effects = book["Enchantment Effects"]
    h = _headers(effects)
    new_keys = {b.key for b in ruling.NEW_BENEFITS}
    for row in range(2, effects.max_row + 1):
        key = effects.cell(row, h["Enchantment"]).value
        if key in new_keys:
            for column in h.values():
                effects.cell(row, column).value = None
        elif key == ruling.NEW_POSITIVE:
            for name, value in (("Enchantment", ruling.OLD_POSITIVE), ("Effect", ruling.OLD_POSITIVE_TEXT),
                                ("Stat", "mana_cost")):
                effects.cell(row, h[name]).value = value
        elif key == ruling.NEW_NEGATIVE:
            for name, value in (("Enchantment", ruling.OLD_NEGATIVE), ("Effect", ruling.OLD_NEGATIVE_TEXT),
                                ("Stat", "mana_cost"), ("Value Kind", "increased"), ("Value Low", 100),
                                ("Value High", 200)):
                effects.cell(row, h[name]).value = value

    passives = book["Passive Effects"]
    h = _headers(passives)
    added = {(each.key, each.stat) for each in ruling.NODE_ROWS}
    for row in range(2, passives.max_row + 1):
        if (passives.cell(row, h["Node"]).value, passives.cell(row, h["Stat"]).value) in added:
            for column in h.values():
                passives.cell(row, column).value = None
    book.save(path)


def _unrule_trees(folder: pathlib.Path) -> None:
    for name in TREES:
        path = folder / name
        tree = json.loads(path.read_bytes().decode("utf-8"))
        for node in tree["nodes"]:
            for each in ruling.NODE_ROWS:
                if f"{each.tree}_Class_Tree_Final.json" == name and node["id"] == each.node:
                    node["data"]["description"] = each.old_sentence
        path.write_text(json.dumps(tree, indent=2, ensure_ascii=False), encoding="utf-8")


@pytest.fixture
def unruled(tmp_path) -> tuple[pathlib.Path, pathlib.Path]:
    workbook = tmp_path / "All_Things_Cataclysm.xlsx"
    shutil.copyfile(ruling.WORKBOOK, workbook)
    _unrule_workbook(workbook)
    folder = tmp_path / "docs"
    folder.mkdir()
    for name in TREES:
        shutil.copyfile(ruling.DOCS / name, folder / name)
    _unrule_trees(folder)
    return workbook, folder


def _snapshot(workbook: pathlib.Path, folder: pathlib.Path) -> tuple[bytes, ...]:
    return (workbook.read_bytes(), *((folder / name).read_bytes() for name in TREES))


def test_check_reports_work_pending_on_an_unruled_copy_and_writes_nothing(unruled):
    workbook, folder = unruled
    before = _snapshot(workbook, folder)
    assert ruling.main(["--check", "--workbook", str(workbook), "--docs", str(folder)]) == 1
    assert _snapshot(workbook, folder) == before


def test_a_run_writes_the_ruling_and_a_second_run_writes_nothing(unruled):
    workbook, folder = unruled
    assert ruling.main(["--workbook", str(workbook), "--docs", str(folder)]) == 0
    after_first = _snapshot(workbook, folder)
    report = ruling.apply(workbook, folder)
    assert report[-1] == "everything is already written; nothing written"
    assert _snapshot(workbook, folder) == after_first
    assert ruling.main(["--check", "--workbook", str(workbook), "--docs", str(folder)]) == 0

    book = openpyxl.load_workbook(workbook)
    slots = book["Skill Slots"]
    h = _headers(slots)
    reserved = {slots.cell(r, h["Slot"]).value: slots.cell(r, h["Mana Reserved"]).value
                for r in range(2, slots.max_row + 1) if slots.cell(r, h["Slot"]).value}
    assert reserved == {"Basic": 0, "Heavy": 0, "Special": 0, "Support": 0, "Aura": 50, "Ultimate": 0,
                        "Movement": 0}
    positives = [c.value for c in book["Enchantments"]["A"]]
    assert ruling.NEW_POSITIVE_TEXT in positives and ruling.OLD_POSITIVE_TEXT not in positives
    for benefit in ruling.NEW_BENEFITS:
        assert positives.count(benefit.sentence) == 1
    for each in ruling.NODE_ROWS:
        tree = json.loads((folder / f"{each.tree}_Class_Tree_Final.json").read_bytes().decode("utf-8"))
        (node,) = [n for n in tree["nodes"] if n["id"] == each.node]
        assert node["data"]["description"] == each.new_sentence


def test_a_cell_holding_a_third_value_is_refused_and_nothing_is_saved(unruled):
    workbook, folder = unruled
    book = openpyxl.load_workbook(workbook)
    slots = book["Skill Slots"]
    h = _headers(slots)
    (aura,) = [r for r in range(2, slots.max_row + 1) if slots.cell(r, h["Slot"]).value == "Aura"]
    slots.cell(aura, h["Mana Cost"]).value = 35
    book.save(workbook)
    before = _snapshot(workbook, folder)
    assert ruling.main(["--workbook", str(workbook), "--docs", str(folder)]) == 2
    assert _snapshot(workbook, folder) == before
    with pytest.raises(ruling.RulingError, match="Mana Cost is 35"):
        ruling.apply(workbook, folder)


def test_a_sentence_holding_a_third_value_is_refused_and_nothing_is_saved(unruled):
    workbook, folder = unruled
    path = folder / "Bulwark_Class_Tree_Final.json"
    tree = json.loads(path.read_bytes().decode("utf-8"))
    for node in tree["nodes"]:
        if node["id"] == "basic_tw_t3_001":
            node["data"]["description"] = "Something somebody wrote since."
    path.write_text(json.dumps(tree, indent=2), encoding="utf-8")
    before = _snapshot(workbook, folder)
    assert ruling.main(["--workbook", str(workbook), "--docs", str(folder)]) == 2
    assert _snapshot(workbook, folder) == before


def test_rows_moved_by_another_branch_are_still_found_by_their_keys(unruled):
    """Two rows inserted at the top of every sheet the script edits move every row it finds; it must not care."""
    workbook, folder = unruled
    book = openpyxl.load_workbook(workbook)
    for name in ("Skill Slots", "Enchantments", "Enchantment Effects", "Passive Effects"):
        book[name].insert_rows(2, amount=2)
    book.save(workbook)
    assert ruling.main(["--workbook", str(workbook), "--docs", str(folder)]) == 0
    assert ruling.apply(workbook, folder)[-1] == "everything is already written; nothing written"


def test_the_new_keys_are_the_generator_s_row_names():
    """The keys are computed with the generator's own `row_name`, from the first 48 characters."""
    assert ruling.NEW_POSITIVE == "Positive_Your_auras_reserve_30_50_less_mana"
    assert ruling.NEW_NEGATIVE == "Negative_Your_auras_reserve_50_100_increased_mana"
    assert [b.key for b in ruling.NEW_BENEFITS] == [
        "Positive_Your_auras_reserve_15_30_reduced_mana",
        "Positive_Each_aura_you_hold_reserves_5_15_less_mana",
        "Positive_Your_gadgets_reserve_15_30_reduced_Fervour"]


def _values(path: pathlib.Path) -> dict[str, list[tuple]]:
    book = openpyxl.load_workbook(path, read_only=True)
    out = {}
    for name in ("Skill Slots", "Enchantments", "Enchantment Effects", "Passive Effects"):
        rows = [tuple(r) for r in book[name].iter_rows(values_only=True)]
        while rows and all(v is None for v in rows[-1]):
            rows.pop()
        out[name] = rows
    return out


def test_taking_the_ruling_out_and_writing_it_again_gives_back_the_repository_s_files(tmp_path):
    """`take_out` is what four older scripts' tests compare against, so it must be the exact inverse: taken out and
    written again, every value of the four sheets and every byte of the tree files is the repository's."""
    # FROM A COPY WITH THE WAR TREES' ROWS OF 2026-11-27 TAKEN OUT, which came after this ruling and append rows
    # after its rows (`tools/tests/without_the_war_trees_rows.py`), so "the repository's" files are that copy.
    before = without_the_war_trees_rows.docs()
    workbook = tmp_path / "All_Things_Cataclysm.xlsx"
    shutil.copyfile(before / "All_Things_Cataclysm.xlsx", workbook)
    folder = tmp_path / "docs"
    folder.mkdir()
    for name in TREES:
        shutil.copyfile(before / name, folder / name)

    ruling.take_out(workbook, folder)
    assert ruling.main(["--check", "--workbook", str(workbook), "--docs", str(folder)]) == 1
    assert ruling.main(["--workbook", str(workbook), "--docs", str(folder)]) == 0

    def trim(rows):
        # A COLUMN DELETED AND WRITTEN AGAIN LEAVES NO TRAILING EMPTY CELL BEHIND, so compare rows without them.
        out = []
        for row in rows:
            row = list(row)
            while row and row[-1] is None:
                row.pop()
            out.append(tuple(row))
        return out

    ours, theirs = _values(workbook), _values(before / "All_Things_Cataclysm.xlsx")
    for name in theirs:
        assert trim(ours[name]) == trim(theirs[name]), name
    for name in TREES:
        assert (folder / name).read_bytes() == (before / name).read_bytes(), name
