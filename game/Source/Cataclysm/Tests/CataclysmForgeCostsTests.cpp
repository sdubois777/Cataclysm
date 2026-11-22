// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmForgeLayout.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * THE FORGE CHARGES THE CRAFTING SHEET'S COSTS. Issue #2830, slice G7 of the gems work (#46).
 *
 * The owner, 2026-11-21: "I have costs set in the crafting excel sheet. Something like that should be implemented";
 * "+0 to +10 takes 55 days per item (1+2+...+10)"; and for combining, "Gold only (Recommended)". Under the owner's
 * delegation, the coordinating session the same day: 250 gold a base day at tier 1, the sheet's multiplier
 * `(CR / 50) + 1` not rounded, and 50 gold for a combine at the lowest rung, doubled for each rung above.
 *
 * The scene's character is Solo Self-Found, so its gold is its own and no account record is written. A world built by
 * a test has no game mode, and these scenes' runs are at tier 1, whose gold scale is 1.
 */
namespace CataclysmForgeCostsTest
{
	const FName Life(TEXT("Gem_Of_Life"));

	FCataclysmItem PlainOf(const TCHAR* Base, int32 GearLevel, float Residue)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = GearLevel;
		Item.Residue = Residue;
		return Item;
	}

	FCataclysmItem Helm(float Residue)
	{
		FCataclysmItem Item = PlainOf(TEXT("Head_Helm"), 0, Residue);
		FCataclysmSocket Empty;
		Empty.Type = ECataclysmSocketType::Defense;
		FCataclysmSocket Filled;
		Filled.Type = ECataclysmSocketType::Attack;
		Filled.Gem = FName(TEXT("Gem_Of_The_Viper"));
		Filled.GemRarity = ECataclysmRarity::Superb;
		Item.Sockets = {Empty, Filled};
		return Item;
	}

	struct FScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmEmpireRun* Run = nullptr;

		bool IsUsable() const
		{
			return World && Instance && State && Character && Character->GetInventory() && Character->GetEquipment()
				&& Run && Run->Clock;
		}
		UCataclysmInventoryComponent* Bag() const { return Character->GetInventory(); }
	};

	/** A world with a possessed Solo Self-Found player holding `Gold` of its own, and a run; nothing carried. */
	FScene MakeScene(int64 Gold)
	{
		FScene Out;
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
			Partition.bSoloSelfFound = true;
			Out.State->SetIdentity(TEXT("Assayer"), Partition);
			Out.State->SetOwnGold(Gold);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		Out.Run->Begin(/* InSeed */ 2830, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		if (Out.Character && Out.Character->GetInventory() && Out.Character->GetEquipment())
		{
			Out.Character->GetInventory()->RemoveEverything();
			Out.Character->GetEquipment()->RestoreWornGear({});
		}
		return Out;
	}
}

// --------------------------------------------------------------------------------------------------------------------
// FC1. The prices: base gold by base days, the tier's scale, the unrounded residue multiplier, the combine's doubling,
// the upgrade's days by level, and every refusal for too little gold.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeCostsPricesTest,
	"Cataclysm.ForgeCosts.ACraftCosts250GoldABaseDayScaledByTheTierAndTheResidueAndACombineDoublesByRung",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeCostsPricesTest::RunTest(const FString&)
{
	using namespace CataclysmForgeCostsTest;

	TestEqual(TEXT("a craft of no days costs one day's price"), UCataclysmForge::BaseGoldFor(0), 250);
	TestEqual(TEXT("nor does a negative figure, Accelerate Craft's, cost less"), UCataclysmForge::BaseGoldFor(-1), 250);
	TestEqual(TEXT("three base days, 750"), UCataclysmForge::BaseGoldFor(3), 750);

	TestEqual(TEXT("no residue: the base"), UCataclysmForge::GoldFor(250, 0.0f, 1), static_cast<int64>(250));
	TestEqual(TEXT("50 residue: twice the base"), UCataclysmForge::GoldFor(250, 50.0f, 1), static_cast<int64>(500));
	TestEqual(TEXT("10 residue: 1.2 times, the multiplier not rounded"), UCataclysmForge::GoldFor(250, 10.0f, 1),
			  static_cast<int64>(300));
	TestEqual(TEXT("87 residue: 2.74 times"), UCataclysmForge::GoldFor(250, 87.0f, 1), static_cast<int64>(685));
	TestEqual(TEXT("tier 8: the gold scale's 30 times"), UCataclysmForge::GoldFor(250, 0.0f, 8), static_cast<int64>(7500));

	TestEqual(TEXT("a combine at the lowest rung, 50"), UCataclysmForge::CombineGoldFor(0, 1), static_cast<int64>(50));
	TestEqual(TEXT("one rung up, 100"), UCataclysmForge::CombineGoldFor(1, 1), static_cast<int64>(100));
	TestEqual(TEXT("Ascendant's rung, 1,600"), UCataclysmForge::CombineGoldFor(5, 1), static_cast<int64>(1600));
	TestEqual(TEXT("the lowest rung at tier 8, 1,500"), UCataclysmForge::CombineGoldFor(0, 8), static_cast<int64>(1500));

	// ONE GEM FROM EVERYDAY TO ASCENDANT: 32, 16, 8, 4, 2 and 1 combines.
	int64 Climb = 0;
	int32 Combines = 0;
	for (int32 Rung = 0; Rung < 6; ++Rung)
	{
		const int32 Count = 1 << (5 - Rung);
		Combines += Count;
		Climb += Count * UCataclysmForge::CombineGoldFor(Rung, 1);
	}
	TestEqual(TEXT("the climb is 63 combines"), Combines, 63);
	TestEqual(TEXT("and 9,600 gold at tier 1"), Climb, static_cast<int64>(9600));

	// +0 TO +10 AT NO RESIDUE: 1 + 2 + ... + 10 = 55 days, the owner's figure.
	int32 Days = 0;
	for (int32 Level = 0; Level < 10; ++Level)
	{
		Days += UCataclysmForge::UpgradeOf(PlainOf(TEXT("Gloves_Handwraps"), Level, 0.0f), 2, 1, 1000000).Days;
	}
	TestEqual(TEXT("+0 to +10 is 55 base days"), Days, 55);
	AddInfo(FString::Printf(TEXT("FORGE COSTS: climb %d combines %lld gold; +0 to +10 %d days"), Combines, Climb, Days));

	TestEqual(TEXT("an upgrade without its gold is refused"),
			  UCataclysmForge::UpgradeOf(PlainOf(TEXT("Gloves_Handwraps"), 2, 87.0f), 5, 1, 100).Refusal,
			  FString(TEXT("Raising it to +3 takes 2,055 gold; 100 gold held.")));
	TestEqual(TEXT("a stone combine without its gold"), UCataclysmForge::CombineOf(1, 2, 1, 10).Refusal,
			  FString(TEXT("Combining two +1 upgrade stones takes 50 gold; 10 gold held.")));
	TestEqual(TEXT("a gem combine without its gold"),
			  UCataclysmForge::GemCombineOf(Life, ECataclysmRarity::Superb, 2, 1, 0).Refusal,
			  FString(TEXT("Combining two Of Life (Superb) takes 200 gold; 0 gold held.")));
	TestEqual(TEXT("a socketing without its gold"),
			  UCataclysmForge::SocketGemOf(Helm(87.0f), 0, Life, ECataclysmRarity::Superb, TEXT("Defense"), 1, 1, 1, 684)
				  .Refusal,
			  FString(TEXT("Socketing a gem takes 685 gold; 684 gold held.")));
	TestEqual(TEXT("an unsocketing without its gold"),
			  UCataclysmForge::UnsocketGemOf(Helm(87.0f), 1, 1, true, 1, 0).Refusal,
			  FString(TEXT("Unsocketing a gem takes 685 gold; 0 gold held.")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FC2. The crafts take their gold from the character's balance; with too little, nothing moves.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeCostsCraftTest,
	"Cataclysm.ForgeCosts.ACraftTakesItsGoldAndWithTooLittleNothingMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeCostsCraftTest::RunTest(const FString&)
{
	using namespace CataclysmForgeCostsTest;
	FScene Scene = MakeScene(600);
	ON_SCOPE_EXIT { if (Scene.World) { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); } };
	if (!TestTrue(TEXT("set-up: a world, a Solo Self-Found player with 600 gold and a run"), Scene.IsUsable()))
	{
		return false;
	}
	const int32 Cell = Scene.Bag()->AddItem(PlainOf(TEXT("Gloves_Handwraps"), 0, 50.0f));
	Scene.Bag()->AddMaterial(UCataclysmForge::StoneFor(1), 4);
	Scene.Bag()->AddMaterial(UCataclysmForge::StoneFor(2), 2);
	Scene.Bag()->AddGems(Life, ECataclysmRarity::Everyday, 4);

	// +0 TO +1 AT 50 RESIDUE: 250 x 2 = 500 gold and one day.
	const int32 DayBefore = Scene.Run->Day();
	FCataclysmForgeUpgrade Done;
	FString Message;
	TestEqual(TEXT("the upgrade is done"),
			  static_cast<int32>(UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Cell), false,
															   Done, Message)),
			  static_cast<int32>(ECataclysmForgeResult::Done));
	TestEqual(TEXT("it took 500 gold"), Scene.State->GetOwnGold(), static_cast<int64>(100));
	TestEqual(TEXT("and one day"), Scene.Run->Day(), DayBefore + 1);

	// +1 TO +2 AT 75 RESIDUE: two days' 500, times 2.5, is 1,250, against 100 held. Nothing moves.
	const int32 DayRefused = Scene.Run->Day();
	TestEqual(TEXT("the next upgrade is refused for its gold"),
			  static_cast<int32>(UCataclysmForgeCraft::Upgrade(Scene.World, FCataclysmForgeTarget::Carried(Cell), false,
															   Done, Message)),
			  static_cast<int32>(ECataclysmForgeResult::Refused));
	TestEqual(TEXT("saying what it takes"), Message, FString(TEXT("Raising it to +2 takes 1,250 gold; 100 gold held.")));
	TestEqual(TEXT("the piece stays +1"), Scene.Bag()->ItemAt(Cell)->GearLevel, 1);
	TestEqual(TEXT("the +2 stones stay"), Scene.Bag()->CountOfMaterial(UCataclysmForge::StoneFor(2)), 2);
	TestEqual(TEXT("the gold stays"), Scene.State->GetOwnGold(), static_cast<int64>(100));
	TestEqual(TEXT("and no day passes"), Scene.Run->Day(), DayRefused);

	// A STONE COMBINE: 50 gold.
	FCataclysmForgeCombine Combined;
	FString Reason;
	TestTrue(TEXT("two +1 stones combine"), UCataclysmForgeCraft::Combine(Scene.World, 1, Combined, Reason));
	TestEqual(TEXT("for 50 gold"), Scene.State->GetOwnGold(), static_cast<int64>(50));

	// A GEM COMBINE: 50 gold, the last of it; the next is refused and nothing moves.
	FCataclysmForgeGemCombine Gems;
	TestTrue(TEXT("two Everyday Of Life combine"),
			 UCataclysmForgeCraft::CombineGems(Scene.World, Life, ECataclysmRarity::Everyday, Gems, Reason));
	TestEqual(TEXT("for 50 gold, the last"), Scene.State->GetOwnGold(), static_cast<int64>(0));
	TestFalse(TEXT("the next gem combine is refused"),
			  UCataclysmForgeCraft::CombineGems(Scene.World, Life, ECataclysmRarity::Everyday, Gems, Reason));
	TestEqual(TEXT("for its gold"), Reason, FString(TEXT("Combining two Of Life (Everyday) takes 50 gold; 0 gold held.")));
	TestEqual(TEXT("and both Everyday stay"), Scene.Bag()->CountOfGems(Life, ECataclysmRarity::Everyday), 2);
	AddInfo(FString::Printf(TEXT("FORGE GOLD: %lld left; refused '%s'"), Scene.State->GetOwnGold(), *Reason));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// FC3. The screen's detail names the gold and the days the level reached costs.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForgeCostsScreenTest,
	"Cataclysm.ForgeCosts.TheScreenSaysTheGoldAndTheDaysOfTheLevelReached",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmForgeCostsScreenTest::RunTest(const FString&)
{
	using namespace CataclysmForgeCostsTest;
	const FCataclysmItem Gloves = PlainOf(TEXT("Gloves_Handwraps"), 2, 87.0f);
	const FString Detail = UCataclysmForgeLayout::DetailFor(Gloves, UCataclysmForge::UpgradeOf(Gloves, 5, 1, 1000000));
	AddInfo(FString::Printf(TEXT("FORGE DETAIL: %s"), *Detail));
	TestTrue(TEXT("the detail names the gold and three days"),
			 Detail.EndsWith(TEXT(" Raising it to +3 takes two +3 stones (5 held) and 2,055 gold, adds 25 residue (to "
								  "112) and costs 3 days of the run.")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
