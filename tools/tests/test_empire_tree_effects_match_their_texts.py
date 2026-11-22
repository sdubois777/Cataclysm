"""What each city node of the empire upgrade tree does must be what its own words say.

WHY THIS EXISTS. Issue #2741 slice 3a makes 31 nodes and capstone options act on the
empire's cities, slice 3b seven more that act at an event, slice 4 nineteen
for a dungeon's walk and depth and for a clear, slice 5a fifteen for the
player inside a dungeon, slice 5b five more, and slice 6a eleven for the gold a
kill drops and a city upgrade's price. What each one does is stated once, in `EMPIRE_TREE_EFFECTS` in
`tools/generate_datatables.py`, which becomes `game/Data/EmpireTreeEffects.csv`
and `DT_EmpireTreeEffects`, which `UCataclysmEmpireTree::CityBonusesFor` and
`PlayerBonusesFor` add up.
A value typed there is a copy of a node's text, and this project has recorded
copies drifting before (`CLAUDE.md`).

WHAT IS CHECKED:
  the effect names           the generator's list and the C++ enum agree
  which nodes act            exactly the 31 slice 3a builds, the seven of
                             slice 3b, the nineteen of slice 4, the fifteen of
                             slice 5a, the five of slice 5b and the eleven of
                             slice 6a
  every value                found in its own node's or option's text
  the nine caps              Golden Age +40% and Bastion Spirit +15 days, the
                             coordinating session's S3 and S4 of 2026-11-18;
                             Global Vigilance's 15%, Sovereign's Haste's 30
                             days, Speed Runner's 100% and Efficiency
                             Premium's 50%, their own texts; Reclaimer's
                             Resolve's 15%, the Q2 of 2026-11-19; Architectural
                             Insight's +10 floors, the W4 of 2026-11-19;
                             Temporal Efficiency's +50%, the D3 of 2026-11-19;
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

#: SLICE 4'S NINETEEN, a dungeon's walk and depth and a clear: the
#: coordinating session's W1 to W9 of 2026-11-19.
SLICE_4 = {"Pacing", "Overclock", "Temporal Mastery", "Fleet Footed", "Tactical Entry",
           "The Delver", "Opportunist", "Sovereign's Haste", "Imperial Roads",
           "Rapid Descent", "The Last Stand", "Deep Boring", "Architect of Greed",
           "Exclusionary Mapping", "The Collector's Decree", "Infinite Depths",
           "Architectural Insight", "The Warlord", "Veteran's Edge"}

#: SLICE 5A'S FIFTEEN, the player inside a dungeon: the coordinating session's
#: D1 to D4 of 2026-11-19.
SLICE_5A = {"Eyes of the Empire", "Bounty", "The Hoarder", "Pathfinding", "Spoils of War",
            "Speed Runner", "Efficiency Premium", "Quality over Quantity", "Dungeon Expertise",
            "Manifest Wealth", "Dungeon Legend", "The Cartographer's Blessing",
            "Temporal Efficiency", "One-Day Specialist", "Dungeon Mastery"}

#: SLICE 5B'S FIVE, Relentless Pursuit and the four drop rules: the
#: coordinating session's D5, D6 and E1 to E4 of 2026-11-19, at run record
#: version 11.
SLICE_5B = {"Relentless Pursuit", "Opportunistic Looting", "Plunderer's Eye", "Scavenger",
            "The High Roller"}

#: SLICE 6A'S ELEVEN, the gold a kill drops and a city upgrade's price: the
#: coordinating session's G1 to G3, P1 and P2 of 2026-11-20. Price Controls is
#: 6b's: its P3 reading was withdrawn the same day.
SLICE_6A = {"Coinage", "Minting Rights", "The Midas Touch", "Untouchable Wealth",
            "Wartime Economy", "Profiteering", "Prosperity Index", "Fiscal Policy",
            "Municipal Bonds", "Public Works", "War Funding"}

#: SLICE 6B'S FIVE, on run record version 12: the coordinating session's B1 to
#: B5 of 2026-11-20.
SLICE_6B = {"Price Controls", "Stimulus Package", "Cleanup Crew", "Salvage Rights",
            "Investment Funds"}

#: SLICE 6C'S FIVE, the gold held and the day's income: the coordinating
#: session's B6 to B8 of 2026-11-20.
SLICE_6C = {"The Gilded Path", "Venture Capital", "Imperial Reserve", "Pax Imperialis",
            "Wealth Interest"}

#: SLICE 6D'S TWO, the owner's rulings of 2026-11-20 relayed by the coordinating
#: session (D8, D9).
SLICE_6D = {"Treasury Guard", "Tax Collection"}

#: SLICE 6E'S TWO, the gold debt the owner allowed through them the same day
#: (D1 to D7), on account record version 5 and character record version 11.
SLICE_6E = {"Global Credit", "Sovereign Loans"}

#: SLICE 7'S FOUR, the nodes that read a Treasurer city upgrade (R1 to R4;
#: readings T1 to T4, ruled 2026-11-21).
SLICE_7 = {"Commercial Hubs", "Economic Zones", "Trade Routes", "The Golden Touch"}

#: SLICE 8'S ONE, Imperial Prowess: the project owner's ruling of 2026-11-21 and
#: the coordinating session's J1 to J5 the same day.
SLICE_8 = {"Imperial Prowess"}

DAY_EFFECTS = {"ResolveDaysAll", "ResolveDaysOutposts", "ResolveDaysPerBranchStep",
               "ResolveDaysBelowTenPercent", "SurgeGapDays", "WalkDaysFewerWhenAlone",
               "WalkDaysFewerPerActiveType", "WalkDaysFewerOnFallenCities",
               "WalkDaysFewerPerFloorCleared", "WalkOneDayWithinDaysOfFalling"}

#: AN EFFECT IN FLOORS, found in its text as "<value> floor": the depth nodes.
FLOOR_EFFECTS = {"FloorsAdded", "FloorsAddedPerActiveType", "FloorsPerBranchStep"}

#: AN EFFECT WHOSE VALUE THE TEXT STATES ANOTHER WAY, with the words it is found
#: in: The Delver's "by 5", Tactical Entry's "more than 50 floors".
OTHER_WORDS = {"WalkDaysFewer": "by {:g}", "WalkTimeHalvedAboveFloors": "more than {:g} floors",
               "ExplorerLootDoubledAtOneDay": "{:g}-Day minimum",
               # Slice 6b: Stimulus Package's 1 is its "doubled", and Salvage
               # Rights' 50 is "50 Gold for every" floor.
               "GoldDoubledBelowHalfPopulation": "Gold drops doubled",
               "GoldPerFloorAtClear": "{:g} Gold for every",
               # Slice 6c: Pax Imperialis' 5 is "5 Gold every 1 day".
               "GoldPerThousandCitizensDaily": "{:g} Gold every 1 day",
               # Slice 7: Trade Routes' 50 is "+50 Gold every 20 days".
               "GoldEveryTwentyDaysPerTreasurerCity": "+{:g} Gold every 20 days",
               # Slice 8: Imperial Prowess's 1 is its "an additional keystone".
               "ExtraEarlierMilestoneOption": "an additional keystone"}

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


def test_exactly_the_107_nodes_of_slices_3a_3b_4_5a_5b_6a_6b_6c_6d_6e_7_and_8_act() -> None:
    named = set(gen.EMPIRE_TREE_EFFECTS)
    assert len(SLICE_3A) == 31 and len(SLICE_3B) == 7 and len(SLICE_4) == 19 and len(SLICE_5A) == 15
    assert len(SLICE_5B) == 5 and len(SLICE_6A) == 11 and len(SLICE_6B) == 5 and len(SLICE_6C) == 5
    assert len(SLICE_6D) == 2 and len(SLICE_6E) == 2 and len(SLICE_7) == 4 and len(SLICE_8) == 1
    assert not SLICE_3A & SLICE_3B and not (SLICE_3A | SLICE_3B) & SLICE_4
    assert not (SLICE_3A | SLICE_3B | SLICE_4) & SLICE_5A
    assert not (SLICE_3A | SLICE_3B | SLICE_4 | SLICE_5A) & SLICE_5B
    assert not (SLICE_3A | SLICE_3B | SLICE_4 | SLICE_5A | SLICE_5B) & SLICE_6A
    assert not (SLICE_3A | SLICE_3B | SLICE_4 | SLICE_5A | SLICE_5B | SLICE_6A) & SLICE_6B
    assert not (SLICE_3A | SLICE_3B | SLICE_4 | SLICE_5A | SLICE_5B | SLICE_6A | SLICE_6B) & SLICE_6C
    assert not (SLICE_3A | SLICE_3B | SLICE_4 | SLICE_5A | SLICE_5B | SLICE_6A | SLICE_6B | SLICE_6C) & SLICE_6D
    assert not (SLICE_3A | SLICE_3B | SLICE_4 | SLICE_5A | SLICE_5B | SLICE_6A | SLICE_6B | SLICE_6C | SLICE_6D) & SLICE_6E
    assert not (SLICE_3A | SLICE_3B | SLICE_4 | SLICE_5A | SLICE_5B | SLICE_6A | SLICE_6B | SLICE_6C | SLICE_6D
                | SLICE_6E) & SLICE_7
    assert not (SLICE_3A | SLICE_3B | SLICE_4 | SLICE_5A | SLICE_5B | SLICE_6A | SLICE_6B | SLICE_6C | SLICE_6D
                | SLICE_6E | SLICE_7) & SLICE_8
    built = (SLICE_3A | SLICE_3B | SLICE_4 | SLICE_5A | SLICE_5B | SLICE_6A | SLICE_6B | SLICE_6C | SLICE_6D
             | SLICE_6E | SLICE_7 | SLICE_8)
    assert named == built, (
        f"EMPIRE_TREE_EFFECTS names {sorted(named - built)} beyond slice 3 and "
        f"leaves out {sorted(built - named)}")
    assert all(gen.EMPIRE_TREE_BUILT.get(name) == 3 for name in SLICE_3A | SLICE_3B), (
        "every slice 3 node with an effect is one EMPIRE_TREE_BUILT gives to slice 3")
    assert all(gen.EMPIRE_TREE_BUILT.get(name) == 4 for name in SLICE_4), (
        "every slice 4 node with an effect is one EMPIRE_TREE_BUILT gives to slice 4")
    assert all(gen.EMPIRE_TREE_BUILT.get(name) == 5 for name in SLICE_5A | SLICE_5B), (
        "every slice 5a node with an effect is one EMPIRE_TREE_BUILT gives to slice 5")
    assert all(gen.EMPIRE_TREE_BUILT.get(name) == 6
               for name in SLICE_6A | SLICE_6B | SLICE_6C | SLICE_6D | SLICE_6E), (
        "every slice 6a node with an effect is one EMPIRE_TREE_BUILT gives to slice 6")
    assert all(gen.EMPIRE_TREE_BUILT.get(name) == 7 for name in SLICE_7), (
        "every slice 7 node with an effect is one EMPIRE_TREE_BUILT gives to slice 7")
    assert all(gen.EMPIRE_TREE_BUILT.get(name) == 8 for name in SLICE_8), (
        "every slice 8 option with an effect is one EMPIRE_TREE_BUILT gives to slice 8")


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
            elif kind in FLOOR_EFFECTS:
                wanted = f"{abs(per_point):g} floor"
                if wanted not in lowered:
                    wrong.append(f"{name}: {wanted!r} is not in {words!r}")
            elif kind in OTHER_WORDS:
                wanted = OTHER_WORDS[kind].format(per_point)
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


def test_the_fourteen_caps_are_the_rulings_and_nothing_else_is_capped(texts) -> None:
    caps = {name: effect[4]
            for name, effects in gen.EMPIRE_TREE_EFFECTS.items()
            for effect in effects if len(effect) > 4 and effect[4]}
    # S4: Golden Age +40% in total; S3: Bastion Spirit +15 days in total; Global
    # Vigilance's own "15% cap"; Q2 of 2026-11-19: Reclaimer's Resolve 15%.
    # W4 of 2026-11-19: Architectural Insight +10 floors in total; Sovereign's
    # Haste's own "(Max of -30 floors)", read as 30 days (W8). Slice 5a: Speed
    # Runner's own "cap 100%", Efficiency Premium's own "cap 50%", and D3 of
    # 2026-11-19: Temporal Efficiency +50% in total, Efficiency Premium's figure.
    # Slice 5b, D5 of 2026-11-19: Relentless Pursuit +100% in total.
    assert caps == {"Golden Age": 0.40, "Bastion Spirit": 15.0,
                    "Global Vigilance": 0.15, "Reclaimer's Resolve": 0.15,
                    "Architectural Insight": 10.0, "Sovereign's Haste": 30.0,
                    "Speed Runner": 1.00, "Efficiency Premium": 0.50,
                    "Temporal Efficiency": 0.50, "Relentless Pursuit": 1.00,
                    # Slice 6c, each its own text's: Venture Capital's "cap 20%",
                    # Imperial Reserve's "25% cap", Wealth Interest's "capped at
                    # 5,000 gold per tick" (a tier 1 figure, R10).
                    "Venture Capital": 0.20, "Imperial Reserve": 0.25,
                    "Wealth Interest": 5000.0,
                    # Slice 6d, its own text's "20% cap", held apart from
                    # Imperial Reserve's (D9).
                    "Treasury Guard": 0.20}, caps
    assert "cap 20%" in texts["Venture Capital"]
    assert "20% cap" in texts["Treasury Guard"]
    assert "25% cap" in texts["Imperial Reserve"]
    assert "capped at 5,000 gold per tick" in texts["Wealth Interest"]
    assert "15% cap" in texts["Global Vigilance"]
    assert "Max of -30" in texts["Sovereign's Haste"]
    assert "cap 100%" in texts["Speed Runner"]
    assert "cap 50%" in texts["Efficiency Premium"]
    assert caps["Temporal Efficiency"] == caps["Efficiency Premium"], (
        "D3 gave Temporal Efficiency the cap Efficiency Premium states; they have come apart")
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
