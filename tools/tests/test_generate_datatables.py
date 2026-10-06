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

    def test_two_gems_cannot_share_a_name(self, tmp_path):
        """Issue #211. Two different gems were both called Of Recovery.

        One raised health regeneration and one reduced cooldowns. `unique` gave
        the second the row key `Gem_Of_Recovery_1`, so both imported and both
        showed the player the same name, with nothing to tell them apart and
        nothing anywhere reporting a problem. It stood for a month.
        """
        book = openpyxl.load_workbook(workbook_with(tmp_path / "w.xlsx", {
            "Gems": [["Column 1", "Everyday Gemstone", "Quality Gemstone",
                      "Superb Gemstone", "Masterful Gemstone", "Legendary Gemstone",
                      "Mythical Gemstone", "Ascendant Gemstone",
                      "Cataclysmic Gemstone", "Type"],
                     ["Of Recovery", "Increases hp regen by 10%",
                      0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.6, "Defense"],
                     ["Of Recovery", "Increases CDR by 10%",
                      0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.9, "Utility"]]}))
        with pytest.raises(gen.DataError, match="already the name of the gem"):
            gen.gems(book)

    def test_two_gems_with_different_names_are_fine(self, tmp_path):
        """The other side of it: the check must not refuse ordinary rows.

        A guard that rejected two gems sharing an effect, or two gems in the
        same type, would pass the test above and break the sheet.
        """
        book = openpyxl.load_workbook(workbook_with(tmp_path / "w.xlsx", {
            "Gems": [["Column 1", "Everyday Gemstone", "Quality Gemstone",
                      "Superb Gemstone", "Masterful Gemstone", "Legendary Gemstone",
                      "Mythical Gemstone", "Ascendant Gemstone",
                      "Cataclysmic Gemstone", "Type"],
                     ["Of Recovery", "Increases hp regen by 10%",
                      0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.6, "Defense"],
                     ["Of Urgency", "Increases CDR by 10%",
                      0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.9, "Utility"]]}))
        names = [row["Name"] for row in gen.gems(book)]
        assert names == ["Gem_Of_Recovery", "Gem_Of_Urgency"], (
            "a gem row key must be readable, not a numeric suffix on someone "
            "else's name")

    @staticmethod
    def one_gem(tmp_path, effect: str, *values):
        """A workbook holding a single gem, so a value can be read back."""
        return openpyxl.load_workbook(workbook_with(tmp_path / "w.xlsx", {
            "Gems": [["Column 1", "Everyday Gemstone", "Quality Gemstone",
                      "Superb Gemstone", "Masterful Gemstone",
                      "Legendary Gemstone", "Mythical Gemstone",
                      "Ascendant Gemstone", "Cataclysmic Gemstone", "Type"],
                     ["Of Testing", effect, *values, "Utility"]]}))

    def test_a_percentage_written_without_a_leading_zero_is_read_whole(
            self, tmp_path):
        """Issue #246. The pattern was `\\d+(?:\\.\\d+)?`, which needs a digit
        before the decimal point, so ".5%" matched only its "5%" and the gem Of
        The Goblin shipped at 0.05 -- ten times its stated value, and larger
        than the six rarity tiers above it."""
        book = self.one_gem(tmp_path, "Increases Magic Find by .5%",
                            0.01, 0.015, 0.02, 0.025, 0.03, 0.035, 0.05)
        assert gen.gems(book)[0]["Everyday"] == pytest.approx(0.005)

    @pytest.mark.parametrize("effect,expected", [
        ("Increases Magic Find by .5%", 0.005),
        ("Increases Magic Find by 0.5%", 0.005),
        ("Increases Mana by 5%", 0.05),
        ("Increases AoE by 10%", 0.10),
        ("Increases movespeed by 1%", 0.01),
        ("20% chance to apply poison", 0.20),
        ("Increases something by 2.5 %", 0.025),
    ])
    def test_every_way_a_percentage_gets_written(self, tmp_path, effect,
                                                 expected):
        """The wider pattern must still read the forms that already worked. A
        fix that only handled the leading dot would be as wrong as the original.
        Values below rise from the largest of these so the ladder check passes
        whichever effect is used."""
        book = self.one_gem(tmp_path, effect,
                            0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9)
        assert gen.gems(book)[0]["Everyday"] == pytest.approx(expected)

    def test_a_gem_that_gets_worse_as_it_gets_rarer_is_an_error(self, tmp_path):
        """The check that would have caught issue #246 whatever caused it.

        Gear and gem rarity equal the difficulty tier, so this ladder is the
        whole of a gem's progression. A tier paying less than the one below it
        means finding a rarer gem is a downgrade, and nothing in the interface
        would say so.
        """
        book = self.one_gem(tmp_path, "Increases Magic Find by 5%",
                            0.01, 0.015, 0.02, 0.025, 0.03, 0.035, 0.05)
        with pytest.raises(gen.DataError, match="does not get better as it gets"):
            gen.gems(book)

    def test_a_gem_that_stalls_at_one_tier_is_also_an_error(self, tmp_path):
        """Equal is not rising. A tier worth exactly what the one below is worth
        is a rarity step a player pays for and gets nothing from."""
        book = self.one_gem(tmp_path, "Increases Magic Find by 1%",
                            0.02, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07)
        with pytest.raises(gen.DataError, match="does not get better as it gets"):
            gen.gems(book)

    def test_the_error_names_the_gem_and_shows_all_eight_values(self, tmp_path):
        """A message giving only 'a gem is wrong' costs whoever reads it a
        search of the sheet."""
        book = self.one_gem(tmp_path, "Increases Magic Find by 5%",
                            0.01, 0.015, 0.02, 0.025, 0.03, 0.035, 0.05)
        with pytest.raises(gen.DataError) as caught:
            gen.gems(book)
        message = str(caught.value)
        assert "Of Testing" in message
        assert "0.05" in message and "0.01" in message

    def test_a_rising_ladder_is_accepted(self, tmp_path):
        """The other side of it. A check that refused ordinary gems would pass
        every test above and break the whole sheet."""
        book = self.one_gem(tmp_path, "Increases Magic Find by .5%",
                            0.01, 0.015, 0.02, 0.025, 0.03, 0.035, 0.05)
        row = gen.gems(book)[0]
        assert row["Everyday"] == pytest.approx(0.005)
        assert row["Cataclysmic"] == pytest.approx(0.05)


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

    # A RETIRED ENCHANTMENT. Issue #1833, ruled 2026-09-25: weight 0 on an
    # ordinary enchantment row means it never drops, and only there.
    @pytest.mark.parametrize("table", ["EnchantmentsPositive", "EnchantmentsNegative"])
    def test_weight_0_retires_an_ordinary_enchantment(self, table):
        row = {"Name": "r", "Weight": 0, "EnchantmentType": "Generic"}
        assert gen.validate_weights({table: [row]}) == []
        assert gen.is_retired(table, row)

    def test_weight_0_is_refused_on_a_set_row(self):
        row = {"Name": "r", "Weight": 0, "EnchantmentType": "Set"}
        assert len(gen.validate_weights({"EnchantmentsPositive": [row]})) == 1

    def test_weight_0_is_refused_on_any_other_table(self):
        row = {"Name": "r", "Weight": 0, "EnchantmentType": "Generic"}
        assert len(gen.validate_weights({"Affixes": [row]})) == 1

    def test_a_negative_weight_is_still_refused_on_an_enchantment(self):
        row = {"Name": "r", "Weight": -1, "EnchantmentType": "Generic"}
        assert len(gen.validate_weights({"EnchantmentsPositive": [row]})) == 1


class TestAnEnchantmentRowMustStateAType:
    """ISSUE #1486. One negative row had an empty Type for as long as the sheet
    existed and nothing anywhere reported it.

    WHY IT WAS SILENT. `UCataclysmDropRoll::EnchantmentSuitsSlot` decides
    whether a row is a set by asking whether its type IS `Set`. An empty string
    is not `Set`, so the blank row was drawn like any other drawback, which is
    what the design wants of it. The fault only surfaces when something asks the
    question the other way round -- a filter that keeps `Generic`, or a
    validator requiring a known type -- and then one of the eight severe
    drawbacks written at weight 1 vanishes from the pool with nothing failing.

    THE COUNTER-TESTS MATTER AS MUCH AS THE FAULT ONES. A check that reported
    every row would pass `test_a_blank_type_is_reported` and stop the sheet
    generating at all, so `Generic`, `Set` and a lowercase spelling each have a
    test saying they are accepted.
    """

    @staticmethod
    def rows(*types: str) -> dict[str, list[dict]]:
        return {"EnchantmentsNegative": [
            {"Name": f"Negative_row_{i}", "EnchantmentType": written}
            for i, written in enumerate(types)]}

    def test_a_blank_type_is_reported(self):
        """The exact fault. `''` is what the generator produces from an empty
        cell, because `clean()` turns a `None` into one."""
        problems = gen.validate_enchantment_types(self.rows(""))
        assert len(problems) == 1, problems
        assert "Negative_row_0" in problems[0]
        assert "EnchantmentType is ''" in problems[0]

    def test_a_cell_holding_only_spaces_is_reported(self):
        """A cell someone typed a space into looks filled in the spreadsheet."""
        assert len(gen.validate_enchantment_types(self.rows("   "))) == 1

    def test_a_misspelt_type_is_reported(self):
        """`Genric` reads as "not a set" exactly the way a blank does."""
        problems = gen.validate_enchantment_types(self.rows("Genric"))
        assert len(problems) == 1, problems
        assert "'Genric'" in problems[0]

    def test_generic_and_set_are_accepted(self):
        assert gen.validate_enchantment_types(self.rows("Generic", "Set")) == []

    def test_the_comparison_ignores_case(self):
        """`EnchantmentSuitsSlot` passes `ESearchCase::IgnoreCase`, so a
        lowercase cell works in the game. Failing generation over one would be
        this check inventing a rule the game does not have."""
        assert gen.validate_enchantment_types(self.rows("generic", "SET")) == []

    def test_both_enchantment_tables_are_read(self):
        """Reading only the negatives would miss a blank on the positive side,
        and the positives are the larger table of the two."""
        tables = {"EnchantmentsPositive": [{"Name": "Positive_row",
                                            "EnchantmentType": ""}]}
        problems = gen.validate_enchantment_types(tables)
        assert len(problems) == 1, problems
        assert "EnchantmentsPositive/Positive_row" in problems[0]

    def test_a_table_that_carries_no_type_column_is_reported(self):
        """A check that inspected nothing passes for the same reason a correct
        sheet does, and from the outside the two are identical. This is the one
        that tells them apart."""
        tables = {"EnchantmentsNegative": [{"Name": "Negative_row"}]}
        problems = gen.validate_enchantment_types(tables)
        assert len(problems) == 1, problems
        assert "inspected nothing" in problems[0]

    def test_tables_without_either_enchantment_table_are_left_alone(self):
        """Every other validator in this file returns nothing for a table set
        that does not contain its subject, and this one must too, or the
        fixture workbooks the rest of these tests build would all fail."""
        assert gen.validate_enchantment_types({}) == []
        assert gen.validate_enchantment_types({"Affixes": [{"Name": "a"}]}) == []


class TestASkillRowCannotNameADamageTypeNobodyHas:
    """ISSUE #579. The Damage Type column of the Weapon Skills sheet was checked
    by nothing at all, one column over from a WeaponType column that has been
    checked since a rename left five weapons with no skills.

    WHY A BAD ONE IS SILENT. `UCataclysmWeaponSkills::SkillsFor` in the engine
    compares the damage type exactly, so a row naming one nobody has is offered
    to nobody: it generates cleanly, imports cleanly, fills no slot and reports
    nothing. One misspelling costs one skill and says so nowhere.
    """

    DECLARED = {"Element.Demonic", "Element.War", "Item.Weapon.Sword"}

    @staticmethod
    def skills(*damage_types: str) -> dict[str, list[dict]]:
        return {"WeaponSkills": [
            {"Name": f"{d}_Sword_Heavy", "WeaponType": "Sword",
             "DamageType": d, "Slot": "Heavy"} for d in damage_types]}

    def test_declared_damage_types_pass(self):
        assert gen.validate_weapon_skill_damage_types(
            self.skills("Demonic", "War"), self.DECLARED) == []

    def test_a_damage_type_nobody_declared_is_reported(self):
        problems = gen.validate_weapon_skill_damage_types(
            self.skills("Demonic", "War", "Demonc"), self.DECLARED)
        assert len(problems) == 1
        assert "'Demonc' is not declared" in problems[0]

    def test_the_wildcard_is_refused_by_name_and_says_why(self):
        """`All` is what the WEAPON column means by every weapon. Somebody
        writing it in this column has guessed at a symmetry that is not there,
        so the refusal says that rather than only listing the legal values."""
        problems = gen.validate_weapon_skill_damage_types(
            self.skills("Demonic", "War", "All"), self.DECLARED)
        assert len(problems) == 1
        assert "is not a wildcard" in problems[0]
        assert "granted to nobody" in problems[0]

    def test_a_damage_type_with_no_rows_at_all_is_reported(self):
        """The other direction. A Cataclysm whose characters have no skills is a
        hole rather than a design choice, which is the same check the weapon
        column has carried since the rename that produced it."""
        problems = gen.validate_weapon_skill_damage_types(
            self.skills("Demonic"), self.DECLARED)
        assert len(problems) == 1
        assert "the War damage type has no rows at all" in problems[0]

    def test_it_reads_the_tags_sheet_rather_than_a_list_written_here(self):
        """So adding a ninth damage type to the design needs no change in
        tools/generate_datatables.py."""
        assert gen.validate_weapon_skill_damage_types(
            self.skills("Rust"), {"Element.Rust"}) == []

    def test_the_real_workbook_passes(self):
        """The check above is worth nothing if it does not run against the
        shipping data. All 398 rows name one of the eight declared types."""
        import openpyxl
        if not gen.WORKBOOK.is_file():
            pytest.skip("the design workbook is not present")
        book = openpyxl.load_workbook(gen.WORKBOOK, data_only=True)
        tables = {"WeaponSkills": gen.weapon_skills(book)}
        assert gen.validate_weapon_skill_damage_types(
            tables, gen.declared_tags(book)) == []


class TestWeaponTagsAreNamedAfterTheWeapon:
    """ISSUE #620. Three weapons carried a tag named after what they used to be
    called -- a Greataxe's rows said `Item.Weapon.2hAxe` -- and nothing compared
    the two, so the old vocabulary survived a rename that corrected everything
    else.

    NOTHING WAS BROKEN BY IT, which is why it lasted. The naming was consistent
    within the data, so every lookup worked and no test failed.
    """

    @staticmethod
    def bases(*weapon_types: str) -> dict[str, list[dict]]:
        return {"ItemBases": [{"Name": f"Weapon_{t}", "WeaponType": t}
                              for t in weapon_types]}

    def test_a_tag_named_after_the_weapon_passes(self):
        assert gen.validate_weapon_tags(
            self.bases("Greataxe"), {"Item.Weapon.Greataxe"}) == []

    def test_the_old_name_is_reported_from_both_sides(self):
        problems = gen.validate_weapon_tags(
            self.bases("Greataxe"), {"Item.Weapon.2hAxe"})
        assert len(problems) == 2
        assert any("Item.Weapon.Greataxe is not declared" in p for p in problems)
        assert any("Item.Weapon.2hAxe names no weapon type" in p
                   for p in problems)

    def test_a_weapon_with_no_tag_at_all_is_reported(self):
        problems = gen.validate_weapon_tags(
            self.bases("Sword", "Whip"), {"Item.Weapon.Sword"})
        assert len(problems) == 1
        assert "Item.Weapon.Whip is not declared" in problems[0]

    def test_a_space_in_a_weapon_type_is_removed_and_nothing_else_is(self):
        """A gameplay tag cannot contain a space and "2H Crossbow" does. The
        letter case is not touched, so the tag is `Item.Weapon.2HCrossbow` and
        not the `2hCrossbow` it used to be."""
        assert gen.weapon_tag_leaf("2H Crossbow") == "2HCrossbow"
        assert gen.validate_weapon_tags(
            self.bases("2H Crossbow"), {"Item.Weapon.2HCrossbow"}) == []
        assert gen.validate_weapon_tags(
            self.bases("2H Crossbow"), {"Item.Weapon.2hCrossbow"}) != []

    def test_tags_outside_the_weapon_prefix_are_ignored(self):
        """It reads `Item.Weapon.*` and nothing else, so the other 166 declared
        tags are not weapons that went missing."""
        assert gen.validate_weapon_tags(
            self.bases("Sword"),
            {"Item.Weapon.Sword", "Item.Slot.Weapon", "Element.War"}) == []

    def test_the_real_workbook_passes(self):
        """The check above is worth nothing if it does not run against the
        shipping data. This is what the four renames were for."""
        import openpyxl
        workbook = gen.WORKBOOK
        if not workbook.is_file():
            pytest.skip("the design workbook is not present")
        book = openpyxl.load_workbook(workbook, data_only=True)
        tables = {"ItemBases": gen.TABLES["ItemBases"](book)}
        assert gen.validate_weapon_tags(tables, gen.declared_tags(book)) == []


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


class TestASkillsOwnCriticalStrikeChance:
    """The Crit Chance column of the Weapon Skills sheet. Issue #657.

    WHAT WAS MISSING. The design says critical strike chance belongs to the skill
    being used -- its stat source table names "the skill being used" and adds "A
    character has no critical strike chance in the abstract" -- and the sheet had
    nowhere to say it. Every one of the 398 rows took the 5% default and a skill
    designed to critically strike more or less often than average could not be
    built.

    THE SENTINEL IS -1 AND NOT 0, which is what most of these check. The decision
    of 2026-08-04 says the 5% is "a default and not a floor: a skill that states
    1% gets 1%", so a skill built never to critically strike states 0 and must get
    0 rather than silently getting 5.
    """

    HEADERS = ["Weapon Type", "Damage Type", "Slot", "Skill Name",
               "Skill Description", "Tags", "Shape", "Shape Params",
               "Crit Chance"]

    def rows(self, tmp_path, crit):
        path = workbook_with(tmp_path / "b.xlsx", {"Weapon Skills": [
            self.HEADERS,
            ["Sword", "War", "Heavy", "Cut", "Cuts.", "", "", "", crit]]})
        book = openpyxl.load_workbook(path, data_only=True)
        return gen.weapon_skills(book)

    def test_a_blank_cell_states_nothing(self, tmp_path):
        assert self.rows(tmp_path, None)[0]["CritChancePercent"] == -1.0

    def test_a_stated_chance_is_carried(self, tmp_path):
        assert self.rows(tmp_path, 12.5)[0]["CritChancePercent"] == 12.5

    def test_zero_is_a_real_answer_and_not_a_blank(self, tmp_path):
        """A skill designed never to critically strike states 0 and gets 0."""
        assert self.rows(tmp_path, 0)[0]["CritChancePercent"] == 0.0

    def test_a_chance_written_as_text_is_read(self, tmp_path):
        """A spreadsheet cell formatted as text still holds a number."""
        assert self.rows(tmp_path, "7")[0]["CritChancePercent"] == 7.0

    def test_something_that_is_not_a_number_fails_generation(self, tmp_path):
        with pytest.raises(gen.DataError, match="is not a number"):
            self.rows(tmp_path, "often")

    def test_a_chance_above_the_hard_cap_fails_generation(self, tmp_path):
        """Otherwise the engine clamps it to 100 and the row lies."""
        with pytest.raises(gen.DataError, match="outside 0 to 100"):
            self.rows(tmp_path, 150)

    def test_a_negative_chance_fails_generation(self, tmp_path):
        """-1 is the generator's own sentinel, not something a row may state."""
        with pytest.raises(gen.DataError, match="outside 0 to 100"):
            self.rows(tmp_path, -1)

    def test_a_sheet_without_the_column_still_generates(self, tmp_path):
        """Every row predates this column, so a short sheet must still work."""
        path = workbook_with(tmp_path / "b.xlsx", {"Weapon Skills": [
            ["Weapon Type", "Damage Type", "Slot", "Skill Name",
             "Skill Description", "Tags"],
            ["Sword", "War", "Heavy", "Cut", "Cuts.", ""]]})
        book = openpyxl.load_workbook(path, data_only=True)
        assert gen.weapon_skills(book)[0]["CritChancePercent"] == -1.0

    def test_every_shipped_row_states_nothing(self):
        """No skill is designed to differ yet, and that is the owner's call.

        This is not a rule -- it is a record of where the data stands. When a
        skill is deliberately given its own chance, change this test with it.
        """
        book = openpyxl.load_workbook(gen.WORKBOOK, data_only=True)
        stated = {r["Name"]: r["CritChancePercent"]
                  for r in gen.weapon_skills(book)
                  if r["CritChancePercent"] != -1.0}
        assert not stated, (
            "these skill rows state a critical strike chance of their own: "
            f"{stated}. That is allowed; update this test to say so.")


class TestBasicAttacks:
    """The basic attack on the weapon base. Issue #524.

    THE SILENT FAILURE HERE IS A CHARACTER WITH NOTHING BETWEEN ITS COOLDOWNS.
    Before this, `game/Data/WeaponSkills.csv` had 398 rows across six slots and
    not one Basic row anywhere, so every character was granted five abilities and
    no ordinary attack. The project owner found it by playing. An empty column is
    exactly as invisible as an empty sheet, so the generator refuses one.
    """

    # EVERY COLUMN THE READER ASKS FOR, the ones this test leaves empty
    # appended at the end so no row changes. Issue #1882: a column a
    # sheet lacks is now refused rather than read as empty.
    HEADERS = ["Base Name", "Slot", "Hands", "Sub-Type", "Weapon Type",
               "Max Damage Types", "Implicit 1 Stat", "Implicit 1 Kind",
               "Implicit 1 Value", "Attack Speed", "Basic Shape",
               "Basic Shape Params", "Cells Wide", "Cells High",
               "Implicit 2 Stat", "Implicit 2 Kind", "Implicit 2 Value"]

    def sheet(self, tmp_path, *rows) -> "openpyxl.Workbook":
        path = workbook_with(tmp_path / "b.xlsx",
                             {"Item Bases": [self.HEADERS, *rows]})
        return openpyxl.load_workbook(path, data_only=True)

    def armed(self, **changes) -> list:
        row = ["Sword", "Weapon", 1, "Slashing", "Sword", 4,
               "attack_damage", "flat", 40.0, 1.3,
               "Strike", "Radius=1.8; Angle=90; MaxTargets=1", 1, 3]
        for column, value in changes.items():
            row[self.HEADERS.index(column)] = value
        return row

    def test_a_base_with_no_footprint_is_refused(self, tmp_path):
        """Issue #855. Every base needs one, because a base with no
        footprint could not be placed in the bag at all and a zero would
        read as a piece that takes no room rather than as one somebody
        forgot to measure."""
        for column in ("Cells Wide", "Cells High"):
            with pytest.raises(gen.DataError, match=f"no {column}"):
                gen.item_bases(self.sheet(
                    tmp_path, self.armed(**{column: ""})))
            with pytest.raises(gen.DataError, match=f"no {column}"):
                gen.item_bases(self.sheet(
                    tmp_path, self.armed(**{column: 0})))

    def test_a_footprint_reaches_the_generated_row(self, tmp_path):
        """The figures come from the footprint table in
        docs/Inventory_Screen_Design.md and have to survive the trip."""
        rows = gen.item_bases(self.sheet(
            tmp_path, self.armed(**{"Cells Wide": 2, "Cells High": 6})))
        assert rows[0]["CellsWide"] == 2
        assert rows[0]["CellsHigh"] == 6

    def test_an_armed_weapon_keeps_its_basic_attack(self, tmp_path):
        rows = gen.item_bases(self.sheet(tmp_path, self.armed()))
        assert rows[0]["BasicShape"] == "Strike"
        assert rows[0]["BasicShapeParams"] == "Radius=1.8; Angle=90; MaxTargets=1"

    def test_an_armed_weapon_with_no_basic_attack_is_refused(self, tmp_path):
        """The exact hole issue #524 reported, moved to where it now lives."""
        with pytest.raises(gen.DataError, match="no basic attack shape"):
            gen.item_bases(self.sheet(
                tmp_path, self.armed(**{"Basic Shape": "",
                                        "Basic Shape Params": ""})))

    def test_a_weapon_granting_no_attack_damage_gets_none_and_needs_none(
            self, tmp_path):
        """The Shield. It is a one-handed weapon that grants no attack damage,
        so there is no hit to compose from it. Issue #619."""
        shield = self.armed(**{"Base Name": "Shield", "Weapon Type": "Shield",
                               "Implicit 1 Stat": "block_chance",
                               "Basic Shape": "", "Basic Shape Params": ""})
        rows = gen.item_bases(self.sheet(tmp_path, shield))
        assert rows[0]["BasicShape"] == ""

    def test_a_weapon_granting_no_attack_damage_may_not_state_one(self, tmp_path):
        with pytest.raises(gen.DataError, match="100% of nothing"):
            gen.item_bases(self.sheet(tmp_path, self.armed(
                **{"Base Name": "Shield", "Weapon Type": "Shield",
                   "Implicit 1 Stat": "block_chance"})))

    def test_something_that_is_not_a_weapon_may_not_state_one(self, tmp_path):
        """A glove can grant flat attack damage -- the Vambraces base grants 12 --
        and still have no swing to describe."""
        with pytest.raises(gen.DataError, match="not a weapon"):
            gen.item_bases(self.sheet(tmp_path, self.armed(
                **{"Base Name": "Vambraces", "Slot": "Gloves", "Hands": "",
                   "Weapon Type": "", "Attack Speed": ""})))

    def test_a_shape_a_swing_cannot_take_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="basic attack shape is 'Summon'"):
            gen.item_bases(self.sheet(
                tmp_path, self.armed(**{"Basic Shape": "Summon"})))

    def test_a_misspelled_parameter_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="no parameter 'Radiuss'"):
            gen.item_bases(self.sheet(tmp_path, self.armed(
                **{"Basic Shape Params": "Radiuss=1.8"})))

    def test_a_rider_is_refused_even_though_every_other_slot_may_carry_one(
            self, tmp_path):
        """A basic attack is 100% weapon damage and nothing else, which is what
        makes it the anchor every other slot is a percentage of. `Burn=1` is
        legal on any weapon skill and is not legal here."""
        with pytest.raises(gen.DataError, match=r"carries \['Burn'\]"):
            gen.item_bases(self.sheet(tmp_path, self.armed(
                **{"Basic Shape Params":
                   "Radius=1.8; Angle=90; MaxTargets=1; Burn=1"})))

    def test_parameters_without_a_shape_are_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="parameters but no shape"):
            gen.item_bases(self.sheet(
                tmp_path, self.armed(**{"Basic Shape": ""})))

    def test_the_two_basic_attack_shape_vocabularies_agree(self):
        """`BASIC_ATTACK_SHAPES` exists in this generator and again in
        `sim/cataclysm_sim/affixes.py`, which enforces the same rule on the
        model. Two copies of a vocabulary drift, so they are compared here the
        same way the two copies of the full shape vocabulary are."""
        from cataclysm_sim import affixes as af

        assert af.BASIC_ATTACK_SHAPES == gen.BASIC_ATTACK_SHAPES, (
            "the basic attack shapes differ between affixes.py and "
            "generate_datatables.py: "
            f"{sorted(af.BASIC_ATTACK_SHAPES ^ gen.BASIC_ATTACK_SHAPES)}")

    def test_the_basic_attack_shapes_are_shapes_the_generator_knows(self):
        """Both names in BASIC_ATTACK_SHAPES appear in SHAPE_PARAMS, so a basic
        attack is validated against a real parameter list rather than against a
        missing dictionary key.

        ONE VOCABULARY IS CHECKED HERE AND THAT IS ENOUGH, because
        test_the_two_basic_attack_shape_vocabularies_agree above proves the copy
        in affixes.py is the same set.

        WHAT THIS DOES NOT CHECK is that a C++ ability template implements the
        shape, which is the stronger property and is not knowable from Python.
        Cataclysm.WeaponSlots.EveryArmedWeaponGrantsABasicAttack asserts it
        against the real table, calling TemplateFor on every armed weapon's
        basic attack shape.
        """
        assert gen.BASIC_ATTACK_SHAPES <= set(gen.SHAPE_PARAMS)


class TestElementVisuals:
    """The eight damage types' effect palette. Issue #549.

    THE SILENT FAILURE HERE IS A COLOUR NOBODY ASKED FOR. `FColor::FromHex` does
    not report bad input, which is how a length-only check in
    ACataclysmTelegraphMarker accepted the word "nonsense" -- eight characters --
    and produced a colour out of it. So the hex is checked character by
    character, and these tests are what say that check works.
    """

    HEADERS = ["Element Tag", "Primary", "Secondary",
               "Emissive Multiplier", "Spawn Rate Scale", "Velocity Scale"]

    def sheet(self, tmp_path, *rows):
        path = workbook_with(tmp_path / "b.xlsx",
                             {"Element Visuals": [self.HEADERS, *rows]})
        return openpyxl.load_workbook(path, data_only=True)

    def test_reads_a_row_and_keys_it_on_the_tags_leaf(self, tmp_path):
        book = self.sheet(tmp_path,
                          ["Element.War", "#FFFFFF", "#000000", 1, 1, 1])
        assert gen.element_visuals(book) == [{
            "Name": "War",
            "ElementTag": "Element.War",
            "PrimaryColour": "(R=1.000000,G=1.000000,B=1.000000,A=1.000000)",
            "SecondaryColour": "(R=0.000000,G=0.000000,B=0.000000,A=1.000000)",
            "EmissiveMultiplier": 1.0,
            "SpawnRateScale": 1.0,
            "VelocityScale": 1.0,
        }]

    def test_the_design_documents_srgb_becomes_linear(self, tmp_path):
        """#FF7A2E is Demonic's primary. Its middle channel is 0x7A, which is
        122/255 = 0.478 in sRGB and 0.195 in linear. Writing the sRGB figure
        into an FLinearColor would render a visibly paler orange."""
        book = self.sheet(tmp_path,
                          ["Element.War", "#FF7A2E", "#000000", 1, 1, 1])
        primary = gen.element_visuals(book)[0]["PrimaryColour"]
        assert primary == "(R=1.000000,G=0.194618,B=0.027321,A=1.000000)"

    def test_a_leading_hash_is_optional(self, tmp_path):
        book = self.sheet(tmp_path,
                          ["Element.War", "FFFFFF", "#000000", 1, 1, 1])
        assert gen.element_visuals(book)[0]["PrimaryColour"].startswith("(R=1.0")

    def test_six_characters_that_are_not_hex_digits_are_refused(self, tmp_path):
        """The real bug this guards. "wrong!" is six characters, so a check on
        the length alone would accept it and produce some colour."""
        book = self.sheet(tmp_path,
                          ["Element.War", "wrong!", "#000000", 1, 1, 1])
        with pytest.raises(gen.DataError, match="not six hex digits"):
            gen.element_visuals(book)

    @pytest.mark.parametrize("text", ["#FFF", "#FFFFFFFF", ""])
    def test_a_hex_of_the_wrong_length_is_refused(self, tmp_path, text):
        book = self.sheet(tmp_path,
                          ["Element.War", text, "#000000", 1, 1, 1])
        with pytest.raises(gen.DataError, match="not six hex digits"):
            gen.element_visuals(book)

    def test_a_key_that_is_not_a_damage_type_tag_is_refused(self, tmp_path):
        book = self.sheet(tmp_path,
                          ["Slot.Ultimate", "#FFFFFF", "#000000", 1, 1, 1])
        with pytest.raises(gen.DataError, match="must start with 'Element.'"):
            gen.element_visuals(book)

    @pytest.mark.parametrize("column", [3, 4, 5])
    @pytest.mark.parametrize("scale", [0, -1])
    def test_a_scale_of_zero_or_less_is_refused(self, tmp_path, column, scale):
        """Each of the three fails as a broken-looking effect rather than as
        bad data: no particles, particles that never move, or a black effect."""
        row = ["Element.War", "#FFFFFF", "#000000", 1, 1, 1]
        row[column] = scale
        book = self.sheet(tmp_path, row)
        with pytest.raises(gen.DataError, match="makes the effect invisible"):
            gen.element_visuals(book)

    def test_an_empty_scale_is_refused(self, tmp_path):
        book = self.sheet(tmp_path,
                          ["Element.War", "#FFFFFF", "#000000", None, 1, 1])
        with pytest.raises(gen.DataError, match="Emissive Multiplier is empty"):
            gen.element_visuals(book)

    def test_a_damage_type_with_no_row_is_reported(self):
        tables = {"ElementVisuals": [{"Name": "War",
                                      "ElementTag": "Element.War"}]}
        problems = gen.validate_element_visuals(
            tables, {"Element.War", "Element.Void", "Slot.Ultimate"})
        assert len(problems) == 1
        assert "Element.Void" in problems[0]

    def test_a_row_naming_an_undeclared_tag_is_reported(self):
        tables = {"ElementVisuals": [{"Name": "Sparkly",
                                      "ElementTag": "Element.Sparkly"}]}
        assert gen.validate_element_visuals(tables, {"Element.Sparkly"}) == []

        problems = gen.validate_element_visuals(tables, {"Element.War"})
        assert len(problems) == 2, problems
        assert any("Element.War" in p and "no effect palette row" in p
                   for p in problems)
        assert any("Element.Sparkly" in p and "not declared" in p
                   for p in problems)

    def test_a_matching_set_reports_nothing(self):
        tables = {"ElementVisuals": [{"Name": "War",
                                      "ElementTag": "Element.War"}]}
        assert gen.validate_element_visuals(
            tables, {"Element.War", "Slot.Ultimate"}) == []


# --------------------------------------------------------------------------
# The three loot sheets, and the word an affix gives an item's name.
#
# EVERY GUARD BELOW IS SHOWN FAILING. A check that cannot fail is worthless, and
# none of these would fail anywhere else: a drop weight of zero, a gate above +10
# or a residue band running backwards all produce a table that loads cleanly and
# carries the wrong numbers.
# --------------------------------------------------------------------------

GEAR_RARITY_HEADER = ["Rarity", "Drop Weight", "Gear Level Gate",
                      "Residue On Drop Lowest", "Residue On Drop Highest",
                      "Colour"]

#: The real ladder, weakest first. Tests copy this and change one cell.
GEAR_RARITY_ROWS = [
    ["Everyday", 15625, 0, 38, 62, "#9D9D9D"],
    ["Quality", 6250, 0, 75, 125, "#FFFFFF"],
    ["Superb", 2500, 0, 112, 188, "#1EFF00"],
    ["Masterful", 1000, 0, 150, 250, "#2E9BFF"],
    ["Legendary", 125, 4, 188, 312, "#FFD100"],
    ["Mythical", 25, 6, 225, 375, "#FF8000"],
    ["Ascendant", 5, 8, 262, 438, "#A335EE"],
    ["Cataclysmic", 1, 10, 300, 500, "#FF4040"],
]


def gear_rarity_book(tmp_path, rows):
    return openpyxl.load_workbook(workbook_with(
        tmp_path / "w.xlsx", {"Gear Rarity": [GEAR_RARITY_HEADER] + rows}))


def changed(rows, index, column, value):
    """A copy of `rows` with one cell replaced."""
    out = [list(row) for row in rows]
    out[index][GEAR_RARITY_HEADER.index(column)] = value
    return out


class TestGearRarity:
    def test_reads_the_ladder_weakest_first(self, tmp_path):
        rows = gen.gear_rarity(gear_rarity_book(tmp_path, GEAR_RARITY_ROWS))
        assert [row["Rarity"] for row in rows] == list(gen.RARITY_LADDER)
        assert rows[-1] == {
            "Name": "Cataclysmic", "Rarity": "Cataclysmic",
            "DropWeight": 1.0, "GearLevelGate": 10,
            "ResidueOnDropLowest": 300.0, "ResidueOnDropHighest": 500.0,
            "Colour": "(R=1.000000,G=0.051269,B=0.051269,A=1.000000)"}

    def test_the_ladder_decides_the_order_not_the_sheet(self, tmp_path):
        """A sheet sorted some other way still generates in ladder order, so
        nothing downstream has to trust how the rows happen to be typed."""
        backwards = list(reversed(GEAR_RARITY_ROWS))
        rows = gen.gear_rarity(gear_rarity_book(tmp_path, backwards))
        assert [row["Rarity"] for row in rows] == list(gen.RARITY_LADDER)

    def test_rejects_a_rarity_that_is_not_on_the_ladder(self, tmp_path):
        rows = changed(GEAR_RARITY_ROWS, 0, "Rarity", "Sparkly")
        with pytest.raises(gen.DataError, match="is not a rarity"):
            gen.gear_rarity(gear_rarity_book(tmp_path, rows))

    def test_rejects_the_same_rarity_twice(self, tmp_path):
        rows = changed(GEAR_RARITY_ROWS, 1, "Rarity", "Everyday")
        with pytest.raises(gen.DataError, match="appears twice"):
            gen.gear_rarity(gear_rarity_book(tmp_path, rows))

    def test_rejects_a_ladder_with_a_rarity_missing(self, tmp_path):
        with pytest.raises(gen.DataError, match="has no row for"):
            gen.gear_rarity(gear_rarity_book(tmp_path, GEAR_RARITY_ROWS[:-1]))

    def test_rejects_a_rarity_that_can_never_drop(self, tmp_path):
        rows = changed(GEAR_RARITY_ROWS, 7, "Drop Weight", 0)
        with pytest.raises(gen.DataError, match="can never drop"):
            gen.gear_rarity(gear_rarity_book(tmp_path, rows))

    def test_rejects_a_gate_above_the_highest_upgrade_level(self, tmp_path):
        rows = changed(GEAR_RARITY_ROWS, 7, "Gear Level Gate", 11)
        with pytest.raises(gen.DataError, match="outside 0 to 10"):
            gen.gear_rarity(gear_rarity_book(tmp_path, rows))

    def test_rejects_a_rarer_thing_that_drops_more_often(self, tmp_path):
        """The one that would look like a working table. Making Cataclysmic
        weigh 2000 keeps every other check happy and inverts the ladder."""
        rows = changed(GEAR_RARITY_ROWS, 7, "Drop Weight", 2000)
        with pytest.raises(gen.DataError, match="drops more often"):
            gen.gear_rarity(gear_rarity_book(tmp_path, rows))

    def test_rejects_a_gate_that_falls_as_rarity_rises(self, tmp_path):
        rows = changed(GEAR_RARITY_ROWS, 7, "Gear Level Gate", 2)
        with pytest.raises(gen.DataError, match="is gated lower"):
            gen.gear_rarity(gear_rarity_book(tmp_path, rows))

    def test_rejects_a_residue_band_that_runs_backwards(self, tmp_path):
        rows = changed(GEAR_RARITY_ROWS, 0, "Residue On Drop Highest", 10)
        with pytest.raises(gen.DataError, match="runs from"):
            gen.gear_rarity(gear_rarity_book(tmp_path, rows))

    def test_rejects_a_drop_carrying_no_residue(self, tmp_path):
        rows = changed(GEAR_RARITY_ROWS, 0, "Residue On Drop Lowest", 0)
        with pytest.raises(gen.DataError, match="Every drop carries some"):
            gen.gear_rarity(gear_rarity_book(tmp_path, rows))

    def test_rejects_a_residue_band_that_shrinks_as_rarity_rises(self, tmp_path):
        """Both ends are checked, so a band that starts higher and ends lower
        than the rarity below it is still caught."""
        rows = changed(GEAR_RARITY_ROWS, 7, "Residue On Drop Highest", 400)
        with pytest.raises(gen.DataError,
                           match="ResidueOnDropHighest is smaller"):
            gen.gear_rarity(gear_rarity_book(tmp_path, rows))


    def test_every_rarity_is_drawn_in_its_own_colour(self, tmp_path):
        """A player reads a rarity off the floor by the colour of its name, so
        two rarities sharing one would be two things that cannot be told apart.
        """
        # QUALITY GIVEN EVERYDAY'S COLOUR. It used to be given white,
        # which collided with Everyday until the two were swapped on
        # 2026-08-19 under issue #711 and white became Quality's own.
        rows = changed(GEAR_RARITY_ROWS, 1, "Colour", "#9D9D9D")
        with pytest.raises(gen.DataError, match="same colour"):
            gen.gear_rarity(gear_rarity_book(tmp_path, rows))

    def test_a_colour_that_is_not_six_hex_digits_is_rejected(self, tmp_path):
        """FColor::FromHex does not report bad input, so a nonsense value would
        become a colour nobody chose. The generator checks every character."""
        rows = changed(GEAR_RARITY_ROWS, 0, "Colour", "cornflower")
        with pytest.raises(gen.DataError, match="six hex digits"):
            gen.gear_rarity(gear_rarity_book(tmp_path, rows))

    def test_a_missing_colour_is_rejected(self, tmp_path):
        rows = changed(GEAR_RARITY_ROWS, 0, "Colour", None)
        with pytest.raises(gen.DataError, match="six hex digits"):
            gen.gear_rarity(gear_rarity_book(tmp_path, rows))

    def test_the_colour_is_converted_from_srgb_to_linear(self, tmp_path):
        """A colour picker shows sRGB and an FLinearColor is linear. Feeding the
        raw figures through renders a visibly different colour, which is why
        this conversion happens at generation rather than in the engine.

        Mid grey is the case that shows it: 0x9D is 157 of 255, which is 0.616
        as a raw fraction and 0.337 once converted.

        EVERYDAY IS THE GREY ONE AND QUALITY THE WHITE ONE, which is the other
        way round from how this test was first written. The two were swapped on
        2026-08-19 under issue #711, because grey below white is what the genre
        does and the reverse read as backwards to the project owner in play.
        """
        rows = gen.gear_rarity(gear_rarity_book(tmp_path, GEAR_RARITY_ROWS))
        grey = next(r for r in rows if r["Rarity"] == "Everyday")
        assert "R=0.337164" in grey["Colour"]
        assert "0.615686" not in grey["Colour"], (
            "the sRGB fraction reached the table unconverted")

        white = next(r for r in rows if r["Rarity"] == "Quality")
        assert white["Colour"] == "(R=1.000000,G=1.000000,B=1.000000,A=1.000000)"


class TestItemSockets:
    HEADER = ["Slot", "Hands", "Max Sockets"]
    ROWS = [["Head", 0, 2], ["Chest", 0, 6],
            ["Weapon", 1, 3], ["Weapon", 2, 6]]

    def book(self, tmp_path, rows):
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "w.xlsx", {"Item Sockets": [self.HEADER] + rows}))

    def test_a_weapon_gets_one_row_per_hand_count(self, tmp_path):
        rows = gen.item_sockets(self.book(tmp_path, self.ROWS))
        assert [row["Name"] for row in rows] == [
            "Head", "Chest", "Weapon_1H", "Weapon_2H"]
        assert rows[3] == {"Name": "Weapon_2H", "Slot": "Weapon",
                           "Hands": 2, "MaxSockets": 6}

    def test_rejects_a_weapon_taking_three_hands(self, tmp_path):
        with pytest.raises(gen.DataError, match="takes 3 hands"):
            gen.item_sockets(self.book(tmp_path, [["Weapon", 3, 4]]))

    def test_rejects_the_same_slot_and_hand_count_twice(self, tmp_path):
        with pytest.raises(gen.DataError, match="appears twice"):
            gen.item_sockets(self.book(
                tmp_path, [["Head", 0, 2], ["Head", 0, 3]]))

    def test_rejects_a_slot_that_holds_no_gem(self, tmp_path):
        with pytest.raises(gen.DataError, match="no gem could ever go in one"):
            gen.item_sockets(self.book(tmp_path, [["Head", 0, 0]]))


class TestAffixTiers:
    HEADER = ["Tier", "Drop Weight"]
    ROWS = [[1, 64], [2, 32], [3, 16]]

    def book(self, tmp_path, rows):
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "w.xlsx", {"Affix Tiers": [self.HEADER] + rows}))

    def test_reads_a_tier_per_row(self, tmp_path):
        rows = gen.affix_tiers(self.book(tmp_path, self.ROWS))
        assert rows == [{"Name": "T1", "Tier": 1, "DropWeight": 64.0},
                        {"Name": "T2", "Tier": 2, "DropWeight": 32.0},
                        {"Name": "T3", "Tier": 3, "DropWeight": 16.0}]

    def test_rejects_a_gap_in_the_tiers(self, tmp_path):
        """A drop draws from every tier at or below its cap, so a missing tier
        is one the draw has no weight for rather than one that never rolls."""
        with pytest.raises(gen.DataError, match="every tier from 1 upward"):
            gen.affix_tiers(self.book(tmp_path, [[1, 64], [3, 16]]))

    def test_rejects_tiers_that_do_not_start_at_one(self, tmp_path):
        with pytest.raises(gen.DataError, match="every tier from 1 upward"):
            gen.affix_tiers(self.book(tmp_path, [[2, 32], [3, 16]]))

    def test_rejects_a_tier_that_can_never_roll(self, tmp_path):
        with pytest.raises(gen.DataError, match="can never roll"):
            gen.affix_tiers(self.book(tmp_path, [[1, 64], [2, 0]]))

    def test_rejects_a_higher_tier_that_rolls_more_often(self, tmp_path):
        with pytest.raises(gen.DataError, match="rolls more often"):
            gen.affix_tiers(self.book(tmp_path, [[1, 64], [2, 128]]))


class TestTheWordAnAffixGivesAnItemsName:
    """An item is called `<rarity> <base> of <word>`, so only a suffix has one.

    None of these would fail anywhere else. A suffix with no word leaves an item
    that rolled it with nothing to be named after; a word on a prefix can never
    be read, because the first word of the name is the rarity.
    """

    # EVERY COLUMN THE READER ASKS FOR, the ones this test leaves empty
    # appended at the end so no row changes. Issue #1882: a column a
    # sheet lacks is now refused rather than read as empty.
    HEADER = ["Affix Name", "Affix Kind", "Position", "Stat", "Value Kind",
              "Top Value", "Breadth", "Ailment", "Gem", "Hybrid Part 1",
              "Hybrid Part 2", "Allowed Slots", "Name Word", "Floor",
              "Percent"]

    def row(self, name, position, word):
        return [name, "Stat", position, "max_health", "flat", 100, None,
                None, None, None, None, "Chest", word]

    def book(self, tmp_path, rows):
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "w.xlsx", {"Affixes": [self.HEADER] + rows}))

    def test_a_suffix_carries_its_word_into_the_table(self, tmp_path):
        rows = gen.affixes(self.book(tmp_path, [
            self.row("Flat life leech", "suffix", "the Leech"),
            self.row("Flat maximum health", "prefix", None)]))
        assert [row["NameWord"] for row in rows] == ["the Leech", ""]

    def test_rejects_a_suffix_with_no_word(self, tmp_path):
        with pytest.raises(gen.DataError, match="has no name word"):
            gen.affixes(self.book(
                tmp_path, [self.row("Flat life leech", "suffix", None)]))

    def test_rejects_a_prefix_that_carries_one(self, tmp_path):
        with pytest.raises(gen.DataError, match="could never appear"):
            gen.affixes(self.book(
                tmp_path, [self.row("Flat maximum health", "prefix", "Vigour")]))

    def test_rejects_two_affixes_sharing_a_word(self, tmp_path):
        """A player reading "of Warding" should find one thing."""
        with pytest.raises(gen.DataError, match="is carried by both"):
            gen.affixes(self.book(tmp_path, [
                self.row("Single resistance", "suffix", "Warding"),
                self.row("Two resistances", "suffix", "Warding")]))


class TestSocketMaximaCoverEverySlotThatIsWorn:
    """The Item Sockets and Item Bases sheets have to describe the same slots.

    A slot missing from Item Sockets leaves a drop with nothing to say how many
    sockets the piece can hold. A slot in Item Sockets that no base occupies is a
    maximum nothing will ever read. Neither is reported anywhere else.
    """

    def tables(self, bases, sockets):
        return {"ItemBases": [{"Name": f"b{i}", "Slot": slot, "Hands": hands}
                              for i, (slot, hands) in enumerate(bases)],
                "ItemSockets": [{"Name": f"s{i}", "Slot": slot, "Hands": hands}
                                for i, (slot, hands) in enumerate(sockets)]}

    def test_a_matching_set_reports_nothing(self):
        pairs = [("Head", 0), ("Weapon", 1), ("Weapon", 2)]
        assert gen.validate_socket_slots(self.tables(pairs, pairs)) == []

    def test_a_worn_slot_with_no_maximum_is_reported(self):
        problems = gen.validate_socket_slots(
            self.tables([("Head", 0), ("Relic", 0)], [("Head", 0)]))
        assert len(problems) == 1, problems
        assert "Relic" in problems[0] and "which item bases occupy" in problems[0]

    def test_a_maximum_for_a_slot_nobody_wears_is_reported(self):
        problems = gen.validate_socket_slots(
            self.tables([("Head", 0)], [("Head", 0), ("Wings", 0)]))
        assert len(problems) == 1, problems
        assert "Wings" in problems[0] and "no item base occupies" in problems[0]

    def test_the_two_hand_counts_of_a_weapon_are_matched_separately(self):
        """A one-handed maximum does not stand in for a two-handed one, which is
        the whole reason a weapon has two rows."""
        problems = gen.validate_socket_slots(
            self.tables([("Weapon", 1), ("Weapon", 2)], [("Weapon", 1)]))
        assert len(problems) == 1, problems
        assert "at 2 hand(s)" in problems[0]


class TestEnemyDropsAndTheRarityLadderStayInStep:
    """The Enemy Drops sheet and the enemy rarity ladder come from different
    places, and neither is checked against the other anywhere else.

    `EnemyRarities.csv` is generated from `sim/cataclysm_sim/enemy_stats.py`,
    whose ladder is a port of the external DungeonSimulator power model.
    `EnemyDrops.csv` is typed into the workbook. A rarity in one and not the
    other is a creature that drops nothing, or a drop rate for a creature that
    cannot exist.
    """

    def tables(self, dropping, known):
        return {
            "EnemyDrops": [{"Name": name, "Step": step}
                           for name, step in dropping],
            "EnemyRarities": [{"Name": name, "Step": step}
                              for name, step in known],
        }

    def test_a_matching_ladder_reports_nothing(self):
        both = [("Common", 0), ("Elite", 1), ("Boss", 2)]
        assert gen.validate_enemy_drop_rarities(self.tables(both, both)) == []

    def test_a_rarity_that_drops_nothing_is_reported(self):
        problems = gen.validate_enemy_drop_rarities(self.tables(
            [("Common", 0), ("Elite", 1)],
            [("Common", 0), ("Elite", 1), ("Boss", 2)]))
        assert len(problems) == 1, problems
        assert "Boss" in problems[0]

    def test_a_drop_rate_for_a_creature_that_cannot_exist_is_reported(self):
        problems = gen.validate_enemy_drop_rarities(self.tables(
            [("Common", 0), ("Elite", 1), ("Sparkly", 2)],
            [("Common", 0), ("Elite", 1)]))
        assert len(problems) == 1, problems
        assert "Sparkly" in problems[0]

    def test_the_two_have_to_agree_on_the_order_as_well(self):
        """Both tables carry a Step saying where a rarity sits, so membership
        alone is not enough."""
        problems = gen.validate_enemy_drop_rarities(self.tables(
            [("Elite", 1), ("Common", 0)],
            [("Common", 0), ("Elite", 1)]))
        assert len(problems) == 1, problems
        assert "same order" in problems[0]

    def test_a_step_that_disagrees_is_reported(self):
        problems = gen.validate_enemy_drop_rarities(self.tables(
            [("Common", 0), ("Elite", 4)],
            [("Common", 0), ("Elite", 1)]))
        assert len(problems) == 1, problems
        assert "Step 4" in problems[0]


class TestMaterialTierCountsMatchTheMaterials:
    """How often a NAMED top-tier material drops is the tier's share divided by
    how many share it. Purified Essence is one of three in the top tier and is
    the only thing that clears the Consumption Threshold, so a wrong count makes
    that figure wrong and nothing else notices.
    """

    def tables(self, stated, materials):
        return {
            "MaterialTiers": [{"Name": f"T{i}", "TierName": name,
                               "Materials": count}
                              for i, (name, count) in enumerate(stated, 1)],
            "CraftingMaterials": [
                {"Name": f"m{i}", "TierAndSource": f"Tier {tier} ({name}). x"}
                for i, (tier, name) in enumerate(materials)],
        }

    def test_a_matching_set_reports_nothing(self):
        assert gen.validate_material_tier_counts(self.tables(
            [("Common", 2), ("Rare", 1)],
            [(1, "Common"), (1, "Common"), (3, "Rare")])) == []

    def test_a_wrong_count_is_reported(self):
        problems = gen.validate_material_tier_counts(self.tables(
            [("Common", 4)],
            [(1, "Common"), (1, "Common")]))
        assert len(problems) == 1, problems
        assert "says 4" in problems[0] and "has 2" in problems[0]

    def test_a_tier_no_material_is_in_is_reported(self):
        problems = gen.validate_material_tier_counts(self.tables(
            [("Common", 1), ("Mythic", 1)], [(1, "Common")]))
        assert any("Mythic" in p for p in problems), problems

    def test_a_material_in_a_tier_with_no_row_is_reported(self):
        problems = gen.validate_material_tier_counts(self.tables(
            [("Common", 1)], [(1, "Common"), (9, "Impossible")]))
        assert any("Impossible" in p for p in problems), problems

    def test_unreadable_tier_cells_are_reported_rather_than_passing(self):
        """A parser that matched nothing would make every check above vacuous,
        so it says so instead of reporting no problems."""
        problems = gen.validate_material_tier_counts({
            "MaterialTiers": [{"Name": "T1", "TierName": "Common",
                               "Materials": 1}],
            "CraftingMaterials": [{"Name": "m", "TierAndSource": "who knows"}],
        })
        assert len(problems) == 1, problems
        assert "could not be checked at all" in problems[0]


class TestAPassiveNodeCanGrantSeveralStats:
    """ISSUE #953. A node had exactly one effect row, because the DataTable's row
    name WAS the node name.

    THAT SHAPE CANNOT EXPRESS SEVERAL REAL NODES. "Pain Tolerance" in the
    Masochist tree says "+1% increased Maximum Health and +0.5% increased Armor",
    which is two stats, and the Masochist's starting node grants three Fervour
    rates at once. The node moved into a column of its own and the row name
    became the node with `#1`, `#2` and so on after it.

    WHAT IS STILL REFUSED. The same node granting the same STAT twice, which is
    what copying a spreadsheet row and forgetting to change the stat produces:
    both would apply and the node would be worth double what its own description
    says.
    """

    @staticmethod
    def sheet(rows: list[list]) -> list[list]:
        # EVERY COLUMN THE READER ASKS FOR, the ones this test leaves empty
        # appended at the end so no row changes. Issue #1882: a column a
        # sheet lacks is now refused rather than read as empty.
        return [["Node", "Stat", "Value Kind", "Value Per Point",
                 "Required Tags", "Condition", "Condition Value", "Scale",
                 "Scale Step", "Option", "Reach Metres", "Min Points"]] + rows

    def book(self, tmp_path, rows: list[list]):
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "effects.xlsx",
            {"Passive Effects": self.sheet(rows)}))

    def test_two_stats_on_one_node_both_survive(self, tmp_path):
        rows = self.book(tmp_path, [
            ["Masochist_basic_fc_stem1", "max_health", "increased", 1, None],
            ["Masochist_basic_fc_stem1", "armor", "increased", 0.5, None],
        ])
        out = gen.passive_effects(rows)

        assert [r["Stat"] for r in out] == ["max_health", "armor"]
        assert all(r["Node"] == "Masochist_basic_fc_stem1" for r in out)

    def test_the_row_names_are_the_node_and_an_index(self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "max_health", "increased", 1, None],
            ["A_node", "armor", "increased", 2, None],
            ["B_node", "armor", "increased", 3, None],
        ])
        out = gen.passive_effects(rows)

        assert [r["Name"] for r in out] == ["A_node#1", "A_node#2", "B_node#1"]

    def test_the_same_stat_twice_on_one_node_is_refused(self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "armor", "increased", 3, None],
            ["A_node", "armor", "increased", 4, None],
        ])
        with pytest.raises(gen.DataError, match="same stat twice"):
            gen.passive_effects(rows)

    def test_a_stack_seconds_column_on_this_sheet_is_refused(self, tmp_path):
        """Issue #1833. Only an enchantment's row counts stacks of its own."""
        book = openpyxl.load_workbook(workbook_with(
            tmp_path / "effects.xlsx",
            {"Passive Effects": [["Node", "Stat", "Value Kind", "Value Per Point",
                                  "Required Tags", "Stack Seconds"],
                                 ["A_node", "armor", "increased", 3, None, None]]}))
        with pytest.raises(gen.DataError, match="Stack Seconds"):
            gen.passive_effects(book)

    def test_an_offset_column_on_this_sheet_is_refused(self, tmp_path):
        """Issue #1686. Only an enchantment's modifier reads an offset."""
        book = openpyxl.load_workbook(workbook_with(
            tmp_path / "effects.xlsx",
            {"Passive Effects": [["Node", "Stat", "Value Kind", "Value Per Point",
                                  "Required Tags", "Scale Offset"],
                                 ["A_node", "armor", "increased", 3, None, None]]}))
        with pytest.raises(gen.DataError, match="Scale Offset"):
            gen.passive_effects(book)

    def test_a_cap_column_on_this_sheet_is_refused(self, tmp_path):
        """Issue #1815. Only an enchantment's modifier reads a cap, so a cap
        written on this sheet would be dropped with no error."""
        book = openpyxl.load_workbook(workbook_with(
            tmp_path / "effects.xlsx",
            {"Passive Effects": [["Node", "Stat", "Value Kind", "Value Per Point",
                                  "Required Tags", "Scale Max Steps"],
                                 ["A_node", "armor", "increased", 3, None, None]]}))
        with pytest.raises(gen.DataError, match="Scale Max Steps"):
            gen.passive_effects(book)

    def test_a_node_name_containing_the_separator_is_refused(self, tmp_path):
        """Otherwise a row name would be ambiguous about where the node ends."""
        rows = self.book(tmp_path, [["A#node", "armor", "increased", 3, None]])
        with pytest.raises(gen.DataError, match="number sign"):
            gen.passive_effects(rows)

    def test_a_bad_value_kind_is_still_refused(self, tmp_path):
        rows = self.book(tmp_path, [["A_node", "armor", "sideways", 3, None]])
        with pytest.raises(gen.DataError,
                           match="not flat, increased, more or removed"):
            gen.passive_effects(rows)

    def test_a_removal_is_a_kind_a_node_may_carry(self, tmp_path):
        """The sheets share one vocabulary of kinds. Issue #1791: no node removes
        a stat yet, and the reader accepts one the way the enchantment reader
        does, so the two cannot come to disagree."""
        rows = self.book(tmp_path, [["A_node", "armor", "removed", 1, None]])
        out = gen.passive_effects(rows)

        assert [(r["Stat"], r["ValueKind"]) for r in out] == [("armor", "removed")]

    def test_the_validator_checks_the_node_column_and_not_the_row_name(self):
        """The row name carries a `#1` and no node is called that, so a validator
        still reading the row name would report every row as unreachable."""
        tables = {
            "PassiveEffects": [{"Name": "Real_node#1", "Node": "Real_node",
                                "Stat": "armor", "ValueKind": "increased",
                                "RequiredTags": ""}],
            "PassiveNodes": [{"Name": "Real_node"}],
            "ClassStats": [{"Stat": "armor"}],
        }
        assert gen.validate_passive_effects(tables, set()) == []

    def test_a_node_that_does_not_exist_is_still_reported(self):
        tables = {
            "PassiveEffects": [{"Name": "Ghost#1", "Node": "Ghost",
                                "Stat": "armor", "ValueKind": "increased",
                                "RequiredTags": ""}],
            "PassiveNodes": [{"Name": "Real_node"}],
            "ClassStats": [{"Stat": "armor"}],
        }
        problems = gen.validate_passive_effects(tables, set())
        assert len(problems) == 1, problems
        assert "no passive node is called Ghost" in problems[0]

    def test_a_removal_is_not_asked_for_a_base(self):
        """The rule `validate_enchantment_effects` has, on this sheet too, because
        the two share one vocabulary of kinds. Issue #1791. The same row as an
        increase is still reported, which is the control."""
        removal = {"Name": "Real_node#1", "Node": "Real_node",
                   "Stat": "resistance_war", "ValueKind": "removed",
                   "RequiredTags": ""}
        tables = {"PassiveEffects": [removal],
                  "PassiveNodes": [{"Name": "Real_node"}],
                  "ClassStats": [{"Stat": "armor"}]}
        assert gen.validate_passive_effects(tables, set()) == []

        tables["PassiveEffects"] = [dict(removal, ValueKind="increased")]
        problems = gen.validate_passive_effects(tables, set())
        assert len(problems) == 1 and "'resistance_war' is not a stat" in problems[0]


class TestARowCountingNearbyEnemiesCarriesItsOwnRadius:
    """ISSUE #1597. Two readings count the enemies standing near a character: the
    condition `enemies_in_reach_at_least` and the scale `enemies_in_reach`. Both
    need a radius, and neither neighbouring column can hold it -- `Condition
    Value` holds the COUNT asked for, `Scale Step` holds how many enemies one
    step is worth -- so the radius has a column of its own, `Reach Metres`.

    THREE RULES, AND EACH FAILS IN THE DIRECTION THAT IS VISIBLE.

    A row naming either reading MUST state a radius, because a modifier with no
    radius counts nobody: the node would import cleanly, grant nothing, and say
    so nowhere.

    A row naming neither MUST NOT state one, because nothing would read it. A
    number in a column the game never looks at is a statement somebody believes
    is doing something.

    A radius must be above nothing and no larger than 100 metres, which is the
    bound `target_within_metres` uses and for the same reason.

    THIS CLASS REPLACED ONE COVERING A TEMPORARY REFUSAL. Between the engine
    learning the two names and the sheet gaining the column, a row using either
    was refused outright, because it could only have counted nobody.
    """

    @staticmethod
    def sheet(rows: list[list]) -> list[list]:
        # EVERY COLUMN THE READER ASKS FOR, the ones this test leaves empty
        # appended at the end so no row changes. Issue #1882: a column a
        # sheet lacks is now refused rather than read as empty.
        return [["Node", "Stat", "Value Kind", "Value Per Point",
                 "Condition", "Condition Value", "Scale", "Scale Step",
                 "Reach Metres", "Required Tags", "Option", "Min Points"]] + rows

    def book(self, tmp_path, rows: list[list]):
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "reach.xlsx", {"Passive Effects": self.sheet(rows)}))

    # THE FOUR CHECKS THE SHEET SHARES WITH THE ENCHANTMENT SHEETS, on the
    # passive path. Issue #1593 moved `passive_effects` onto
    # `_condition_and_scale`; until then no test drove these four refusals
    # through this function, so the move could have dropped one in silence.

    def test_an_unknown_condition_is_refused_on_a_passive_row(self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "armor", "increased", 3,
             "while_dancing", 3, None, None, None],
        ])
        with pytest.raises(gen.DataError, match="Passive Effects row .*cannot judge"):
            gen.passive_effects(rows)

    def test_a_value_beside_a_condition_that_compares_nothing_is_refused_on_a_passive_row(
            self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "armor", "increased", 3,
             "while_bleeding", 5, None, None, None],
        ])
        with pytest.raises(gen.DataError, match="compares nothing"):
            gen.passive_effects(rows)

    def test_a_condition_value_outside_its_range_is_refused_on_a_passive_row(
            self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "armor", "increased", 3,
             "health_at_or_below", 500, None, None, None],
        ])
        with pytest.raises(gen.DataError, match="between"):
            gen.passive_effects(rows)

    def test_an_unknown_scale_is_refused_on_a_passive_row(self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "armor", "increased", 3,
             None, None, "moons_held", 1, None],
        ])
        with pytest.raises(gen.DataError, match="Passive Effects row .*cannot judge"):
            gen.passive_effects(rows)

    def test_a_scale_step_of_nothing_is_refused_on_a_passive_row(self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "armor", "increased", 3,
             None, None, "minions_held", 0, None],
        ])
        with pytest.raises(gen.DataError, match="step of nothing"):
            gen.passive_effects(rows)

    def test_a_condition_row_carries_its_radius_to_the_output(self, tmp_path):
        rows = self.book(tmp_path, [
            ["Ravager_basic_spine_003", "attack_damage", "increased", 2,
             "enemies_in_reach_at_least", 1, None, None, 4],
        ])
        out = gen.passive_effects(rows)

        assert len(out) == 1
        assert out[0]["Condition"] == "enemies_in_reach_at_least"
        assert out[0]["ConditionValue"] == 1.0
        assert out[0]["ReachMetres"] == 4.0

    def test_a_scale_row_carries_its_radius_to_the_output(self, tmp_path):
        rows = self.book(tmp_path, [
            ["Ravager_keystone_d_kB", "attack_damage", "more", 2,
             None, None, "enemies_in_reach", 1, 4],
        ])
        out = gen.passive_effects(rows)

        assert out[0]["Scale"] == "enemies_in_reach"
        assert out[0]["ScaleStep"] == 1.0
        assert out[0]["ReachMetres"] == 4.0

    def test_a_row_that_counts_nobody_says_so_rather_than_importing(self, tmp_path):
        """The radius is missing, so the row would grant nothing silently."""
        rows = self.book(tmp_path, [
            ["A_node", "attack_damage", "increased", 2,
             "enemies_in_reach_at_least", 1, None, None, None],
        ])
        with pytest.raises(gen.DataError, match="counts nobody"):
            gen.passive_effects(rows)

    def test_a_scale_row_with_no_radius_is_refused_too(self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "attack_damage", "more", 2,
             None, None, "enemies_in_reach", 1, None],
        ])
        with pytest.raises(gen.DataError, match="counts nobody"):
            gen.passive_effects(rows)

    def test_a_radius_on_a_row_that_reads_none_is_refused(self, tmp_path):
        """Nothing would read it, so it is a number that looks as though it works."""
        rows = self.book(tmp_path, [
            ["A_node", "armor", "increased", 3,
             "health_at_or_below", 20, None, None, 4],
        ])
        with pytest.raises(gen.DataError, match="Nothing would read it"):
            gen.passive_effects(rows)

    def test_a_radius_of_nothing_is_refused(self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "attack_damage", "increased", 2,
             "enemies_in_reach_at_least", 1, None, None, 0],
        ])
        with pytest.raises(gen.DataError, match="above 0"):
            gen.passive_effects(rows)

    def test_a_radius_past_a_hundred_metres_is_refused(self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "attack_damage", "increased", 2,
             "enemies_in_reach_at_least", 1, None, None, 140],
        ])
        with pytest.raises(gen.DataError, match="up to 100"):
            gen.passive_effects(rows)

    def test_a_row_using_neither_reading_still_imports_with_no_radius(self, tmp_path):
        """The control. Without it, a refusal firing on every row would pass the
        five refusal tests above and nothing here would say so."""
        rows = self.book(tmp_path, [
            ["A_node", "armor", "increased", 3,
             "health_at_or_below", 20, "minions_held", 1, None],
        ])
        out = gen.passive_effects(rows)

        assert len(out) == 1
        assert out[0]["Condition"] == "health_at_or_below"
        assert out[0]["Scale"] == "minions_held"
        assert out[0]["ReachMetres"] == -1.0


class TestABonusCanGrowWithDamageReductionOrMaximumMana:
    """ISSUE #1515. Two scales read a stat the character has rather than a
    state it is in: `damage_reduction`, for Weight Against Them's "+1% increased
    Attack Damage for every 2% of Damage Reduction you have", and `max_mana`,
    for Drawn Deep's "for every full 200 maximum mana you have".

    EACH STEP HAS AN UPPER BOUND, AND A ROW PAST IT IS REFUSED. The damage
    reduction reading stops at the 75% cap, so a step above 75 could never
    pay; a maximum mana step above 1,000 is a judgement recorded beside the
    bound in `tools/generate_datatables.py`.

    NO ROW ON THE SHEET USES EITHER YET. The two rows wait on a turn of the
    design workbook, so these tests are what holds the names and bounds until
    then.
    """

    def book(self, tmp_path, rows: list[list]):
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "stat_scales.xlsx",
            {"Passive Effects":
             TestARowCountingNearbyEnemiesCarriesItsOwnRadius.sheet(rows)}))

    def test_a_damage_reduction_scale_imports_with_its_step(self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "attack_damage", "increased", 1,
             None, None, "damage_reduction", 2, None],
        ])
        out = gen.passive_effects(rows)

        assert len(out) == 1
        assert out[0]["Scale"] == "damage_reduction"
        assert out[0]["ScaleStep"] == 2.0

    def test_a_damage_reduction_step_at_the_cap_imports(self, tmp_path):
        """The control for the refusal below: 75 is inside the bound."""
        rows = self.book(tmp_path, [
            ["A_node", "attack_damage", "increased", 1,
             None, None, "damage_reduction", 75, None],
        ])
        assert gen.passive_effects(rows)[0]["ScaleStep"] == 75.0

    def test_a_damage_reduction_step_past_the_cap_is_refused(self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "attack_damage", "increased", 1,
             None, None, "damage_reduction", 76, None],
        ])
        with pytest.raises(gen.DataError, match="up to 75"):
            gen.passive_effects(rows)

    def test_a_maximum_mana_scale_imports_with_its_step(self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "spell_damage", "increased", 1,
             None, None, "max_mana", 200, None],
        ])
        out = gen.passive_effects(rows)

        assert len(out) == 1
        assert out[0]["Scale"] == "max_mana"
        assert out[0]["ScaleStep"] == 200.0

    def test_a_maximum_mana_step_past_a_thousand_is_refused(self, tmp_path):
        rows = self.book(tmp_path, [
            ["A_node", "spell_damage", "increased", 1,
             None, None, "max_mana", 1001, None],
        ])
        with pytest.raises(gen.DataError, match="up to 1000"):
            gen.passive_effects(rows)


class TestScaleStepHigh:
    """The step that rolls with the value. Issue #1833, the kill counter:
    "for every 100,000-500,000 kills" states the step as a range, and a row
    writes it as Scale Step and Scale Step High so the item's roll picks one
    step where it picks the value."""

    WEAPON = "Positive_This_weapon_has_5_20_more_damage_for_every_100"
    WEAPON_WORDS = "This weapon has 5-20% more damage for every 100,000-500,000 kills"
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [WEAPON_WORDS, "Generic", 2, "Item.Slot.Weapon", None,
         "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life"],
    ]
    HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
              "Value High", "Required Tags", "Condition", "Condition Value",
              "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
              "Scale Max Steps", "Stack Seconds", "Scale Offset",
              "Every Seconds", "Every Nth", "Scale Step High",
              "Stack Seconds High", "Condition 2", "Condition Value 2",
              "Condition Value High", "Trigger Cooldown", "Event Value",
              "Ailment", "Damage Share"]

    def book(self, tmp_path, changes):
        values = {"Enchantment": self.WEAPON, "Effect": self.WEAPON_WORDS,
                  "Stat": "attack_damage", "Value Kind": "more",
                  "Value Low": 5, "Value High": 20, "Scale": "weapon_kills",
                  "Scale Step": 100000, "Scale Step High": 500000}
        values.update(changes)
        row = [values.get(column) for column in self.HEADER]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "scale_step_high.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER, row]}))

    def test_a_stated_step_range_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, {}))
        assert (out[0]["Scale"], out[0]["ScaleStep"], out[0]["ScaleStepHigh"]) == (
            "weapon_kills", 100000.0, 500000.0)

    def test_a_step_range_the_words_do_not_state_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="do not state as a range"):
            gen.enchantment_effects(self.book(tmp_path, {"Scale Step High": 400000}))

    def test_a_high_end_not_above_the_step_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="The high end is"):
            gen.enchantment_effects(self.book(tmp_path, {"Scale Step": 500000,
                                                         "Scale Step High": 100000}))

    def test_a_high_end_with_no_scale_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="no scale"):
            gen.enchantment_effects(self.book(tmp_path, {"Scale": None, "Scale Step": None}))


class TestStackSecondsHigh:
    """How long stacks last, rolling with the value. Issue #1833, ruled
    2026-09-30 under the owner's delegation, following Scale Step High:
    "Killing an enemy triggers a 1-2 second global cooldown on all your
    skills" states the time as a range, and a row writes it as Stack Seconds
    and Stack Seconds High so the item's roll picks one time where it picks
    the value."""

    LOCK = "Negative_Killing_an_enemy_triggers_a_1_2_second_global_co"
    LOCK_WORDS = "Killing an enemy triggers a 1-2 second global cooldown on all your skills"
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        ["Double your energy shield", "Generic", 1, "Stat.Defense.EnergyShield",
         None, LOCK_WORDS, "Generic", 3, "Stat.Utility.Cooldown"],
    ]
    HEADER = TestScaleStepHigh.HEADER + ["Stack Seconds High"]

    def book(self, tmp_path, changes):
        values = {"Enchantment": self.LOCK, "Effect": self.LOCK_WORDS,
                  "Stat": "skill_locked", "Value Kind": "flat",
                  "Value Low": 1, "Value High": 1, "Scale": "own_stacks",
                  "Scale Step": 1, "Action Event": "kill",
                  "Scale Max Steps": 1, "Stack Seconds": 1,
                  "Stack Seconds High": 2}
        values.update(changes)
        row = [values.get(column) for column in self.HEADER]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "stack_seconds_high.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER, row]}))

    def test_a_stated_time_range_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, {}))
        assert (out[0]["StackSeconds"], out[0]["StackSecondsHigh"]) == (1.0, 2.0)

    def test_one_stated_time_carries_no_high_end(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, {"Stack Seconds High": None}))
        assert (out[0]["StackSeconds"], out[0]["StackSecondsHigh"]) == (1.0, 0.0)

    def test_a_time_range_the_words_do_not_state_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="do not state as a range"):
            gen.enchantment_effects(self.book(tmp_path, {"Stack Seconds High": 3}))

    def test_a_high_end_below_the_low_end_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="The high end is above Stack Seconds"):
            gen.enchantment_effects(self.book(tmp_path, {"Stack Seconds": 2,
                                                         "Stack Seconds High": 1}))

    def test_a_high_end_equal_to_the_low_end_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="The high end is above Stack Seconds"):
            gen.enchantment_effects(self.book(tmp_path, {"Stack Seconds High": 1}))

    def test_a_high_end_past_the_bound_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="The high end is above Stack Seconds"):
            gen.enchantment_effects(self.book(tmp_path, {
                "Stack Seconds High": gen.MAX_STACK_SECONDS + 1}))

    def test_a_high_end_with_no_stack_seconds_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="no Stack Seconds, so it would be dropped"):
            gen.enchantment_effects(self.book(tmp_path, {
                "Scale": None, "Scale Step": None, "Action Event": None,
                "Scale Max Steps": None, "Stack Seconds": None}))


class TestSecondConditionAndThresholdHigh:
    """A second condition, and a threshold that rolls. Issue #1833 group C part
    3a, ruled 2026-09-30 under the owner's delegation. "You take 20%-35%
    increased damage from melee attacks while your HP is above 75%" names two
    states, and both must hold; "Your abilities are free when above 80%-95% hp"
    states its threshold as a range, which rolls with the value."""

    FREE = "Positive_Your_abilities_are_free_when_above_80_95_hp"
    FREE_WORDS = "Your abilities are free when above 80%-95% hp"
    MELEE = "Negative_You_take_20_35_increased_damage_from_melee_att"
    MELEE_WORDS = ("You take 20%-35% increased damage from melee attacks while "
                   "your HP is above 75%")
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [FREE_WORDS, "Generic", 3, "Stat.Utility.Mana", None,
         MELEE_WORDS, "Generic", 3, "Stat.Defense.Life"],
    ]
    HEADER = TestScaleStepHigh.HEADER

    def book(self, tmp_path, values):
        row = [values.get(column) for column in self.HEADER]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "second_condition.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER, row]}))

    def melee(self, tmp_path, changes):
        values = {"Enchantment": self.MELEE, "Effect": self.MELEE_WORDS,
                  "Stat": "damage_taken", "Value Kind": "increased",
                  "Value Low": 20, "Value High": 35,
                  "Condition": "hit_is_melee_attack",
                  "Condition 2": "health_above", "Condition Value 2": 75}
        values.update(changes)
        return self.book(tmp_path, values)

    def free(self, tmp_path, changes):
        values = {"Enchantment": self.FREE, "Effect": self.FREE_WORDS,
                  "Stat": "mana_cost", "Value Kind": "more", "Value Low": -100,
                  "Condition": "health_above", "Condition Value": 80,
                  "Condition Value High": 95}
        values.update(changes)
        return self.book(tmp_path, values)

    def test_a_second_condition_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.melee(tmp_path, {}))
        assert (out[0]["Condition"], out[0]["Condition2"], out[0]["ConditionValue2"]) == (
            "hit_is_melee_attack", "health_above", 75.0)

    def test_a_second_condition_with_no_first_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="and no Condition"):
            gen.enchantment_effects(self.melee(tmp_path, {"Condition": None}))

    def test_an_unknown_second_condition_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="which the game cannot judge"):
            gen.enchantment_effects(self.melee(tmp_path, {"Condition 2": "while_juggling"}))

    def test_the_same_condition_twice_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="as both conditions"):
            gen.enchantment_effects(self.melee(tmp_path, {
                "Condition": "health_above", "Condition Value": 50}))

    def test_a_second_value_out_of_bounds_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="second condition value of 150"):
            gen.enchantment_effects(self.melee(tmp_path, {"Condition Value 2": 150}))

    def test_a_second_value_with_no_second_condition_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="no Condition 2, so it would be dropped"):
            gen.enchantment_effects(self.melee(tmp_path, {"Condition 2": None}))

    def test_a_stated_threshold_range_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.free(tmp_path, {}))
        assert (out[0]["ConditionValue"], out[0]["ConditionValueHigh"]) == (80.0, 95.0)

    def test_a_threshold_range_the_words_do_not_state_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="do not state as a range"):
            gen.enchantment_effects(self.free(tmp_path, {"Condition Value High": 90}))

    def test_a_threshold_high_end_not_above_the_low_end_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="The high end is above Condition Value"):
            gen.enchantment_effects(self.free(tmp_path, {"Condition Value": 95,
                                                         "Condition Value High": 80}))

    def test_a_threshold_high_end_with_no_condition_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="no condition that compares a value"):
            gen.enchantment_effects(self.free(tmp_path, {"Condition": None,
                                                         "Condition Value": None}))


class TestTriggerCooldownAndRandomDot:
    """An action row's trigger cooldown, and the random damage over time.
    Issue #1833 group D, ruled 2026-09-30 under the owner's delegation: an
    action row that makes something happen on a hit-fired event waits 0.25 s
    after it fires unless its Trigger Cooldown says otherwise, and "Critical
    strikes apply a random DoT to the target" applies one of five."""

    CRIT = gen.row_name("Positive", "Critical strikes apply a random DoT to the target"[:48])
    CRIT_WORDS = "Critical strikes apply a random DoT to the target"
    BLOCK = gen.row_name("Positive", "Blocking an attack restores 3%-6% of your maximum HP"[:48])
    BLOCK_WORDS = "Blocking an attack restores 3%-6% of your maximum HP"
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [CRIT_WORDS, "Generic", 4, "Trigger.OnCrit", None,
         "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life"],
        [BLOCK_WORDS, "Generic", 4, "Stat.Defense.Block", None,
         None, None, None, None],
    ]
    HEADER = TestScaleStepHigh.HEADER

    def book(self, tmp_path, values, header=None):
        header = header or self.HEADER
        row = [values.get(column) for column in header]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "trigger_cooldown.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [header, row]}))

    def heal(self, tmp_path, changes, header=None):
        values = {"Enchantment": self.BLOCK, "Effect": self.BLOCK_WORDS,
                  "Action": "health", "Action Event": "block",
                  "Value Low": 3, "Value High": 6}
        values.update(changes)
        return self.book(tmp_path, values, header)

    def dot(self, tmp_path, changes):
        values = {"Enchantment": self.CRIT, "Effect": self.CRIT_WORDS,
                  "Action": "apply_random_dot", "Action Event": "critical_strike",
                  "Value Low": 100}
        values.update(changes)
        return self.book(tmp_path, values)

    def test_an_empty_cell_on_a_hit_fired_event_is_the_quarter_second(self, tmp_path):
        out = gen.enchantment_effects(self.heal(tmp_path, {}))
        assert out[0]["TriggerCooldown"] == gen.DEFAULT_TRIGGER_COOLDOWN == 0.25

    def test_a_sheet_without_the_column_is_refused(self, tmp_path):
        header = [h for h in self.HEADER if h != "Trigger Cooldown"]
        with pytest.raises(gen.DataError, match="'Trigger Cooldown' column"):
            gen.enchantment_effects(self.heal(tmp_path, {}, header))

    def test_an_explicit_nought_is_none(self, tmp_path):
        out = gen.enchantment_effects(self.heal(tmp_path, {"Trigger Cooldown": 0}))
        assert out[0]["TriggerCooldown"] == 0.0

    def test_a_stated_cooldown_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.heal(tmp_path, {"Trigger Cooldown": 600}))
        assert out[0]["TriggerCooldown"] == 600.0

    def test_an_event_no_hit_fires_takes_no_default(self, tmp_path):
        out = gen.enchantment_effects(self.heal(tmp_path, {"Action Event": "kill"}))
        assert out[0]["TriggerCooldown"] == 0.0

    def test_a_stat_row_gets_no_default_and_refuses_one(self, tmp_path):
        stat = {"Action": None, "Action Event": None, "Stat": "block_chance",
                "Value Kind": "increased"}
        assert gen.enchantment_effects(self.heal(tmp_path, stat))[0]["TriggerCooldown"] == 0.0
        with pytest.raises(gen.DataError, match="never waits"):
            gen.enchantment_effects(self.heal(tmp_path, dict(stat, **{"Trigger Cooldown": 1})))

    def test_a_timed_row_refuses_one(self, tmp_path):
        with pytest.raises(gen.DataError, match="never waits"):
            gen.enchantment_effects(self.heal(tmp_path, {
                "Action Event": "every_seconds", "Every Seconds": 10,
                "Trigger Cooldown": 1}))

    def test_a_cooldown_past_an_hour_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="up to 3600"):
            gen.enchantment_effects(self.heal(tmp_path, {"Trigger Cooldown": 3601}))

    def test_a_random_dot_on_a_critical_strike_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.dot(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["FractionOf"], out[0]["TriggerCooldown"]) == (
            "apply_random_dot", "critical_strike", 100.0, "", 0.25)

    def test_a_random_dot_on_retaliation_is_accepted(self, tmp_path):
        out = gen.enchantment_effects(self.dot(tmp_path, {"Action Event": "retaliation_dealt"}))
        assert out[0]["ActionEvent"] == "retaliation_dealt"

    def test_a_random_dot_on_an_event_carrying_no_amount_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="what reached its health"):
            gen.enchantment_effects(self.dot(tmp_path, {"Action Event": "hit_dealt"}))

    def test_a_random_dot_chance_past_a_hundred_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="A chance is above 0"):
            gen.enchantment_effects(self.dot(tmp_path, {"Value Low": 150}))

    def test_a_random_dot_with_a_fraction_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="must be empty"):
            gen.enchantment_effects(self.dot(tmp_path, {"Fraction Of": "maximum"}))


class TestHealthThresholdAndFloorStart:
    """A health crossing, its Event Value, the health cap and an explicit
    cooldown on an own stack. Issue #1833 group D part 2, ruled 2026-09-30:
    Archon's Aegis, the Demon King's Regalia and "You start every dungeon floor
    at 30%-50% of your maximum HP"."""

    FLOOR = gen.row_name("Negative", "You start every dungeon floor at 30%-50% of your maximum HP"[:48])
    FLOOR_WORDS = "You start every dungeon floor at 30%-50% of your maximum HP"
    ENCHANTMENTS = TestTriggerCooldownAndRandomDot.ENCHANTMENTS[:2] + [
        [TestTriggerCooldownAndRandomDot.BLOCK_WORDS, "Generic", 4, "Stat.Defense.Block", None,
         FLOOR_WORDS, "Generic", 3, "Stat.Defense.Life"],
    ]
    HEADER = TestTriggerCooldownAndRandomDot.HEADER

    def book(self, tmp_path, values):
        row = [values.get(column) for column in self.HEADER]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "threshold.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER, row]}))

    def heal(self, tmp_path, changes):
        values = {"Enchantment": TestTriggerCooldownAndRandomDot.BLOCK,
                  "Effect": TestTriggerCooldownAndRandomDot.BLOCK_WORDS,
                  "Action": "health", "Action Event": "health_falls_below",
                  "Event Value": 10, "Value Low": 3, "Value High": 6}
        values.update(changes)
        return self.book(tmp_path, values)

    def stack(self, tmp_path, changes):
        values = {"Enchantment": TestTriggerCooldownAndRandomDot.CRIT,
                  "Effect": TestTriggerCooldownAndRandomDot.CRIT_WORDS,
                  "Stat": "attack_damage", "Value Kind": "more", "Value Low": 100,
                  "Scale": "own_stacks", "Scale Step": 1,
                  "Action Event": "health_falls_below", "Event Value": 25,
                  "Stack Seconds": 10, "Scale Max Steps": 1}
        values.update(changes)
        return self.book(tmp_path, values)

    def cap(self, tmp_path, changes):
        values = {"Enchantment": self.FLOOR, "Effect": self.FLOOR_WORDS,
                  "Action": "health_capped_at", "Action Event": "floor_start",
                  "Value Low": 30, "Value High": 50}
        values.update(changes)
        return self.book(tmp_path, values)

    def test_a_crossing_row_carries_its_event_value(self, tmp_path):
        out = gen.enchantment_effects(self.heal(tmp_path, {}))
        assert (out[0]["ActionEvent"], out[0]["EventValue"]) == ("health_falls_below", 10.0)

    def test_the_crossing_event_without_an_event_value_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="states no Event Value"):
            gen.enchantment_effects(self.heal(tmp_path, {"Event Value": None}))

    def test_an_event_value_on_another_event_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="Only health_falls_below reads one"):
            gen.enchantment_effects(self.heal(tmp_path, {"Action Event": "block"}))

    def test_an_event_value_of_a_hundred_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="above 0 and below 100"):
            gen.enchantment_effects(self.heal(tmp_path, {"Event Value": 100}))

    def test_the_crossing_is_not_hit_fired_so_takes_no_default_cooldown(self, tmp_path):
        out = gen.enchantment_effects(self.heal(tmp_path, {}))
        assert out[0]["TriggerCooldown"] == 0.0

    def test_an_own_stack_takes_a_stated_cooldown(self, tmp_path):
        out = gen.enchantment_effects(self.stack(tmp_path, {"Trigger Cooldown": 300}))
        assert (out[0]["Scale"], out[0]["EventValue"], out[0]["TriggerCooldown"]) == (
            "own_stacks", 25.0, 300.0)

    def test_an_own_stack_takes_no_cooldown_by_default(self, tmp_path):
        out = gen.enchantment_effects(self.stack(tmp_path, {"Action Event": "critical_strike",
                                                            "Event Value": None}))
        assert out[0]["TriggerCooldown"] == 0.0

    def test_a_health_cap_row_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.cap(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["ValueHigh"], out[0]["FractionOf"]) == (
            "health_capped_at", "floor_start", 30.0, 50.0, "")

    def test_a_health_cap_of_a_hundred_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="above 0 and below 100"):
            gen.enchantment_effects(self.cap(tmp_path, {"Value Low": 100, "Value High": None}))

    def test_a_health_cap_with_a_fraction_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="must be"):
            gen.enchantment_effects(self.cap(tmp_path, {"Fraction Of": "maximum"}))


class TestNearbyActionsAndTheirEvents:
    """A smite or a heal of every enemy nearby, on a broken energy shield or the
    player's death. Issue #1833 group D part 3, ruled 2026-10-01: "When your
    energy shield is broken, you smite all nearby enemies" and "On death all
    nearby enemies are healed for 10%-20% of their maximum HP"."""

    SMITE_WORDS = "When your energy shield is broken, you smite all nearby enemies"
    SMITE = gen.row_name("Positive", SMITE_WORDS[:48])
    DEATH_WORDS = "On death all nearby enemies are healed for 10%-20% of their maximum HP"
    DEATH = gen.row_name("Negative", DEATH_WORDS[:48])
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [SMITE_WORDS, "Generic", 4, "Stat.Defense.EnergyShield", None,
         DEATH_WORDS, "Generic", 3, "Stat.Defense.Life"],
    ]
    HEADER = TestScaleStepHigh.HEADER

    def book(self, tmp_path, values):
        row = [values.get(column) for column in self.HEADER]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "nearby.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER, row]}))

    def smite(self, tmp_path, changes):
        values = {"Enchantment": self.SMITE, "Effect": self.SMITE_WORDS,
                  "Action": "smite_nearby", "Action Event": "energy_shield_broken",
                  "Value Low": 100}
        values.update(changes)
        return self.book(tmp_path, values)

    def heal(self, tmp_path, changes):
        values = {"Enchantment": self.DEATH, "Effect": self.DEATH_WORDS,
                  "Action": "heal_nearby_enemies", "Action Event": "player_death",
                  "Value Low": 10, "Value High": 20}
        values.update(changes)
        return self.book(tmp_path, values)

    def test_a_smite_on_a_broken_shield_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.smite(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["FractionOf"]) == ("smite_nearby", "energy_shield_broken", 100.0, "")

    def test_a_broken_shield_is_hit_fired_so_takes_the_quarter_second(self, tmp_path):
        out = gen.enchantment_effects(self.smite(tmp_path, {}))
        assert out[0]["TriggerCooldown"] == gen.DEFAULT_TRIGGER_COOLDOWN == 0.25

    def test_a_heal_on_the_players_death_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.heal(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["ValueHigh"], out[0]["FractionOf"]) == (
            "heal_nearby_enemies", "player_death", 10.0, 20.0, "")

    def test_the_players_death_is_not_hit_fired_so_takes_no_default_cooldown(self, tmp_path):
        out = gen.enchantment_effects(self.heal(tmp_path, {}))
        assert out[0]["TriggerCooldown"] == 0.0

    def test_a_nearby_action_on_an_event_the_game_does_not_fire_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="which the game does not fire"):
            gen.enchantment_effects(self.smite(tmp_path, {"Action Event": "shield_shattered"}))

    def test_a_nearby_action_with_no_event_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="which the game does not fire"):
            gen.enchantment_effects(self.smite(tmp_path, {"Action Event": None}))

    def test_a_heal_of_more_than_a_whole_maximum_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="up to 100"):
            gen.enchantment_effects(self.heal(tmp_path, {"Value Low": 150, "Value High": None}))

    def test_a_smite_past_its_bound_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="up to 1000"):
            gen.enchantment_effects(self.smite(tmp_path, {"Value Low": 1500}))

    def test_a_nearby_action_with_a_fraction_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="must be empty"):
            gen.enchantment_effects(self.heal(tmp_path, {"Fraction Of": "maximum"}))

    def test_a_nearby_action_with_stacks_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="caps nothing"):
            gen.enchantment_effects(self.smite(tmp_path, {"Scale Max Steps": 3}))


class TestRemainingDamageAndTheAilmentColumn:
    """The remaining damage of the wearer's own damage over time, around the
    wearer on its death or on the event's target, and the Ailment column that
    limits it. Issue #1833 group D part 4, ruled 2026-10-01: "When you die, all
    active DoTs on nearby enemies instantly deal their remaining damage" and
    "Necrosis effects deal 20%-40% of their remaining damage instantly when you
    land a critical strike"."""

    DEATH_WORDS = "When you die, all active DoTs on nearby enemies instantly deal their remaining damage"
    DEATH = gen.row_name("Positive", DEATH_WORDS[:48])
    NECROSIS_WORDS = "Necrosis effects deal 20%-40% of their remaining damage instantly when you land a critical strike"
    NECROSIS = gen.row_name("Positive", NECROSIS_WORDS[:48])
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [DEATH_WORDS, "Generic", 4, "Keyword.DoT", None,
         "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life"],
        [NECROSIS_WORDS, "Generic", 4, "Keyword.DoT.Necrosis", None,
         None, None, None, None],
    ]
    HEADER = TestScaleStepHigh.HEADER

    def book(self, tmp_path, values, header=None):
        header = header or self.HEADER
        row = [values.get(column) for column in header]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "remaining.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [header, row]}))

    def death(self, tmp_path, changes, header=None):
        values = {"Enchantment": self.DEATH, "Effect": self.DEATH_WORDS,
                  "Action": "dot_remaining_nearby", "Action Event": "player_death",
                  "Value Low": 100}
        values.update(changes)
        return self.book(tmp_path, values, header)

    def necrosis(self, tmp_path, changes):
        values = {"Enchantment": self.NECROSIS, "Effect": self.NECROSIS_WORDS,
                  "Action": "dot_remaining_target", "Action Event": "critical_strike",
                  "Ailment": "Necrosis", "Value Low": 20, "Value High": 40}
        values.update(changes)
        return self.book(tmp_path, values)

    def test_the_death_row_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.death(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["FractionOf"], out[0]["Ailment"], out[0]["TriggerCooldown"]) == (
            "dot_remaining_nearby", "player_death", 100.0, "", "", 0.0)

    def test_the_necrosis_row_is_carried_through_with_its_ailment(self, tmp_path):
        out = gen.enchantment_effects(self.necrosis(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["ValueHigh"], out[0]["Ailment"]) == (
            "dot_remaining_target", "critical_strike", 20.0, 40.0, "Necrosis")

    def test_a_critical_strike_is_hit_fired_so_takes_the_quarter_second(self, tmp_path):
        out = gen.enchantment_effects(self.necrosis(tmp_path, {}))
        assert out[0]["TriggerCooldown"] == gen.DEFAULT_TRIGGER_COOLDOWN == 0.25

    def test_the_death_row_may_name_an_ailment(self, tmp_path):
        out = gen.enchantment_effects(self.death(tmp_path, {"Ailment": "Burn"}))
        assert out[0]["Ailment"] == "Burn"

    def test_a_sheet_without_the_column_is_refused_now_its_rows_are_written(self, tmp_path):
        header = [column for column in self.HEADER if column != "Ailment"]
        with pytest.raises(gen.DataError, match="no 'Ailment' column"):
            gen.enchantment_effects(self.death(tmp_path, {}, header=header))

    def test_the_target_row_without_an_ailment_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="names no Ailment"):
            gen.enchantment_effects(self.necrosis(tmp_path, {"Ailment": None}))

    def test_an_ailment_the_game_does_not_have_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="not one the game has"):
            gen.enchantment_effects(self.necrosis(tmp_path, {"Ailment": "Frostbite"}))

    def test_an_ailment_on_an_action_that_does_not_read_it_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="would be dropped"):
            gen.enchantment_effects(self.death(tmp_path, {
                "Action": "health", "Action Event": "block", "Ailment": "Burn"}))

    def test_an_ailment_on_a_stat_row_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="would be dropped"):
            gen.enchantment_effects(self.death(tmp_path, {
                "Action": None, "Action Event": None, "Stat": "attack_damage",
                "Value Kind": "increased", "Value Low": 10, "Ailment": "Burn"}))

    def test_the_target_row_on_an_event_naming_nobody_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="cannot use"):
            gen.enchantment_effects(self.necrosis(tmp_path, {"Action Event": "block"}))

    def test_the_death_row_on_an_event_the_game_does_not_fire_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="cannot use"):
            gen.enchantment_effects(self.death(tmp_path, {"Action Event": "shield_shattered"}))

    def test_a_share_past_a_hundred_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="up to 100"):
            gen.enchantment_effects(self.death(tmp_path, {"Value Low": 150}))

    def test_a_remaining_damage_row_with_a_fraction_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="must be empty"):
            gen.enchantment_effects(self.death(tmp_path, {"Fraction Of": "maximum"}))


class TestGadgetDestroyedAndResourceConsumed:
    """Two events and the rows they carry. Issue #1833 group D part 5, ruled
    2026-10-01: "When any of your gadgets is destroyed, all remaining gadgets gain
    30%-50% increased damage for 5 seconds", "Consuming a resource stack or charge
    grants 5%-10% increased damage for 3 seconds" and "Each resource or charge
    consumed restores 1%-3% of your maximum HP"."""

    GADGET_WORDS = ("When any of your gadgets is destroyed, all remaining gadgets gain "
                    "30%-50% increased damage for 5 seconds")
    GADGET = gen.row_name("Positive", GADGET_WORDS[:48])
    HEAL_WORDS = "Each resource or charge consumed restores 1%-3% of your maximum HP"
    HEAL = gen.row_name("Positive", HEAL_WORDS[:48])
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [GADGET_WORDS, "Generic", 4, "Type.Deployable", None,
         "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life"],
        [HEAL_WORDS, "Generic", 4, "Stat.Defense.Life", None,
         None, None, None, None],
    ]
    HEADER = TestScaleStepHigh.HEADER

    def book(self, tmp_path, values):
        row = [values.get(column) for column in self.HEADER]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "gadget_resource.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER, row]}))

    def gadget(self, tmp_path, changes):
        values = {"Enchantment": self.GADGET, "Effect": self.GADGET_WORDS,
                  "Stat": "attack_damage", "Value Kind": "increased",
                  "Value Low": 30, "Value High": 50, "Required Tags": "Type.Deployable",
                  "Scale": "own_stacks", "Scale Step": 1,
                  "Action Event": "gadget_destroyed", "Stack Seconds": 5,
                  "Scale Max Steps": 1}
        values.update(changes)
        return self.book(tmp_path, values)

    def heal(self, tmp_path, changes):
        values = {"Enchantment": self.HEAL, "Effect": self.HEAL_WORDS,
                  "Action": "health", "Action Event": "resource_consumed",
                  "Value Low": 1, "Value High": 3}
        values.update(changes)
        return self.book(tmp_path, values)

    def test_an_own_stack_on_a_gadget_destroyed_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.gadget(tmp_path, {}))
        assert (out[0]["Scale"], out[0]["ActionEvent"], out[0]["RequiredTags"],
                out[0]["StackSeconds"], out[0]["ScaleMaxSteps"]) == (
            "own_stacks", "gadget_destroyed", "Type.Deployable", 5.0, 1)

    def test_a_gadget_destroyed_is_not_hit_fired_so_takes_no_default_cooldown(self, tmp_path):
        out = gen.enchantment_effects(self.heal(tmp_path, {"Action Event": "gadget_destroyed"}))
        assert out[0]["TriggerCooldown"] == 0.0

    def test_a_heal_on_a_resource_consumed_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.heal(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["ValueHigh"], out[0]["FractionOf"], out[0]["TriggerCooldown"]) == (
            "health", "resource_consumed", 1.0, 3.0, "maximum", 0.0)

    def test_both_events_are_ones_an_action_row_may_name(self):
        assert {"gadget_destroyed", "resource_consumed"} <= gen.action_events()

    def test_neither_event_is_hit_fired(self):
        assert not {"gadget_destroyed", "resource_consumed"} & set(gen.HIT_FIRED_EVENTS)


class TestApplyStatusAndItsEvents:
    """A status applied to the other character of an event, a chance or for
    stated seconds, named in the Ailment column; and the event `first_hit_dealt`.
    Issue #1833 group E part 1, ruled 2026-10-01: "Retaliation damage has a
    20%-40% chance to stagger the attacker" and "Retaliation damage applies a 2-4
    second slow to the attacker"."""

    STAGGER_WORDS = "Retaliation damage has a 20%-40% chance to stagger the attacker"
    STAGGER = gen.row_name("Positive", STAGGER_WORDS[:48])
    SLOW_WORDS = "Retaliation damage applies a 2-4 second slow to the attacker"
    SLOW = gen.row_name("Positive", SLOW_WORDS[:48])
    GADGET_WORDS = "Gadgets apply a 1-2 second stagger to enemies they hit, once every 5 seconds"
    GADGET = gen.row_name("Positive", GADGET_WORDS[:48])
    OVER_WORDS = "Retaliation damage has a 120% chance to stagger the attacker"
    OVER = gen.row_name("Positive", OVER_WORDS[:48])
    LONG_WORDS = "Retaliation damage applies a 12 second slow to the attacker"
    LONG = gen.row_name("Positive", LONG_WORDS[:48])
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [STAGGER_WORDS, "Generic", 4, "Stat.Defense.Retaliation", None,
         "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life"],
        [SLOW_WORDS, "Generic", 4, "Stat.Defense.Retaliation", None,
         None, None, None, None],
        [GADGET_WORDS, "Generic", 4, "Type.Deployable", None,
         None, None, None, None],
        [OVER_WORDS, "Generic", 4, "Stat.Defense.Retaliation", None,
         None, None, None, None],
        [LONG_WORDS, "Generic", 4, "Stat.Defense.Retaliation", None,
         None, None, None, None],
    ]
    HEADER = TestScaleStepHigh.HEADER

    def book(self, tmp_path, values):
        row = [values.get(column) for column in self.HEADER]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "status.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER, row]}))

    def stagger(self, tmp_path, changes):
        values = {"Enchantment": self.STAGGER, "Effect": self.STAGGER_WORDS,
                  "Action": "apply_status", "Action Event": "retaliation_dealt",
                  "Ailment": "Stagger", "Value Low": 20, "Value High": 40}
        values.update(changes)
        return self.book(tmp_path, values)

    def slow(self, tmp_path, changes):
        values = {"Enchantment": self.SLOW, "Effect": self.SLOW_WORDS,
                  "Action": "apply_status_seconds", "Action Event": "retaliation_dealt",
                  "Ailment": "Cripple", "Value Low": 2, "Value High": 4}
        values.update(changes)
        return self.book(tmp_path, values)

    def test_a_chance_row_is_carried_through_with_its_status(self, tmp_path):
        out = gen.enchantment_effects(self.stagger(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["ValueHigh"], out[0]["FractionOf"], out[0]["Ailment"]) == (
            "apply_status", "retaliation_dealt", 20.0, 40.0, "", "Stagger")

    def test_a_seconds_row_is_carried_through_with_its_status(self, tmp_path):
        out = gen.enchantment_effects(self.slow(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ValueLow"], out[0]["ValueHigh"],
                out[0]["Ailment"]) == ("apply_status_seconds", 2.0, 4.0, "Cripple")

    def test_retaliation_is_hit_fired_so_a_status_takes_the_quarter_second(self, tmp_path):
        out = gen.enchantment_effects(self.stagger(tmp_path, {}))
        assert out[0]["TriggerCooldown"] == gen.DEFAULT_TRIGGER_COOLDOWN == 0.25

    def test_a_stated_trigger_cooldown_is_kept(self, tmp_path):
        out = gen.enchantment_effects(self.slow(tmp_path, {
            "Enchantment": self.GADGET, "Effect": self.GADGET_WORDS,
            "Action Event": "deployable_hit", "Ailment": "Stagger",
            "Value Low": 1, "Value High": 2, "Trigger Cooldown": 5}))
        assert out[0]["TriggerCooldown"] == 5.0

    @pytest.mark.parametrize("status", ["Bleed", "Cripple", "Random Debuff"])
    def test_a_chance_row_may_name_an_ailment_or_the_random_debuff(self, tmp_path, status):
        out = gen.enchantment_effects(self.stagger(tmp_path, {"Ailment": status}))
        assert out[0]["Ailment"] == status

    def test_the_first_hit_is_an_event_a_status_may_hang_on(self, tmp_path):
        out = gen.enchantment_effects(self.stagger(tmp_path, {"Action Event": "first_hit_dealt"}))
        assert out[0]["ActionEvent"] == "first_hit_dealt"
        assert out[0]["TriggerCooldown"] == gen.DEFAULT_TRIGGER_COOLDOWN

    def test_the_first_hit_is_an_event_the_game_fires_and_a_hit(self):
        assert "first_hit_dealt" in gen.ACTION_ONLY_EVENTS
        assert "first_hit_dealt" in gen.HIT_FIRED_EVENTS

    def test_a_status_row_with_no_status_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="cannot"):
            gen.enchantment_effects(self.stagger(tmp_path, {"Ailment": None}))

    def test_a_status_the_game_does_not_have_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="cannot"):
            gen.enchantment_effects(self.stagger(tmp_path, {"Ailment": "Frostbite"}))

    def test_a_stun_is_refused_because_only_the_random_debuff_reaches_it(self, tmp_path):
        with pytest.raises(gen.DataError, match="cannot"):
            gen.enchantment_effects(self.stagger(tmp_path, {"Ailment": "Stun"}))

    def test_seconds_for_a_status_whose_duration_cannot_be_stated_are_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="cannot"):
            gen.enchantment_effects(self.slow(tmp_path, {"Ailment": "Bleed"}))

    def test_a_status_on_an_event_naming_nobody_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="Only an event naming"):
            gen.enchantment_effects(self.stagger(tmp_path, {"Action Event": "block"}))

    def test_a_chance_past_a_hundred_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="up to 100"):
            gen.enchantment_effects(self.stagger(tmp_path, {
                "Enchantment": self.OVER, "Effect": self.OVER_WORDS,
                "Value Low": 120, "Value High": 120}))

    def test_seconds_past_the_bound_are_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="up to 10"):
            gen.enchantment_effects(self.slow(tmp_path, {
                "Enchantment": self.LONG, "Effect": self.LONG_WORDS,
                "Value Low": 12, "Value High": 12}))

    def test_a_status_row_with_a_fraction_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="must be empty"):
            gen.enchantment_effects(self.stagger(tmp_path, {"Fraction Of": "maximum"}))

class TestDamageImmunityAndTheShieldRecharge:
    """A no-damage window opened for a row's seconds, and the event of the
    energy shield filled by regeneration. Issue #1833 group E part 2, ruled
    2026-10-02: "Archon's Aegis (6-Piece Bonus): When you block an attack, you
    become immune to all damage for 3 seconds. (10s cd)" and "When your energy
    shield fully recharges, release a nova dealing 50%-100% weapon damage to
    nearby enemies"."""

    IMMUNE_WORDS = ("Archon's Aegis (6-Piece Bonus): When you block an attack, you "
                    "become immune to all damage for 3 seconds. (10s cd)")
    IMMUNE = gen.row_name("Positive", IMMUNE_WORDS[:48])
    NOVA_WORDS = ("When your energy shield fully recharges, release a nova dealing "
                  "50%-100% weapon damage to nearby enemies")
    NOVA = gen.row_name("Positive", NOVA_WORDS[:48])
    LONG_WORDS = "When you block an attack, you become immune to all damage for 12 seconds"
    LONG = gen.row_name("Positive", LONG_WORDS[:48])
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [IMMUNE_WORDS, "Generic", 4, "Stat.Defense.Block", None,
         "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life"],
        [NOVA_WORDS, "Generic", 4, "Keyword.Shield", None,
         None, None, None, None],
        [LONG_WORDS, "Generic", 4, "Stat.Defense.Block", None,
         None, None, None, None],
    ]
    HEADER = TestScaleStepHigh.HEADER

    def book(self, tmp_path, values):
        row = [values.get(column) for column in self.HEADER]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "immunity.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER, row]}))

    def immune(self, tmp_path, changes):
        values = {"Enchantment": self.IMMUNE, "Effect": self.IMMUNE_WORDS,
                  "Action": "damage_immunity", "Action Event": "block",
                  "Value Low": 3, "Trigger Cooldown": 10}
        values.update(changes)
        return self.book(tmp_path, values)

    def test_an_immunity_row_is_carried_through_with_its_cooldown(self, tmp_path):
        out = gen.enchantment_effects(self.immune(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["FractionOf"], out[0]["TriggerCooldown"]) == (
            "damage_immunity", "block", 3.0, "", 10.0)

    def test_an_immunity_row_on_a_block_takes_the_quarter_second_when_it_states_none(
            self, tmp_path):
        out = gen.enchantment_effects(self.immune(tmp_path, {"Trigger Cooldown": None}))
        assert out[0]["TriggerCooldown"] == gen.DEFAULT_TRIGGER_COOLDOWN

    def test_an_immunity_row_on_an_event_the_game_does_not_fire_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="does not fire"):
            gen.enchantment_effects(self.immune(tmp_path, {"Action Event": "parried"}))

    def test_an_immunity_past_the_bound_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="up to 10"):
            gen.enchantment_effects(self.immune(tmp_path, {
                "Enchantment": self.LONG, "Effect": self.LONG_WORDS,
                "Value Low": 12, "Trigger Cooldown": None}))

    def test_an_immunity_row_with_a_fraction_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="must be empty"):
            gen.enchantment_effects(self.immune(tmp_path, {"Fraction Of": "maximum"}))

    def test_the_shield_recharge_is_an_event_a_nova_may_hang_on(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, {
            "Enchantment": self.NOVA, "Effect": self.NOVA_WORDS,
            "Action": "smite_nearby", "Action Event": "energy_shield_recharged",
            "Value Low": 50, "Value High": 100}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["ValueHigh"]) == ("smite_nearby", "energy_shield_recharged", 50.0, 100.0)

    def test_the_shield_recharge_is_fired_by_the_game_and_is_no_hit(self, tmp_path):
        assert "energy_shield_recharged" in gen.ACTION_ONLY_EVENTS
        assert "energy_shield_recharged" not in gen.HIT_FIRED_EVENTS
        out = gen.enchantment_effects(self.book(tmp_path, {
            "Enchantment": self.NOVA, "Effect": self.NOVA_WORDS,
            "Action": "smite_nearby", "Action Event": "energy_shield_recharged",
            "Value Low": 50, "Value High": 100}))
        assert out[0]["TriggerCooldown"] == 0.0

class TestRidersOnAnAilment:
    """A number hung on an ailment the wearer applies. Issue #1833, ruled
    2026-10-06: "Bleeding enemies take 20%-40% increased damage from all
    sources". No event, an Ailment, and a percent."""

    WORDS = "Bleeding enemies take 20%-40% increased damage from all sources"
    NAME = gen.row_name("Positive", WORDS[:48])
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [WORDS, "Generic", 4, "Keyword.DoT.Bleed", None,
         "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life"],
    ]
    HEADER = TestScaleStepHigh.HEADER

    def rider(self, tmp_path, changes):
        values = {"Enchantment": self.NAME, "Effect": self.WORDS,
                  "Action": "ailment_damage_taken", "Ailment": "Bleed",
                  "Value Low": 20, "Value High": 40}
        values.update(changes)
        row = [values.get(column) for column in self.HEADER]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "rider.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER, row]}))

    def test_a_rider_is_carried_through_with_its_ailment_and_no_event(self, tmp_path):
        out = gen.enchantment_effects(self.rider(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["Ailment"],
                out[0]["ValueLow"], out[0]["ValueHigh"], out[0]["FractionOf"],
                out[0]["TriggerCooldown"]) == (
            "ailment_damage_taken", "", "Bleed", 20.0, 40.0, "", 0.0)

    def test_a_rider_with_an_event_is_refused(self, tmp_path):
        # NOTHING FIRES A RIDER. An event on one would be a column nothing reads.
        with pytest.raises(gen.DataError, match="must be empty"):
            gen.enchantment_effects(self.rider(tmp_path, {"Action Event": "hit_dealt"}))

    def test_a_rider_with_no_ailment_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="names no Ailment"):
            gen.enchantment_effects(self.rider(tmp_path, {"Ailment": None}))

    def test_a_rider_on_an_ailment_the_game_does_not_have_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="not one the game has"):
            gen.enchantment_effects(self.rider(tmp_path, {"Ailment": "Void Splinter"}))

    def test_a_rider_with_a_condition_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="must be empty"):
            gen.enchantment_effects(self.rider(tmp_path, {
                "Condition": "health_below", "Condition Value": 50}))


class TestTheTimedCleanse:
    """A cleanse on a clock. Issue #1833, written 2026-10-05: "You are cleansed
    every 5 seconds". The game has cleansed on the timed event since 2026-09-26
    and the generator refused the action's name until this."""

    WORDS = "You are cleansed every 5 seconds"
    NAME = gen.row_name("Positive", WORDS[:48])
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [WORDS, "Generic", 4, "Trigger.Timer", None,
         "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life"],
    ]
    HEADER = TestScaleStepHigh.HEADER

    def cleanse(self, tmp_path, changes):
        values = {"Enchantment": self.NAME, "Effect": self.WORDS,
                  "Action": "cleanse", "Action Event": "every_seconds",
                  "Every Seconds": 5, "Value Low": 100}
        values.update(changes)
        row = [values.get(column) for column in self.HEADER]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "cleanse.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER, row]}))

    def test_a_timed_cleanse_is_carried_through_with_its_period(self, tmp_path):
        out = gen.enchantment_effects(self.cleanse(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["EverySeconds"],
                out[0]["ValueLow"], out[0]["FractionOf"], out[0]["TriggerCooldown"]) == (
            "cleanse", "every_seconds", 5.0, 100.0, "", 0.0)

    def test_a_cleanse_on_any_event_but_the_timed_one_is_refused(self, tmp_path):
        # THE GAME READS THE FLAG ON THE TIMED PATH ONLY. A cleanse on a kill
        # would reach the pool path, find no pool named cleanse and do nothing.
        with pytest.raises(gen.DataError, match="on nothing else"):
            gen.enchantment_effects(self.cleanse(tmp_path, {
                "Action Event": "kill", "Every Seconds": None}))

    def test_a_cleanse_with_a_size_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="is 100 and nothing else"):
            gen.enchantment_effects(self.cleanse(tmp_path, {"Value Low": 50}))

    def test_a_cleanse_with_a_fraction_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="must be empty"):
            gen.enchantment_effects(self.cleanse(tmp_path, {"Fraction Of": "maximum"}))

    def test_a_cleanse_with_no_period_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="would never grant"):
            gen.enchantment_effects(self.cleanse(tmp_path, {"Every Seconds": None}))


class TestReflectAndTheBlockCount:
    """A share of what a block removed paid back to the attacker, a nearby blow
    scaled by armour, and a nearby action on the Nth of its events inside a
    window. Issue #1833 group E part 3, ruled 2026-10-02: "Reflect 20%-100% of
    damage blocked back at attackers", "Blocking attacks deals 200%-400% of your
    armor as damage to nearby enemies" and "Every 3 blocks in quick succession
    triggers a shockwave dealing 100%-200% weapon damage to nearby enemies"."""

    REFLECT_WORDS = "Reflect 20%-100% of damage blocked back at attackers"
    REFLECT = gen.row_name("Positive", REFLECT_WORDS[:48])
    ARMOUR_WORDS = "Blocking attacks deals 200%-400% of your armor as damage to nearby enemies"
    ARMOUR = gen.row_name("Positive", ARMOUR_WORDS[:48])
    WAVE_WORDS = ("Every 3 blocks in quick succession triggers a shockwave dealing "
                  "100%-200% weapon damage to nearby enemies")
    WAVE = gen.row_name("Positive", WAVE_WORDS[:48])
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [REFLECT_WORDS, "Generic", 4, "Stat.Defense.Block", None,
         "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life"],
        [ARMOUR_WORDS, "Generic", 4, "Stat.Defense.Block", None,
         None, None, None, None],
        [WAVE_WORDS, "Generic", 4, "Stat.Defense.Block", None,
         None, None, None, None],
    ]
    HEADER = TestScaleStepHigh.HEADER

    def book(self, tmp_path, values):
        row = [values.get(column) for column in self.HEADER]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "reflect.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER, row]}))

    def reflect(self, tmp_path, changes):
        values = {"Enchantment": self.REFLECT, "Effect": self.REFLECT_WORDS,
                  "Action": "reflect_blocked", "Action Event": "block",
                  "Value Low": 20, "Value High": 100, "Trigger Cooldown": 0}
        values.update(changes)
        return self.book(tmp_path, values)

    def wave(self, tmp_path, changes):
        values = {"Enchantment": self.WAVE, "Effect": self.WAVE_WORDS,
                  "Action": "smite_nearby", "Action Event": "block",
                  "Value Low": 100, "Value High": 200, "Every Nth": 3, "Stack Seconds": 3}
        values.update(changes)
        return self.book(tmp_path, values)

    def test_a_reflect_row_is_carried_through_with_no_cooldown(self, tmp_path):
        out = gen.enchantment_effects(self.reflect(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["ValueHigh"], out[0]["FractionOf"], out[0]["TriggerCooldown"]) == (
            "reflect_blocked", "block", 20.0, 100.0, "", 0.0)

    def test_a_reflect_on_an_event_carrying_no_block_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="Only an event carrying"):
            gen.enchantment_effects(self.reflect(tmp_path, {"Action Event": "hit_taken"}))

    def test_a_reflect_row_with_a_fraction_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="must be empty"):
            gen.enchantment_effects(self.reflect(tmp_path, {"Fraction Of": "maximum"}))

    def test_an_armour_nova_row_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, {
            "Enchantment": self.ARMOUR, "Effect": self.ARMOUR_WORDS,
            "Action": "smite_nearby_by_armor", "Action Event": "block",
            "Value Low": 200, "Value High": 400}))
        assert (out[0]["Action"], out[0]["ValueLow"], out[0]["ValueHigh"]) == (
            "smite_nearby_by_armor", 200.0, 400.0)

    def test_a_counted_nearby_row_carries_its_count_and_window(self, tmp_path):
        out = gen.enchantment_effects(self.wave(tmp_path, {}))
        assert (out[0]["Action"], out[0]["EveryNth"], out[0]["StackSeconds"]) == (
            "smite_nearby", 3, 3.0)

    def test_a_counted_nearby_row_with_no_window_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="no Stack Seconds"):
            gen.enchantment_effects(self.wave(tmp_path, {"Stack Seconds": None}))

    def test_a_nearby_row_with_a_window_and_no_count_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="no Every Nth"):
            gen.enchantment_effects(self.wave(tmp_path, {"Every Nth": None}))

    def test_a_count_of_one_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="from 2"):
            gen.enchantment_effects(self.wave(tmp_path, {"Every Nth": 1}))

class TestRepeatSkill:
    """A row that repeats the skill just used, free. Mechanism B2, ruled
    2026-10-05: "Every skill use has a 5%-15% chance to cast a second time for
    free"."""

    WORDS = "Every skill use has a 5%-15% chance to cast a second time for free"
    NAME = gen.row_name("Positive", WORDS[:48])
    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [WORDS, "Generic", 2, "Trigger.OnSkillUse, Scope.Global", None,
         "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life"],
    ]
    HEADER = TestScaleStepHigh.HEADER

    def repeat(self, tmp_path, changes):
        values = {"Enchantment": self.NAME, "Effect": self.WORDS,
                  "Action": "repeat_skill", "Action Event": "skill_use",
                  "Value Low": 5, "Value High": 15}
        values.update(changes)
        row = [values.get(column) for column in self.HEADER]
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "repeat.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER, row]}))

    def test_a_repeat_row_is_carried_through_with_its_chance_and_no_fraction(self, tmp_path):
        out = gen.enchantment_effects(self.repeat(tmp_path, {}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["ValueHigh"], out[0]["FractionOf"]) == (
            "repeat_skill", "skill_use", 5.0, 15.0, "")

    def test_a_repeat_row_may_be_written_on_the_event_that_includes_the_basic_attack(self, tmp_path):
        out = gen.enchantment_effects(self.repeat(tmp_path, {"Action Event": "attack_use"}))
        assert (out[0]["Action"], out[0]["ActionEvent"]) == ("repeat_skill", "attack_use")

    def test_a_repeat_row_on_the_basic_attack_event_that_names_no_skill_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="names no skill to repeat"):
            gen.enchantment_effects(self.repeat(tmp_path, {"Action Event": "basic_attack"}))

    def test_a_repeat_row_on_an_event_that_names_no_skill_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="names no skill to repeat"):
            gen.enchantment_effects(self.repeat(tmp_path, {"Action Event": "kill"}))

    @pytest.mark.parametrize("action", ["trigger_held_skill", "trigger_held_spell"])
    def test_a_row_that_triggers_a_held_skill_is_carried_through(self, tmp_path, action):
        out = gen.enchantment_effects(self.repeat(
            tmp_path, {"Action": action, "Action Event": "attack_use",
                       "Value Low": 5, "Value High": 15}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["ValueHigh"], out[0]["FractionOf"]) == (
            action, "attack_use", 5.0, 15.0, "")

    @pytest.mark.parametrize("action", ["trigger_held_skill", "trigger_held_spell"])
    def test_a_row_that_triggers_a_held_skill_on_an_event_naming_no_skill_is_refused(
            self, tmp_path, action):
        with pytest.raises(gen.DataError, match="names no skill to repeat"):
            gen.enchantment_effects(self.repeat(
                tmp_path, {"Action": action, "Action Event": "kill"}))

    @pytest.mark.parametrize("action", ["use_no_damage", "cooldown_use_no_damage"])
    @pytest.mark.parametrize("event", ["skill_use", "attack_use"])
    def test_a_row_that_rolls_a_use_dealing_no_damage_is_carried_through(
            self, tmp_path, action, event):
        out = gen.enchantment_effects(self.repeat(
            tmp_path, {"Action": action, "Action Event": event}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["ValueHigh"], out[0]["FractionOf"]) == (
            action, event, 5.0, 15.0, "")

    def test_a_row_that_rolls_a_use_dealing_no_damage_needs_an_event_that_names_a_skill(
            self, tmp_path):
        with pytest.raises(gen.DataError, match="names no skill to repeat"):
            gen.enchantment_effects(self.repeat(
                tmp_path, {"Action": "use_no_damage", "Action Event": "kill"}))

    @pytest.mark.parametrize("action", ["use_increased_damage",
                                        "cooldown_use_increased_damage"])
    def test_a_row_that_rolls_increased_damage_is_refused_until_a_column_carries_the_increase(
            self, tmp_path, action):
        with pytest.raises(gen.DataError, match="no column carries that yet"):
            gen.enchantment_effects(self.repeat(
                tmp_path, {"Action": action, "Action Event": "skill_use"}))

    def test_a_row_that_rolls_a_strike_hitting_all_nearby_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.repeat(
            tmp_path, {"Action": "use_hits_all_nearby", "Action Event": "skill_use"}))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["ValueHigh"]) == ("use_hits_all_nearby", "skill_use", 5.0, 15.0)

    def test_a_repeat_row_with_a_fraction_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="must be empty"):
            gen.enchantment_effects(self.repeat(tmp_path, {"Fraction Of": "maximum"}))

    # THE SHARE OF ITS DAMAGE THE REPEAT DEALS. The column the rows needed,
    # 2026-10-06: empty is the whole and is written as 0.
    def test_a_repeat_row_that_states_no_share_writes_none(self, tmp_path):
        out = gen.enchantment_effects(self.repeat(tmp_path, {}))
        assert out[0]["DamageShare"] == 0.0

    def test_a_repeat_row_carries_the_share_it_states(self, tmp_path):
        out = gen.enchantment_effects(self.repeat(tmp_path, {"Damage Share": 50}))
        assert out[0]["DamageShare"] == 50.0

    @pytest.mark.parametrize("share", [-50, 100.5, 150])
    def test_a_share_outside_above_0_and_up_to_100_is_refused(self, tmp_path, share):
        with pytest.raises(gen.DataError, match="above 0 and up to 100"):
            gen.enchantment_effects(self.repeat(tmp_path, {"Damage Share": share}))

    def test_a_share_on_a_row_that_repeats_nothing_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="states a Damage Share"):
            gen.enchantment_effects(self.repeat(tmp_path, {
                "Action": "mana", "Action Event": "kill", "Fraction Of": "maximum",
                "Damage Share": 50}))


class TestEnchantmentEffects:
    """What an enchantment grants, read from the Enchantment Effects sheet. #45.

    A row names an enchantment by the row name an item stores, repeats its
    words, and states one stat, one bucket and a value or a range. Five kinds
    of row are refused, and each is a mistake a person editing the sheet can
    make: a name that matches nothing, words that no longer match the
    enchantment, a range the words do not state in that order, a range on a set
    row, and half a set.
    """

    ENCHANTMENTS = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        ["Double your energy shield", "Generic", 1, "Stat.Defense.EnergyShield",
         None, "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life"],
        ["Archon's Aegis (2-Piece Bonus): Your block chance is increased by 25%",
         "Set", 5, "Stat.Defense.Block", None,
         "Your movement speed is reduced by 10%", "Set", 5,
         "Stat.Utility.Movespeed"],
        ["Archon's Aegis (6-Piece Bonus): Your block value is doubled",
         "Set", 5, "Stat.Defense.Block", None,
         "You cannot block", "Generic", 2, "Stat.Defense.Block"],
        ["Your block chance is increased by 10%-20%", "Generic", 3,
         "Stat.Defense.Block", None,
         "Your attack speed is reduced by 20%-35%", "Generic", 3,
         "Stat.Offense.Speed"],
        ["Blocking an attack restores 3%-6% of your maximum HP", "Generic", 3,
         "Stat.Defense.Block", None,
         "You take 5%-10% more damage", "Generic", 3,
         "Stat.Defense.Life"],
    ]
    HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
              "Value High", "Required Tags", "Condition", "Condition Value",
              "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
              "Scale Max Steps", "Stack Seconds", "Scale Offset",
              "Every Seconds", "Every Nth", "Scale Step High",
              "Stack Seconds High", "Condition 2", "Condition Value 2",
              "Condition Value High", "Trigger Cooldown", "Event Value",
              "Ailment", "Damage Share"]
    SHIELD = "Positive_Double_your_energy_shield"
    SHIELD_WORDS = "Double your energy shield"

    def book(self, tmp_path, rows):
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "enchantment_effects.xlsx",
            {"Enchantments": self.ENCHANTMENTS,
             "Enchantment Effects": [self.HEADER] + rows}))

    def row(self, changes=None):
        values = {"Enchantment": self.SHIELD, "Effect": self.SHIELD_WORDS,
                  "Stat": "max_energy_shield", "Value Kind": "more",
                  "Value Low": 100}
        values.update(changes or {})
        return [values.get(column) for column in self.HEADER]

    def test_a_row_becomes_one_csv_row(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row()]))

        assert out == [{
            "Name": f"{self.SHIELD}#1", "Enchantment": self.SHIELD,
            "Stat": "max_energy_shield", "ValueKind": "more",
            "ValueLow": 100.0, "ValueHigh": 100.0, "RequiredTags": "",
            "Condition": "", "ConditionValue": 0.0, "Scale": "",
            "ScaleStep": 0.0, "Action": "", "ActionEvent": "",
            "FractionOf": "", "ScaleMaxSteps": 0, "StackSeconds": 0.0, "ScaleOffset": 0.0,
            "EverySeconds": 0.0, "EveryNth": 0, "ScaleStepHigh": 0.0,
            "StackSecondsHigh": 0.0, "Condition2": "", "ConditionValue2": 0.0,
            "ConditionValueHigh": 0.0, "TriggerCooldown": 0.0, "EventValue": 0.0,
            "Ailment": "", "DamageShare": 0.0}]

    # A ROW'S OWN STACKS. Issue #1833: the Action Event grants one, Stack
    # Seconds is how long they last and Scale Max Steps the cap.
    OWN = {"Scale": "own_stacks", "Scale Step": 1, "Action Event": "critical_strike",
           "Stack Seconds": 5, "Scale Max Steps": 5}

    def test_own_stacks_are_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(self.OWN)]))
        assert (out[0]["Scale"], out[0]["ActionEvent"], out[0]["StackSeconds"],
                out[0]["ScaleMaxSteps"]) == ("own_stacks", "critical_strike", 5.0, 5)

    def test_own_stacks_with_no_event_are_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="to grant them"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.OWN, "Action Event": None})]))

    def test_own_stacks_on_an_event_the_game_does_not_record_are_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="to grant them"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.OWN, "Action Event": "sneeze"})]))

    def test_own_stacks_with_no_seconds_are_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="no Stack Seconds"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.OWN, "Stack Seconds": None})]))

    def test_own_stacks_with_no_cap_are_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="no cap"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.OWN, "Scale Max Steps": None})]))

    def test_stack_seconds_on_another_scale_are_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="would\\s+be dropped|would be dropped"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {"Scale": "debuffs_carried", "Scale Step": 1, "Stack Seconds": 5})]))

    # HITS IN A ROW ON ONE ENEMY. Issue #1833, phase 2: counted on hit_dealt,
    # capped by Scale Max Steps, and with no timer.
    CONSECUTIVE = {"Scale": "consecutive_hits", "Scale Step": 1,
                   "Action Event": "hit_dealt", "Scale Max Steps": 8}

    def test_consecutive_hits_are_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(self.CONSECUTIVE)]))
        assert (out[0]["Scale"], out[0]["ActionEvent"], out[0]["StackSeconds"],
                out[0]["ScaleMaxSteps"]) == ("consecutive_hits", "hit_dealt", 0.0, 8)

    @pytest.mark.parametrize("event", [None, "critical_strike", "hit_taken"])
    def test_consecutive_hits_on_an_event_naming_nobody_struck_are_refused(
            self, tmp_path, event):
        with pytest.raises(gen.DataError, match="naming who was struck"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.CONSECUTIVE, "Action Event": event})]))

    def test_consecutive_hits_with_stack_seconds_are_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="has no timer"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.CONSECUTIVE, "Stack Seconds": 5})]))

    def test_consecutive_hits_with_no_cap_are_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="no cap"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.CONSECUTIVE, "Scale Max Steps": None})]))

    def test_consecutive_hits_counted_two_at_a_time_are_refused(self, tmp_path):
        with pytest.raises(gen.DataError):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.CONSECUTIVE, "Scale Step": 2})]))

    # AN EVERY-Nth ROW. Issue #1833, phase 2: the action names what it counts,
    # and Every Nth is N.
    NTH = {"Stat": None, "Value Kind": None, "Action": "nth_spell_mana_cost",
           "Value Low": 50, "Every Nth": 3}

    def test_an_every_nth_row_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(self.NTH)]))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["EveryNth"],
                out[0]["FractionOf"]) == ("nth_spell_mana_cost", "", 3, "")

    def test_an_every_nth_row_with_no_n_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="states no Every Nth"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.NTH, "Every Nth": None})]))

    @pytest.mark.parametrize("n", [1, 2.5, 101])
    def test_an_every_nth_row_with_an_n_outside_its_bounds_is_refused(
            self, tmp_path, n):
        with pytest.raises(gen.DataError, match="whole number from 2"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.NTH, "Every Nth": n})]))

    @pytest.mark.parametrize("column, written", [
        ("Action Event", "spell"), ("Scale", "debuffs_carried"),
        ("Fraction Of", "maximum"), ("Value Kind", "increased")])
    def test_an_every_nth_row_refuses_a_column_it_cannot_use(self, tmp_path,
                                                              column, written):
        with pytest.raises(gen.DataError, match="must be\\s+empty|must be empty"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.NTH, column: written})]))

    def test_an_every_nth_row_with_stack_seconds_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="no window"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.NTH, "Stack Seconds": 5})]))

    def test_every_nth_on_another_row_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="not an every-Nth action"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {"Every Nth": 3})]))

    def test_an_attack_that_deals_no_damage_is_written_100(self, tmp_path):
        attack = {**self.NTH, "Action": "nth_attack_no_damage", "Value Low": 100,
                  "Every Nth": 10}
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(attack)]))
        assert (out[0]["ValueLow"], out[0]["EveryNth"]) == (100, 10)
        with pytest.raises(gen.DataError, match="No damage is all"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**attack, "Value Low": 50})]))

    # A COOLDOWN REDUCTION. Issue #1833, the cooldown reduction action: an
    # action row whose value is seconds, above 0 and up to 60.
    REDUCE = {"Stat": None, "Value Kind": None, "Action": "cooldown_reduce_all",
              "Action Event": "resource_empty", "Value Low": 4}

    def test_a_cooldown_reduction_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(self.REDUCE)]))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["FractionOf"]) == ("cooldown_reduce_all", "resource_empty", 4.0, "")

    @pytest.mark.parametrize("seconds", [0, -1, 61])
    def test_a_cooldown_reduction_outside_its_bounds_is_refused(self, tmp_path, seconds):
        with pytest.raises(gen.DataError, match="above 0 and up\\s+to 60|above 0 and up to 60"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.REDUCE, "Value Low": seconds})]))

    def test_a_cooldown_reduction_of_exactly_60_seconds_is_accepted(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(
            {**self.REDUCE, "Value Low": 60})]))
        assert out[0]["ValueLow"] == 60.0

    def test_a_cooldown_reduction_with_no_event_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="names no event"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.REDUCE, "Action Event": None})]))

    def test_a_next_spell_cooldown_charge_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(
            {"Stat": None, "Value Kind": None, "Action": "next_spell_cooldown_reduced",
             "Action Event": "spell", "Value Low": 1.5, "Scale Max Steps": 1})]))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ValueLow"],
                out[0]["ScaleMaxSteps"]) == ("next_spell_cooldown_reduced", "spell", 1.5, 1)

    # A STACK PLACED ON THE OTHER CHARACTER. Issue #1833, phase 2: an action
    # row whose event names that character; Stack Seconds is required, the cap
    # is not.
    PLACED = {"Stat": None, "Value Kind": None, "Action": "enemy_armor_removed",
              "Action Event": "hit_dealt", "Stack Seconds": 5, "Scale Max Steps": 6}

    def test_a_placed_row_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(self.PLACED)]))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["StackSeconds"],
                out[0]["ScaleMaxSteps"], out[0]["FractionOf"]) == (
                    "enemy_armor_removed", "hit_dealt", 5.0, 6, "")

    def test_a_placed_row_with_no_cap_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(
            {**self.PLACED, "Scale Max Steps": None})]))
        assert out[0]["ScaleMaxSteps"] == 0

    @pytest.mark.parametrize("action, event", [
        ("enemy_armor_removed", "melee_hit_taken"),
        ("enemy_armor_removed", None),
        ("attacker_damage_removed", "hit_dealt"),
        ("attacker_damage_removed", "hit_taken")])
    def test_a_placed_row_on_an_event_naming_nobody_it_places_on_is_refused(
            self, tmp_path, action, event):
        with pytest.raises(gen.DataError, match="naming the character"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.PLACED, "Action": action, "Action Event": event})]))

    def test_a_placed_row_with_no_seconds_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="no Stack Seconds"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.PLACED, "Stack Seconds": None})]))

    @pytest.mark.parametrize("column, written", [
        ("Scale", "debuffs_carried"), ("Fraction Of", "maximum"),
        ("Value Kind", "increased")])
    def test_a_placed_row_refuses_a_column_it_cannot_use(self, tmp_path,
                                                         column, written):
        with pytest.raises(gen.DataError, match="must be\\s+empty|must be empty"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.PLACED, column: written})]))

    # A CHARGE THE NEXT USE SPENDS. Issue #1833, phase 2: an action row whose
    # event grants a charge, worth its value as increased damage, capped by
    # Scale Max Steps.
    NEXT = {"Stat": None, "Value Kind": None, "Action": "next_skill_damage",
            "Action Event": "dodge", "Scale Max Steps": 1}

    def test_a_next_use_row_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(self.NEXT)]))
        assert (out[0]["Action"], out[0]["ActionEvent"], out[0]["ScaleMaxSteps"],
                out[0]["FractionOf"], out[0]["Stat"]) == (
                    "next_skill_damage", "dodge", 1, "", "")

    def test_a_next_use_row_with_no_event_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="names no event to grant it on"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.NEXT, "Action Event": None})]))

    def test_a_next_use_row_with_no_cap_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="states no Scale Max Steps"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.NEXT, "Scale Max Steps": None})]))

    @pytest.mark.parametrize("column, written", [
        ("Scale", "debuffs_carried"), ("Fraction Of", "maximum"),
        ("Value Kind", "increased")])
    def test_a_next_use_row_refuses_a_column_it_cannot_use(self, tmp_path,
                                                           column, written):
        with pytest.raises(gen.DataError, match="must be empty"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.NEXT, column: written})]))

    def test_two_rows_of_one_next_use_action_on_one_enchantment_are_refused(
            self, tmp_path):
        with pytest.raises(gen.DataError, match="two rows of the same next-use"):
            gen.enchantment_effects(self.book(tmp_path, [
                self.row(self.NEXT),
                self.row({**self.NEXT, "Action Event": "block"})]))

    # A ROW THAT GRANTS ON A CLOCK. Issue #1833, timed grants: the event is
    # every_seconds and Every Seconds is the period.
    TIMED = {"Action Event": "every_seconds", "Every Seconds": 8}

    def test_a_timed_next_use_row_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(
            {**self.NEXT, **self.TIMED})]))
        assert (out[0]["ActionEvent"], out[0]["EverySeconds"]) == ("every_seconds", 8.0)

    def test_a_timed_own_stack_row_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(
            {**self.OWN, **self.TIMED})]))
        assert (out[0]["Scale"], out[0]["ActionEvent"], out[0]["EverySeconds"]) == (
            "own_stacks", "every_seconds", 8.0)

    def test_every_seconds_with_no_period_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="states no Every Seconds"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.NEXT, "Action Event": "every_seconds"})]))

    def test_a_period_on_any_other_event_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="Only every_seconds reads a period"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.NEXT, "Every Seconds": 8})]))

    @pytest.mark.parametrize("period", [0, 61])
    def test_a_period_outside_its_bounds_is_refused(self, tmp_path, period):
        with pytest.raises(gen.DataError, match="A period is above 0"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {**self.NEXT, **self.TIMED, "Every Seconds": period})]))

    def test_every_seconds_is_not_an_event_the_game_fires_by_name(self):
        """Issue #1833: the timed event is accepted beside the named ones, and is
        kept out of the list the engine's named `ActOnEvent` calls are held to."""
        assert gen.TIMED_EVENT == "every_seconds"
        assert gen.TIMED_EVENT not in gen.action_events()
        assert gen.TIMED_EVENT in gen.granting_events()

    def test_a_next_skill_effectiveness_row_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row(
            {**self.NEXT, "Action": "next_skill_effectiveness"})]))
        assert out[0]["Action"] == "next_skill_effectiveness"

    def test_an_event_on_a_stat_row_that_counts_no_stacks_is_refused(self, tmp_path):
        """Before issue #1833 such an event was read by nothing and dropped."""
        with pytest.raises(gen.DataError, match="own_stacks"):
            gen.enchantment_effects(self.book(tmp_path, [self.row(
                {"Action Event": "critical_strike"})]))

    # AN OFFSET ON THE CLASS POINT SCALE. Issue #1686: "above 100" is 100.
    def test_an_offset_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row({
            "Scale": "class_points_spent", "Scale Step": 10,
            "Scale Offset": 100})]))
        assert out[0]["ScaleOffset"] == 100.0

    def test_an_offset_on_another_scale_is_refused(self, tmp_path):
        """The engine reads an offset on one scale only, so anywhere else it
        would be dropped with no error."""
        with pytest.raises(gen.DataError, match="would be dropped"):
            gen.enchantment_effects(self.book(tmp_path, [self.row({
                "Scale": "debuffs_carried", "Scale Step": 1,
                "Scale Offset": 100})]))

    def test_an_offset_with_no_scale_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="would be dropped"):
            gen.enchantment_effects(self.book(tmp_path, [self.row({
                "Scale Offset": 100})]))

    def test_an_offset_past_the_point_budget_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="from 0 to 230"):
            gen.enchantment_effects(self.book(tmp_path, [self.row({
                "Scale": "class_points_spent", "Scale Step": 10,
                "Scale Offset": 231})]))

    # A CAP ON A SCALE. Issue #1815: "up to 10 stacks" is a cap of 10 steps.
    def test_a_cap_is_carried_through(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row({
            "Scale": "debuffs_carried", "Scale Step": 1,
            "Scale Max Steps": 5})]))
        assert out[0]["ScaleMaxSteps"] == 5

    def test_a_cap_with_no_scale_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="caps nothing"):
            gen.enchantment_effects(self.book(tmp_path, [self.row({
                "Scale Max Steps": 5})]))

    def test_a_cap_that_is_not_a_whole_number_of_steps_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="whole number of steps"):
            gen.enchantment_effects(self.book(tmp_path, [self.row({
                "Scale": "debuffs_carried", "Scale Step": 1,
                "Scale Max Steps": 2.5})]))

    # AN ACTION ROW MOVES A POOL WHEN AN EVENT HAPPENS, rather than changing a
    # stat. Issue #1815. Every refusal below is tested because a refusal that
    # does not fire leaves a row that validates, writes, loads and grants
    # nothing, with nothing anywhere saying so.

    ACTION_ENCHANTMENT = "Positive_Blocking_an_attack_restores_3_6_of_your_maximu"
    ACTION_WORDS = "Blocking an attack restores 3%-6% of your maximum HP"

    def action_row(self, changes=None):
        """A row restoring health on a block, with the stat columns empty."""
        values = {
            "Enchantment": self.ACTION_ENCHANTMENT,
            "Effect": self.ACTION_WORDS,
            "Stat": None, "Value Kind": None,
            "Value Low": 3, "Value High": 6,
            "Action": "health", "Action Event": "block",
            "Fraction Of": "maximum",
        }
        values.update(changes or {})
        return self.row(values)

    def test_an_action_row_carries_its_pool_event_and_base(self, tmp_path):
        out = gen.enchantment_effects(
            self.book(tmp_path, [self.action_row()]))

        assert out[0]["Action"] == "health"
        assert out[0]["ActionEvent"] == "block"
        assert out[0]["FractionOf"] == "maximum"
        assert out[0]["Stat"] == ""
        assert out[0]["ValueLow"] == 3.0 and out[0]["ValueHigh"] == 6.0

    def test_an_empty_base_means_the_maximum(self, tmp_path):
        # MOST SENTENCES SAY MAXIMUM OR SAY NOTHING, so the empty column is the
        # common case and must not be left blank in the written row -- the game
        # would have no name to match and would grant nothing.
        out = gen.enchantment_effects(
            self.book(tmp_path, [self.action_row({"Fraction Of": ""})]))

        assert out[0]["FractionOf"] == "maximum"

    def test_a_row_naming_a_stat_and_an_action_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="does one or the other"):
            gen.enchantment_effects(self.book(tmp_path, [self.action_row(
                {"Stat": "max_health", "Value Kind": "increased"})]))

    def test_a_row_naming_neither_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="names no stat and no"):
            gen.enchantment_effects(self.book(tmp_path, [self.action_row(
                {"Action": ""})]))

    def test_a_pool_the_game_does_not_have_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="which is not one the game has"):
            gen.enchantment_effects(self.book(tmp_path, [self.action_row(
                {"Action": "stamina"})]))

    def test_an_action_with_no_event_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="names no event"):
            gen.enchantment_effects(self.book(tmp_path, [self.action_row(
                {"Action Event": ""})]))

    def test_an_event_the_game_does_not_record_is_refused(self, tmp_path):
        # THIS USED TO USE A CRITICAL STRIKE as its example of an event the
        # game does not record. It records one now, so the example had to
        # change -- which is the test noticing the vocabulary grew rather than
        # quietly passing on a name that had become real.
        with pytest.raises(gen.DataError, match="which the game does not record"):
            gen.enchantment_effects(self.book(tmp_path, [self.action_row(
                {"Action Event": "sneezes_twice"})]))

    def test_an_event_with_no_clock_of_its_own_is_accepted(self, tmp_path):
        # FIVE EVENTS HAVE NO CLOCK, because every clock is something done TO
        # the character and these are things the character DID.
        # AND THE ONE THAT NEEDS A THRESHOLD IS GIVEN ONE: health_falls_below
        # requires an Event Value, issue #1833 group D part 2.
        for event in gen.ACTION_ONLY_EVENTS:
            changes = {"Action Event": event}
            if event == gen.THRESHOLD_EVENT:
                changes["Event Value"] = 10
            out = gen.enchantment_effects(self.book(tmp_path, [
                self.action_row(changes)]))
            assert out[0]["ActionEvent"] == event

    def test_a_hit_dealt_is_not_a_hit_taken(self):
        # THE SAME WORD AT OPPOSITE ENDS OF ONE BLOW. `hit_taken` is stamped on
        # the character that was hit; `hit_dealt` fires on the one that hit. A
        # test exists for this because the two are one character apart in
        # writing and a whole side of the blow apart in meaning.
        events = gen.action_events()
        assert "hit_dealt" in events and "hit_taken" in events
        assert "hit_dealt" in gen.ACTION_ONLY_EVENTS
        assert "hit_taken" not in gen.ACTION_ONLY_EVENTS

    def test_a_base_that_is_neither_maximum_nor_current_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="takes its percentage of"):
            gen.enchantment_effects(self.book(tmp_path, [self.action_row(
                {"Fraction Of": "missing"})]))

    def test_an_action_row_carrying_a_value_kind_is_refused(self, tmp_path):
        # THE THREE BUCKETS MULTIPLY A STAT and an action has no stat, so a
        # bucket here would be read by nothing.
        with pytest.raises(gen.DataError, match="must be empty on an action row"):
            gen.enchantment_effects(self.book(tmp_path, [self.action_row(
                {"Value Kind": "more"})]))

    def test_an_action_row_may_carry_a_condition(self, tmp_path):
        # THIS WAS A REFUSAL AND IS NOW A PERMISSION. It was refused while
        # nothing judged a condition at the moment an action fires; the change
        # that added the five clockless events judges it there, so
        # "killing an enemy while below 30% HP" can be written.
        #
        # AND THE NAME IS A REAL ONE. This test first used a made-up condition
        # and passed anyway, because the refusal it was written against fired
        # before anything looked the name up. With the refusal gone the name is
        # checked, which is the rule underneath doing its job.
        out = gen.enchantment_effects(self.book(tmp_path, [self.action_row(
            {"Condition": "health_below", "Condition Value": 30})]))

        assert out[0]["Condition"] == "health_below"
        assert out[0]["ConditionValue"] == 30.0
        assert out[0]["Action"] == "health"

    def test_an_action_row_carrying_a_scale_is_still_refused(self, tmp_path):
        # A SCALE SIZES A STAT'S MODIFIER and an action has no stat, so this one
        # stays refused while the condition beside it does not.
        with pytest.raises(gen.DataError, match="an action has no stat"):
            gen.enchantment_effects(self.book(tmp_path, [self.action_row(
                {"Scale": "momentum_stacks", "Scale Step": 1})]))

    def test_an_action_row_may_carry_required_tags(self, tmp_path):
        # TWO ROWS ARE SCOPED: "melee kills" and "strike skills ... on hit".
        # Type.Melee is on 31 of the 403 weapon skills and Type.Strike on 31,
        # since issue #944 gave The Whole Weight the melee tag it lacked (30 and
        # 31, measured 2026-09-14).
        out = gen.enchantment_effects(self.book(tmp_path, [self.action_row(
            {"Required Tags": "Type.Melee", "Action Event": "kill"})]))

        assert out[0]["RequiredTags"] == "Type.Melee"
        assert out[0]["ActionEvent"] == "kill"

    def test_a_fraction_of_the_events_own_amount_is_accepted(self, tmp_path):
        # "Skills that cost HP restore that amount as mana" is a fraction of
        # what the event carried rather than of a pool. The health cost was the
        # only event carrying an amount until 2026-10-05; the two tests below
        # are the two that joined it.
        out = gen.enchantment_effects(self.book(tmp_path, [self.action_row(
            {"Action": "mana", "Action Event": "health_cost",
             "Fraction Of": "event_amount"})]))

        assert out[0]["FractionOf"] == "event_amount"

    def test_a_share_of_a_hit_dealt_is_accepted_and_may_fire_on_every_hit(self, tmp_path):
        # "You take 10%-20% of the damage dealt by your own point blank AOE
        # skills", issue #1833, 2026-10-05. A STATED COOLDOWN OF 0 SURVIVES: a
        # hit-fired event defaults to a quarter second, which would charge the
        # wearer for one enemy of a burst that struck five.
        out = gen.enchantment_effects(self.book(tmp_path, [self.action_row(
            {"Action Event": "hit_dealt", "Fraction Of": "event_amount",
             "Trigger Cooldown": 0, "Required Tags": "Type.AOE.PointBlank"})]))

        assert (out[0]["ActionEvent"], out[0]["FractionOf"],
                out[0]["TriggerCooldown"], out[0]["RequiredTags"]) == (
            "hit_dealt", "event_amount", 0.0, "Type.AOE.PointBlank")

    def test_a_share_of_a_hit_dealt_waits_a_quarter_second_when_it_states_none(
            self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.action_row(
            {"Action Event": "hit_dealt", "Fraction Of": "event_amount"})]))

        assert out[0]["TriggerCooldown"] == gen.DEFAULT_TRIGGER_COOLDOWN

    def test_a_share_of_what_retaliation_dealt_is_accepted(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.action_row(
            {"Action Event": "retaliation_dealt", "Fraction Of": "event_amount",
             "Trigger Cooldown": 0})]))

        assert (out[0]["ActionEvent"], out[0]["FractionOf"],
                out[0]["TriggerCooldown"]) == ("retaliation_dealt", "event_amount", 0.0)

    def test_a_fraction_of_an_amount_no_event_carries_is_refused(self, tmp_path):
        # A ROW ASKING FOR THE AMOUNT OF AN EVENT THAT CARRIES NONE would
        # resolve to nothing and say so nowhere, which is the silent failure
        # this table keeps producing.
        with pytest.raises(gen.DataError, match="carries no amount"):
            gen.enchantment_effects(self.book(tmp_path, [self.action_row(
                {"Action Event": "block", "Fraction Of": "event_amount"})]))

    def test_an_action_row_carrying_a_scale_is_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="would be dropped without"):
            gen.enchantment_effects(self.book(tmp_path, [self.action_row(
                {"Scale": "momentum_stacks", "Scale Step": 1})]))

    def test_the_event_vocabulary_is_the_clocks_plus_the_five_with_none(self):
        # THE CLOCK HALF IS STILL DERIVED, so a clock added later cannot be
        # missing from the actions. The other half is written out because those
        # events have no clock to derive from, and this pins the union so that
        # neither half can be dropped without a test saying so.
        clocks = {name[len("seconds_after_"):] for name in gen.CONDITIONS
                  if name.startswith("seconds_after_")}

        assert gen.action_events() == clocks | set(gen.ACTION_ONLY_EVENTS)
        assert clocks, "no clock names were derived at all"
        assert gen.ACTION_ONLY_EVENTS, "the clockless list is empty"
        assert not (clocks & set(gen.ACTION_ONLY_EVENTS)), (
            "an event is in both halves, so one of them is wrong")

    def test_every_event_that_carries_an_amount_is_an_event(self):
        # A NAME IN THE SHORTER LIST THAT IS NOT IN THE LONGER ONE would refuse
        # every row using it and look like a rule rather than a typo.
        assert set(gen.EVENTS_WITH_AN_AMOUNT) <= gen.action_events()

    def test_two_actions_on_one_enchantment_both_survive(self, tmp_path):
        # THE ACTION COLUMNS ARE PART OF THE DUPLICATE KEY. Without them these
        # two would read as one row written twice, because both have an empty
        # stat, condition and scale.
        out = gen.enchantment_effects(self.book(tmp_path, [
            self.action_row(),
            self.action_row({"Action": "class_resource"}),
        ]))

        assert [r["Action"] for r in out] == ["health", "class_resource"]

    def test_two_action_rows_that_are_the_same_are_refused(self, tmp_path):
        with pytest.raises(gen.DataError, match="the same stat twice"):
            gen.enchantment_effects(self.book(tmp_path, [
                self.action_row(), self.action_row()]))

    def test_two_stats_on_one_enchantment_both_survive(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [
            self.row(),
            self.row({"Stat": "max_health", "Value Kind": "increased",
                      "Value Low": 5}),
        ]))

        assert [r["Name"] for r in out] == [f"{self.SHIELD}#1",
                                            f"{self.SHIELD}#2"]

    def test_a_drawback_can_be_named(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row({
            "Enchantment": "Negative_You_have_20_less_hp",
            "Effect": "You have 20% less hp.", "Stat": "max_health",
            "Value Low": -20})]))

        assert out[0]["Enchantment"] == "Negative_You_have_20_less_hp"
        assert out[0]["ValueLow"] == -20.0

    def test_a_condition_is_carried_across(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row({
            "Condition": "health_below", "Condition Value": 30})]))

        assert (out[0]["Condition"], out[0]["ConditionValue"]) == \
            ("health_below", 30.0)

    def test_an_enchantment_that_does_not_exist_is_refused(self, tmp_path):
        book = self.book(tmp_path, [self.row({"Enchantment": "Positive_Ghost"})])
        with pytest.raises(gen.DataError, match="no enchantment is called"):
            gen.enchantment_effects(book)

    def test_words_that_disagree_with_the_enchantment_are_refused(self,
                                                                  tmp_path):
        """A reworded enchantment must be read again before its numbers are
        trusted, so a stale copy of its words is refused."""
        book = self.book(tmp_path, [self.row({
            "Effect": "Triple your energy shield"})])
        with pytest.raises(gen.DataError, match="must agree"):
            gen.enchantment_effects(book)

    # A SET'S ROWS, AND THE RULE THAT THEY ARE WRITTEN WHOLE OR NOT AT ALL. A
    # set's first bonus and its drawback turn on together at the same threshold,
    # which the project owner ruled on 2026-09-08, so writing one without the
    # other would be a bonus with no cost or a cost with no bonus.
    SET_BONUS = "Positive_Archon_s_Aegis_2_Piece_Bonus_Your_block_chanc"
    SET_BONUS_WORDS = ("Archon's Aegis (2-Piece Bonus): Your block chance is "
                       "increased by 25%")
    SET_SIX_WORDS = "Archon's Aegis (6-Piece Bonus): Your block value is doubled"
    SET_DRAWBACK = "Negative_Your_movement_speed_is_reduced_by_10"
    SET_DRAWBACK_WORDS = "Your movement speed is reduced by 10%"

    def set_bonus(self, changes=None):
        values = {"Enchantment": self.SET_BONUS, "Effect": self.SET_BONUS_WORDS,
                  "Stat": "block_chance", "Value Kind": "increased",
                  "Value Low": 25}
        values.update(changes or {})
        return self.row(values)

    def set_drawback(self):
        return self.row({"Enchantment": self.SET_DRAWBACK,
                         "Effect": self.SET_DRAWBACK_WORDS,
                         "Stat": "movement_speed", "Value Kind": "increased",
                         "Value Low": -10})

    def set_six_piece(self):
        return self.row({"Enchantment": gen.row_name("Positive",
                                                     self.SET_SIX_WORDS[:48]),
                         "Effect": self.SET_SIX_WORDS, "Stat": "block_chance",
                         "Value Kind": "more", "Value Low": 100})

    def test_a_whole_set_is_accepted(self, tmp_path):
        out = gen.enchantment_effects(self.book(
            tmp_path, [self.set_bonus(), self.set_drawback()]))

        assert [row["Enchantment"] for row in out] == [self.SET_BONUS,
                                                       self.SET_DRAWBACK]
        assert [row["ValueLow"] for row in out] == [25.0, -10.0]

    def test_a_set_bonus_without_its_drawback_is_refused(self, tmp_path):
        book = self.book(tmp_path, [self.set_bonus()])
        with pytest.raises(gen.DataError, match="its drawback"):
            gen.enchantment_effects(book)

    def test_a_set_drawback_without_its_first_bonus_is_refused(self, tmp_path):
        book = self.book(tmp_path, [self.set_drawback()])
        with pytest.raises(gen.DataError, match="its first bonus"):
            gen.enchantment_effects(book)

    def test_a_higher_threshold_without_the_first_bonus_is_refused(self,
                                                                   tmp_path):
        """A player only ever reaches the 6-piece bonus through pieces that
        already meet the 2-piece threshold, so writing it alone is the same
        fault as writing half a set."""
        book = self.book(tmp_path, [self.set_six_piece(), self.set_drawback()])
        with pytest.raises(gen.DataError, match="its first bonus"):
            gen.enchantment_effects(book)

    def test_a_whole_set_with_a_higher_threshold_is_accepted(self, tmp_path):
        out = gen.enchantment_effects(self.book(
            tmp_path,
            [self.set_bonus(), self.set_six_piece(), self.set_drawback()]))

        assert len(out) == 3

    def test_a_range_on_a_set_row_is_refused(self, tmp_path):
        """An item records only a set's lowest threshold row, so nothing
        records a roll for the 6-piece or 10-piece row."""
        book = self.book(tmp_path, [
            self.set_bonus({"Value Low": 20, "Value High": 30}),
            self.set_drawback()])
        with pytest.raises(gen.DataError, match="states one number"):
            gen.enchantment_effects(book)

    BLOCK = "Positive_Your_block_chance_is_increased_by_10_20"
    BLOCK_WORDS = "Your block chance is increased by 10%-20%"
    SLOWER = "Negative_Your_attack_speed_is_reduced_by_20_35"
    SLOWER_WORDS = "Your attack speed is reduced by 20%-35%"

    def test_a_range_its_words_state_is_kept(self, tmp_path):
        out = gen.enchantment_effects(self.book(tmp_path, [self.row({
            "Enchantment": self.BLOCK, "Effect": self.BLOCK_WORDS,
            "Stat": "block_chance", "Value Kind": "increased",
            "Value Low": 10, "Value High": 20})]))

        assert (out[0]["ValueLow"], out[0]["ValueHigh"]) == (10.0, 20.0)

    def test_a_drawback_range_carries_its_sign_in_the_sentences_order(
            self, tmp_path):
        """"Reduced by 20%-35%" is -20 at the lowest roll and -35 at the
        highest, so the top roll gives the larger reduction the hover text
        shows."""
        out = gen.enchantment_effects(self.book(tmp_path, [self.row({
            "Enchantment": self.SLOWER, "Effect": self.SLOWER_WORDS,
            "Stat": "attack_speed", "Value Kind": "increased",
            "Value Low": -20, "Value High": -35})]))

        assert (out[0]["ValueLow"], out[0]["ValueHigh"]) == (-20.0, -35.0)

    def test_a_range_its_words_do_not_state_is_refused(self, tmp_path):
        book = self.book(tmp_path, [self.row({"Value Low": 10,
                                              "Value High": 30})])
        with pytest.raises(gen.DataError, match="do not state in that order"):
            gen.enchantment_effects(book)

    def test_a_range_written_backwards_is_refused(self, tmp_path):
        book = self.book(tmp_path, [self.row({
            "Enchantment": self.BLOCK, "Effect": self.BLOCK_WORDS,
            "Stat": "block_chance", "Value Kind": "increased",
            "Value Low": 20, "Value High": 10})])
        with pytest.raises(gen.DataError, match="do not state in that order"):
            gen.enchantment_effects(book)

    def test_a_range_with_two_signs_is_refused(self, tmp_path):
        book = self.book(tmp_path, [self.row({
            "Enchantment": self.SLOWER, "Effect": self.SLOWER_WORDS,
            "Stat": "attack_speed", "Value Kind": "increased",
            "Value Low": 20, "Value High": -35})])
        with pytest.raises(gen.DataError, match="do not state in that order"):
            gen.enchantment_effects(book)

    def test_the_ranges_in_a_sentence_are_read_as_written(self):
        assert gen.enchantment_ranges(
            "a 20%-35% slow for 2-4 seconds") == [(20.0, 35.0), (2.0, 4.0)]
        assert gen.enchantment_ranges(
            "for every 100,000 - 500,000 kills") == [(100000.0, 500000.0)]
        assert gen.enchantment_ranges("You lose 1-4% max resistances") == \
            [(1.0, 4.0)]
        assert gen.enchantment_ranges(
            "Archon's Aegis (2-Piece Bonus): increased by 25%") == []

    def test_an_unknown_condition_is_refused(self, tmp_path):
        book = self.book(tmp_path, [self.row({"Condition": "while_dancing",
                                              "Condition Value": 3})])
        with pytest.raises(gen.DataError, match="cannot judge"):
            gen.enchantment_effects(book)

    def test_a_value_beside_a_condition_that_compares_nothing_is_refused(
            self, tmp_path):
        book = self.book(tmp_path, [self.row({"Condition": "while_bleeding",
                                              "Condition Value": 5})])
        with pytest.raises(gen.DataError, match="compares nothing"):
            gen.enchantment_effects(book)

    def test_an_unknown_scale_is_refused(self, tmp_path):
        book = self.book(tmp_path, [self.row({"Scale": "moons_held",
                                              "Scale Step": 1})])
        with pytest.raises(gen.DataError, match="cannot judge"):
            gen.enchantment_effects(book)

    def test_a_bad_value_kind_is_refused(self, tmp_path):
        book = self.book(tmp_path, [self.row({"Value Kind": "sideways"})])
        with pytest.raises(gen.DataError,
                           match="not flat, increased, more or removed"):
            gen.enchantment_effects(book)

    def test_a_removal_is_written_as_its_own_kind(self, tmp_path):
        """"You have no armor" is a removal: the game multiplies the finished
        armour by nothing. Issue #1791. The row states 1, which nothing reads."""
        book = self.book(tmp_path, [self.row({"Stat": "armor",
                                              "Value Kind": "removed",
                                              "Value Low": 1})])
        out = gen.enchantment_effects(book)

        assert (out[0]["Stat"], out[0]["ValueKind"]) == ("armor", "removed")
        assert out[0]["ValueLow"] == 1.0 and out[0]["ValueHigh"] == 1.0

    def test_mana_on_hit_may_only_be_removed(self, tmp_path):
        """The code reading `mana_on_hit` asks only whether it is removed, so an
        increase on it would be written and read by nothing. Issue #1791. The
        removal itself is accepted, which is the control."""
        book = self.book(tmp_path, [self.row({"Stat": "mana_on_hit",
                                              "Value Kind": "increased",
                                              "Value Low": 20})])
        with pytest.raises(gen.DataError, match="may only be removed"):
            gen.enchantment_effects(book)

        book = self.book(tmp_path, [self.row({"Stat": "mana_on_hit",
                                              "Value Kind": "removed",
                                              "Value Low": 1})])
        assert gen.enchantment_effects(book)[0]["ValueKind"] == "removed"

    def test_every_removal_only_stat_is_one_the_engine_records(self):
        """A removal is recorded on a character only for a stat with an attribute
        or on the engine's list of stats with none, and a stat that may only be
        removed has no attribute. So each must be on that list, or its removal
        row would be written and never reach the character."""
        unrecorded = sorted(gen.REMOVAL_ONLY_STATS - gen.stats_with_no_attribute())
        assert gen.REMOVAL_ONLY_STATS and not unrecorded, unrecorded

    def test_an_action_row_may_not_be_a_removal(self, tmp_path):
        """A removal is a kind of stat row, and an action row has no stat. The
        refusal `_check_pool_action` makes of any kind reaches this one too."""
        book = self.book(tmp_path, [self.action_row({"Value Kind": "removed"})])
        with pytest.raises(gen.DataError, match="must be empty on an action row"):
            gen.enchantment_effects(book)

    def test_the_same_stat_twice_on_one_enchantment_is_refused(self, tmp_path):
        book = self.book(tmp_path, [self.row(), self.row()])
        with pytest.raises(gen.DataError, match="same stat twice"):
            gen.enchantment_effects(book)

    def test_the_validator_reports_a_stat_nothing_supplies(self):
        tables = {"EnchantmentEffects": [
            {"Name": "X#1", "Enchantment": "X", "Stat": "ghost_stat",
             "ValueKind": "increased", "RequiredTags": ""}],
            "ClassStats": [{"Stat": "armor"}]}
        problems = gen.validate_enchantment_effects(tables, set())
        assert len(problems) == 1, problems
        assert "'ghost_stat' is not a stat" in problems[0]

    def test_a_flat_row_supplies_its_own_stat(self):
        """The retaliation radius has no class line and no attribute; the flat
        row in this sheet is what supplies it, as a flat passive row does."""
        tables = {"EnchantmentEffects": [
            {"Name": "X#1", "Enchantment": "X",
             "Stat": "retaliation_radius_metres", "ValueKind": "flat",
             "RequiredTags": ""}]}
        assert gen.validate_enchantment_effects(tables, set()) == []

    def test_a_removal_is_not_asked_for_a_base(self):
        """A removal multiplies nothing, so a stat only an affix supplies -- which
        this check does not count -- is one it may remove. Issue #1791: "You have
        no resistances." is eight such rows. The same stat on an increase is
        still refused, which is the control."""
        removal = {"Name": "X#1", "Enchantment": "X", "Stat": "resistance_war",
                   "ValueKind": "removed", "RequiredTags": ""}
        tables = {"EnchantmentEffects": [removal], "ClassStats": [{"Stat": "armor"}]}
        assert gen.validate_enchantment_effects(tables, set()) == []

        increase = dict(removal, ValueKind="increased")
        tables = {"EnchantmentEffects": [increase], "ClassStats": [{"Stat": "armor"}]}
        problems = gen.validate_enchantment_effects(tables, set())
        assert len(problems) == 1 and "'resistance_war' is not a stat" in problems[0]

    def test_the_validator_reports_an_undeclared_tag(self):
        tables = {"EnchantmentEffects": [
            {"Name": "X#1", "Enchantment": "X", "Stat": "armor",
             "ValueKind": "increased", "RequiredTags": "Not.A.Tag"}],
            "ClassStats": [{"Stat": "armor"}]}
        problems = gen.validate_enchantment_effects(tables, {"Slot.Aura"})
        assert problems == ["EnchantmentEffects/X#1: undefined tag Not.A.Tag"]

    def test_a_row_that_moves_a_pool_is_not_asked_for_a_stat(self):
        """A row moves a pool or changes a stat, never both, so a pool row's
        stat is empty by design, and `enchantment_effects` refuses a row naming
        both. This validator asked EVERY row for a stat with a base, so it
        refused every pool row: none had been authored until the eight rows of
        issue #1815 were run through the whole generator on 2026-09-16, and all
        eight failed here.

        THE TAG CHECK STILL APPLIES, which is the second half. A pool row honours
        its required tags, so an undeclared one matches no skill exactly as it
        would on a stat row."""
        pool_row = {"Name": "X#1", "Enchantment": "X", "Stat": "",
                    "ValueKind": "", "RequiredTags": "", "Action": "health"}
        assert gen.validate_enchantment_effects(
            {"EnchantmentEffects": [pool_row]}, set()) == []

        tagged = dict(pool_row, RequiredTags="Not.A.Tag")
        assert gen.validate_enchantment_effects(
            {"EnchantmentEffects": [tagged]}, {"Slot.Aura"}) == [
            "EnchantmentEffects/X#1: undefined tag Not.A.Tag"]


class TestAgainstTheRealWorkbook:
    def test_the_committed_csvs_are_current(self):
        if not gen.WORKBOOK.is_file():
            pytest.skip("design workbook not present")
        assert gen.main(["--check"]) == 0, (
            "game/Data/*.csv are out of date. "
            "Run: python tools/generate_datatables.py"
        )

    def test_the_closing_line_names_the_directory_it_wrote_to(
            self, tmp_path, capsys):
        """Issue #1487. This line read `Wrote N CSVs to game/Data/` whatever
        `--output-dir` was, because the option was added after the message.

        WHY IT MATTERS ENOUGH FOR A TEST. Generating into a scratch directory
        and comparing is the safe way to see what a workbook edit does, instead
        of writing over `game/Data/` and hoping to undo it. The old line said
        that run had written over the repository's data, so the next person
        either loses a `git status` establishing it had not, or believes it and
        restores files that were never touched.

        IT WRITES SOMEWHERE HARMLESS AND CHECKS IT LANDED THERE, so this also
        fails if `--output-dir` stops being honoured, not only if the message
        stops naming it.
        """
        if not gen.WORKBOOK.is_file():
            pytest.skip("design workbook not present")

        destination = tmp_path / "generated"
        assert gen.main(["--output-dir", str(destination)]) == 0

        written = sorted(path.name for path in destination.glob("*.csv"))
        assert written, (
            f"--output-dir {destination} produced no CSVs, so the message this "
            "test reads describes nothing.")

        printed = capsys.readouterr().out.splitlines()
        closing = printed[-1]
        assert str(destination) in closing, (
            f"the closing line names the wrong directory. It reads "
            f"{closing!r} and the files were written to {destination}.")
        assert "game/Data/" not in closing, (
            f"the closing line still names game/Data/ although --output-dir "
            f"sent the CSVs to {destination}. It reads {closing!r}. "
            "Issue #1487.")
        assert f"{len(written)} " in closing, (
            f"the closing line reads {closing!r} and {len(written)} CSVs were "
            "written, so the count does not describe the run.")

    def test_every_table_has_rows(self):
        if not gen.WORKBOOK.is_file():
            pytest.skip("design workbook not present")
        book = openpyxl.load_workbook(gen.WORKBOOK, data_only=True)
        for name, builder in gen.TABLES.items():
            assert len(builder(book)) > 0, f"{name} produced no rows"

    def enchantment_tables(self) -> dict[str, list[dict]]:
        if not gen.WORKBOOK.is_file():
            pytest.skip("design workbook not present")
        book = openpyxl.load_workbook(gen.WORKBOOK, data_only=True)
        return {name: gen.TABLES[name](book)
                for name in gen.ENCHANTMENT_TABLES}

    def test_every_enchantment_row_in_the_sheet_states_a_type(self):
        """Issue #1486, against the real sheet rather than a fixture."""
        assert gen.validate_enchantment_types(self.enchantment_tables()) == []

    def test_blanking_one_real_row_is_caught(self):
        """THE TEST ABOVE PASSES WHETHER THE CHECK WORKS OR NOT. It says the
        sheet is clean, which a check that inspected nothing would also say.
        This one puts the fault back into the real rows and confirms exactly one
        problem comes out naming exactly that row."""
        tables = self.enchantment_tables()
        broken = tables["EnchantmentsNegative"][0]
        assert broken["EnchantmentType"], (
            "the first negative row already has no type, so blanking it "
            "changes nothing and this test proves nothing")
        broken["EnchantmentType"] = ""

        problems = gen.validate_enchantment_types(tables)
        assert len(problems) == 1, problems
        assert broken["Name"] in problems[0]

    def test_the_set_rows_are_left_where_the_owner_put_them(self):
        """A set row's Weight is a set identifier of 5 to 18, not a rarity
        weight, and the project owner set those deliberately. Nothing in the
        type check may move one, so this states the counts and the identifiers
        rather than trusting that."""
        tables = self.enchantment_tables()
        expected = {"EnchantmentsPositive": 42, "EnchantmentsNegative": 14}
        for name, wanted in expected.items():
            sets = [row for row in tables[name]
                    if row["EnchantmentType"].strip().casefold() == "set"]
            assert len(sets) == wanted, (
                f"{name} holds {len(sets)} set rows, not {wanted}")
            identifiers = sorted({int(float(row["Weight"])) for row in sets})
            assert identifiers == list(range(5, 19)), (
                f"{name} set identifiers are {identifiers}, not 5 to 18")


class TestAnAtNPointsRowStatesItsThreshold:
    """ISSUE #1755. A node sentence's "At 4 points: ..." clause -- Set Stance's
    and Scarred Plate's -- is a row with a `Min Points` column: it applies from
    that many points in its own node, and then once rather than per point.

    OPTIONAL UNTIL THE ROWS ARRIVED. The column was declared in `OPTIONAL_COLUMNS`
    while the design workbook was with another session, so a sheet without it read
    every row as 0. The rows change of 2026-09-25 put it in the workbook, and it is
    required now like any other column.

    A THRESHOLD IS A WHOLE NUMBER OF POINTS FROM 1, and one the node cannot
    reach is reported, because no player could ever earn the row.
    """

    @staticmethod
    def sheet(rows: list[list]) -> list[list]:
        return [["Node", "Stat", "Value Kind", "Value Per Point",
                 "Min Points", "Required Tags", "Condition", "Condition Value",
                 "Scale", "Scale Step", "Option", "Reach Metres"]] + rows

    def book(self, tmp_path, rows: list[list]):
        return openpyxl.load_workbook(workbook_with(
            tmp_path / "threshold.xlsx", {"Passive Effects": self.sheet(rows)}))

    def test_a_threshold_reaches_the_output_and_an_empty_one_is_nought(self, tmp_path):
        rows = self.book(tmp_path, [
            ["Ravager_basic_spine_008", "armor", "increased", 1.5, None],
            ["Ravager_basic_spine_008", "crowd_control_resistance", "increased", 5, 4],
        ])
        out = gen.passive_effects(rows)

        assert [(r["Stat"], r["MinPoints"]) for r in out] == [
            ("armor", 0), ("crowd_control_resistance", 4)]

    def test_the_same_stat_per_point_and_from_a_threshold_are_two_rows(self, tmp_path):
        """The threshold is part of what makes a row distinct, since a node could
        grant a stat per point and the same stat again, whole, from four."""
        rows = self.book(tmp_path, [
            ["A_node", "armor", "increased", 1, None],
            ["A_node", "armor", "increased", 5, 4],
        ])
        assert len(gen.passive_effects(rows)) == 2

    @pytest.mark.parametrize("written", [0, -1, 2.5])
    def test_a_threshold_that_is_not_a_whole_number_from_one_is_refused(
            self, tmp_path, written):
        rows = self.book(tmp_path, [
            ["A_node", "armor", "increased", 5, written],
        ])
        with pytest.raises(gen.DataError, match="whole number of points from 1"):
            gen.passive_effects(rows)

    def test_a_sheet_without_the_column_is_refused_now_the_rows_have_it(self, tmp_path):
        """The column was optional until Set Stance's and Scarred Plate's rows came.

        Until 2026-09-25 this read "a sheet without the column reads every row as
        nought", which was the committed workbook before the rows. The rows change
        put the column in the workbook and took it out of `OPTIONAL_COLUMNS`, so a
        sheet without it is now refused like any other column the generator reads.
        """
        book = openpyxl.load_workbook(workbook_with(
            tmp_path / "without.xlsx",
            {"Passive Effects": [["Node", "Stat", "Value Kind", "Value Per Point",
                                  "Required Tags", "Condition", "Condition Value",
                                  "Scale", "Scale Step", "Option", "Reach Metres"],
                                 ["A_node", "armor", "increased", 5]]}))
        with pytest.raises(gen.DataError, match="Min Points"):
            gen.passive_effects(book)

    def test_a_threshold_the_node_cannot_reach_is_reported(self):
        def tables(min_points):
            return {
                "PassiveEffects": [{"Name": "Real_node#1", "Node": "Real_node",
                                    "Stat": "armor", "ValueKind": "increased",
                                    "RequiredTags": "", "MinPoints": min_points}],
                "PassiveNodes": [{"Name": "Real_node", "MaxPoints": 8}],
                "ClassStats": [{"Stat": "armor"}],
            }
        assert gen.validate_passive_effects(tables(8), set()) == []
        problems = gen.validate_passive_effects(tables(9), set())
        assert len(problems) == 1 and "no player can ever earn it" in problems[0], problems


class TestRangesThatRollDown:
    """A sentence marks, by place, the ranges that roll from their second
    number to their first, and a marked range's effect pair leaves the generator
    written the other way round. Ruled 2026-10-05: a better roll gives the
    better outcome. "When a minion dies it automatically re-summons after 3-6
    seconds" is best at 3, and "This weapon has 5-20% more damage for every
    100,000-500,000 kills" is best at 20 and at 100,000, so only its second
    range is marked. Issue #1833."""

    WAIT_WORDS = "When a minion dies it automatically re-summons after 3-6 seconds"
    WAIT = gen.row_name("Positive", WAIT_WORDS[:48])
    KILLS_WORDS = "This weapon has 5-20% more damage for every 100,000-500,000 kills"
    KILLS = gen.row_name("Positive", KILLS_WORDS[:48])
    HEADER = TestScaleStepHigh.HEADER

    def sentences(self, wait_mark, kills_mark):
        return [
            ["Positives", "Type", "Weight", "Column 4", "Rolls Down",
             "Negatives", "Type", "Weight", "Tags", "Rolls Down"],
            [self.WAIT_WORDS, "Generic", 4, "Type.Minion", wait_mark,
             "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life", None],
            [self.KILLS_WORDS, "Generic", 4, "Item.Slot.Weapon", kills_mark,
             None, None, None, None, None],
        ]

    def effects(self, tmp_path, wait_mark=None, kills_mark=None):
        wait = {"Enchantment": self.WAIT, "Effect": self.WAIT_WORDS,
                "Stat": "minion_resummoned_after_seconds", "Value Kind": "flat",
                "Value Low": 3, "Value High": 6}
        kills = {"Enchantment": self.KILLS, "Effect": self.KILLS_WORDS,
                 "Stat": "attack_damage", "Value Kind": "more",
                 "Value Low": 5, "Value High": 20, "Scale": "weapon_kills",
                 "Scale Step": 100000, "Scale Step High": 500000}
        book = openpyxl.load_workbook(workbook_with(
            tmp_path / "rolls_down.xlsx",
            {"Enchantments": self.sentences(wait_mark, kills_mark),
             "Enchantment Effects": [self.HEADER]
             + [[row.get(column) for column in self.HEADER] for row in (wait, kills)]}))
        return {row["Enchantment"]: row for row in gen.enchantment_effects(book)}, book

    def test_an_unmarked_range_is_written_in_its_sentences_order(self, tmp_path):
        rows, book = self.effects(tmp_path)
        assert (rows[self.WAIT]["ValueLow"], rows[self.WAIT]["ValueHigh"]) == (3, 6)
        assert (rows[self.KILLS]["ScaleStep"], rows[self.KILLS]["ScaleStepHigh"]) == (100000, 500000)
        assert [row["RollsDown"] for row in gen.enchantments(book, negative=False)] == ["", ""]

    def test_a_marked_value_range_is_written_the_other_way_round(self, tmp_path):
        rows, book = self.effects(tmp_path, wait_mark="1")
        assert (rows[self.WAIT]["ValueLow"], rows[self.WAIT]["ValueHigh"]) == (6, 3)
        assert gen.enchantments(book, negative=False)[0]["RollsDown"] == "1"

    def test_only_the_marked_range_of_two_is_turned_round(self, tmp_path):
        """The weapon kill sentence states two ranges and marks the second: its
        step is turned round and its value is left as the sentence has it."""
        rows, book = self.effects(tmp_path, kills_mark=2)
        kills = rows[self.KILLS]
        assert (kills["ValueLow"], kills["ValueHigh"]) == (5, 20)
        assert (kills["ScaleStep"], kills["ScaleStepHigh"]) == (500000, 100000)
        # A CELL TYPED AS A NUMBER STILL READS AS A PLACE.
        assert gen.enchantments(book, negative=False)[1]["RollsDown"] == "2"

    def test_both_ranges_can_be_marked(self):
        assert gen.ranges_rolling_down("1, 2", self.KILLS_WORDS) == frozenset({1, 2})
        assert gen.ranges_rolling_down("", self.KILLS_WORDS) == frozenset()

    @pytest.mark.parametrize("cell", ["2", "0", "second", "1;2"])
    def test_a_place_the_sentence_does_not_have_is_refused(self, cell):
        """The wait sentence states one range, so only `1` names one."""
        with pytest.raises(gen.DataError, match="Rolls Down"):
            gen.ranges_rolling_down(cell, self.WAIT_WORDS, 7)
