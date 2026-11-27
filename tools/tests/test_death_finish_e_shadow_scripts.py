"""The Death line's finishing slice E: the Shadow's last four vow options and Ever Moving, and their rows script.

2026-11-26. `tools/reapply_death_finish_e_shadow_workbook.py` writes six rows: Afterimage's flag, Shadow Clone's count and
seconds, Shadow Step's metres, Flurry of the Dead's extra strikes by the Fervour held, and Ever Moving's share of evasion
against area damage while moving. Each is a stat with no gameplay attribute, read by `UCataclysmShadowImages` or
`UCataclysmDamageCalculation::Resolve`.

These tests hold each figure to the words of its option's own sentence in `game/Data/PassiveNodes.csv`, hold that the
workbook and the generated table carry the rows, that the script is idempotent and refuses what somebody wrote since,
and that the engine reads the six names and lists them as having no attribute. The game's half is
`game/Source/Cataclysm/Tests/CataclysmShadowVowsTests.cpp`.
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

import reapply_death_finish_e_shadow_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
DATA = REPO_ROOT / "game" / "Data"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
SHADOW_IMAGES_CPP = SOURCE / "AbilitySystem" / "CataclysmShadowImages.cpp"
DAMAGE_CPP = SOURCE / "AbilitySystem" / "CataclysmDamageCalculation.cpp"
CLASS_STATS_CPP = SOURCE / "Character" / "CataclysmPlayerClassStats.cpp"

#: Each row, by (node, option, stat), and the words of its option's sentence that state its figure.
WORDS = {
    ("Shadow_capstone_25", 2, "afterimage_repeats_your_next_attack"): "repeats your next attack",
    ("Shadow_capstone_50", 2, "shadow_clone_every_elite_kills"): "Every third kill of an Elite or stronger",
    ("Shadow_capstone_50", 2, "shadow_clone_seconds"): "fights for 5 seconds",
    ("Shadow_capstone_100", 1, "movement_targets_an_enemy_within_metres"): "any enemy within 15 metres",
    ("Shadow_capstone_200", 3, "hits_strike_again"): "strike again once for every 10 Fervour held, up to 10 extra",
    ("Shadow_keystone_d_kB", 0, "evasion_against_area_percent"): "While you are moving",
}


def sentence_of(node: str, option: int) -> str:
    """The option's own sentence, or the node's description for option 0."""
    with open(DATA / "PassiveNodes.csv", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            if row["Name"] == node:
                return row[f"Option{option}Description"] if option else row["Description"]
    raise AssertionError(f"{node} is not in PassiveNodes.csv")


def wanted() -> dict[tuple[str, int, str], rows.base.Row]:
    return {(node, int(row.option or 0), row.stat): row for node, _, group in rows.NODES for row in group}


def test_the_script_lists_the_six_rows_and_each_names_an_option_of_its_node():
    assert set(wanted()) == set(WORDS)
    for node, option, _ in WORDS:
        assert sentence_of(node, option), (node, option)


@pytest.mark.parametrize("key", sorted(WORDS))
def test_each_figure_is_the_one_its_sentence_states(key):
    node, option, _ = key
    sentence = sentence_of(node, option)
    assert WORDS[key] in sentence, (key, sentence)
    row = wanted()[key]
    numbers = [float(n) for n in re.findall(r"\d+(?:\.\d+)?", sentence)]
    if key[2] == "afterimage_repeats_your_next_attack":
        assert row.value == 1 and not numbers, "a flag of one, on a sentence that states no figure"
    elif key[2] == "shadow_clone_every_elite_kills":
        assert row.value == 3, "every THIRD kill"
    elif key[2] == "hits_strike_again":
        # "once for every 10 Fervour held, up to 10 extra strikes": one a step of ten, ten steps at most.
        assert (row.value, row.scale, row.step, row.cap) == (1, "class_resource_held", 10, 10)
        assert numbers == [10.0, 10.0], numbers
    elif key[2] == "evasion_against_area_percent":
        # "at half its value", while moving.
        assert "at half its value" in sentence
        assert (row.value, row.cond) == (50, "while_moving")
    else:
        assert row.value in numbers, (row.value, sentence)


def test_the_workbook_holds_the_rows():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    with open(DATA / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        written = {(row["Node"], int(row["Option"]), row["Stat"]): row for row in csv.DictReader(handle)
                   if (row["Node"], int(row["Option"]), row["Stat"]) in WORDS}
    assert set(written) == set(WORDS)
    for key, row in wanted().items():
        line = written[key]
        assert (line["ValueKind"], float(line["ValuePerPoint"])) == (row.kind, float(row.value)), key
        assert line["Condition"] == (row.cond or ""), key
        assert line["Scale"] == (row.scale or ""), key
        assert float(line["ScaleStep"]) == float(row.step or 0), key
        assert int(line["ScaleMaxSteps"]) == int(row.cap or 0), key


def _workbook_without_ours(tmp_path) -> pathlib.Path:
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    ours = {(node, row.key()) for node, _, group in rows.NODES for row in group}
    for index in range(sheet.max_row, 1, -1):
        cells = {name: sheet.cell(index, column + 1).value for column, name in enumerate(header)}
        if (cells.get("Node"), rows.base.row_from_cells(cells).key()) in ours:
            sheet.delete_rows(index, 1)
    book.save(copy)
    return copy


def test_the_script_writes_the_six_rows_once(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    first = rows.apply(copy)
    assert sum(1 for line in first if ": + " in line) == 6, first
    assert rows.apply(copy)[-1] == "every row is already written; nothing written"


def test_the_script_refuses_a_row_somebody_wrote_on_its_option(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Shadow_capstone_100", "Option": 1, "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="finishing slice E"):
        rows.apply(copy)
    assert copy.read_bytes() == before


def test_the_script_leaves_the_capstones_other_options_alone():
    """Each capstone already holds another option's rows, which the Death trees' script wrote; this one reads none."""
    for node, _, group in rows.NODES:
        assert {int(row.option or 0) for row in group} != set(), node
    with open(DATA / "PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        others = [row for row in csv.DictReader(handle)
                  if row["Node"] in {"Shadow_capstone_25", "Shadow_capstone_50", "Shadow_capstone_200"}
                  and (row["Node"], int(row["Option"]), row["Stat"]) not in WORDS]
    assert others, "the three capstones' other options hold rows of their own"


def test_the_engine_reads_the_six_names():
    """The names the rows state are the ones the engine reads, and each is listed as having no attribute."""
    readers = SHADOW_IMAGES_CPP.read_text(encoding="utf-8") + DAMAGE_CPP.read_text(encoding="utf-8")
    listed = CLASS_STATS_CPP.read_text(encoding="utf-8")
    for (_, _, stat) in WORDS:
        assert re.search(r'::\w+Stat =\s*TEXT\("' + stat + r'"\)', readers), stat
        assert f'TEXT("{stat}")' in listed, stat
