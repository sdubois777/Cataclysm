"""A passive row may carry the Enchantment Effects sheet's action columns, read by that sheet's rules.

THE PASSIVE ROW PORT OF 2026-11-24 (`plan_death_and_war.md`, slice B28). Until it, a passive node could only
change a stat: the Passive Effects sheet had no `Action`, `Action Event`, `Stack Seconds`, `Condition 2`,
`Scale Offset` or the rest, and refused the two of them a sheet once tried to carry. The War and Death class trees
need nodes that DO something when an event happens -- restore health on a block, gain a stack when hit, apply a
bleed when retaliation is paid -- and the enchantment rows already do all of it.

WHAT THESE TESTS HOLD. A row stating any action column (`gen.PASSIVE_ACTION_COLUMNS`), or scaled by a count an
action grants, is read by `gen._effect_row`, the Enchantment Effects sheet's own per-row rules, so a passive action
and a worn one cannot be judged by two sets of rules; a refusal names the Passive Effects sheet; the columns about an
item are refused; `Per Point From` pays a threshold row per point; and the game's row struct carries the fourteen
columns in the order the CSV writes them. The game's half is in
`game/Source/Cataclysm/Tests/CataclysmPassiveRowPortTests.cpp`.
"""
from __future__ import annotations

import csv
import pathlib
import re
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: Every column the passive reader asks for, the fourteen of the port included.
HEADERS = ["Node", "Stat", "Value Kind", "Value Per Point", "Required Tags", "Condition",
           "Condition Value", "Scale", "Scale Step", "Option", "Reach Metres", "Min Points",
           "Scale Max Steps", "Action", "Action Event", "Fraction Of", "Stack Seconds",
           "Scale Offset", "Every Seconds", "Every Nth", "Condition 2", "Condition Value 2",
           "Trigger Cooldown", "Event Value", "Ailment", "Damage Share", "Per Point From",
           "Chance Percent"]

#: Real nodes, so a row's words can be read from the tree files. Their own sentences do not matter here.
BLOCK_NODE = "Bulwark_basic_ic_t2_002"
STACK_NODE = "Bulwark_basic_tw_stem_002"
OATH = "Bulwark_capstone_25"


def row(**cells) -> list:
    """One sheet row: each keyword is a column, with spaces written as underscores."""
    named = {key.replace("_", " "): value for key, value in cells.items()}
    unknown = set(named) - set(HEADERS)
    assert not unknown, f"not a column: {unknown}"
    return [named.get(header) for header in HEADERS]


def read(tmp_path, rows: list[list], headers: list[str] = HEADERS) -> list[dict]:
    book = openpyxl.Workbook()
    book.remove(book.active)
    sheet = book.create_sheet("Passive Effects")
    sheet.append(headers)
    for line in rows:
        sheet.append(line)
    path = tmp_path / "passive.xlsx"
    book.save(path)
    return gen.passive_effects(openpyxl.load_workbook(path))


# ---------------------------------------------------------------------------
# A row with an action column is read by the enchantment rules
# ---------------------------------------------------------------------------

def test_an_action_row_is_read_by_the_enchantment_rules(tmp_path):
    """Shield Oath's shape: "Blocking an attack restores 50 HP". The two values the enchantment rules write for an
    empty cell arrive too, which is how this test knows those rules read the row: a pool action's fraction is of the
    maximum, and a hit-fired event waits a quarter second (`DEFAULT_TRIGGER_COOLDOWN`)."""
    out = read(tmp_path, [row(Node=OATH, Option=1, Action="health", Action_Event="block",
                              Value_Per_Point=50)])
    assert len(out) == 1
    got = out[0]
    assert got["Name"] == f"{OATH}#1"
    assert (got["Action"], got["ActionEvent"], got["ValuePerPoint"]) == ("health", "block", 50.0)
    assert got["FractionOf"] == "maximum"
    assert got["TriggerCooldown"] == gen.DEFAULT_TRIGGER_COOLDOWN
    assert got["Option"] == 1
    assert got["Stat"] == "" and got["ValueKind"] == ""


def test_a_refusal_names_the_passive_sheet(tmp_path):
    """`_effect_row` names the Enchantment Effects sheet in every message; the row number is this sheet's, so the
    passive reader corrects the name. A pool the game does not have is the enchantment rules' own refusal."""
    with pytest.raises(gen.DataError) as raised:
        read(tmp_path, [row(Node=BLOCK_NODE, Action="no_such_pool", Action_Event="block", Value_Per_Point=3)])
    message = str(raised.value)
    assert message.startswith("Passive Effects row 2: "), message
    assert "Enchantment Effects row" not in message
    assert "no_such_pool" in message


def test_the_control_the_same_pool_is_accepted(tmp_path):
    """The control for the test above: the row that differs only in naming a real pool is read."""
    out = read(tmp_path, [row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block",
                              Value_Per_Point=3)])
    assert out[0]["Action"] == "class_resource"


def test_an_own_stack_row_is_read_by_the_enchantment_rules(tmp_path):
    """Thick Hide's rider: "Each hit you survive increases your retaliation damage by 1% for 5 seconds, stacking up
    to 10 times", at 6 points. A stat row, but scaled by its own stacks, so it needs the action that grants them."""
    out = read(tmp_path, [row(Node=STACK_NODE, Stat="retaliation", Value_Kind="increased", Value_Per_Point=1,
                              Scale="own_stacks", Scale_Step=1, Scale_Max_Steps=10, Action_Event="hit_taken",
                              Stack_Seconds=5, Min_Points=6)])
    got = out[0]
    assert (got["Scale"], got["ScaleMaxSteps"], got["ActionEvent"], got["StackSeconds"]) == (
        "own_stacks", 10, "hit_taken", 5.0)
    assert got["MinPoints"] == 6


def test_an_own_stack_row_with_no_event_is_refused(tmp_path):
    """The enchantment rules refuse an own stack nothing grants. Read by those rules, a passive one is too."""
    with pytest.raises(gen.DataError, match=r"^Passive Effects row 2: .* counts its own stacks and names the event"):
        read(tmp_path, [row(Node=STACK_NODE, Stat="retaliation", Value_Kind="increased", Value_Per_Point=1,
                            Scale="own_stacks", Scale_Step=1, Scale_Max_Steps=10, Stack_Seconds=5)])


def test_a_second_condition_is_kept_on_a_stat_row(tmp_path):
    """Berserker's Crushing Weight shape: two-handed AND the target staggered. Both must hold, by the enchantment
    rows' rule."""
    out = read(tmp_path, [row(Node="Berserker_twoh_006", Stat="attack_damage", Value_Kind="increased",
                              Value_Per_Point=3, Condition="wielding_two_handed_weapon",
                              Condition_2="target_is_staggered")])
    got = out[0]
    assert (got["Condition"], got["Condition2"]) == ("wielding_two_handed_weapon", "target_is_staggered")
    assert got["Action"] == ""


def test_a_row_stating_no_action_column_is_read_as_before(tmp_path):
    """Every row of today's sheet: no action column, so the passive reader's own path, and the fourteen new fields
    at their empty values."""
    out = read(tmp_path, [row(Node=BLOCK_NODE, Stat="armor", Value_Kind="increased", Value_Per_Point=3)])
    got = out[0]
    for field, empty in gen.PASSIVE_PORT_DEFAULTS:
        assert got[field] == empty, field
    assert got["PerPointFrom"] == 0
    assert got["ReachMetres"] == -1.0


def test_an_action_row_may_not_count_enemies_in_a_radius(tmp_path):
    """The enchantment rows' builder carries no radius, so such a row would count nobody."""
    with pytest.raises(gen.DataError, match="carries no radius"):
        read(tmp_path, [row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block", Value_Per_Point=3,
                            Reach_Metres=4)])


def test_two_rows_of_one_node_sharing_an_action_and_event_are_refused(tmp_path):
    """The game keys a row's cooldown and counts by node, option, action and event, and names a second row under a
    different condition apart (the Death trees' slice C6b3). Two under the SAME conditions are refused: nothing would
    tell them apart. The two differ in their base only, since two rows the same in every column are refused earlier, as a
    duplicated row."""
    with pytest.raises(gen.DataError, match="same action on the same event"):
        read(tmp_path, [
            row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block", Value_Per_Point=3,
                Condition="health_below", Condition_Value=50),
            row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block", Value_Per_Point=3,
                Fraction_Of="current", Condition="health_below", Condition_Value=50),
        ])


def test_two_rows_of_one_node_on_one_action_and_event_under_different_conditions_are_accepted(tmp_path):
    """The Death trees' slice C6b3 (2026-11-25), Opening Found's shape: 1 always and 1 more under a condition, both
    on one event. `UCataclysmPassiveTree::EnchantmentRowsFor` names the second `_Row2`. THE CONTROL is the refusal
    above, the same two rows under one condition."""
    out = read(tmp_path, [
        row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block", Value_Per_Point=3),
        row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block", Value_Per_Point=3,
            Condition="health_below", Condition_Value=50),
    ])
    assert [got["Condition"] for got in out] == ["", "health_below"]


def test_two_steps_of_one_node_at_different_thresholds_may_share_an_action_and_event(tmp_path):
    """The Death trees' slice C6a (2026-11-25): "1 second for every 4 points" is
    a row at 4 and a row at 8, and the engine names each step apart by its
    threshold, so the two carry two keys. THE CONTROL beside the refusal above:
    the same two rows at one threshold are refused."""
    out = read(tmp_path, [
        row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block", Value_Per_Point=3, Min_Points=4),
        row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block", Value_Per_Point=3, Min_Points=8),
    ])
    assert [got["MinPoints"] for got in out] == [4, 8]
    with pytest.raises(gen.DataError, match="same action on the same event"):
        read(tmp_path, [
            row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block", Value_Per_Point=3, Min_Points=4),
            row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block", Value_Per_Point=3, Min_Points=4,
                Fraction_Of="current"),
        ])


def test_two_options_of_one_oath_may_share_an_action_and_event(tmp_path):
    """The control: two options of one capstone carry two keys, and only one is ever taken."""
    out = read(tmp_path, [
        row(Node=OATH, Option=1, Action="health", Action_Event="block", Value_Per_Point=50),
        row(Node=OATH, Option=2, Action="health", Action_Event="block", Value_Per_Point=20),
    ])
    assert [got["Option"] for got in out] == [1, 2]


@pytest.mark.parametrize("column", gen.ENCHANTMENT_ONLY_COLUMNS)
def test_a_column_about_an_item_is_refused(tmp_path, column):
    """A passive row names its node, is paid per point and rolls nothing."""
    with pytest.raises(gen.DataError, match=re.escape(repr(column))):
        read(tmp_path, [row(Node=BLOCK_NODE, Stat="armor", Value_Kind="increased", Value_Per_Point=3) + [None]],
             headers=HEADERS + [column])


def test_a_sheet_with_no_port_column_is_refused_now_the_rows_have_them(tmp_path):
    """The fourteen were optional until the first rows using them came.

    Until 2026-11-25 this read "the sheet reads with no port column at all": the workbook had none of the fourteen,
    and `OPTIONAL_COLUMNS` let the sheet read without them. The Death trees' slice C1 wrote the first rows using them,
    put all fourteen in the workbook with `tools/reapply_death_trees_workbook.py` and took them out of
    `OPTIONAL_COLUMNS`, so a sheet without them is now refused like any other column the generator reads."""
    old = HEADERS[:HEADERS.index("Action")]
    with pytest.raises(gen.DataError, match="the Passive Effects sheet has no '") as refused:
        read(tmp_path, [[BLOCK_NODE, "armor", "increased", 3]], headers=old)
    missing = str(refused.value).split("'")[1]
    assert missing in set(gen.PASSIVE_ACTION_COLUMNS) | {"Per Point From"}, missing
    assert "Passive Effects" not in gen.OPTIONAL_COLUMNS


# ---------------------------------------------------------------------------
# Per Point From
# ---------------------------------------------------------------------------

def test_per_point_from_is_kept_with_a_threshold(tmp_path):
    """Tempered Steel's "At 6 points: These bonuses are doubled while at maximum Fervour": all its points, from 6."""
    out = read(tmp_path, [row(Node="Bulwark_basic_trunk_006", Stat="armor", Value_Kind="increased",
                              Value_Per_Point=2, Condition="class_resource_at_maximum", Min_Points=6,
                              Per_Point_From=1)])
    assert (out[0]["MinPoints"], out[0]["PerPointFrom"]) == (6, 1)


@pytest.mark.parametrize("min_points, per_point_from, says", [
    (None, 1, "no Min Points"),
    (6, 7, "at most the Min Points"),
    (6, 0, "whole number of points from 1"),
    (6, 1.5, "whole number of points from 1"),
])
def test_per_point_from_is_refused_where_it_cannot_mean_anything(tmp_path, min_points, per_point_from, says):
    with pytest.raises(gen.DataError, match=says):
        read(tmp_path, [row(Node="Bulwark_basic_trunk_006", Stat="armor", Value_Kind="increased",
                            Value_Per_Point=2, Min_Points=min_points, Per_Point_From=per_point_from)])


# ---------------------------------------------------------------------------
# The engine's struct and the CSV
# ---------------------------------------------------------------------------

def _passive_row_struct_fields() -> list[str]:
    """The UPROPERTY field names of FCataclysmPassiveEffectRow, in declaration order."""
    text = (REPO_ROOT / "game/Source/Cataclysm/Data/CataclysmDataRows.h").read_text(encoding="utf-8")
    start = text.index("struct FCataclysmPassiveEffectRow : public FTableRowBase")
    end = text.index("\n};", start)
    body = text[start:end]
    # One level of parentheses inside the macro, for the port's `meta = (DataTableImportOptional)`.
    return re.findall(r"UPROPERTY\((?:[^()]|\([^()]*\))*\)\s*\n\s*[\w:<>]+\s+(\w+)", body)


def test_the_struct_carries_the_port_fields_in_the_csv_order():
    """`test_csv_columns_match_their_row_structs.py` holds the column NAMES to the struct; this holds the ORDER of
    the fourteen, so a reader of either file finds the port's fields after `ScaleMaxSteps`, as both say."""
    fields = _passive_row_struct_fields()
    assert fields[fields.index("ScaleMaxSteps") + 1:] == [*gen.PASSIVE_PORT_FIELDS, "PerPointFrom", "ChancePercent"]


def test_the_written_csv_has_the_port_columns_last():
    with open(REPO_ROOT / "game/Data/PassiveEffects.csv", encoding="utf-8", newline="") as handle:
        header = next(csv.reader(handle))
    assert header[header.index("ScaleMaxSteps") + 1:] == [*gen.PASSIVE_PORT_FIELDS, "PerPointFrom", "ChancePercent"]


# ---------------------------------------------------------------------------
# The Death trees' slice C6b3 (2026-11-25): a flat amount of class resource,
# and the chance a class resource row pays
# ---------------------------------------------------------------------------

def test_a_flat_amount_and_a_chance_are_read_on_a_class_resource_row(tmp_path):
    """Tear Loose's shape: 1 Fervour, a flat amount, on a 2% chance a point."""
    out = read(tmp_path, [row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block",
                              Value_Per_Point=1, Fraction_Of="points", Chance_Percent=2)])
    assert (out[0]["FractionOf"], out[0]["ValuePerPoint"], out[0]["ChancePercent"]) == ("points", 1.0, 2.0)


def test_an_empty_chance_is_every_time_and_leaves_an_existing_row_as_it_was(tmp_path):
    """Every row written before the column carries 0, which the game reads as every time; and a sheet without the
    column reads the same."""
    plain = row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block", Value_Per_Point=3)
    assert read(tmp_path, [plain])[0]["ChancePercent"] == 0.0
    without = [header for header in HEADERS if header != "Chance Percent"]
    assert read(tmp_path, [plain[:-1]], headers=without)[0]["ChancePercent"] == 0.0


@pytest.mark.parametrize("cells", [
    {"Action": "health", "Action_Event": "block", "Value_Per_Point": 3},
    {"Stat": "armor", "Value_Kind": "flat", "Value_Per_Point": 3},
])
def test_a_chance_is_refused_on_a_row_that_does_not_move_the_class_resource(tmp_path, cells):
    with pytest.raises(gen.DataError, match="A chance is read by a 'class_resource' row and the action"):
        read(tmp_path, [row(Node=BLOCK_NODE, Chance_Percent=2, **cells)])


def test_a_chance_is_read_on_the_doubled_evasion_too(tmp_path):
    """The Death trees' slice C6b4 (2026-11-25), as ruled: Twice Answered's `next_hit_critical_twice` on `dodge`,
    its value the count, 2. THE CONTROL is the refusal above, on any other action."""
    out = read(tmp_path, [row(Node=BLOCK_NODE, Action="next_hit_critical_twice", Action_Event="dodge",
                              Value_Per_Point=2, Chance_Percent=5)])
    assert (out[0]["Action"], out[0]["ValuePerPoint"], out[0]["ChancePercent"]) == ("next_hit_critical_twice", 2.0, 5.0)


@pytest.mark.parametrize("cells, refused", [
    ({"Action_Event": "kill", "Value_Per_Point": 2}, "Only an evasion does"),
    ({"Action_Event": "dodge", "Value_Per_Point": 3}, "It holds 2 and no other number"),
])
def test_the_doubled_evasion_is_refused_off_an_evasion_and_at_any_count_but_two(tmp_path, cells, refused):
    with pytest.raises(gen.DataError, match=refused):
        read(tmp_path, [row(Node=BLOCK_NODE, Action="next_hit_critical_twice", Chance_Percent=5, **cells)])


@pytest.mark.parametrize("chance", [0, -1, 101])
def test_a_chance_outside_above_0_to_100_is_refused(tmp_path, chance):
    with pytest.raises(gen.DataError, match="a chance is above 0 and up to 100"):
        read(tmp_path, [row(Node=BLOCK_NODE, Action="class_resource", Action_Event="block", Value_Per_Point=1,
                            Fraction_Of="points", Chance_Percent=chance)])


def test_a_flat_amount_is_refused_on_any_pool_but_the_class_resource(tmp_path):
    with pytest.raises(gen.DataError, match="A flat amount is of 'class_resource' only"):
        read(tmp_path, [row(Node=BLOCK_NODE, Action="health", Action_Event="block", Value_Per_Point=50,
                            Fraction_Of="points")])


def test_the_engine_names_the_same_two_scales_an_action_grants():
    """`UCataclysmPassiveTree::IsBuiltAsAnEnchantmentRow` decides by the pipeline's scale enum, and the generator by
    `SCALES_AN_ACTION_GRANTS`. The pipeline's table spells the two enum values' names."""
    pipeline = (REPO_ROOT / "game/Source/Cataclysm/AbilitySystem/CataclysmStatPipeline.cpp").read_text(
        encoding="utf-8")
    tree = (REPO_ROOT / "game/Source/Cataclysm/Character/CataclysmPassiveTree.cpp").read_text(encoding="utf-8")
    named = dict(re.findall(r'\{\s*TEXT\("(\w+)"\),\s*ECataclysmStatScale::(\w+)\s*\}', pipeline))
    start = tree.index("bool UCataclysmPassiveTree::IsBuiltAsAnEnchantmentRow(")
    body = tree[start:tree.index("\n}", start)]
    asked = set(re.findall(r"Scale == ECataclysmStatScale::(\w+)", body))
    assert {name for name, value in named.items() if value in asked} == set(gen.SCALES_AN_ACTION_GRANTS)
