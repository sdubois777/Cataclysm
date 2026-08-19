// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "CataclysmTestWorld.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for what ends up on the dungeon floor when something dies.
 *
 * WHAT CANNOT BE TESTED HERE, and it is the same wall every drawing test in
 * this project meets: the automation command runs with -nullrhi and
 * AHUD::PostRender checks FApp::CanEverRender() before DrawHUD is called at
 * all, so nothing here can watch a name reach the screen. What is tested is
 * every decision that leads up to the drawing -- where a drop lands, what it is
 * called, and what colour it is called in -- which is why those live on
 * UCataclysmDropSpawner as static functions rather than inside the draw.
 */

namespace CataclysmDroppedItemTest
{
	using FSpawner = UCataclysmDropSpawner;

	template <typename RowType>
	UDataTable* LoadTable(const TCHAR* FileName)
	{
		FString Contents;
		const FString Path = FPaths::ProjectDir() / TEXT("Data") / FileName;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			return nullptr;
		}

		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = RowType::StaticStruct();
		if (Table->CreateTableFromCSVString(Contents).Num() > 0)
		{
			return nullptr;
		}
		return Table;
	}
}

// ---------------------------------------------------------------------------
// Where several drops from one kill land
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropScatterTest,
	"Cataclysm.Drop.SeveralDropsFromOneKillDoNotLandOnOneSpot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropScatterTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDroppedItemTest;

	// ONE DROP LANDS WHERE THE KILL HAPPENED. Offsetting a single drop would
	// move it off the corpse for no reason.
	TestEqual(TEXT("one drop lands on the corpse"),
		FSpawner::ScatterOffset(0, 1), FVector::ZeroVector);

	// A CATACLYSM BOSS DROPS TWELVE, and twelve names drawn at one position
	// would be one unreadable stack. This is the case the spread exists for.
	constexpr int32 Twelve = 12;
	TArray<FVector> Places;
	for (int32 Index = 0; Index < Twelve; ++Index)
	{
		Places.Add(FSpawner::ScatterOffset(Index, Twelve));
	}

	for (int32 Left = 0; Left < Twelve; ++Left)
	{
		for (int32 Right = Left + 1; Right < Twelve; ++Right)
		{
			if (Places[Left].Equals(Places[Right], 1.0f))
			{
				AddError(FString::Printf(
					TEXT("drops %d and %d of %d land within a centimetre of "
						 "each other"), Left, Right, Twelve));
				return false;
			}
		}
	}

	// FLAT, WITH NO HEIGHT. Nothing here knows where the floor is, so raising a
	// drop would put it at a height nobody measured.
	for (const FVector& Place : Places)
	{
		TestEqual(TEXT("a drop is not moved up or down"), Place.Z, 0.0);
	}

	// THE RADIUS GROWS WITH THE COUNT, so a big drop does not crowd itself into
	// the same circle a small one uses.
	TestTrue(TEXT("twelve drops spread wider than three"),
		FSpawner::ScatterOffset(0, Twelve).Size()
			> FSpawner::ScatterOffset(0, 3).Size());

	return true;
}

// ---------------------------------------------------------------------------
// Joining a creature's rarity to what it drops
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropRarityJoinTest,
	"Cataclysm.Drop.ACreaturesRarityStepFindsItsDropRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropRarityJoinTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDroppedItemTest;

	UDataTable* Drops = LoadTable<FCataclysmEnemyDropRow>(TEXT("EnemyDrops.csv"));
	if (!TestNotNull(TEXT("EnemyDrops.csv loads"), Drops))
	{
		return false;
	}

	// A CREATURE KNOWS ITS RARITY AS A STEP FROM 0 TO 5 and the drop table is
	// keyed on the rarity's name, so something has to join them. Reading the
	// table is what avoids a second copy of the ladder in the character class.
	const TCHAR* Expected[] = { TEXT("Common"), TEXT("Elite"),
								TEXT("Legendary"), TEXT("Herald"),
								TEXT("Boss"), TEXT("Cataclysm_Boss") };

	for (int32 Step = 0; Step < UE_ARRAY_COUNT(Expected); ++Step)
	{
		TestEqual(*FString::Printf(TEXT("step %d is %s"), Step, Expected[Step]),
			FSpawner::EnemyRarityForStep(Drops, Step), FName(Expected[Step]));
	}

	// A STEP NOTHING HAS DROPS NOTHING rather than picking an arbitrary row. A
	// creature whose rarity is not in the table is a fault to report, not a
	// creature to quietly give Common loot to.
	TestTrue(TEXT("a step no rarity has finds nothing"),
		FSpawner::EnemyRarityForStep(Drops, 99).IsNone());
	TestTrue(TEXT("and neither does a negative one"),
		FSpawner::EnemyRarityForStep(Drops, -1).IsNone());

	return true;
}

// ---------------------------------------------------------------------------
// What colour a name is drawn in
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropColourTest,
	"Cataclysm.Drop.EveryRarityIsDrawnInItsOwnColour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropColourTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDroppedItemTest;

	UDataTable* Rarities =
		LoadTable<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
	if (!TestNotNull(TEXT("GearRarity.csv loads"), Rarities))
	{
		return false;
	}

	const ECataclysmRarity Ladder[] = {
		ECataclysmRarity::Everyday,	   ECataclysmRarity::Quality,
		ECataclysmRarity::Superb,	   ECataclysmRarity::Masterful,
		ECataclysmRarity::Legendary,   ECataclysmRarity::Mythical,
		ECataclysmRarity::Ascendant,   ECataclysmRarity::Cataclysmic,
	};

	// A PLAYER READS A RARITY OFF THE FLOOR BY THIS COLOUR AND NOTHING ELSE,
	// because a drop is shown as its name rather than as a model. Two rarities
	// sharing a colour would be two things that cannot be told apart.
	TArray<FLinearColor> Seen;
	for (ECataclysmRarity Rarity : Ladder)
	{
		const FLinearColor Colour = FSpawner::ColourFor(Rarities, Rarity);
		for (const FLinearColor& Already : Seen)
		{
			if (Colour.Equals(Already, 0.001f))
			{
				AddError(FString::Printf(
					TEXT("%s is drawn in a colour another rarity already uses"),
					*UCataclysmDropRoll::RowNameFor(Rarity).ToString()));
				return false;
			}
		}
		Seen.Add(Colour);
	}

	// EVERYDAY IS WHITE, which is the one value stated plainly enough in the
	// design document to pin here without restating the whole table.
	TestTrue(TEXT("an Everyday item is drawn white"),
		FSpawner::ColourFor(Rarities, ECataclysmRarity::Everyday)
			.Equals(FLinearColor::White, 0.001f));

	// A MISSING TABLE DRAWS WHITE rather than black or transparent, so a data
	// fault leaves a name that can still be read.
	TestTrue(TEXT("no table still gives a readable colour"),
		FSpawner::ColourFor(nullptr, ECataclysmRarity::Cataclysmic)
			.Equals(FLinearColor::White, 0.001f));

	return true;
}

// ---------------------------------------------------------------------------
// A kill really puts items on the floor
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDropsReachTheFloorTest,
	"Cataclysm.Drop.AKillPutsNamedItemsOnTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDropsReachTheFloorTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A BOSS, BECAUSE ITS RATE IS A WHOLE NUMBER. Its five drops are certain,
	// so this test does not depend on a fractional roll going one way. Step 4
	// is Boss; a Common enemy at 0.16 would drop nothing five times in six.
	constexpr int32 BossStep = 4;
	constexpr int32 BossDrops = 5;

	FRandomStream Stream(/*InSeed=*/20260818);
	const FVector Where(300.0f, -200.0f, 90.0f);

	const int32 Spawned = UCataclysmDropSpawner::SpawnDropsFor(
		World, BossStep, /*MagicFind=*/0.0f,
		UCataclysmDropRoll::BaselineLootQuantity, Where, Stream);

	if (!TestEqual(TEXT("a Boss put five items on the floor"), Spawned,
				   BossDrops))
	{
		return false;
	}

	int32 Found = 0;

	// EVERY PLACE A DROP SHOULD BE, built from the same function the spawner
	// uses. Each drop found takes one entry off this list, so the list being
	// empty at the end means every position was filled exactly once.
	TArray<FVector> Expected;
	for (int32 Index = 0; Index < BossDrops; ++Index)
	{
		Expected.Add(Where + UCataclysmDropSpawner::ScatterOffset(Index, BossDrops));
	}

	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		const ACataclysmDroppedItem* Drop = *It;
		++Found;

		// EVERY DROP HAS A NAME, which is the whole of what a player sees. An
		// item that rolled to an empty name is a drop nobody could identify.
		if (Drop->DisplayName.IsEmpty())
		{
			AddError(TEXT("an item reached the floor with no name"));
			return false;
		}

		// AND IT IS A WHOLE ITEM rather than an empty one.
		if (Drop->Item.Base.IsNone())
		{
			AddError(FString::Printf(TEXT("'%s' has no base"),
									 *Drop->DisplayName));
			return false;
		}
		ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
		if (!UCataclysmItemValues::RarityOf(Drop->Item.EnchantmentCount,
											Drop->Item.Affixes.Num(), Rarity))
		{
			AddError(FString::Printf(
				TEXT("'%s' has %d affixes and %d enchantments, which is no "
					 "rarity"), *Drop->DisplayName, Drop->Item.Affixes.Num(),
				Drop->Item.EnchantmentCount));
			return false;
		}

		// THE NAME BEGINS WITH THE RARITY, so the text and the colour say the
		// same thing.
		if (!Drop->DisplayName.StartsWith(
				UCataclysmItemName::RarityWord(Rarity)))
		{
			AddError(FString::Printf(TEXT("'%s' does not begin with its rarity"),
									 *Drop->DisplayName));
			return false;
		}

		// AND IT CARRIES RESIDUE, which every drop does from the moment it
		// falls. Zero would mean the roll never reached it.
		if (Drop->Item.Residue <= 0.0f)
		{
			AddError(FString::Printf(TEXT("'%s' carries no residue"),
									 *Drop->DisplayName));
			return false;
		}

		// IT LANDED ON ONE OF THE PLACES THE SCATTER PUTS A DROP, and on a
		// place no other drop has already taken.
		//
		// THE TOLERANCE USED TO BE 1000 cm AND THE CHECK COULD NOT FAIL. `Where`
		// is 360 cm from the world origin, so a drop left sitting at (0,0,0) was
		// 360 cm away and satisfied a check whose own comment said "not at the
		// world origin". Every drop was in fact at the origin, because
		// ACataclysmDroppedItem created no root component, and an actor without
		// one cannot be positioned at all: SpawnActor's location is discarded
		// and GetActorLocation answers zero. The project owner saw it as every
		// item from every kill piled in the middle of the room.
		const int32 Match = Expected.IndexOfByPredicate(
			[Drop](const FVector& Candidate)
			{
				return Drop->GetActorLocation().Equals(Candidate, 1.0f);
			});

		if (Match == INDEX_NONE)
		{
			AddError(FString::Printf(
				TEXT("'%s' landed at %s, which is not the kill at %s plus any "
					 "of the %d scatter offsets, or is where another drop "
					 "already lies"),
				*Drop->DisplayName, *Drop->GetActorLocation().ToCompactString(),
				*Where.ToCompactString(), Expected.Num()));
			return false;
		}

		// TAKEN, so two drops on one spot cannot both match it.
		Expected.RemoveAt(Match);
	}

	TestEqual(TEXT("and every one of them is in the world"), Found, BossDrops);

	// AND NO POSITION WAS LEFT UNFILLED, which is the other half of "no two
	// drops share a spot": five drops covering four positions leaves one here.
	TestEqual(TEXT("every scatter position got exactly one drop"),
		Expected.Num(), 0);

	// A CREATURE WHOSE RARITY IS NOT IN THE TABLE DROPS NOTHING, rather than
	// dropping Common loot or crashing.
	const int32 None = UCataclysmDropSpawner::SpawnDropsFor(
		World, /*EnemyRarityStep=*/99, 0.0f,
		UCataclysmDropRoll::BaselineLootQuantity, Where, Stream);
	TestEqual(TEXT("an unknown rarity drops nothing"), None, 0);

	return true;
}

// ---------------------------------------------------------------------------
// The death itself is what drops them
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillingAnEnemyDropsLootTest,
	"Cataclysm.Drop.KillingAnEnemyIsWhatPutsLootOnTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmKillingAnEnemyDropsLootTest::RunTest(const FString& Parameters)
{
	// WHY THIS EXISTS SEPARATELY FROM THE TEST ABOVE. That one calls
	// SpawnDropsFor directly, so it proves the roll and the spawn work and says
	// nothing about whether anything ever calls them. This kills a creature and
	// looks at the floor, which is the claim a player cares about.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// NOTHING IS ON THE FLOOR BEFORE THE KILL. Without this the test could pass
	// on drops left by something else and never notice the hook was missing.
	int32 Before = 0;
	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		++Before;
	}
	TestEqual(TEXT("the floor is empty to begin with"), Before, 0);

	ACataclysmEnemyCharacter* Victim =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(500.0f, 500.0f, 100.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature to kill"), Victim))
	{
		return false;
	}
	Victim->SetHealth(1000.0f);

	// A BOSS, BECAUSE ITS DROP RATE IS A WHOLE NUMBER. Five items are certain,
	// so this does not depend on a fractional roll landing one way. A Common
	// enemy drops nothing five times in six, which would make the test flap.
	constexpr int32 BossStep = 4;
	Victim->SetRarityStep(BossStep);

	Victim->HandleDeath();

	int32 After = 0;
	FString AnyName;
	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		++After;
		if (AnyName.IsEmpty())
		{
			AnyName = It->DisplayName;
		}
	}

	TestEqual(TEXT("killing a Boss left five items on the floor"), After, 5);
	TestFalse(TEXT("and they have names a player could read"),
			  AnyName.IsEmpty());

	// KILLING IT TWICE DROPS NOTHING MORE. HandleDeath returns early on a
	// creature already marked dead, and it would otherwise be reachable twice:
	// the comment on MarkDead says nothing there is safe to run twice.
	Victim->HandleDeath();

	int32 Again = 0;
	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		++Again;
	}
	TestEqual(TEXT("and a second death drops nothing more"), Again, After);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
