"""Tests for the DataTable CSV generator.

Most run against fixture workbooks built in the test, so a design change cannot
break them. The last group checks the real workbook, because the committed CSVs
going stale is the drift this tool exists to prevent.
"""

from __future__ import annotations

import pathlib
import sys

import openpyxl
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import generate_datatables as gen  # noqa: E402


def workbook_with(path: pathlib.Path, sheets: dict[str, list[list]]) -> pathlib.Path:
    """Build a workbook from {sheet name: rows}, always including a Tags sheet."""
    book = openpyxl.Workbook()
    book.remove(book.active)
    if "Tags" not in sheets:
        sheets = dict(sheets)
        sheets["Tags"] = [["Tag Name", "Description"],
                          ["Element.War", "Physical"],
                          ["Slot.Ultimate", "Ultimates"]]
    for name, rows in sheets.items():
        sheet = book.create_sheet(name)
        for row in rows:
            sheet.append(row)
    book.save(path)
    return path


class TestRowNames:
    def test_row_names_are_safe_for_fnames(self):
        assert gen.row_name("Fallen City", "Edict of Silence!") == \
            "Fallen_City_Edict_of_Silence"

    def test_empty_parts_are_skipped(self):
        assert gen.row_name("", "Thing", "") == "Thing"

    def test_a_name_that_reduces_to_nothing_is_an_error(self):
        with pytest.raises(gen.DataError, match="could not build a row name"):
            gen.row_name("!!!", "###")

    def test_duplicate_row_names_get_suffixes(self):
        rows = [{"Name": "A"}, {"Name": "A"}, {"Name": "A"}, {"Name": "B"}]
        gen.unique(rows, "test")
        assert [r["Name"] for r in rows] == ["A", "A_1", "A_2", "B"]


class TestNamedCells:
    def test_splits_name_from_description(self):
        assert gen.split_named("Hellfire Aura: burns things") == \
            ("Hellfire Aura", "burns things")

    def test_a_cell_with_no_colon_keeps_the_whole_text(self):
        assert gen.split_named("just a description") == ("", "just a description")


class TestDungeonModifiers:
    def test_reads_rows(self, tmp_path):
        book = openpyxl.load_workbook(workbook_with(tmp_path / "w.xlsx", {
            "Dungeon Modifiers": [["Cataclysm Type", "Modifier Name", "Weight", "Description"],
                                  ["Demonic", "Hellfire", 20, "burns"]]}))
        rows = gen.dungeon_modifiers(book)
        assert rows == [{"Name": "Demonic_Hellfire", "CataclysmType": "Demonic",
                         "ModifierName": "Hellfire", "Weight": 20.0,
                         "Description": "burns"}]

    def test_rejects_an_unknown_cataclysm(self, tmp_path):
        book = openpyxl.load_workbook(workbook_with(tmp_path / "w.xlsx", {
            "Dungeon Modifiers": [["Cataclysm Type", "Modifier Name", "Weight", "Description"],
                                  ["Sparkly", "Thing", 1, "d"]]}))
        with pytest.raises(gen.DataError, match="not a Cataclysm type"):
            gen.dungeon_modifiers(book)

    def test_rejects_a_non_numeric_weight(self, tmp_path):
        book = openpyxl.load_workbook(workbook_with(tmp_path / "w.xlsx", {
            "Dungeon Modifiers": [["Cataclysm Type", "Modifier Name", "Weight", "Description"],
                                  ["Demonic", "Thing", "heavy", "d"]]}))
        with pytest.raises(gen.DataError, match="not a number"):
            gen.dungeon_modifiers(book)


class TestEnchantments:
    """The sheet holds two independent tables side by side."""

    SHEET = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        ["More damage", "Generic", 1, "Element.War", None,
         "No basic attack", "Generic", 2, "Slot.Ultimate"],
        ["Only a positive", "Generic", 3, "Element.War", None, None, None, None, None],
    ]

    def test_positives_and_negatives_are_read_separately(self, tmp_path):
        book = openpyxl.load_workbook(
            workbook_with(tmp_path / "w.xlsx", {"Enchantments": self.SHEET}))
        positives = gen.enchantments(book, negative=False)
        negatives = gen.enchantments(book, negative=True)
        assert len(positives) == 2
        assert len(negatives) == 1
        assert positives[0]["IsNegative"] == "False"
        assert negatives[0]["IsNegative"] == "True"

    def test_the_negatives_tag_column_is_read(self, tmp_path):
        """Regression: an off-by-one once pointed this at the Weight column,
        so no negative enchantment's tags were ever checked."""
        book = openpyxl.load_workbook(
            workbook_with(tmp_path / "w.xlsx", {"Enchantments": self.SHEET}))
        assert gen.enchantments(book, negative=True)[0]["Tags"] == "Slot.Ultimate"

    def test_a_short_negative_column_does_not_invent_rows(self, tmp_path):
        book = openpyxl.load_workbook(
            workbook_with(tmp_path / "w.xlsx", {"Enchantments": self.SHEET}))
        assert all(r["Effect"] for r in gen.enchantments(book, negative=True))


class TestReshapedSheets:
    def test_enemy_modifiers_matrix_becomes_rows(self, tmp_path):
        book = openpyxl.load_workbook(workbook_with(tmp_path / "w.xlsx", {
            "Enemy Modifiers": [["Demonic Modifiers", "Death Modifiers"],
                                ["Hellfire: burns", "Haunting: copies"],
                                ["Brute: tanky", None]]}))
        rows = gen.enemy_modifiers(book)
        assert len(rows) == 3
        assert {r["CataclysmType"] for r in rows} == {"Demonic", "Death"}
        assert rows[0]["ModifierName"] == "Hellfire"

    def test_status_effects_treat_the_first_row_as_data(self, tmp_path):
        """These sheets have no header. Skipping row one loses an effect."""
        book = openpyxl.load_workbook(workbook_with(tmp_path / "w.xlsx", {
            "Buffs": [["Mana Surge: more damage"], ["Warhound: a minion"]],
            "Debuffs": [["Wither: reduces things"]],
            "DoTs": [["Bleed: hurts"]]}))
        rows = gen.status_effects(book)
        assert len(rows) == 4
        assert {r["EffectKind"] for r in rows} == {"Buff", "Debuff", "DoT"}
        assert any(r["EffectName"] == "Mana Surge" for r in rows)


class TestValidation:
    def test_an_undefined_tag_is_reported(self):
        tables = {"T": [{"Name": "row", "Tags": "Element.War, Type.Nonexistent"}]}
        problems = gen.validate_tags(tables, {"Element", "Element.War"})
        assert len(problems) == 1
        assert "Type.Nonexistent" in problems[0]

    def test_an_implicit_parent_is_accepted(self):
        tables = {"T": [{"Name": "row", "Tags": "Item.Weapon"}]}
        assert gen.validate_tags(tables, {"Item", "Item.Weapon", "Item.Weapon.Sword"}) == []

    @pytest.mark.parametrize("weight", [0, -1, 101])
    def test_a_weight_outside_the_range_is_reported(self, weight):
        tables = {"T": [{"Name": "row", "Weight": weight}]}
        assert len(gen.validate_weights(tables)) == 1

    def test_a_valid_weight_passes(self):
        assert gen.validate_weights({"T": [{"Name": "r", "Weight": 20}]}) == []


class TestAgainstTheRealWorkbook:
    def test_the_committed_csvs_are_current(self):
        if not gen.WORKBOOK.is_file():
            pytest.skip("design workbook not present")
        assert gen.main(["--check"]) == 0, (
            "game/Data/*.csv are out of date. "
            "Run: python tools/generate_datatables.py"
        )

    def test_every_table_has_rows(self):
        if not gen.WORKBOOK.is_file():
            pytest.skip("design workbook not present")
        book = openpyxl.load_workbook(gen.WORKBOOK, data_only=True)
        for name, builder in gen.TABLES.items():
            assert len(builder(book)) > 0, f"{name} produced no rows"
