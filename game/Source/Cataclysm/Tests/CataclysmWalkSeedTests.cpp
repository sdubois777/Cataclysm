// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Empire/CataclysmDungeonModifier.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Each run's own mask on its dungeons' walk seeds, so two runs' dungeons differ. Issue #2749, B10, ruled by the
 * coordinating session on 2026-11-18: "two runs' dungeons must differ, because the design's dungeons are procedurally
 * generated and a new character meeting the same dungeon 0 every run is not that".
 *
 * BEFORE IT, a dungeon's walk seed was `max(1, DungeonId + 1)`, identifiers start at 0 in every run, and the owner's
 * log of 2026-11-18 shows dungeon 0's floor 1 roll for roll the same as the free-play floor a dungeon level builds on
 * load, whose seed is 1. `FCataclysmDungeon::WalkSeed` now XORs the identifier plus one with the run's
 * `UCataclysmEmpireRun::WalkSeedMask`; a mask of 0 is the old seed, and `Migrate_7_to_8` gives a run saved before the
 * change exactly that.
 */
namespace CataclysmWalkSeedTest
{
	/** A run begun as a game instance begins one, with its modifier pool, at difficulty tier 1. */
	UCataclysmEmpireRun* BegunWalkSeedRun(int32 Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->ModifierPool = UCataclysmDungeonModifierTable::LoadPool();
		Run->Begin(Seed, ECataclysmSurgeMode::Static, /*LethalityRung=*/0, /*InDifficultyTier=*/1);
		return Run;
	}

	/** The run record `Name` reads into, or null with the reason added as an error. */
	UCataclysmRunSave* ReadWalkSeedFixture(FAutomationTestBase& Test, const TCHAR* Name, FString& OutText)
	{
		FString Reason;
		if (!CataclysmSaveFixtures::Read(Name, OutText, Reason))
		{
			Test.AddError(Reason);
			return nullptr;
		}
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		FString Message;
		UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			OutText, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
		if (Read == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s would not load: %s -- %s"), Name,
										  FCataclysmSaveStorage::Describe(Result), *Message));
		}
		return Read;
	}
}

#define CATACLYSM_WALK_SEED_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

/**
 * The formula: the identifier plus one, XORed with the run's mask, and never below 1. A mask of 0 is the seed before
 * B10; every dungeon of a run with a large mask gets its own; a mask equal to a dungeon's identifier plus one gives that
 * dungeon 1, the one exception the coordinating session ruled to be stated.
 */
CATACLYSM_WALK_SEED_TEST(FCataclysmWalkSeedFormulaTest,
	"Cataclysm.WalkSeed.ADungeonsWalkSeedIsItsIdentifierPlusOneMaskedByItsRun")
{
	FCataclysmDungeon Dungeon;
	Dungeon.DungeonId = 0;
	TestEqual(TEXT("dungeon 0 with no mask walks on seed 1, as before"), Dungeon.WalkSeed(0), 1);
	Dungeon.DungeonId = 4;
	TestEqual(TEXT("dungeon 4 with no mask walks on seed 5, as before"), Dungeon.WalkSeed(0), 5);
	TestEqual(TEXT("dungeon 4 with the mask 0x5A5A walks on 5 XOR 0x5A5A"), Dungeon.WalkSeed(0x5A5A), 5 ^ 0x5A5A);
	TestNotEqual(TEXT("CONTROL: and that is not its seed with no mask"), Dungeon.WalkSeed(0x5A5A), Dungeon.WalkSeed(0));
	TestEqual(TEXT("a mask equal to its identifier plus one gives it 1, never 0"), Dungeon.WalkSeed(5), 1);
	Dungeon.DungeonId = INDEX_NONE;
	TestEqual(TEXT("a dungeon with no identifier and no mask walks on 1, never 0"), Dungeon.WalkSeed(0), 1);

	// EVERY DUNGEON OF A RUN ITS OWN SEED, for a mask no smaller than the run's dungeon count.
	constexpr int32 Mask = 0x05A5A5A5;
	TSet<int32> Seeds;
	for (int32 Id = 0; Id < 200; ++Id)
	{
		Dungeon.DungeonId = Id;
		Seeds.Add(Dungeon.WalkSeed(Mask));
	}
	TestEqual(TEXT("200 dungeons of one run, 200 seeds"), Seeds.Num(), 200);
	return true;
}

/**
 * Two runs begun on different seeds draw different masks, so their dungeon 0 walks different floors; one seed always
 * draws the same mask (CONTROL), so a run's dungeons stay the same when it is begun again from its seed.
 */
CATACLYSM_WALK_SEED_TEST(FCataclysmWalkSeedTwoRunsTest,
	"Cataclysm.WalkSeed.TwoRunsBegunOnDifferentSeedsDrawDifferentMasks")
{
	using namespace CataclysmWalkSeedTest;

	const UCataclysmEmpireRun* First = BegunWalkSeedRun(11);
	const UCataclysmEmpireRun* Second = BegunWalkSeedRun(12);
	const UCataclysmEmpireRun* FirstAgain = BegunWalkSeedRun(11);
	FCataclysmDungeon DungeonZero;
	DungeonZero.DungeonId = 0;

	AddInfo(FString::Printf(TEXT("WALK SEED MASKS: seed 11 draws %d, seed 12 draws %d, seed 11 again %d; dungeon 0 walks "
								 "on %d and %d"),
							First->WalkSeedMask, Second->WalkSeedMask, FirstAgain->WalkSeedMask,
							DungeonZero.WalkSeed(First->WalkSeedMask), DungeonZero.WalkSeed(Second->WalkSeedMask)));

	TestTrue(TEXT("a begun run draws a mask above 0"), First->WalkSeedMask > 0 && Second->WalkSeedMask > 0);
	TestNotEqual(TEXT("two seeds, two masks"), First->WalkSeedMask, Second->WalkSeedMask);
	TestNotEqual(TEXT("so the two runs' dungeon 0 walk different seeds"), DungeonZero.WalkSeed(First->WalkSeedMask),
				 DungeonZero.WalkSeed(Second->WalkSeedMask));
	TestEqual(TEXT("CONTROL: one seed, one mask"), FirstAgain->WalkSeedMask, First->WalkSeedMask);
	return true;
}

/**
 * `Run_v7.json`, written before the mask, migrates to a mask of 0 with every other field kept, and each of its
 * dungeons keeps the walk seed it had: its identifier plus one. So a walk saved at version 7 loads the floors it was on.
 * CONTROL: the committed run file's mask, 12345 -- `Run_v11.json`'s since the run record went to version 11 (issue
 * #2741 slice 5b), `Run_v10.json`'s from version 10 (issue #2725), `Run_v9.json`'s from version 9 (issue #2741 slice
 * 3b), `Run_v8.json`'s before -- gives the same dungeons other seeds. Prints "WALK SEED MIGRATED".
 */
CATACLYSM_WALK_SEED_TEST(FCataclysmWalkSeedMigrationTest,
	"Cataclysm.WalkSeed.ARunFileFromVersion7MigratesToMaskZeroAndKeepsItsWalkSeeds")
{
	using namespace CataclysmWalkSeedTest;

	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v7.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	const TSharedPtr<FJsonObject> Stepped = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Run_v7.json is a JSON object with no mask"),
				  Stepped.IsValid() && !Stepped->HasField(TEXT("WalkSeedMask"))))
	{
		return false;
	}
	FString StepMessage;
	const ECataclysmSaveMigrationResult Stepping = FCataclysmSaveMigration::Migrate(
		GetDefault<UCataclysmRunSave>()->MigrationSteps(), UCataclysmRunSave::SchemaVersionNow, Stepped.ToSharedRef(),
		StepMessage);
	double Written = -1.0;
	const bool bWritten = Stepped->TryGetNumberField(TEXT("WalkSeedMask"), Written);

	FString ReadText;
	const UCataclysmRunSave* Read = ReadWalkSeedFixture(*this, TEXT("Run_v7.json"), ReadText);
	FString CurrentText;
	const UCataclysmRunSave* Current = ReadWalkSeedFixture(*this, TEXT("Run_v11.json"), CurrentText);
	if (Read == nullptr || Current == nullptr)
	{
		return false;
	}

	// EVERY OTHER FIELD KEPT: the version 7 file, at the current version with what the step writes, is what the record
	// writes back.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmRunSave::SchemaVersionNow);
	CataclysmSaveFixtures::AddTheRunsVersion8Defaults(*Expected);
	// AND THE TREE'S EVENT COUNTS AT NOTHING AND A BOUGHT DAY OF 0 ON EVERY UPGRADE, which `Migrate_8_to_9` writes,
	// because the chain runs on to version 9 since 2026-11-19 (issue #2741 slice 3b).
	CataclysmSaveFixtures::AddTheRunsVersion9Defaults(*Expected);
	// AND A `bIsVengefulWraith` OF FALSE ON EVERY SAVED CREATURE, which `Migrate_9_to_10` writes, because
	// the chain runs on to run record version 10 since 2026-11-19 (issue #2725).
	CataclysmSaveFixtures::AddTheRunsVersion10Defaults(*Expected);
	// AND THE CLEARS SINCE THE LAST DEATH `Migrate_10_to_11` WRITES, because the chain runs on to run record
	// version 11 since 2026-11-20 (issue #2741 slice 5b).
	CataclysmSaveFixtures::AddTheRunsVersion11Defaults(*Expected);
	FString Rewritten;
	FString Error;
	FString Where;
	const bool bSame = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error)
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	int32 Kept = 0;
	int32 MovedByTheCurrentFilesMask = 0;
	for (const FCataclysmDungeon& Dungeon : Read->Dungeons)
	{
		Kept += Dungeon.WalkSeed(Read->WalkSeedMask) == Dungeon.DungeonId + 1 ? 1 : 0;
		MovedByTheCurrentFilesMask += Dungeon.WalkSeed(Current->WalkSeedMask) != Dungeon.DungeonId + 1 ? 1 : 0;
	}

	AddInfo(FString::Printf(TEXT("WALK SEED MIGRATED: the step %d (%s); mask written %s %.0f; the record at version %d with "
								 "mask %d; %d of %d dungeons keep their seed; Run_v11.json's mask %d moves %d; the rest %s"),
							static_cast<int32>(Stepping), *StepMessage, bWritten ? TEXT("yes") : TEXT("NO"), Written,
							Read->SchemaVersion, Read->WalkSeedMask, Kept, Read->Dungeons.Num(), Current->WalkSeedMask,
							MovedByTheCurrentFilesMask, bSame ? TEXT("kept") : *Where));

	TestEqual(TEXT("the chain migrates the version 7 file"), static_cast<int32>(Stepping),
			  static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("the step writes a mask of 0"), bWritten && Written == 0.0);
	TestEqual(TEXT("the record reads a mask of 0"), Read->WalkSeedMask, 0);
	TestEqual(TEXT("it arrives at the current version"), Read->SchemaVersion, UCataclysmRunSave::SchemaVersionNow);
	TestTrue(TEXT("and every other field is kept"), bSame);
	if (TestTrue(TEXT("set-up: the file holds a dungeon"), Read->Dungeons.Num() > 0))
	{
		TestEqual(TEXT("every dungeon keeps its version 7 seed, its identifier plus one"), Kept, Read->Dungeons.Num());
		TestEqual(TEXT("CONTROL: the committed run file's mask moves every one"), MovedByTheCurrentFilesMask,
				  Read->Dungeons.Num());
	}
	return true;
}

/**
 * Entering a dungeon of a begun run carves its floors from its masked seed, not its identifier plus one.
 */
CATACLYSM_WALK_SEED_TEST(FCataclysmWalkSeedEnteredTest,
	"Cataclysm.WalkSeed.EnteringADungeonCarvesItsFloorsFromTheRunsMaskedSeed")
{
	using namespace CataclysmWalkSeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// ONE DAY, WHICH IS THE FIRST SURGE, so a dungeon stands on the map: the two lines `CataclysmDungeonGameModeTests.cpp`
	// uses for the same reason.
	UCataclysmEmpireRun* Run = BegunWalkSeedRun(5150);
	Run->AdvanceDay();
	if (!TestTrue(TEXT("set-up: the first surge put a dungeon on the map"), Run->Dungeons.Num() > 0))
	{
		return false;
	}
	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode))
	{
		return false;
	}
	Mode->SetEmpireRunForTests(Run);

	const FCataclysmDungeon Walked = Run->Dungeons[0];
	const bool bEntered = Mode->EnterEmpireDungeon(Walked.DungeonId);

	AddInfo(FString::Printf(TEXT("WALK SEED ENTERED: dungeon %d, mask %d, the game mode's seed %d, its masked seed %d"),
							Walked.DungeonId, Run->WalkSeedMask, Mode->DungeonSeed,
							Walked.WalkSeed(Run->WalkSeedMask)));

	TestTrue(TEXT("set-up: the dungeon was entered"), bEntered);
	TestNotEqual(TEXT("CONTROL: the run's masked seed is not the identifier plus one"),
				 Walked.WalkSeed(Run->WalkSeedMask), Walked.DungeonId + 1);
	TestEqual(TEXT("the floors are carved from the run's masked seed"), Mode->DungeonSeed,
			  Walked.WalkSeed(Run->WalkSeedMask));
	return true;
}

/**
 * The Corrupted Stalker is rolled on each dungeon's masked seed as the run makes it: across 200 dungeons given their
 * modifiers by a begun run, each carries it exactly when `CarriesTheCorruptedStalker` says so for its masked seed.
 * CONTROL: the same roll on the unmasked seeds names another set, so the check can tell the two apart.
 */
CATACLYSM_WALK_SEED_TEST(FCataclysmWalkSeedStalkerTest,
	"Cataclysm.WalkSeed.TheCorruptedStalkerIsRolledOnTheRunsMaskedSeed")
{
	using namespace CataclysmWalkSeedTest;

	UCataclysmEmpireRun* Run = BegunWalkSeedRun(2749);
	const FName StalkerKey = UCataclysmDungeonModifierRules::CorruptedStalkerKey();
	if (!TestTrue(TEXT("set-up: the run's pool holds the Corrupted Stalker's row"),
				  Run->ModifierPool.ContainsByPredicate([&StalkerKey](const FCataclysmDungeonModifier& Row)
				  {
					  return Row.RowKey == StalkerKey;
				  })))
	{
		return false;
	}

	TSet<int32> Carriers;
	TSet<int32> ExpectedMasked;
	TSet<int32> ExpectedUnmasked;
	for (int32 Id = 0; Id < 200; ++Id)
	{
		FCataclysmDungeon Made;
		Made.DungeonId = Id;
		Run->GiveModifiers(Made);
		if (Made.Modifiers.Contains(StalkerKey))
		{
			Carriers.Add(Id);
		}
		if (UCataclysmDungeonModifierRules::CarriesTheCorruptedStalker(Run->CorruptedStalkerSeed,
																		Made.WalkSeed(Run->WalkSeedMask)))
		{
			ExpectedMasked.Add(Id);
		}
		if (UCataclysmDungeonModifierRules::CarriesTheCorruptedStalker(Run->CorruptedStalkerSeed, Made.WalkSeed(0)))
		{
			ExpectedUnmasked.Add(Id);
		}
	}
	const bool bSameAsMasked = Carriers.Num() == ExpectedMasked.Num() && Carriers.Includes(ExpectedMasked);
	const bool bSameAsUnmasked = ExpectedUnmasked.Num() == ExpectedMasked.Num()
		&& ExpectedUnmasked.Includes(ExpectedMasked);

	AddInfo(FString::Printf(TEXT("WALK SEED STALKER: mask %d; %d of 200 carry it; the masked roll names %d, the unmasked %d"),
							Run->WalkSeedMask, Carriers.Num(), ExpectedMasked.Num(), ExpectedUnmasked.Num()));

	TestTrue(TEXT("set-up: some dungeons carry it and some do not"), Carriers.Num() > 0 && Carriers.Num() < 200);
	TestTrue(TEXT("each carries it exactly when its masked seed's roll says so"), bSameAsMasked);
	TestFalse(TEXT("CONTROL: the unmasked seeds' roll names another set"), bSameAsUnmasked);
	return true;
}

#undef CATACLYSM_WALK_SEED_TEST

#endif // WITH_AUTOMATION_TESTS
