"""What each city node of the empire upgrade tree does must be what its own words say.

WHY THIS EXISTS. Issue #2741 slice 3a makes 31 nodes and capstone options act on the
empire's cities, and slice 3b seven more that act at an event. What each one does is stated once, in `EMPIRE_TREE_EFFECTS` in
`tools/generate_datatables.py`, which becomes `game/Data/EmpireTreeEffects.csv`
and `DT_EmpireTreeEffects`, which `UCataclysmEmpireTree::CityBonusesFor` adds up.
A value typed there is a copy of a node's text, and this project has recorded
copies drifting before (`CLAUDE.md`).

WHAT IS CHECKED:
  the effect names           the generator's list and the C++ enum agree
  which nodes act            exactly the 31 slice 3a builds and the seven of
                             slice 3b; slices 4 and 5 have none yet
  every value                found in its own node's or option's text
  the four caps              Golden Age +40% and Bastion Spirit +15 days, the
                             coordinating session's S3 and S4 of 2026-11-18;
                             Global Vigilance's 15%, its own text; and
                             Reclaimer's Resolve's 15%, the Q2 of 2026-11-19;
                             and no other effect has one
  the simulation's preset    the city-health and damage nodes, at full points,
                             for a Sanctuary next to the Pillar, give the
                             simulation's `TREE_ARCHITECT_AS_DESIGNED`

WHAT IS NOT: what the game does with the table. That is the Unreal group
`Cataclysm.EmpireTreeCity.`.
"""

from __future__ import annotations

import csv
import json
import math
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TREE_FILE = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"
NODES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv"
EFFECTS_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeEffects.csv"
ROWS_HEADER = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Data" / "CataclysmDataRows.h"
sys.path.insert(0, str(REPO_ROOT / "tools"))
sys.path.insert(0, str(REPO_ROOT / "sim"))

import generate_datatables as gen  # noqa: E402
from cataclysm_sim.config import TREE_ARCHITECT_AS_DESIGNED  # noqa: E402

#: THE 31 THAT ACT IN SLICE 3A, by name. The coordinating session's S2 of
#: 2026-11-18 split slice 3: these, which act through numbers the run already
#: saves, and seven event rules that need run state (slice 3b).
SLICE_3A = {
    # Maximum defence.
    "Foundation", "Fortified Gates", "Masonry Techniques", "Reinforced Walls",
    "Monument Building", "Beacon of Hope", "Imperial Decree", "The Aegis of Hope",
    "The Royal Engineer's Blessing",
    # Population.
    "Urban Planning", "Imperial Census", "Metropolitan Growth", "Manifest Destiny",
    "Golden Age",
    # Damage taken.
    "Structural Integrity", "Urban Fortification", "Civil Defense", "Supply Lines",
    "Siege Resistance", "Sovereign's Might", "Unyielding Defense", "Fortified Pillar",
    "Iron Will", "Imperial Command", "The Sentinel",
    # Days.
    "Border Patrol", "Emergency Shelters", "Strategic Reserve", "Bastion Spirit",
    "Martial Law", "The Flood Barrier",
}

#: SLICE 3B'S SEVEN, which act at an event and need run state: the
#: coordinating session's Q1 to Q8 of 2026-11-19.
SLICE_3B = {"Civil Order", "The Mason's Guild", "The Architect's Legacy",
            "Last Stand Protocols", "Enduring Foundations", "Global Vigilance",
            "Reclaimer's Resolve"}

#: GLOBAL VIGILANCE AT ITS 15% CAP, as the simulation's damage product counts it
#: (config.py, the derivation above `TREE_ARCHITECT_AS_DESIGNED`). The game
#: counts it from the clears since the last surge (slice 3b), so the preset
#: check below multiplies the cap in, and `test_the_four_caps...` holds the
#: table's cap to it.
GLOBAL_VIGILANCE_AT_ITS_CAP = 0.85

DAY_EFFECTS = {"ResolveDaysAll", "ResolveDaysOutposts", "ResolveDaysPerBranchStep",
               "ResolveDaysBelowTenPercent", "SurgeGapDays"}

#: AN EFFECT WHOSE VALUE IS A DEFENCE, found in its text as "at <value>": Last
#: Stand Protocols' "survive at 1".
SURVIVES_AT_EFFECTS = {"SanctuarySurvivesAt"}


@pytest.fixture(scope="module")
def texts() -> dict[str, str]:
    """Every node's and option's own words, by name."""
    tree = json.loads(TREE_FILE.read_text(encoding="utf-8"))
    out = {}
    for node in tree["nodes"]:
        body = node.get("data", {})
        out[gen.clean(body.get("name", ""))] = gen.clean(body.get("description", ""))
        for option in body.get("options", []) or []:
            out[gen.clean(option.get("name", ""))] = gen.clean(option.get("description", ""))
    return out


@pytest.fixture(scope="module")
def max_points() -> dict[str, int]:
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        return {row["NodeName"]: int(row["MaxPoints"]) for row in csv.DictReader(handle)}


def test_the_effect_table_is_what_the_generator_writes() -> None:
    with EFFECTS_CSV.open(encoding="utf-8", newline="") as handle:
        written = list(csv.DictReader(handle))
    built = [{key: str(value) for key, value in row.items()} for row in gen.empire_tree_effects()]
    assert written == built, (
        "game/Data/EmpireTreeEffects.csv is not what the generator writes. Run "
        "python tools/generate_datatables.py")


def test_the_effect_names_agree_with_the_cpp_enum() -> None:
    text = ROWS_HEADER.read_text(encoding="utf-8")
    found = re.search(r"enum class ECataclysmEmpireTreeEffect : uint8\s*\{(.*?)\};", text, re.S)
    assert found, "CataclysmDataRows.h has no ECataclysmEmpireTreeEffect enum"
    body = re.sub(r"/\*.*?\*/", "", found.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    names = [name.strip() for name in body.split(",") if name.strip()]
    assert names[0] == "None", f"the enum's first value is {names[0]!r}, not None"
    assert tuple(names[1:]) == gen.EMPIRE_TREE_EFFECT_KINDS, (
        f"ECataclysmEmpireTreeEffect is {names[1:]} and EMPIRE_TREE_EFFECT_KINDS is "
        f"{list(gen.EMPIRE_TREE_EFFECT_KINDS)}; they must be the same names in the same order")


def test_exactly_the_38_nodes_of_slices_3a_and_3b_act() -> None:
    named = set(gen.EMPIRE_TREE_EFFECTS)
    assert len(SLICE_3A) == 31 and len(SLICE_3B) == 7
    assert not SLICE_3A & SLICE_3B
    built = SLICE_3A | SLICE_3B
    assert named == built, (
        f"EMPIRE_TREE_EFFECTS names {sorted(named - built)} beyond slice 3 and "
        f"leaves out {sorted(built - named)}")
    assert all(gen.EMPIRE_TREE_BUILT.get(name) == 3 for name in named), (
        "every node with an effect is one EMPIRE_TREE_BUILT gives to slice 3")


def test_every_value_is_in_its_own_words(texts) -> None:
    wrong = []
    for name, effects in gen.EMPIRE_TREE_EFFECTS.items():
        words = texts[name]
        lowered = words.lower()
        for effect in effects:
            kind, per_point = effect[0], effect[1]
            if kind in DAY_EFFECTS:
                wanted = f"{per_point:g} day"
                if wanted not in lowered:
                    wrong.append(f"{name}: {wanted!r} is not in {words!r}")
            elif kind in SURVIVES_AT_EFFECTS:
                wanted = f"at {per_point:g}"
                if wanted not in words:
                    wrong.append(f"{name}: {wanted!r} is not in {words!r}")
            else:
                wanted = f"{abs(per_point) * 100:g}%"
                if wanted not in words:
                    wrong.append(f"{name}: {wanted!r} is not in {words!r}")
            if len(effect) > 2 and effect[2]:
                step, branch = effect[2], effect[3]
                if f"{step} points" not in words or branch not in words:
                    wrong.append(f"{name}: '{step} points' and {branch!r} are not both in {words!r}")
    assert not wrong, "\n".join(wrong)


def test_the_four_caps_are_the_rulings_and_nothing_else_is_capped(texts) -> None:
    caps = {name: effect[4]
            for name, effects in gen.EMPIRE_TREE_EFFECTS.items()
            for effect in effects if len(effect) > 4 and effect[4]}
    # S4: Golden Age +40% in total; S3: Bastion Spirit +15 days in total; Global
    # Vigilance's own "15% cap"; Q2 of 2026-11-19: Reclaimer's Resolve 15%.
    assert caps == {"Golden Age": 0.40, "Bastion Spirit": 15.0,
                    "Global Vigilance": 0.15, "Reclaimer's Resolve": 0.15}, caps
    assert "15% cap" in texts["Global Vigilance"]
    assert math.isclose(1.0 - caps["Global Vigilance"], GLOBAL_VIGILANCE_AT_ITS_CAP), (
        "the table's Global Vigilance cap and the one the simulation's preset multiplies in differ")


def test_the_city_health_nodes_give_the_simulations_preset(max_points) -> None:
    """For the preset's scenario, a Sanctuary next to the Pillar, at full points.

    THE ROYAL ENGINEER'S BLESSING IS LEFT OUT, as the preset leaves it out: it
    grows with how many cities hold a defensive upgrade, which a tree does not
    say. Fortified Gates and The Aegis of Hope are for Outposts.
    """
    reaches_a_sanctuary_in_ring_1 = {"MaxDefenceAll", "MaxDefenceBulwarksAndSanctuaries",
                                     "MaxDefenceSanctuaries", "MaxDefenceNearPillar"}
    total = sum(effect[1] * max_points[name]
                for name, effects in gen.EMPIRE_TREE_EFFECTS.items()
                for effect in effects if effect[0] in reaches_a_sanctuary_in_ring_1)
    assert math.isclose(1.0 + total, TREE_ARCHITECT_AS_DESIGNED.city_health_mult, rel_tol=1e-9), (
        f"the table's maximum-defence increases for a Sanctuary next to the Pillar sum to "
        f"x{1.0 + total:.4f}; the simulation's preset says x{TREE_ARCHITECT_AS_DESIGNED.city_health_mult}")


def test_the_damage_nodes_give_the_simulations_preset(max_points) -> None:
    """The ten damage nodes the preset multiplies, at full points, for the same Sanctuary.

    Each at full points; Iron Will at a full empire; Imperial Command for a city
    holding two upgrades; Global Vigilance, slice 3b's, at its cap. The Sentinel
    is a capstone option the preset leaves out. Unyielding Defense is the
    preset's per-type field.
    """
    in_the_product = {"DamageTakenCompounded", "PopulationDamageTakenCompounded",
                      "DamageTakenAdjacentToPillarCompounded",
                      "PopulationDamageTakenNearPillarCompounded", "DamageTakenByLivingShare",
                      "DamageTakenWithUpgrades"}
    product = GLOBAL_VIGILANCE_AT_ITS_CAP
    per_type = 1.0
    for name, effects in gen.EMPIRE_TREE_EFFECTS.items():
        for effect in effects:
            if effect[0] in in_the_product:
                product *= (1.0 - effect[1]) ** max_points[name]
            elif effect[0] == "DamageTakenPerActiveTypeCompounded":
                per_type *= (1.0 - effect[1]) ** max_points[name]
    assert math.isclose(product, TREE_ARCHITECT_AS_DESIGNED.city_damage_mult, rel_tol=1e-3), (
        f"the table's damage nodes multiply to {product:.6f}; the simulation's preset says "
        f"{TREE_ARCHITECT_AS_DESIGNED.city_damage_mult:.6f}")
    assert math.isclose(per_type, TREE_ARCHITECT_AS_DESIGNED.city_damage_mult_per_type, rel_tol=1e-9), (
        f"Unyielding Defense gives {per_type:.6f} a type; the preset says "
        f"{TREE_ARCHITECT_AS_DESIGNED.city_damage_mult_per_type:.6f}")
