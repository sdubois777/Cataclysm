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
#include "Items/CataclysmGems.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"
#include "Tests/CataclysmTestRun.h"

/**
 * COMBINING GEMS AT THE FORGE: TWO OF A KIND AND RARITY MAKE ONE OF THE NEXT RARITY, WITH NO CAP AND NO COST.
 * Issue #46, slice G6.
 *
 * The owner ruled on 2026-11-21 that it works as the stone combine: two make one, no cap by the difficulty tier ("I
 * don't think I want to cap what players can level their stuff to"), and the stone combine's cost, which is no day, no
 * residue and no gold. The scene is the one `CataclysmForgeGemsTests.cpp` builds, copied into this file's namespace
 * because the module is built in unity files.
 */
namespace CataclysmGemCombineTest
{
	const FName Life(TEXT("Gem_Of_Life"));
	const FName Viper(TEXT("Gem_Of_The_Viper"));
	const FName Rot(TEXT("Gem_Of_Rot"));

	struct FGemCombineScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmEmpireRun* Run = nullptr;

		bool IsUsable() const
		{
			return World && Instance && Character && Character->GetInventory() && Character->GetEquipment() && Run
				&& Run->Clock;
		}
		UCataclysmInventoryComponent* Bag() const { return Character->GetInventory(); }
	};

	/** A world with this project's game instance, a possessed player and a run; nothing carried, nothing worn. */
	FGemCombineScene MakeScene()
	{
		FGemCombineScene Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.Instance = NewObject<UCataclysmGameInstance>();
		Out.World->SetGameInstance(Out.Instance);
		ACataclysmPlayerState* State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (State && Controller && Out.Character)
		{
			Controller->SetPlayerState(State);
			Controller->Possess(Out.Character);
			FCataclysmCharacterPartition Partition;
			Partition.Lethality = ECataclysmLethality::Standard;
			Partition.Population = ECataclysmPopulation::Offline;
			// SOLO SELF-FOUND, WITH GOLD OF ITS OWN, so every craft can pay (#2830) and no account record is written.
			Partition.bSoloSelfFound = true;
			State->SetIdentity(TEXT("Lapidary"), Partition);
			State->SetOwnGold(1000000);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Out.Run->Begin(/* InSeed */ 2846, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
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
// GC1. The rule: two make one of the next rarity, at any rarity up to Cataclysmic, and every refusal.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemCombineRulesTest,
	"Cataclysm.GemCombine.TwoGemsMakeOneOfTheNextRarityAtAnyRarityBelowCataclysmic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemCombineRulesTest::RunTest(const FString&)
{
	using namespace CataclysmGemCombineTest;

	// EVERY RARITY BELOW CATACLYSMIC MAKES THE NEXT, with two held: no tier enters the rule.
	FString Combines;
	for (uint8 Rung = 0; Rung < static_cast<uint8>(ECataclysmRarity::Cataclysmic); ++Rung)
	{
		const FCataclysmForgeGemCombine Each =
			UCataclysmForge::GemCombineOf(Life, static_cast<ECataclysmRarity>(Rung), 2, 1, 1000000);
		Combines += FString::Printf(TEXT(" %s to %s;"), *UCataclysmGems::RarityName(Each.From),
								 *UCataclysmGems::RarityName(Each.Into));
		TestTrue(FString::Printf(TEXT("two %s Of Life make the next rarity"),
								 *UCataclysmGems::RarityName(static_cast<ECataclysmRarity>(Rung))),
				 Each.bPossible && static_cast<uint8>(Each.Into) == Rung + 1);
	}
	AddInfo(FString::Printf(TEXT("GEM COMBINES:%s"), *Combines));
	TestEqual(TEXT("a combine takes the stone combine's two"), UCataclysmForge::GemsPerCombine,
			  UCataclysmForge::StonesPerCombine);

	const FCataclysmForgeGemCombine Made = UCataclysmForge::GemCombineOf(Life, ECataclysmRarity::Everyday, 2, 1, 1000000);
	TestTrue(TEXT("two Everyday Of Life combine"), Made.bPossible);
	TestTrue(TEXT("into a Quality one"), Made.Into == ECataclysmRarity::Quality);
	TestEqual(TEXT("taking two"), Made.GemsNeeded, 2);

	TestEqual(TEXT("one held is refused"), UCataclysmForge::GemCombineOf(Life, ECataclysmRarity::Everyday, 1, 1, 1000000).Refusal,
			  FString(TEXT("Combining takes two Of Life (Everyday); 1 held.")));
	TestEqual(TEXT("a Cataclysmic gem is the highest"),
			  UCataclysmForge::GemCombineOf(Life, ECataclysmRarity::Cataclysmic, 4, 1, 1000000).Refusal,
			  FString(TEXT("A Cataclysmic gem is the highest; there is none above it to make.")));
	TestEqual(TEXT("no gem is refused"), UCataclysmForge::GemCombineOf(NAME_None, ECataclysmRarity::Everyday, 2, 1, 1000000).Refusal,
			  FString(TEXT("There is no gem to combine.")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GC2. The craft: two gems leave the bag, one of the next rarity joins it, no day passes, at any rarity; one held moves
// nothing.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemCombineCraftTest,
	"Cataclysm.GemCombine.CombiningTakesTwoGemsGivesOneOfTheNextRarityAndPassesNoDayWithNoTierCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemCombineCraftTest::RunTest(const FString&)
{
	using namespace CataclysmGemCombineTest;
	FGemCombineScene Scene = MakeScene();
	ON_SCOPE_EXIT { if (Scene.World) { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); } };
	if (!TestTrue(TEXT("set-up: a world, a player and a run"), Scene.IsUsable())
		|| !TestTrue(TEXT("set-up: three Everyday Of Life"), Scene.Bag()->AddGems(Life, ECataclysmRarity::Everyday, 3))
		|| !TestTrue(TEXT("set-up: two Quality Of Rot"), Scene.Bag()->AddGems(Rot, ECataclysmRarity::Quality, 2)))
	{
		return false;
	}
	const int32 DayBefore = Scene.Run->Day();

	FCataclysmForgeGemCombine Done;
	FString Reason;
	if (!TestTrue(TEXT("two Everyday Of Life combine"),
				  UCataclysmForgeCraft::CombineGems(Scene.World, Life, ECataclysmRarity::Everyday, Done, Reason)))
	{
		AddError(Reason);
		return false;
	}
	AddInfo(FString::Printf(TEXT("GEMS COMBINED: %d Everyday and %d Quality Of Life held, day %d to %d"),
							Scene.Bag()->CountOfGems(Life, ECataclysmRarity::Everyday),
							Scene.Bag()->CountOfGems(Life, ECataclysmRarity::Quality), DayBefore, Scene.Run->Day()));
	TestEqual(TEXT("one Everyday is left"), Scene.Bag()->CountOfGems(Life, ECataclysmRarity::Everyday), 1);
	TestEqual(TEXT("and one Quality was made"), Scene.Bag()->CountOfGems(Life, ECataclysmRarity::Quality), 1);
	TestEqual(TEXT("and no day passed"), Scene.Run->Day(), DayBefore);

	// NO CAP BY THE TIER: at tier 1, a world with no game mode, two Quality Of Rot make a Superb.
	TestTrue(TEXT("two Quality Of Rot combine at tier 1"),
			 UCataclysmForgeCraft::CombineGems(Scene.World, Rot, ECataclysmRarity::Quality, Done, Reason));
	TestEqual(TEXT("both Quality are gone"), Scene.Bag()->CountOfGems(Rot, ECataclysmRarity::Quality), 0);
	TestEqual(TEXT("and a Superb was made"), Scene.Bag()->CountOfGems(Rot, ECataclysmRarity::Superb), 1);
	TestEqual(TEXT("and still no day passed"), Scene.Run->Day(), DayBefore);

	// ONE HELD: refused, and it stays.
	TestFalse(TEXT("the one Everyday Of Life left does not combine"),
			  UCataclysmForgeCraft::CombineGems(Scene.World, Life, ECataclysmRarity::Everyday, Done, Reason));
	TestEqual(TEXT("it is still carried"), Scene.Bag()->CountOfGems(Life, ECataclysmRarity::Everyday), 1);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GC3. The screen offers a combine for each carried gem stack below Cataclysmic, with its count.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemCombineScreenTest,
	"Cataclysm.GemCombine.TheScreenOffersACombineForEachGemStackBelowCataclysmic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemCombineScreenTest::RunTest(const FString&)
{
	using namespace CataclysmGemCombineTest;
	UCataclysmInventoryComponent* Bag = NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("set-up: a bag"), Bag)
		|| !TestTrue(TEXT("set-up: three Superb Of Life"), Bag->AddGems(Life, ECataclysmRarity::Superb, 3))
		|| !TestTrue(TEXT("set-up: two Cataclysmic Of The Viper"), Bag->AddGems(Viper, ECataclysmRarity::Cataclysmic, 2))
		|| !TestTrue(TEXT("set-up: one Everyday Of Rot"), Bag->AddGems(Rot, ECataclysmRarity::Everyday, 1))
		|| !TestEqual(TEXT("set-up: as three gem stacks"), Bag->GetGemStacks().Num(), 3))
	{
		return false;
	}

	const TArray<FCataclysmForgeGemCombineAction> Shown = UCataclysmForgeLayout::GemCombineActionsFor(Bag);
	FString Labels;
	for (const FCataclysmForgeGemCombineAction& Action : Shown)
	{
		Labels += TEXT(" '") + Action.Label + TEXT("'");
	}
	AddInfo(FString::Printf(TEXT("GEM COMBINES SHOWN:%s"), *Labels));
	if (!TestEqual(TEXT("two combines: the Life and the Rot, and not the Cataclysmic Viper"), Shown.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("the Life's words"), Shown[0].Label,
			  FString(TEXT("Combine two Of Life (Superb) into one Of Life (Masterful), 3 held")));
	TestEqual(TEXT("the Rot's, offered with one held so that a press says why"), Shown[1].Label,
			  FString(TEXT("Combine two Of Rot (Everyday) into one Of Rot (Quality), 1 held")));

	FCataclysmForgeGemCombineAction Found;
	TestTrue(TEXT("a button finds its combine"),
			 UCataclysmForgeLayout::GemCombineActionForButton(Shown[1].ButtonName, Shown, Found) && Found.Gem == Rot);
	TestFalse(TEXT("a stone combine's button is no gem combine"),
			  UCataclysmForgeLayout::GemCombineActionForButton(UCataclysmForgeLayout::CombineButtonNameFor(2), Shown, Found));
	TestEqual(TEXT("and a gem combine's button is no stone combine"),
			  UCataclysmForgeLayout::CombineLevelForButton(Shown[0].ButtonName), static_cast<int32>(INDEX_NONE));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
