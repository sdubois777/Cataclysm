// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for when a wound-up ability actually lands.
 *
 * WHAT THESE GUARD. Issue #413. An ability used to land on "the first thinking
 * pass whose clock has gone past the deadline", which sounds exact and is not. A
 * timer callback runs on the first FRAME past its deadline, so every pass carries
 * up to a frame of overshoot, and the overshoot on the pass that starts a wind-up
 * is not the overshoot on the pass that should land it. Where a telegraph sits on
 * a pass boundary -- which is exactly where the Brute's rock throw's 1.000 second
 * telegraph sits against a 0.250 second pass -- that difference of a few
 * milliseconds decided a whole quarter of a second, roughly half and half.
 *
 * WHY NOTHING CAUGHT IT. Every other test in this project moves the world clock
 * by hand in one jump and then calls Think, so the clock is always far past the
 * deadline and the coin toss never happens. These tests deliberately do the
 * opposite: they never move the clock at all, so the only thing that can land an
 * ability is the pass count.
 */

namespace CataclysmWindUpTimingTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (World)
		{
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}
		return World;
	}

	/** Metres, so these read the way the design document does. */
	constexpr float M = 100.0f;

	/** Something on the player's side for a Brute to attack. An enemy character
	 *  re-teamed, because a player pawn's ability system lives on a player state
	 *  that a synthetic world has no controller to create. */
	static ACataclysmEnemyCharacter* SpawnTarget(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Target = World->SpawnActor<ACataclysmEnemyCharacter>(
			Where, FRotator::ZeroRotator);
		if (Target)
		{
			Target->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			Target->SetHealth(1000.0f);
			Target->SetAttackDamage(0.0f);
		}
		return Target;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWindUpTakesTheCountedNumberOfPasses,
	"Cataclysm.AI.AWindUpTakesTheSameNumberOfPassesEveryTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWindUpTakesTheCountedNumberOfPasses::RunTest(const FString&)
{
	using namespace CataclysmWindUpTimingTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	// Out past the stomp's 3.5 m ring and inside the throw's 10 m range, so the
	// rock throw is the ability it chooses. That is the one whose telegraph sits
	// on a pass boundary.
	ACataclysmEnemyCharacter* Target = SpawnTarget(World, FVector(7.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("brute"), Brute)
		|| !TestNotNull(TEXT("something for it to attack"), Target))
	{
		return false;
	}

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Brute->GetController());
	if (!TestNotNull(TEXT("the Brute has a controller"), Brain))
	{
		return false;
	}

	// THE CLOCK IS NEVER TOUCHED IN THIS TEST. A world made by UWorld::CreateWorld
	// is never ticked, so its clock stands still at whatever it was. Every pass
	// below therefore sees the same time, which is well short of the deadline --
	// so if the pass count were not doing the work, this ability would never land
	// at all and the test would fail rather than pass by accident.
	const float ClockAtStart = World->GetTimeSeconds();

	if (!TestEqual(TEXT("it begins a rock throw"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp)))
	{
		return false;
	}

	const int32 Expected = ACataclysmEnemyController::PassesForWindUp(
		ACataclysmBruteCharacter::RockThrowWindUpSeconds);

	TestEqual(TEXT("and counts the passes its telegraph is worth"),
		Brain->WindUpPassesLeft, Expected);
	TestEqual(TEXT("which for a one second telegraph and a quarter second pass is four"),
		Expected, 4);

	// One short of the count. Every one of these must still be winding up: an
	// ability that landed here would be landing before its telegraph was over.
	for (int32 Pass = 1; Pass < Expected; ++Pass)
	{
		TestEqual(FString::Printf(
			TEXT("pass %d of %d is still winding up"), Pass, Expected),
			static_cast<int32>(Brain->Think()),
			static_cast<int32>(ECataclysmBrainAction::WindingUp));
		TestEqual(FString::Printf(TEXT("and no ability has landed by pass %d"), Pass),
			Brain->AbilitiesUsed, 0);
	}

	// The counted pass. This is the one the old code tossed a coin over.
	Brain->Think();

	TestEqual(TEXT("the ability lands on exactly the counted pass"),
		Brain->AbilitiesUsed, 1);
	TestEqual(TEXT("and the wind-up is over"), Brain->WindingUpAbility, -1);
	TestEqual(TEXT("and its pass count is cleared"), Brain->WindUpPassesLeft, 0);

	TestEqual(TEXT("with the world clock never having moved"),
		static_cast<double>(World->GetTimeSeconds()),
		static_cast<double>(ClockAtStart));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWindUpPassCountRoundsUp,
	"Cataclysm.AI.ATelegraphIsRoundedUpToAWholePassNeverDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWindUpPassCountRoundsUp::RunTest(const FString&)
{
	const float Pass = ACataclysmEnemyController::ThinkIntervalSeconds;

	// THE PLAYER'S GUARANTEE IS A FLOOR, NOT AN APPROXIMATION. The design states
	// a telegraph as the time the player has to walk clear, so an attack must
	// never land sooner than it. Rounding rather than taking the ceiling would
	// land the Brute's 1.4 second stomp at 1.25.
	TestEqual(TEXT("exactly one pass is one pass"),
		ACataclysmEnemyController::PassesForWindUp(Pass), 1);
	TestEqual(TEXT("a hair over one pass is two"),
		ACataclysmEnemyController::PassesForWindUp(Pass + 0.001f), 2);
	TestEqual(TEXT("a hair under two passes is still two"),
		ACataclysmEnemyController::PassesForWindUp(Pass * 2.0f - 0.001f), 2);

	// The two telegraphs the game actually has.
	TestEqual(TEXT("the rock throw's one second telegraph is four passes"),
		ACataclysmEnemyController::PassesForWindUp(
			ACataclysmBruteCharacter::RockThrowWindUpSeconds), 4);
	TestEqual(TEXT("the stomp's 1.4 second telegraph is six, so it lands at 1.5"),
		ACataclysmEnemyController::PassesForWindUp(
			ACataclysmBruteCharacter::StompWindUpSeconds), 6);

	// A very short telegraph is still committed to for a pass, rather than
	// landing on the pass that began it.
	TestEqual(TEXT("a telegraph shorter than a pass still takes one"),
		ACataclysmEnemyController::PassesForWindUp(0.01f), 1);

	// No telegraph at all is answered explicitly rather than as "already over".
	TestEqual(TEXT("no telegraph is no passes"),
		ACataclysmEnemyController::PassesForWindUp(0.0f), 0);

	// AND NO ROUNDED TELEGRAPH IS EVER SHORTER THAN THE ONE IT STANDS FOR. Said
	// as the property rather than as six examples, so a change to the pass
	// length is covered too.
	for (int32 Hundredths = 1; Hundredths <= 300; ++Hundredths)
	{
		const float WindUp = static_cast<float>(Hundredths) / 100.0f;
		const float Effective =
			ACataclysmEnemyController::PassesForWindUp(WindUp) * Pass;
		if (Effective + UE_KINDA_SMALL_NUMBER < WindUp)
		{
			AddError(FString::Printf(
				TEXT("a telegraph of %.2f s lands after %.2f s, which is sooner "
					 "than the player was told they had"), WindUp, Effective));
			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTheMarkerLastsAsLongAsTheWindUpReallyDoes,
	"Cataclysm.Telegraph.TheMarkerIsShownForAsLongAsTheAttackReallyTakes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheMarkerLastsAsLongAsTheWindUpReallyDoes::RunTest(const FString&)
{
	using namespace CataclysmWindUpTimingTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	// Inside the stomp's ring. The stomp is the ability whose telegraph is NOT a
	// whole number of passes -- 1.4 seconds against a 0.25 second pass -- so it
	// is the one whose marker and landing could come apart.
	ACataclysmEnemyCharacter* Target = SpawnTarget(World, FVector(1.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("brute"), Brute)
		|| !TestNotNull(TEXT("something for it to attack"), Target))
	{
		return false;
	}

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Brute->GetController());
	if (!TestNotNull(TEXT("the Brute has a controller"), Brain))
	{
		return false;
	}

	if (!TestEqual(TEXT("it begins a stomp"),
		static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp)))
	{
		return false;
	}

	ACataclysmTelegraphMarker* Marker = Brain->WindUpMarker.Get();
	if (!TestNotNull(TEXT("and draws a marker"), Marker))
	{
		return false;
	}

	// WHAT WENT WRONG. The marker carries its own lifespan as a backstop, and it
	// was given the DESIGNED telegraph of 1.4 seconds. An ability lands on a
	// whole thinking pass, so the stomp really lands at 1.5. The marker took
	// itself off the floor a tenth of a second before the ring it warned about
	// went off, which is the one moment it must not do that.
	const float Designed = ACataclysmBruteCharacter::StompWindUpSeconds;
	const float Real = ACataclysmEnemyController::PassesForWindUp(Designed)
		* ACataclysmEnemyController::ThinkIntervalSeconds;

	TestTrue(FString::Printf(
		TEXT("the stomp really takes longer than its designed %.2f s (%.2f s), "
			 "which is what makes this test worth having"), Designed, Real),
		Real > Designed);

	TestEqual(TEXT("and the marker is shown for exactly that long"),
		Marker->GetLifeSpan(), Real);

	TestTrue(TEXT("so it is still on the floor when the attack lands"),
		Marker->GetLifeSpan() >= Real);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
