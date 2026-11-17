// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "Tests/CataclysmTestWorld.h"
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
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
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

// ---------------------------------------------------------------------------
// When a creature's first thinking pass comes. Issue #1543
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmCreaturesPossessedTogetherThinkApart,
	"Cataclysm.AI.CreaturesPossessedInTheSameFrameDoNotAllThinkInTheSameFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreaturesPossessedTogetherThinkApart::RunTest(const FString&)
{
	using namespace CataclysmWindUpTimingTest;

	// **WHAT THIS GUARDS, AND IT FAILS ON THE CODE BEFORE ISSUE #1543.** Every
	// controller used to schedule its first pass exactly one interval after it
	// was possessed, so creatures possessed in one frame all had their first
	// pass in one frame -- and every pass after it, because the timer repeats.
	// A capture of the project owner's Horde session showed a wave of 139 doing
	// that four times a second, about 170 ms of timer work each time.
	//
	// IT READS THE ENGINE'S OWN SCHEDULE. `SecondsUntilNextThink` asks the
	// world's timer manager when this controller's timer will next fire, so what
	// is checked is what the engine will do rather than a number the controller
	// wrote down about itself. A world made for a test is never ticked, so each
	// timer is still waiting for its first pass when it is read.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// TWENTY, ALL POSSESSED IN THIS ONE FRAME. The clock never moves in this
	// test and one test runs inside one frame, so every spawn below happens in
	// the same frame, which is what a wave did.
	constexpr int32 HowMany = 20;
	const float Interval = ACataclysmEnemyController::ThinkIntervalSeconds;

	TArray<float> FirstThinks;
	for (int32 Index = 0; Index < HowMany; ++Index)
	{
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(3.0f * M * static_cast<float>(Index), 0.0f, 0.0f),
			FRotator::ZeroRotator);
		ACataclysmEnemyController* Brain = Creature
			? Cast<ACataclysmEnemyController>(Creature->GetController()) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("creature %d has a controller"), Index),
				Brain))
		{
			return false;
		}

		const float FirstThink = Brain->SecondsUntilNextThink();
		if (!TestTrue(FString::Printf(
				TEXT("creature %d has its first pass %.4f s after it was possessed, "
					 "which is later than now and no later than one %.2f s "
					 "interval"), Index, FirstThink, Interval),
				FirstThink > 0.0f && FirstThink <= Interval + UE_KINDA_SMALL_NUMBER))
		{
			return false;
		}

		// ONLY THE FIRST PASS MOVES. Wind-ups are counted in whole passes and the
		// Brute sizes its clips to them, so once it has started the timer must
		// still repeat every interval.
		TestEqual(FString::Printf(TEXT("and creature %d then thinks every %.2f s"),
				Index, Interval),
			Brain->SecondsBetweenThinks(), Interval);

		FirstThinks.Add(FirstThink);
	}

	// **THE ASSERTION THAT FAILS ON THE OLD CODE.** Put into frames at 60 frames
	// a second, the first passes must not pile into one frame. Fifteen frames
	// make one interval, so twenty spread evenly would be one or two to a
	// frame. The old delay put all twenty into the same frame.
	constexpr float Frame = 1.0f / 60.0f;
	TMap<int32, int32> PerFrame;
	for (const float FirstThink : FirstThinks)
	{
		++PerFrame.FindOrAdd(FMath::CeilToInt(FirstThink / Frame));
	}
	int32 Busiest = 0;
	for (const TPair<int32, int32>& Entry : PerFrame)
	{
		Busiest = FMath::Max(Busiest, Entry.Value);
	}
	TestTrue(FString::Printf(
			TEXT("at 60 frames a second the busiest frame holds %d of the %d first "
				 "passes, and no more than 3 is allowed"), Busiest, HowMany),
		Busiest <= 3);

	// AND THEY COVER THE WHOLE INTERVAL rather than one part of it. Each quarter
	// of the interval should hold about a quarter of them.
	int32 PerQuarter[4] = { 0, 0, 0, 0 };
	for (const float FirstThink : FirstThinks)
	{
		++PerQuarter[FMath::Clamp(
			FMath::CeilToInt(FirstThink / (Interval / 4.0f)) - 1, 0, 3)];
	}
	for (int32 Quarter = 0; Quarter < 4; ++Quarter)
	{
		TestTrue(FString::Printf(
				TEXT("quarter %d of the interval holds %d of the %d first passes, "
					 "and between 3 and 7 is allowed"),
				Quarter + 1, PerQuarter[Quarter], HowMany),
			PerQuarter[Quarter] >= 3 && PerQuarter[Quarter] <= 7);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAWholeWaveSpreadsFromAnyCount,
	"Cataclysm.AI.AWholeWavesFirstThinksSpreadEvenlyFromAnyStartingCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAWholeWaveSpreadsFromAnyCount::RunTest(const FString&)
{
	// THE SIZE OF A REAL WAVE, WITHOUT SPAWNING ONE. The test above drives the
	// possession a wave makes. This checks the arithmetic for the 139 creatures
	// of the owner's session, starting from counts spread across the whole range
	// the possession counter holds -- the counter is shared by every controller
	// in the process, so a wave can start from any of them -- including a start
	// just below where the counter wraps back to zero.
	const float Interval = ACataclysmEnemyController::ThinkIntervalSeconds;
	constexpr int32 Wave = 139;
	constexpr float Frame = 1.0f / 60.0f;

	TestEqual(TEXT("a count of zero waits one whole interval, as every creature "
				   "did before issue #1543"),
		ACataclysmEnemyController::FirstThinkDelaySeconds(0u), Interval);

	const uint32 Starts[] = { 0u, 1u, 138u, 1000u, 65535u, 123456789u,
							  2147483647u, 4294967200u };
	for (const uint32 Start : Starts)
	{
		TMap<int32, int32> PerFrame;
		int32 PerQuarter[4] = { 0, 0, 0, 0 };
		for (int32 Index = 0; Index < Wave; ++Index)
		{
			// UNSIGNED, so a start near the top wraps back through zero the way
			// the counter itself does.
			const float Delay = ACataclysmEnemyController::FirstThinkDelaySeconds(
				Start + static_cast<uint32>(Index));
			if (Delay <= 0.0f || Delay > Interval + UE_KINDA_SMALL_NUMBER)
			{
				AddError(FString::Printf(
					TEXT("from count %u, creature %d has its first pass %.6f s "
						 "away, which is not inside one %.2f s interval"),
					Start, Index, Delay, Interval));
				return false;
			}
			++PerFrame.FindOrAdd(FMath::CeilToInt(Delay / Frame));
			++PerQuarter[FMath::Clamp(
				FMath::CeilToInt(Delay / (Interval / 4.0f)) - 1, 0, 3)];
		}

		int32 Busiest = 0;
		for (const TPair<int32, int32>& Entry : PerFrame)
		{
			Busiest = FMath::Max(Busiest, Entry.Value);
		}

		// 139 SPREAD EVENLY OVER FIFTEEN FRAMES IS 9.3 A FRAME. The old delay
		// put all 139 into one.
		TestTrue(FString::Printf(
				TEXT("from count %u, the busiest frame at 60 frames a second holds "
					 "%d of a wave of %d, and no more than 11 is allowed"),
				Start, Busiest, Wave),
			Busiest <= 11);

		for (int32 Quarter = 0; Quarter < 4; ++Quarter)
		{
			TestTrue(FString::Printf(
					TEXT("from count %u, quarter %d of the interval holds %d of "
						 "%d, and between 30 and 40 is allowed"),
					Start, Quarter + 1, PerQuarter[Quarter], Wave),
				PerQuarter[Quarter] >= 30 && PerQuarter[Quarter] <= 40);
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
