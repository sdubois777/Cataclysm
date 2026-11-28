"""The War class stat lines script: what it writes, that it writes it once, and that it refuses a changed number.

Issue #932 (2026-11-27). The numbers are judgements; these tests hold the script's behaviour and the rules the lines must
meet, not the numbers themselves, which the simulation's copy (`sim/cataclysm_sim/classes.py`) is compared against by
`test_class_sheets_match_the_model.py` once the workbook holds them.
"""

from __future__ import annotations

import pathlib
import shutil
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))
sys.path.insert(0, str(REPO_ROOT / "sim"))

import reapply_war_class_stats_workbook as war  # noqa: E402

WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"


def test_every_line_is_a_war_class_and_names_each_stat_once():
    seen = set()
    for class_name, stat, _, _ in war.CLASS_STATS:
        assert class_name in war.WAR_CLASSES, class_name
        assert (class_name, stat) not in seen, (class_name, stat)
        seen.add((class_name, stat))
    assert {c for c, _, _, _ in war.CLASS_STATS} == set(war.WAR_CLASSES)


def test_no_war_class_has_an_evasion_base():
    """No class line names evasion: the rule the Shadow's line was held to on 2026-11-24."""
    assert not [line for line in war.CLASS_STATS if line[1] == "evasion"]


def test_the_simulation_holds_the_same_numbers():
    from cataclysm_sim.classes import WAR_CLASSES

    for class_name, stat, base, per_level in war.CLASS_STATS:
        scaling = WAR_CLASSES[class_name].overrides[stat]
        assert (scaling.base, scaling.per_level) == pytest.approx((base, per_level)), (class_name, stat)
    named = {(c, s) for c, s, _, _ in war.CLASS_STATS}
    held = {(c, s) for c, d in WAR_CLASSES.items() for s in d.overrides}
    assert named == held


@pytest.fixture
def copy(tmp_path):
    if not WORKBOOK.is_file():
        pytest.skip("design workbook not present")
    target = tmp_path / "workbook.xlsx"
    shutil.copyfile(WORKBOOK, target)
    return target


def test_it_writes_the_lines_once_and_then_nothing(copy):
    first = war.apply(copy)
    assert first[-1].startswith(f"wrote {len(war.CLASS_STATS)} row(s)") or first == [
        "every War class line is already as this script writes it; nothing written"], first
    assert war.apply(copy) == ["every War class line is already as this script writes it; nothing written"]
    assert war.apply(copy, check_only=True) == [
        "every War class line is already as this script writes it; nothing written"]


def test_it_refuses_a_war_stat_somebody_changed(copy):
    war.apply(copy)
    changed = tuple((c, s, b + 1 if (c, s) == ("Bulwark", "armor") else b, p) for c, s, b, p in war.CLASS_STATS)
    import openpyxl

    book = openpyxl.load_workbook(copy)
    with pytest.raises(war.ReapplyError, match="Bulwark armor already holds"):
        war.plan_class_stats(book[war.CLASS_SHEET], changed)
