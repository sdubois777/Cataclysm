// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyController.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmCharacterBase.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
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

	// WHERE IT STARTED, RECORDED ONCE. Roaming is a circle around this, so a
	// character left alone wanders around where it was put rather than
	// random-walking across the level. Taken here rather than at the first roam
	// because by then it may have chased something a long way, and "where it
	// started" would silently come to mean "where it gave up".
	if (InPawn)
	{
		RoamAnchor = InPawn->GetActorLocation();
		bHasRoamAnchor = true;
	}

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

	// The anchor and the roam target belong to the pawn that is leaving. A
	// controller that possesses a second pawn without clearing them would walk
	// it to a point chosen for the first one, from an anchor somewhere else
	// entirely.
	bHasRoamAnchor = false;
	bHasRoamTarget = false;
	RoamPauseUntil = 0.0f;

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
		// WHAT USED TO BE HERE WAS AN UNCONDITIONAL StopMovement(). It was
		// right when standing still was the only thing to do with nothing in
		// sight, and it is a trap now: Think runs four times a second, so a
		// StopMovement on this path cancels every roam leg a quarter second
		// after it is ordered and the character never takes a step. Stopping is
		// now Roam's business, which does it once on arrival rather than every
		// pass.
		return Roam();
	}

	// IT HAS SOMETHING TO CHASE, SO WHATEVER IT WAS WALKING TO IS STALE. Left
	// set, the roam target it had before it noticed the player would be resumed
	// the moment the player left again, sending it back to a point it chose for
	// reasons that no longer exist.
	//
	// WHAT IT PICKS INSTEAD IS NEAR THE ANCHOR, NOT NEAR WHERE THE CHASE ENDED,
	// because ChooseRoamTarget always draws around RoamAnchor and the anchor is
	// set once at possession. So a character that chased the player across the
	// level and lost them walks back toward where it started. That is a soft
	// leash-return and it is a good thing, but it arrived as a consequence of
	// anchoring rather than as a designed rule -- there is still no leash while
	// the target is in sight, which is the part issue #383 asks for.
	bHasRoamTarget = false;
	RoamPauseUntil = 0.0f;

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

bool ACataclysmEnemyController::ChooseRoamTarget(FVector& OutTarget) const
{
	const ACataclysmCharacterBase* Driven = Body();
	if (!Driven || !bHasRoamAnchor)
	{
		return false;
	}

	const float RoamRadius = Driven->RoamRadiusCm();
	if (RoamRadius <= 0.0f)
	{
		return false;
	}

	// THE NAVIGATION SYSTEM'S ANSWER WHEN THERE IS ONE. It returns a point on
	// the navigation mesh that a path exists to, so it respects walls and it
	// never picks somewhere the character would walk into a corner trying to
	// reach. FNavigationSystem::GetCurrent is the accessor the rest of this
	// project uses; see UCataclysmLevelAuthoring.
	// NOT A const UWorld*, and the compiler is right to insist. FNavigationSystem
	// ::GetCurrent returns a const navigation system for a const world, and
	// GetRandomReachablePointInRadius is not a const member -- it can build
	// missing data as a side effect of being asked.
	if (UWorld* World = GetWorld())
	{
		if (UNavigationSystemV1* Navigation =
				FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			FNavLocation Reachable;
			if (Navigation->GetRandomReachablePointInRadius(
					RoamAnchor, RoamRadius, Reachable))
			{
				OutTarget = Reachable.Location;
				return true;
			}
		}
	}

	// A CIRCLE AROUND THE ANCHOR WHEN THERE IS NOT, and this branch is not only
	// for broken levels: every automation test world is built by
	// UWorld::CreateWorld, which has no navigation system at all, so without
	// this there would be no way to check any of the roaming behaviour headless.
	//
	// SAFE IN A WAY THE CHASE'S EQUIVALENT IS NOT. The chase falls back to
	// walking straight at a target that could be anywhere. This point is always
	// within RoamRadiusCm of the anchor, so the character cannot end up further
	// from where it started than the radius its own class asked for. That is
	// what makes the fallback acceptable rather than merely convenient.
	//
	// SQUARE ROOT ON THE RADIUS, which is not decoration. Drawing the distance
	// uniformly puts half the points inside the half-radius circle, which is a
	// quarter of the area, so the character would spend most of its time near
	// the anchor and the roam would read as fidgeting. The square root spreads
	// them evenly over the disc.
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const float Distance = RoamRadius * FMath::Sqrt(FMath::FRand());

	OutTarget = RoamAnchor
		+ FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f);
	return true;
}

ECataclysmBrainAction ACataclysmEnemyController::Roam()
{
	ACataclysmCharacterBase* Driven = Body();

	// NOT A ROAMER, SO THE OLD BEHAVIOUR EXACTLY. Standing still with nothing in
	// sight is what every character in this project did before roaming existed,
	// and it is still what a summoned imp and a stationary enemy should do.
	if (!Driven || Driven->RoamRadiusCm() <= 0.0f)
	{
		StopMovement();
		bHasRoamTarget = false;
		LastAction = ECataclysmBrainAction::Idle;
		return LastAction;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	// Standing at the point it reached, waiting out the pause.
	if (Now < RoamPauseUntil)
	{
		LastAction = ECataclysmBrainAction::Roaming;
		return LastAction;
	}

	if (bHasRoamTarget)
	{
		// ARRIVED? Measured in the horizontal plane, for the same reason the
		// reach comparison is: the character's capsule centre and a point on
		// the floor are at different heights, and charging the arrival test for
		// that difference would mean it never fires. Issue #373 records the
		// same mistake made against the attack reach.
		const float Remaining = FVector::Dist2D(Driven->GetActorLocation(), RoamTarget);

		// OR IT HAS TAKEN LONGER THAN THE WALK COULD POSSIBLY TAKE, which is
		// what stops a character freezing for good. A roam move is ordered once
		// and never re-issued, so anything that ends the walk short of the
		// target -- blocked by geometry, the path invalidated, the request
		// aborted, or simply stopping just outside the acceptance radius --
		// otherwise leaves it standing still holding a target it will never
		// reach and nothing to notice.
		//
		// A DEADLINE RATHER THAN THE PATH FOLLOWING STATUS, and that is measured
		// rather than preferred. The obvious test is GetMoveStatus() == Idle,
		// and it is wrong here: on 2026-08-07 a world built by UWorld::CreateWorld
		// -- every automation test world, and the case with no navigation mesh
		// that the circle fallback exists to serve -- was found to accept the
		// move request and report Idle immediately. Using the status would make
		// the character "arrive" on the pass after setting off without having
		// moved at all, so the fallback would never walk anywhere. A test caught
		// it by failing.
		const bool bTakenTooLong = Now >= RoamLegDeadline;

		if (Remaining <= RoamAcceptanceRadiusCm || bTakenTooLong)
		{
			StopMovement();
			bHasRoamTarget = false;
			RoamPauseUntil = Now + RoamPauseSeconds;
			LastAction = ECataclysmBrainAction::Roaming;
			return LastAction;
		}

		// Still walking. Deliberately no second MoveTo: the path following
		// component is already carrying out the first one, and re-issuing it
		// four times a second restarts the path and makes the character stutter.
		LastAction = ECataclysmBrainAction::Roaming;
		return LastAction;
	}

	// Nothing chosen and nothing to wait for, so pick somewhere and set off.
	FVector Chosen = FVector::ZeroVector;
	if (!ChooseRoamTarget(Chosen))
	{
		StopMovement();
		LastAction = ECataclysmBrainAction::Idle;
		return LastAction;
	}

	RoamTarget = Chosen;
	bHasRoamTarget = true;

	FAIMoveRequest Request(RoamTarget);
	Request.SetAcceptanceRadius(RoamAcceptanceRadiusCm);
	Request.SetUsePathfinding(true);

	// The same fallback the chase makes, for the same reason. A roam target
	// chosen by the circle rather than by the navigation system has no path to
	// follow, so without this the request fails and the character stands still
	// holding a target it never walks to.
	if (MoveTo(Request) == EPathFollowingRequestResult::Failed)
	{
		Request.SetUsePathfinding(false);
		MoveTo(Request);
	}

	// HOW LONG THIS LEG IS ALLOWED TO TAKE. The straight-line time at the
	// character's own walking speed, times a generous factor because a path
	// around obstacles is longer than the straight line, plus a floor so that a
	// very short leg still gets a sensible allowance. This is a safety net for
	// a walk that ends early, not a pacing mechanism: a leg that finishes
	// normally is detected by arriving, long before this.
	const float Speed = Driven->GetCharacterMovement()
		? Driven->GetCharacterMovement()->GetMaxSpeed() : 0.0f;
	const float StraightLineSeconds = Speed > 0.0f
		? FVector::Dist2D(Driven->GetActorLocation(), RoamTarget) / Speed
		: 0.0f;
	RoamLegDeadline = Now + FMath::Max(
		RoamLegMinimumSeconds, StraightLineSeconds * RoamLegGenerosity);

	LastAction = ECataclysmBrainAction::Roaming;
	return LastAction;
}
