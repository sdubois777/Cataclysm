// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "EngineUtils.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for the Abyssal Warden, the mini-boss of the Demonic vertical slice.
 *
 * WHAT THESE GUARD, and each is something this creature can fail at silently:
 *
 * 1. Being an Abyssal Warden rather than an inherited training dummy. Every
 *    assertion on a designed figure also asserts it differs from the base
 *    enemy's, because a test that would pass on the base class proves nothing.
 *
 * 2. Offering its ring and its charge, IN THAT ORDER. ChooseAbility takes the
 *    first entry whose range and cooldown fit and never looks at the shape, so
 *    listing the 5 second charge before the 12 second ring would crowd the ring
 *    out of the band where both are legal. Issue #491.
 *
 * 2a. The charge going where it said it would, at the speed it said, hitting
 *    what it passes once each, and RUNNING PAST rather than stopping at what it
 *    hit. The overshoot is what the design says a miss costs.
 *
 * 3. Never being able to catch the player. That is designed, and one line in
 *    the constructor would undo it.
 *
 * 4. Wearing its art without breaking without it. The Paragon packs are
 *    gitignored, so these run both ways and must pass either way.
 */

namespace CataclysmWardenTest
{
	/** A world that has begun play, so spawned characters get their attributes. */
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (!World)
		{
			return nullptr;
		}

		FWorldContext& Context =
			GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		return World;
	}

	static void TearDown(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	static ACataclysmAbyssalWardenCharacter* SpawnWarden(UWorld* World,
														 const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmAbyssalWardenCharacter>(
			ACataclysmAbyssalWardenCharacter::StaticClass(), Where,
			FRotator::ZeroRotator, Params);
	}
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenCarriesItsDesignedProfile,
	"Cataclysm.Warden.ItCarriesItsDesignedProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenCarriesItsDesignedProfile::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	using Warden_t = ACataclysmAbyssalWardenCharacter;

	TestEqual(TEXT("it reaches exactly contact distance"),
		Warden->AttackReachCm(), Warden_t::DesignedMeleeReachCm);
	TestEqual(TEXT("it swings on its designed interval"),
		Warden->SecondsBetweenAttacks(), Warden_t::DesignedAttackIntervalSeconds);
	TestEqual(TEXT("it wanders its designed distance"),
		Warden->RoamRadiusCm(), Warden_t::WardenRoamRadiusCm);
	TestEqual(TEXT("it notices at its designed distance"),
		Warden->SightRadiusCm(), Warden_t::WardenNoticeRadiusCm);

	// AND EVERY ONE OF THOSE DIFFERS FROM THE BASE ENEMY'S, which is what makes
	// the four above mean something. A creature that inherited the base's
	// figures would pass them all.
	TestNotEqual(TEXT("its reach is not the base enemy's"),
		Warden->AttackReachCm(), 200.0f);
	TestNotEqual(TEXT("its interval is not the base enemy's"),
		Warden->SecondsBetweenAttacks(), 1.5f);
	TestTrue(TEXT("it roams, where the base enemy does not"),
		Warden->RoamRadiusCm() > 0.0f);

	const UCapsuleComponent* Capsule = Warden->GetCapsuleComponent();
	TestEqual(TEXT("its capsule is its designed radius"),
		Capsule->GetScaledCapsuleRadius(), Warden_t::WardenCapsuleRadius);
	TestEqual(TEXT("its capsule is its designed half-height"),
		Capsule->GetScaledCapsuleHalfHeight(), Warden_t::WardenCapsuleHalfHeight);

	const UCharacterMovementComponent* Movement = Warden->GetCharacterMovement();
	TestEqual(TEXT("it walks at its designed speed"),
		Movement->MaxWalkSpeed, Warden_t::DesignedWalkSpeedCmPerSecond);
	// CAST BECAUSE FRotator's COMPONENTS ARE DOUBLES IN UNREAL 5. Comparing one
	// against a float constant is an ambiguous overload, error C2666, and the
	// message names eight candidates rather than saying so.
	TestEqual(TEXT("it turns at its designed rate"),
		Movement->RotationRate.Yaw,
		static_cast<double>(Warden_t::DesignedTurnRateDegreesPerSecond));

	// THE ONE THAT WOULD OTHERWISE BE SILENT. ACataclysmEnemyCharacter never
	// sets MaxWalkSpeed, so a creature that forgot to would move at Unreal's
	// default 600 -- faster than the player, which is exactly what this one is
	// designed not to be.
	TestTrue(TEXT("and it is not Unreal's default walk speed"),
		Movement->MaxWalkSpeed < 600.0f);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenOffersItsRingThenItsCharge,
	"Cataclysm.Warden.ItOffersItsRingThenItsCharge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenOffersItsRingThenItsCharge::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	using Warden_t = ACataclysmAbyssalWardenCharacter;

	const TArray<FCataclysmEnemyAbility> Abilities = Warden->EnemyAbilities();

	// TWO, IN THIS ORDER, AND THE ORDER IS THE POINT. ChooseAbility takes the
	// first entry whose range and cooldown both fit and never looks at the
	// shape. Both are legal from 2.32 to 5.60 metres; the ring is on 12 seconds
	// against the charge's 5, so listing the charge first would crowd it out
	// almost entirely. Issue #491.
	if (!TestEqual(TEXT("it offers exactly two abilities"), Abilities.Num(), 2))
	{
		return false;
	}

	const FCataclysmEnemyAbility& Roar = Abilities[Warden_t::MoltenRoarAbility];
	const FCataclysmEnemyAbility& Charge = Abilities[Warden_t::StampedeAbility];

	TestEqual(TEXT("the ring comes first"), Roar.Name, FName(TEXT("Molten Roar")));
	TestEqual(TEXT("the charge comes second"), Charge.Name,
		FName(TEXT("Stampede")));

	TestEqual(TEXT("the ring is a Strike"),
		static_cast<int32>(Roar.Shape),
		static_cast<int32>(ECataclysmSkillShape::Strike));
	TestEqual(TEXT("it reaches its designed radius"),
		Roar.MaxRangeCm, Warden_t::MoltenRoarRadiusCm);
	TestEqual(TEXT("its marker is the same radius the damage sweeps"),
		Roar.MarkerRadiusCm, Warden_t::MoltenRoarRadiusCm);
	TestEqual(TEXT("it comes round on its designed cooldown"),
		Roar.CooldownSeconds, Warden_t::MoltenRoarCooldownSeconds);
	TestEqual(TEXT("it warns for its designed wind-up"),
		Roar.WindUpSeconds, Warden_t::MoltenRoarWindUpSeconds);

	// FROM ITS OWN FEET OUT. A target pressed against the creature is inside
	// the ring and must be hit by it, so a minimum range would be wrong here in
	// a way it is not wrong for a lobbed attack.
	TestEqual(TEXT("the ring has no minimum range"), Roar.MinRangeCm, 0.0f);

	TestFalse(TEXT("it is not lobbed, so its marker is a ring at the feet"),
		Roar.bArcsOntoItsTarget);

	// AND THE MARKER CLEARS THE ONE METRE FLOOR, below which the design document
	// says a marker is smaller than the creature standing in it.
	TestTrue(TEXT("its marker is larger than the smallest useful one"),
		Roar.MarkerRadiusCm >= 100.0f);

	// THE CHARGE. A Movement shape, which is what makes the controller draw a
	// lane and capture the far end of the charge as the aim point rather than
	// the target's feet.
	TestEqual(TEXT("the charge is a Movement shape"),
		static_cast<int32>(Charge.Shape),
		static_cast<int32>(ECataclysmSkillShape::Movement));
	TestEqual(TEXT("it runs its designed range"),
		Charge.MaxRangeCm, Warden_t::StampedeRangeCm);
	TestEqual(TEXT("its lane is its designed half-width"),
		Charge.MarkerRadiusCm, Warden_t::StampedeRadiusCm);
	TestEqual(TEXT("it comes round on its designed cooldown"),
		Charge.CooldownSeconds, Warden_t::StampedeCooldownSeconds);
	TestEqual(TEXT("it warns for its designed wind-up"),
		Charge.WindUpSeconds, Warden_t::StampedeWindUpSeconds);

	// IT REFUSES A TARGET IT COULD SIMPLY WALK TO, which is the design's own
	// test for whether a charge is worth winding up for.
	TestEqual(TEXT("it refuses a target inside its walking distance"),
		Charge.MinRangeCm, Warden_t::StampedeMinimumRangeCm);
	TestTrue(TEXT("and that minimum is beyond the creature's own reach"),
		Charge.MinRangeCm > Warden_t::DesignedMeleeReachCm);

	// THE TWO DO NOT SHADOW EACH OTHER COMPLETELY. There has to be ground the
	// charge covers that the ring does not, or the charge could never be the
	// entry that fits.
	TestTrue(TEXT("the charge reaches beyond the ring"),
		Charge.MaxRangeCm > Roar.MaxRangeCm);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenChargeTravelsItsLane,
	"Cataclysm.Warden.StampedeTravelsItsWholeLaneAtItsDesignedSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenChargeTravelsItsLane::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	// WHERE IT STARTED, HOW FAST IT GOES, HOW THAT SPEED IS DISTRIBUTED, AND
	// WHERE IT ENDS. Four separate things, because the Brute's rock throw
	// shipped four defects in a row each of which was adjacent to what the
	// previous test had proved.

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	using Warden_t = ACataclysmAbyssalWardenCharacter;

	const FVector Started = Warden->GetActorLocation();
	const float Speed = Warden_t::StampedeSpeedCmPerSecond;

	// DOWN +X, which is the actor's forward with no rotation applied.
	const FVector LaneEnd =
		Started + FVector(Warden_t::StampedeRangeCm, 0.0f, 0.0f);

	Warden->BeginCharge(LaneEnd, Speed, Warden_t::StampedeRadiusCm,
						Warden_t::StampedeDamagePercent);

	TestTrue(TEXT("it is charging once told to"), Warden->IsCharging());

	// WHERE IT STARTED: it has not moved yet.
	TestEqual(TEXT("it has travelled nothing before the first step"),
		Warden->ChargeTravelledCm, 0.0f);
	TestTrue(TEXT("and it is still where it began"),
		Warden->GetActorLocation().Equals(Started, 0.01f));

	// HOW FAST, AND HOW THE SPEED IS DISTRIBUTED. A tenth of a second at a time,
	// each of which must cover exactly the same ground. A charge that
	// accelerated, or that moved the whole distance on the first frame, would
	// pass a test that only looked at where it ended up.
	constexpr float Slice = 0.1f;
	const float ExpectedPerSlice = Speed * Slice;

	float PreviousTravelled = 0.0f;
	for (int32 Step = 1; Step <= 3; ++Step)
	{
		Warden->AdvanceCharge(Slice);

		const float MovedThisSlice = Warden->ChargeTravelledCm - PreviousTravelled;
		PreviousTravelled = Warden->ChargeTravelledCm;

		TestEqual(
			*FString::Printf(
				TEXT("slice %d covers the same ground as every other"), Step),
			MovedThisSlice, ExpectedPerSlice, 0.5f);

		TestTrue(
			*FString::Printf(TEXT("it is still charging after slice %d"), Step),
			Warden->IsCharging());
	}

	// IT IS ON THE LANE, NOT MERELY THE RIGHT DISTANCE ALONG IT. A charge that
	// drifted sideways would satisfy every distance check above.
	// A DOUBLE TOLERANCE, NOT A FLOAT ONE. FVector's components are doubles in
	// Unreal 5, and a float tolerance beside them is an ambiguous overload --
	// error C2666, which names eight candidates rather than saying so.
	TestEqual(TEXT("it has not drifted off the lane"),
		Warden->GetActorLocation().Y, Started.Y, 0.01);
	TestEqual(TEXT("and it has not risen or sunk"),
		Warden->GetActorLocation().Z, Started.Z, 0.01);

	// WHERE IT ENDS. Enough time for the whole 8 metres and more, to prove it
	// stops at the end of its lane rather than running on.
	Warden->AdvanceCharge(5.0f);

	TestFalse(TEXT("it stops charging once the lane is run"),
		Warden->IsCharging());
	TestEqual(TEXT("it travelled exactly its designed range"),
		Warden->ChargeTravelledCm, Warden_t::StampedeRangeCm, 0.5f);
	TestTrue(TEXT("and it finished at the end of the lane it was given"),
		Warden->GetActorLocation().Equals(LaneEnd, 1.0f));

	// FURTHER ADVANCES DO NOTHING, so a charge that ended cannot be nudged on
	// by the next frame.
	const FVector Rested = Warden->GetActorLocation();
	Warden->AdvanceCharge(1.0f);
	TestTrue(TEXT("a finished charge does not move again"),
		Warden->GetActorLocation().Equals(Rested, 0.01f));

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenChargeHitsAlongTheWayOnce,
	"Cataclysm.Warden.StampedeHitsAlongItsLaneOnceAndRunsPast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenChargeHitsAlongTheWayOnce::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	using Warden_t = ACataclysmAbyssalWardenCharacter;

	// ONE IN THE LANE AND ONE BESIDE IT. Spawned far away and then moved,
	// because two capsules created at contact distance push each other apart.
	// Both on the players' side, because FindEnemiesInLine finds actors hostile
	// to the one asking and a second enemy would be the Warden's ALLY.
	ACataclysmEnemyCharacter* InTheLane =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(),
			FVector(20000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* BesideIt =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(),
			FVector(20000.0f, 500.0f, 0.0f), FRotator::ZeroRotator);

	if (!InTheLane || !BesideIt)
	{
		AddError(TEXT("could not spawn something to stand in the lane"));
		return false;
	}

	InTheLane->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	BesideIt->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));

	// HALF WAY DOWN THE LANE, so the charge must still be travelling when it
	// reaches them rather than meeting them at either end.
	const float HalfWay = Warden_t::StampedeRangeCm / 2.0f;
	InTheLane->SetActorLocation(FVector(HalfWay, 0.0f, 0.0f));

	// JUST OUTSIDE THE LANE'S HALF-WIDTH, which is what makes the hit above
	// mean something rather than proving the charge hits everything.
	BesideIt->SetActorLocation(
		FVector(HalfWay, Warden_t::StampedeRadiusCm + 100.0f, 0.0f));

	const FVector LaneEnd = FVector(Warden_t::StampedeRangeCm, 0.0f, 0.0f);
	Warden->BeginCharge(LaneEnd, Warden_t::StampedeSpeedCmPerSecond,
						Warden_t::StampedeRadiusCm,
						Warden_t::StampedeDamagePercent);

	// MANY SMALL STEPS, WHICH IS THE POINT OF THIS TEST. The lane is re-tested
	// every step, so a target standing still inside it would be hit on every one
	// of them unless the charge remembers who it has already hit. Sixty steps
	// against one expected hit.
	for (int32 Frame = 0; Frame < 60 && Warden->IsCharging(); ++Frame)
	{
		Warden->AdvanceCharge(1.0f / 60.0f);
	}

	TestEqual(TEXT("it hit exactly one thing, once"), Warden->ChargeHitCount, 1);

	// IT DID NOT STOP AT WHAT IT HIT. The design says the creature is committed
	// and runs the full distance, ending past its target -- that overshoot is
	// the window the telegraph buys. A charge that stopped on contact would end
	// half way down the lane, in melee range, which is the opposite.
	TestFalse(TEXT("it is no longer charging"), Warden->IsCharging());
	TestEqual(TEXT("it ran the whole lane rather than stopping at what it hit"),
		Warden->ChargeTravelledCm, Warden_t::StampedeRangeCm, 1.0f);
	TestTrue(TEXT("so it finished past its target, not at it"),
		Warden->GetActorLocation().X > InTheLane->GetActorLocation().X);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenDoesNotTurnWhileCharging,
	"Cataclysm.Warden.TheBrainDoesNothingWhileAChargeIsInFlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenDoesNotTurnWhileCharging::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	// THE GAP THAT LET THIS SHIP. The three other charge tests call BeginCharge
	// and AdvanceCharge directly and never call Think at all, so the brain was
	// not in the picture and nothing noticed it was still steering. Issue #499.
	// This one drives the brain WHILE a charge is running, which is the only
	// arrangement that can see the defect.

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	using Warden_t = ACataclysmAbyssalWardenCharacter;

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Warden->GetController());
	if (!Brain)
	{
		AddError(TEXT("the Warden has no brain"));
		return false;
	}

	// A TARGET OFF TO THE SIDE, WHICH IS WHAT MAKES THIS TEST ABLE TO FAIL. The
	// charge runs down +X and the target stands on +Y, so a brain that turned to
	// face it would swing the creature a long way off the lane. A target
	// straight ahead would leave the yaw unchanged either way and the test could
	// not tell a fixed facing from a re-aimed one.
	ACataclysmEnemyCharacter* OffToTheSide =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(),
			FVector(20000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!OffToTheSide)
	{
		AddError(TEXT("could not spawn something for the brain to notice"));
		return false;
	}
	OffToTheSide->SetGenericTeamId(
		UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	OffToTheSide->SetActorLocation(FVector(0.0f, 400.0f, 0.0f));

	// Charging down +X, away from the target on +Y.
	const FVector LaneEnd = FVector(Warden_t::StampedeRangeCm, 0.0f, 0.0f);
	Warden->BeginCharge(LaneEnd, Warden_t::StampedeSpeedCmPerSecond,
						Warden_t::StampedeRadiusCm,
						Warden_t::StampedeDamagePercent);

	const FRotator FacingAtStart = Warden->GetActorRotation();
	const FVector WhereItStarted = Warden->GetActorLocation();

	// THE BRAIN THINKS WHILE THE CHARGE RUNS, which is what really happens: the
	// thinking timer keeps firing four times a second throughout.
	for (int32 Pass = 0; Pass < 3 && Warden->IsCharging(); ++Pass)
	{
		const ECataclysmBrainAction Action = Brain->Think();

		TestEqual(
			*FString::Printf(
				TEXT("pass %d reports it is charging, not chasing or attacking"),
				Pass),
			static_cast<int32>(Action),
			static_cast<int32>(ECataclysmBrainAction::Charging));

		// A FEW FRAMES OF TRAVEL BETWEEN PASSES, so the charge is genuinely
		// mid-flight when the next pass runs rather than finished.
		for (int32 Frame = 0; Frame < 15; ++Frame)
		{
			Warden->AdvanceCharge(1.0f / 60.0f);
		}
	}

	// THE CONTROL ROTATION IS WHAT THE DEFECT MOVED. FaceTarget writes it and
	// the movement component turns the pawn toward it over the following frames,
	// so on a world that is never ticked the pawn's own rotation would not have
	// moved yet even with the defect present. Reading the controller's rotation
	// is what makes this test able to fail.
	const FRotator Ordered = Brain->GetControlRotation();
	const float OrderedOffLane = FMath::Abs(FRotator::NormalizeAxis(Ordered.Yaw));

	TestTrue(
		*FString::Printf(
			TEXT("the brain did not re-aim the creature during the charge; it "
				 "ordered a yaw of %.1f degrees off the lane"), OrderedOffLane),
		OrderedOffLane < 1.0f);

	TestEqual(TEXT("and the creature's own facing is unchanged"),
		Warden->GetActorRotation().Yaw, FacingAtStart.Yaw, 0.5);

	// IT DID TRAVEL, so the passes above were not simply a charge that never
	// started.
	TestTrue(TEXT("the charge moved the creature while the brain thought"),
		Warden->GetActorLocation().X > WhereItStarted.X + 100.0f);

	// AND IT STAYED ON THE LANE. A brain that ordered a walk toward the target
	// would show up here even if the facing did not move.
	TestEqual(TEXT("it did not drift toward the target off the lane"),
		Warden->GetActorLocation().Y, WhereItStarted.Y, 1.0);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenChargeStopsWhenStunned,
	"Cataclysm.Warden.AStunStopsAChargeInFlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenChargeStopsWhenStunned::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	// A STUN OUTRANKS A CHARGE, which is the one thing that does. The design
	// says a stunned creature cannot act at all, and one still travelling would
	// be acting. Issue #499.

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	using Warden_t = ACataclysmAbyssalWardenCharacter;

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Warden->GetController());
	if (!Brain)
	{
		AddError(TEXT("the Warden has no brain"));
		return false;
	}

	Warden->BeginCharge(FVector(Warden_t::StampedeRangeCm, 0.0f, 0.0f),
						Warden_t::StampedeSpeedCmPerSecond,
						Warden_t::StampedeRadiusCm,
						Warden_t::StampedeDamagePercent);

	// PART WAY DOWN THE LANE, so there is a charge to interrupt rather than one
	// that had already finished.
	for (int32 Frame = 0; Frame < 15; ++Frame)
	{
		Warden->AdvanceCharge(1.0f / 60.0f);
	}

	if (!TestTrue(TEXT("it is charging before the stun"), Warden->IsCharging()))
	{
		return false;
	}

	const FVector StoppedAt = Warden->GetActorLocation();

	// A DESIGNED STUN, so it lands regardless of the damage threshold. What is
	// under test is what a stun does to a charge, not whether a stun applies.
	if (!TestTrue(TEXT("the stun landed"),
			UCataclysmSkillEffects::ApplyStun(Warden, Warden, /*Duration=*/1.0f,
											  /*DamageDealt=*/0.0f,
											  /*bStunIsDesigned=*/true)))
	{
		return false;
	}

	const ECataclysmBrainAction Action = Brain->Think();

	TestEqual(TEXT("the brain reports it is stunned, not charging"),
		static_cast<int32>(Action),
		static_cast<int32>(ECataclysmBrainAction::Stunned));

	TestFalse(TEXT("and the charge has been cancelled"), Warden->IsCharging());

	// IT STAYS WHERE IT STOPPED. Advancing again must move nothing, which is
	// what proves the charge is really over rather than merely paused.
	Warden->AdvanceCharge(1.0f);
	TestTrue(TEXT("a cancelled charge does not travel any further"),
		Warden->GetActorLocation().Equals(StoppedAt, 1.0f));

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenChargeMarksTheWholeLane,
	"Cataclysm.Warden.StampedeMarksItsWholeLaneAndNotJustAsFarAsTheTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenChargeMarksTheWholeLane::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	// THE DEFECT THIS EXISTS FOR. A charge runs its full range and ends PAST
	// whatever it was aimed at. A lane drawn only as far as the target would be
	// telling the truth about the start of the charge and lying about the end,
	// and the player would learn to trust it and then be run over on ground it
	// never marked. This is the same class of defect issue #471 records, where
	// the Brute's rock landed a metre above the circle it had drawn.
	//
	// IT IS CHECKED AGAINST THE MARKER ACTUALLY IN THE WORLD, not against the
	// number the controller was given. A test that compared the aim point to the
	// expression that computes it could not fail.

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	using Warden_t = ACataclysmAbyssalWardenCharacter;

	// SEVEN METRES AWAY, WHICH IS THE DISTANCE THAT MAKES THIS TEST WORK. It is
	// past Molten Roar's reach, so the ring is not the entry that fits, and
	// inside the charge's 8 metres. It is also a metre SHORT of the charge's
	// range, which is the gap the lane must not stop at.
	//
	// IT WAS 600 UNTIL 2026-08-09, when Molten Roar's radius grew from 5.6 to
	// 6.5 metres and swallowed it: the ring became the ability the brain chose
	// at that distance, so the marker drawn was a circle and this test failed.
	// Issues #487 and #496. The static_assert below is what makes that a
	// compile error next time rather than a puzzling test failure.
	constexpr float TargetAtCm = 700.0f;

	static_assert(TargetAtCm > Warden_t::MoltenRoarRadiusCm,
		"this test needs a distance at which the charge is the ability the "
		"brain chooses, which means past the ring's reach. Molten Roar has "
		"grown past it.");
	static_assert(TargetAtCm < Warden_t::StampedeRangeCm,
		"this test needs the target to be SHORT of the charge's full range, "
		"because the whole point is that the lane runs past the target rather "
		"than stopping at it.");

	ACataclysmEnemyCharacter* Quarry =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(),
			FVector(20000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!Quarry)
	{
		AddError(TEXT("could not spawn something to charge at"));
		return false;
	}
	Quarry->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	Quarry->SetActorLocation(FVector(TargetAtCm, 0.0f, 0.0f));

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Warden->GetController());
	if (!Brain)
	{
		AddError(TEXT("the Warden has no brain"));
		return false;
	}

	// THE CREATURE ALREADY FACES +X with no rotation applied, and the target is
	// down +X, so the charge does not spend a pass turning first. That is
	// arranged rather than assumed: AbilityNeedsFacing returns true for a
	// Movement shape, so a creature pointed elsewhere would return Turning here
	// and draw nothing.
	Brain->Think();

	const ACataclysmTelegraphMarker* Lane = nullptr;
	for (TActorIterator<ACataclysmTelegraphMarker> It(World); It; ++It)
	{
		Lane = *It;
		break;
	}

	if (!Lane)
	{
		AddError(TEXT("the charge's wind-up drew no marker at all, which is the "
					  "failure issue #491 describes: the marker switch used to "
					  "return silently for a Movement shape"));
		return false;
	}

	TestTrue(TEXT("a charge marks a lane, not a circle"), Lane->IsLane());

	// THE WHOLE RANGE, NOT THE DISTANCE TO THE TARGET. 800, not 600.
	TestEqual(TEXT("the lane runs the charge's full range"),
		Lane->LengthCm, Warden_t::StampedeRangeCm, 1.0f);
	TestTrue(TEXT("which is further than the target it was aimed at"),
		Lane->LengthCm > TargetAtCm);

	TestEqual(TEXT("and it is as wide as the corridor that hits"),
		Lane->RadiusCm, Warden_t::StampedeRadiusCm);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenSwingsLeftThenRightThenRecovers,
	"Cataclysm.Warden.ItSwingsLeftThenRightThenRecovers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenSwingsLeftThenRightThenRecovers::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	const bool bDressed = Warden->ResolveBody(/*bIncludeAnimation=*/true);
	if (!bDressed || !Warden->LeftSwingAnimation)
	{
		AddInfo(TEXT("SKIPPED: the Paragon Grux pack is not present, so there "
					 "are no clips to sequence. The behaviour this checks was "
					 "NOT verified on this machine."));
		return true;
	}

	// THREE CLIPS, IN ORDER, AS ONE BASIC ATTACK. Asked for by the project
	// owner: "if he has an attack for both arms include them both so he gives a
	// good left right", and the recovery is what stops the creature snapping
	// back to neutral from a metre and a half of hand travel away.
	const TArray<UAnimSequence*> Clips = Warden->BasicAttackClips();

	if (!TestEqual(TEXT("one basic attack is three clips"), Clips.Num(), 3))
	{
		return false;
	}

	TestEqual(TEXT("the left swing comes first"),
		Clips[0], Warden->LeftSwingAnimation.Get());
	TestEqual(TEXT("the right swing comes second"),
		Clips[1], Warden->RightSwingAnimation.Get());
	TestEqual(TEXT("and the recovery comes last"),
		Clips[2], Warden->SwingRecoveryAnimation.Get());

	TestNotEqual(TEXT("the two swings are different animations"),
		Clips[0], Clips[1]);

	// AND THE WHOLE THING FITS INSIDE ONE ATTACK INTERVAL AT AUTHORED SPEED,
	// which is the reason the fast swing variants were chosen over the
	// full-speed ones. Measured 2026-08-09: 2.100 s against 2.4.
	float Total = 0.0f;
	for (UAnimSequence* Clip : Clips)
	{
		Total += Clip->GetPlayLength();
	}
	TestTrue(TEXT("the whole combo fits inside one attack interval"),
		Total <= ACataclysmAbyssalWardenCharacter::DesignedAttackIntervalSeconds);

	// THE SEQUENCE REALLY RUNS, one clip at a time, and ends back at rest.
	Warden->AttackTarget(nullptr);

	TestEqual(TEXT("the left swing is playing first"),
		Warden->LastPlayedAnimation.Get(), Clips[0]);

	for (int32 Step = 1; Step < Clips.Num(); ++Step)
	{
		// The clip's end is moved into the past rather than the world being
		// ticked forward, which keeps the test instant and exercises the same
		// branch.
		Warden->OneShotEndsAtSeconds = 0.0f;
		Warden->UpdateLoopingAnimation();
		TestEqual(
			FString::Printf(TEXT("clip %d of the combo follows"), Step + 1),
			Warden->LastPlayedAnimation.Get(), Clips[Step]);
	}

	// AND THEN IT STANDS, rather than holding the recovery's last frame.
	Warden->OneShotEndsAtSeconds = 0.0f;
	Warden->UpdateLoopingAnimation();
	TestEqual(TEXT("and then it stands in its idle again"),
		Warden->CurrentLoopingAnimation.Get(), Warden->IdleAnimation.Get());

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenRingReachesItsWholeMarker,
	"Cataclysm.Warden.MoltenRoarHitsEverythingInsideItsMarker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenRingReachesItsWholeMarker::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	using Warden_t = ACataclysmAbyssalWardenCharacter;

	// THE SWEEP THE ABILITY REALLY USES, asked at the same radius the marker is
	// drawn from. What this proves is that the circle promised and the circle
	// swept are the same number rather than two that can disagree -- which is
	// the defect the Brute's rock throw shipped four times.
	const FCataclysmEnemyAbility Roar = Warden->EnemyAbilities()[0];

	TestEqual(TEXT("the marker and the ability's reach are one number"),
		Roar.MarkerRadiusCm, Roar.MaxRangeCm);
	TestEqual(TEXT("and both are the constant the damage sweeps at"),
		Roar.MarkerRadiusCm, Warden_t::MoltenRoarRadiusCm);

	// SOMETHING STANDING AT THE EDGE IS INSIDE IT. Spawned far off and then
	// moved, because two capsules created at contact distance push each other
	// apart before anything can be measured.
	//
	// AND PUT ON THE PLAYERS' TEAM, WHICH IS NOT OPTIONAL. FindEnemiesInSphere
	// finds actors hostile to the actor asking, and a second creature spawned
	// as an enemy is the Warden's ALLY. The first version of this test left it
	// on the default team, and the sweep correctly returned nothing -- which
	// read as the ring not working rather than as the target being on the wrong
	// side.
	ACataclysmEnemyCharacter* AtTheEdge =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(),
			FVector(10000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!AtTheEdge)
	{
		AddError(TEXT("could not spawn something to stand in the ring"));
		return false;
	}

	AtTheEdge->SetGenericTeamId(
		UCataclysmTeams::IdFor(ECataclysmTeam::Players));

	AtTheEdge->SetActorLocation(
		FVector(Warden_t::MoltenRoarRadiusCm - 1.0f, 0.0f, 0.0f));

	const TArray<AActor*> Caught = UCataclysmTargeting::FindEnemiesInSphere(
		World, Warden, Warden->GetActorLocation(),
		Warden_t::MoltenRoarRadiusCm);

	TestTrue(TEXT("something just inside the ring is caught by it"),
		Caught.Contains(AtTheEdge));

	// AND SOMETHING JUST OUTSIDE IT IS NOT, which is what makes the check above
	// mean something rather than proving the sweep catches everything.
	AtTheEdge->SetActorLocation(
		FVector(Warden_t::MoltenRoarRadiusCm + 200.0f, 0.0f, 0.0f));

	const TArray<AActor*> Missed = UCataclysmTargeting::FindEnemiesInSphere(
		World, Warden, Warden->GetActorLocation(),
		Warden_t::MoltenRoarRadiusCm);

	TestFalse(TEXT("something outside the ring is not caught by it"),
		Missed.Contains(AtTheEdge));

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenHidesItsPlaceholderOnceDressed,
	"Cataclysm.Warden.ItHidesItsPlaceholderOnceDressed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenHidesItsPlaceholderOnceDressed::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	if (!Warden->PlaceholderBody)
	{
		AddError(TEXT("the enemy base no longer creates a PlaceholderBody, so "
					  "there is nothing to hide and this check is meaningless"));
		return false;
	}

	// IT STARTS VISIBLE, which is what makes the rest of this mean something. A
	// placeholder that was hidden from birth would pass the check below on a
	// creature that never turns it off.
	TestTrue(TEXT("the placeholder cylinder starts visible"),
		Warden->PlaceholderBody->IsVisible());

	const bool bDressed = Warden->ResolveBody(/*bIncludeAnimation=*/true);

	// THE RELATIONSHIP RATHER THAN AN ABSOLUTE, so this runs the same on a
	// machine with the Paragon Grux pack and on one without. With the art the
	// mesh resolves and the cylinder must go; without it, ResolveBody returns
	// false and the cylinder is all there is, so it must stay.
	//
	// THE PROJECT OWNER SAW THIS FAIL ON 2026-08-09: "the cylinder base is
	// appearing over him". The class set the skeletal mesh and left the
	// placeholder visible on top of it.
	TestEqual(
		TEXT("the placeholder is hidden exactly when the real mesh resolved"),
		Warden->PlaceholderBody->IsVisible(), !bDressed);

	if (!bDressed)
	{
		AddInfo(TEXT("the Paragon Grux pack is not present, so what was checked "
					 "is that the placeholder is KEPT rather than that it is "
					 "hidden. Both directions matter; only one ran here."));
	}

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenReturnsToARestingPoseAfterASwing,
	"Cataclysm.Warden.ItReturnsToARestingPoseAfterASwing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenReturnsToARestingPoseAfterASwing::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	const bool bDressed = Warden->ResolveBody(/*bIncludeAnimation=*/true);
	if (!bDressed || !Warden->IdleAnimation || !Warden->LeftSwingAnimation)
	{
		AddInfo(TEXT("SKIPPED: the Paragon Grux pack is not present, so there "
					 "are no clips to move between. The behaviour this checks "
					 "was NOT verified on this machine."));
		return true;
	}

	// STANDING TO BEGIN WITH.
	Warden->UpdateLoopingAnimation();
	TestEqual(TEXT("it stands in its idle before anything happens"),
		Warden->CurrentLoopingAnimation.Get(), Warden->IdleAnimation.Get());

	// A SWING TAKES THE MESH AND RECORDS WHEN IT WILL GIVE IT BACK. Without
	// that the creature held the last frame of the swing until the next one,
	// which the project owner reported on 2026-08-09.
	Warden->AttackTarget(nullptr);

	TestTrue(TEXT("a swing records when it will finish"),
		Warden->OneShotEndsAtSeconds > World->GetTimeSeconds());
	TestNull(TEXT("and nothing is looping while it plays"),
		Warden->CurrentLoopingAnimation.Get());

	// WHILE IT IS STILL PLAYING, NOTHING TAKES THE MESH BACK.
	Warden->UpdateLoopingAnimation();
	TestNull(TEXT("the swing is left alone until it ends"),
		Warden->CurrentLoopingAnimation.Get());

	// AND ONCE THE WHOLE ATTACK HAS ENDED, THE RESTING POSE COMES BACK.
	//
	// DRAINED RATHER THAN STEPPED ONCE, and that is what this test learned on
	// 2026-08-09. It used to clear the end time a single time and expect the
	// idle, which was right when a basic attack was one clip. It is now three --
	// a left swing, a right swing and a recovery -- so a single step just starts
	// the next clip. The loop is bounded so a sequence that never finishes fails
	// here rather than hanging.
	//
	// Each end time is moved into the past rather than the world being ticked
	// forward, which keeps the test instant and exercises the same branch.
	constexpr int32 MostClipsAnAttackMayHave = 8;
	for (int32 Step = 0; Step < MostClipsAnAttackMayHave; ++Step)
	{
		if (Warden->CurrentLoopingAnimation)
		{
			break;
		}
		Warden->OneShotEndsAtSeconds = 0.0f;
		Warden->UpdateLoopingAnimation();
	}

	TestEqual(TEXT("it returns to its idle once the whole attack has finished"),
		Warden->CurrentLoopingAnimation.Get(), Warden->IdleAnimation.Get());

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenWalksRatherThanSlides,
	"Cataclysm.Warden.ItWalksRatherThanSlides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenWalksRatherThanSlides::RunTest(const FString&)
{
	using Warden_t = ACataclysmAbyssalWardenCharacter;

	// THE ARITHMETIC THAT STOPS THE FOOT SLIDING, checked without a world.
	//
	// A planted foot travels backwards at the clip's authored speed while the
	// body travels forwards at the designed speed. Playing the clip at the
	// ratio of the two makes them cancel. `tools/measure_animation_stride.py`
	// measured Jog_Fwd at 281.6 cm/s on 2026-08-09 and the creature is designed
	// at 280, so the rate is 0.994.
	const float Expected = Warden_t::DesignedWalkSpeedCmPerSecond
						 / Warden_t::AuthoredJogSpeedCmPerSecond;

	TestEqual(TEXT("the walk plays at the ratio of designed to authored speed"),
		Warden_t::JogPlayRate(), Expected);

	// AND THE PRODUCT IS THE SPEED IT ACTUALLY MOVES AT, which is the thing
	// that matters and is not the same statement. If either figure were read
	// from the wrong place this would still pass the check above and fail here.
	TestEqual(TEXT("so the foot travels backwards at the body's own speed"),
		Warden_t::JogPlayRate() * Warden_t::AuthoredJogSpeedCmPerSecond,
		Warden_t::DesignedWalkSpeedCmPerSecond);

	// THE RATE IS INSIDE THE CLAMP, so it is not being silently corrected.
	TestTrue(TEXT("the rate is not clamped"),
		Warden_t::JogPlayRate() > Warden_t::MinimumPlayRate
		&& Warden_t::JogPlayRate() < Warden_t::MaximumPlayRate);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenCanRegisterContactDespiteItsHeight,
	"Cataclysm.Warden.ItCanRegisterContactDespiteItsHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenCanRegisterContactDespiteItsHeight::RunTest(const FString&)
{
	using Warden_t = ACataclysmAbyssalWardenCharacter;

	// THE BOUND THAT IS NOT OBVIOUS, checked in the engine as well as in Python.
	// The brain compares reach on the floor plane, but the two capsule centres
	// do not sit at the same height and ContactToleranceCm is the only slack
	// there is. This creature's capsule is 34 cm taller than the base enemy's,
	// which is what makes it the one where the arithmetic is tight.
	constexpr float PlayerCapsuleRadius = 42.0f;
	constexpr float PlayerCapsuleHalfHeight = 96.0f;

	const float Horizontal = PlayerCapsuleRadius + Warden_t::WardenCapsuleRadius;
	const float HeightGap =
		Warden_t::WardenCapsuleHalfHeight - PlayerCapsuleHalfHeight;
	const double Contact3D = FMath::Sqrt(
		static_cast<double>(Horizontal) * Horizontal
		+ static_cast<double>(HeightGap) * HeightGap);

	const double Allowed = static_cast<double>(Warden_t::DesignedMeleeReachCm)
						 + ACataclysmEnemyController::ContactToleranceCm;

	TestTrue(TEXT("the contact tolerance absorbs its height, so it can reach "
				  "the player at all"),
		Contact3D <= Allowed);

	// AND THE HORIZONTAL DISTANCE IS EXACTLY ITS REACH, which is what the
	// creature's designed 0.90 m means: the two bodies touch and no further.
	TestEqual(TEXT("its reach is exactly the two capsule radii"),
		Horizontal, Warden_t::DesignedMeleeReachCm);

	return true;
}


// --------------------------------------------------------------------------
// Displacement. Issue #625.
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWardenChargeShovesWhatItRunsThrough,
	"Cataclysm.Warden.StampedeShovesWhatItRunsThroughSidewaysOutOfTheLane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWardenChargeShovesWhatItRunsThrough::RunTest(const FString&)
{
	using namespace CataclysmWardenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnWarden(World, FVector::ZeroVector);
	if (!Warden)
	{
		AddError(TEXT("could not spawn an Abyssal Warden"));
		return false;
	}

	using Warden_t = ACataclysmAbyssalWardenCharacter;

	// Spawned far away and then moved, because two capsules created at contact
	// distance push each other apart. On the players' side, because
	// FindEnemiesInLine finds actors hostile to the one asking.
	ACataclysmEnemyCharacter* InTheLane =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(),
			FVector(20000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!InTheLane)
	{
		AddError(TEXT("could not spawn something to stand in the lane"));
		return false;
	}
	InTheLane->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));

	// OFF THE CENTRE LINE BUT INSIDE THE LANE. Standing exactly on the axis would
	// put it directly in front of the creature, and the shove is away from the
	// creature, so it would be pushed ALONG the lane and this could not tell
	// sideways from forwards. Half the lane's half-width is comfortably inside it
	// and gives a sideways component to measure.
	const float HalfWay = Warden_t::StampedeRangeCm / 2.0f;
	const float OffAxis = Warden_t::StampedeRadiusCm / 2.0f;
	InTheLane->SetActorLocation(FVector(HalfWay, OffAxis, 0.0f));
	const FVector Before = InTheLane->GetActorLocation();

	Warden->BeginCharge(FVector(Warden_t::StampedeRangeCm, 0.0f, 0.0f),
						Warden_t::StampedeSpeedCmPerSecond,
						Warden_t::StampedeRadiusCm,
						Warden_t::StampedeDamagePercent,
						Warden_t::StampedeKnockbackCm);

	for (int32 Frame = 0; Frame < 60 && Warden->IsCharging(); ++Frame)
	{
		Warden->AdvanceCharge(1.0f / 60.0f);
	}

	TestEqual(TEXT("it ran through exactly one thing"),
		Warden->ChargeHitCount, 1);

	const FVector After = InTheLane->GetActorLocation();
	const float Moved = FVector::Dist2D(Before, After);

	// IT MOVED, AND ROUGHLY THE DESIGNED DISTANCE. A tolerance rather than
	// equality, because the shove is swept and a capsule can stop short against
	// another body. What must not happen is it not moving at all, which is what a
	// knockback that reached nothing looks like.
	TestTrue(FString::Printf(
			TEXT("it was shoved; designed %.0f cm, moved %.0f cm"),
			Warden_t::StampedeKnockbackCm, Moved),
		Moved > Warden_t::StampedeKnockbackCm * 0.5f);

	// IT ENDS OUTSIDE THE LANE, which is what knocking somebody aside has to mean
	// for a charge: the ground the creature is running down is cleared.
	//
	// FORWARD AND OUT RATHER THAN STRAIGHT OUT, and that is measured rather than
	// assumed. The shove is away from the creature, and the creature meets its
	// target at the LEADING EDGE of the lane -- 150 cm of half-width against a
	// target 75 cm off the axis puts first contact about 130 cm short of it. So
	// away-from-the-creature at that moment is diagonal, and the target moves
	// further along the lane than across it: 334 cm along against 219 cm across,
	// measured 2026-08-16. It still leaves the lane, which is the requirement,
	// and that is why displacement is left as one rule rather than given a
	// special case for a charge.
	const float OutFromAxis = FMath::Abs(After.Y);
	TestTrue(FString::Printf(
			TEXT("it ended %.0f cm off the axis, outside the %.0f cm lane"),
			OutFromAxis, Warden_t::StampedeRadiusCm),
		OutFromAxis > Warden_t::StampedeRadiusCm);

	// AND AWAY FROM THE CENTRE LINE, not across it into the creature's path. It
	// started at +Y, so it must end further out in +Y.
	TestTrue(TEXT("it was pushed away from the centre line, not through it"),
		After.Y > Before.Y);

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
