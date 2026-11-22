"""A city upgrade takes days to build, by its tier, at run record version 13.

WHY THIS EXISTS. Issue #2825. The owner reversed on 2026-11-21 the ruling of 2026-11-18 that a city upgrade is instant
("They need build times. Probably based on their tier."), and the coordinating session ruled the rest the same day:
6, 12 and 24 days for upgrade tiers 1 to 3, the 6 derived from the two empire tree nodes that shorten a build and their
shared floor of a day; the gold paid and the slot taken at purchase, nothing done until the build is finished; a Siege
or a fall pausing it; one build at a time per city; the run record carrying the build.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.CityUpgradeBuild.`,
`Cataclysm.CityUpgradeBuildSave.` and `Cataclysm.CityScreen.` automation tests never run on a pull request.

WHAT IS CHECKED:

  1. the days per tier are the ruled ones, and tier 1's is the sum the tree file's two build nodes and their floor give;
  2. the purchase passes the days to the run, and the run puts a build on the city's list rather than applying it;
  3. the day step counts builds before the city upgrade repairs and the day's assaults, and a fallen or besieged city
     counts none;
  4. one build at a time is refused after the slot count and before the price, an upgrade being built is a duplicate,
     and it takes its slot;
  5. the run record is at version 13 or later, carries the build, and `Migrate_12_to_13` writes nothing building;
  6. `Run_v13.json` is `Run_v12.json` at version 13 with the build fields added, one of them a build under way;
  7. every test that runs an older run file through the chain adds what `Migrate_12_to_13` writes, and no test reads
     `Run_v12.json` but the one that migrates it; the fixtures README lists `Run_v13.json`;
  8. the city screen, the map's city detail and the console say the build, in one wording;
  9. War Funding's "first upgrade in a city" counts an upgrade being built as the first;
 10. the design document's "City Upgrades" section states the three figures the code holds.
"""

from __future__ import annotations

import json
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
TESTS = SOURCE / "Cataclysm" / "Tests"
EMPIRE = SOURCE / "CataclysmEmpire" / "Empire"
TREE = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"

#: The one test that reads `Run_v12.json` since the run record went to version 13: it migrates it.
MIGRATION_TEST = "ARunFileFromVersion12MigratesBuildingNothing"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def braced(text: str, start: int, where: str) -> str:
    """The braces-matched block that opens at the first `{` after `start`."""
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[opening:index + 1]
    raise AssertionError(f"{where} is not closed")


def body(text: str, signature: str, where: str) -> str:
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    return braced(text, start, f"{signature!r} in {where}")


def node_text(name: str) -> tuple[str, int]:
    """A node's description and its maximum points, from the tree file."""
    for node in json.loads(TREE.read_text(encoding="utf-8"))["nodes"]:
        if node["data"].get("name") == name:
            return node["data"]["description"], int(node["data"].get("maxPoints", 1))
    raise AssertionError(f"{TREE.name} has no node {name!r}")


# --------------------------------------------------------------------------
# 1: the days
# --------------------------------------------------------------------------

def test_the_days_per_tier_are_the_ruled_ones_and_tier_one_is_derived_from_the_two_nodes() -> None:
    header = code(EMPIRE / "CataclysmCityUpgrade.h")
    days = {name: int(value) for name, value in
            re.findall(r"static constexpr int32 (BuildDaysTier[123]|MinimumBuildDays) = (\d+);", header)}
    assert days == {"BuildDaysTier1": 6, "BuildDaysTier2": 12, "BuildDaysTier3": 24, "MinimumBuildDays": 1}, (
        f"the build days read {days}; the coordinating session ruled 6, 12 and 24 and a floor of 1 on 2026-11-21")

    renovation, renovation_points = node_text("Rapid Renovation")
    funding, _ = node_text("Rapid Funding")
    per_point = re.search(r"-(\d+) Day to city upgrade construction time", renovation)
    rushed = re.search(r"reduce its construction time by (\d+) days", funding)
    floors = {re.search(r"Min (\d+) day", text).group(1) for text in (renovation, funding)}
    assert per_point and rushed and len(floors) == 1, (renovation, funding)
    derived = int(per_point.group(1)) * renovation_points + int(rushed.group(1)) + int(floors.pop())
    assert derived == days["BuildDaysTier1"], (
        f"Rapid Renovation ({renovation_points} x {per_point.group(1)}), Rapid Funding ({rushed.group(1)}) and their "
        f"floor sum to {derived}, and tier 1 takes {days['BuildDaysTier1']}: tier 1 was derived as the least at which "
        "both nodes act in full")
    assert days["BuildDaysTier2"] == 2 * days["BuildDaysTier1"] and days["BuildDaysTier3"] == 2 * days["BuildDaysTier2"], (
        "tiers 2 and 3 are the price ladder's doubling, the judgement ruled with them")
    assert "BuildDays = 0" not in header and "IsInstant" not in code(EMPIRE / "CataclysmCityUpgrade.cpp"), (
        "the constant build days of nought, or the guard on them, came back")


# --------------------------------------------------------------------------
# 2 and 3: the purchase and the day
# --------------------------------------------------------------------------

def test_the_purchase_passes_the_days_and_the_run_puts_a_build_on_the_list() -> None:
    purchase = code(SOURCE / "Cataclysm" / "Player" / "CataclysmCityUpgradePurchase.cpp")
    buy = body(purchase, "ECataclysmCityUpgradeResult UCataclysmCityUpgradePurchase::Buy(", "the purchase")
    assert "const int32 Days = BuildDaysIn(WorldContext, Run, RowName, CityId);" in buy, (
        "Buy does not ask BuildDaysIn for the days, so the days shown and the days counted could differ")
    assert "Run->BuyCityUpgrade(CityId, Upgrade, Price, Held, Days)" in buy, (
        "Buy does not pass the days to the run, so every upgrade the game sells would be finished at once")
    days_in = body(purchase, "int32 UCataclysmCityUpgradePurchase::BuildDaysIn(", "the purchase")
    assert "UCataclysmCityUpgradeRules::BuildDaysFor(" in days_in, "BuildDaysIn does not read the tier's days"

    run = code(EMPIRE / "CataclysmEmpireRun.cpp")
    bought = body(run, "ECataclysmCityUpgradeResult UCataclysmEmpireRun::BuyCityUpgrade(", "the run")
    started = bought.find("Map->StartBuild(CityId, Bought);")
    finished = bought.find("CompleteCityUpgrade(CityId, Bought, Clock->Day);")
    assert -1 < started < finished and "if (BuildDays > 0)" in bought, (
        "BuyCityUpgrade does not put an upgrade with build days on the city's list before the at-once path")
    complete = body(run, "void UCataclysmEmpireRun::CompleteCityUpgrade(", "the run")
    assert "Bought.BoughtDay = Day;" in complete and "Map->AddUpgrade(CityId, Bought);" in complete, (
        "CompleteCityUpgrade does not stamp the finishing day and apply the upgrade")


def test_the_day_counts_builds_before_the_repairs_and_the_assaults_and_pauses_a_fallen_or_besieged_city() -> None:
    run = code(EMPIRE / "CataclysmEmpireRun.cpp")
    day = body(run, "FCataclysmDayReport UCataclysmEmpireRun::AdvanceDay(", "the run")
    builds = day.find("AdvanceCityBuilds(Today, Report);")
    repairs = day.find("RunCityUpgradeIntervals(Today);")
    timers = day.find("Clock->AdvanceDay();")
    assert -1 < builds < repairs < timers, (
        "AdvanceDay does not count the builds before the city upgrade repairs and the timers; a finished wall would "
        "not stand against that day's bites")
    advance = body(run, "void UCataclysmEmpireRun::AdvanceCityBuilds(", "the run")
    assert re.search(r"City\.Building\.IsEmpty\(\)\s*\|\|\s*City\.bFallen\s*\|\|\s*IsBesieged\(City\.CityId\)",
                     advance), "AdvanceCityBuilds counts a day of a build on a fallen or besieged city"
    assert "CompleteCityUpgrade(CityId, Done, Today);" in advance and "Report.UpgradesFinishedOn.Add(CityId);" in advance


def test_one_build_at_a_time_after_the_slots_and_before_the_price_and_a_build_takes_its_slot() -> None:
    run = code(EMPIRE / "CataclysmEmpireRun.cpp")
    would = body(run, "ECataclysmCityUpgradeResult UCataclysmEmpireRun::WouldBuyCityUpgrade(", "the run")
    slots = would.find("ECataclysmCityUpgradeResult::NoSlotsLeft")
    one = would.find("ECataclysmCityUpgradeResult::BuildUnderWay")
    gold = would.find("if (GoldCost > 0 && GoldHeld < GoldCost)")
    assert -1 < slots < one < gold, (
        "WouldBuyCityUpgrade does not refuse a second build after the slot count and before the price")
    assert "City->HasUpgrade(Upgrade.RowName) || City->IsBuilding(Upgrade.RowName)" in would, (
        "an upgrade being built can be bought a second time")
    assert "CannotWaitForABuildYet" not in run, "the guard that refused every purchase came back"
    slots_code = body(code(EMPIRE / "CataclysmEmpireMap.cpp"), "int32 UCataclysmEmpireMap::FreeUpgradeSlots(", "the map")
    assert "City->Upgrades.Num() - City->Building.Num()" in slots_code, "an upgrade being built does not take its slot"


# --------------------------------------------------------------------------
# 5 to 7: the run record
# --------------------------------------------------------------------------

def test_the_run_record_is_at_version_13_and_migrate_12_to_13_writes_nothing_building() -> None:
    header = (SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.h").read_text(encoding="utf-8")
    run_class = header.split("class CATACLYSM_API UCataclysmRunSave")[1]
    version = re.search(r"static constexpr int32 SchemaVersionNow = (\d+);", run_class)
    assert version is not None and int(version.group(1)) >= 13, "the run record is below version 13"
    city = code(EMPIRE / "CataclysmEmpireMap.h")
    assert re.search(r"UPROPERTY\(SaveGame[^)]*\)\s*TArray<FCataclysmCityUpgrade>\s+Building\s*;", city), (
        "FCataclysmCity does not save what it is building")
    assert re.search(r"UPROPERTY\(SaveGame[^)]*\)\s*int32\s+BuildDaysLeft\s*=\s*0\s*;",
                     code(EMPIRE / "CataclysmCityUpgrade.h")), "FCataclysmCityUpgrade does not save its days left"
    records = code(SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.cpp")
    step = body(records, "bool Migrate_12_to_13(", "the run record")
    assert 'SetArrayField(TEXT("Building"), TArray<TSharedPtr<FJsonValue>>())' in step, (
        "Migrate_12_to_13 does not write an empty build list")
    assert 'SetNumberField(TEXT("BuildDaysLeft"), 0)' in step, "Migrate_12_to_13 does not write 0 days left"
    assert re.search(r'\{\s*12\s*,\s*TEXT\("Migrate_12_to_13"\)', records), "the run record's chain has no step from 12"
    apply = code(SOURCE / "Cataclysm" / "Save" / "CataclysmSaveApply.cpp")
    assert re.search(r"City->Building\s*=\s*Saved\.Building\s*;", apply), "a load does not put the build back"


def test_run_v13_is_run_v12_with_the_build_fields_and_one_build_under_way() -> None:
    before = json.loads((FIXTURES / "Run_v12.json").read_text(encoding="utf-8"))
    after = json.loads((FIXTURES / "Run_v13.json").read_text(encoding="utf-8"))
    assert before["SchemaVersion"] == 12 and after["SchemaVersion"] == 13
    building = [build for city in after["Cities"] for build in city["Building"]]
    assert any(build["BuildDaysLeft"] > 0 for build in building), (
        "Run_v13.json holds no build under way, so the test that reads it cannot show the field is read")
    assert any(not city["Building"] for city in after["Cities"]), "every city in Run_v13.json is building"
    rest = json.loads(json.dumps(after))
    rest["SchemaVersion"] = 12
    for city in rest["Cities"]:
        del city["Building"]
        for upgrade in city["Upgrades"]:
            assert upgrade.pop("BuildDaysLeft") == 0, "a finished upgrade in Run_v13.json has days left"
    assert rest == before, "Run_v13.json differs from Run_v12.json beyond its version and the build fields"


def test_every_chain_test_adds_version_13_and_only_the_migration_reads_run_v12() -> None:
    twelve = 0
    for path in sorted(TESTS.glob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        twelve_here = text.count("AddTheRunsVersion12Defaults(")
        thirteen_here = text.count("AddTheRunsVersion13Defaults(")
        twelve += twelve_here
        assert twelve_here <= thirteen_here, (
            f"{path.name} runs an older run file through the chain without what Migrate_12_to_13 writes")
        for match in re.finditer(r'TEXT\("Run_v12\.json"\)', text):
            names = re.findall(r'"Cataclysm\.[\w.]+\.(\w+)"', text[:match.start()])
            assert names and names[-1] == MIGRATION_TEST, (
                f"{path.name} reads Run_v12.json in {names[-1] if names else 'a test with no name before it'}: the "
                "committed run file is Run_v13.json since the run record went to version 13 (issue #2825)")
    assert twelve >= 10, f"only {twelve} chain tests were found; the sweep reads the wrong folder"
    readme = (FIXTURES / "README.md").read_text(encoding="utf-8")
    assert "| `Run_v13.json` |" in readme, "game/Tests/SaveFixtures/README.md does not list Run_v13.json"


# --------------------------------------------------------------------------
# 8 and 9: what the screens say, and War Funding
# --------------------------------------------------------------------------

def test_the_city_screen_the_map_and_the_console_say_the_build_in_one_wording() -> None:
    layout = code(SOURCE / "Cataclysm" / "Interface" / "CataclysmCityScreenLayout.cpp")
    held = body(layout, "TArray<FString> UCataclysmCityScreenLayout::HeldLinesFor(", "the city screen layout")
    assert "BuildLineFor(Run, CityId)" in held, "the city's list does not end with the build"
    slots = body(layout, "FString UCataclysmCityScreenLayout::SlotsTextFor(", "the city screen layout")
    assert "City->Upgrades.Num() + City->Building.Num()" in slots, "the slot line does not count the build"
    widget = code(SOURCE / "Cataclysm" / "Interface" / "CataclysmCityScreenWidget.cpp")
    assert "!Offer.bWaitsOnABuild" in widget, "an offer another build stops vanishes instead of being greyed"
    detail = code(SOURCE / "Cataclysm" / "Interface" / "CataclysmEmpireMapWidget.cpp")
    assert "UCataclysmCityScreenLayout::BuildLineFor(Run, CityId)" in detail, (
        "the map's city detail does not say the build, or says it in words of its own")
    console = code(SOURCE / "Cataclysm" / "Character" / "CataclysmPlayerCharacter.cpp")
    assert "UCataclysmCityScreenLayout::BuildLineFor(Run, CityId)" in console, "Cataclysm.CityUpgrades omits the build"
    assert "UCataclysmCityUpgradePurchase::BuildDaysIn(World, Run, RowName, City->CityId)" in console, (
        "Cataclysm.CityUpgrades does not list each offer's days")


def test_the_design_document_states_the_days_the_code_holds() -> None:
    header = code(EMPIRE / "CataclysmCityUpgrade.h")
    days = [int(re.search(rf"static constexpr int32 BuildDaysTier{tier} = (\d+);", header).group(1)) for tier in (1, 2, 3)]
    design = (REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md").read_text(encoding="utf-8")
    paragraph = design[design.index("**A city upgrade costs gold, and takes days to build.**"):]
    paragraph = paragraph[:paragraph.index(chr(10))]
    stated = (f"An upgrade at upgrade tier 1 takes {days[0]} days, at tier 2 {days[1]} days and at tier 3 {days[2]} days")
    assert stated in paragraph, f"docs/Cataclysm_GDD_v2.md's City Upgrades paragraph does not say {stated!r}"
    assert "not built yet** (issue #2825)" not in paragraph, "the design document still says build times are not built"


def test_war_funding_counts_an_upgrade_being_built_as_the_first() -> None:
    tree = code(SOURCE / "Cataclysm" / "Player" / "CataclysmEmpireTree.cpp")
    factor = body(tree, "float UCataclysmEmpireTree::UpgradePriceFactorFor(", "the empire tree")
    assert "City->Upgrades.Num() + City->Building.Num() == 0" in factor, (
        "War Funding's first-upgrade discount ignores an upgrade being built, so a city building its first would be "
        "priced as holding none")
