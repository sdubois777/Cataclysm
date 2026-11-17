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

    game/Data/*.csv                          every generated table the game loads
    docs/Cataclysm_GDD_v2.md                 the design document
    docs/Empire_Skill_Tree_Keystones.md      the empire tree keystone prose
    docs/Empire_Development_Tree_Final.json  the empire tree node graph

THE LAST TWO WERE ADDED BY ISSUE #247, which finished the rename. They were left
out of #244 for two reasons and one of them turned out to be wrong.

The reason that held: they describe the same empire tree twice and already
disagree with each other, and issue #25 is open to reconcile them. Renaming in
both at once does not make that harder, because it changes node descriptions and
not node identity or structure, and it leaves the two files agreeing on this word
rather than adding a new way for them to differ.

The reason that did not hold: the JSON was said to be authored by a separate tool
outside this repository, so a hand edit risked being overwritten by the next
export. The tool at `C:\\Projects\\PassiveTreeCreator` has no tree data of its own
— `src/utils/serialization.ts` reads a JSON file the user opens and downloads a
JSON file back. This file IS the data. An edit here is carried through by anyone
who opens it in the tool, not overwritten by them.

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

#: The two descriptions of the empire passive tree. The prose covers 12
#: keystones across 4 tiers plus 4 quadrants of nodes; the JSON is the node
#: graph the passive tree editor at C:\PassiveTreeCreator opens and saves.
KEYSTONES = REPO_ROOT / "docs" / "Empire_Skill_Tree_Keystones.md"
TREE_JSON = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"

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


@pytest.mark.parametrize("path", [KEYSTONES, TREE_JSON], ids=lambda p: p.name)
def test_the_empire_tree_documents_do_not_name_the_stat_anything_else(path):
    """Issue #247. Thirteen places across these two called it Loot Rarity."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    hits = offending(path.read_text(encoding="utf-8"))
    assert not hits, (
        f"{path.name} names the magic find stat by another name:\n  "
        + "\n  ".join(hits)
        + f"\n\nThe stat is magic find. {TREE_JSON.name} is the passive tree "
          "editor's own save file, not generated output, so edit it directly "
          "or in the editor. See issue #247.")


#: What each of the eight renamed nodes in the node graph does, matched on the
#: effect rather than on the word, so deleting a sentence cannot pass as a
#: rename. Taken from the descriptions as they read before issue #247.
RENAMED_NODE_EFFECTS = (
    "Magic Find increased by 25% and loot quantity by 100%.",
    "+5% Magic Find per point.",
    "+2% Magic Find for every Floor removed from default dungeon depth.",
    "+4% Magic Find for each unique dungeon modifier active per point.",
    "+1% Magic Find per floor cleared in the current dungeon.",
    "+1% Magic Find per 5 unique dungeons cleared this run per point.",
    "Every 50,000 Gold stored grants +1% Magic Find (cap 20%) per point.",
    "+1% Magic Find per 10,000 Gold currently held.",
)

#: The same five effects as the keystone prose states them. The prose and the
#: graph word things differently -- issue #25 is open about that -- so these are
#: listed separately rather than reused.
RENAMED_KEYSTONE_EFFECTS = (
    "Magic Find is increased by **25%** and loot quantity by **100%**.",
    "Gain +1% Magic Find in dungeons for every 5%",
    "+5% Magic Find per point.",
    "+2% Magic Find for every **Floor** removed",
    "+1% Magic Find per floor cleared in the current dungeon.",
)


def node_descriptions() -> list[str]:
    """Every `description` string anywhere in the node graph."""
    import json

    def walk(obj):
        if isinstance(obj, dict):
            for key, value in obj.items():
                if key == "description" and isinstance(value, str):
                    yield value
                else:
                    yield from walk(value)
        elif isinstance(obj, list):
            for item in obj:
                yield from walk(item)

    return list(walk(json.loads(TREE_JSON.read_text(encoding="utf-8"))))


def test_the_node_graph_still_parses_as_json():
    """It is edited by hand as well as by the passive tree editor, and a broken
    file would make every check below skip rather than fail."""
    if not TREE_JSON.is_file():
        pytest.skip(f"{TREE_JSON.name} is not present")
    assert len(node_descriptions()) > 100, (
        "the empire tree node graph has almost no node descriptions in it")


def test_the_renamed_nodes_in_the_graph_still_grant_the_stat():
    """A rename can be 'passed' by deleting the sentence. Each of the eight is
    checked as a whole description, so a truncated one is caught too."""
    if not TREE_JSON.is_file():
        pytest.skip(f"{TREE_JSON.name} is not present")
    present = set(node_descriptions())
    for effect in RENAMED_NODE_EFFECTS:
        assert effect in present, (
            f"{TREE_JSON.name} has no node whose description is {effect!r}. "
            "Renaming a stat must not quietly change or delete what a node "
            "does. Issue #247.")


def test_the_renamed_keystones_in_the_prose_still_grant_the_stat():
    if not KEYSTONES.is_file():
        pytest.skip(f"{KEYSTONES.name} is not present")
    text = KEYSTONES.read_text(encoding="utf-8")
    for effect in RENAMED_KEYSTONE_EFFECTS:
        assert effect in text, (
            f"{KEYSTONES.name} no longer states {effect!r}. Issue #247.")


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
