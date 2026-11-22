// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmGems.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmWearing.h"
#include "Misc/ScopeExit.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"

/**
 * GEM DROPS: KILLS DROP GEMS, WHICH LIE ON THE FLOOR AND GO INTO THEIR STACK. Issue #46, slice G5.
 *
 * Ruled by the coordinating session on 2026-11-21 (the plan's section 3d): a Gem Drops column on the Enemy Drops sheet,
 * measured in the simulation; the gear's rarity roll and tier gate; which gem evenly among the 28. The rate is
 * `sim/analyse_gem_drops.py`'s: the gear column times 0.0068.
 *
 * A WORLD BUILT BY A TEST HAS NO GAME MODE, so the difficulty tier is 1 there. The names are read from the generated
 * `DT_Gems`, the rates from `DT_EnemyDrops`.
 */
namespace CataclysmGemDropsTest
{
	const FName Viper(TEXT("Gem_Of_The_Viper"));
	const FName Life(TEXT("Gem_Of_Life"));

	/** The share of a gear drop a gem is, as `loot.GEM_DROPS_PER_GEAR_DROP` holds it. */
	constexpr float GemsPerGearDrop = 0.0068f;

	/** Every gem drop lying in the world. */
	TArray<ACataclysmDroppedItem*> GemDropsIn(UWorld* World)
	{
		TArray<ACataclysmDroppedItem*> Found;
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			if (IsValid(*It) && It->IsGem())
			{
				Found.Add(*It);
			}
		}
		return Found;
	}
}

// --------------------------------------------------------------------------------------------------------------------
// GD1. The gem column is the gear column times the measured share, rising with the creature's rung, and loot quantity
// raises it as it raises gear.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemDropsRateTest,
	"Cataclysm.GemDrops.TheGemColumnIsTheGearColumnTimesTheMeasuredShareAndLootQuantityRaisesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemDropsRateTest::RunTest(const FString&)
{
	using namespace CataclysmGemDropsTest;
	const UDataTable* Drops = UCataclysmDropRoll::LoadEnemyDropTable();
	if (!TestNotNull(TEXT("set-up: the enemy drop table"), Drops))
	{
		return false;
	}

	FString Table;
	float Below = -1.0f;
	for (int32 Step = 0; Step <= 5; ++Step)
	{
		const FName Rarity = UCataclysmDropSpawner::EnemyRarityForStep(Drops, Step);
		if (!TestFalse(FString::Printf(TEXT("set-up: a row has step %d"), Step), Rarity.IsNone()))
		{
			return false;
		}
		const float Gems = UCataclysmDropRoll::ExpectedGemDrops(Drops, Rarity, 100.0f);
		const float Gear = UCataclysmDropRoll::ExpectedGearDrops(Drops, Rarity, 100.0f);
		Table += FString::Printf(TEXT(" %s %.6f (gear %.2f);"), *Rarity.ToString(), Gems, Gear);

		TestEqual(FString::Printf(TEXT("a %s drops the gear column times %.4f in gems"), *Rarity.ToString(),
								  GemsPerGearDrop),
				  Gems, Gear * GemsPerGearDrop, 1e-6f);
		TestTrue(FString::Printf(TEXT("a %s drops more gems than the rung below"), *Rarity.ToString()), Gems > Below);
		TestEqual(FString::Printf(TEXT("twice the loot quantity doubles a %s's gems"), *Rarity.ToString()),
				  UCataclysmDropRoll::ExpectedGemDrops(Drops, Rarity, 200.0f), 2.0f * Gems, 1e-6f);
		Below = Gems;
	}
	AddInfo(FString::Printf(TEXT("GEM DROPS:%s"), *Table));

	TestEqual(TEXT("a Cataclysm Boss drops 0.0816 gems, the measurement's 12 x 0.0068"),
			  UCataclysmDropRoll::ExpectedGemDrops(Drops, UCataclysmDropSpawner::EnemyRarityForStep(Drops, 5), 100.0f),
			  0.0816f, 1e-6f);
	TestEqual(TEXT("no loot quantity, no gems"),
			  UCataclysmDropRoll::ExpectedGemDrops(Drops, UCataclysmDropSpawner::EnemyRarityForStep(Drops, 5), 0.0f),
			  0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GD2. Which gem is chosen evenly among the Gems sheet's rows, and a gem is named with its rarity.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemDropsWhichTest,
	"Cataclysm.GemDrops.AGemIsChosenEvenlyAmongTheGemsAndNamedWithItsRarity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemDropsWhichTest::RunTest(const FString&)
{
	using namespace CataclysmGemDropsTest;
	const UDataTable* Gems = UCataclysmGems::LoadGeneratedTable();
	if (!TestNotNull(TEXT("set-up: the gem table"), Gems))
	{
		return false;
	}
	const TArray<FName> Rows = Gems->GetRowNames();
	TestEqual(TEXT("the Gems sheet has 28 gems"), Rows.Num(), 28);

	// 1,000 DRAWS A GEM: one standard deviation is about 31, so 850 to 1,150 is nearly five either side.
	TMap<FName, int32> Counts;
	FRandomStream Stream(46);
	for (int32 Draw = 0; Draw < 1000 * Rows.Num(); ++Draw)
	{
		++Counts.FindOrAdd(UCataclysmGems::RollGem(Gems, Stream));
	}
	int32 Fewest = MAX_int32;
	int32 Most = 0;
	for (const FName& Row : Rows)
	{
		const int32 Count = Counts.FindRef(Row);
		Fewest = FMath::Min(Fewest, Count);
		Most = FMath::Max(Most, Count);
	}
	AddInfo(FString::Printf(TEXT("WHICH GEM: %d draws over %d gems, %d distinct, the fewest %d and the most %d"),
							1000 * Rows.Num(), Rows.Num(), Counts.Num(), Fewest, Most));
	TestEqual(TEXT("every draw is one of the sheet's gems"), Counts.Num(), Rows.Num());
	TestTrue(TEXT("no gem is drawn fewer than 850 times in 1,000 a gem"), Fewest >= 850);
	TestTrue(TEXT("no gem is drawn more than 1,150 times in 1,000 a gem"), Most <= 1150);

	TestTrue(TEXT("no table, no gem"), UCataclysmGems::RollGem(nullptr, Stream).IsNone());
	TestEqual(TEXT("a gem is named with its rarity"), UCataclysmGems::FloorNameOf(Viper, ECataclysmRarity::Superb),
			  FString(TEXT("Of The Viper (Superb)")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GD3. A kill's gems lie on the floor, one to a drop, at a rarity the tier allows, named and bordered by it, off the
// corpse; they come without a click, and go into the stack of their gem and rarity.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemDropsFloorTest,
	"Cataclysm.GemDrops.AKillsGemsLieOnTheFloorAtRaritiesTheTierAllowsAndGoIntoTheirStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemDropsFloorTest::RunTest(const FString&)
{
	using namespace CataclysmGemDropsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const UDataTable* Drops = UCataclysmDropRoll::LoadEnemyDropTable();
	const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable();
	if (!TestNotNull(TEXT("set-up: the enemy drop table"), Drops)
		|| !TestNotNull(TEXT("set-up: the gear rarity table"), Rarities))
	{
		return false;
	}

	// CONTROL: NO LOOT QUANTITY PLACES NOTHING.
	const FVector Corpse(400.0f, 200.0f, 0.0f);
	TestEqual(TEXT("CONTROL: a Cataclysm Boss with no loot quantity drops no gems"),
			  UCataclysmDropSpawner::SpawnGemsFor(World, 5, 0.0f, 0.0f, Corpse, FRandomStream(46)), 0);

	// A CATACLYSM BOSS AT 1,000 TIMES THE LOOT QUANTITY: about 82 gems expected, so none would be a fault, not luck.
	const int32 Placed = UCataclysmDropSpawner::SpawnGemsFor(World, 5, 0.0f, 100000.0f, Corpse, FRandomStream(46));
	const TArray<ACataclysmDroppedItem*> Found = GemDropsIn(World);
	if (!TestTrue(TEXT("the kill placed gems"), Placed > 0) || !TestEqual(TEXT("each placed gem lies there"),
																			Found.Num(), Placed))
	{
		return false;
	}

	// AT TIER 1, the rarities a Cataclysm Boss's magic find reaches there and no others.
	TArray<float> Shares;
	UCataclysmDropRoll::RarityDistribution(
		Rarities, 1, UCataclysmDropRoll::MagicFindFrom(Drops, UCataclysmDropSpawner::EnemyRarityForStep(Drops, 5)),
		Shares);
	int32 Unreachable = 0;
	int32 Misnamed = 0;
	int32 OnTheCorpse = 0;
	TSet<ECataclysmRarity> Seen;
	for (const ACataclysmDroppedItem* Drop : Found)
	{
		Seen.Add(Drop->GemRarity);
		Unreachable += Shares.IsValidIndex(static_cast<int32>(Drop->GemRarity))
							   && Shares[static_cast<int32>(Drop->GemRarity)] > 0.0f
						   ? 0
						   : 1;
		Misnamed += Drop->DisplayName == UCataclysmGems::FloorNameOf(Drop->Gem, Drop->GemRarity)
						&& Drop->Rarity == Drop->GemRarity && Drop->GemQuantity == 1
						&& UCataclysmDropPickup::NameBorderThicknessOf(*Drop)
							   == UCataclysmDropPickup::NameBorderThicknessFor(Drop->GemRarity)
					? 0
					: 1;
		OnTheCorpse += FVector::Dist2D(Drop->GetActorLocation(), Corpse) < 74.0f ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("GEMS ON THE FLOOR: %d placed, %d rarities seen, first '%s'"), Placed, Seen.Num(),
							*Found[0]->DisplayName));
	TestEqual(TEXT("no gem has a rarity tier 1 does not reach"), Unreachable, 0);
	TestEqual(TEXT("each is one gem, named and bordered by its rarity"), Misnamed, 0);
	TestEqual(TEXT("none lies on the corpse, where a lone item would"), OnTheCorpse, 0);

	// TAKEN WITHOUT A CLICK, into the stack of its gem and rarity.
	ACataclysmDroppedItem* First = Found[0];
	const FName Gem = First->Gem;
	const ECataclysmRarity Rarity = First->GemRarity;
	TestTrue(TEXT("a gem is a quantity"), First->IsAQuantity());
	TestTrue(TEXT("and comes without a click from beside it"),
			 UCataclysmDropPickup::ComesAutomatically(First->IsAQuantity(), First->GetActorLocation(),
													 First->GetActorLocation(), First->bInfested));
	UCataclysmInventoryComponent* Bag = NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestTrue(TEXT("it is taken"), UCataclysmDropPickup::TakeInto(Bag, First)))
	{
		return false;
	}
	TestEqual(TEXT("into a stack of its gem and rarity"), Bag->CountOfGems(Gem, Rarity), 1);
	TestFalse(TEXT("and it is gone from the floor"), IsValid(First));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GD4. A gem drop is saved and restored as one, and a gem stack dropped from the bag lies whole on the floor.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemDropsSaveTest,
	"Cataclysm.GemDrops.AGemDropIsSavedAndRestoredAndADroppedStackLiesWholeOnTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemDropsSaveTest::RunTest(const FString&)
{
	using namespace CataclysmGemDropsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDroppedItem* Lying = UCataclysmDropSpawner::PutOnTheFloor(
		World, FVector(300.0f, 100.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 0, Life, ECataclysmRarity::Superb, 3);
	if (!TestNotNull(TEXT("set-up: three Superb Of Life on the floor"), Lying))
	{
		return false;
	}
	const FCataclysmSavedGroundItem Saved = FCataclysmSaveGather::GroundItemFrom(*Lying);
	Lying->Destroy();
	TestTrue(TEXT("the record holds the gem"), Saved.Gem == Life);
	TestTrue(TEXT("its rarity"), Saved.GemRarity == ECataclysmRarity::Superb);
	TestEqual(TEXT("and how many"), Saved.GemQuantity, 3);

	const ACataclysmDroppedItem* Restored = FCataclysmSaveApply::GroundItemInto(*World, Saved);
	if (!TestNotNull(TEXT("the record puts it back"), Restored))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("GEM GROUND: restored '%s', %d of them, a gem %s"), *Restored->DisplayName,
							Restored->GemQuantity, Restored->IsGem() ? TEXT("yes") : TEXT("no")));
	TestTrue(TEXT("as gems"), Restored->IsGem());
	TestEqual(TEXT("three of them"), Restored->GemQuantity, 3);
	TestEqual(TEXT("named as a spawned one is"), Restored->DisplayName, FString(TEXT("Of Life (Superb)")));

	// A STACK DROPPED FROM THE BAG LIES WHOLE, as one gem drop, and leaves the bag.
	UCataclysmInventoryComponent* Bag = NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestEqual(TEXT("set-up: three Superb Of The Viper in the bag"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 3),
				   0))
	{
		return false;
	}
	const FVector At(-500.0f, 0.0f, 0.0f);
	TestTrue(TEXT("the stack is dropped"),
			 UCataclysmWearing::DropCarried(Bag, 0, World, At) == ECataclysmWearResult::Dropped);
	TestEqual(TEXT("and has left the bag"), Bag->CountOfGems(Viper, ECataclysmRarity::Superb), 0);
	int32 ViperDrops = 0;
	for (const ACataclysmDroppedItem* Drop : GemDropsIn(World))
	{
		if (Drop->Gem == Viper && FVector::Dist2D(Drop->GetActorLocation(), At) < 1.0f)
		{
			++ViperDrops;
			TestEqual(TEXT("the three lie as one drop"), Drop->GemQuantity, 3);
			TestTrue(TEXT("at their rarity"), Drop->GemRarity == ECataclysmRarity::Superb);
		}
	}
	TestEqual(TEXT("one drop of them lies where it was put"), ViperDrops, 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
