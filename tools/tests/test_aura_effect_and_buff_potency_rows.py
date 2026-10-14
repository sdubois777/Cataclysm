"""The rows that make auras more effective and buffs more potent are ones the
generator carries through, and wrong rows on those stats are refused.

Ruled 2026-10-13 under the owner's delegation and built ahead of their rows.
Each sentence is one row, a `more` row rolled between the sentence's two
figures, with no Required Tags, no Condition and no Scale:

    "Your auras are 20%-50% more effective"             `aura_effect`

THE ROWS HERE ARE ON A MADE-UP SHEET holding the real sentences, as the
generator's other tests of a stat built ahead of its row are. No row of
`game/Data/EnchantmentEffects.csv` is on either stat, and this file does not
read that table. It does read `game/Data/EnchantmentsPositive.csv`, to hold the
row name the hand-back gives the session that writes rows.

WHY A ROW REQUIRING A TAG IS REFUSED. The game asks for each stat with the tags
of the skill whose figure it multiplies. "Your auras" is every skill of the
Aura shape, and Living Pyre is one that does not carry `Slot.Aura`; a row
requiring that tag would leave it out.

THE ENGINE'S NAME IS READ OUT OF THE SOURCE, with comment lines left out, so a
name spelled differently on the two sides fails here and not in play.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

import openpyxl

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

SKILL_TEMPLATES = (ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                   / "CataclysmSkillTemplates.cpp")
POSITIVES = ROOT / "game" / "Data" / "EnchantmentsPositive.csv"

#: The header of the Enchantment Effects sheet, as the generator's own tests
#: of that sheet write it.
HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low",
          "Value High", "Required Tags", "Condition", "Condition Value",
          "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
          "Scale Max Steps", "Stack Seconds", "Scale Offset",
          "Every Seconds", "Every Nth", "Scale Step High",
          "Stack Seconds High", "Condition 2", "Condition Value 2",
          "Condition Value High", "Trigger Cooldown", "Event Value",
          "Ailment", "Damage Share"]

#: Each stat with: its sentence, the two figures the sentence states, the name
#: of the engine constant that holds the stat's name and the class it is on,
#: and a tag a wrong row might require.
RULED = {
    "aura_effect": ("Your auras are 20%-50% more effective", 20.0, 50.0,
                    "UCataclysmAuraSkill", "AuraEffectStat", "Slot.Aura"),
}


def workbook_with(path: pathlib.Path, sheets: dict[str, list[list]]) -> pathlib.Path:
    """Build a workbook from {sheet name: rows}, with a Tags sheet."""
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheets = dict(sheets)
    sheets.setdefault("Tags", [["Tag Name", "Description"],
                               ["Element.War", "Physical"],
                               ["Slot.Ultimate", "Ultimates"]])
    for name, rows in sheets.items():
        sheet = book.create_sheet(name)
        for row in rows:
            sheet.append(row)
    book.save(path)
    return path


def carried(tmp_path: pathlib.Path, stat: str, **columns) -> list[dict]:
    """The generator's rows for a made-up sheet holding `stat`'s sentence and
    its ruled row, with any of the row's cells replaced by `columns`."""
    words, low, high = RULED[stat][:3]
    values = {"Enchantment": gen.row_name("Positive", words[:48]),
              "Effect": words, "Stat": stat, "Value Kind": "more",
              "Value Low": low, "Value High": high}
    values.update(columns)
    enchantments = [
        ["Positives", "Type", "Weight", "Column 4", None,
         "Negatives", "Type", "Weight", "Tags"],
        [words, "Generic", 3, "Type.AOE.Aura", None,
         "You have 20% less hp.", "Generic", 3, "Stat.Defense.Life"],
    ]
    return gen.enchantment_effects(openpyxl.load_workbook(workbook_with(
        tmp_path / f"{stat}.xlsx",
        {"Enchantments": enchantments,
         "Enchantment Effects": [HEADER,
                                 [values.get(column) for column in HEADER]]})))


def refusals(rows: list[dict]) -> list[str]:
    return gen.refuse_an_aura_or_buff_effect_row_the_game_cannot_read(
        "EnchantmentEffects", rows)


def code_lines(path: pathlib.Path) -> str:
    """The file with every line that opens a `//` comment left out."""
    return "\n".join(line for line in path.read_text(encoding="utf-8").splitlines()
                     if not line.lstrip().startswith("//"))


def test_the_list_holds_the_ruled_stats_with_their_kind_and_bounds():
    assert gen.AURA_AND_BUFF_EFFECT_STATS == {
        stat: ("more", low, high)
        for stat, (_, low, high, *_rest) in RULED.items()}


def test_each_sentences_row_is_carried_through_and_is_not_refused(tmp_path):
    for stat, (_words, low, high, *_rest) in RULED.items():
        out = carried(tmp_path, stat)
        assert [(row["Stat"], row["ValueKind"], row["ValueLow"],
                 row["ValueHigh"], row["RequiredTags"], row["Condition"],
                 row["Scale"], row["Action"]) for row in out] == [
            (stat, "more", low, high, "", "", "", "")]
        assert refusals(out) == []
        # NO CONDITION AND NO SCALE, so neither of those checks has anything
        # to say.
        assert gen.refuse_a_condition_nothing_asks_for(
            "EnchantmentEffects", out) == []
        assert gen.refuse_a_scale_nothing_asks_for(
            "EnchantmentEffects", out) == []


def test_each_row_is_named_as_the_table_of_sentences_names_its_sentence(tmp_path):
    """The made-up row's enchantment is the one `EnchantmentsPositive.csv`
    holds for the sentence, so the row a session writes from this shape lands
    on the real sentence. The control is that the table holds the sentence
    once."""
    with POSITIVES.open(encoding="utf-8", newline="") as handle:
        table = list(csv.DictReader(handle))
    for stat, (words, *_rest) in RULED.items():
        named = [row["Name"] for row in table if row["Effect"] == words]
        assert len(named) == 1, (stat, named)
        out = carried(tmp_path, stat)
        assert [row["Enchantment"] for row in out] == named


def test_each_stat_is_one_with_no_attribute():
    """What lets the rows above name them: none holds a gameplay attribute,
    so each name is on the engine's list of stats that need none. TESTED BY
    NAME. The control is a made-up name on no list."""
    for stat in RULED:
        assert stat in gen.stats_with_no_attribute()
    assert "aura_effectiveness" not in gen.stats_with_no_attribute()


def test_the_generators_names_are_the_engines():
    """Each stat's name is the one its engine constant holds. The control is
    that the reader finds the constant at all: a constant renamed or moved
    fails by name here, where it would otherwise leave nothing to compare."""
    code = code_lines(SKILL_TEMPLATES)
    for stat, (_words, _low, _high, owner, constant, _tag) in RULED.items():
        found = re.findall(
            rf'const TCHAR\* {owner}::{constant}\s*=\s*TEXT\("([^"]*)"\);', code)
        assert len(found) == 1, (stat, found)
        assert found == [stat]


def test_every_shape_the_reader_gives_another_meaning_is_refused(tmp_path):
    """One change to a carried row at a time, each refused once and by its
    own sentence. The control is the first line: the row unchanged is not
    refused."""
    for stat, (_words, low, high, _owner, _constant, tag) in RULED.items():
        row = carried(tmp_path, stat)[0]
        assert refusals([row]) == []
        for change, said in (
                ({"ValueKind": "increased"}, "value kind 'increased'"),
                ({"ValueKind": "flat"}, "value kind 'flat'"),
                ({"RequiredTags": tag}, "under a required tag"),
                ({"ValueLow": low - 1.0}, f"states {low - 1.0:g} on"),
                ({"ValueHigh": high + 1.0}, f"states {high + 1.0:g} on")):
            problems = refusals([dict(row, **change)])
            assert len(problems) == 1, (stat, change, problems)
            assert said in problems[0], (stat, change, problems)
            assert stat in problems[0]


def test_a_figure_inside_the_bounds_is_not_refused(tmp_path):
    """A row may state a narrower pair than its sentence's, and each bound
    itself is allowed."""
    for stat, (_words, low, high, *_rest) in RULED.items():
        row = carried(tmp_path, stat)[0]
        assert refusals([dict(row, ValueLow=low + 5.0, ValueHigh=high - 5.0)]) == []
        assert refusals([dict(row, ValueLow=low, ValueHigh=high)]) == []


def test_a_condition_or_a_scale_on_a_stat_is_refused_elsewhere(tmp_path):
    """None of the stats is on the list of stats asked for under a condition
    or the list asked for under a scale, so the two checks that read those
    lists refuse such a row. The control is the row unchanged."""
    for stat in RULED:
        row = carried(tmp_path, stat)[0]
        assert gen.refuse_a_condition_nothing_asks_for(
            "EnchantmentEffects", [row]) == []
        assert gen.refuse_a_scale_nothing_asks_for(
            "EnchantmentEffects", [row]) == []
        conditioned = gen.refuse_a_condition_nothing_asks_for(
            "EnchantmentEffects", [dict(row, Condition="health_below")])
        assert len(conditioned) == 1 and stat in conditioned[0]
        scaled = gen.refuse_a_scale_nothing_asks_for(
            "EnchantmentEffects", [dict(row, Scale="own_stacks")])
        assert len(scaled) == 1 and stat in scaled[0]


def test_a_passive_nodes_row_and_another_stat_are_left_alone():
    """The check reads enchantment rows. A passive node's row states a value
    per point, which the bounds do not describe; and a row on another stat is
    not this check's to judge."""
    for stat in RULED:
        node_row = {"Name": "Made_up_node#1", "Node": "Made_up_node",
                    "Stat": stat, "ValueKind": "increased",
                    "ValuePerPoint": 1.0}
        assert gen.refuse_an_aura_or_buff_effect_row_the_game_cannot_read(
            "PassiveEffects", [node_row]) == []
    assert refusals([{"Name": "X#1", "Stat": "buff_duration",
                      "ValueKind": "increased", "ValueLow": 1.0,
                      "ValueHigh": 1.0, "RequiredTags": "Type.Buff"}]) == []


def test_the_whole_sheets_validation_runs_the_check(tmp_path):
    """`validate_enchantment_effects` is what the generator runs over the
    finished rows. The row unchanged gives it nothing to say; the same row
    made an increase is refused there, in this check's words."""
    for stat in RULED:
        good = carried(tmp_path, stat)[0]
        assert gen.validate_enchantment_effects(
            {"EnchantmentEffects": [good]}, set()) == []
        problems = gen.validate_enchantment_effects(
            {"EnchantmentEffects": [dict(good, ValueKind="increased")]}, set())
        assert any("value kind 'increased'" in problem and stat in problem
                   for problem in problems), problems
