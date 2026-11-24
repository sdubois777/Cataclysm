"""The empire upgrade tree's slice 9: Civic Pride, Reinvestment, Economic Bailout, Bribery and Scout Parties.

WHY THIS EXISTS. Issue #2741 slice 9. The coordinating session ruled the five nodes on 2026-11-21 under the project
owner's delegation ("The rest can probably go with your rec"), and V1 to V20 on 2026-11-22. Continuous integration
compiles no C++, so the slice's automation tests never run on a pull request; these checks read the source for the
rules each ruling names, where a slip would pass a compile and change the game.

WHAT IS CHECKED:

  1. the five nodes are built in slice 9, each with its one effect row, and no node waits on a reason they answered;
  2. Civic Pride raises the population part of each of the four restores, held at the maximum, and no defence (V11);
  3. Reinvestment counts the price charged, in tier 1 gold, once the purchase is accepted, on the account or a Solo
     Self-Found character, and its share is added to Veteran's Edge's and held at its own 30% (V2 to V5, V20);
  4. Economic Bailout is asked only where a dungeon's damage takes a city to 0, after the Last Stand; refuses the
     Pillar and a city saved before; pays all or nothing with no credit and logs either way; and a saved city keeps half
     its maximum defence (V6 to V10);
  5. Bribery's refusals come from the run and the game module, the gold is taken before the timer is reset and with no
     credit, every surge gives the bribe back, and the screen and the console both reach it (V12 to V14);
  6. Scout Parties rolls on its own stream, adds an always-Basic dungeon with no Siege, and a bonus dungeon counts for
     what pays the player and not for the boss's floors, the kinds defeated, the empty-board surge, a city's cap or a
     detonation (V15 to V19);
  7. the three records' new steps write each field's default, the committed files are the previous ones with the new
     fields and non-default figures, and every test that runs an older file through the chain adds what they write.
"""

from __future__ import annotations

import json
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
FIXTURES = REPO_ROOT / "game" / "Tests" / "SaveFixtures"
TESTS = SOURCE / "Cataclysm" / "Tests"
EMPIRE = SOURCE / "CataclysmEmpire" / "Empire"
RUN_CPP = EMPIRE / "CataclysmEmpireRun.cpp"
MAP_CPP = EMPIRE / "CataclysmEmpireMap.cpp"
BONUSES_CPP = EMPIRE / "CataclysmEmpireTreeBonuses.cpp"
TREE_CPP = SOURCE / "Cataclysm" / "Player" / "CataclysmEmpireTree.cpp"
TREE_H = SOURCE / "Cataclysm" / "Player" / "CataclysmEmpireTree.h"
PURCHASE = SOURCE / "Cataclysm" / "Player" / "CataclysmCityUpgradePurchase.cpp"
INSTANCE = SOURCE / "Cataclysm" / "Player" / "CataclysmGameInstance.cpp"
BRIBE = SOURCE / "Cataclysm" / "Player" / "CataclysmDungeonBribe.cpp"
WIDGET = SOURCE / "Cataclysm" / "Interface" / "CataclysmCityScreenWidget.cpp"
CHARACTER = SOURCE / "Cataclysm" / "Character" / "CataclysmPlayerCharacter.cpp"
RECORDS_CPP = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.cpp"
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

NINE = {"Civic Pride": [("PopulationRestoreIncrease", 0.05)],
        "Reinvestment": [("EmpirePointsPerSpendingStep", 0.01, 0, "", 0.30)],
        "Economic Bailout": [("CityBailout", 1.0)],
        "Bribery": [("DungeonBribe", 1.0)],
        "Scout Parties": [("BonusDungeonChance", 0.10)]}


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


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def test_the_five_are_built_in_slice_9_with_their_rows() -> None:
    for name, effects in NINE.items():
        assert gen.EMPIRE_TREE_BUILT.get(name) == 9, f"{name} is not built in slice 9"
        assert name not in gen.EMPIRE_TREE_REFUSED, f"{name} still waits on something"
        assert gen.EMPIRE_TREE_EFFECTS.get(name) == effects, f"{name}'s effect row is {gen.EMPIRE_TREE_EFFECTS.get(name)}"
    # THE REASONS THEY ANSWERED ARE GONE: no other node waited on them, so a reason left would be a dead entry.
    for reason in ("fee figure", "cap figure", "population growth", "bonus dungeon"):
        assert reason not in gen.EMPIRE_TREE_SYSTEMS, f"{reason!r} is still a reason a node waits on"


def test_civic_pride_raises_each_population_restore_and_holds_it_at_the_maximum() -> None:
    share = flat(body(code(BONUSES_CPP), "float FCataclysmEmpireTreeBonuses::PopulationRestoredShare(",
                      "CataclysmEmpireTreeBonuses.cpp"))
    assert "(1.0f + FMath::Max(0.0f, PopulationRestoreIncrease))" in share, "the share is not made 5% a point larger"
    retake = flat(body(code(MAP_CPP), "bool UCataclysmEmpireMap::Retake(", "CataclysmEmpireMap.cpp"))
    assert ("City->Population = FMath::Min(MaxPopulation, MaxPopulation * TreeBonuses.PopulationRestoredShare(Fraction));"
            in retake), "a retake's population is not raised, or not held at the maximum (0.75 x 1.5 is 112.5%)"
    assert "City->Defence = EffectiveMaxDefence(*City) * Fraction;" in retake, "a retake's defence is raised (V11)"
    # IN `RestoreOnce` SINCE ISSUE #1265, which a purchase and a raise of the restore both fire.
    restore = flat(body(code(MAP_CPP), "void UCataclysmEmpireMap::RestoreOnce(", "CataclysmEmpireMap.cpp"))
    assert "Min(Max, PopulationOf(City) + Max * TreeBonuses.PopulationRestoredShare(Value))" in restore
    run = flat(code(RUN_CPP))
    assert ("MaxPopulation * TreeBonuses.PopulationRestoredShare( TreeBonuses.LowestCityPopulationRestored)" in run), (
        "The Mason's Guild's population is not raised")
    assert "Max * TreeBonuses.PopulationRestoredShare(Map->UpgradeValueNow(Upgrade)))" in run, (
        "the 20-day recovery is not raised")
    assert run.count("PopulationRestoredShare(") == 2 and flat(code(MAP_CPP)).count("PopulationRestoredShare(") == 2, (
        "a population restore beyond the four, or a defence restore, reads Civic Pride")


def test_reinvestment_counts_the_accepted_price_in_tier_one_gold_and_adds_to_veterans_edge() -> None:
    # `PayAndBuild`, the body `Buy` and `Raise` share since issue #1265, so a raise's price counts as a purchase's.
    buy = flat(body(code(PURCHASE), "ECataclysmCityUpgradeResult PayAndBuild(", "the purchase"))
    accepted = buy.find("if (Result != ECataclysmCityUpgradeResult::Bought)")
    counted = buy.find("UCataclysmGameInstance::AddGoldSpentOnCityUpgradesFor(")
    assert -1 < accepted < counted, "the count is added before the run accepts the purchase (V4)"
    assert ("AddGoldSpentOnCityUpgradesFor(WorldContext, static_cast<double>(Price) / Scale);" in buy
            and "UCataclysmDropRoll::GoldScaleFor(Run->DifficultyTier)" in buy), (
        "the count is not the price charged in tier 1 gold, unrounded (V20)")
    for held in (code(SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.h"),
                 code(SOURCE / "Cataclysm" / "Player" / "CataclysmPlayerState.h")):
        assert "int64 GoldSpentOnCityUpgrades" not in held and "int64 OwnGoldSpentOnCityUpgrades" not in held, (
            "the count is held as a whole number; it is an unrounded double (V20)")
    assert "UCataclysmGameInstance::RefreshEmpireTreeBonuses(WorldContext);" in buy[counted:], (
        "the run's bonuses are not read again after the count moves")
    spending = flat(body(code(TREE_CPP), "float UCataclysmEmpireTree::EmpirePointsFromSpendingFor(", "the tree"))
    assert "FMath::FloorToDouble(GoldSpentAtTierOne / static_cast<double>(ReinvestmentGoldAStep))" in spending and "FMath::Min(Share, Bonuses.EmpirePointsSpendingCap)" in spending
    assert "static constexpr int64 ReinvestmentGoldAStep = 25000;" in code(TREE_H)
    clear = flat(body(code(RUN_CPP), "bool UCataclysmEmpireRun::ClearDungeon(", "the run"))
    assert ("1.0 + static_cast<double>(TreeBonuses.EmpirePointsIncrease) + static_cast<double>(TreeBonuses.EmpirePointsFromSpending)"
            in clear), "Reinvestment's share is not added to Veteran's Edge's (V3)"
    bonuses = flat(body(code(INSTANCE), "FCataclysmEmpireTreeBonuses UCataclysmGameInstance::EmpireTreeBonusesFor(", "x"))
    assert "Bonuses.EmpirePointsFromSpending = UCataclysmEmpireTree::EmpirePointsFromSpendingFor(" in bonuses
    owner = flat(body(code(INSTANCE), "bool UCataclysmGameInstance::AddGoldSpentOnCityUpgradesFor(", "x"))
    assert "State->GetPartition().bSoloSelfFound" in owner and "Account->GoldSpentOnCityUpgrades" in owner, (
        "the count is not kept on a Solo Self-Found character apart from the account (V2)")


def test_economic_bailout_is_asked_only_at_zero_defence_after_the_last_stand() -> None:
    damage = flat(body(code(MAP_CPP), "bool UCataclysmEmpireMap::Damage(", "CataclysmEmpireMap.cpp"))
    stand = damage.find("City->Defence = LastStandDefence;")
    saved = damage.find("SaveFromFalling(CityId)")
    fall = damage.find("return Fall(CityId);")
    assert -1 < stand < saved < fall, "the bailout is not asked after the Last Stand and before the fall (V7)"
    voids = flat(body(code(MAP_CPP), "bool UCataclysmEmpireMap::AddVoidStacks(", "CataclysmEmpireMap.cpp"))
    assert "SaveFromFalling" not in voids, "a fall at the tenth Void stack is saved (V6)"
    save = flat(body(code(RUN_CPP), "bool UCataclysmEmpireRun::SaveCityFromFalling(", "the run"))
    for part in ("City->Tier == ECataclysmCityTier::Pillar", "City->bBailedOut", "TreeBonuses.CityBailoutPoints <= 0",
                 "City->Defence = Map->EffectiveMaxDefence(*City) * BailoutDefenceFraction;"):
        assert part in save, f"SaveCityFromFalling leaves out {part!r}"
    assert save.find("PayTheBailout(*this, CityId, City->Tier)") < save.find("City->bBailedOut = true;"), (
        "a city is marked saved before its fee is paid (V9)")
    assert "bDoomed" not in save, "the bailout clears or reads the doom mark (V10)"
    run = code(RUN_CPP)
    assert run.count("NoteTheBailouts(OutReport);") == 3, "a dungeon's hit does not report the cities it saved"


def test_economic_bailouts_fee_is_paid_all_or_nothing_and_logged_either_way() -> None:
    fees = flat(body(code(TREE_CPP), "int64 UCataclysmEmpireTree::BailoutFeeAtTierOneFor(", "the tree"))
    for tier, fee in (("Outpost", 50000), ("Bulwark", 100000), ("Sanctuary", 150000)):
        assert f"case ECataclysmCityTier::{tier}: return {fee};" in fees, f"the {tier}'s fee is not {fee}"
    assert "default: return 0;" in fees, "the Pillar's fee is not nought"
    wiring = flat(body(code(INSTANCE), "void UCataclysmGameInstance::WireTheRunToTheBalance(", "x"))
    pay = wiring[wiring.find("Run->PayTheBailout ="):]
    assert "SpendGoldFor(Paying, Fee, bFromTheCharacter)" in pay and "Credit" not in pay, (
        "the fee is taken on credit or not all or nothing (V9)")
    assert pay.count("FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::BailoutLine(") == 2, (
        "the log does not say both when the fee is paid and why when it is not (V9)")
    assert "UCataclysmDropRoll::GoldScaleFor(BailRun.DifficultyTier)" in pay, "the fee is not scaled to the run's tier"


def test_bribery_refuses_in_the_run_and_the_game_module_and_pays_before_the_reset() -> None:
    refusal = flat(body(code(RUN_CPP), "FString UCataclysmEmpireRun::RefusalForBribing(", "the run"))
    for words in ("The Cataclysm's own dungeon cannot be bribed.", "Only a dungeon whose timer resolves can be bribed.",
                  "Bribery has been used this surge", "Its timer is already at its start."):
        assert words in refusal, f"RefusalForBribing does not say {words!r}"
    assert "Timer->DaysUntilResolve >= Timer->ResolveDays" in refusal
    reset = flat(body(code(RUN_CPP), "bool UCataclysmEmpireRun::BribeDungeon(", "the run"))
    assert "Timer->DaysUntilResolve = Timer->ResolveDays;" in reset and "bBribedThisSurge = true;" in reset
    surge = flat(body(code(RUN_CPP), "void UCataclysmEmpireRun::FireSurge(", "the run"))
    assert "bBribedThisSurge = false;" in surge, "a surge does not give the bribe back (V12)"
    bribe = code(BRIBE)
    assert "SpendGoldOnCreditFor" not in bribe and "CreditIn" not in bribe, "Bribery takes credit (V13)"
    pays = flat(body(bribe, "FString FCataclysmDungeonBribe::Bribe(", "the bribe"))
    assert -1 < pays.find("SpendGoldFor(WorldContext, Fee, bFromTheCharacter)") < pays.find("Run->BribeDungeon(DungeonId)"), (
        "the timer is reset before the fee is taken")
    held = flat(body(bribe, "FString FCataclysmDungeonBribe::RefusalFor(", "the bribe"))
    assert "DungeonBribePoints <= 0" in held and "Run->RefusalForBribing(DungeonId)" in held
    assert "static constexpr int64 BribeFeeAtTierOne = 20000;" in code(TREE_H)
    assert 'TEXT("Cataclysm.BribeDungeon")' in code(CHARACTER) and "FCataclysmDungeonBribe::Bribe(World, Run, DungeonId)" in code(CHARACTER)
    assert "FCataclysmDungeonBribe::Bribe(this, ShownRun()" in code(WIDGET) and "AddBribeButton(DungeonIds[Index]);" in code(WIDGET)


def test_scout_parties_rolls_on_its_own_stream_and_adds_a_basic_dungeon_with_no_siege() -> None:
    clear = flat(body(code(RUN_CPP), "bool UCataclysmEmpireRun::ClearDungeon(", "the run"))
    assert "FRandomStream Roll(CataclysmSeedMix::Mix(WalkSeedMask ^ (DungeonId + 1), ScoutSalt, 0));" in clear
    # THE BOSS STANDS ON NO CITY, so the city it needs keeps the boss's clear from rolling.
    assert "!bBonus && TreeBonuses.BonusDungeonChance > 0.0f && CityId != INDEX_NONE" in clear, (
        "a bonus dungeon's or the boss's clear rolls Scout Parties (V15)")
    added = flat(body(code(RUN_CPP), "int32 UCataclysmEmpireRun::AddBonusDungeon(", "the run"))
    assert "Day(), Roll, false, ECataclysmDungeonType::Basic, Sender)" in added, (
        "a bonus dungeon is not Basic, or may be a Siege (V18)")
    assert "Dungeon.bBonus = true;" in added and "GiveModifiersFrom(Dungeon, Roll);" in added
    assert "!City->IsAlive()" in added, "a bonus dungeon may appear on a fallen city (V15)"
    for stream in ("Stream.", "CataclysmStream", "ModifierStream"):
        assert stream not in added.replace("FRandomStream& Roll", ""), f"a bonus dungeon draws from {stream} (V15)"


def test_a_bonus_dungeon_pays_and_moves_nothing_of_the_empires_pressure() -> None:
    clear = flat(body(code(RUN_CPP), "bool UCataclysmEmpireRun::ClearDungeon(", "the run"))
    assert "if (bBonus) { ++BonusDungeonsCleared; } else if (Kind == ECataclysmDungeonType::Basic) { ++BasicDungeonsCleared;" in clear, (
        "a bonus clear adds a floor to the boss, or is not counted apart (V16, V17)")
    for paid in ("++DungeonsCleared;", "++ClearsSinceLastSurge;", "++ClearsSinceLastDeath;"):
        assert clear.index(paid) < clear.index("if (bBonus)"), f"a bonus clear does not count {paid!r} (V16)"
    kinds = flat(code(TREE_CPP))
    assert "Run.DungeonsCleared - Run.BasicDungeonsCleared - Run.QuestObjectives - Won - Run.BonusDungeonsCleared" in kinds, (
        "a bonus clear reads as a Fallen City clear in the kinds defeated (V17, V19)")
    # LEFT BEFORE `ResolveDungeon`, BY ITS TWO CALLERS: the day and a death (V16).
    leave = flat(body(code(RUN_CPP), "bool UCataclysmEmpireRun::LetABonusDungeonLeave(", "the run"))
    assert "Dungeon->bBonus && RemoveDungeon(DungeonId)" in leave
    day = flat(body(code(RUN_CPP), "FCataclysmDayReport UCataclysmEmpireRun::AdvanceDay(", "the run"))
    assert "if (!LetABonusDungeonLeave(DungeonId)) { ResolveDungeon(DungeonId, Report); }" in day, (
        "a bonus dungeon whose timer runs out is resolved and damages its city (V16)")
    death = flat(body(code(RUN_CPP), "FCataclysmDayReport UCataclysmEmpireRun::ResolveDungeonOnDeath(", "the run"))
    assert -1 < death.find("if (LetABonusDungeonLeave(DungeonId)) { return Report; }") < death.find(
        "ResolveDungeon(DungeonId, Report);"), "a death in a bonus dungeon resolves it (V16)"
    assert "bBonus" not in flat(body(code(RUN_CPP), "void UCataclysmEmpireRun::ResolveDungeon(", "the run"))
    run = flat(code(RUN_CPP))
    # ORDINARY DUNGEONS ONLY since issue #2865, which leaves a bonus dungeon out as `ThreateningDungeonCount` did.
    assert "Surges->IsDueOnEmptyBoard(Today, OrdinaryDungeonCount())" in run, "the empty-board surge counts a bonus dungeon"
    assert "!Dungeon.bBonus" in body(run, "int32 UCataclysmEmpireRun::OrdinaryDungeonCount(", "the run"), (
        "the empty-board count no longer leaves out a bonus dungeon")
    assert "if (Standing.bBonus) { continue; }" in run, "a bonus dungeon counts against a city's cap"
    assert "if (Dungeon.CityId != CityId || Dungeon.bBonus) { continue; }" in run, "the scheduled loss counts a bonus dungeon"


def test_the_new_steps_write_each_fields_default() -> None:
    records = code(RECORDS_CPP)
    account = body(records, "namespace CataclysmAccountSaveMigration", "x")
    character = body(records, "namespace CataclysmCharacterSaveMigration", "x")
    run = body(records, "namespace CataclysmRunSaveMigration", "x")
    for chain, step in ((account, "Migrate_6_to_7"), (character, "Migrate_13_to_14")):
        text = flat(body(chain, f"bool {step}(", step))
        assert 'Record->SetNumberField(TEXT("GoldSpentOnCityUpgrades"), Held);' in text and "return false;" in text, (
            f"{step} does not write the count, keeping a number and refusing anything else")
    step = flat(body(run, "bool Migrate_14_to_15(", "the run record"))
    for part in ('SetBoolField(TEXT("bBailedOut"), false)', 'SetBoolField(TEXT("bBonus"), false)',
                 'SetNumberField(TEXT("BonusDungeonsCleared"), 0)', 'SetBoolField(TEXT("bBribedThisSurge"), false)'):
        assert part in step, f"Migrate_14_to_15 does not {part}"


def test_each_record_declares_the_fields_its_step_writes() -> None:
    """A step that writes a field the record does not declare compiles; the code that reads the field does not. The
    rebase onto the gems window left the character record without its count, and only the Unreal build found it."""
    header = code(SOURCE / "Cataclysm" / "Save" / "CataclysmSaveRecords.h")

    def record(name: str) -> str:
        start = header.find(f"class CATACLYSM_API {name} ")
        assert start != -1, f"CataclysmSaveRecords.h has no {name}"
        following = header.find("\nclass ", start + 1)
        return flat(header[start:following if following != -1 else len(header)])

    saved = r"UPROPERTY\(SaveGame[^)]*\)\s*"
    for name, field in (("UCataclysmAccountSave", r"double\s+GoldSpentOnCityUpgrades\s*=\s*0\.0\s*;"),
                        ("UCataclysmCharacterSave", r"double\s+GoldSpentOnCityUpgrades\s*=\s*0\.0\s*;"),
                        ("UCataclysmRunSave", r"int32\s+BonusDungeonsCleared\s*=\s*0\s*;"),
                        ("UCataclysmRunSave", r"bool\s+bBribedThisSurge\s*=\s*false\s*;")):
        assert re.search(saved + field, record(name)), f"{name} does not declare {field} as a saved field"
    for path, field in ((SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmEmpireMap.h", r"bool\s+bBailedOut\s*=\s*false\s*;"),
                        (SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmSurge.h", r"bool\s+bBonus\s*=\s*false\s*;")):
        assert re.search(saved + field, flat(code(path))), f"{path.name} does not declare {field} as a saved field"


def test_the_committed_files_are_the_previous_ones_with_the_new_fields() -> None:
    # FRACTIONS, because the count is a double and unrounded (V20): a whole number would not show one kept.
    for old, new, added in (("Account_v6.json", "Account_v7.json", {"GoldSpentOnCityUpgrades": 75000.5}),
                            ("Character_v13.json", "Character_v14.json", {"GoldSpentOnCityUpgrades": 50000.25}),
                            ("Run_v14.json", "Run_v15.json", {"BonusDungeonsCleared": 2, "bBribedThisSurge": True})):
        before = json.loads((FIXTURES / old).read_text(encoding="utf-8"))
        after = json.loads((FIXTURES / new).read_text(encoding="utf-8"))
        assert after.pop("SchemaVersion") == before.pop("SchemaVersion") + 1
        for key, value in added.items():
            assert after.pop(key) == value, f"{new}'s {key} is not {value}, a figure rather than the default"
        if new.startswith("Run"):
            assert [city.pop("bBailedOut") for city in after["Cities"]] == [True, False]
            # FALSE, THE DEFAULT, ON PURPOSE: its one dungeon is a Siege, and a bonus dungeon never is (V18).
            assert [dungeon.pop("bBonus") for dungeon in after["Dungeons"]] == [False]
            assert all(dungeon["SubType"] == "Siege" for dungeon in after["Dungeons"])
        assert after == before, f"{new} differs from {old} beyond its version and the new fields"
    readme = (FIXTURES / "README.md").read_text(encoding="utf-8")
    for name in ("Account_v7.json", "Character_v14.json", "Run_v15.json"):
        assert f"| `{name}` |" in readme, f"the fixtures README does not list {name}"


def test_every_chain_test_adds_what_the_three_new_steps_write() -> None:
    missing = []
    for path in sorted(TESTS.glob("*.cpp")):
        text = code(path)
        for old, new in (("AddTheAccountsVersion6Defaults", "AddTheAccountsVersion7Defaults"),
                         ("AddTheCharactersVersion13Defaults", "AddTheCharactersVersion14Defaults"),
                         ("AddTheRunsVersion14Defaults", "AddTheRunsVersion15Defaults")):
            if text.count(f"{old}(*Expected)") != text.count(f"{new}(*Expected)"):
                missing.append(f"{path.name}: {old} {text.count(old)}, {new} {text.count(new)}")
    assert not missing, "a test runs an older file through the chain without the new step's defaults:\n" + "\n".join(missing)
