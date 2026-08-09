// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
// ON_SCOPE_EXIT. This file used it from the first version and compiled anyway,
// through a transitive include that nothing guarantees. Named here so a change
// to some other header cannot break this one.
#include "Misc/ScopeExit.h"
#include "UObject/SoftObjectPath.h"

/**
 * Tests for The Brute, the first of the seven Demonic vertical slice enemies.
 *
 * WHAT THESE GUARD. Three separate things that a Brute can silently fail at:
 *
 * 1. Being a Brute at all rather than an inherited training dummy. Every
 *    assertion on a designed number also asserts it differs from the base
 *    enemy's, because a test that passes on the base class proves nothing.
 *
 * 2. Reaching the player. The Brute's designed reach is exactly contact --
 *    0.42 + 0.48 metres, the two capsule radii -- with no margin at all. That
 *    makes it the only enemy where the distance check's own arithmetic decides
 *    whether it ever lands a hit. See the horizontal-distance test.
 *
 * 3. Wearing its art without breaking without it. The Paragon packs are
 *    gitignored, so these tests run both with the art present and absent and
 *    must pass either way.
 */

namespace CataclysmBruteTest
{
	/** A world that has begun play, so spawned characters get their attributes. */
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

	/** The player capsule radius in CataclysmPlayerCharacter.cpp. */
	constexpr float PlayerCapsuleRadius = 42.0f;

	/** The player capsule half-height in CataclysmPlayerCharacter.cpp. */
	constexpr float PlayerCapsuleHalfHeight = 96.0f;

	/** The base enemy capsule half-height in CataclysmEnemyCharacter.cpp. */
	constexpr float BaseEnemyCapsuleHalfHeight = 80.0f;

	/**
	 * The base enemy capsule radius in CataclysmEnemyCharacter.cpp.
	 *
	 * The six vertical slice enemies that have no class of their own are this
	 * wide. tools/tests/test_enemy_abilities.py holds it against the design
	 * model's default body radius of 0.48 m.
	 */
	constexpr float BaseEnemyCapsuleRadius = 48.0f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteCarriesItsDesignedNumbers,
	"Cataclysm.Brute.CarriesItsDesignedNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteCarriesItsDesignedNumbers::RunTest(const FString&)
{
	using namespace CataclysmBruteTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute spawned"), Brute))
	{
		return false;
	}

	TestEqual(TEXT("reach is the designed 0.90 m"),
		Brute->AttackReachCm(), ACataclysmBruteCharacter::DesignedMeleeReachCm);
	TestEqual(TEXT("attack interval is the designed 2.8 s"),
		Brute->SecondsBetweenAttacks(),
		ACataclysmBruteCharacter::DesignedAttackIntervalSeconds);

	const UCharacterMovementComponent* Movement = Brute->GetCharacterMovement();
	if (!TestNotNull(TEXT("movement component"), Movement))
	{
		return false;
	}
	TestEqual(TEXT("walks at the designed 250 cm/s"),
		Movement->MaxWalkSpeed,
		ACataclysmBruteCharacter::DesignedWalkSpeedCmPerSecond);
	// CAST, BECAUSE FRotator's components are double in Unreal 5 while the
	// designed constant is a float, and TestEqual has an overload for each.
	TestEqual(TEXT("turns at the designed 180 deg/s"),
		static_cast<float>(Movement->RotationRate.Yaw),
		ACataclysmBruteCharacter::DesignedTurnRateDegreesPerSecond);

	const UCapsuleComponent* Capsule = Brute->GetCapsuleComponent();
	if (!TestNotNull(TEXT("capsule"), Capsule))
	{
		return false;
	}
	TestEqual(TEXT("capsule radius is the designed body radius, 48 cm"),
		Capsule->GetUnscaledCapsuleRadius(),
		ACataclysmBruteCharacter::BruteCapsuleRadius);
	TestEqual(TEXT("capsule half-height fits the 221 cm mesh"),
		Capsule->GetUnscaledCapsuleHalfHeight(),
		ACataclysmBruteCharacter::BruteCapsuleHalfHeight);

	// THE POINT OF THE WHOLE CLASS, stated as an inequality rather than a value.
	// "Heavily armored slow melee. Can be outmaneuvered" is a claim about being
	// slower than other things, so it is tested against another thing.
	//
	// WHICH PROPERTIES CARRY THAT CLAIM CHANGED ON 2026-08-09. Three did:
	// moving, turning and swinging. The project owner settled the swing at 1.2
	// seconds by playing it, against the generic enemy's 1.5, so the Brute now
	// swings FASTER than anything unconfigured. Moving and turning still carry
	// the claim, and turning is the one that means "outmanoeuvred": a player
	// circling at the Brute's own reach sweeps 223 degrees per second even in
	// the slowest class, against this creature's 180.
	ACataclysmEnemyCharacter* Ordinary = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("ordinary enemy spawned"), Ordinary))
	{
		return false;
	}

	TestTrue(TEXT("a Brute walks strictly slower than an ordinary enemy"),
		Brute->GetCharacterMovement()->MaxWalkSpeed
			< Ordinary->GetCharacterMovement()->MaxWalkSpeed);
	TestTrue(TEXT("a Brute turns strictly slower than an ordinary enemy"),
		Brute->GetCharacterMovement()->RotationRate.Yaw
			< Ordinary->GetCharacterMovement()->RotationRate.Yaw);

	// ASSERTED IN THE DIRECTION IT WAS DELIBERATELY MOVED, which is a guard
	// rather than a hole: putting the swing back above the generic default
	// would fail here and send whoever did it to docs/DECISIONS.md.
	TestTrue(FString::Printf(
		TEXT("a Brute swings faster than an ordinary enemy, which was decided "
			 "by play on 2026-08-09 (%.2f s against %.2f)"),
		Brute->SecondsBetweenAttacks(), Ordinary->SecondsBetweenAttacks()),
		Brute->SecondsBetweenAttacks() < Ordinary->SecondsBetweenAttacks());

	// AND IT STILL CLEARS ITS OWN SWING ANIMATION. Attack_Biped_Melee_A is
	// 1.0000 seconds long and nothing rate-scales it, so an interval under a
	// second would start a swing the creature had not finished.
	TestTrue(FString::Printf(
		TEXT("and its interval still clears the 1.0 s swing clip (%.2f s)"),
		Brute->SecondsBetweenAttacks()),
		Brute->SecondsBetweenAttacks() >= 1.0f);

	TestTrue(TEXT("a Brute stands taller than an ordinary enemy"),
		Brute->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()
			> BaseEnemyCapsuleHalfHeight);

	Ordinary->Destroy();
	Brute->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteCanActuallyReachThePlayer,
	"Cataclysm.Brute.CanActuallyReachThePlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteCanActuallyReachThePlayer::RunTest(const FString&)
{
	using namespace CataclysmBruteTest;

	// ARITHMETIC, NOT A WORLD. This is the check that the Brute's reach is
	// reachable at all, and it is why ACataclysmEnemyController::Think measures
	// floor-plane distance rather than 3D distance.
	//
	// Two capsules touching are their two radii apart on the floor plane. Their
	// centres are not at the same height: a player's capsule is 96 cm
	// half-height and a BRUTE'S IS 110, so the Brute's centre is 14 cm ABOVE the
	// player's.
	//
	// THE BRUTE'S OWN HALF-HEIGHT, NOT THE BASE ENEMY'S. Until 2026-08-08 this
	// test used the 80 cm from ACataclysmEnemyCharacter, which is the wrong
	// capsule for the creature the test is named after. It gave a 16 cm gap
	// where the Brute's is 14, and a 91.41 cm 3D distance where the Brute's is
	// 91.08. Both still cleared the assertion below, so the test passed while
	// describing a creature that does not exist.
	const float ContactHorizontal =
		ACataclysmBruteCharacter::BruteCapsuleRadius + PlayerCapsuleRadius;
	const float HeightDifference =
		ACataclysmBruteCharacter::BruteCapsuleHalfHeight - PlayerCapsuleHalfHeight;
	const float Contact3D = FMath::Sqrt(
		ContactHorizontal * ContactHorizontal
		+ HeightDifference * HeightDifference);

	const float Reach = ACataclysmBruteCharacter::DesignedMeleeReachCm;

	TestEqual(TEXT("contact on the floor plane is exactly the designed reach"),
		ContactHorizontal, Reach);

	TestTrue(
		TEXT("3D distance at contact exceeds the reach, which is why the "
			 "controller measures the floor plane"),
		Contact3D > Reach);

	// AND THE HONEST PART, WHICH THIS TEST DID NOT SAY BEFORE. Measuring in 3D
	// would no longer actually stop the Brute attacking, because Think() chases
	// only when the distance exceeds the reach PLUS ContactToleranceCm, and that
	// tolerance is 2 cm against a 1.08 cm excess.
	//
	// The tolerance did not exist when issue #373 was written: the floor-plane
	// change landed in pull request #367 and the tolerance in #385 and #388.
	// TWO MECHANISMS NOW COVER THE SAME GAP, so neither can be caught by a
	// behaviour test on its own. Cataclysm.Enemy.EveryHeightGapFitsInsideTheContactTolerance
	// is the guard for the day that stops being true.
	TestTrue(
		TEXT("and the contact tolerance currently absorbs that excess, so the "
			 "3D distance would still be treated as in reach"),
		Contact3D <= Reach + ACataclysmEnemyController::ContactToleranceCm);

	// And the same check for the training dummy, showing why this never showed
	// up before: its reach has over a metre of margin.
	constexpr float OrdinaryReach = 200.0f;
	TestTrue(TEXT("an ordinary enemy had margin to spare either way"),
		Contact3D < OrdinaryReach);

	return true;
}

namespace CataclysmBruteTest
{
	/** A number a Blueprint variable holds, whether it is a float or a double. */
	static bool ReadNumber(const UObject* Object, const TCHAR* Name, double& Out)
	{
		// FNumericProperty RATHER THAN FFloatProperty. Blueprint reals are
		// doubles in Unreal 5, and asking for a float finds nothing at all --
		// which would read as "the graph has no such variable" rather than as
		// the wrong question.
		const FNumericProperty* Property =
			FindFProperty<FNumericProperty>(Object->GetClass(), Name);
		if (!Property)
		{
			return false;
		}
		Out = Property->GetFloatingPointPropertyValue(
			Property->ContainerPtrToValuePtr<void>(Object));
		return true;
	}

	/** A boolean a Blueprint variable holds. */
	static bool ReadFlag(const UObject* Object, const TCHAR* Name, bool& Out)
	{
		const FBoolProperty* Property =
			FindFProperty<FBoolProperty>(Object->GetClass(), Name);
		if (!Property)
		{
			return false;
		}
		Out = Property->GetPropertyValue_InContainer(Object);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTheAnimationGraphRunsAndReadsTheCreaturesSpeed,
	"Cataclysm.Brute.TheAnimationGraphRunsAndReadsTheCreaturesSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheAnimationGraphRunsAndReadsTheCreaturesSpeed::RunTest(const FString&)
{
	using namespace CataclysmBruteTest;

	// WHAT THIS IS FOR. Issue #374 says no automated test ever runs the
	// animation graph, because doing so hung the test process and never
	// returned. That was measured against the PACK'S graph,
	// Rampage_AnimBlueprint, which casts its owning pawn to the Paragon
	// character class it was written for. The Brute has used a
	// project-authored graph, ABP_Brute, since 2026-08-07, and this test
	// establishes by running it that the hang does not happen with that one.
	//
	// EVALUATED, NOT ONLY BOUND. Cataclysm.Brute.RunsItsAnimationBlueprint
	// already checks that the mesh is in animation-Blueprint mode and that the
	// class is ABP_Brute. Binding a class is not running a graph, and the hang
	// issue #374 records came from evaluation. This one ticks it and reads back
	// the three variables its event graph computes.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute spawned"), Brute))
	{
		return false;
	}

	if (FSoftObjectPath(ACataclysmBruteCharacter::BodyMeshPath).TryLoad() == nullptr)
	{
		AddInfo(TEXT("Paragon Rampage pack is not installed, so there is no "
					 "skeleton to run a graph on. Expected in continuous "
					 "integration."));
		Brute->Destroy();
		return true;
	}

	if (!TestTrue(TEXT("the body and its animation Blueprint resolved"),
		Brute->ResolveBody(/*bIncludeAnimation=*/true)))
	{
		return false;
	}

	USkeletalMeshComponent* Mesh = Brute->GetMesh();
	if (!TestNotNull(TEXT("mesh component"), Mesh))
	{
		return false;
	}

	UAnimInstance* Graph = Mesh->GetAnimInstance();
	if (!TestNotNull(TEXT("the graph has an instance to run"), Graph))
	{
		return false;
	}

	// THE THRESHOLDS THE GRAPH ACTUALLY CARRIES, read out of the asset for
	// issue #430 and recorded in game/Data/animation_graph_readings.json:
	//
	//     bMoving  = ground speed > 10
	//     bChasing = ground speed > 375
	//
	// Written here as literals on purpose. This test is the other end of that
	// reading: the record says what the asset contains, and this says the
	// running graph behaves that way.
	struct FCase
	{
		const TCHAR* What;
		float SpeedCmPerSecond;
		bool bExpectMoving;
		bool bExpectChasing;
	};

	const FCase Cases[] = {
		{TEXT("standing still"), 0.0f, false, false},
		{TEXT("wandering at its designed walk speed"), 250.0f, true, false},
		{TEXT("chasing at its designed chase speed"), 500.0f, true, true},
	};

	for (const FCase& Case : Cases)
	{
		Brute->GetCharacterMovement()->Velocity =
			FVector(Case.SpeedCmPerSecond, 0.0f, 0.0f);

		// THIS IS THE LINE ISSUE #374 IS ABOUT. With the pack's own graph the
		// process stopped here and never returned.
		Mesh->TickAnimation(0.1f, /*bNeedsValidRootMotion=*/false);

		double GroundSpeed = -1.0;
		bool bMoving = false;
		bool bChasing = false;

		if (!TestTrue(FString::Printf(
				TEXT("%s: the graph has a GroundSpeed variable"), Case.What),
			ReadNumber(Graph, TEXT("GroundSpeed"), GroundSpeed))
			|| !TestTrue(FString::Printf(
				TEXT("%s: the graph has a bMoving variable"), Case.What),
			ReadFlag(Graph, TEXT("bMoving"), bMoving))
			|| !TestTrue(FString::Printf(
				TEXT("%s: the graph has a bChasing variable"), Case.What),
			ReadFlag(Graph, TEXT("bChasing"), bChasing)))
		{
			return false;
		}

		TestEqual(FString::Printf(
			TEXT("%s: the graph read the creature's ground speed"), Case.What),
			static_cast<float>(GroundSpeed), Case.SpeedCmPerSecond, 1.0f);

		TestEqual(FString::Printf(TEXT("%s: bMoving"), Case.What),
			bMoving, Case.bExpectMoving);
		TestEqual(FString::Printf(TEXT("%s: bChasing"), Case.What),
			bChasing, Case.bExpectChasing);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmEveryHeightGapFitsInsideTheContactTolerance,
	"Cataclysm.Brute.EveryHeightGapFitsInsideTheContactTolerance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEveryHeightGapFitsInsideTheContactTolerance::RunTest(const FString&)
{
	using namespace CataclysmBruteTest;

	// WHAT THIS IS FOR, AND IT IS NOT WHAT IT LOOKS LIKE. Issue #373 changed
	// Think() from a 3D distance to a floor-plane one, because a Brute pressed
	// against a player measured further away than its own reach. That was true
	// when it was written. It is not true now: ContactToleranceCm arrived
	// afterwards and is 2 cm, which is wider than any height gap in the project
	// costs at contact.
	//
	// SO NEITHER MECHANISM IS LOAD-BEARING ON ITS OWN TODAY, and no behaviour
	// test can catch either being removed. Measured on 2026-08-08: changing
	// FVector::Dist2D back to FVector::Dist in the controller left all 204
	// automation tests passing.
	//
	// THIS TEST IS THE TRIPWIRE FOR THE DAY THAT CHANGES. A taller creature, a
	// wider one, or a smaller tolerance makes the choice matter again, and then
	// removing the floor-plane measurement really would stop that creature ever
	// landing a hit. When this fails, the answer is not to widen the tolerance:
	// it is to note that Think() measuring the floor plane has become
	// load-bearing and to guard it as such.
	const float Tolerance = ACataclysmEnemyController::ContactToleranceCm;

	struct FCreature
	{
		const TCHAR* Name;
		float CapsuleRadius;
		float CapsuleHalfHeight;
		float Reach;
	};

	// EVERY ENEMY CLASS THAT SETS ITS OWN CAPSULE. The base class is what the
	// other six vertical slice enemies will use until each gets its own.
	const FCreature Creatures[] = {
		{TEXT("the base enemy"), BaseEnemyCapsuleRadius,
		 BaseEnemyCapsuleHalfHeight, 200.0f},
		{TEXT("the Brute"), ACataclysmBruteCharacter::BruteCapsuleRadius,
		 ACataclysmBruteCharacter::BruteCapsuleHalfHeight,
		 ACataclysmBruteCharacter::DesignedMeleeReachCm},
	};

	for (const FCreature& Creature : Creatures)
	{
		const float ContactHorizontal = Creature.CapsuleRadius + PlayerCapsuleRadius;
		const float HeightGap =
			FMath::Abs(Creature.CapsuleHalfHeight - PlayerCapsuleHalfHeight);
		const float Contact3D = FMath::Sqrt(
			ContactHorizontal * ContactHorizontal + HeightGap * HeightGap);

		// THE EXCESS THE HEIGHT COSTS, which is what the tolerance has to cover.
		const float Excess = Contact3D - ContactHorizontal;

		TestTrue(
			FString::Printf(
				TEXT("%s: standing at contact costs %.2f cm of extra distance "
					 "when measured in 3D, and the contact tolerance is %.2f cm"),
				Creature.Name, Excess, Tolerance),
			Excess <= Tolerance);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteNoticesLaterThanAnOrdinaryEnemy,
	"Cataclysm.Brute.NoticesLaterThanAnOrdinaryEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteNoticesLaterThanAnOrdinaryEnemy::RunTest(const FString&)
{
	using namespace CataclysmBruteTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Ordinary = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(0.0f, 5000.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute spawned"), Brute)
		|| !TestNotNull(TEXT("ordinary enemy spawned"), Ordinary))
	{
		return false;
	}
	ON_SCOPE_EXIT { Brute->Destroy(); Ordinary->Destroy(); };

	// THE FIGURE IS PLAY-TESTED, NOT DERIVED, so there is no arithmetic to
	// restate here. It was 700, derived as one attack cycle of walking, and
	// that derivation stopped being true on 2026-08-07 when the attack interval
	// moved to 1.6 and the chase speed to 500. What can still be checked is
	// what the number has to do, which is the rest of this test.
	TestEqual(TEXT("the Brute uses its own notice radius"),
		Brute->SightRadiusCm(), ACataclysmBruteCharacter::BruteNoticeRadiusCm);

	// STRICTLY SHORTER THAN WHAT IT INHERITED, stated as an inequality so that
	// the test says what the change was for rather than repeating a number.
	TestTrue(FString::Printf(
		TEXT("a Brute notices strictly later than an ordinary enemy (%.0f vs %.0f)"),
		Brute->SightRadiusCm(), Ordinary->SightRadiusCm()),
		Brute->SightRadiusCm() < Ordinary->SightRadiusCm());

	// IT STILL NOTICES BEFORE IT CAN BE HIT, which is the floor on how small
	// this may go. An enemy whose notice radius was inside its own reach could
	// be stood next to without ever waking up.
	TestTrue(TEXT("and it notices from much further than it can reach"),
		Brute->SightRadiusCm() > Brute->AttackReachCm() * 2.0f);

	// AND IT ROAMS A SHORTER DISTANCE THAN IT SEES. A character that wandered
	// further from its anchor than it can notice would leave the ground it is
	// meant to be holding without any way of knowing.
	TestTrue(FString::Printf(
		TEXT("it roams less far than it sees (%.0f vs %.0f)"),
		Brute->RoamRadiusCm(), Brute->SightRadiusCm()),
		Brute->RoamRadiusCm() < Brute->SightRadiusCm());

	// THE ONE HARD BOUND ON THE NUMBER. ACataclysmGameMode spawns the Brute
	// 1200 cm from the player start. Any notice radius at or above that means
	// it sees the player as the level opens, never wanders, and none of the
	// roaming behaviour can be observed at all -- which is what the inherited
	// 1500 did. At 1000 there is 200 cm of margin, and this is the assertion
	// that fails first if the radius creeps back up.
	constexpr float SandboxSpawnDistanceCm = 1200.0f;
	TestTrue(FString::Printf(
		TEXT("a Brute does NOT notice a player standing at the player start, "
			 "%.0f cm away (it notices from %.0f)"),
		SandboxSpawnDistanceCm, Brute->SightRadiusCm()),
		Brute->SightRadiusCm() < SandboxSpawnDistanceCm);
	TestTrue(TEXT("whereas an ordinary enemy at that distance would"),
		Ordinary->SightRadiusCm() > SandboxSpawnDistanceCm);

	// AND ITS ROAMING STAYS ON THE FLOOR. The sandbox navigation bounds are
	// 4000 cm across centred on the origin, so a Brute spawned 1200 cm out has
	// 800 cm to the edge, less the capsule radius Recast insets by.
	constexpr float FloorExtentCm = 4000.0f;
	const float HeadroomCm = FloorExtentCm / 2.0f - SandboxSpawnDistanceCm
		- ACataclysmBruteCharacter::BruteCapsuleRadius;
	TestTrue(FString::Printf(
		TEXT("its roam radius fits between the spawn and the bounds "
			 "(%.0f cm of %.0f available)"),
		Brute->RoamRadiusCm(), HeadroomCm),
		Brute->RoamRadiusCm() < HeadroomCm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteFightsWithOrWithoutItsArt,
	"Cataclysm.Brute.FightsWithOrWithoutItsArt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteFightsWithOrWithoutItsArt::RunTest(const FString&)
{
	using namespace CataclysmBruteTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute spawned"), Brute))
	{
		return false;
	}
	Brute->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Brute->SetHealth(11000.0f);
	Brute->SetAttackDamage(35.0f);

	// PROVES THE ABILITY SYSTEM CAME UP. An enemy whose attribute sets never
	// registered has no health to read.
	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Brute);
	if (!TestNotNull(TEXT("ability system"), AbilitySystem))
	{
		return false;
	}
	TestEqual(TEXT("health was applied"),
		AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute()),
		11000.0f);

	TestEqual(TEXT("it is on the monsters' side"),
		Brute->GetGenericTeamId().GetId(),
		UCataclysmTeams::IdFor(ECataclysmTeam::Monsters).GetId());

	// THE ART IS OPTIONAL AND THIS TEST SAYS SO RATHER THAN ASSUMING EITHER WAY.
	// The Paragon packs are excluded by .gitignore, so on a fresh clone and in
	// continuous integration the mesh is absent and that is not a failure.
	const bool bArtIsInstalled =
		FSoftObjectPath(ACataclysmBruteCharacter::BodyMeshPath).ResolveObject() != nullptr
		|| FSoftObjectPath(ACataclysmBruteCharacter::BodyMeshPath).TryLoad() != nullptr;

	if (bArtIsInstalled)
	{
		// CALLED DIRECTLY RATHER THAN RELYING ON BeginPlay. BeginPlay calls this
		// too, but whether it fires depends on how the world under test was
		// built, and reading the mesh afterwards cannot tell "the art failed to
		// load" apart from "BeginPlay never ran". The return value can.
		//
		// WITHOUT THE ANIMATIONS, because this half of the test is about whether
		// the Brute wears the mesh. The animations get their own test below.
		const bool bResolved = Brute->ResolveBody(/*bIncludeAnimation=*/false);
		TestTrue(TEXT("the Brute resolved its body when the art is installed"),
			bResolved);

		const USkeletalMeshComponent* MeshComponent = Brute->GetMesh();
		if (TestNotNull(TEXT("mesh component"), MeshComponent))
		{
			TestNotNull(TEXT("the Rampage mesh is on the Brute"),
				MeshComponent->GetSkeletalMeshAsset());
		}
	}
	else
	{
		AddInfo(TEXT("Paragon Rampage pack is not installed, so the mesh test "
					 "is skipped. The Brute keeps its placeholder cylinder. "
					 "This is the expected state in continuous integration."));
	}

	// EITHER WAY IT MUST STILL BE A FIGHTABLE ENEMY. This is what makes the
	// skip above honest rather than a hole: whether or not the art loaded, the
	// Brute is findable by the targeting system from the other side.
	ACataclysmEnemyCharacter* Hero = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(50.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("opposing character"), Hero))
	{
		return false;
	}
	Hero->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	Hero->SetHealth(1000.0f);

	TArray<AActor*> Found = UCataclysmTargeting::FindEnemiesInSphere(
		World, Hero, Hero->GetActorLocation(), 500.0f);
	TestTrue(TEXT("the Brute is a valid enemy target"), Found.Contains(Brute));

	Hero->Destroy();
	Brute->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteIsDrivenByItsOwnAnimationBlueprint,
	"Cataclysm.Brute.IsDrivenByItsOwnAnimationBlueprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteIsDrivenByItsOwnAnimationBlueprint::RunTest(const FString&)
{
	using namespace CataclysmBruteTest;

	// WHAT THIS GUARDS, AND WHAT IT DELIBERATELY DOES NOT.
	//
	// It guards the wiring: that ABP_Brute's generated class loads, that the
	// mesh component is put into animation Blueprint mode, and that the class
	// it is given is that one. Those are the three things that would silently
	// leave the Brute holding its reference pose.
	//
	// It does NOT run the animation graph. That is now a division of labour
	// rather than a restriction: Cataclysm.Brute.TheAnimationGraphRunsAndReadsTheCreaturesSpeed
	// ticks the graph and reads back what its event graph computes.
	//
	// Issue #374 recorded a Paragon animation graph instantiated inside a world
	// built by UWorld::CreateWorld hanging the test process for over three
	// minutes. That was Rampage_AnimBlueprint, the pack's own, which casts its
	// owning pawn to the Paragon character class it was written for. ABP_Brute
	// is this project's and does not.
	//
	// Whether the poses LOOK right is still answered by watching a
	// Play-In-Editor session, not here.
	//
	// It replaced Cataclysm.Brute.AnimatesInsteadOfSliding, which asserted the
	// opposite setting -- that the component was in single-node mode -- because
	// that was how the Brute was driven until 2026-08-08. The fault that test
	// was written against, a creature sliding in a fixed pose, is still guarded:
	// a Brute with no animation class is exactly that creature.

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute spawned"), Brute))
	{
		return false;
	}

	const bool bArtIsInstalled =
		FSoftObjectPath(ACataclysmBruteCharacter::BodyMeshPath).TryLoad() != nullptr;
	if (!bArtIsInstalled)
	{
		AddInfo(TEXT("Paragon Rampage pack is not installed, so the animation "
					 "Blueprint test is skipped. Expected in continuous "
					 "integration."));
		Brute->Destroy();
		return true;
	}

	// THE GENERATED CLASS, LOADED THE SAME WAY THE CHARACTER LOADS IT. A wrong
	// path, a missing _C suffix or a deleted asset all show up here rather than
	// as a creature standing still in a level.
	UClass* Expected = FSoftClassPath(
		ACataclysmBruteCharacter::AnimationBlueprintPath)
			.TryLoadClass<UAnimInstance>();
	if (!TestNotNull(TEXT("ABP_Brute's generated class loads"), Expected))
	{
		return false;
	}

	TestTrue(TEXT("the body and its animation Blueprint resolved"),
		Brute->ResolveBody(/*bIncludeAnimation=*/true));

	USkeletalMeshComponent* MeshComponent = Brute->GetMesh();
	if (!TestNotNull(TEXT("mesh component"), MeshComponent))
	{
		return false;
	}

	// AN ANIMATION BLUEPRINT, NOT A SINGLE ANIMATION. This is the setting the
	// whole approach rests on: single-node mode plays exactly one clip and
	// cannot blend, which is what made every ability cut between its wind-up
	// and its release.
	TestEqual(TEXT("the mesh runs an animation graph rather than one clip"),
		static_cast<int32>(MeshComponent->GetAnimationMode()),
		static_cast<int32>(EAnimationMode::AnimationBlueprint));

	TestEqual(TEXT("and the graph it runs is ABP_Brute"),
		MeshComponent->GetAnimClass(), Expected);

	// THE ATTACK ANIMATIONS ARE STILL THIS CLASS'S JOB, because they are played
	// into the graph's slot rather than selected by it. All three, so a broken
	// path in any one of them is named here.
	//
	// THREE, NOT SIX, SINCE 2026-08-08. Each ability's wind-up and release now
	// live inside one montage rather than being loaded and sequenced separately.
	TestNotNull(TEXT("the swing clip loaded"), Brute->AttackAnimation.Get());
	TestNotNull(TEXT("the stomp montage loaded"), Brute->StompMontage.Get());
	TestNotNull(TEXT("the rock throw montage loaded"),
		Brute->RockThrowMontage.Get());

	Brute->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteAsksForASlotThatItsGraphHas,
	"Cataclysm.Brute.AsksForASlotThatItsGraphHas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteAsksForASlotThatItsGraphHas::RunTest(const FString&)
{
	// WHAT THIS GUARDS. An attack is played with
	// PlaySlotAnimationAsDynamicMontage into a slot named by AttackSlotName.
	// A montage played into a slot the graph does not have is dropped without
	// an error, so a renamed Slot node in ABP_Brute would make every attack
	// invisible and nothing would say why.
	//
	// IT CHECKS THE NAME, NOT THE PLAYBACK. The name is half the contract and
	// it is the half that can be checked without art; the other half is that
	// ABP_Brute's Slot node is still called this, which needs the graph and the
	// skeleton. That the graph can be run at all is settled -- see
	// Cataclysm.Brute.TheAnimationGraphRunsAndReadsTheCreaturesSpeed and issue
	// #374 -- so the missing half is reachable work rather than a wall.
	TestEqual(TEXT("attacks are played into DefaultSlot"),
		ACataclysmBruteCharacter::AttackSlotName, FName(TEXT("DefaultSlot")));

	// AND THE TWO BLEND TIMES ARE NOT ZERO, which is the whole difference from
	// the single-node mode this replaced. Zero blend is a one-frame cut.
	TestTrue(TEXT("an attack blends in rather than cutting"),
		ACataclysmBruteCharacter::AttackBlendInSeconds > 0.0f);
	TestTrue(TEXT("and blends back out to locomotion"),
		ACataclysmBruteCharacter::AttackBlendOutSeconds > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmBruteAbilityMontagesAreBuiltCorrectly,
	"Cataclysm.Brute.ItsAbilityMontagesAreBuiltCorrectly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBruteAbilityMontagesAreBuiltCorrectly::RunTest(const FString&)
{
	// WHAT THIS GUARDS, AND WHY IT IS THE MOST IMPORTANT TEST OF THE THREE HERE.
	//
	// The Brute's two abilities are now driven by montage assets rather than by
	// arithmetic in C++. That is the right place for them -- the timing can be
	// scrubbed against a live preview -- but it moves several tuned numbers into
	// binary files that a pull request cannot diff. Issue #406 made exactly that
	// complaint about the two play rates that moved into ABP_Brute. Those are
	// covered a different way now: tools/read_animation_graph.py reads them out
	// of the asset into a committed record, and three tests run from the design
	// model through to the asset's own SHA-256. This test is the montage
	// equivalent, and reads its figures straight off the assets instead.
	//
	// So everything this project relies on being true of those two assets is
	// asserted here, read back off the asset itself:
	//
	//   the slot they play into      -- wrong, and every ability is invisible
	//   the two clips and their order-- wrong, and the creature throws a punch
	//                                   when it meant to throw a rock
	//   where the join falls         -- wrong, and the damage lands at a moment
	//                                   that does not match what is on screen
	//   the blend settings           -- wrong, and the poised pose dissolves
	//                                   before the attack lands, which is the
	//                                   fault pull request #410 fixed
	//
	// tools/generate_brute_montages.py writes all of them. This is what stops it
	// and this code disagreeing.
	struct FCase
	{
		const TCHAR* What;
		int32 Ability;
		const TCHAR* Path;
		const TCHAR* FirstClip;
		const TCHAR* SecondClip;
		float WindUpSeconds;
		float JoinSeconds;
		float TotalSeconds;
	};

	// THE MEASURED FIGURES. The clip lengths were taken from the pack on
	// 2026-08-08 and are reported by tools/generate_brute_montages.py on every
	// run. Where each blow lands inside its release clip was measured separately
	// with tools/measure_animation_impact.py, because the clips carry no
	// animation notifies and nothing else records it.
	const FCase Cases[] = {
		{TEXT("the stomp"), ACataclysmBruteCharacter::StompAbility,
		 ACataclysmBruteCharacter::StompMontagePath,
		 TEXT("Ability_GroundSmash_Start"), TEXT("Ability_GroundSmash_End"),
		 ACataclysmBruteCharacter::StompWindUpSeconds, 0.8333f, 1.5333f},
		{TEXT("the rock throw"), ACataclysmBruteCharacter::RockThrowAbility,
		 ACataclysmBruteCharacter::RockThrowMontagePath,
		 TEXT("Ability_RipNToss_Rip"), TEXT("Ability_RipNToss_Toss"),
		 ACataclysmBruteCharacter::RockThrowWindUpSeconds, 1.1333f, 2.0000f},
	};

	// A HUNDREDTH OF A SECOND. The clips are authored at 30 frames a second, so
	// a third of a frame is as close as any of these figures can be stated.
	const float Tolerance = 0.01f;

	int32 Checked = 0;
	for (const FCase& Case : Cases)
	{
		UAnimMontage* Montage =
			Cast<UAnimMontage>(FSoftObjectPath(Case.Path).TryLoad());
		if (!Montage)
		{
			continue;
		}

		// THE ART TEST IS THE CLIP, NOT THE MONTAGE, AND THAT DISTINCTION IS THE
		// WHOLE REASON THIS LOOP IS SHAPED THIS WAY. The two montage assets are
		// committed; the Paragon clips inside them are gitignored. So on a fresh
		// clone and in continuous integration the montage loads perfectly well
		// and every segment's animation reference is null. Testing the montage
		// for null would report the art as present and then compare figures that
		// cannot be right.
		const bool bHasSegments =
			Montage->SlotAnimTracks.Num() > 0 &&
			Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() > 0;
		if (!bHasSegments ||
			Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0]
				.GetAnimReference() == nullptr)
		{
			continue;
		}

		++Checked;

		const FSlotAnimationTrack& Track = Montage->SlotAnimTracks[0];

		// THE SLOT, WHICH IS THE ONE THAT FAILS SILENTLY. A montage played into
		// a slot the animation graph does not contain is dropped with no error
		// of any kind -- the creature simply never plays it. ABP_Brute's Slot
		// node and AttackSlotName are the other two corners of that agreement.
		TestEqual(FString::Printf(
			TEXT("%s plays into the slot the graph has"), Case.What),
			Track.SlotName, ACataclysmBruteCharacter::AttackSlotName);

		if (!TestEqual(FString::Printf(
				TEXT("%s holds exactly two clips"), Case.What),
				Track.AnimTrack.AnimSegments.Num(), 2))
		{
			continue;
		}

		// IN ORDER: THE WIND-UP, THEN THE RELEASE. Reversed, the creature would
		// smash the ground and then raise its fist.
		TestEqual(FString::Printf(TEXT("%s winds up first"), Case.What),
			Track.AnimTrack.AnimSegments[0].GetAnimReference()->GetName(),
			FString(Case.FirstClip));
		TestEqual(FString::Printf(TEXT("%s releases second"), Case.What),
			Track.AnimTrack.AnimSegments[1].GetAnimReference()->GetName(),
			FString(Case.SecondClip));

		// BACK TO BACK WITH NO GAP. The second segment starts exactly where the
		// first ends, which is what UAnimMontage::PostLoad's call to
		// FAnimTrack::ValidateSegmentTimes arranges and what makes the two clips
		// one continuous movement rather than two.
		TestEqual(FString::Printf(TEXT("%s starts its wind-up at zero"), Case.What),
			Track.AnimTrack.AnimSegments[0].StartPos, 0.0f, Tolerance);
		TestEqual(FString::Printf(
			TEXT("%s begins its release exactly where the wind-up ends"), Case.What),
			Track.AnimTrack.AnimSegments[1].StartPos, Case.JoinSeconds, Tolerance);

		// AND BOTH AT AUTHORED SPEED. A play rate baked into a segment would be a
		// second place speed is decided, competing with the montage's own rate.
		TestEqual(FString::Printf(
			TEXT("%s plays its wind-up at authored speed"), Case.What),
			Track.AnimTrack.AnimSegments[0].AnimPlayRate, 1.0f, Tolerance);
		TestEqual(FString::Printf(
			TEXT("%s plays its release at authored speed"), Case.What),
			Track.AnimTrack.AnimSegments[1].AnimPlayRate, 1.0f, Tolerance);

		TestEqual(FString::Printf(TEXT("%s is as long as its two clips"), Case.What),
			Montage->GetPlayLength(), Case.TotalSeconds, Tolerance);

		TestEqual(FString::Printf(
			TEXT("%s reports the join the character reads"), Case.What),
			ACataclysmBruteCharacter::JoinSecondsFor(Montage), Case.JoinSeconds,
			Tolerance);

		// THE BLEND SETTINGS THE ASSET CARRIES. Montage_Play takes no blend
		// arguments -- it reads BlendIn, BlendOut and BlendOutTriggerTime off the
		// montage -- so these are the live values, not documentation.
		TestEqual(FString::Printf(TEXT("%s blends in over the same time a swing "
									   "does"), Case.What),
			Montage->BlendIn.GetBlendTime(),
			ACataclysmBruteCharacter::AttackBlendInSeconds, Tolerance);
		TestEqual(FString::Printf(TEXT("%s blends out over the same time"), Case.What),
			Montage->BlendOut.GetBlendTime(),
			ACataclysmBruteCharacter::AttackBlendOutSeconds, Tolerance);

		// AND PLAYS TO ITS LAST FRAME FIRST. At the engine's default of -1 the
		// blend FINISHES as the montage ends, so it STARTS a blend-length before
		// the end and the last 0.15 seconds of the release dissolves into
		// walking. That is the fault pull request #410 fixed, and it now lives in
		// the asset where it can come back without a code change.
		TestEqual(FString::Printf(
			TEXT("%s plays to its last frame before blending out"), Case.What),
			Montage->BlendOutTriggerTime,
			ACataclysmBruteCharacter::AbilityBlendOutTriggerTime, Tolerance);

		// THE PROPERTY THE WHOLE DESIGN RESTS ON: the blow you can see arrives at
		// the moment the damage is dealt.
		//
		// THIS IS THE ASSERTION THAT WOULD HAVE CAUGHT THE FAULT THE PROJECT
		// OWNER REPORTED ON 2026-08-08. The first version of this code assumed
		// the release clip begins at the moment of impact. It does not: the
		// creature's fists are still overhead when it starts, and take 0.179
		// seconds to reach the ground. Lining up the join instead of the blow
		// left 0.488 seconds of telegraph uncovered, which was filled by freezing
		// the montage on one frame -- "he reaches his arms up in the air, freezes
		// for a second or so, then continues to slamming down".
		const float LandsAt =
			ACataclysmBruteCharacter::LandsAtSecondsFor(Case.WindUpSeconds);
		const float Impact =
			ACataclysmBruteCharacter::ImpactSecondsFor(Montage, Case.Ability);
		const float Rate =
			ACataclysmBruteCharacter::MontageRateFor(Impact, LandsAt);
		const float Delay =
			ACataclysmBruteCharacter::MontageDelaySecondsFor(Montage, Case.Ability);

		// The blow is measured from the start of the telegraph: wait out the
		// delay, then play until the montage reaches its strike.
		const float BlowArrivesAt = Delay + Impact / Rate;

		TestEqual(FString::Printf(
			TEXT("%s strikes at the moment its damage is dealt "
				 "(blow at %.3f s, damage at %.3f s)"),
			Case.What, BlowArrivesAt, LandsAt),
			BlowArrivesAt, LandsAt, Tolerance);

		// AND THE STRIKE IS INSIDE THE RELEASE CLIP, NOT AT ITS EDGE. Stated as a
		// comparison rather than a number so that it fails if anyone sets the
		// measured constant back to zero, which is the assumption that was wrong.
		TestTrue(FString::Printf(
			TEXT("%s strikes after its join rather than on it"), Case.What),
			Impact > Case.JoinSeconds + 0.001f);

		TestTrue(FString::Printf(
			TEXT("%s strikes before its montage ends"), Case.What),
			Impact < Montage->GetPlayLength());

		// NEVER SLOWER THAN AUTHORED, which is the other half of the same rule.
		// Stretching an animation to fill its telegraph was tried and reported
		// from a play session as slow motion; waiting before starting is what
		// fills the gap instead.
		TestTrue(FString::Printf(
			TEXT("%s is never played slower than it was authored"), Case.What),
			Rate >= 1.0f);
	}

	if (Checked == 0)
	{
		AddInfo(TEXT("The Paragon art is absent, so the montage contents could "
					 "not be checked. Expected in continuous integration; the "
					 "montage assets are committed but the clips inside them "
					 "are not."));
	}
	else if (Checked != UE_ARRAY_COUNT(Cases))
	{
		// A HARD ERROR RATHER THAN A SKIP. One montage resolving and the other
		// not means a broken path or a half-built pair, not a missing pack.
		AddError(TEXT("One ability montage resolved its clips and the other did "
					  "not, which means one of the two is wrong rather than the "
					  "art being absent. Re-run tools/generate_brute_montages.py."));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
