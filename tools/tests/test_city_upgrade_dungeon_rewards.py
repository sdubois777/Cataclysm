"""The five city upgrades that change what a kill pays in a dungeon standing on the city.

WHY THIS EXISTS. Issue #2781, the coordinating session's rulings 1 to 4 and readings X1 to X6 of 2026-11-20. The four
Treasurer upgrades and the Artisan one were refused for wiring alone: experience, magic find, gold, loot and crafting
materials all exist where a dungeon standing on a city is walked. They are built now, each at its tier 1 figure, for
kills in a dungeon standing on the city that holds it, and for none on a fallen city (X1).

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.CityDungeonRewards.` automation
tests never run on a pull request, and the kill's own call is where no automation test reaches.

WHAT IS CHECKED:

  1. the five effects are built, and the table's five rows state the figures the rewards read;
  2. the rewards multiply every "more" and add the magic find, and a fallen city's give nothing;
  3. a kill asks for them before its paying branch, adds the magic find and multiplies the loot quantity before it
     rolls, passes the materials figure to the drop roll, multiplies the gold by the city's after the tree's, and gives
     both experience grants the city's figure;
  4. the drop roll gives the materials figure to the material roll alone;
  5. no refusal on the empire tree says a Treasurer city upgrade cannot be bought, now that one can.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
UPGRADE_CPP = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmCityUpgrade.cpp"
MODE_CPP = SOURCE / "Cataclysm" / "Dungeon" / "CataclysmDungeonGameMode.cpp"
ENEMY = SOURCE / "Cataclysm" / "Character" / "CataclysmEnemyCharacter.cpp"
SPAWNER = SOURCE / "Cataclysm" / "Items" / "CataclysmDroppedItem.cpp"
UPGRADES_CSV = REPO_ROOT / "game" / "Data" / "CityUpgrades.csv"
NODES_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeNodes.csv"

FIVE = ("DungeonExperience", "DungeonMagicFind", "DungeonGold", "DungeonLoot", "DungeonCraftingMaterials")


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def body(text: str, signature: str, where: str) -> str:
    """The braces-matched body of the function whose signature opens at `signature`."""
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[opening:index + 1]
    raise AssertionError(f"{signature!r} in {where} is not closed")


def test_the_five_are_built_and_the_table_states_their_figures() -> None:
    built = body(code(UPGRADE_CPP), "bool UCataclysmCityUpgradeRules::IsBuilt(", "CataclysmCityUpgrade.cpp")
    first_true = built.index("return true;")
    for effect in FIVE:
        case = built.find(f"ECataclysmCityUpgradeEffect::{effect}:")
        assert -1 < case < first_true, f"{effect} is not among the effects IsBuilt answers true for"

    with UPGRADES_CSV.open(encoding="utf-8", newline="") as handle:
        rows = {row["Effect"]: row for row in csv.DictReader(handle)}
    wanted = {"Dungeons here provide 2x more experience": ("Multiplier", 2.0),
              "Dungeons here have 10% increased magic find.": ("Percent", 0.1),
              "Dungeons here drop 25% more gold.": ("Percent", 0.25),
              "Dungeons here drop 25% more loot.": ("Percent", 0.25),
              "Dungeons here drop 25% more crafting materials.": ("Percent", 0.25)}
    for words, (kind, value) in wanted.items():
        assert words in rows, f"game/Data/CityUpgrades.csv no longer has {words!r}"
        assert rows[words]["Tier1Kind"] == kind and float(rows[words]["Tier1Value"]) == value, (
            f"{words!r} is {rows[words]['Tier1Kind']} {rows[words]['Tier1Value']}, where the rewards read {kind} {value}")


def test_every_more_multiplies_the_magic_find_adds_and_a_fallen_city_gives_nothing() -> None:
    rewards = body(code(UPGRADE_CPP), "FCataclysmCityDungeonRewards UCataclysmCityUpgradeRules::DungeonRewardsOf(",
                   "CataclysmCityUpgrade.cpp")
    for field in ("ExperienceMore", "GoldMore", "LootMore", "MaterialsMore"):
        assert re.search(rf"Rewards\.{field}\s*\*=", rewards), f"DungeonRewardsOf does not multiply {field}"
    assert re.search(r"Rewards\.MagicFindIncreased\s*\+=\s*100\.0f\s*\*\s*Upgrade\.Value", rewards), (
        "DungeonRewardsOf does not add the magic find as percentage points")

    here = body(code(MODE_CPP), "FCataclysmCityDungeonRewards ACataclysmDungeonGameMode::CityRewardsHere(",
                "CataclysmDungeonGameMode.cpp")
    fallen = here.find("City->bFallen")
    assert fallen != -1, "CityRewardsHere does not ask whether the city has fallen (X1)"
    assert here.find("return FCataclysmCityDungeonRewards();", fallen) != -1, (
        "CityRewardsHere does not give nothing for a fallen city (X1)")


def test_a_kill_takes_the_city_rewards_before_it_rolls_and_in_each_grant() -> None:
    handler = body(code(ENEMY), "void ACataclysmEnemyCharacter::HandleDeath()", "CataclysmEnemyCharacter.cpp")
    asked = handler.find("ACataclysmDungeonGameMode::CityRewardsIn(World)")
    paying = handler.find("if (bPays)")
    assert -1 < asked < paying, "the kill does not ask for the city's rewards before its paying branch"
    for change in (r"MagicFind\s*\+=\s*CityRewards\.MagicFindIncreased\s*;",
                   r"LootQuantity\s*\*=\s*CityRewards\.LootMore\s*;"):
        found = re.search(change, handler)
        assert found and found.start() < paying, f"the kill does not apply {change!r} before it rolls"

    drops = re.search(r"UCataclysmDropSpawner::SpawnDropsFor\(([^;]*)\);", handler)
    # Slice 7 of #2741 multiplies the city's figure by The Golden Touch's (T4), and the city's is still the last
    # argument's first factor.
    assert drops and re.search(r"CityRewards\.MaterialsMore(\s*\*\s*TreeDrops\.MaterialsMultiplier)?$",
                               drops.group(1).rstrip()), (
        "the kill does not give its drop roll the city's materials figure")
    gold = re.search(r"UCataclysmDropSpawner::SpawnGoldFor\(([^;]*)\);", handler)
    last = gold.group(1).rsplit(",", 1)[-1] if gold else ""
    # Since #2824 slice c the multiplier reaches the spawner through `FCataclysmPowerGapRule::KillLootFor`, which
    # takes the tree's times the city's as its second argument and multiplies it by the Power Score gap's loot factor.
    if "KillLoot.GoldMultiplier" in last:
        killloot = re.search(r"FCataclysmPowerGapRule::KillLootFor\(([^;]*)\);", handler)
        last = killloot.group(1).split(",")[1] if killloot else ""
    assert "TreeDrops.GoldMultiplier" in last and "CityRewards.GoldMore" in last, (
        f"the kill's gold is not the tree's multiplier times the city's: {last.strip()!r}")
    for grant in ("State->GrantExperience(", "State->GrantSkillExperience("):
        at = handler.find(grant)
        assert at != -1, f"the kill no longer calls {grant}"
        call = handler[at:handler.index(";", at)]
        assert "CityRewards.ExperienceFrom(" in call, f"{grant} does not take the city's experience figure"


def test_the_drop_roll_gives_the_materials_figure_to_the_material_roll_alone() -> None:
    spawner = body(code(SPAWNER), "int32 UCataclysmDropSpawner::SpawnDropsFor(", "CataclysmDroppedItem.cpp")
    gear = spawner[spawner.index("UCataclysmDropRoll::ExpectedGearDrops("):]
    gear = gear[:gear.index(")")]
    materials = spawner[spawner.index("UCataclysmDropRoll::ExpectedMaterialDrops("):]
    materials = materials[:materials.index("Stream")]
    assert "MaterialsMore" in materials, "the material roll does not take the city's materials figure"
    assert "MaterialsMore" not in gear, "the gear roll takes the city's materials figure"


def test_no_refusal_says_a_treasurer_city_upgrade_cannot_be_bought() -> None:
    with NODES_CSV.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    false = [row["NodeName"] for row in rows if "Treasurer city upgrade, which cannot be bought" in row["WaitsOn"]]
    assert not false, f"these nodes tell the player a Treasurer city upgrade cannot be bought: {false}"
