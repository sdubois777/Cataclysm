"""The row shape Brute's Heart's six-piece resistances need, through the generator.

Ruled 2026-10-18 by the coordinating session: "Brute's Heart 6's eight rows,
resistance_<type> increased 50 under health_below 50", after the engine reads a
resistance row under a condition (`UCataclysmDamageCalculation::TypedResistanceOf`
through `AttributeWithWhatWasNotFolded`). The sentence, "When your health falls
below 50%, you gain a powerful aura that taunts all nearby enemies and increases
your armor and resistances by 50%", already has its armour row (#1) and the
aura's four (#2 to #5); these are #6 to #13. The enchantment session authors the
rows; THIS FILE AUTHORS NOTHING. It builds the made-up workbook
`test_brutes_heart_six_rows.py` builds.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "tests"))

import generate_datatables as gen  # noqa: E402
from test_brutes_heart_six_rows import (  # noqa: E402
    ARMOUR_ROW,
    AURA_ROWS,
    SIX_PIECE,
    SIX_PIECE_WORDS,
    effects,
)

#: The eight damage types in the engine's order, `UCataclysmItemModifiers::DamageTypeNames`.
TYPES = ("War", "Demonic", "Death", "Pestilence", "Famine", "Celestial",
         "Chaos", "Void")
STATS = tuple(f"resistance_{name.lower()}" for name in TYPES)

#: The eight rows, cell by cell, as the enchantment session is to write them,
#: after the aura's four.
RESISTANCE_ROWS = [
    {"Enchantment": SIX_PIECE, "Effect": SIX_PIECE_WORDS, "Stat": stat,
     "Value Kind": "increased", "Value Low": 50, "Value High": 50,
     "Condition": "health_below", "Condition Value": 50}
    for stat in STATS]

ITEM_SOURCE = (ROOT / "game" / "Source" / "Cataclysm" / "Items"
               / "CataclysmItem.cpp")


def carried(tmp_path) -> list[dict]:
    """The eight carried rows, after the armour row and the aura's four."""
    return effects(tmp_path, ARMOUR_ROW, *AURA_ROWS, *RESISTANCE_ROWS)[5:]


def shipped_tables() -> dict[str, list[dict]]:
    """The shipped sheets the whole validation reads, as the CSVs hold them."""
    def load(name: str) -> list[dict]:
        path = ROOT / "game" / "Data" / f"{name}.csv"
        with path.open(encoding="utf-8-sig") as handle:
            return list(csv.DictReader(handle))
    return {name: load(name) for name in (
        "EnchantmentEffects", "PassiveEffects", "ClassStats", "Attributes",
        "ItemBases")}


def test_the_eight_stats_are_the_engines_eight_damage_types_in_order():
    """Read from `DamageTypeNames`. THE CONTROL is that the pattern finds the
    list and that it holds eight names."""
    source = ITEM_SOURCE.read_text(encoding="utf-8")
    block = re.search(r"DamageTypeNames\(\)\s*\{.*?Names = \{(?P<body>.*?)\};",
                      source, re.S)
    assert block, "DamageTypeNames() was not found in CataclysmItem.cpp"
    names = tuple(re.findall(r'TEXT\("(\w+)"\)', block.group("body")))
    assert len(names) == 8, names
    assert names == TYPES


def test_each_resistance_is_on_the_conditioned_list_asked_with_the_wearers_state():
    for stat in STATS:
        assert gen.CONDITIONED_STATS_WITH_AN_ASKER[stat] == gen.ASKER_PASSES_NOTHING_MORE
    assert "resistance_cap" not in gen.CONDITIONED_STATS_WITH_AN_ASKER


def test_the_eight_rows_are_carried_through_after_the_five_and_nothing_refuses_them(
        tmp_path):
    out = carried(tmp_path)
    assert [(row["Name"], row["Stat"], row["ValueKind"], row["ValueLow"],
             row["ValueHigh"], row["RequiredTags"], row["Condition"],
             row["ConditionValue"], row["Scale"], row["Action"])
            for row in out] == [
        (f"{SIX_PIECE}#{place}", stat, "increased", 50.0, 50.0, "",
         "health_below", 50.0, "", "")
        for place, stat in enumerate(STATS, start=6)]
    assert gen.refuse_a_condition_nothing_asks_for("EnchantmentEffects", out) == []
    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", out) == []
    assert gen.refuse_a_taunt_aura_row_the_game_cannot_read(
        "EnchantmentEffects", out) == []


def test_a_condition_about_a_blow_or_a_hit_is_still_refused_on_a_resistance(
        tmp_path):
    """The asker hands over the wearer's state and no blow, so a condition about
    the blow taken or a hit dealt would be refused every time. THE CONTROL is
    the ruled row, which the same check accepts."""
    (row, *_) = carried(tmp_path)
    assert gen.refuse_a_condition_nothing_asks_for("EnchantmentEffects", [row]) == []
    blow = gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", [dict(row, Condition="hit_is_spell")])
    assert len(blow) == 1 and "a blow being taken" in blow[0], blow
    hit = gen.refuse_a_condition_nothing_asks_for(
        "EnchantmentEffects", [dict(row, Condition="target_is_boss")])
    assert len(hit) == 1 and "a hit being dealt" in hit[0], hit


def test_a_scaled_resistance_row_is_still_refused(tmp_path):
    """The ruling put the eight on the conditioned list only. The engine adds a
    scaled row's difference too, but the generator still refuses one, so a
    scaled resistance row waits for its own ruling. THE CONTROL is the ruled
    row, unscaled."""
    (row, *_) = carried(tmp_path)
    assert gen.refuse_a_scale_nothing_asks_for("EnchantmentEffects", [row]) == []
    found = gen.refuse_a_scale_nothing_asks_for(
        "EnchantmentEffects", [dict(row, Condition="", ConditionValue=0.0,
                                    Scale="enemies_in_reach", ScaleStep=1.0)])
    assert len(found) == 1, found


def test_on_the_shipped_sheets_the_eight_rows_pass_the_whole_validation(tmp_path):
    """Added to the shipped Enchantment Effects rows and put through the whole
    validation, the eight draw no problem but the undefined tags of rows already
    shipped, which `known` is left empty for.

    THE CONTROL, AND WHAT IT SHOWS. An `increased` row needs a base under it,
    and no class line, attribute or item base supplies a resistance: a player's
    resistances come from affixes. On the shipped sheet the base check is met by
    the eight `flat` rows of "Enemies ignore 10-20% of your resistances", which
    supply each `resistance_<type>`. With those taken out, each of the eight is
    refused by the base check, once.

    THE SHIPPED #6 TO #13 ARE TAKEN OUT BEFORE THE EIGHT ARE ADDED, so the table
    holds each once whether or not the rows have shipped. Ruled 2026-10-18:
    appending alone counted each refusal twice once they shipped, 16 against 8.
    Once they have shipped they are first compared with the eight built here,
    cell by cell, so the test cannot pass on a table that differs from the one
    that ships."""
    new = carried(tmp_path)
    names = {row["Name"] for row in new}
    tables = shipped_tables()
    shipped = [row for row in tables["EnchantmentEffects"] if row["Name"] in names]
    if shipped:
        assert shipped == [{key: str(value) for key, value in row.items()}
                           for row in new]
    tables["EnchantmentEffects"] = [
        row for row in tables["EnchantmentEffects"]
        if row["Name"] not in names] + new
    found = [problem for problem in gen.validate_enchantment_effects(tables, set())
             if any(name in problem for name in names)]
    assert found == []

    flats = [row for row in tables["EnchantmentEffects"]
             if row["Stat"] in STATS and row["ValueKind"] == "flat"]
    assert len(flats) == 8, [row["Name"] for row in flats]
    tables["EnchantmentEffects"] = [row for row in tables["EnchantmentEffects"]
                                    if row not in flats]
    refused = [problem for problem in gen.validate_enchantment_effects(tables, set())
               if any(name in problem for name in names)]
    assert len(refused) == 8, refused
    assert all("is not a stat any class line" in problem for problem in refused)
