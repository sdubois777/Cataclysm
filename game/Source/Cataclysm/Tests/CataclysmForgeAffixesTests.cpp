// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmForgeLayout.h"
#include "Interface/CataclysmForgeWidget.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "DayClock/CataclysmDayClock.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * THE FORGE'S THREE AFFIX CRAFTS. Issue #47, slice F1, ruled by the coordinating session on 2026-11-22.
 *
 * The Crafting sheet's rows: Reroll Affix Value, a Corrupted Mote, 2 residue, 0 base days, Base Gold 250; Perfect Roll
 * Attempt, a Primal Spark, 50 residue, 5 base days, Base Gold 1,250; Upgrade Affix Tier, a Potency Crystal, 10 residue
 * and one base day for each tier the affix has, Base Gold 250; the residue's time penalty and the gold formula on all
 * three. Each acts on one affix the player chooses. A reroll draws the roll again evenly, from the item's saved contents,
 * the run's mask and the day; a perfecting makes it 1, certainly; an upgrade raises the tier by one, to 7, and keeps the
 * roll. The scene is the one `CataclysmForgeSocketsTests.cpp` builds, copied into this file's namespace because the
 * module is built in unity files.
 */
namespace CataclysmForgeAffixesTest
{
	const FName Mote(UCataclysmForge::CorruptedMoteRow);
	const FName Spark(UCataclysmForge::PrimalSparkRow);
	const FName Crystal(UCataclysmForge::PotencyCrystalRow);

	FCataclysmRolledAffix MakeAffix(const TCHAR* Row, int32 Tier, float Roll)
	{
		FCataclysmRolledAffix Out;
		Out.Affix = FName(Row);
		Out.Tier = Tier;
		Out.Roll = Roll;
		return Out;
	}

	/**
	 * THE FIXED HELM: gear level 3, flat evasion at tier 2 rolled `FirstRoll` and flat armor at tier 4 rolled 0.25, no
	 * sockets, carrying `Residue`. Its seed text and the rolls FA2 asserts were computed with the Python copy of
	 * `UCataclysmForge::RerolledAffixRoll` in `tools/tests/test_the_forge_crafts_one_affix.py`.
	 */
	FCataclysmItem Helm(float FirstRoll = 0.5f, float Residue = 40.0f)
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		Item.GearLevel = 3;
		Item.Affixes = {MakeAffix(TEXT("Stat_Flat_evasion"), 2, FirstRoll), MakeAffix(TEXT("Stat_Flat_armor"), 4, 0.25f)};
		Item.Residue = Residue;
		return Item;
	}

	struct FForgeAffixesScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEmpireRun* Run = nullptr;

		bool IsUsable() const
		{
			return World && Instance && Character && State && Character->GetInventory() && Character->GetEquipment()
				&& Run && Run->Clock;
		}
		UCataclysmInventoryComponent* Bag() const { return Character->GetInventory(); }
		UCataclysmEquipmentComponent* Worn() const { return Character->GetEquipment(); }
	};

	/** A world with this project's game instance, a possessed player with a million gold of its own, and a run. */
	FForgeAffixesScene MakeScene()
	{
		FForgeAffixesScene Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.Instance = NewObject<UCataclysmGameInstance>();
		Out.World->SetGameInstance(Out.Instance);
		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Out.State && Controller && Out.Character)
		{
			Controller->SetPlayerState(Out.State);
			Controller->Possess(Out.Character);
			FCataclysmCharacterPartition Partition;
			Partition.Lethality = ECataclysmLethality::Standard;
			Partition.Population = ECataclysmPopulation::Offline;
			// SOLO SELF-FOUND, WITH GOLD OF ITS OWN, so every craft can pay (#2830) and no account record is written.
			Partition.bSoloSelfFound = true;
			Out.State->SetIdentity(TEXT("Affixer"), Partition);
			Out.State->SetOwnGold(1000000);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		Out.Run->Begin(/* InSeed */ 2847, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}
		return Out;
	}

	ECataclysmGearSlot Wear(const FForgeAffixesScene& Scene, const FCataclysmItem& Item)
	{
		FCataclysmItem First;
		FCataclysmItem Second;
		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		Scene.Worn()->Equip(Item, First, Second, Went);
		return Went;
	}

	FString Labels(const TArray<FCataclysmForgeAffixAction>& Actions)
	{
		TArray<FString> Out;
		for (const FCataclysmForgeAffixAction& Action : Actions)
		{
			Out.Add(Action.Label);
		}
		return FString::Join(Out, TEXT(" | "));
	}
}

// --------------------------------------------------------------------------------------------------------------------
// FA1. Rerolling an affix: the rule's figures, its question and refusals, and the craft on a carried helm.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeAffixesRerollTest,
	"Cataclysm.ForgeAffixes.RerollingAnAffixTakesACorruptedMoteTwoResidueNoBaseDayAndItsGold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeAffixesRerollTest::RunTest(const FString&)
{
	using namespace CataclysmForgeAffixesTest;
	const FCataclysmItem Item = Helm();
	constexpr ECataclysmForgeAffixCraft Reroll = ECataclysmForgeAffixCraft::RerollValue;

	const FCataclysmForgeAffixWork Plan = UCataclysmForge::AffixWorkOf(Item, Reroll, 0, 1, 1, 1000000);
	TestTrue(TEXT("affix 1 can be rerolled"), Plan.bPossible);
	TestEqual(TEXT("with a Corrupted Mote"), Plan.Material, Mote);
	TestEqual(TEXT("residue 40 becomes 42"), Plan.ResidueAfter, 42.0f, 0.001f);
	TestEqual(TEXT("no day below 100 residue"), Plan.Days, 0);
	TestEqual(TEXT("250 gold a time, at 40 residue 450"), Plan.Gold, static_cast<int64>(450));
	TestEqual(TEXT("the tier is kept"), Plan.ToTier, 2);
	TestEqual(TEXT("the first press's question"), UCataclysmForge::AffixWorkQuestion(Plan),
			  FString(TEXT("Press again to reroll affix 1: one Corrupted Mote, 450 gold, residue 40 to 42, 0 days of the "
						   "run; its roll, now 50 percent of tier 2's range, is drawn again and can fall.")));
	TestTrue(TEXT("a roll at the top can be rerolled: it can fall, and the player chose it"),
			 UCataclysmForge::AffixWorkOf(Helm(1.0f), Reroll, 0, 1, 1, 1000000).bPossible);
	TestEqual(TEXT("no Corrupted Mote"), UCataclysmForge::AffixWorkOf(Item, Reroll, 0, 0, 1, 1000000).Refusal,
			  FString(TEXT("Rerolling an affix's value takes a Corrupted Mote; 0 held.")));
	TestEqual(TEXT("too little gold"), UCataclysmForge::AffixWorkOf(Item, Reroll, 0, 1, 1, 100).Refusal,
			  FString(TEXT("Rerolling an affix's value takes 450 gold; 100 gold held.")));
	TestEqual(TEXT("no such affix"), UCataclysmForge::AffixWorkOf(Item, Reroll, 5, 1, 1, 1000000).Refusal,
			  FString(TEXT("That piece has no affix 6.")));
	TestEqual(TEXT("no piece"), UCataclysmForge::AffixWorkOf(FCataclysmItem(), Reroll, 0, 1, 1, 1000000).Refusal,
			  FString(TEXT("There is nothing there to craft.")));

	FForgeAffixesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Item);
	Scene.Bag()->AddMaterial(Mote, 1);
	const FCataclysmItem Before = *Scene.Bag()->ItemAt(Cell);
	const int32 DayBefore = Scene.Run->Day();
	const int64 GoldBefore = Scene.State->GetOwnGold();
	const float Expected = UCataclysmForge::RerolledAffixRoll(Before, 0, Scene.Run->WalkSeedMask, DayBefore);

	FCataclysmForgeAffixWork Done;
	FString Message;
	const ECataclysmForgeResult Result = UCataclysmForgeCraft::CraftAffix(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), Reroll, 0, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the reroll is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After) || !TestEqual(TEXT("with 2 affixes"), After->Affixes.Num(), 2))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("REROLLED: roll %.6f to %.6f (expected %.6f), tier %d, residue %.0f, day %d to %d, gold %lld"),
							Before.Affixes[0].Roll, After->Affixes[0].Roll, Expected, After->Affixes[0].Tier,
							After->Residue, DayBefore, Scene.Run->Day(), GoldBefore - Scene.State->GetOwnGold()));
	TestEqual(TEXT("the new roll is the one drawn for this item, run and day"), After->Affixes[0].Roll, Expected, 0.000001f);
	TestEqual(TEXT("and the craft reports it"), Done.ToRoll, Expected, 0.000001f);
	TestEqual(TEXT("the tier is kept"), After->Affixes[0].Tier, 2);
	TestTrue(TEXT("the row is kept"), After->Affixes[0].Affix == Before.Affixes[0].Affix);
	TestTrue(TEXT("affix 2 is untouched"), After->Affixes[1].Tier == 4 && After->Affixes[1].Roll == 0.25f);
	TestEqual(TEXT("residue 40 becomes 42"), After->Residue, 42.0f, 0.001f);
	TestEqual(TEXT("the Corrupted Mote is used"), Scene.Bag()->CountOfMaterial(Mote), 0);
	TestEqual(TEXT("no day passed"), Scene.Run->Day(), DayBefore);
	TestEqual(TEXT("450 gold was paid"), GoldBefore - Scene.State->GetOwnGold(), static_cast<int64>(450));

	// WITH NO MOTE LEFT IT IS REFUSED, AND NOTHING MOVES.
	const ECataclysmForgeResult Refused = UCataclysmForgeCraft::CraftAffix(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), Reroll, 0, false, Done, Message);
	TestEqual(TEXT("a second reroll with no mote is refused"), static_cast<int32>(Refused),
			  static_cast<int32>(ECataclysmForgeResult::Refused));
	TestEqual(TEXT("saying so"), Message, FString(TEXT("Rerolling an affix's value takes a Corrupted Mote; 0 held.")));
	TestEqual(TEXT("and the roll is as the first reroll left it"), Scene.Bag()->ItemAt(Cell)->Affixes[0].Roll, Expected,
			  0.000001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FA2. A rerolled roll: the item's seed text, the same roll for the same item, run, day and affix, another for another
// affix or an item differing in one roll, and an even spread over the days. Every figure computed in Python.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeAffixesDrawTest,
	"Cataclysm.ForgeAffixes.ARerolledRollIsDrawnEvenlyAndTheSameForTheSameRunDayAndItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeAffixesDrawTest::RunTest(const FString&)
{
	using namespace CataclysmForgeAffixesTest;
	const FCataclysmItem A = Helm(0.5f);
	const FCataclysmItem B = Helm(0.625f);
	constexpr int32 Mask = 12345;
	constexpr int32 Day = 7;

	TestEqual(TEXT("the item's seed text, as the Python copy writes it"), UCataclysmForge::ItemSeedText(A),
			  FString(TEXT("Base=Head_Helm;Level=3;Types=;Affixes=Stat_Flat_evasion,2,0.500000,|Stat_Flat_armor,4,0.250000,;"
						   "Enchantments=0:;Sockets=;Residue=40.000000;Kills=0;Made=0")));
	TestEqual(TEXT("affix 1's seed on day 7 of mask 12345"), UCataclysmForge::RerolledAffixSeed(A, 0, Mask, Day), 939699652);
	TestEqual(TEXT("affix 2's"), UCataclysmForge::RerolledAffixSeed(A, 1, Mask, Day), 1276092663);
	const float First = UCataclysmForge::RerolledAffixRoll(A, 0, Mask, Day);
	TestEqual(TEXT("affix 1 draws 0.110541"), First, 0.11054110527038574f, 0.000001f);
	TestEqual(TEXT("and again 0.110541: a reload on the same day draws the same"),
			  UCataclysmForge::RerolledAffixRoll(A, 0, Mask, Day), First, 0.0f);
	TestEqual(TEXT("affix 2 draws 0.179841"), UCataclysmForge::RerolledAffixRoll(A, 1, Mask, Day), 0.17984116077423096f,
			  0.000001f);
	TestEqual(TEXT("the same helm with affix 1 rolled 0.625 instead of 0.5 draws 0.984251"),
			  UCataclysmForge::RerolledAffixRoll(B, 0, Mask, Day), 0.9842512607574463f, 0.000001f);

	int32 Quarters[4] = {0, 0, 0, 0};
	for (int32 Each = 1; Each <= 3000; ++Each)
	{
		const float Roll = UCataclysmForge::RerolledAffixRoll(A, 0, Mask, Each);
		++Quarters[FMath::Clamp(FMath::FloorToInt(Roll * 4.0f), 0, 3)];
	}
	AddInfo(FString::Printf(TEXT("REROLL DRAWS: seed %d, roll %.6f; over days 1 to 3000 by quarter %d, %d, %d, %d"),
							UCataclysmForge::RerolledAffixSeed(A, 0, Mask, Day), First, Quarters[0], Quarters[1],
							Quarters[2], Quarters[3]));
	TestEqual(TEXT("over days 1 to 3000: below a quarter 750, as computed"), Quarters[0], 750);
	TestEqual(TEXT("a quarter to a half 703"), Quarters[1], 703);
	TestEqual(TEXT("a half to three quarters 746"), Quarters[2], 746);
	TestEqual(TEXT("three quarters and up 801"), Quarters[3], 801);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FA3. Perfecting an affix: the rule's figures, question and refusals, and the craft on a carried helm.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeAffixesPerfectTest,
	"Cataclysm.ForgeAffixes.PerfectingAnAffixTakesAPrimalSparkFiftyResidueFiveDaysAndMakesTheRollTheTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeAffixesPerfectTest::RunTest(const FString&)
{
	using namespace CataclysmForgeAffixesTest;
	const FCataclysmItem Item = Helm();
	constexpr ECataclysmForgeAffixCraft Perfect = ECataclysmForgeAffixCraft::PerfectRoll;

	const FCataclysmForgeAffixWork Plan = UCataclysmForge::AffixWorkOf(Item, Perfect, 0, 1, 1, 1000000);
	TestTrue(TEXT("affix 1, rolled 0.5, can be perfected"), Plan.bPossible);
	TestEqual(TEXT("with a Primal Spark"), Plan.Material, Spark);
	TestEqual(TEXT("to a roll of 1"), Plan.ToRoll, 1.0f, 0.0f);
	TestEqual(TEXT("residue 40 becomes 90"), Plan.ResidueAfter, 90.0f, 0.001f);
	TestEqual(TEXT("five days below 100 residue"), Plan.Days, 5);
	TestEqual(TEXT("1,250 gold a time, at 40 residue 2,250"), Plan.Gold, static_cast<int64>(2250));
	TestEqual(TEXT("the first press's question"), UCataclysmForge::AffixWorkQuestion(Plan),
			  FString(TEXT("Press again to perfect affix 1, from 50 to 100 percent of tier 2's range: one Primal Spark, "
						   "2,250 gold, residue 40 to 90, 5 days of the run.")));
	TestEqual(TEXT("a roll at the top already"), UCataclysmForge::AffixWorkOf(Helm(1.0f), Perfect, 0, 1, 1, 1000000).Refusal,
			  FString(TEXT("That affix's roll is the top of its tier already.")));
	TestEqual(TEXT("no Primal Spark"), UCataclysmForge::AffixWorkOf(Item, Perfect, 0, 0, 1, 1000000).Refusal,
			  FString(TEXT("Perfecting an affix's roll takes a Primal Spark; 0 held.")));

	FForgeAffixesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Item);
	Scene.Bag()->AddMaterial(Spark, 1);
	const int32 DayBefore = Scene.Run->Day();
	const int64 GoldBefore = Scene.State->GetOwnGold();
	FCataclysmForgeAffixWork Done;
	FString Message;
	const ECataclysmForgeResult Result = UCataclysmForgeCraft::CraftAffix(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), Perfect, 0, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the perfecting is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("PERFECTED: roll %.6f, tier %d, residue %.0f, day %d to %d, gold %lld"),
							After->Affixes[0].Roll, After->Affixes[0].Tier, After->Residue, DayBefore, Scene.Run->Day(),
							GoldBefore - Scene.State->GetOwnGold()));
	TestEqual(TEXT("affix 1's roll is 1, the top of its tier"), After->Affixes[0].Roll, 1.0f, 0.0f);
	TestEqual(TEXT("its tier is kept"), After->Affixes[0].Tier, 2);
	TestTrue(TEXT("affix 2 is untouched"), After->Affixes[1].Tier == 4 && After->Affixes[1].Roll == 0.25f);
	TestEqual(TEXT("residue 40 becomes 90"), After->Residue, 90.0f, 0.001f);
	TestEqual(TEXT("the Primal Spark is used"), Scene.Bag()->CountOfMaterial(Spark), 0);
	TestEqual(TEXT("five days passed"), Scene.Run->Day(), DayBefore + 5);
	TestEqual(TEXT("2,250 gold was paid"), GoldBefore - Scene.State->GetOwnGold(), static_cast<int64>(2250));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FA4. Raising an affix's tier: the rule's figures at tiers 4 and 6, the time penalty, the refusal at tier 7, and the
// craft on a carried helm, which keeps the roll.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeAffixesRaiseTest,
	"Cataclysm.ForgeAffixes.RaisingAnAffixsTierTakesAPotencyCrystalTenResidueATierAndKeepsTheRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeAffixesRaiseTest::RunTest(const FString&)
{
	using namespace CataclysmForgeAffixesTest;
	const FCataclysmItem Item = Helm();
	constexpr ECataclysmForgeAffixCraft Raise = ECataclysmForgeAffixCraft::UpgradeTier;

	const FCataclysmForgeAffixWork Plan = UCataclysmForge::AffixWorkOf(Item, Raise, 1, 1, 1, 1000000);
	TestTrue(TEXT("affix 2, tier 4, can be raised"), Plan.bPossible);
	TestEqual(TEXT("with a Potency Crystal"), Plan.Material, Crystal);
	TestEqual(TEXT("to tier 5"), Plan.ToTier, 5);
	TestEqual(TEXT("its roll kept"), Plan.ToRoll, 0.25f, 0.0f);
	TestEqual(TEXT("10 residue a tier: 40 becomes 80"), Plan.ResidueAfter, 80.0f, 0.001f);
	TestEqual(TEXT("a day a tier: 4"), Plan.Days, 4);
	TestEqual(TEXT("250 gold flat, at 40 residue 450"), Plan.Gold, static_cast<int64>(450));
	TestEqual(TEXT("the first press's question"), UCataclysmForge::AffixWorkQuestion(Plan),
			  FString(TEXT("Press again to raise affix 2 from tier 4 to tier 5, its roll kept at 25 percent: one Potency "
						   "Crystal, 450 gold, residue 40 to 80, 4 days of the run.")));

	FCataclysmItem AtSix = Helm();
	AtSix.Affixes[1].Tier = 6;
	const FCataclysmForgeAffixWork SixToSeven = UCataclysmForge::AffixWorkOf(AtSix, Raise, 1, 1, 1, 1000000);
	TestTrue(TEXT("tier 6 can be raised to 7"), SixToSeven.bPossible && SixToSeven.ToTier == 7);
	TestEqual(TEXT("for 60 residue"), SixToSeven.ResidueAfter - SixToSeven.ResidueBefore, 60.0f, 0.001f);
	TestEqual(TEXT("and 6 days"), SixToSeven.Days, 6);
	const FCataclysmForgeAffixWork Penalised = UCataclysmForge::AffixWorkOf(Helm(0.5f, 150.0f), Raise, 1, 1, 1, 1000000);
	TestEqual(TEXT("at 150 residue the penalty adds a day: 5"), Penalised.Days, 5);
	TestEqual(TEXT("and the gold is 250 x 4, 1,000"), Penalised.Gold, static_cast<int64>(1000));
	FCataclysmItem AtSeven = Helm();
	AtSeven.Affixes[1].Tier = 7;
	TestEqual(TEXT("tier 7 is the top"), UCataclysmForge::AffixWorkOf(AtSeven, Raise, 1, 1, 1, 1000000).Refusal,
			  FString(TEXT("That affix is tier 7, the highest.")));
	TestEqual(TEXT("no Potency Crystal"), UCataclysmForge::AffixWorkOf(Item, Raise, 1, 0, 1, 1000000).Refusal,
			  FString(TEXT("Raising an affix's tier takes a Potency Crystal; 0 held.")));

	FForgeAffixesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Item);
	Scene.Bag()->AddMaterial(Crystal, 1);
	const int32 DayBefore = Scene.Run->Day();
	const int64 GoldBefore = Scene.State->GetOwnGold();
	FCataclysmForgeAffixWork Done;
	FString Message;
	const ECataclysmForgeResult Result = UCataclysmForgeCraft::CraftAffix(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), Raise, 1, false, Done, Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	TestEqual(TEXT("the raise is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("RAISED: tier %d, roll %.6f, residue %.0f, day %d to %d, gold %lld"), After->Affixes[1].Tier,
							After->Affixes[1].Roll, After->Residue, DayBefore, Scene.Run->Day(),
							GoldBefore - Scene.State->GetOwnGold()));
	TestEqual(TEXT("affix 2 is tier 5"), After->Affixes[1].Tier, 5);
	TestEqual(TEXT("its roll kept at 0.25"), After->Affixes[1].Roll, 0.25f, 0.0f);
	TestTrue(TEXT("affix 1 is untouched"), After->Affixes[0].Tier == 2 && After->Affixes[0].Roll == 0.5f);
	TestEqual(TEXT("residue 40 becomes 80"), After->Residue, 80.0f, 0.001f);
	TestEqual(TEXT("the Potency Crystal is used"), Scene.Bag()->CountOfMaterial(Crystal), 0);
	TestEqual(TEXT("four days passed"), Scene.Run->Day(), DayBefore + 4);
	TestEqual(TEXT("450 gold was paid"), GoldBefore - Scene.State->GetOwnGold(), static_cast<int64>(450));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FA5. The screen: each affix's buttons, none the rule would refuse for the affix itself, and two presses doing one.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeAffixesScreenTest,
	"Cataclysm.ForgeAffixes.TheScreenOffersEachAffixCraftOnlyWhereItCanChangeTheAffix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeAffixesScreenTest::RunTest(const FString&)
{
	using namespace CataclysmForgeAffixesTest;
	FCataclysmItem Top = Helm(1.0f);
	Top.Affixes.SetNum(1);
	Top.Affixes[0].Tier = 7;
	AddInfo(FString::Printf(TEXT("AFFIX BUTTONS: %s; at the top: %s; detail: %s"), *Labels(UCataclysmForgeLayout::AffixActionsFor(Helm())),
							*Labels(UCataclysmForgeLayout::AffixActionsFor(Top)),
							*UCataclysmForgeLayout::AffixesDetailFor(Helm())));
	TestEqual(TEXT("each affix: a reroll, a perfecting below the top and a raise below tier 7"),
			  Labels(UCataclysmForgeLayout::AffixActionsFor(Helm())),
			  FString(TEXT("Reroll affix 1 | Perfect affix 1 | Raise affix 1 to tier 3 | Reroll affix 2 | Perfect affix 2 | "
						   "Raise affix 2 to tier 5")));
	TestEqual(TEXT("a tier 7 affix at the top: a reroll only"), Labels(UCataclysmForgeLayout::AffixActionsFor(Top)),
			  FString(TEXT("Reroll affix 1")));
	TestTrue(TEXT("no piece: nothing offered"), UCataclysmForgeLayout::AffixActionsFor(FCataclysmItem()).IsEmpty());
	const FString Detail = UCataclysmForgeLayout::AffixesDetailFor(Helm());
	TestTrue(TEXT("the detail numbers the affixes and says each roll"),
			 Detail.StartsWith(TEXT("Affixes: 1. ")) && Detail.Contains(TEXT("(tier 2), roll 50 percent; 2. "))
				 && Detail.EndsWith(TEXT("(tier 4), roll 25 percent")));

	FForgeAffixesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Helm());
	Scene.Bag()->AddMaterial(Crystal, 1);
	UCataclysmForgeWidget* Screen = NewObject<UCataclysmForgeWidget>();
	if (!TestNotNull(TEXT("set-up: the screen was made"), Screen))
	{
		return false;
	}
	Screen->SetWorldContextForTests(Scene.World);
	Screen->RefreshDisplay();
	Screen->ClickPieceForTests(UCataclysmForgeLayout::ButtonNameFor(FCataclysmForgeTarget::Carried(Cell)));
	TestEqual(TEXT("the chosen helm shows six affix buttons"), Screen->AffixActionsShown().Num(), 6);
	TestTrue(TEXT("and its affixes in the detail"), Screen->DetailText().Contains(TEXT("Affixes: 1. ")));

	const FName RaiseTwo(TEXT("RaiseAffix_1"));
	Screen->ClickAffixActionForTests(RaiseTwo);
	TestTrue(TEXT("the first press asks"), Screen->MessageText().StartsWith(TEXT("Press again to raise affix 2 from tier 4")));
	TestEqual(TEXT("and changes nothing"), Scene.Bag()->ItemAt(Cell)->Affixes[1].Tier, 4);
	Screen->ClickAffixActionForTests(RaiseTwo);
	AddInfo(FString::Printf(TEXT("SCREEN: %s"), *Screen->MessageText()));
	TestEqual(TEXT("the second press does it"), Scene.Bag()->ItemAt(Cell)->Affixes[1].Tier, 5);
	TestTrue(TEXT("and says so"), Screen->MessageText().StartsWith(TEXT("Affix 2 is tier 5 now, its roll kept at 25 percent.")));
	TestEqual(TEXT("the crystal is used"), Scene.Bag()->CountOfMaterial(Crystal), 0);
	TestEqual(TEXT("the button now offers tier 6"), Labels(Screen->AffixActionsShown()),
			  FString(TEXT("Reroll affix 1 | Perfect affix 1 | Raise affix 1 to tier 3 | Reroll affix 2 | Perfect affix 2 | "
						   "Raise affix 2 to tier 6")));

	// WITH NO CRYSTAL, THE FIRST PRESS SAYS WHY AND ASKS FOR NO SECOND.
	Screen->ClickAffixActionForTests(FName(TEXT("RaiseAffix_1")));
	TestEqual(TEXT("no crystal: refused on the first press"), Screen->MessageText(),
			  FString(TEXT("Raising an affix's tier takes a Potency Crystal; 0 held.")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FA6. A worn piece: a craft that stays below the Consumption Threshold is done at once; one that would cross it asks,
// changes nothing, and marks the character only when confirmed.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeAffixesThresholdTest,
	"Cataclysm.ForgeAffixes.AWornAffixCraftThatWouldCrossTheThresholdAsksAndMarksOnlyWhenConfirmed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeAffixesThresholdTest::RunTest(const FString&)
{
	using namespace CataclysmForgeAffixesTest;
	FForgeAffixesScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const float Threshold =
		UCataclysmEquipmentComponent::ConsumptionThresholdFor(ACataclysmGameMode::DifficultyTierIn(Scene.World));
	// A HELM WORN 10 BELOW THE THRESHOLD: a reroll's 2 stays below it, a raise of the tier 2 affix's 20 crosses it.
	const ECataclysmGearSlot Head = Wear(Scene, Helm(0.5f, Threshold - 10.0f - Scene.Worn()->WornResidue()));
	if (!TestTrue(TEXT("set-up: the helm is worn"), Scene.Worn()->EquippedAt(Head) != nullptr))
	{
		return false;
	}
	Scene.Bag()->AddMaterial(Mote, 1);
	Scene.Bag()->AddMaterial(Crystal, 1);

	FCataclysmForgeAffixWork Done;
	FString Message;
	const ECataclysmForgeResult Below = UCataclysmForgeCraft::CraftAffix(
		Scene.World, FCataclysmForgeTarget::Worn(Head), ECataclysmForgeAffixCraft::RerollValue, 0, false, Done, Message);
	TestEqual(TEXT("CONTROL: a reroll that stays below is done at once"), static_cast<int32>(Below),
			  static_cast<int32>(ECataclysmForgeResult::Done));
	TestFalse(TEXT("CONTROL: and does not mark the character"), Scene.State->IsMarkedByConsumption());

	const int32 DayBefore = Scene.Run->Day();
	const ECataclysmForgeResult Asked = UCataclysmForgeCraft::CraftAffix(
		Scene.World, FCataclysmForgeTarget::Worn(Head), ECataclysmForgeAffixCraft::UpgradeTier, 0, false, Done, Message);
	TestEqual(TEXT("raising affix 1's tier asks first"), static_cast<int32>(Asked),
			  static_cast<int32>(ECataclysmForgeResult::AsksAboutTheThreshold));
	TestTrue(TEXT("saying what the craft is and naming the threshold"),
			 Message.StartsWith(TEXT("Raising an affix's tier on ")) && Message.Contains(TEXT("Consumption Threshold")));
	TestEqual(TEXT("asking changes nothing: the affix is tier 2"), Scene.Worn()->EquippedAt(Head)->Affixes[0].Tier, 2);
	TestEqual(TEXT("nor the crystal"), Scene.Bag()->CountOfMaterial(Crystal), 1);
	TestEqual(TEXT("nor the day"), Scene.Run->Day(), DayBefore);
	TestFalse(TEXT("nor marks the character"), Scene.State->IsMarkedByConsumption());

	const ECataclysmForgeResult Confirmed = UCataclysmForgeCraft::CraftAffix(
		Scene.World, FCataclysmForgeTarget::Worn(Head), ECataclysmForgeAffixCraft::UpgradeTier, 0, true, Done, Message);
	AddInfo(FString::Printf(TEXT("AFFIX THRESHOLD: %.0f; worn %.0f after; confirmed %d, marked %s, build held %s"), Threshold,
							Scene.Worn()->WornResidue(), static_cast<int32>(Confirmed),
							Scene.State->IsMarkedByConsumption() ? TEXT("yes") : TEXT("no"),
							Scene.State->HoldsConsumptionBuild() ? TEXT("yes") : TEXT("no")));
	TestEqual(TEXT("confirmed, it is done"), static_cast<int32>(Confirmed), static_cast<int32>(ECataclysmForgeResult::Done));
	TestEqual(TEXT("the affix is tier 3"), Scene.Worn()->EquippedAt(Head)->Affixes[0].Tier, 3);
	TestTrue(TEXT("the Worn Residue is past the threshold"), Scene.Worn()->WornResidue() >= Threshold);
	TestTrue(TEXT("the character is marked"), Scene.State->IsMarkedByConsumption());
	TestTrue(TEXT("and the build its double wears is held"), Scene.State->HoldsConsumptionBuild());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
