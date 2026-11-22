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
#include "Items/CataclysmDropRoll.h"
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

/**
 * COMBINING GEMS AT THE FORGE: TWO OF A KIND AND RARITY MAKE ONE OF THE NEXT RARITY, NO DAY, UP TO THE TIER'S CEILING.
 * Issue #46, slice G6.
 *
 * Ruled by the coordinating session on 2026-11-21: two make one, as stones combine ("same as gear"); it costs no day
 * and no residue, because it changes no item; combining is capped by the difficulty tier, because how high a gem's
 * rarity can be pushed is where its power comes from. The cap is the highest rarity that drops at full weight there,
 * `UCataclysmDropRoll::HighestUnpenalisedRarity`. A world built by a test has no game mode, so the tier is 1 there,
 * and its ceiling is Quality. The scene is the one `CataclysmForgeGemsTests.cpp` builds, copied into this file's
 * namespace because the module is built in unity files.
 */
namespace CataclysmGemCombineTest
{
	const FName Life(TEXT("Gem_Of_Life"));
	const FName Viper(TEXT("Gem_Of_The_Viper"));
	const FName Rot(TEXT("Gem_Of_Rot"));

	struct FScene
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
	FScene MakeScene()
	{
		FScene Out;
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
			State->SetIdentity(TEXT("Lapidary"), Partition);
		}
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
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
// GC1. The rule: two make one of the next rarity, up to the tier's ceiling, and every refusal.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemCombineRulesTest,
	"Cataclysm.GemCombine.TwoGemsMakeOneOfTheNextRarityUpToTheTiersCeiling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemCombineRulesTest::RunTest(const FString&)
{
	using namespace CataclysmGemCombineTest;

	// THE CEILING BY TIER: the tier's own rarity and one above, Cataclysmic at the top.
	const ECataclysmRarity Expected[] = {ECataclysmRarity::Quality, ECataclysmRarity::Superb, ECataclysmRarity::Masterful,
										 ECataclysmRarity::Legendary, ECataclysmRarity::Mythical,
										 ECataclysmRarity::Ascendant, ECataclysmRarity::Cataclysmic,
										 ECataclysmRarity::Cataclysmic};
	FString Ceilings;
	for (int32 Tier = 1; Tier <= 8; ++Tier)
	{
		const ECataclysmRarity Ceiling = UCataclysmForge::GemCombineCeiling(Tier);
		Ceilings += FString::Printf(TEXT(" %d %s;"), Tier, *UCataclysmGems::RarityName(Ceiling));
		TestTrue(FString::Printf(TEXT("tier %d's ceiling is the rarity that drops there at full weight"), Tier),
				 Ceiling == Expected[Tier - 1] && Ceiling == UCataclysmDropRoll::HighestUnpenalisedRarity(Tier));
	}
	AddInfo(FString::Printf(TEXT("GEM COMBINE CEILINGS:%s"), *Ceilings));

	const FCataclysmForgeGemCombine Made = UCataclysmForge::GemCombineOf(Life, ECataclysmRarity::Everyday, 2, 1);
	TestTrue(TEXT("two Everyday Of Life combine at tier 1"), Made.bPossible);
	TestTrue(TEXT("into a Quality one"), Made.Into == ECataclysmRarity::Quality);
	TestEqual(TEXT("taking two"), Made.GemsNeeded, 2);

	TestEqual(TEXT("one held is refused"), UCataclysmForge::GemCombineOf(Life, ECataclysmRarity::Everyday, 1, 1).Refusal,
			  FString(TEXT("Combining takes two Of Life (Everyday); 1 held.")));
	TestEqual(TEXT("above tier 1's ceiling is refused"),
			  UCataclysmForge::GemCombineOf(Life, ECataclysmRarity::Quality, 5, 1).Refusal,
			  FString(TEXT("At difficulty tier 1 a gem can be combined up to Quality; a Superb Of Life is above that.")));
	TestTrue(TEXT("the same combine at tier 2, whose ceiling is Superb, is not"),
			 UCataclysmForge::GemCombineOf(Life, ECataclysmRarity::Quality, 5, 2).bPossible);
	TestTrue(TEXT("two Ascendant make a Cataclysmic at tier 8"),
			 UCataclysmForge::GemCombineOf(Life, ECataclysmRarity::Ascendant, 2, 8).bPossible);
	TestEqual(TEXT("a Cataclysmic gem is the highest"),
			  UCataclysmForge::GemCombineOf(Life, ECataclysmRarity::Cataclysmic, 4, 8).Refusal,
			  FString(TEXT("A Cataclysmic gem is the highest; there is none above it to make.")));
	TestEqual(TEXT("no gem is refused"), UCataclysmForge::GemCombineOf(NAME_None, ECataclysmRarity::Everyday, 2, 1).Refusal,
			  FString(TEXT("There is no gem to combine.")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GC2. The craft: two gems leave the bag, one of the next rarity joins it, no day passes; above the ceiling nothing moves.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemCombineCraftTest,
	"Cataclysm.GemCombine.CombiningTakesTwoGemsGivesOneOfTheNextRarityAndPassesNoDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemCombineCraftTest::RunTest(const FString&)
{
	using namespace CataclysmGemCombineTest;
	FScene Scene = MakeScene();
	ON_SCOPE_EXIT { if (Scene.World) { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); } };
	if (!TestTrue(TEXT("set-up: a world, a player and a run"), Scene.IsUsable())
		|| !TestEqual(TEXT("set-up: three Everyday Of Life"), Scene.Bag()->AddGems(Life, ECataclysmRarity::Everyday, 3), 0)
		|| !TestEqual(TEXT("set-up: two Quality Of Rot"), Scene.Bag()->AddGems(Rot, ECataclysmRarity::Quality, 2), 1))
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

	// ABOVE TIER 1'S CEILING: two Quality Of Rot would make a Superb, so nothing moves.
	TestFalse(TEXT("two Quality Of Rot do not combine at tier 1"),
			  UCataclysmForgeCraft::CombineGems(Scene.World, Rot, ECataclysmRarity::Quality, Done, Reason));
	TestEqual(TEXT("and say why"), Reason,
			  FString(TEXT("At difficulty tier 1 a gem can be combined up to Quality; a Superb Of Rot is above that.")));
	TestEqual(TEXT("both are still carried"), Scene.Bag()->CountOfGems(Rot, ECataclysmRarity::Quality), 2);
	TestEqual(TEXT("and no Superb was made"), Scene.Bag()->CountOfGems(Rot, ECataclysmRarity::Superb), 0);

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
		|| !TestEqual(TEXT("set-up: three Superb Of Life"), Bag->AddGems(Life, ECataclysmRarity::Superb, 3), 0)
		|| !TestEqual(TEXT("set-up: two Cataclysmic Of The Viper"), Bag->AddGems(Viper, ECataclysmRarity::Cataclysmic, 2), 1)
		|| !TestEqual(TEXT("set-up: one Everyday Of Rot"), Bag->AddGems(Rot, ECataclysmRarity::Everyday, 1), 2))
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
