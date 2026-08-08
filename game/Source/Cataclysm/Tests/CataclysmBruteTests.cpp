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
	TestTrue(TEXT("a Brute attacks strictly less often than an ordinary enemy"),
		Brute->SecondsBetweenAttacks() > Ordinary->SecondsBetweenAttacks());
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
	// reachable at all, and it is the reason ACataclysmEnemyController::Think
	// measures horizontal distance rather than 3D distance.
	//
	// Two capsules touching are their two radii apart on the floor plane. Their
	// centres are not at the same height, because a player's capsule is 96 cm
	// half-height and an enemy's is 80.
	const float ContactHorizontal =
		ACataclysmBruteCharacter::BruteCapsuleRadius + PlayerCapsuleRadius;
	const float HeightDifference =
		PlayerCapsuleHalfHeight - BaseEnemyCapsuleHalfHeight;
	const float Contact3D = FMath::Sqrt(
		ContactHorizontal * ContactHorizontal
		+ HeightDifference * HeightDifference);

	const float Reach = ACataclysmBruteCharacter::DesignedMeleeReachCm;

	TestEqual(TEXT("contact on the floor plane is exactly the designed reach"),
		ContactHorizontal, Reach);

	// THE GUARD. If someone changes the controller back to a 3D distance, this
	// records what breaks: the Brute pressed against the player measures further
	// away than its own reach and chases for ever.
	TestTrue(
		TEXT("3D distance at contact EXCEEDS the reach, which is why the "
			 "controller must not use it"),
		Contact3D > Reach);

	// And the same check for the training dummy, showing why this never showed
	// up before: its reach has 110 cm of margin.
	constexpr float OrdinaryReach = 200.0f;
	TestTrue(TEXT("an ordinary enemy had margin to spare either way"),
		Contact3D < OrdinaryReach);

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
	// It does NOT run the animation graph, and that is on purpose. Issue #374
	// records a Paragon animation graph instantiated inside a world built by
	// UWorld::CreateWorld hanging the test process for over three minutes.
	// Whether the poses actually blend is answered by watching it in a
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

	// THE ATTACK CLIPS ARE STILL THIS CLASS'S JOB, because they are played into
	// the graph's slot rather than selected by it. All four, so a broken path
	// in any one of them is named here.
	TestNotNull(TEXT("the swing clip loaded"), Brute->AttackAnimation.Get());
	TestNotNull(TEXT("the stomp wind-up clip loaded"), Brute->StompAnimation.Get());
	TestNotNull(TEXT("the stomp release clip loaded"),
		Brute->StompReleaseAnimation.Get());
	TestNotNull(TEXT("the rock throw wind-up clip loaded"),
		Brute->RockThrowAnimation.Get());

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
	// IT CHECKS THE NAME, NOT THE PLAYBACK, for the reason the test above
	// gives: instantiating the graph in a synthetic world is issue #374. The
	// name is half the contract and it is the half that can be checked cheaply;
	// the other half is that ABP_Brute's Slot node is still called this, which
	// is verified by playing it.
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

#endif // WITH_AUTOMATION_TESTS
