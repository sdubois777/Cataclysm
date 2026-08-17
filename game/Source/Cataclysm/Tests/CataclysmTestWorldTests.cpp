// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Tests/CataclysmTestWorld.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for the test harness itself.
 *
 * THESE EXIST BECAUSE THE HARNESS LIED FOR WEEKS. Twenty test files called their
 * world helper `MakeWorldThatHasBegunPlay` and it did not begin play, and two of
 * them stated in comments that "actors spawned after this point get their
 * BeginPlay called as they spawn" while no actor ever received it. Issue #654.
 *
 * A CLAIM ABOUT THE HARNESS NEEDS A TEST LIKE ANY OTHER. Everything below is
 * about the world helper rather than about the game, and each one would have
 * failed before this change.
 */

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmTestWorldDispatchesBeginPlayTest,
	"Cataclysm.TestWorld.AnActorSpawnedIntoABegunWorldReceivesBeginPlay")
{
	// THE ONE CLAIM EVERYTHING ELSE RESTS ON. `UWorld::HasBegunPlay` is the gate
	// `AActor::PostActorConstruction` checks before dispatching, and it asks two
	// things: that the world's begun-play flag is set, and that the persistent
	// level holds at least one actor. The second is a property of the engine
	// rather than of our code, so it is asked here rather than reasoned about.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	TestTrue(TEXT("the world reports that play has begun"), World->HasBegunPlay());

	AActor* First = World->SpawnActor<AActor>();
	if (TestNotNull(TEXT("an actor"), First))
	{
		// THE FIRST ACTOR SPAWNED, SPECIFICALLY. If the persistent level were
		// empty at this moment the gate would refuse, and the very first spawn in
		// every test would silently skip BeginPlay while later ones did not --
		// which is a worse state than the one being fixed, because it is
		// intermittent.
		TestTrue(TEXT("the first actor spawned has begun play"),
			First->HasActorBegunPlay());
		First->Destroy();
	}

	AActor* Second = World->SpawnActor<AActor>();
	if (TestNotNull(TEXT("a second actor"), Second))
	{
		TestTrue(TEXT("and so has one spawned after it"),
			Second->HasActorBegunPlay());
		Second->Destroy();
	}

	return true;
}

CATACLYSM_TEST(FCataclysmTestWorldWithoutBeginPlayTest,
	"Cataclysm.TestWorld.TheOtherHelperGivesAWorldThatHasNotBegunPlay")
{
	// THE TWO HELPERS HAVE TO DIFFER, or the honest name is decoration. A test
	// that deliberately wants no BeginPlay -- because it is about something a
	// creature's BeginPlay would disturb -- has to be able to get one.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasNotBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	TestFalse(TEXT("this world has not begun play"), World->HasBegunPlay());

	AActor* Actor = World->SpawnActor<AActor>();
	if (TestNotNull(TEXT("an actor"), Actor))
	{
		TestFalse(TEXT("so an actor spawned into it has not begun play"),
			Actor->HasActorBegunPlay());
		Actor->Destroy();
	}

	return true;
}

CATACLYSM_TEST(FCataclysmTestWorldStartsTheRegenerationTimerTest,
	"Cataclysm.TestWorld.ACharactersBeginPlayWorkActuallyRuns")
{
	// NOT THE FLAG BUT THE CONSEQUENCE. The point of issue #654 was never the
	// boolean: it was that everything a class starts in BeginPlay went untested.
	// `ACataclysmCharacterBase::BeginPlay` sets a repeating regeneration timer,
	// so a character in a begun world should be holding one and a character in an
	// unbegun world should not.
	UWorld* Begun = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a begun world"), Begun))
	{
		return false;
	}
	{
		ON_SCOPE_EXIT { Begun->DestroyWorld(false); };

		ACataclysmEnemyCharacter* Creature =
			Begun->SpawnActor<ACataclysmEnemyCharacter>();
		if (TestNotNull(TEXT("a creature"), Creature))
		{
			TestTrue(TEXT("a creature in a begun world has begun play"),
				Creature->HasActorBegunPlay());
			TestTrue(TEXT("and it is regenerating on a timer"),
				Creature->IsRegenerating());
		}
	}

	UWorld* Unbegun = CataclysmTestWorld::MakeWorldThatHasNotBegunPlay();
	if (!TestNotNull(TEXT("an unbegun world"), Unbegun))
	{
		return false;
	}
	{
		ON_SCOPE_EXIT { Unbegun->DestroyWorld(false); };

		ACataclysmEnemyCharacter* Creature =
			Unbegun->SpawnActor<ACataclysmEnemyCharacter>();
		if (TestNotNull(TEXT("a creature"), Creature))
		{
			TestFalse(TEXT("a creature in an unbegun world has not begun play"),
				Creature->HasActorBegunPlay());
			TestFalse(TEXT("and holds no regeneration timer"),
				Creature->IsRegenerating());
		}
	}

	return true;
}

#undef CATACLYSM_TEST

#endif  // WITH_AUTOMATION_TESTS
