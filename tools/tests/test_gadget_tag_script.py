"""`Type.Gadget`: the script that writes it, the generator's rule that holds it, and what it reaches.

2026-11-25, the coordinating session's ruling on the Saboteur survey's question 1: every machine that is not a trap
carries `Type.Gadget`, so a row meaning gadgets alone can say so. `tools/apply_gadget_tag_2026_11_25.py` declares the
tag on the Tags sheet, adds it to the three gadget types, and moves Support Network's two rows onto it. These tests
hold that the workbook, the generated tables and the tag list are in that state; that the script is idempotent and
refuses a cell somebody changed; and that the generator refuses a gadget type without the tag and a trap type with it.
"""
from __future__ import annotations

import csv
import pathlib
import shutil
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import apply_gadget_tag_2026_11_25 as script  # noqa: E402
import generate_datatables as gen  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
DATA = REPO_ROOT / "game" / "Data"
TAGS_INI = REPO_ROOT / "game" / "Config" / "Tags" / "CataclysmTags.ini"


def _rows(name: str) -> list[dict]:
    with open(DATA / f"{name}.csv", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def _tags(text: str) -> set[str]:
    return {tag.strip() for tag in text.split(",") if tag.strip()}


def test_the_workbook_holds_the_tags():
    assert script.apply(WORKBOOK, check_only=True) == ["every tag is already written; nothing written"]


def test_every_gadget_type_carries_the_tag_and_no_trap_type_does():
    """THE CONTROL is the trap types: both are deployable and neither carries it."""
    carrying = {row["Name"] for row in _rows("MinionTypes") if "Type.Gadget" in _tags(row["Tags"])}
    deployable = {row["Name"]: _tags(row["Tags"]) for row in _rows("MinionTypes") if "Type.Deployable" in _tags(row["Tags"])}
    # AND THE EFFIGY, the Death Crossbow's decoy, a machine that is not a trap (the Death finish slice A, 2026-11-26).
    assert carrying == {"BoltTurret", "Ballista", "SkillGadget", "Effigy"}
    assert {name for name, tags in deployable.items() if "Type.Trap" in tags} == {"SpikeTrap", "SkillTrap"}


def test_support_networks_gadget_damage_names_gadgets_alone():
    tags = {(row["Stat"], row["RequiredTags"]) for row in _rows("PassiveEffects")
            if row["Node"] == "Saboteur_basic_gadget_stem_005"}
    assert ("attack_damage", "Type.Gadget") in tags and ("spell_damage", "Type.Gadget") in tags
    assert not any(required == "Type.Deployable" for _, required in tags)


def test_no_passive_row_names_deployables():
    """THE RULING HELD: a passive row meaning gadgets names `Type.Gadget` and one meaning traps `Type.Trap`; none names
    `Type.Deployable`, which would reach both. THE CONTROL is that passive rows naming the two tags exist."""
    named = {row["RequiredTags"] for row in _rows("PassiveEffects")}
    assert {"Type.Gadget", "Type.Trap"} <= named
    assert not [row["Name"] for row in _rows("PassiveEffects") if "Type.Deployable" in _tags(row["RequiredTags"])]


def test_unreal_is_given_the_tag():
    assert 'Tag="Type.Gadget"' in TAGS_INI.read_text(encoding="utf-8")


def _copy(tmp_path) -> pathlib.Path:
    copy = tmp_path / "workbook.xlsx"
    shutil.copy(WORKBOOK, copy)
    return copy


def _set(path: pathlib.Path, sheet: str, key_column: str, key: str, column: str, value) -> None:
    book = openpyxl.load_workbook(path)
    ws = book[sheet]
    header = [cell.value for cell in ws[1]]
    for row in ws.iter_rows(min_row=2):
        if row[header.index(key_column)].value == key:
            row[header.index(column)].value = value
    book.save(path)


def test_the_script_writes_the_old_state_once(tmp_path):
    """The three gadget types put back to their tags before the ruling: written once, then nothing left to write."""
    copy = _copy(tmp_path)
    for name, before, _ in script.MINION_TAGS:
        _set(copy, "Minion Types", "Minion Type", name, "Tags", before)
    first = script.apply(copy)
    assert sum(1 for line in first if "Tags + Type.Gadget" in line) == 3, first
    assert script.apply(copy) == ["every tag is already written; nothing written"]


def test_the_script_refuses_a_cell_somebody_changed(tmp_path):
    copy = _copy(tmp_path)
    _set(copy, "Minion Types", "Minion Type", "Ballista", "Tags", "Type.Minion, Minion.Machine")
    with pytest.raises(script.ReapplyError, match="somebody has changed it since"):
        script.apply(copy)


@pytest.mark.parametrize("name, tags, refused", [
    ("BoltTurret", "Type.Minion, Type.Deployable, Minion.Machine, Minion.Projectile",
     "does not carry Type.Gadget"),
    ("SpikeTrap", "Type.Minion, Type.Deployable, Type.Gadget, Minion.Machine, Type.Trap",
     "carries Type.Gadget and is not"),
])
def test_the_generator_refuses_a_gadget_without_the_tag_and_a_trap_with_it(tmp_path, name, tags, refused):
    """THE CONTROL is the workbook as it is, which reads."""
    assert gen.minion_types(openpyxl.load_workbook(WORKBOOK))
    copy = _copy(tmp_path)
    _set(copy, "Minion Types", "Minion Type", name, "Tags", tags)
    with pytest.raises(gen.DataError, match=refused):
        gen.minion_types(openpyxl.load_workbook(copy))


def test_a_reserve_row_may_name_gadgets_alone():
    assert "Type.Gadget" in gen.MINION_RESERVE_PERCENT_REQUIRED_TAGS
