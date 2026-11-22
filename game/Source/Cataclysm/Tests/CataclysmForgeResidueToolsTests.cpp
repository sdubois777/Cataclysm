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
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "DayClock/CataclysmDayClock.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * THE FORGE'S ONE ITEM-CRAFT SEQUENCE AND ITS RESIDUE TOOLS. Issue #47, slice F5, ruled by the coordinating session on
 * 2026-11-22.
 *
 * Every item craft goes through `UCataclysmForgeCraft::DoItemCraft`. The Chaos Stabilizer and the Tainted Shards are
 * options on a craft's own press: the Stabilizer halves the residue the craft adds (Q27); each Shard removes one day of
 * the residue's penalty, `CR / 100` rounded down, never a base day (Q24, G2); each charges its row's 250 Base Gold
 * through `GoldFor`, the Shards once per shard (G1). The cleanse halves an item's residue in a day for each 500 residue
 * or part of it, at least one (Q25), and lifts no Consumption mark (Q26). The scene is `CataclysmForgeAffixesTests.cpp`'s,
 * copied into this file's namespace because the module is built in unity files, and named for this file: a test type
 * named `FScene` is ambiguous with the engine's (`tools/tests/test_test_files_declare_no_engine_global_type.py`).
 */
namespace CataclysmForgeResidueToolsTest
{
	const FName Stabilizer(UCataclysmForge::ChaosStabilizerRow);
	const FName Shard(UCataclysmForge::TaintedShardRow);
	const FName Essence(UCataclysmForge::PurifiedEssenceRow);
	const FName Core(UCataclysmForge::ShatteredCoreRow);

	/** A helm at a gear level, carrying `Residue`, with one empty Defense socket of the two a Head takes. */
	FCataclysmItem Helm(float Residue, int32 GearLevel = 0)
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		Item.GearLevel = GearLevel;
		Item.Residue = Residue;
		FCataclysmSocket& Socket = Item.Sockets.AddDefaulted_GetRef();
		Socket.Type = ECataclysmSocketType::Defense;
		return Item;
	}

	FCataclysmForgeCraftOptions OptionsOf(bool bStabilizer, int32 Shards)
	{
		FCataclysmForgeCraftOptions Out;
		Out.bChaosStabilizer = bStabilizer;
		Out.TaintedShards = Shards;
		return Out;
	}

	struct FResidueToolsScene
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
	FResidueToolsScene MakeScene()
	{
		FResidueToolsScene Out;
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
			Out.State->SetIdentity(TEXT("Cleanser"), Partition);
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

	ECataclysmGearSlot Wear(const FResidueToolsScene& Scene, const FCataclysmItem& Item)
	{
		FCataclysmItem First;
		FCataclysmItem Second;
		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		Scene.Worn()->Equip(Item, First, Second, Went);
		return Went;
	}

	FString Labels(const TArray<FCataclysmForgeOptionAction>& Actions)
	{
		TArray<FString> Out;
		for (const FCataclysmForgeOptionAction& Action : Actions)
		{
			Out.Add(Action.Label);
		}
		return FString::Join(Out, TEXT(" | "));
	}
}

// --------------------------------------------------------------------------------------------------------------------
// FR1. The options, alone: what each does to an added socket at 250 residue, its gold, its materials and their tiers,
// and every refusal.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeResidueToolsOptionsTest,
	"Cataclysm.ForgeResidueTools.TheOptionsHalveTheResidueAndRemovePenaltyDaysEachAtItsRowsGold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeResidueToolsOptionsTest::RunTest(const FString&)
{
	using namespace CataclysmForgeResidueToolsTest;
	// AN ADDED SOCKET AT 250 RESIDUE, TIER 1: 15 residue, 3 base days and 2 penalty days, 750 x 6 = 4,500 gold.
	const auto On = [](const FCataclysmForgeCraftOptions& Options, int32 Stabilizers, int32 Shards, int64 Gold = 1000000)
	{
		return UCataclysmForge::OptionsOn(250.0f, 265.0f, 5, 4500, Options, Stabilizers, Shards, 1, Gold);
	};
	const FCataclysmForgeOptionEffect None = On(OptionsOf(false, 0), 1, 2);
	TestTrue(TEXT("no option: the plan as it is, no gold, no material"),
			 None.bPossible && None.ResidueAfter == 265.0f && None.Days == 5 && None.Gold == 0 && None.Materials.IsEmpty());

	const FCataclysmForgeOptionEffect Halved = On(OptionsOf(true, 0), 1, 0);
	AddInfo(FString::Printf(TEXT("STABILIZER: residue %.2f, saved %.2f, days %d, gold %lld"), Halved.ResidueAfter,
							Halved.ResidueSaved, Halved.Days, Halved.Gold));
	TestEqual(TEXT("a Stabilizer halves the 15 added: 257.5"), Halved.ResidueAfter, 257.5f, 0.001f);
	TestEqual(TEXT("7.5 saved"), Halved.ResidueSaved, 7.5f, 0.001f);
	TestEqual(TEXT("the days are as they were"), Halved.Days, 5);
	TestEqual(TEXT("250 x 6 = 1,500 gold"), Halved.Gold, static_cast<int64>(1500));
	TestTrue(TEXT("one Chaos Stabilizer taken"),
			 Halved.Materials.Num() == 1 && Halved.Materials[0].Material == Stabilizer && Halved.Materials[0].Count == 1);

	const FCataclysmForgeOptionEffect One = On(OptionsOf(false, 1), 0, 2);
	const FCataclysmForgeOptionEffect Two = On(OptionsOf(false, 2), 0, 2);
	TestTrue(TEXT("1 Shard: 4 days, not 5, for 1,500 gold"), One.Days == 4 && One.DaysSaved == 1 && One.Gold == 1500);
	TestTrue(TEXT("2 Shards: 3 days, the base, for 3,000 gold"), Two.Days == 3 && Two.Gold == 3000);
	TestTrue(TEXT("2 Tainted Shards taken"),
			 Two.Materials.Num() == 1 && Two.Materials[0].Material == Shard && Two.Materials[0].Count == 2);
	const FCataclysmForgeOptionEffect Both = On(OptionsOf(true, 1), 1, 1);
	TestTrue(TEXT("both: 257.5 in 4 days for 3,000 gold, the Stabilizer then the Shard"),
			 Both.bPossible && Both.ResidueAfter == 257.5f && Both.Days == 4 && Both.Gold == 3000 && Both.Materials.Num() == 2
				 && Both.Materials[0].Material == Stabilizer && Both.Materials[1].Material == Shard);

	TestEqual(TEXT("3 Shards against 2 penalty days"), On(OptionsOf(false, 3), 0, 3).Refusal,
			  FString(TEXT("This craft has 2 days of the residue's penalty, so at most 2 Tainted Shards can be used.")));
	TestEqual(TEXT("a Shard not held"), On(OptionsOf(false, 1), 0, 0).Refusal,
			  FString(TEXT("1 Tainted Shard is asked for; 0 held.")));
	TestEqual(TEXT("a Stabilizer not held"), On(OptionsOf(true, 0), 0, 0).Refusal,
			  FString(TEXT("A Chaos Stabilizer is asked for; 0 held.")));
	TestEqual(TEXT("a Shard below 100 residue"),
			  UCataclysmForge::OptionsOn(40.0f, 55.0f, 3, 1350, OptionsOf(false, 1), 0, 1, 1, 1000000).Refusal,
			  FString(TEXT("This craft has no day of the residue's penalty for a Tainted Shard to remove.")));
	TestEqual(TEXT("a Stabilizer on a craft adding nothing"),
			  UCataclysmForge::OptionsOn(40.0f, 40.0f, 3, 1350, OptionsOf(true, 0), 1, 0, 1, 1000000).Refusal,
			  FString(TEXT("This craft adds no residue for a Chaos Stabilizer to halve.")));
	TestEqual(TEXT("the craft's gold and the options' together"), On(OptionsOf(true, 1), 1, 1, 7000).Refusal,
			  FString(TEXT("The craft with its options takes 7,500 gold; 7,000 gold held.")));
	TestTrue(TEXT("the penalty days: 0 at 99, 1 at 100, 2 at 250"),
			 UCataclysmForge::PenaltyDaysFor(99.0f) == 0 && UCataclysmForge::PenaltyDaysFor(100.0f) == 1
				 && UCataclysmForge::PenaltyDaysFor(250.0f) == 2);

	// Q17: THE TIER OF EACH MATERIAL TAKEN, from the table, as the material nodes read it.
	AddInfo(FString::Printf(TEXT("TIERS: Stabilizer %d, Shard %d, Essence %d, +4 stone %d, +5 stone %d, unknown %d"),
							UCataclysmForgeCraft::MaterialTierOf(Stabilizer), UCataclysmForgeCraft::MaterialTierOf(Shard),
							UCataclysmForgeCraft::MaterialTierOf(Essence),
							UCataclysmForgeCraft::MaterialTierOf(UCataclysmForge::StoneFor(4)),
							UCataclysmForgeCraft::MaterialTierOf(UCataclysmForge::StoneFor(5)),
							UCataclysmForgeCraft::MaterialTierOf(FName(TEXT("No_Such_Material")))));
	TestEqual(TEXT("a Chaos Stabilizer is tier 3"), UCataclysmForgeCraft::MaterialTierOf(Stabilizer), 3);
	TestEqual(TEXT("a Tainted Shard tier 2"), UCataclysmForgeCraft::MaterialTierOf(Shard), 2);
	TestEqual(TEXT("a Purified Essence tier 5"), UCataclysmForgeCraft::MaterialTierOf(Essence), 5);
	TestEqual(TEXT("a +4 stone tier 2"), UCataclysmForgeCraft::MaterialTierOf(UCataclysmForge::StoneFor(4)), 2);
	TestEqual(TEXT("a +5 stone tier 3"), UCataclysmForgeCraft::MaterialTierOf(UCataclysmForge::StoneFor(5)), 3);
	TestEqual(TEXT("a row the table lacks 0"), UCataclysmForgeCraft::MaterialTierOf(FName(TEXT("No_Such_Material"))), 0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FR2. An upgrade with both options on a carried helm at 250 residue: the stones and both options' materials taken,
// one payment, the residue halved, a penalty day removed.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeResidueToolsUpgradeTest,
	"Cataclysm.ForgeResidueTools.AnUpgradeWithBothOptionsTakesTheirMaterialsAndGoldAndSavesResidueAndADay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeResidueToolsUpgradeTest::RunTest(const FString&)
{
	using namespace CataclysmForgeResidueToolsTest;
	FResidueToolsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Helm(250.0f, 2));
	Scene.Bag()->AddMaterial(UCataclysmForge::StoneFor(3), 2);
	Scene.Bag()->AddMaterial(Stabilizer, 1);
	Scene.Bag()->AddMaterial(Shard, 1);
	const int32 DayBefore = Scene.Run->Day();
	const int64 GoldBefore = Scene.State->GetOwnGold();

	FCataclysmForgeUpgrade Done;
	FString Message;
	const ECataclysmForgeResult Result = UCataclysmForgeCraft::Upgrade(
		Scene.World, FCataclysmForgeTarget::Carried(Cell), false, Done, Message, OptionsOf(true, 1));
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	AddInfo(FString::Printf(TEXT("UPGRADE WITH OPTIONS: result %d '%s'; +%d, residue %.2f, day %d to %d, gold %lld"),
							static_cast<int32>(Result), *Message, After ? After->GearLevel : -1, After ? After->Residue : -1.0f,
							DayBefore, Scene.Run->Day(), GoldBefore - Scene.State->GetOwnGold()));
	TestEqual(TEXT("the upgrade is done"), static_cast<int32>(Result), static_cast<int32>(ECataclysmForgeResult::Done));
	if (!TestNotNull(TEXT("the helm is still in its cell"), After))
	{
		return false;
	}
	TestEqual(TEXT("raised to +3"), After->GearLevel, 3);
	TestEqual(TEXT("25 residue halved: 250 to 262.5"), After->Residue, 262.5f, 0.001f);
	TestEqual(TEXT("3 level days and 2 penalty days, less 1: 4 days"), Scene.Run->Day(), DayBefore + 4);
	TestEqual(TEXT("4,500 for the upgrade and 1,500 for each option: 7,500"), GoldBefore - Scene.State->GetOwnGold(),
			  static_cast<int64>(7500));
	TestTrue(TEXT("and the craft reports them"), Done.Days == 4 && Done.Gold == 7500);
	TestEqual(TEXT("the two stones are used"), Scene.Bag()->CountOfMaterial(UCataclysmForge::StoneFor(3)), 0);
	TestEqual(TEXT("the Stabilizer is used"), Scene.Bag()->CountOfMaterial(Stabilizer), 0);
	TestEqual(TEXT("the Shard is used"), Scene.Bag()->CountOfMaterial(Shard), 0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FR3. The one sequence: a change it is handed that refuses gives back the gold and every material; a material that
// cannot be taken takes none; a change that succeeds is written, its residue set, its days passed before its line.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeResidueToolsSequenceTest,
	"Cataclysm.ForgeResidueTools.TheOneSequenceGivesBackEverythingWhenAChangeIsRefusedAndSaysItsLineAfterTheDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeResidueToolsSequenceTest::RunTest(const FString&)
{
	using namespace CataclysmForgeResidueToolsTest;
	FResidueToolsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Helm(40.0f));
	Scene.Bag()->AddMaterial(Core, 1);
	Scene.Bag()->AddMaterial(Shard, 1);
	FCataclysmForgeCraftStep Step;
	Step.Target = FCataclysmForgeTarget::Carried(Cell);
	Step.Materials = {{Core, 1}, {Shard, 1}};
	Step.Gold = 1000;
	Step.ResidueAfter = 55.0f;
	Step.Days = 2;
	Step.ThresholdWords = TEXT("Testing");
	const int64 GoldBefore = Scene.State->GetOwnGold();
	const int32 DayBefore = Scene.Run->Day();

	FString Message;
	const ECataclysmForgeResult Refused = UCataclysmForgeCraft::DoItemCraft(
		Scene.World, Step, false,
		[](const FCataclysmItem&, FCataclysmItem&, const UCataclysmEmpireRun&, FString& OutRefusal)
		{
			OutRefusal = TEXT("No change today.");
			return false;
		},
		[](const FString&, int32, int32) {}, FCataclysmForgeCraftHooks(), Message);
	TestEqual(TEXT("a refused change refuses the craft"), static_cast<int32>(Refused),
			  static_cast<int32>(ECataclysmForgeResult::Refused));
	TestEqual(TEXT("with the change's own reason"), Message, FString(TEXT("No change today.")));
	TestEqual(TEXT("the gold is given back"), Scene.State->GetOwnGold(), GoldBefore);
	TestTrue(TEXT("both materials are given back"),
			 Scene.Bag()->CountOfMaterial(Core) == 1 && Scene.Bag()->CountOfMaterial(Shard) == 1);
	TestEqual(TEXT("the residue is as it was"), Scene.Bag()->ItemAt(Cell)->Residue, 40.0f, 0.001f);
	TestEqual(TEXT("and no day passed"), Scene.Run->Day(), DayBefore);

	FCataclysmForgeCraftStep TooMany = Step;
	TooMany.Materials = {{Shard, 1}, {Core, 2}};
	const ECataclysmForgeResult Short = UCataclysmForgeCraft::DoItemCraft(
		Scene.World, TooMany, false, [](const FCataclysmItem&, FCataclysmItem&, const UCataclysmEmpireRun&, FString&) { return true; },
		[](const FString&, int32, int32) {}, FCataclysmForgeCraftHooks(), Message);
	TestEqual(TEXT("two cores with one held refuses"), static_cast<int32>(Short), static_cast<int32>(ECataclysmForgeResult::Refused));
	TestTrue(TEXT("and takes none, the Shard it took first given back"),
			 Scene.Bag()->CountOfMaterial(Core) == 1 && Scene.Bag()->CountOfMaterial(Shard) == 1
				 && Scene.State->GetOwnGold() == GoldBefore);

	int32 LineFrom = -1;
	int32 LineTo = -1;
	const ECataclysmForgeResult Done = UCataclysmForgeCraft::DoItemCraft(
		Scene.World, Step, false,
		[](const FCataclysmItem&, FCataclysmItem& Changed, const UCataclysmEmpireRun&, FString&)
		{
			Changed.GearLevel = 7;
			return true;
		},
		[&](const FString&, int32 DayFrom, int32 DayTo)
		{
			LineFrom = DayFrom;
			LineTo = DayTo;
		},
		FCataclysmForgeCraftHooks(), Message);
	const FCataclysmItem* After = Scene.Bag()->ItemAt(Cell);
	AddInfo(FString::Printf(TEXT("ONE SEQUENCE: refused '%s'; done %d, +%d, residue %.0f, line day %d to %d"), *Message,
							static_cast<int32>(Done), After ? After->GearLevel : -1, After ? After->Residue : -1.0f, LineFrom,
							LineTo));
	TestEqual(TEXT("a change that succeeds is done"), static_cast<int32>(Done), static_cast<int32>(ECataclysmForgeResult::Done));
	TestTrue(TEXT("written, with the step's residue"), After != nullptr && After->GearLevel == 7 && After->Residue == 55.0f);
	TestTrue(TEXT("the line is said once its 2 days have passed"), LineFrom == DayBefore && LineTo == DayBefore + 2);
	TestTrue(TEXT("both materials and the gold are taken"),
			 Scene.Bag()->CountOfMaterial(Core) == 0 && Scene.Bag()->CountOfMaterial(Shard) == 0
				 && GoldBefore - Scene.State->GetOwnGold() == 1000);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FR4. The cleanse: its figures, question and refusals; on a worn piece of a marked character, the residue halved, no
// threshold question, and the mark kept.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeResidueToolsCleanseTest,
	"Cataclysm.ForgeResidueTools.TheCleanseHalvesTheResidueInADayPer500AndLiftsNoMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeResidueToolsCleanseTest::RunTest(const FString&)
{
	using namespace CataclysmForgeResidueToolsTest;
	const FCataclysmForgeCleanse Plan = UCataclysmForge::CleanseOf(Helm(1200.0f), 1, 1, 1000000);
	TestTrue(TEXT("a helm at 1,200 residue can be cleansed"), Plan.bPossible && Plan.Material == Essence);
	TestEqual(TEXT("to 600"), Plan.ResidueAfter, 600.0f, 0.001f);
	TestEqual(TEXT("in 3 days, one for each 500 or part of it"), Plan.Days, 3);
	TestEqual(TEXT("250 x (1200 / 50 + 1) = 6,250 gold"), Plan.Gold, static_cast<int64>(6250));
	TestEqual(TEXT("the first press's question"), UCataclysmForge::CleanseQuestion(Plan),
			  FString(TEXT("Press again to cleanse half the residue: one Purified Essence, 6,250 gold, residue 1200 to 600, 3 "
						   "days of the run.")));
	TestEqual(TEXT("500 residue: 1 day"), UCataclysmForge::CleanseOf(Helm(500.0f), 1, 1, 1000000).Days, 1);
	TestEqual(TEXT("1,001 residue: 3 days"), UCataclysmForge::CleanseOf(Helm(1001.0f), 1, 1, 1000000).Days, 3);
	TestEqual(TEXT("10 residue: at least 1 day"), UCataclysmForge::CleanseOf(Helm(10.0f), 1, 1, 1000000).Days, 1);
	TestEqual(TEXT("no residue"), UCataclysmForge::CleanseOf(Helm(0.0f), 1, 1, 1000000).Refusal,
			  FString(TEXT("That piece carries no residue.")));
	TestEqual(TEXT("no Purified Essence"), UCataclysmForge::CleanseOf(Helm(1200.0f), 0, 1, 1000000).Refusal,
			  FString(TEXT("Cleansing takes a Purified Essence; 0 held.")));

	FResidueToolsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const ECataclysmGearSlot Head = Wear(Scene, Helm(1200.0f));
	if (!TestTrue(TEXT("set-up: the helm is worn"), Scene.Worn()->EquippedAt(Head) != nullptr))
	{
		return false;
	}
	Scene.Bag()->AddMaterial(Essence, 1);
	Scene.State->MarkByConsumption();
	const int32 DayBefore = Scene.Run->Day();
	FCataclysmForgeCleanse Done;
	FString Message;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::Cleanse(Scene.World, FCataclysmForgeTarget::Worn(Head), Done, Message);
	AddInfo(FString::Printf(TEXT("CLEANSED: result %d '%s', residue %.0f, day %d to %d, marked %s"), static_cast<int32>(Result),
							*Message, Scene.Worn()->EquippedAt(Head)->Residue, DayBefore, Scene.Run->Day(),
							Scene.State->IsMarkedByConsumption() ? TEXT("yes") : TEXT("no")));
	TestEqual(TEXT("the cleanse is done without a threshold question"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmForgeResult::Done));
	TestEqual(TEXT("the worn helm carries 600"), Scene.Worn()->EquippedAt(Head)->Residue, 600.0f, 0.001f);
	TestEqual(TEXT("3 days passed"), Scene.Run->Day(), DayBefore + 3);
	TestEqual(TEXT("the Purified Essence is used"), Scene.Bag()->CountOfMaterial(Essence), 0);
	TestTrue(TEXT("and the character is still marked: only killing the double clears it"),
			 Scene.State->IsMarkedByConsumption());
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FR5. The screen: the residue tools' buttons, the options' effect in a craft's question, the options used and then
// held to what is left.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeResidueToolsScreenTest,
	"Cataclysm.ForgeResidueTools.TheScreenSaysWhatTheOptionsDoBeforeACraftAndUsesThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeResidueToolsScreenTest::RunTest(const FString&)
{
	using namespace CataclysmForgeResidueToolsTest;
	FResidueToolsScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 Cell = Scene.Bag()->AddItem(Helm(250.0f));
	Scene.Bag()->AddMaterial(Core, 1);
	Scene.Bag()->AddMaterial(Stabilizer, 1);
	Scene.Bag()->AddMaterial(Shard, 2);
	Scene.Bag()->AddMaterial(Essence, 1);
	UCataclysmForgeWidget* Screen = NewObject<UCataclysmForgeWidget>();
	if (!TestNotNull(TEXT("set-up: the screen was made"), Screen))
	{
		return false;
	}
	Screen->SetWorldContextForTests(Scene.World);
	Screen->RefreshDisplay();
	Screen->ClickPieceForTests(UCataclysmForgeLayout::ButtonNameFor(FCataclysmForgeTarget::Carried(Cell)));
	TestEqual(TEXT("the residue tools' buttons"), Labels(Screen->OptionActionsShown()),
			  FString(TEXT("Chaos Stabilizer: off (1 held) | Tainted Shards: 0 of 2 held | Cleanse half the residue (1 "
						   "Purified Essence held)")));

	Screen->ClickOptionForTests(UCataclysmForgeLayout::StabilizerButtonName());
	Screen->ClickOptionForTests(UCataclysmForgeLayout::ShardsButtonName());
	TestTrue(TEXT("the Stabilizer is on and 1 Shard asked"),
			 Screen->OptionsForTests().bChaosStabilizer && Screen->OptionsForTests().TaintedShards == 1);
	TestTrue(TEXT("and the buttons say so"), Labels(Screen->OptionActionsShown()).StartsWith(
												 TEXT("Chaos Stabilizer: on (1 held) | Tainted Shards: 1 of 2 held")));

	const FName AddSocket(TEXT("AddSocket"));
	Screen->ClickSocketActionForTests(AddSocket);
	AddInfo(FString::Printf(TEXT("SCREEN ASKED: %s"), *Screen->MessageText()));
	TestTrue(TEXT("the first press says what the options do"),
			 Screen->MessageText().Contains(TEXT("With a Chaos Stabilizer: residue 250 to 257.5, not 265."))
				 && Screen->MessageText().Contains(TEXT("With 1 Tainted Shard: 4 days, not 5."))
				 && Screen->MessageText().EndsWith(TEXT("The options cost 3,000 gold more.")));
	TestEqual(TEXT("and changes nothing"), Scene.Bag()->ItemAt(Cell)->Sockets.Num(), 1);
	Screen->ClickSocketActionForTests(AddSocket);
	AddInfo(FString::Printf(TEXT("SCREEN DONE: %s"), *Screen->MessageText()));
	TestEqual(TEXT("the second press adds the socket"), Scene.Bag()->ItemAt(Cell)->Sockets.Num(), 2);
	TestEqual(TEXT("with the residue halved"), Scene.Bag()->ItemAt(Cell)->Residue, 257.5f, 0.001f);
	TestTrue(TEXT("the Stabilizer and one Shard are used"),
			 Scene.Bag()->CountOfMaterial(Stabilizer) == 0 && Scene.Bag()->CountOfMaterial(Shard) == 1);
	TestTrue(TEXT("and the options are held to what is left: no Stabilizer, 1 Shard"),
			 !Screen->OptionsForTests().bChaosStabilizer && Screen->OptionsForTests().TaintedShards == 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
