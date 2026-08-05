"""The magic find stat has one name, checked across every shipped table.

WHY THIS EXISTS. Issue #244. One stat had three names. It was `magic_find` on
the character sheet, in `game/Data/Attributes.csv`, in the `Flat magic find`
affix and as the `MagicFind` gameplay attribute — and it was written as "rarity
find" in the gem table, the city upgrade table and a dungeon modifier, and as
"loot rarity" in another dungeon modifier. A player reading the gem Of The Goblin
had no way to know its "Rarity Find" is the same number the affix calls magic
find and stacks with it.

MAGIC FIND IS THE NAME. Not because it is a better phrase, but because it is
already the one the value lives under everywhere it is actually stored, and
renaming those would touch the character sheet, the affix pool, the C++ attribute
set, the gameplay tags and every test that names them. The case for calling it
Rarity Find instead is written down in issue #244 rather than lost.

WHAT THIS FILE CHECKS AND WHERE.

    game/Data/*.csv               every generated table the game loads
    docs/Cataclysm_GDD_v2.md      the design document

WHAT IT DELIBERATELY DOES NOT CHECK.

    docs/Empire_Skill_Tree_Keystones.md     "Loot Rarity", 5 places
    docs/Empire_Development_Tree_Final.json  "Loot Rarity", 8 places

Those two describe the empire passive tree, they already disagree with each other
(issue #25 is open to reconcile them), and the JSON is authored by a separate tool
outside this repository. Renaming there is filed as its own issue rather than
done here.

THE WORD "RARITY" ON ITS OWN IS FINE and is not searched for. Gear rarity is the
eight-tier ladder from Everyday to Cataclysmic and enemy rarity is the Common to
Cataclysm Boss ladder. Both are real and unrelated. Only the phrases that name the
STAT are refused.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: Phrases that name the stat by a name it no longer has. Each is a regular
#: expression so "rarity find" and "loot rarity find" are both caught by the
#: first, and the second only fires on "loot rarity" not followed by "find".
REFUSED: tuple[tuple[str, str], ...] = (
    (r"rarity\s+find", "rarity find"),
    (r"loot\s+rarity", "loot rarity"),
)

WANTED = "magic find"


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


def test_there_are_tables_to_check():
    """A glob that matches nothing would make every check below pass while
    checking nothing at all."""
    assert len(csv_files()) >= 10, (
        f"only found {len(csv_files())} tables in {DATA}")


@pytest.mark.parametrize("path", csv_files(), ids=lambda p: p.name)
def test_no_shipped_table_names_the_stat_anything_else(path):
    hits = offending(path.read_text(encoding="utf-8-sig"))
    assert not hits, (
        f"{path.name} names the magic find stat by another name:\n  "
        + "\n  ".join(hits)
        + "\n\nThe stat is magic find. These tables are generated from "
          "docs/All_Things_Cataclysm.xlsx, so edit the sheet and run "
          "python tools/generate_datatables.py. See issue #244.")


def test_the_design_document_does_not_name_the_stat_anything_else():
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    hits = offending(GDD.read_text(encoding="utf-8"))
    assert not hits, (
        f"{GDD.name} names the magic find stat by another name:\n  "
        + "\n  ".join(hits))


def test_the_tables_that_used_to_use_another_name_still_describe_the_stat():
    """A rename can be 'passed' by deleting the sentence. These three rows are
    the ones that carried the other names, so each is checked to still say what
    it does."""
    expected = {
        "Gems.csv": ("Of The Goblin", "Increases Magic Find by .5%"),
        "CityUpgrades.csv": ("Treasurer",
                             "Dungeons here have 10% increased magic find."),
        "DungeonModifiers.csv": ("Infernal Beacons",
                                 "stacking magic find buff"),
    }
    for name, (row, phrase) in expected.items():
        path = DATA / name
        if not path.is_file():
            pytest.skip(f"{name} not present")
        text = path.read_text(encoding="utf-8-sig")
        assert row in text, f"{name} no longer has a {row} row"
        assert phrase in text, (
            f"{name}'s {row} row no longer says {phrase!r}. Renaming a stat "
            "must not quietly delete what the row does.")


def test_the_design_document_states_that_the_stat_has_one_name():
    """Without a sentence saying so, the next table added is as likely to
    invent a fourth name as to use the right one."""
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    body = GDD.read_text(encoding="utf-8")
    assert "The stat is called Magic Find, and it has only that name." in body


def test_the_design_document_still_allows_rarity_for_gear_and_enemies():
    """The refusals above are narrow on purpose. If this stops being true the
    checks have become a blanket ban on a word the design needs."""
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    body = GDD.read_text(encoding="utf-8")
    assert "Gear rarity" in body
    assert "enemy rarity" in body.lower()


def test_the_stat_is_called_magic_find_where_the_value_lives():
    """The reason Magic Find won rather than Rarity Find: it is already the name
    in the places the number is actually stored. If any of these change, the
    decision recorded in docs/DECISIONS.md has been reversed."""
    from cataclysm_sim import character

    assert WANTED.replace(" ", "_") in character.DEFAULT_STAT_LINE
    assert WANTED.replace(" ", "_") in character.ATTRIBUTE_EFFECTS["luck"]

    attributes = (DATA / "Attributes.csv").read_text(encoding="utf-8-sig")
    assert "luck_magic_find" in attributes

    affixes = (DATA / "Affixes.csv").read_text(encoding="utf-8-sig")
    assert "Flat magic find" in affixes

    header = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
              / "CataclysmCombatAttributeSet.h")
    if header.is_file():
        assert "FGameplayAttributeData MagicFind;" in header.read_text(
            encoding="utf-8")
