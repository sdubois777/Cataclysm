// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyController.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmCharacterBase.h"
#include "Engine/World.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"

ACataclysmEnemyController::ACataclysmEnemyController()
{
	// Nothing to do per frame. Think() runs on a timer; see the class comment.
	PrimaryActorTick.bCanEverTick = false;

	// A monster turns to face where it is walking, like the player character
	// does. Without this the pawn slides sideways while facing its old
	// direction, which reads as the movement not working.
	bSetControlRotationFromPawnOrientation = false;
}

ACataclysmCharacterBase* ACataclysmEnemyController::Body() const
{
	return Cast<ACataclysmCharacterBase>(GetPawn());
}

void ACataclysmEnemyController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ThinkTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { Think(); }),
			ThinkIntervalSeconds, /*bLoop=*/true,
			/*InFirstDelay=*/ThinkIntervalSeconds);
	}
}

void ACataclysmEnemyController::OnUnPossess()
{
	// The pawn is going away. Without clearing this the timer keeps firing on a
	// controller with nothing to drive, and Think() does its sphere overlap for
	// a pawn that no longer exists.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ThinkTimer);
	}

	CurrentTarget = nullptr;
	LastAction = ECataclysmBrainAction::Idle;

	Super::OnUnPossess();
}

void ACataclysmEnemyController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ThinkTimer);
	}

	Super::EndPlay(EndPlayReason);
}

AActor* ACataclysmEnemyController::ChooseTarget() const
{
	const ACataclysmCharacterBase* Driven = Body();
	if (!Driven)
	{
		return nullptr;
	}

	const float Sight = Driven->SightRadiusCm();
	if (Sight <= 0.0f)
	{
		return nullptr;
	}

	// Nearest first, one result. FindEnemiesInSphere already sorts by distance
	// and already asks UCataclysmTeams which side everything is on, so this does
	// not repeat either.
	const TArray<AActor*> Nearby = UCataclysmTargeting::FindEnemiesInSphere(
		GetWorld(), Driven, Driven->GetActorLocation(), Sight, /*MaxTargets=*/1);

	return Nearby.IsEmpty() ? nullptr : Nearby[0];
}

ECataclysmBrainAction ACataclysmEnemyController::Think()
{
	ACataclysmCharacterBase* Driven = Body();
	if (!Driven)
	{
		LastAction = ECataclysmBrainAction::Idle;
		CurrentTarget = nullptr;
		return LastAction;
	}

	AActor* Target = ChooseTarget();
	CurrentTarget = Target;

	if (!Target)
	{
		// Stopped rather than left walking. A monster whose target died or
		// walked out of sight otherwise keeps following the last path it was
		// given, which reads as it chasing something that is not there.
		StopMovement();
		LastAction = ECataclysmBrainAction::Idle;
		return LastAction;
	}

	const float Reach = Driven->AttackReachCm();

	// HORIZONTAL DISTANCE, NOT 3D DISTANCE. Reach is a floor-plane question --
	// how close is it standing -- and capsule centres sit at different heights,
	// so a 3D distance charges melee reach for a height difference nobody chose.
	//
	// This matters because contact reach is exact. The model derives it as
	// PLAYER_BODY_RADIUS + body_radius (ring_distance in
	// sim/cataclysm_sim/enemy_abilities.py), which for the Brute is
	// 0.42 + 0.48 = 0.90 m, and the engine's capsules are the same 42 and 48 cm.
	// The player's capsule half-height is 96 and an enemy's is 80, so two of them
	// touching are 16 cm apart vertically. In 3D that is
	// sqrt(90^2 + 16^2) = 91.4 cm, which is greater than the 90 cm reach, so a
	// Brute pressed against the player would read as out of reach and chase for
	// ever without landing a hit.
	//
	// Nothing that existed before this changes behaviour: the training dummy's
	// reach is 200 cm and the imp's is well clear of contact, so both were
	// already far outside the margin this affects. Issue #373.
	const float Distance = FVector::Dist2D(
		Driven->GetActorLocation(), Target->GetActorLocation());

	if (Distance > Reach)
	{
		// MOVETOACTOR RATHER THAN MOVETOLOCATION, because the target moves. A
		// location is where the target was when the order was given; an actor is
		// re-followed as it walks.
		const float AcceptanceRadius = Reach * ApproachFractionOfReach;

		FAIMoveRequest Request(Target);
		Request.SetAcceptanceRadius(AcceptanceRadius);
		Request.SetUsePathfinding(true);

		// A STRAIGHT LINE WHEN THERE IS NO NAVIGATION MESH. Pathfinding needs a
		// NavMeshBoundsVolume in the level; without one the request fails and,
		// with no fallback, the pawn stands still and nothing says why -- the
		// same failure the player's click-to-move has, recorded on
		// UCataclysmLevelAuthoring::BuildNavigation. Walking straight at the
		// target ignores walls, which is wrong in a real level, but a level with
		// no navigation mesh is already broken and a monster that visibly walks
		// at you is a far easier fault to notice than one that does nothing.
		if (MoveTo(Request) == EPathFollowingRequestResult::Failed)
		{
			Request.SetUsePathfinding(false);
			MoveTo(Request);
		}

		LastAction = ECataclysmBrainAction::Chasing;
		return LastAction;
	}

	// In reach. Stop walking before hitting, so that a pawn does not push its
	// target along in front of it while attacking.
	StopMovement();
	LastAction = ECataclysmBrainAction::Attacking;

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	if (!bHasAttacked || Now - LastAttackTime >= Driven->SecondsBetweenAttacks())
	{
		LastAttackTime = Now;
		bHasAttacked = true;
		Driven->AttackTarget(Target);
		++AttacksOrdered;
	}

	return LastAction;
}
