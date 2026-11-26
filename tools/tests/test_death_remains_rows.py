"""The Necromancer's remains: the rows the remains slice writes, the tag, and the engine's hooks for them.

The Death finish's slice D (2026-11-26). `tools/reapply_death_remains_workbook.py` writes the rows of the Ossuary limb,
Many Made One and the two vows (the Abomination and the Bone Colossus) and declares `Minion.RaisedOnRemains`. These
tests hold that the workbook and the generated table are in the state the script writes, that it is idempotent, that it
refuses a node somebody changed since, that every number is the node's own sentence, and that the engine calls the
remains code at the places the approved design names (each a one-line hook in a shared file, which no Unreal test
reaches by name).
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

import reapply_death_remains_workbook as rows  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
NODES_CSV = REPO_ROOT / "game" / "Data" / "PassiveNodes.csv"
EFFECTS_CSV = REPO_ROOT / "game" / "Data" / "PassiveEffects.csv"
TAGS_INI = REPO_ROOT / "game" / "Config" / "Tags" / "CataclysmTags.ini"

#: Thirteen Ossuary nodes (the eleven ordinary ones, Grave Robber and Plague Pit; Bone Fuse is one of them), Many Made
#: One, and two vow options.
OSSUARY_NODES = 13
ROW_COUNT = 32


def _rows_of(node: str) -> tuple:
    (found,) = [wanted for each, _, wanted in rows.NODES if each == node]
    return found


def _sentence(node: str, option: int = 0) -> str:
    with open(NODES_CSV, encoding="utf-8", newline="") as handle:
        (row,) = [row for row in csv.DictReader(handle) if row["Name"] == node]
    return row["Description"] if option == 0 else row[f"Option{option}Description"]


def test_every_ossuary_node_has_rows_and_so_do_many_made_one_and_the_two_vows():
    with open(NODES_CSV, encoding="utf-8", newline="") as handle:
        ossuary = sorted(row["Name"] for row in csv.DictReader(handle)
                         if row["Name"].startswith("Necromancer_") and "_os_" in row["Name"])
    assert len(ossuary) == OSSUARY_NODES
    written = {node for node, _, _ in rows.NODES}
    assert set(ossuary) <= written
    assert written - set(ossuary) == {"Necromancer_basic_lg_d0", "Necromancer_capstone_200", "Necromancer_capstone_25"}
    assert sum(len(wanted) for _, _, wanted in rows.NODES) == ROW_COUNT


def test_the_vows_are_their_own_options():
    assert {row.option for row in _rows_of("Necromancer_capstone_200")} == {1}
    assert "Abomination" in _sentence("Necromancer_capstone_200", 1)
    assert {row.option for row in _rows_of("Necromancer_capstone_25")} == {2}
    assert "One giant you feed remains to" in _sentence("Necromancer_capstone_25", 2)
    assert {row.stat for row in _rows_of("Necromancer_capstone_25")} == {"bone_colossus", "remains_left"}


def test_bone_fuse_says_the_vow_makes_remains_appear_too():
    assert "or the Bone Colossus vow, no remains appear" in _sentence("Necromancer_basic_os_stem0")


@pytest.mark.parametrize("node, number", [
    ("Necromancer_basic_os_stem0", rows.BONE_FUSE_FERVOUR),
    ("Necromancer_keystone_os_kB", rows.GRAVE_ROBBER_FERVOUR),
    ("Necromancer_basic_os_stem1", rows.GRAVEFINDER_METRES_PER_POINT),
    ("Necromancer_basic_os_stem2", rows.RAISED_ON_BONES_PERCENT),
    ("Necromancer_basic_os_a0", rows.MANTLE_REACH_METRES),
    ("Necromancer_basic_os_a0", rows.MANTLE_MOST_REMAINS),
    ("Necromancer_basic_os_a1", rows.GATHERED_PERCENT),
    ("Necromancer_basic_os_a2", rows.FALLEN_AGAIN_CHANCE),
    ("Necromancer_basic_os_b0", rows.BONE_BOUND_PERCENT),
    ("Necromancer_basic_os_b1", rows.PICKED_CLEAN_POINTS_PER_STEP),
    ("Necromancer_basic_os_b2", rows.TWO_FROM_ONE_CHANCE),
    ("Necromancer_basic_os_c0", rows.FESTERING_PERCENT),
    ("Necromancer_basic_os_c1", rows.GRAVE_SPORES_CHANCE),
    ("Necromancer_basic_lg_d0", rows.MANY_MADE_ONE_PERCENT),
])
def test_every_number_is_the_nodes_own_sentence(node, number):
    assert re.search(rf"(?<![\d.]){number}(?![\d])", _sentence(node)), (node, number)


def test_a_for_every_n_points_node_has_a_row_at_every_step():
    """Mantle of Bone, 12 points, "for every 2 points" twice; Picked Clean, 8 points, "for every 3 points"."""
    mantle = _rows_of("Necromancer_basic_os_a0")
    for stat in ("remains_detonation_percent_of_maximum_health", "armor"):
        assert sorted(row.min_points for row in mantle if row.stat == stat) == [2, 4, 6, 8, 10, 12]
    armour = [row for row in mantle if row.stat == "armor"]
    assert {(row.scale, row.step, row.cap, row.reach) for row in armour} == {("remains_in_reach", 1, 10, 6)}
    assert sorted(row.min_points for row in _rows_of("Necromancer_basic_os_b1")) == [3, 6]


def test_the_action_rows_hang_on_the_remains_events():
    actions = {(node, row.action, row.action_event, row.value) for node, _, wanted in rows.NODES for row in wanted
               if row.action}
    assert actions == {
        ("Necromancer_basic_os_stem0", "class_resource", "minion_raised_on_remains", 3),
        ("Necromancer_keystone_os_kB", "class_resource", "minion_raised_on_remains", 5),
        ("Necromancer_basic_os_b1", "health", "remains_consumed", 1),
        ("Necromancer_basic_os_c1", "apply_status", "remains_detonation_hit", 6),
    }


def test_raised_on_bones_rows_require_the_tag_and_the_tag_is_declared():
    assert {row.tags for row in _rows_of("Necromancer_basic_os_stem2")} == {rows.RAISED_ON_REMAINS_TAG}
    assert f'Tag="{rows.RAISED_ON_REMAINS_TAG}"' in TAGS_INI.read_text(encoding="utf-8")


def test_the_workbook_holds_the_rows_and_the_tag():
    assert rows.apply_tag(WORKBOOK, check_only=True)[-1].endswith("nothing written")
    report = rows.base.apply(WORKBOOK, check_only=True, nodes=rows.NODES, label=rows.LABEL)
    assert report[-1] == "every row is already written; nothing written", report


def test_the_generated_csv_holds_the_rows():
    def number(text: str) -> float:
        return float(text) if text else 0.0

    with open(EFFECTS_CSV, encoding="utf-8", newline="") as handle:
        written = [(row["Node"], row["Stat"], row["ValueKind"], number(row["ValuePerPoint"]), row["RequiredTags"],
                    row["Scale"], number(row["ScaleStep"]), number(row["ScaleMaxSteps"]), number(row["MinPoints"]),
                    row["Action"], row["ActionEvent"], number(row["Option"]), row["Ailment"])
                   for row in csv.DictReader(handle)]
    for node, _, wanted in rows.NODES:
        for row in wanted:
            key = (node, row.stat or "", row.kind, float(row.value), row.tags or "", row.scale or "",
                   float(row.step or 0), float(row.cap or 0), float(row.min_points or 0), row.action or "",
                   row.action_event or "", float(row.option or 0), row.ailment or "")
            assert written.count(key) == 1, key


def _workbook_without_the_rows(tmp_path) -> pathlib.Path:
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    node_column = header.index("Node") + 1
    option_column = header.index("Option") + 1
    ours = {(node, row.option or 0) for node, _, wanted in rows.NODES for row in wanted}
    for index in range(sheet.max_row, 1, -1):
        option = sheet.cell(index, option_column).value or 0
        if (sheet.cell(index, node_column).value, int(option)) in ours:
            sheet.delete_rows(index, 1)
    tags = book[rows.TAGS_SHEET]
    for index in range(tags.max_row, 1, -1):
        if tags.cell(index, 1).value == rows.RAISED_ON_REMAINS_TAG:
            tags.delete_rows(index, 1)
    book.save(copy)
    return copy


def test_the_script_appends_the_rows_and_the_tag_once(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    tag = rows.apply_tag(copy)
    assert tag[-1].startswith("wrote 1 tag"), tag
    first = rows.base.apply(copy, nodes=rows.NODES, label=rows.LABEL)
    assert sum(1 for line in first if ": + " in line) == ROW_COUNT, first
    assert rows.apply_tag(copy)[-1].endswith("nothing written")
    second = rows.base.apply(copy, nodes=rows.NODES, label=rows.LABEL)
    assert second[-1] == "every row is already written; nothing written", second


def test_the_script_refuses_a_node_somebody_wrote_since(tmp_path):
    copy = _workbook_without_the_rows(tmp_path)
    book = openpyxl.load_workbook(copy)
    sheet = book[rows.base.SHEET]
    header = [cell.value for cell in sheet[1]]
    sheet.append([{"Node": "Necromancer_basic_os_a1", "Stat": "remains_duration", "Value Kind": "increased",
                   "Value Per Point": 20}.get(name) for name in header])
    book.save(copy)
    with pytest.raises(rows.base.ReapplyError, match="remains slice"):
        rows.base.apply(copy, nodes=rows.NODES, label=rows.LABEL)


# ----------------------------------------------------------------------------------------------------------------
# The engine's hooks, each one line in a shared file
# ----------------------------------------------------------------------------------------------------------------

def _function_body(path: pathlib.Path, signature: str) -> str:
    text = path.read_text(encoding="utf-8")
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[brace:index + 1]
    raise AssertionError(f"no end to {signature}")


def _code(body: str) -> str:
    return re.sub(r"//[^\n]*", "", body)


def test_every_weapon_skills_landing_detonates_remains_before_the_minions_rule():
    body = _code(_function_body(SOURCE / "AbilitySystem" / "CataclysmSkillTemplate.cpp",
                                "int32 UCataclysmSkillTemplate::DetonateOwnMinionsWhere("))
    assert "UCataclysmRemains::DetonateWhere(Self, IsInside);" in body
    # SLICE C'S RULE FOR THE MINIONS IS `DetonatesOwnMinions()`, the row's key or Lit Fuse; it asked
    # `Params.bDetonates` itself before the bundle (2026-11-26) joined the two slices.
    assert body.index("UCataclysmRemains::DetonateWhere") < body.index("DetonatesOwnMinions()")


def test_a_summon_chooses_its_remains_before_the_reserve_and_raises_on_them_after_holding_it():
    body = _code(_function_body(SOURCE / "AbilitySystem" / "CataclysmSkillTemplates.cpp",
                                "ACataclysmMinion* UCataclysmSummonSkill::SummonOne()"))
    choose = body.index("UCataclysmRemains::ChooseToRaiseOn(Self, RiftLocation)")
    lowered = body.index("UCataclysmRemains::ReserveOnRemains(Self, Reserve)")
    room = body.index("UCataclysmCommand::HasRoomToReserve(Self, Reserve)")
    held = body.index("UCataclysmCommand::HoldReserve(Self, Minion, Reserve")
    raised = body.index("UCataclysmRemains::RaiseOn(Self, Minion, RisesOn)")
    assert choose < lowered < room < held < raised


def test_the_floor_population_notes_what_the_floor_started_with_on_a_carved_arena_only():
    text = _code((SOURCE / "Dungeon" / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8"))
    noted = [match.start() for match in re.finditer(r"UCataclysmBoneGiants::NoteStartedWith\(Enemy\);", text)]
    added = [match.start() for match in re.finditer(r"CurrentWave\.Add\(Enemy\);", text)]
    assert len(noted) == len(added) == 2
    for note, add in zip(sorted(noted), sorted(added), strict=True):
        between = text[add:note]
        assert 0 < note - add < 400 and "if (!bKeptTheArena)" in between
    begin = _code(_function_body(SOURCE / "Dungeon" / "CataclysmDungeonGameMode.cpp",
                                 "int32 ACataclysmDungeonGameMode::PopulateFloor()"))
    assert re.search(r"if \(!bKeptTheArena\)\s*\{\s*UCataclysmBoneGiants::BeginFloor\(World\);", begin)


def test_leaving_the_floor_clears_remains_and_ends_the_giants():
    body = _code(_function_body(SOURCE / "Dungeon" / "CataclysmFloorContents.cpp",
                                "int32 UCataclysmFloorContents::ClearTheFloor("))
    assert "FloorContentsDestroyEvery<ACataclysmRemainsMarker>(World);" in body
    assert "UCataclysmBoneGiants::EndTheFloor(World);" in body


def test_the_player_hears_every_death_for_remains_and_a_follower_killed_may_leave_some():
    player = _code(_function_body(SOURCE / "Character" / "CataclysmPlayerCharacter.cpp",
                                  "void ACataclysmPlayerCharacter::OnSomethingDied("))
    assert "UCataclysmRemains::HearDeath(this, Notice);" in player
    # BEFORE THE KILLER TEST, which returns for a death this character did not cause.
    assert player.index("UCataclysmRemains::HearDeath") < player.index("if (Notice.Killer != this || !Notice.Victim)")
    vital = _code(_function_body(SOURCE / "AbilitySystem" / "CataclysmVitalAttributeSet.cpp",
                                 "void UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero("))
    assert vital.index("UCataclysmRemains::LeaveForAFollower(Commander, Character, Where);") \
        < vital.index("Character->HandleDeath();")


def test_the_hud_draws_the_remains_tags():
    body = _code(_function_body(SOURCE / "Interface" / "CataclysmHUD.cpp", "void ACataclysmHUD::DrawHUD()"))
    assert "DrawRemainsNames();" in body
