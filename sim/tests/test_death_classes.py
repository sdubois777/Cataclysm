"""Tests for the three Death class stat lines, added 2026-11-24 with their trees.

`test_classes.py` holds the three Demonic lines and the rules they were built
under. These hold the Death lines to the same rules, and to one more that the
Demonic lines met without it being written down: every stat a class's own tree
raises by an "increased" percentage has a base under it.
"""

from __future__ import annotations

import json
import pathlib
import re

import pytest

from cataclysm_sim import character as ch
from cataclysm_sim import classes as cl

REPO = pathlib.Path(__file__).resolve().parents[2]

#: The three Death classes as the game spells them,
#: `UCataclysmCharacterCreation::ClassesByDamageType`.
DEATH = ("Soul Collector", "Necromancer", "Shadow")


def _at_100(name, **kwargs):
    return ch.Character(cl.CLASSES[name], level=100, **kwargs)


def test_the_three_death_classes_are_defined_with_the_games_spelling():
    assert tuple(cl.DEATH_CLASSES) == DEATH
    assert set(cl.CLASSES) == set(cl.DEMONIC_CLASSES) | set(DEATH)


@pytest.mark.parametrize("name", DEATH)
def test_a_death_class_sets_only_stats_whose_base_is_a_classs_to_give(name):
    """Critical strike chance comes from the skill and attack speed from the
    weapon; a class override of either would do nothing."""
    for stat in cl.CLASSES[name].overrides:
        assert stat in ch.ALL_STATS
        assert ch.BASE_SOURCE[stat] == "class", (
            f"{name} overrides {stat}, whose base comes from the "
            f"{ch.BASE_SOURCE[stat]}")


@pytest.mark.parametrize("name", DEATH)
def test_a_death_class_commits_to_a_handful_of_stats(name):
    count = len(cl.CLASSES[name].overrides)
    assert 4 <= count <= 12, f"{name} overrides {count} stats"


def test_no_two_of_the_six_classes_have_the_same_stat_line():
    lines = {name: tuple(sorted((s, d.scaling(s).base, d.scaling(s).per_level)
                                for s in ch.ALL_STATS))
             for name, d in cl.CLASSES.items()}
    assert len(set(lines.values())) == len(lines) == 6


def test_the_shadow_is_the_only_class_with_evasion():
    """Its Evasion limb increases evasion, so it needs a base; no other class's
    identity is avoiding the blow."""
    evading = [n for n in cl.CLASSES if _at_100(n).stat("evasion") > 0]
    assert evading == ["Shadow"]


def test_the_necromancer_trades_health_for_mana_and_is_not_a_second_ritualist():
    """Diablo II's Necromancer gains less life and more mana a level than its
    fighters. Against the Ritualist: no energy shield, some armor, and the
    Ritualist keeps the largest mana pool."""
    generic = ch.Character(ch.GENERIC, level=100)
    necro = _at_100("Necromancer")
    assert necro.stat("max_mana") > generic.stat("max_mana")
    assert necro.stat("max_health") < generic.stat("max_health")
    assert necro.stat("max_energy_shield") == 0
    assert necro.stat("armor") > 0
    mana = {n: _at_100(n).stat("max_mana") for n in cl.CLASSES}
    assert max(mana, key=mana.get) == "Ritualist"


def test_no_death_class_leeches_or_has_an_energy_shield():
    for name in DEATH:
        assert _at_100(name).stat("life_leech") == 0, name
        assert _at_100(name).stat("max_energy_shield") == 0, name


@pytest.mark.parametrize("name", DEATH)
def test_no_death_class_approaches_the_bulwark_tree_thresholds(name):
    assert _at_100(name, attributes=ch.Attributes(vitality=100)).stat("max_health") < 10_000


@pytest.mark.parametrize("name", DEATH)
def test_the_death_classes_keep_the_shared_pool_of_one_hundred(name):
    """The Ritualist's 150 is for thralls that reserve 30 each. No Death class
    was given a different pool; its tree's nodes grow it."""
    assert _at_100(name).stat("class_resource") == 100


# --------------------------------------------------------------------------
# What each tree increases has a base under it
# --------------------------------------------------------------------------

#: For each Death tree: a stat its nodes raise by an "increased" percentage, and
#: the node and the words in its sentence that say so. The words are checked
#: against the tree file, so a node reworded away from the stat fails here
#: rather than leaving a stale entry.
INCREASED_BY_THE_TREE = {
    "Soul Collector": {
        "damage_reduction": ("Pale Ward", "increased Damage Reduction"),
        "crowd_control_resistance": ("Held Close", "increased Crowd Control Resistance"),
        "health_regen": ("Hoarded Life", "increased Health Regeneration"),
        "crit_multiplier": ("Cold Precision", "increased Critical Strike Multiplier"),
        "movement_speed": ("Between the Dead", "increased Movement Speed"),
        "area_of_effect": ("Gathered Weight", "increased Area of Effect"),
        "class_resource": ("Deep Urn", "increased maximum Fervour"),
    },
    "Necromancer": {
        "armor": ("Gravekeeper's Hide", "increased Armor"),
        "damage_reduction": ("Shielding Ranks", "increased Damage Reduction"),
        "crowd_control_resistance": ("Steadfast Among Them",
                                     "increased Crowd Control Resistance"),
        "health_regen": ("Bone Knit", "increased Health Regeneration"),
        "max_health": ("Carapace", "increased Maximum Health"),
        "class_resource": ("Wider Grave", "increased maximum Fervour"),
    },
    "Shadow": {
        "evasion": ("Nimble", "increased Evasion chance"),
        "crit_multiplier": ("Find the Gap", "increased Critical Strike Multiplier"),
        "movement_speed": ("Light Feet", "increased Movement Speed"),
        "dot_duration": ("Lasting Wounds", "increased duration"),
        "dot_damage": ("Lingering", "increased damage over time damage"),
    },
}


def _tree(name: str) -> dict:
    path = REPO / "docs" / f"{name.replace(' ', '_')}_Class_Tree_Final.json"
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return json.loads(path.read_text(encoding="utf-8"))


@pytest.mark.parametrize("name", DEATH)
def test_every_stat_the_tree_increases_has_a_base_on_the_class_line(name):
    """An increase multiplies a base, so a node raising a stat its class has
    none of is worth nothing to the one class that can take it. Issue #1105 was
    two Masochist nodes in that state for the life of the tree."""
    descriptions = {n["data"]["name"]: n["data"]["description"]
                    for n in _tree(name)["nodes"]}
    character = _at_100(name)
    for stat, (node, words) in INCREASED_BY_THE_TREE[name].items():
        assert node in descriptions, f"{name} has no node {node!r}"
        assert words in descriptions[node], (
            f"{name}'s {node} no longer says {words!r}: {descriptions[node]!r}")
        assert character.stat(stat) > 0, (
            f"{name}'s {node} increases {stat} and the class has no base for it, "
            "so the node is worth nothing to it")


# --------------------------------------------------------------------------
# The design document's table
# --------------------------------------------------------------------------

def test_the_design_document_death_table_matches_the_model():
    """`docs/Cataclysm_GDD_v2.md` carries the three lines as a table, typed by
    hand. The Demonic table has the same check in `test_classes.py`."""
    gdd = (REPO / "docs" / "Cataclysm_GDD_v2.md").read_text(encoding="utf-8")
    marker = "The Three Death Class Stat Lines"
    assert marker in gdd
    section = gdd[gdd.index(marker):]
    section = section[:section.index("### **Stat Calculation**")]

    labels = {
        "Maximum Health": "max_health",
        "Maximum Mana": "max_mana",
        "Maximum Energy Shield": "max_energy_shield",
        "Health Regeneration": "health_regen",
        "Mana Regeneration": "mana_regen",
        "Armor": "armor",
        "Evasion": "evasion",
        "Damage Reduction": "damage_reduction",
        "Crowd Control Resistance": "crowd_control_resistance",
        "Critical Strike Multiplier": "crit_multiplier",
        "Movement Speed": "movement_speed",
        "Class Resource": "class_resource",
    }
    checked = 0
    for label, stat in labels.items():
        row = re.search(rf"^\| {re.escape(label)} \| (.+?) \|\r?$", section, re.MULTILINE)
        assert row, f"{label} has no row in the Death table"
        cells = [c.strip() for c in row.group(1).split("|")]
        assert len(cells) == 3, f"{label} does not have three class columns"
        for name, cell in zip(DEATH, cells, strict=True):
            written = float(cell.replace(",", "").rstrip("%"))
            actual = _at_100(name).stat(stat)
            assert abs(written - actual) <= 0.01, (
                f"the design document says {name} has {written} {stat}; the "
                f"model gives {actual}")
            checked += 1
    assert checked == len(labels) * 3 == 36
