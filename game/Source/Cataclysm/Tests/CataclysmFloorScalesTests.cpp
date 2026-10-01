// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The four scales of issue #1833 group C part 3c: seconds on this floor, floors
 * cleared this run, points of armour, and unique Cataclysm bosses defeated.
 * Ruled 2026-09-30 under the owner's delegation.
 *
 * THE ARITHMETIC IS MEASURED ON A HAND-BUILT STATE, because what is under test is
 * what the pipeline does with each reading. Where each reading comes from is
 * measured below on the player state, and the rows in
 * CataclysmEnchantmentEffectTests.cpp, where a real enchantment is worn.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFourScalesTest,
	"Cataclysm.Scales.EachFloorAndArmourScaleCountsWholeStepsAndAnUnknownReadingIsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFourScalesTest::RunTest(const FString&)
{
	struct FCase
	{
		const TCHAR* Name;
		ECataclysmStatScale Scale;
		float Step;
		TFunction<void(FCataclysmStatConditions&)> Read;
		float Expected;
	};

	// 10% INCREASED A STEP ON A BASE OF 100, so each answer is 100 plus ten a step.
	const TArray<FCase> Cases = {
		{TEXT("44 seconds on this floor, a step of 15: two steps"),
		 ECataclysmStatScale::PerSecondOnThisFloor, 15.0f,
		 [](FCataclysmStatConditions& S) { S.SecondsOnFloor = 44.0f; }, 120.0f},
		{TEXT("3 floors cleared, a step of 1: three steps"),
		 ECataclysmStatScale::PerFloorClearedThisRun, 1.0f,
		 [](FCataclysmStatConditions& S) { S.FloorsCleared = 3; }, 130.0f},
		{TEXT("250 armour, a step of 100: two steps"),
		 ECataclysmStatScale::PerPointOfArmor, 100.0f,
		 [](FCataclysmStatConditions& S) { S.Armor = 250.0f; }, 120.0f},
		{TEXT("2 unique bosses, a step of 1: two steps"),
		 ECataclysmStatScale::PerUniqueCataclysmBossDefeated, 1.0f,
		 [](FCataclysmStatConditions& S) { S.CataclysmBossesDefeated = 2; }, 120.0f},
	};

	for (const FCase& Case : Cases)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Increased;
		Row.Source = ECataclysmModifierSource::Enchantment;
		Row.Value = 10.0f;
		Row.Scale = Case.Scale;
		Row.ScaleStep = Case.Step;

		FCataclysmStatConditions Known;
		Case.Read(Known);
		TestEqual(Case.Name, UCataclysmStatPipeline::Evaluate(100.0f, {Row}, FGameplayTagContainer(), Known).Final,
				  Case.Expected, 0.001f);

		// AN UNKNOWN READING, the -1 every field starts at, is worth nothing.
		TestEqual(*FString::Printf(TEXT("and unknown, nothing: %s"), Case.Name),
				  UCataclysmStatPipeline::Evaluate(100.0f, {Row}, FGameplayTagContainer(),
												   FCataclysmStatConditions()).Final,
				  100.0f, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorRecordsTest,
	"Cataclysm.Scales.TheFloorClockStartsAgainOnANewFloorAndEachClearIsCounted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * What the dungeon game mode tells the player state: a floor began at a world
 * time, and a floor was cleared. Issue #1833 group C part 3c.
 */
bool FCataclysmFloorRecordsTest::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* Player = World->SpawnActor<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("a player state"), Player))
	{
		return false;
	}

	TestEqual(TEXT("before any floor began, the clock is unknown"), Player->SecondsOnFloor(25.0f), -1.0f, 0.001f);
	Player->NoteFloorBegan(10.0f);
	TestEqual(TEXT("a floor that began at 10, read at 25: 15 seconds"), Player->SecondsOnFloor(25.0f), 15.0f, 0.001f);
	Player->NoteFloorBegan(30.0f);
	TestEqual(TEXT("a second floor at 30 starts it again: 1 second at 31"), Player->SecondsOnFloor(31.0f), 1.0f, 0.001f);

	TestEqual(TEXT("no floors cleared yet"), Player->GetFloorsClearedThisRun(), 0);
	Player->NoteFloorCleared();
	Player->NoteFloorCleared();
	TestEqual(TEXT("two clears, two floors"), Player->GetFloorsClearedThisRun(), 2);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
