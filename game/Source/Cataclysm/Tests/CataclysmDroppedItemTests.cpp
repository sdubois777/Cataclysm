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
#include "Items/CataclysmInventoryComponent.h"
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

	// QUALITY IS WHITE, which is the one value stated plainly enough in the
	// design document to pin here without restating the whole table.
	//
	// IT WAS EVERYDAY UNTIL 2026-08-19. The two lowest rungs were swapped under
	// issue #711, because every game of this kind uses grey for the worthless
	// tier and white for the ordinary one, and the old order read backwards to
	// anyone arriving from one. This test still pinned the old answer after the
	// swap landed, and continuous integration compiles no C++, so nothing on the
	// pull request noticed.
	TestTrue(TEXT("a Quality item is drawn white"),
		FSpawner::ColourFor(Rarities, ECataclysmRarity::Quality)
			.Equals(FLinearColor::White, 0.001f));

	// AND EVERYDAY IS NOT, which is the half that would have caught the swap
	// going missing rather than the swap arriving.
	TestFalse(TEXT("an Everyday item is not drawn white"),
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
	using namespace CataclysmDroppedItemTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A BOSS, BECAUSE ITS RATE OF 5 MAKES AN EMPTY KILL VANISHINGLY RARE. Step 4
	// is Boss; a Common enemy at 0.16 would give nothing on most seeds.
	//
	// HOW MANY IS NOT ASSERTED, AND USED TO BE. Until issue #725 the count was
	// the whole part of the rate plus a fractional chance, so a Boss dropped
	// exactly five every time and this test could say so. The count is drawn
	// from a Poisson distribution now, so what is checked is that the number
	// spawned matches the number of actors that appeared and that each one
	// landed on its own scatter position -- neither of which depends on how many
	// there are. Cataclysm.Drop.DropCountsMatchTheModel is what checks the
	// distribution itself.
	constexpr int32 BossStep = 4;

	// LOADED SO EACH DROP'S COLOUR CAN BE CHECKED AGAINST ITS RARITY further
	// down. The generated table, not a fixture, because the point is that the
	// spawner asked the real one.
	UDataTable* Rarities =
		LoadTable<FCataclysmGearRarityRow>(TEXT("GearRarity.csv"));
	if (!TestNotNull(TEXT("GearRarity.csv loads"), Rarities))
	{
		return false;
	}

	FRandomStream Stream(/*InSeed=*/20260818);
	const FVector Where(300.0f, -200.0f, 90.0f);

	const int32 BossDrops = UCataclysmDropSpawner::SpawnDropsFor(
		World, BossStep, /*MagicFind=*/0.0f,
		UCataclysmDropRoll::BaselineLootQuantity, Where, Stream);

	if (!TestTrue(TEXT("a Boss put at least one item on the floor"),
				  BossDrops > 0))
	{
		return false;
	}

	int32 Found = 0;

	// WHERE EACH DROP LANDED, so no two can be found on the same spot.
	TArray<FVector> Landed;

	// THE CIRCLE THEY ALL SIT ON. ScatterOffset spreads N drops around a circle
	// of 25 cm per drop, so every drop from one kill is exactly that far from
	// where the creature died.
	//
	// THE RADIUS IS COMPUTED FROM WHAT WAS SPAWNED rather than predicted,
	// because a kill drops gear and crafting materials on one circle and this
	// test cannot know how many of each were rolled. What it can check is the
	// property: one circle, the right size, with nothing doubled up.
	const double Radius = 25.0 * static_cast<double>(BossDrops);

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

		// WHERE IT LANDED IS CHECKED FOR BOTH KINDS, before anything below
		// that is about gear only. A kill scatters its gear and its crafting
		// materials around one circle, and every one of them has to be on it.
		//
		// THE TOLERANCE USED TO BE 1000 cm AND THE CHECK COULD NOT FAIL. `Where`
		// is 360 cm from the world origin, so a drop left sitting at (0,0,0) was
		// 360 cm away and satisfied a check whose own comment said "not at the
		// world origin". Every drop was in fact at the origin, because
		// ACataclysmDroppedItem created no root component, and an actor without
		// one cannot be positioned at all: SpawnActor's location is discarded
		// and GetActorLocation answers zero. The project owner saw it as every
		// item from every kill piled in the middle of the room.
		const FVector Landing = Drop->GetActorLocation();

		// ON THE CIRCLE, AT THE RIGHT DISTANCE FROM THE KILL.
		const double Away = FVector::Dist2D(Landing, Where);
		if (FMath::Abs(Away - Radius) > 1.0)
		{
			AddError(FString::Printf(
				TEXT("'%s' landed %.1f cm from the kill at %s and the scatter "
					 "circle for %d drops has a radius of %.1f"),
				*Drop->DisplayName, Away, *Where.ToCompactString(), BossDrops,
				Radius));
			return false;
		}

		// AND AT THE KILL'S OWN HEIGHT, which is what the origin fault broke:
		// every drop sat at Z=0 rather than at the height of the corpse.
		if (!FMath::IsNearlyEqual(Landing.Z, Where.Z, 1.0))
		{
			AddError(FString::Printf(
				TEXT("'%s' landed at height %.1f and the kill was at %.1f"),
				*Drop->DisplayName, Landing.Z, Where.Z));
			return false;
		}

		// AND NOT ON TOP OF ANOTHER ONE. Two drops on one spot draw two names
		// at one place and neither can be read or clicked.
		for (const FVector& Taken : Landed)
		{
			if (FVector::Dist2D(Taken, Landing) < 1.0)
			{
				AddError(FString::Printf(
					TEXT("'%s' landed on top of another drop at %s"),
					*Drop->DisplayName, *Landing.ToCompactString()));
				return false;
			}
		}
		Landed.Add(Landing);

		// A CRAFTING MATERIAL IS NOT A PIECE OF GEAR, and a kill drops both onto
		// the same circle. Everything above this line is true of either kind --
		// where it landed, that it has a name -- and everything below is about
		// gear only. Cataclysm.Drop.AKillPutsCraftingMaterialsOnTheFloorToo is
		// what checks the other kind.
		if (Drop->IsMaterial())
		{
			continue;
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

		// THE DROP REMEMBERS THE RARITY IT ROLLED. The border's thickness is
		// read off this field every frame rather than recomputed, so a spawner
		// that failed to set it would draw every name inside an Everyday border.
		if (Drop->Rarity != Rarity)
		{
			AddError(FString::Printf(
				TEXT("'%s' computes as rarity %d and remembers %d"),
				*Drop->DisplayName, static_cast<int32>(Rarity),
				static_cast<int32>(Drop->Rarity)));
			return false;
		}

		// AND ITS COLOUR CAME FROM THAT RARITY RATHER THAN FROM ANYWHERE ELSE.
		//
		// NOTHING CHECKED THIS UNTIL ISSUE #718, and it was found by breaking
		// the spawner on purpose: replacing the lookup with a plain white passed
		// all 26 tests in this group. Cataclysm.Drop.EveryRarityIsDrawnInItsOwnColour
		// checks that ColourFor answers a different colour per rarity, and
		// nothing checked that a spawned drop ever asked it. Every item on the
		// floor could have been drawn white and no test would have said so.
		const FLinearColor Wanted = FSpawner::ColourFor(Rarities, Rarity);
		if (!Drop->NameColour.Equals(Wanted, 0.001f))
		{
			AddError(FString::Printf(
				TEXT("'%s' is drawn %s and its rarity's colour is %s"),
				*Drop->DisplayName, *Drop->NameColour.ToString(),
				*Wanted.ToString()));
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

	}

	// THE SPAWNER'S ANSWER AND THE WORLD AGREE. That is the part that
	// mattered here all along: a spawner reporting more than it made would
	// be invisible to a test that only counted actors.
	TestEqual(TEXT("every item the spawner reported is in the world"),
		Found, BossDrops);

	// AND EVERY ONE OF THEM WAS CHECKED, which is the other half: a loop that
	// found nothing would pass every assertion inside it.
	TestEqual(TEXT("every drop the spawner made was examined"),
		Landed.Num(), BossDrops);

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

	// TWO MORE KILLS, AND THE TEST IS ABOUT ALL THREE TOGETHER.
	//
	// ONE KILL IS NOT ENOUGH ANY MORE. Since issue #725 the count is a Poisson
	// draw, so a Boss drops nothing about 7 kills in a thousand, and
	// ACataclysmEnemyCharacter::HandleDeath seeds its stream from the creature's
	// unique id and the world's clock -- which this test cannot control. A
	// single kill would therefore fail roughly once in every 150 runs, and a
	// test that fails at random is worse than no test. Three kills bring that to
	// about one run in three million.
	//
	// THE FIRST TIME THIS RAN IT FAILED EXACTLY THAT WAY, reporting
	// "killing a Boss left items on the floor (0)".
	for (int32 More = 0; More < 2; ++More)
	{
		ACataclysmEnemyCharacter* Another =
			World->SpawnActor<ACataclysmEnemyCharacter>(
				ACataclysmEnemyCharacter::StaticClass(),
				FVector(300.0f * static_cast<float>(More + 1), 0.0f, 0.0f),
				FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("another Boss to kill"), Another))
		{
			return false;
		}
		Another->SetRarityStep(BossStep);
		Another->HandleDeath();
	}

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

	// HOW MANY IS NOT ASSERTED, only that killing things is what put loot on the
	// floor. Three Boss kills average fifteen items.
	TestTrue(*FString::Printf(
			TEXT("killing three Bosses left items on the floor (%d)"), After),
		After > 0);
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


// ---------------------------------------------------------------------------
// Crafting materials reach the floor too
// ---------------------------------------------------------------------------

/**
 * A kill puts named, coloured crafting materials on the floor beside its gear.
 *
 * WHAT THIS GUARDS. Issue #717: the material roll ran on every kill and its
 * result was thrown away, so a player got the gear half of what a kill is
 * designed to give and silently lost the other half. Materials come at twice the
 * gear rate, so the half that was missing was the larger one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaterialDropsReachTheFloorTest,
	"Cataclysm.Drop.AKillPutsCraftingMaterialsOnTheFloorToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMaterialDropsReachTheFloorTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDroppedItemTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UDataTable* MaterialTiers =
		LoadTable<FCataclysmMaterialTierRow>(TEXT("MaterialTiers.csv"));
	UDataTable* Materials =
		LoadTable<FCataclysmCraftingMaterialRow>(TEXT("CraftingMaterials.csv"));
	if (!TestNotNull(TEXT("MaterialTiers.csv loads"), MaterialTiers)
		|| !TestNotNull(TEXT("CraftingMaterials.csv loads"), Materials))
	{
		return false;
	}

	// A CATACLYSM BOSS, WHICH AVERAGES 24 MATERIALS. Its rate makes an empty
	// material roll vanishingly unlikely at any seed.
	constexpr int32 CataclysmBossStep = 5;

	FRandomStream Stream(/*InSeed=*/20260819);
	const FVector Where(500.0f, 250.0f, 40.0f);

	const int32 Spawned = UCataclysmDropSpawner::SpawnDropsFor(
		World, CataclysmBossStep, /*MagicFind=*/0.0f,
		UCataclysmDropRoll::BaselineLootQuantity, Where, Stream);
	if (!TestTrue(TEXT("the kill dropped something"), Spawned > 0))
	{
		return false;
	}

	int32 FoundMaterials = 0;
	int32 FoundGear = 0;

	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		const ACataclysmDroppedItem* Drop = *It;
		if (!Drop->IsMaterial())
		{
			++FoundGear;
			continue;
		}
		++FoundMaterials;

		// A MATERIAL DROP IS NOT ALSO A GEAR DROP. One actor carries both kinds,
		// so a drop that answered yes to both would be drawn and picked up as
		// whichever the caller asked about first.
		if (!Drop->Item.Base.IsNone())
		{
			AddError(FString::Printf(
				TEXT("'%s' is a material and also carries the gear base '%s'"),
				*Drop->DisplayName, *Drop->Item.Base.ToString()));
			return false;
		}

		// IT IS A REAL MATERIAL FROM THE TABLE, not an invented name.
		const FCataclysmCraftingMaterialRow* Row =
			Materials->FindRow<FCataclysmCraftingMaterialRow>(
				Drop->Material, TEXT("material drop"), false);
		if (!Row)
		{
			AddError(FString::Printf(
				TEXT("'%s' is not a row in CraftingMaterials.csv"),
				*Drop->Material.ToString()));
			return false;
		}

		// AND IT IS A MATERIAL RATHER THAN A CRAFTING ACTION. Nineteen rows of
		// that table are actions such as "Reroll Affix Value", carrying a tier
		// of 0, and nothing should ever drop one.
		if (Row->Tier <= 0)
		{
			AddError(FString::Printf(
				TEXT("'%s' dropped and it is a crafting action, not a material"),
				*Row->MaterialName));
			return false;
		}

		if (Drop->MaterialTier != Row->Tier)
		{
			AddError(FString::Printf(
				TEXT("'%s' is tier %d in the table and the drop says %d"),
				*Row->MaterialName, Row->Tier, Drop->MaterialTier));
			return false;
		}

		// THE PLAYER READS THE MATERIAL'S OWN NAME, not its tier.
		if (Drop->DisplayName != Row->MaterialName)
		{
			AddError(FString::Printf(
				TEXT("a drop of '%s' is labelled '%s'"),
				*Row->MaterialName, *Drop->DisplayName));
			return false;
		}

		// AND IN ITS TIER'S COLOUR, which is what says it is a material at all.
		const FLinearColor Wanted =
			UCataclysmDropRoll::MaterialColourFor(MaterialTiers, Row->Tier);
		if (!Drop->NameColour.Equals(Wanted, 0.001f))
		{
			AddError(FString::Printf(
				TEXT("'%s' is drawn %s and tier %d's colour is %s"),
				*Row->MaterialName, *Drop->NameColour.ToString(), Row->Tier,
				*Wanted.ToString()));
			return false;
		}

		// ONE PER DROP. They stack when they are picked up rather than where
		// they lie, so a player can see how many fell and can leave some.
		if (Drop->MaterialQuantity != 1)
		{
			AddError(FString::Printf(
				TEXT("'%s' lies on the floor as %d at once"),
				*Row->MaterialName, Drop->MaterialQuantity));
			return false;
		}
	}

	TestTrue(*FString::Printf(
			TEXT("a Cataclysm Boss dropped crafting materials (%d)"),
			FoundMaterials),
		FoundMaterials > 0);
	TestTrue(*FString::Printf(TEXT("and gear as well (%d)"), FoundGear),
		FoundGear > 0);
	TestEqual(TEXT("and the spawner counted every actor it made"),
		FoundMaterials + FoundGear, Spawned);

	return true;
}

/**
 * Picking up a material stacks it, and a full bag leaves it on the floor.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaterialPickupTest,
	"Cataclysm.Drop.PickingUpAMaterialStacksItInTheInventory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMaterialPickupTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDroppedItemTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UCataclysmInventoryComponent* Inventory =
		NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("inventory"), Inventory))
	{
		return false;
	}

	const FName Mote(TEXT("Material_Corrupted_Mote"));

	// THREE OF THE SAME MATERIAL LYING SEPARATELY, which is what one kill
	// produces: each roll is its own drop.
	for (int32 Which = 0; Which < 3; ++Which)
	{
		ACataclysmDroppedItem* Drop = World->SpawnActor<ACataclysmDroppedItem>(
			FVector(100.0f * static_cast<float>(Which), 0.0f, 0.0f),
			FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("a material drop"), Drop))
		{
			return false;
		}
		Drop->Material = Mote;
		Drop->MaterialQuantity = 1;
		Drop->MaterialTier = 1;
		Drop->DisplayName = TEXT("Corrupted Mote");

		TestTrue(TEXT("it was taken"),
			UCataclysmDropPickup::TakeInto(Inventory, Drop));
	}

	TestEqual(TEXT("three are carried"), Inventory->CountOfMaterial(Mote), 3);
	TestEqual(TEXT("in one slot"), Inventory->NumItems(), 1);

	// A FULL BAG LEAVES A MATERIAL IT IS NOT CARRYING ON THE FLOOR.
	Inventory->RemoveEverything();
	for (int32 N = 0; N < UCataclysmInventoryComponent::SlotCount; ++N)
	{
		FCataclysmItem Filler;
		Filler.Base = FName(*FString::Printf(TEXT("Filler%d"), N));
		Inventory->AddItem(Filler);
	}

	ACataclysmDroppedItem* Refused = World->SpawnActor<ACataclysmDroppedItem>(
		FVector(700.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a drop for the full bag"), Refused))
	{
		return false;
	}
	Refused->Material = Mote;
	Refused->MaterialQuantity = 1;
	Refused->MaterialTier = 1;
	Refused->DisplayName = TEXT("Corrupted Mote");

	TestFalse(TEXT("a full bag cannot take a new material"),
		UCataclysmDropPickup::TakeInto(Inventory, Refused));
	TestTrue(TEXT("so it stays on the floor"), IsValid(Refused));
	TestEqual(TEXT("and none of it is carried"),
		Inventory->CountOfMaterial(Mote), 0);

	return true;
}

/**
 * A material's border is one pixel a tier, the same rule the gear names follow.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaterialBorderTest,
	"Cataclysm.Drop.AMaterialsNameCarriesItsTierAsABorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMaterialBorderTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDroppedItemTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	TestEqual(TEXT("tier 1 sits inside one pixel"),
		UCataclysmDropPickup::NameBorderThicknessForMaterialTier(1), 1);
	TestEqual(TEXT("and tier 5 inside five"),
		UCataclysmDropPickup::NameBorderThicknessForMaterialTier(5), 5);

	// ONE THICKER A TIER, WITH NO TWO THE SAME.
	int32 Previous = 0;
	for (int32 Tier = 1; Tier <= 5; ++Tier)
	{
		const int32 Thickness =
			UCataclysmDropPickup::NameBorderThicknessForMaterialTier(Tier);
		TestTrue(*FString::Printf(
				TEXT("tier %d is thicker than the tier below (%d after %d)"),
				Tier, Thickness, Previous),
			Thickness > Previous);
		Previous = Thickness;
	}

	// A DROP IS ASKED WHICH KIND IT IS. The heads-up display draws one border
	// for both kinds and has to read the tier for a material and the rarity for
	// gear; getting that backwards would give every material an Everyday border.
	ACataclysmDroppedItem* Material = World->SpawnActor<ACataclysmDroppedItem>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	ACataclysmDroppedItem* Gear = World->SpawnActor<ACataclysmDroppedItem>(
		FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a material drop"), Material)
		|| !TestNotNull(TEXT("a gear drop"), Gear))
	{
		return false;
	}

	Material->Material = FName(TEXT("Material_Purified_Essence"));
	Material->MaterialQuantity = 1;
	Material->MaterialTier = 5;

	Gear->Item.Base = FName(TEXT("Greataxe"));
	Gear->Rarity = ECataclysmRarity::Cataclysmic;

	TestEqual(TEXT("a tier 5 material gets a five pixel border"),
		UCataclysmDropPickup::NameBorderThicknessOf(*Material), 5);
	TestEqual(TEXT("and a Cataclysmic item gets eight"),
		UCataclysmDropPickup::NameBorderThicknessOf(*Gear), 8);

	// A TIER THAT FAILED TO READ STILL GETS A BORDER rather than none, because a
	// missing border would look like a fault in the drawing rather than the data.
	TestEqual(TEXT("tier 0 still gets the thinnest border"),
		UCataclysmDropPickup::NameBorderThicknessForMaterialTier(0), 1);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
