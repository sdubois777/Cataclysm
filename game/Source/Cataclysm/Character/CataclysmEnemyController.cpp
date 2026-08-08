// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyController.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "Character/CataclysmCharacterBase.h"
#include "Components/CapsuleComponent.h"
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

	// A wind-up belongs to the pawn that started it. Left set, a controller
	// that possessed a second pawn would land the first one's attack.
	WindingUpAbility = INDEX_NONE;
	AbilityLastUsedAt.Reset();

	// AND SO DOES ITS MARKER. A creature killed part way through a stomp is the
	// ordinary case here, and its circle has to go with it rather than stay on
	// the floor warning about an attack that will never arrive.
	DismissWindUpMarker();

	Super::OnUnPossess();
}

void ACataclysmEnemyController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ThinkTimer);
	}

	DismissWindUpMarker();

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

	// A STUN OUTRANKS EVEN A COMMITTED WIND-UP, and it is the only thing that
	// does. The design says a stunned target cannot act at all, and the wind-up
	// rules say interrupting one cancels it -- the rule ECataclysmBrainAction
	// records as the one nothing could satisfy, because nothing could interrupt.
	//
	// THE WIND-UP IS ABANDONED RATHER THAN PAUSED. Left set, WindUpLandsAt would
	// still hold a deadline in the past by the time the stun wore off, so the
	// first pass afterwards would land a stomp the player had already survived
	// the telegraph of. Clearing it means an interrupted attack simply did not
	// happen.
	//
	// IT IS NOT PUT ON COOLDOWN EITHER. AbilityLastUsedAt is stamped when an
	// ability LANDS, in ContinueWindUp, so an attack that was interrupted before
	// it landed was never spent and is available again. That is what makes
	// interrupting worth doing to the enemy rather than to the player.
	if (UCataclysmSkillEffects::IsStunned(Driven))
	{
		WindingUpAbility = INDEX_NONE;

		// THE MARKER GOES WITH THE ATTACK IT WARNED OF. An interrupted wind-up
		// did not happen, so leaving its circle on the floor would be telling
		// the player to walk out of ground where nothing is now going to land.
		// Do that a few times and they stop reading the next one.
		DismissWindUpMarker();

		// Every pass, not once. Think runs four times a second and StopMovement
		// is not sticky -- the comment below on the roaming path records this
		// same trap biting there.
		StopMovement();

		LastAction = ECataclysmBrainAction::Stunned;
		return LastAction;
	}

	// A WIND-UP ALREADY RUNNING OUTRANKS EVERYTHING, including looking for a
	// target at all. See ContinueWindUp for why a committed attack finishes
	// whether or not what it was aimed at is still there.
	if (ContinueWindUp(Driven))
	{
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

	// ABILITIES BEFORE EVERYTHING ELSE, INCLUDING BEFORE THE CHASE. An ability
	// that reaches beyond the melee reach is the only reason a creature would
	// stop walking at something it cannot touch yet, so asking here rather than
	// after the reach test is what lets a Brute throw a rock while closing.
	// Returns Idle when it has nothing to use, and the ordinary logic below
	// then runs unchanged.
	const ECataclysmBrainAction Used = UseAbilitiesOn(Driven, Target, Distance);
	if (Used != ECataclysmBrainAction::Idle)
	{
		return Used;
	}

	// PLUS A TOLERANCE, because the Brute's reach IS its contact distance and a
	// collision solver never leaves two touching capsules at exactly the
	// distance where they touch. See ContactToleranceCm for the measurement
	// this comes from and why no other enemy needs it.
	if (Distance > Reach + ContactToleranceCm)
	{
		FAIMoveRequest Request = MakeChaseMoveRequest(Target, Reach);

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

void ACataclysmEnemyController::ShowWindUpMarker(
	ACataclysmCharacterBase* Driven, const FCataclysmEnemyAbility& Ability)
{
	// Whatever is still up from a previous wind-up goes first. Nothing should
	// reach here with one still drawn, and if something ever does, one marker
	// on the floor is a bug and two is a bug that is also confusing.
	DismissWindUpMarker();

	if (!Driven)
	{
		return;
	}

	// ON THE GROUND, NOT AT THE CREATURE'S MIDDLE. An actor's location is its
	// capsule centre, which for a Brute is 130 cm up. A marker drawn there
	// floats at chest height and reads as a wall rather than as a patch of
	// floor.
	float FeetOffsetCm = 0.0f;
	if (const UCapsuleComponent* Capsule = Driven->GetCapsuleComponent())
	{
		FeetOffsetCm = Capsule->GetScaledCapsuleHalfHeight();
	}
	const FVector Feet = Driven->GetActorLocation() - FVector(0.0f, 0.0f, FeetOffsetCm);

	switch (Ability.Shape)
	{
	case ECataclysmSkillShape::Strike:
		// CENTRED ON THE CREATURE, because that is where a Strike hits from.
		// The Brute's stomp sweeps from its own location, so the circle drawn
		// here is the circle that sweep will use.
		WindUpMarker = ACataclysmTelegraphMarker::ShowCircle(
			Driven, Feet, Ability.MarkerRadiusCm, Ability.WindUpSeconds);
		return;

	case ECataclysmSkillShape::Projectile:
		// FROM THE CREATURE TO WHERE IT AIMED. Flattened to the ground at the
		// creature's feet at both ends, so the lane lies on the floor rather
		// than tilting toward wherever the target's capsule centre happened to
		// be.
		WindUpMarker = ACataclysmTelegraphMarker::ShowLine(
			Driven, Feet,
			FVector(WindUpAimedAt.X, WindUpAimedAt.Y, Feet.Z),
			Ability.MarkerRadiusCm, Ability.WindUpSeconds);
		return;

	default:
		// SelfBuff, Summon, Debuff, Aura, Movement and None. The first three
		// have no marker in the design at all and are answered by interrupting
		// rather than by walking out of. Aura and Movement do have one in
		// sim/cataclysm_sim/enemy_abilities.py and no enemy in the project has
		// either yet, so neither is built; when one is, it lands here.
		return;
	}
}

void ACataclysmEnemyController::DismissWindUpMarker()
{
	if (ACataclysmTelegraphMarker* Marker = WindUpMarker.Get())
	{
		Marker->Dismiss();
	}
	WindUpMarker = nullptr;
}

bool ACataclysmEnemyController::ContinueWindUp(ACataclysmCharacterBase* Driven)
{
	if (WindingUpAbility == INDEX_NONE || !Driven)
	{
		return false;
	}

	// COMMITTED MEANS COMMITTED, INCLUDING WHEN THE TARGET IS GONE. This runs
	// before the target search rather than after it, and that is a behaviour
	// decision rather than an ordering detail: an enemy that has begun a stomp
	// finishes it even if what it was aiming at died or walked out of sight.
	// The attack was already aimed at a fixed point, so there is nothing left
	// for a target to contribute.
	//
	// It also closes a hole. Written the other way round, losing the target
	// mid-wind-up left WindingUpAbility set and never cleared, so the next
	// thing the creature noticed was hit by an attack it started long ago and
	// aimed somewhere else.
	StopMovement();

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	if (Now < WindUpLandsAt)
	{
		LastAction = ECataclysmBrainAction::WindingUp;
		return true;
	}

	// IT LANDS WHERE IT WAS MARKED, not where the target is now. That is the
	// whole of why walking out of a telegraph works.
	const int32 Landing = WindingUpAbility;
	WindingUpAbility = INDEX_NONE;

	// TAKEN AWAY BEFORE THE ATTACK RESOLVES, NOT AFTER. The marker's job is
	// finished the moment there is nothing left to walk out of, and removing it
	// first means the frame the blow lands on is the frame the warning goes,
	// rather than the two overlapping.
	DismissWindUpMarker();

	if (AbilityLastUsedAt.IsValidIndex(Landing))
	{
		AbilityLastUsedAt[Landing] = Now;
	}
	Driven->UseEnemyAbility(Landing, CurrentTarget.Get(), WindUpAimedAt);
	++AbilitiesUsed;

	// AN ABILITY THAT LANDS COUNTS AS AN ATTACK FOR THE ATTACK INTERVAL.
	//
	// WHAT THIS FIXED, reported from a play session on 2026-08-08 as the slam
	// ending part way through. Without it, the ordinary swing was free to start
	// on the very next thinking pass -- a quarter of a second later -- and the
	// swing animation replaced the stomp's release clip after 0.25 of its 0.70
	// seconds. The attack visibly stopped a third of the way in.
	//
	// IT IS ALSO RIGHT ON ITS OWN TERMS, animation aside. A Brute that stomps
	// and then swings a quarter of a second later has attacked twice in the
	// time its designed interval allows one attack, so the interval was not
	// doing what it says.
	LastAttackTime = Now;
	bHasAttacked = true;

	LastAction = ECataclysmBrainAction::Attacking;
	return true;
}

bool ACataclysmEnemyController::IsAbilityReady(int32 Index) const
{
	const ACataclysmCharacterBase* Driven = Body();
	if (!Driven)
	{
		return false;
	}

	const TArray<FCataclysmEnemyAbility> Abilities = Driven->EnemyAbilities();
	if (!Abilities.IsValidIndex(Index))
	{
		return false;
	}

	if (Abilities[Index].CooldownSeconds <= 0.0f)
	{
		return true;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	const float LastUsed = AbilityLastUsedAt.IsValidIndex(Index)
		? AbilityLastUsedAt[Index] : NeverUsed;

	return Now - LastUsed >= Abilities[Index].CooldownSeconds;
}

int32 ACataclysmEnemyController::ChooseAbility(float DistanceCm) const
{
	const ACataclysmCharacterBase* Driven = Body();
	if (!Driven)
	{
		return INDEX_NONE;
	}

	// FIRST IN RANGE AND OFF COOLDOWN WINS. The order of EnemyAbilities is the
	// priority order, which is the pawn's business rather than the brain's:
	// only the creature knows which of its attacks it would rather land.
	const TArray<FCataclysmEnemyAbility> Abilities = Driven->EnemyAbilities();
	for (int32 Index = 0; Index < Abilities.Num(); ++Index)
	{
		const FCataclysmEnemyAbility& Ability = Abilities[Index];
		if (DistanceCm < Ability.MinRangeCm || DistanceCm > Ability.MaxRangeCm)
		{
			continue;
		}
		if (!IsAbilityReady(Index))
		{
			continue;
		}
		return Index;
	}

	return INDEX_NONE;
}

ECataclysmBrainAction ACataclysmEnemyController::UseAbilitiesOn(
	ACataclysmCharacterBase* Driven, AActor* Target, float DistanceCm)
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	const int32 Chosen = ChooseAbility(DistanceCm);
	if (Chosen == INDEX_NONE)
	{
		return ECataclysmBrainAction::Idle;
	}

	// THE ATTACK INTERVAL GATES ABILITIES TOO, NOT ONLY THE ORDINARY SWING.
	//
	// Until 2026-08-08 an ability was held back by its own cooldown and by
	// nothing else, so a creature could land one attack and begin another on
	// the very next thinking pass a quarter of a second later. The Brute did
	// exactly that: it stomped and then immediately began a rock throw, whose
	// wind-up animation replaced the stomp's release clip part way through.
	// Reported from a play session as the slam ending half way.
	//
	// WHAT THE INTERVAL MEANS, WHICH IS THE REAL ARGUMENT. It is how often this
	// creature attacks. A Brute that stomps and throws a rock inside one
	// interval has attacked twice in the time the design allows one attack, and
	// the interval was not describing the creature. Cooldowns say how often a
	// PARTICULAR ability may be used; the interval says how often ANY attack
	// may start. Both have to hold.
	//
	// RETURNING Idle MEANS "NO ABILITY", not "do nothing" -- see ChooseAbility.
	// The caller falls through to chasing or to the ordinary swing, and the
	// ordinary swing has always been gated by this same interval.
	if (bHasAttacked && Driven->SecondsBetweenAttacks() > 0.0f
		&& Now - LastAttackTime < Driven->SecondsBetweenAttacks())
	{
		return ECataclysmBrainAction::Idle;
	}

	const TArray<FCataclysmEnemyAbility> Abilities = Driven->EnemyAbilities();
	if (AbilityLastUsedAt.Num() < Abilities.Num())
	{
		AbilityLastUsedAt.SetNum(Abilities.Num());
		for (int32 Index = 0; Index < AbilityLastUsedAt.Num(); ++Index)
		{
			if (AbilityLastUsedAt[Index] == 0.0f)
			{
				AbilityLastUsedAt[Index] = NeverUsed;
			}
		}
	}

	StopMovement();

	if (Abilities[Chosen].WindUpSeconds > 0.0f)
	{
		WindingUpAbility = Chosen;
		WindUpLandsAt = Now + Abilities[Chosen].WindUpSeconds;
		WindUpAimedAt = Target ? Target->GetActorLocation()
							   : Driven->GetActorLocation();

		// DRAWN BEFORE THE ANIMATION STARTS, AND AFTER WindUpAimedAt IS FIXED.
		// After, because a lane marker runs to the point the shot was aimed at
		// and would otherwise be drawn to last pass's aim. Before the character
		// hook, because that hook is where a subclass does its own thing and
		// this must not depend on what it does. Issue #396.
		ShowWindUpMarker(Driven, Abilities[Chosen]);

		Driven->BeginEnemyAbilityWindUp(Chosen, Target);

		LastAction = ECataclysmBrainAction::WindingUp;
		return LastAction;
	}

	// No wind-up, so it lands at once.
	AbilityLastUsedAt[Chosen] = Now;
	Driven->UseEnemyAbility(Chosen, Target,
							Target ? Target->GetActorLocation()
								   : Driven->GetActorLocation());
	++AbilitiesUsed;

	LastAction = ECataclysmBrainAction::Attacking;
	return LastAction;
}

FAIMoveRequest ACataclysmEnemyController::MakeChaseMoveRequest(
	AActor* Target, float ReachCm) const
{
	// MOVETOACTOR RATHER THAN MOVETOLOCATION, because the target moves. A
	// location is where the target was when the order was given; an actor is
	// re-followed as it walks.
	FAIMoveRequest Request(Target);
	Request.SetAcceptanceRadius(ReachCm * ApproachFractionOfReach);
	Request.SetUsePathfinding(true);

	// BOTH OFF, AND THIS IS THE WHOLE POINT OF THE FUNCTION. They default to
	// true, which quietly adds both capsule radii to the acceptance radius and
	// stops the Brute 162 cm from a target it can only hit at 90. See the
	// header for the measurement.
	Request.SetReachTestIncludesAgentRadius(false);
	Request.SetReachTestIncludesGoalRadius(false);

	return Request;
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
	//
	// SOMEWHERE WORTH WALKING TO, not merely somewhere. A random reachable
	// point can land where the character is already standing, and one that does
	// counts as arrived on the very next pass without a step being taken, then
	// holds the full pause before drawing again. Several of those in a row is a
	// character that stands still and twitches, which is what a play session on
	// 2026-08-07 reported. Re-drawing a handful of times costs nothing and
	// removes it.
	const float Speed = Driven->GetCharacterMovement()
		? Driven->GetCharacterMovement()->GetMaxSpeed() : 0.0f;
	const float ShortestWorthwhileLegCm =
		Speed * ShortestWorthwhileRoamLegSeconds;

	FVector Chosen = FVector::ZeroVector;
	bool bChose = false;
	for (int32 Attempt = 0; Attempt < RoamTargetDrawAttempts; ++Attempt)
	{
		if (!ChooseRoamTarget(Chosen))
		{
			break;
		}
		bChose = true;

		// GOOD ENOUGH, SO STOP DRAWING. The last draw is kept even when every
		// attempt was too close, because a character with nowhere far to go
		// should still shuffle rather than freeze.
		if (FVector::Dist2D(Driven->GetActorLocation(), Chosen)
				>= ShortestWorthwhileLegCm)
		{
			break;
		}
	}

	if (!bChose)
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
	//
	// Speed is the one read above, when the shortest worthwhile leg was worked
	// out; both want the same number.
	const float StraightLineSeconds = Speed > 0.0f
		? FVector::Dist2D(Driven->GetActorLocation(), RoamTarget) / Speed
		: 0.0f;
	RoamLegDeadline = Now + FMath::Max(
		RoamLegMinimumSeconds, StraightLineSeconds * RoamLegGenerosity);

	LastAction = ECataclysmBrainAction::Roaming;
	return LastAction;
}
