"""Hovering an attribute on the character sheet says what each of its data rows gives.

Issue #2815. The owner, 2026-11-21: "do attributes actually do anything? I've
forgotten what each one is supposed to do so I can't tell in game since they
don't have descriptions". `UCataclysmCharacterSheetLayout::AttributeTooltipFor`
writes the per-point line from `DT_Attributes` and `DT_MinionScaling`, so the
words follow the rows. What that leaves to go wrong is a row whose stat or
minion family has no words: `NameFor` would then print the raw stat name, and a
family it does not know would read "minions tagged ...".

These checks read the CSVs and the C++ source as text. Continuous integration
never builds the game; `Cataclysm.AttributeTooltip.` in
`CataclysmAttributeTooltipTests.cpp` checks the built strings.

The design document's Attributes table is checked against the same rows: the
owner ruled on 2026-11-21 ("Keep it, just explain it") that the attributes stay
increases, and the coordinating session ruled that the table says so, since
"+0.5% crit chance" reads as a flat half point and the data is not.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO / "game" / "Data"
LAYOUT = (REPO / "game" / "Source" / "Cataclysm" / "Interface"
          / "CataclysmCharacterSheetLayout.cpp")
WIDGET = (REPO / "game" / "Source" / "Cataclysm" / "Interface"
          / "CataclysmCharacterSheetWidget.cpp")
DESIGN = REPO / "docs" / "Cataclysm_GDD_v2.md"


def rows_of(name: str) -> list[dict[str, str]]:
    with open(DATA / name, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def named_stats(source: str) -> set[str]:
    """The stats `NameFor`'s table gives a player's name: {TEXT("x"), TEXT("Y")}."""
    return set(re.findall(r'\{TEXT\("([a-z_]+)"\),\s*TEXT\("[^"]+"\)\}', source))


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index("\n}", start)]


def test_every_attribute_rows_stat_has_a_players_name():
    names = named_stats(LAYOUT.read_text(encoding="utf-8"))
    stats = {row["Stat"] for row in rows_of("Attributes.csv")}
    unnamed = sorted(stats - names)
    assert not unnamed, (
        f"these Attributes.csv stats would show as raw names in the tooltip: {unnamed}")
    assert len(stats) == 17, "CONTROL: the 17 attribute rows were read"


def test_every_minion_scaling_family_has_words():
    body = function_body(LAYOUT.read_text(encoding="utf-8"),
                         "FString MinionFamilyWords(")
    for row in rows_of("MinionScaling.csv"):
        assert f'TEXT("{row["RequiresTag"]}")' in body, (
            f"{row['Name']}: {row['RequiresTag']} has no words, so the tooltip "
            "would say 'minions tagged ...'")


def test_the_sheet_sets_each_attribute_buttons_tooltip():
    body = function_body(WIDGET.read_text(encoding="utf-8"),
                         "void UCataclysmCharacterSheetWidget::WriteStatus()")
    assert "SetToolTipText" in body and "AttributeTooltipFor(" in body


def test_the_design_table_states_each_row_as_an_increase():
    text = DESIGN.read_text(encoding="utf-8")
    start = text.index("| Attribute | Stats | Per Point |")
    table = text[start:text.index("\n\n", start)]
    cells = {}
    for line in table.splitlines()[2:]:
        parts = [part.strip() for part in line.strip("|").split("|")]
        cells[parts[0].lower()] = parts[2]
    for row in rows_of("Attributes.csv"):
        figure = f"{float(row['PercentPerPoint']):g}"
        expected = f"+{figure}% increased"
        assert expected in cells[row["Attribute"]], (
            f"{row['Name']}: the design table's {row['Attribute']} cell "
            f"{cells[row['Attribute']]!r} does not state {expected!r}")
    assert "cooldown increase" not in table, (
        "Efficacy's stat is cooldown_reduction, an increase of the reduction")
