"""The critical strike multiplier has one name, and the event has one name.

WHY THIS EXISTS. Issue #660. One stat had two names. It was the critical strike
multiplier on the character sheet, in the affix pool, in the Masochist class tree
and as the `CritMultiplier` gameplay attribute -- and it was written as "critical
strike damage" in eight Berserker passive tree nodes and a ninth keystone. A
player reading two nodes had no way to know they add up. The event had two names
as well: the design document says "critical strike" throughout and the
Enchantments sheet said "critical hit" nineteen times.

THE PROJECT OWNER SETTLED BOTH ON 2026-08-17. The stat is the critical strike
MULTIPLIER. They answered only that, and the event's name follows from it: their
chosen name contains "critical strike", and the design document already used that
everywhere, so "critical hit" is the odd one out rather than a second valid form.

`test_magic_find_has_one_name.py` is the pattern this file follows, against the
same class of failure and for the same reason. The design document states the
rule for that stat in as many words -- "The stat is called Magic Find, and it has
only that name ... one name is what lets a player add them up" -- and it applies
here unchanged.

WHAT THIS CHECKS AND WHERE.

    game/Data/*.csv                every generated table the game loads
    docs/Cataclysm_GDD_v2.md       the design document
    docs/*_Class_Tree_*.json       every class passive tree

THE CLASS TREES ARE DATA, NOT EXPORTS, which is worth saying because issue #660
says the opposite. It claims the rename "has to happen there and be re-exported
rather than edited in place". Issue #247 established otherwise: the tool at
`C:\\Projects\\PassiveTreeCreator` has no tree data of its own and reads whichever
JSON file the user opens. These files ARE the data, so an edit here is carried
through by anyone who opens them.

WHAT IS DELIBERATELY NOT CHECKED. `docs/DECISIONS.md`, which is a log of what was
decided and when. Its older entries quote text that has since been reworded and
they were accurate when written; rewriting them would be rewriting history rather
than fixing a name. The magic find check leaves it out for the same reason.

ABBREVIATIONS ARE FINE AND ARE NOT REFUSED. "Crit Chance" is a column heading in
the Weapon Skills sheet and "Crit Multiplier" appears in the attribute table.
Both are the same words shortened, not different names, and the sheets have used
them for months. What is refused is a phrase that names the stat something else.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: Phrases that name the stat or the event by a name they no longer have.
#:
#: "crit multi" IS BOUNDED ON THE RIGHT so it does not fire on "crit
#: multiplier", which is the accepted short form. Without the boundary this
#: check would refuse the very spelling it exists to protect.
REFUSED: tuple[tuple[str, str], ...] = (
    (r"critical\s+strike\s+damage", "critical strike damage"),
    (r"critical\s+hit", "critical hit"),
    (r"\bcrit\s+multi\b", "crit multi"),
)


def offending(text: str) -> list[str]:
    """Every refused phrase present, with a little of the line around it."""
    found = []
    for line in text.splitlines():
        for pattern, label in REFUSED:
            if re.search(pattern, line, re.IGNORECASE):
                found.append(f"{label}: {line.strip()[:160]}")
    return found


def csv_files() -> list[pathlib.Path]:
    """Found by glob rather than listed, so a table added later is covered."""
    return sorted(DATA.glob("*.csv"))


def class_trees() -> list[pathlib.Path]:
    """Every class passive tree, for the same reason."""
    return sorted((REPO_ROOT / "docs").glob("*_Class_Tree_*.json"))


def test_there_are_tables_and_trees_to_check():
    """A glob that matched nothing would make every check below vacuous."""
    assert len(csv_files()) >= 10, (
        f"only found {len(csv_files())} tables in {DATA}")
    assert len(class_trees()) >= 3, (
        f"only found {[p.name for p in class_trees()]} in docs/")


def test_the_refused_patterns_actually_match_what_they_are_for():
    """A regular expression that matches nothing makes this file worthless.

    The first test guards against an empty file list; this one guards against
    patterns that would not fire even on the exact text they were written for.
    """
    assert offending("+5% critical strike damage per point on melee attacks.")
    assert offending("Your critical hit chance cannot exceed 30%-50%")
    assert offending("Increases Crit Multi by 5%")

    # And the accepted spellings are NOT refused, which is the other half.
    assert not offending("+5% critical strike multiplier per point")
    assert not offending("Increases Crit Multiplier by 5%")
    assert not offending("Crit chance | 100% | Hard")
    assert not offending("critical strikes drain an additional 3 Fury per hit")


@pytest.mark.parametrize("path", csv_files(), ids=lambda p: p.name)
def test_no_shipped_table_names_it_anything_else(path):
    hits = offending(path.read_text(encoding="utf-8-sig"))
    assert not hits, (
        f"{path.name} uses a name the project has replaced:\n  "
        + "\n  ".join(hits)
        + "\n\nThe stat is the critical strike multiplier and the event is a "
          "critical strike. These tables are generated from "
          "docs/All_Things_Cataclysm.xlsx, so edit the sheet and run "
          "python tools/generate_datatables.py. See issue #660.")


@pytest.mark.parametrize("path", class_trees(), ids=lambda p: p.name)
def test_no_class_passive_tree_names_it_anything_else(path):
    hits = offending(path.read_text(encoding="utf-8"))
    assert not hits, (
        f"{path.name} uses a name the project has replaced:\n  "
        + "\n  ".join(hits)
        + "\n\nThese files are the passive tree data itself, not an export, so "
          "edit them here. See issue #660.")


def test_the_design_document_does_not_name_it_anything_else():
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    hits = offending(GDD.read_text(encoding="utf-8"))
    assert not hits, (
        f"{GDD.name} uses a name the project has replaced:\n  "
        + "\n  ".join(hits))
