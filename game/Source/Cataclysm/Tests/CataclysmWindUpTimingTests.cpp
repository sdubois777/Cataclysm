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
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for when a wound-up ability actually lands.
 *
 * AT ITS OWN LENGTH, SINCE ISSUE #2808. The owner's choice of 2026-11-21: "each
 * one lands exactly on time instead of waiting up to a quarter second extra".
 * `ACataclysmEnemyCharacter::Tick` asks `ACataclysmEnemyController::
 * LandWindUpIfDue` every frame, so an attack lands on the first frame at or
 * after its wind-up's length.
 *
 * WHAT CAME BEFORE, AND WHY IT WAS REVERSED. Issue #413 found that landing on
 * "the first thinking pass whose clock has gone past the deadline" was a coin
 * toss for a telegraph sitting exactly on a quarter-second pass boundary, and
 * fixed it by counting whole passes, rounded up: the stomp's 1.4 second
 * telegraph landed at 1.5. The coin toss came from checking four times a
 * second. Checked every frame, the lateness is at most one frame whatever the
 * length, so there is nothing to count and nothing to round.
 *
 * DETERMINISTIC IN A TEST, WHICH IS WHAT #413 ALSO NEEDED. No automation test
 * ticks a world, so these move the clock by hand and call `LandWindUpIfDue` as
 * the frame would. Whether it lands is then a comparison of two numbers the
 * test set, and the same every run.
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

	/** A world clock moved by hand. See CataclysmEnemyBehaviourTests.cpp. */
	static void AdvanceWorldClock(UWorld* World, double Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** A Brute, something for it to attack at `TargetMetres`, and its brain. */
	struct FBruteAndBrain
	{
		ACataclysmBruteCharacter* Brute = nullptr;
		ACataclysmEnemyCharacter* Target = nullptr;
		ACataclysmEnemyController* Brain = nullptr;
	};

	static FBruteAndBrain SpawnBruteFacing(UWorld* World, float TargetMetres)
	{
		FBruteAndBrain Out;
		Out.Brute = World->SpawnActor<ACataclysmBruteCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		Out.Target = SpawnTarget(World, FVector(TargetMetres * M, 0.0f, 0.0f));
		Out.Brain = Out.Brute ? Cast<ACataclysmEnemyController>(Out.Brute->GetController()) : nullptr;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWindUpLandsAtItsOwnLength,
	"Cataclysm.AI.AWindUpLandsAtItsOwnLengthNotOnTheNextThinkingPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWindUpLandsAtItsOwnLength::RunTest(const FString&)
{
	using namespace CataclysmWindUpTimingTest;

	// THE TWO CASES ISSUE #413 WAS ABOUT. The stomp's 1.4 seconds sits between two
	// thinking passes and landed on the later, at 1.5. The rock throw's 1.0 sits
	// exactly on one and landed at 1.00 or 1.25 by a coin toss.
	struct FCase
	{
		const TCHAR* What;
		float TargetMetres;
		int32 Ability;
		float WindUp;
	};
	const FCase Cases[] = {
		// Inside the stomp's 3.5 metre ring.
		{TEXT("the stomp"), 1.0f, ACataclysmBruteCharacter::StompAbility,
			ACataclysmBruteCharacter::StompWindUpSeconds},
		// Past the stomp's ring, inside the throw's 10 metres.
		{TEXT("the rock throw"), 7.0f, ACataclysmBruteCharacter::RockThrowAbility,
			ACataclysmBruteCharacter::RockThrowWindUpSeconds},
	};

	for (const FCase& Case : Cases)
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		const FBruteAndBrain Spawned = SpawnBruteFacing(World, Case.TargetMetres);
		if (!TestNotNull(TEXT("brute"), Spawned.Brute) || !TestNotNull(TEXT("its target"), Spawned.Target)
			|| !TestNotNull(TEXT("its brain"), Spawned.Brain))
		{
			return false;
		}
		ACataclysmEnemyController* Brain = Spawned.Brain;

		if (!TestEqual(FString::Printf(TEXT("it begins %s"), Case.What),
				static_cast<int32>(Brain->Think()), static_cast<int32>(ECataclysmBrainAction::WindingUp))
			|| !TestEqual(FString::Printf(TEXT("and it is %s"), Case.What), Brain->WindingUpAbility, Case.Ability))
		{
			return false;
		}

		// A HUNDREDTH SHORT OF ITS LENGTH: NOT YET. Never early.
		AdvanceWorldClock(World, Case.WindUp - 0.01);
		TestFalse(FString::Printf(TEXT("%s has not landed a hundredth of a second short of %.2f s"), Case.What,
			Case.WindUp), Brain->LandWindUpIfDue());
		TestEqual(TEXT("so no ability has been used"), Brain->AbilitiesUsed, 0);

		// AT ITS LENGTH: LANDED. Before issue #2808 the stomp was still winding up here, 0.1 s short of its pass.
		AdvanceWorldClock(World, 0.01);
		TestTrue(FString::Printf(TEXT("**%s lands at %.2f s, its own length**"), Case.What, Case.WindUp),
			Brain->LandWindUpIfDue());
		TestEqual(TEXT("so one ability has been used"), Brain->AbilitiesUsed, 1);
		TestEqual(TEXT("and the wind-up is over"), Brain->WindingUpAbility, static_cast<int32>(INDEX_NONE));
		TestNull(TEXT("and its marker is gone"), Brain->WindUpMarker.Get());

		// AND ASKED AGAIN, NOTHING: one landing per wind-up.
		TestFalse(TEXT("asked again on the next frame, it lands nothing more"), Brain->LandWindUpIfDue());
		TestEqual(TEXT("still one"), Brain->AbilitiesUsed, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWindUpIsDueNeitherEarlyNorLate,
	"Cataclysm.AI.AWindUpIsDueAtItsLengthNeverEarlierAtAnyLengthOrClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWindUpIsDueNeitherEarlyNorLate::RunTest(const FString&)
{
	using Brain_t = ACataclysmEnemyController;

	// AS A PROPERTY RATHER THAN AS EXAMPLES: every wind-up from a hundredth of a second to three seconds, begun at a
	// clock of zero, of a minute and of an hour, is due at its length and not two milliseconds before it. The hour is
	// there because a float deadline loses precision as the clock grows; at an hour it is still under a millisecond.
	TestTrue(TEXT("the tolerance is a millisecond, far under a frame"),
		Brain_t::WindUpLandingToleranceSeconds <= 0.001f);

	const float Starts[] = {0.0f, 60.0f, 3600.0f};
	for (const float Start : Starts)
	{
		for (int32 Hundredths = 1; Hundredths <= 300; ++Hundredths)
		{
			const float WindUp = static_cast<float>(Hundredths) / 100.0f;
			const float LandsAt = Start + WindUp;
			if (Brain_t::IsWindUpDue(Start + WindUp - 0.002f, LandsAt))
			{
				AddError(FString::Printf(TEXT("a %.2f s wind-up begun at %.0f s is due two milliseconds early"),
					WindUp, Start));
				return false;
			}
			if (!Brain_t::IsWindUpDue(LandsAt, LandsAt))
			{
				AddError(FString::Printf(TEXT("a %.2f s wind-up begun at %.0f s is not due at its own length"),
					WindUp, Start));
				return false;
			}
		}
	}

	// THE CONTROL: the rounding issue #2808 removed. The stomp's 1.4 seconds, rounded up to a quarter-second pass,
	// was 1.5; it is due at 1.4 and that is a tenth of a second sooner.
	const float Stomp = ACataclysmBruteCharacter::StompWindUpSeconds;
	const float OldLanding =
		FMath::CeilToFloat(Stomp / Brain_t::ThinkIntervalSeconds) * Brain_t::ThinkIntervalSeconds;
	TestEqual(TEXT("the stomp used to land on the pass at 1.5 s"), OldLanding, 1.5f, 0.0001f);
	TestTrue(TEXT("**and is due at 1.4 s, before that pass**"), Brain_t::IsWindUpDue(Stomp, Stomp));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWindUpNotLandedByTheFrameWhenStunned,
	"Cataclysm.AI.TheFrameDoesNotLandAWindUpForACreatureThatCannotAct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWindUpNotLandedByTheFrameWhenStunned::RunTest(const FString&)
{
	using namespace CataclysmWindUpTimingTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FBruteAndBrain Spawned = SpawnBruteFacing(World, 1.0f);
	if (!TestNotNull(TEXT("brute"), Spawned.Brute) || !TestNotNull(TEXT("its target"), Spawned.Target)
		|| !TestNotNull(TEXT("its brain"), Spawned.Brain))
	{
		return false;
	}
	ACataclysmEnemyController* Brain = Spawned.Brain;

	if (!TestEqual(TEXT("it begins a stomp"), static_cast<int32>(Brain->Think()),
			static_cast<int32>(ECataclysmBrainAction::WindingUp)))
	{
		return false;
	}

	// STUNNED HALF A SECOND IN, then the clock passes the stomp's length with no thinking pass between: in play, the
	// frames come before the next pass. The frame must not land what the creature can no longer make.
	AdvanceWorldClock(World, 0.5);
	if (!TestTrue(TEXT("it is stunned"), UCataclysmSkillEffects::ApplyStun(Spawned.Target, Spawned.Brute,
			/*DurationSeconds=*/1.5f, /*DamageDealt=*/1000.0f, /*bStunIsDesigned=*/true)))
	{
		return false;
	}
	AdvanceWorldClock(World, ACataclysmBruteCharacter::StompWindUpSeconds);
	TestFalse(TEXT("**the frame does not land the stomp of a stunned Brute**"), Brain->LandWindUpIfDue());
	TestEqual(TEXT("so no ability has been used"), Brain->AbilitiesUsed, 0);

	// AND THE THINKING PASS ABANDONS IT, as it always has.
	TestEqual(TEXT("the next pass reports it stunned"), static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::Stunned));
	TestEqual(TEXT("and the wind-up is abandoned"), Brain->WindingUpAbility, static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTheMarkerOutlastsTheLanding,
	"Cataclysm.Telegraph.TheMarkerIsShownUntilTheAttackLandsAndNoLonger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheMarkerOutlastsTheLanding::RunTest(const FString&)
{
	using namespace CataclysmWindUpTimingTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FBruteAndBrain Spawned = SpawnBruteFacing(World, 1.0f);
	if (!TestNotNull(TEXT("brute"), Spawned.Brute) || !TestNotNull(TEXT("its target"), Spawned.Target)
		|| !TestNotNull(TEXT("its brain"), Spawned.Brain))
	{
		return false;
	}
	ACataclysmEnemyController* Brain = Spawned.Brain;

	if (!TestEqual(TEXT("it begins a stomp"), static_cast<int32>(Brain->Think()),
			static_cast<int32>(ECataclysmBrainAction::WindingUp)))
	{
		return false;
	}

	ACataclysmTelegraphMarker* Marker = Brain->WindUpMarker.Get();
	if (!TestNotNull(TEXT("and draws a marker"), Marker))
	{
		return false;
	}

	// THE MARKER'S OWN LIFESPAN IS A BACKSTOP. The landing removes it, up to a frame after the wind-up's length, so a
	// lifespan of exactly the length could take it off the floor a frame early -- the fault issue #413 found when the
	// landing could come a tenth of a second late. It is the length plus a thinking interval.
	const float Stomp = ACataclysmBruteCharacter::StompWindUpSeconds;
	TestTrue(FString::Printf(TEXT("the marker would stay %.2f s, longer than the %.2f s stomp"),
		Marker->GetLifeSpan(), Stomp), Marker->GetLifeSpan() > Stomp);
	TestEqual(TEXT("by exactly a thinking interval"), Marker->GetLifeSpan(),
		Stomp + ACataclysmEnemyController::ThinkIntervalSeconds, 0.0001f);

	// AND THE LANDING TAKES IT AWAY, so it is never on the floor after the ring went off.
	AdvanceWorldClock(World, Stomp);
	TestTrue(TEXT("the stomp lands"), Brain->LandWindUpIfDue());
	TestNull(TEXT("**and its marker goes with it**"), Brain->WindUpMarker.Get());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmFirstThinkIsCountedPerWorld,
	"Cataclysm.AI.ACreaturesFirstThinkDependsOnlyOnItsOwnWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The first creature possessed in a new world has its first thinking pass one whole interval after it was possessed,
 * the count of 0, however many creatures another world possessed first; inside one world the counts still run on.
 * Issue #2827: the count was one for the whole process, so a creature's first think depended on what earlier tests had
 * possessed, and #2811's two tests in window D gave one result alone and another in the suite. Prints "PER WORLD".
 *
 * IT FAILS ON THE OLD CODE WHATEVER RAN BEFORE IT: world A possesses three creatures first, so under a process-wide
 * count world B's first creature would take a count of at least 3.
 */
bool FCataclysmFirstThinkIsCountedPerWorld::RunTest(const FString&)
{
	using namespace CataclysmWindUpTimingTest;
	using Brain = ACataclysmEnemyController;

	UWorld* WorldA = MakeWorldThatHasBegunPlay();
	UWorld* WorldB = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT
	{
		for (UWorld* World : { WorldA, WorldB })
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
	};
	if (!TestNotNull(TEXT("world A"), WorldA) || !TestNotNull(TEXT("world B"), WorldB))
	{
		return false;
	}

	const auto FirstThinkOf = [this](UWorld* World, int32 Index) -> float
	{
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(3.0f * M * static_cast<float>(Index), 0.0f, 0.0f), FRotator::ZeroRotator);
		const Brain* Controller = Creature ? Cast<Brain>(Creature->GetController()) : nullptr;
		TestNotNull(FString::Printf(TEXT("set-up: creature %d has a controller"), Index), Controller);
		return Controller ? Controller->SecondsUntilNextThink() : -1.0f;
	};

	// WORLD A FIRST: three creatures, its counts 0, 1 and 2.
	const float A0 = FirstThinkOf(WorldA, 0);
	const float A1 = FirstThinkOf(WorldA, 1);
	const float A2 = FirstThinkOf(WorldA, 2);
	// THEN WORLD B'S FIRST, and world A's fourth after it.
	const float B0 = FirstThinkOf(WorldB, 0);
	const float A3 = FirstThinkOf(WorldA, 3);

	AddInfo(FString::Printf(TEXT("PER WORLD: world A's first passes %.4f, %.4f, %.4f and %.4f; world B's first %.4f; the "
								 "counts 0 to 3 give %.4f, %.4f, %.4f and %.4f"),
							A0, A1, A2, A3, B0, Brain::FirstThinkDelaySeconds(0u), Brain::FirstThinkDelaySeconds(1u),
							Brain::FirstThinkDelaySeconds(2u), Brain::FirstThinkDelaySeconds(3u)));

	constexpr float Tolerance = 0.0001f;
	TestEqual(TEXT("world A's first creature takes the count 0, one whole interval"), A0,
			  Brain::FirstThinkDelaySeconds(0u), Tolerance);
	TestEqual(TEXT("its second the count 1"), A1, Brain::FirstThinkDelaySeconds(1u), Tolerance);
	TestEqual(TEXT("its third the count 2"), A2, Brain::FirstThinkDelaySeconds(2u), Tolerance);
	TestEqual(TEXT("world B's first creature takes the count 0, whatever world A possessed"), B0,
			  Brain::FirstThinkDelaySeconds(0u), Tolerance);
	TestEqual(TEXT("and world A's fourth goes on from its own count, 3"), A3, Brain::FirstThinkDelaySeconds(3u),
			  Tolerance);
	TestNotEqual(TEXT("CONTROL: the counts 0 and 3 give different first passes"), Brain::FirstThinkDelaySeconds(0u),
				 Brain::FirstThinkDelaySeconds(3u));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
