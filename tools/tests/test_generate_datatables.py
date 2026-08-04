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


class TestCityUpgradeTiers:
    """The tier cells use four notations and the sheet has no kind column."""

    @pytest.mark.parametrize("cell,effect,kind,value,days", [
        ("0.3",    "Increase max defense by 20%",         "Percent",         0.30, 0),
        ("1",      "Remove 25% of dungeons",              "Percent",         1.00, 0),
        ("10",     "no more than 15 dungeons",            "Flat",           10.00, 0),
        ("8",      "take 4 less days to beat",            "Flat",            8.00, 0),
        ("3x",     "provide 2x more experience",          "Multiplier",      3.00, 0),
        ("10/10%", "Every 20 days ... heal 5%.",          "IntervalPercent", 0.10, 10),
        ("5/15%",  "Every 20 days ... heal 5%.",          "IntervalPercent", 0.15, 5),
        ("",       "anything",                            "",                0.00, 0),
    ])
    def test_each_notation_parses(self, cell, effect, kind, value, days):
        assert gen.parse_tier(cell, effect, "Tier 2", 1) == (kind, value, days)

    def test_the_days_half_of_the_pair_is_not_lost(self):
        """Regression: this notation was once flattened to just the percentage,
        which discarded the change to the trigger interval."""
        _, value, days = gen.parse_tier("5/15%", "Every 20 days ... 5%", "T", 1)
        assert (value, days) == (0.15, 5)

    def test_a_bare_number_is_a_percentage_only_when_the_effect_says_so(self):
        assert gen.parse_tier("10", "increase by 5%", "T", 1)[0] == "Percent"
        assert gen.parse_tier("10", "5 more floors", "T", 1)[0] == "Flat"

    def test_an_unreadable_cell_is_an_error(self):
        with pytest.raises(gen.DataError, match="none of a percentage"):
            gen.parse_tier("banana", "effect", "Tier 2", 7)


class TestOneTimeUseUpgrades:
    SHEET = [["Type", "Tier 1", "Tier 2", "Tier 3"],
             ["Architect", "Increase max defense by 20%", 0.3, 0.4],
             ["Architect*", "Restore defenses by 50%", 0.75, 1],
             ["", "A last resort with no tiers", None, None]]

    def _rows(self, tmp_path):
        book = openpyxl.load_workbook(
            workbook_with(tmp_path / "w.xlsx", {"City Upgrades": self.SHEET}))
        return gen.city_upgrades(book)

    def test_the_asterisk_marks_one_time_use(self, tmp_path):
        rows = self._rows(tmp_path)
        assert [r["IsOneTimeUse"] for r in rows] == ["False", "True", "True"]

    def test_the_asterisk_is_stripped_from_the_branch(self, tmp_path):
        """Nothing downstream should have to parse punctuation to know this."""
        assert self._rows(tmp_path)[1]["Branch"] == "Architect"

    def test_the_unbranched_upgrade_is_kept_and_marked(self, tmp_path):
        row = self._rows(tmp_path)[2]
        assert row["Branch"] == ""
        assert row["BranchUndecided"] == "True"
        assert row["IsOneTimeUse"] == "True"

    def test_a_normal_upgrade_is_not_marked(self, tmp_path):
        row = self._rows(tmp_path)[0]
        assert row["IsOneTimeUse"] == "False"
        assert row["BranchUndecided"] == "False"


class TestGems:
    def test_the_everyday_value_is_read_from_the_effect_text(self, tmp_path):
        book = openpyxl.load_workbook(workbook_with(tmp_path / "w.xlsx", {
            "Gems": [["Column 1", "Everyday Gemstone", "Quality Gemstone",
                      "Superb Gemstone", "Masterful Gemstone", "Legendary Gemstone",
                      "Mythical Gemstone", "Ascendant Gemstone",
                      "Cataclysmic Gemstone", "Type"],
                     ["Of The Abyss", "10% chance to apply void splinter",
                      0.3, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, "Attack"]]}))
        row = gen.gems(book)[0]
        assert row["Everyday"] == pytest.approx(0.10)
        assert row["Quality"] == pytest.approx(0.30)
        assert row["Cataclysmic"] == pytest.approx(2.0)

    def test_a_gem_with_no_percentage_in_its_text_is_an_error(self, tmp_path):
        book = openpyxl.load_workbook(workbook_with(tmp_path / "w.xlsx", {
            "Gems": [["Column 1", "Everyday Gemstone", "Quality Gemstone",
                      "Superb Gemstone", "Masterful Gemstone", "Legendary Gemstone",
                      "Mythical Gemstone", "Ascendant Gemstone",
                      "Cataclysmic Gemstone", "Type"],
                     ["Of Nothing", "does a thing", 0.3, 0.5, 0.75, 1.0, 1.25,
                      1.5, 2.0, "Attack"]]}))
        with pytest.raises(gen.DataError, match="states no percentage"):
            gen.gems(book)


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


class TestShapeParams:
    """A skill's shape names which template runs; its params are that template's numbers.

    EVERY ONE OF THESE GUARDS EXISTS BECAUSE THE SILENT FAILURE IS THE BAD ONE.
    A misspelled parameter would read as zero, and a shape with a radius of zero
    hits nothing: it activates, spends mana, starts its cooldown, and does
    nothing. That is the same failure as issue #155's cooldown of zero, which
    went unnoticed across 77 skills.
    """

    def test_a_well_formed_cell_parses(self):
        assert gen.parse_shape_params(
            "Radius=4; Angle=120; Burn=1", "Strike", "here") == {
                "Radius": "4", "Angle": "120", "Burn": "1"}

    def test_an_empty_cell_is_no_parameters(self):
        assert gen.parse_shape_params("", "Strike", "here") == {}

    def test_a_parameter_the_shape_does_not_read_is_refused(self):
        # Pierce belongs to Projectile. On a Strike it would be ignored, and the
        # designer would have no way to tell it had been.
        with pytest.raises(gen.DataError, match="no parameter 'Pierce'"):
            gen.parse_shape_params("Pierce=3", "Strike", "here")

    def test_a_misspelled_parameter_is_refused(self):
        with pytest.raises(gen.DataError, match="no parameter 'Radiuss'"):
            gen.parse_shape_params("Radiuss=4", "Strike", "here")

    def test_a_rider_is_accepted_on_every_shape(self):
        for shape in gen.SHAPE_PARAMS:
            assert gen.parse_shape_params(
                "GroundRadius=3; GroundDuration=6", shape, "here") == {
                    "GroundRadius": "3", "GroundDuration": "6"}

    def test_a_non_numeric_value_is_refused(self):
        with pytest.raises(gen.DataError, match="not a number"):
            gen.parse_shape_params("Radius=wide", "Strike", "here")

    def test_a_missing_equals_is_refused(self):
        with pytest.raises(gen.DataError, match="not Key=Value"):
            gen.parse_shape_params("Radius 4", "Strike", "here")

    def test_a_repeated_parameter_is_refused(self):
        with pytest.raises(gen.DataError, match="given twice"):
            gen.parse_shape_params("Radius=4; Radius=6", "Strike", "here")

    def test_an_empty_value_is_refused(self):
        with pytest.raises(gen.DataError, match="has no value"):
            gen.parse_shape_params("Radius=", "Strike", "here")

    def test_mode_takes_only_the_three_movement_kinds(self):
        assert gen.parse_shape_params("Mode=Blink", "Movement", "here") == {
            "Mode": "Blink"}
        with pytest.raises(gen.DataError, match="not one of"):
            gen.parse_shape_params("Mode=Teleport", "Movement", "here")

    def test_an_unknown_shape_fails_generation(self, tmp_path):
        path = workbook_with(tmp_path / "b.xlsx", {"Weapon Skills": [
            ["Weapon Type", "Damage Type", "Slot", "Skill Name",
             "Skill Description", "Tags", "Shape", "Shape Params"],
            ["Sword", "War", "Heavy", "Cut", "Cuts.", "", "Wiggle", ""]]})
        book = openpyxl.load_workbook(path, data_only=True)
        with pytest.raises(gen.DataError, match="shape 'Wiggle' is not one of"):
            gen.weapon_skills(book)

    def test_parameters_without_a_shape_fail_generation(self, tmp_path):
        path = workbook_with(tmp_path / "b.xlsx", {"Weapon Skills": [
            ["Weapon Type", "Damage Type", "Slot", "Skill Name",
             "Skill Description", "Tags", "Shape", "Shape Params"],
            ["Sword", "War", "Heavy", "Cut", "Cuts.", "", "", "Radius=4"]]})
        book = openpyxl.load_workbook(path, data_only=True)
        with pytest.raises(gen.DataError, match="shape parameters but no shape"):
            gen.weapon_skills(book)

    def test_a_shape_without_a_skill_name_fails_generation(self, tmp_path):
        path = workbook_with(tmp_path / "b.xlsx", {"Weapon Skills": [
            ["Weapon Type", "Damage Type", "Slot", "Skill Name",
             "Skill Description", "Tags", "Shape", "Shape Params"],
            ["Sword", "War", "Heavy", "", "", "", "Strike", "Radius=4"]]})
        book = openpyxl.load_workbook(path, data_only=True)
        with pytest.raises(gen.DataError, match="shape but no skill name"):
            gen.weapon_skills(book)

    def test_a_sheet_without_the_two_columns_still_generates(self, tmp_path):
        """The 61 War rows predate shapes and must keep generating."""
        path = workbook_with(tmp_path / "b.xlsx", {"Weapon Skills": [
            ["Weapon Type", "Damage Type", "Slot", "Skill Name",
             "Skill Description", "Tags"],
            ["Sword", "War", "Heavy", "Cut", "Cuts.", ""]]})
        book = openpyxl.load_workbook(path, data_only=True)
        rows = gen.weapon_skills(book)
        assert rows[0]["Shape"] == "" and rows[0]["ShapeParams"] == ""


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
