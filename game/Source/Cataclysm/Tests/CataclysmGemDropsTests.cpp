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
 * `sim/analyse_gem_drops.py`'s: the gear column times 0.0136 at tier 8 (0.0068 of the gear column before it halved,
 * issue #2853), and three quarters of the tier above's below it, ruled by the owner on 2026-11-24 (issue #2853).
 *
 * A WORLD BUILT BY A TEST HAS NO GAME MODE, so the difficulty tier is 1 there. The names are read from the generated
 * `DT_Gems`, the rates from `DT_EnemyDrops`.
 */
namespace CataclysmGemDropsTest
{
	const FName Viper(TEXT("Gem_Of_The_Viper"));
	const FName Life(TEXT("Gem_Of_Life"));

	/** The share of a gear drop a gem is at tier 8, as `loot.GEM_DROPS_PER_GEAR_DROP` holds it. */
	constexpr float GemsPerGearDrop = 0.0136f;

	/** The share of tier 8's gems a tier 1 kill drops, `loot.gem_drop_share(1)`: 0.75 to the 7th, 2187 / 16384. */
	constexpr float TierOneShare = 0.13348388671875f;

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

/**
 * At difficulty tier 8, whose share of the column is all of it (issue #2853): each rung's gems are its gear drops times
 * `GemsPerGearDrop`, each rung more than the one below, and twice the loot quantity doubles them.
 */
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
		const float Gems = UCataclysmDropRoll::ExpectedGemDrops(Drops, Rarity, 100.0f, 8);
		const float Gear = UCataclysmDropRoll::ExpectedGearDrops(Drops, Rarity, 100.0f);
		Table += FString::Printf(TEXT(" %s %.6f (gear %.2f);"), *Rarity.ToString(), Gems, Gear);

		TestEqual(FString::Printf(TEXT("a %s drops the gear column times %.4f in gems"), *Rarity.ToString(),
								  GemsPerGearDrop),
				  Gems, Gear * GemsPerGearDrop, 1e-6f);
		TestTrue(FString::Printf(TEXT("a %s drops more gems than the rung below"), *Rarity.ToString()), Gems > Below);
		TestEqual(FString::Printf(TEXT("twice the loot quantity doubles a %s's gems"), *Rarity.ToString()),
				  UCataclysmDropRoll::ExpectedGemDrops(Drops, Rarity, 200.0f, 8), 2.0f * Gems, 1e-6f);
		Below = Gems;
	}
	AddInfo(FString::Printf(TEXT("GEM DROPS:%s"), *Table));

	TestEqual(TEXT("a Cataclysm Boss drops 0.0816 gems at tier 8, the measurement's 6 x 0.0136, as it did before the gear "
				   "column halved"),
			  UCataclysmDropRoll::ExpectedGemDrops(Drops, UCataclysmDropSpawner::EnemyRarityForStep(Drops, 5), 100.0f, 8),
			  0.0816f, 1e-6f);
	TestEqual(TEXT("no loot quantity, no gems"),
			  UCataclysmDropRoll::ExpectedGemDrops(Drops, UCataclysmDropSpawner::EnemyRarityForStep(Drops, 5), 0.0f, 8),
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
	// THE GEMS THE SHEET HOLDS, HOWEVER MANY (28 on 2026-11-27): the count is no longer pinned, since it only restated
	// the sheet (issue #2928). Every one of them is drawn below.
	TestTrue(TEXT("CONTROL: the Gems sheet has gems"), Rows.Num() > 0);

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

	// A CATACLYSM BOSS AT 1,000 TIMES THE LOOT QUANTITY: about 11 gems expected at tier 1's share (82 at tier 8's), so
	// none would be a fault, not luck.
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
// GD4. A gem drop is saved and restored as one, and a carried gem stack cannot be dropped from a carried slot. Until
// issue #2849 a stack dropped from the bag lay whole on the floor; since then the stacks are the gems tab, not a slot,
// and there is deliberately no way to drop one (ruled 2026-11-22: it would be collected straight back within 15 m).
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemDropsSaveTest,
	"Cataclysm.GemDrops.AGemDropIsSavedAndRestoredAndACarriedStackCannotBeDropped",
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

	// A CARRIED STACK IS IN NO SLOT, so slot 0, where it went until #2849, holds nothing to drop, and it stays carried.
	UCataclysmInventoryComponent* Bag = NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestTrue(TEXT("set-up: three Superb Of The Viper in the bag"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 3)))
	{
		return false;
	}
	const FVector At(-500.0f, 0.0f, 0.0f);
	TestTrue(TEXT("slot 0 holds nothing to drop"),
			 UCataclysmWearing::DropCarried(Bag, 0, World, At) == ECataclysmWearResult::NothingToDrop);
	TestEqual(TEXT("and all three are still carried"), Bag->CountOfGems(Viper, ECataclysmRarity::Superb), 3);
	int32 ViperDrops = 0;
	int32 LifeDrops = 0;
	for (const ACataclysmDroppedItem* Drop : GemDropsIn(World))
	{
		ViperDrops += Drop->Gem == Viper ? 1 : 0;
		LifeDrops += Drop->Gem == Life ? 1 : 0;
	}
	TestEqual(TEXT("no Of The Viper lies on the floor"), ViperDrops, 0);
	TestEqual(TEXT("CONTROL: the restored Of Life is found on it, so the search can see a gem drop"), LifeDrops, 1);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GD5. A tier 1 kill drops three quarters to the seventh of tier 8's gems: about an eighth. Issue #2853.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemDropsTierShareTest,
	"Cataclysm.GemDrops.ATierOneKillDropsAboutAnEighthOfTierEightsGems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The owner's ruling of 2026-11-24 (issue #2853), "Milder: tier 1 gets 1/8 (Recommended)", with three quarters a tier
 * as the judgement: the share of the Gem Drops column a kill drops is 1 at tier 8 and three quarters of the tier above's
 * below it, so 2187 / 16384 (13.3%) at tier 1, `loot.gem_drop_share`. Read three ways: the share at every tier; the
 * expected gems of every rung at tier 1 against tier 8; and the gems twenty Cataclysm Bosses actually lay on the floor
 * of a test world, whose tier is 1, at 1,000 times the loot quantity: about 218 expected, against about 1,632 if the
 * spawner read tier 8's rate. A tier outside 1 to 8 is read as the nearest. Prints "GEM TIER SHARE".
 */
bool FCataclysmGemDropsTierShareTest::RunTest(const FString&)
{
	using namespace CataclysmGemDropsTest;
	const UDataTable* Drops = UCataclysmDropRoll::LoadEnemyDropTable();
	if (!TestNotNull(TEXT("set-up: the enemy drop table"), Drops))
	{
		return false;
	}

	// THE SHARE AT EVERY TIER, three quarters of the tier above's.
	TArray<FString> Shares;
	float Expected = 1.0f;
	for (int32 Tier = 8; Tier >= 1; --Tier)
	{
		const float Share = UCataclysmDropRoll::GemDropShareAt(Tier);
		Shares.Add(FString::Printf(TEXT("tier %d %.6f"), Tier, Share));
		TestTrue(*FString::Printf(TEXT("**tier %d drops %.6f of tier 8's gems, exactly**"), Tier, Expected),
				 Share == Expected);
		Expected *= 0.75f;
	}
	TestTrue(TEXT("**tier 1's share is 2187 / 16384, about an eighth**"),
			 UCataclysmDropRoll::GemDropShareAt(1) == TierOneShare && TierOneShare >= 0.125f && TierOneShare < 1.0f / 7.0f);
	TestTrue(TEXT("a tier below 1 is read as tier 1"), UCataclysmDropRoll::GemDropShareAt(0) == TierOneShare);
	TestTrue(TEXT("a tier above 8 is read as tier 8"), UCataclysmDropRoll::GemDropShareAt(9) == 1.0f);

	// EVERY RUNG'S EXPECTED GEMS AT TIER 1, the column times the share.
	for (int32 Step = 0; Step <= 5; ++Step)
	{
		const FName Rarity = UCataclysmDropSpawner::EnemyRarityForStep(Drops, Step);
		const float AtEight = UCataclysmDropRoll::ExpectedGemDrops(Drops, Rarity, 100.0f, 8);
		TestEqual(*FString::Printf(TEXT("**a %s at tier 1 drops tier 8's gems times 2187 / 16384**"), *Rarity.ToString()),
				  UCataclysmDropRoll::ExpectedGemDrops(Drops, Rarity, 100.0f, 1), AtEight * TierOneShare, 1e-7f);
	}

	// THE SPAWNER READS THE TIER BEING PLAYED: twenty Cataclysm Bosses in a test world, tier 1, at 1,000 times the loot
	// quantity, each from a stream of its own.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	constexpr int32 Kills = 20;
	constexpr float LootQuantity = 100000.0f;
	int32 Placed = 0;
	for (int32 Kill = 0; Kill < Kills; ++Kill)
	{
		Placed += UCataclysmDropSpawner::SpawnGemsFor(World, 5, 0.0f, LootQuantity,
													  FVector(600.0f * static_cast<float>(Kill), 0.0f, 0.0f),
													  FRandomStream(2853 + Kill));
	}
	const float AtTierOne = Kills * UCataclysmDropRoll::ExpectedGemDrops(
		Drops, UCataclysmDropSpawner::EnemyRarityForStep(Drops, 5), LootQuantity, 1);
	const float AtTierEight = Kills * UCataclysmDropRoll::ExpectedGemDrops(
		Drops, UCataclysmDropSpawner::EnemyRarityForStep(Drops, 5), LootQuantity, 8);
	AddInfo(FString::Printf(TEXT("GEM TIER SHARE: %s; %d Cataclysm Bosses placed %d gems, %.1f expected at tier 1's share "
								 "and %.1f at tier 8's"),
							*FString::Join(Shares, TEXT(", ")), Kills, Placed, AtTierOne, AtTierEight));
	TestTrue(TEXT("set-up: tier 1's expectation is about 218"), AtTierOne > 200.0f && AtTierOne < 236.0f);
	// A QUARTER EITHER SIDE: about 3.7 standard deviations of a Poisson count of 218, and nowhere near 1,632.
	TestTrue(TEXT("**the gems placed are tier 1's share of the column, not tier 8's**"),
			 Placed > 0.75f * AtTierOne && Placed < 1.25f * AtTierOne);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
