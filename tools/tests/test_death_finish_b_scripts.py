"""The Death line's finishing slice B, the Necromancer's army: its rows script and the names the engine reads.

2026-11-26. `tools/reapply_death_finish_b_workbook.py` declares the tag `Minion.Permanent` and writes 29 rows on the
Legion limb's thirteen nodes (Many Made One is slice D's), Carrion Host, Second Death, Harvest of the Host, and four vow
options: The Restless Dead, Grave Swap, Lich Form and Shared Flesh.

These tests hold the rows to their sentences' figures, that the workbook and the generated table hold them, that the
script is idempotent and refuses what somebody wrote since, and that every new stat the rows state is a name the engine
reads and lists as having no attribute. The game's half is
`game/Source/Cataclysm/Tests/CataclysmNecromancerArmyTests.cpp`.
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

import reapply_death_finish_b_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
ARMY_CPP = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem" / "CataclysmNecromancerArmy.cpp"
CLASS_STATS_CPP = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character" / "CataclysmPlayerClassStats.cpp"
TAGS_INI = REPO_ROOT / "game" / "Config" / "Tags" / "CataclysmTags.ini"
NODES_CSV = REPO_ROOT / "game" / "Data" / "PassiveNodes.csv"
EFFECTS_CSV = REPO_ROOT / "game" / "Data" / "PassiveEffects.csv"

#: The stats this slice brings in, each read by `UCataclysmNecromancerArmy` through a `...Stat` constant.
NEW_STATS = (
    "summon_without_room_heals_permanent_percent", "minion_crit_share_of_yours_percent", "minion_rises_again_chance",
    "minion_health_regen_percent_per_second", "summon_raises_more_permanent_minions",
    "permanent_minion_kill_rises_as_husk_chance", "minion_death_rises_as_husk", "kill_rises_as_skeleton_chance",
    "movement_swaps_with_minion", "skills_raise_husks_seconds_after_resource_full", "damage_taken_split_with_minions",
    "lone_revenant_held", "minion_reserve", "permanent_minion_raises_a_second_chance")


def node_rows() -> dict[str, dict]:
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


def test_every_node_the_script_writes_had_no_row_and_is_not_many_made_one():
    names = [node for node, _, _ in rows.NODES]
    assert "Necromancer_basic_lg_d0" not in names, "Many Made One is slice D's"
    assert len(names) == len(set(names))
    known = node_rows()
    for node in names:
        assert node in known, node


def test_each_rows_figure_is_in_its_sentence():
    """The figures read from the sentences, so a reworded node fails here first: each value the row states appears in
    its node's (or its option's) words, except the flags and The Restless Dead's judged chance."""
    known = node_rows()
    unstated = {("Necromancer_capstone_25", "kill_rises_as_skeleton_chance"),
                ("Necromancer_keystone_spine_001", "minion_death_rises_as_husk"),
                ("Necromancer_keystone_spine_003", "permanent_minion_kill_rises_as_husk_chance"),
                ("Necromancer_capstone_50", "movement_swaps_with_minion"),
                ("Necromancer_capstone_100", "damage_taken_split_with_minions"),
                ("Necromancer_keystone_lg_kA", "lone_revenant_held")}
    for node, _, wanted in rows.NODES:
        for row in wanted:
            if (node, row.stat) in unstated:
                continue
            words = known[node][f"Option{row.option}Description"] if row.option else known[node]["Description"]
            assert f"{abs(row.value):g}" in words, (node, row.stat, words)


def test_the_restless_dead_chance_is_the_judged_figure():
    ((option, chance),) = [(row.option, row.value) for node, _, wanted in rows.NODES for row in wanted
                           if row.stat == "kill_rises_as_skeleton_chance"]
    assert (option, chance) == (1, rows.RESTLESS_DEAD_CHANCE) == (1, 20)


def test_every_permanent_minion_row_requires_the_tag():
    """"Your permanent minions" is the tag on every row about them; Motley Host and First Among the Dead's options
    aside, a Legion row on a minion stat that omits it would reach husks and shades as well."""
    for node, _, wanted in rows.NODES:
        sentence = node_rows()[node]["Description"].lower()
        for row in wanted:
            if row.stat.startswith("minion_") and "permanent minion" in sentence:
                assert row.tags == rows.PERMANENT, (node, row.stat)
    motley = [row for node, _, wanted in rows.NODES for row in wanted if node == "Necromancer_basic_lg_stem2"]
    assert [row.tags for row in motley] == [None], "Motley Host says Minion Damage, every minion's"


def test_the_workbook_holds_the_rows_and_the_tag():
    report = rows.apply(WORKBOOK, check_only=True)
    assert report[0] == f"Tags: {rows.PERMANENT} is already declared; nothing written", report
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_files_hold_the_rows_and_the_tag():
    with EFFECTS_CSV.open(encoding="utf-8", newline="") as handle:
        written = [row for row in csv.DictReader(handle) if row["Node"] in {node for node, _, _ in rows.NODES}
                   and (row["Stat"] in NEW_STATS or row["RequiredTags"] == rows.PERMANENT
                        or row["Scale"] == "minion_kinds_beyond_the_first"
                        or row["Condition"] == "seconds_after_resource_full")]
    assert len(written) == sum(len(wanted) for _, _, wanted in rows.NODES) == 29
    assert f'Tag="{rows.PERMANENT}"' in TAGS_INI.read_text(encoding="utf-8")


def _workbook_without_ours(tmp_path) -> pathlib.Path:
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    ours = {(node, row.key()) for node, _, wanted in rows.NODES for row in wanted}
    for index in range(sheet.max_row, 1, -1):
        cells = {name: sheet.cell(index, column + 1).value for column, name in enumerate(header)}
        if (cells.get("Node"), rows.base.row_from_cells(cells).key()) in ours:
            sheet.delete_rows(index, 1)
    tags = book["Tags"]
    for index in range(tags.max_row, 1, -1):
        if tags.cell(index, 1).value == rows.PERMANENT:
            tags.delete_rows(index, 1)
    book.save(copy)
    return copy


def test_the_script_writes_the_tag_and_the_rows_once(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    first = rows.apply(copy)
    assert first[0] == f"Tags: + {rows.PERMANENT}; no other cell or sheet changed", first
    assert sum(1 for line in first if ": + " in line) == 29 + 1, first
    again = rows.apply(copy)
    assert again[-1] == "every row is already written; nothing written"
    assert again[0] == f"Tags: {rows.PERMANENT} is already declared; nothing written"


def test_the_script_refuses_a_row_somebody_wrote_on_its_node(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Necromancer_basic_lg_stem0", "Stat": "armor", "Value Kind": "flat",
                   "Value Per Point": 4}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.base.ReapplyError, match="finishing slice B"):
        rows.apply(copy)


def test_the_script_refuses_a_tag_somebody_described_otherwise(tmp_path):
    copy = _workbook_without_ours(tmp_path)
    book = openpyxl.load_workbook(copy)
    tags = book["Tags"]
    tags.append([rows.PERMANENT, "something else"])
    book.save(copy)
    before = copy.read_bytes()
    with pytest.raises(rows.base.ReapplyError, match="somebody has written it since"):
        rows.apply(copy)
    assert copy.read_bytes() == before


def test_the_engine_reads_every_new_name_and_lists_it():
    """Each new stat is a `...Stat = TEXT("...")` constant of `UCataclysmNecromancerArmy` and is listed as having no
    attribute. Rise Together's is its own, not slice C's `minion_extra_raise_chance`."""
    army = ARMY_CPP.read_text(encoding="utf-8")
    listed = CLASS_STATS_CPP.read_text(encoding="utf-8")
    for stat in NEW_STATS:
        assert re.search(r'UCataclysmNecromancerArmy::\w+Stat =\s+TEXT\("' + stat + r'"\)', army), stat
        assert f'TEXT("{stat}")' in listed, stat
    assert not any(row.stat == "minion_extra_raise_chance" for _, _, wanted in rows.NODES for row in wanted)


def test_the_scale_and_the_condition_are_known_to_the_generator():
    import generate_datatables as gen
    assert "minion_kinds_beyond_the_first" in gen.SCALES
    assert gen.CONDITIONS["minion_has_your_most_maximum_health"] is None
