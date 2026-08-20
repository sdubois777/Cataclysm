// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyController.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "Character/CataclysmCharacterBase.h"
// The charge lives on the enemy subclass rather than the shared base, so the
// brain has to know about it to ask whether one is running. Issue #499.
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"
#include "Save/CataclysmSaveWriter.h"

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
	WindUpPassesLeft = 0;
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

	// DEATH OUTRANKS EVERYTHING, INCLUDING A STUN. A dead creature does nothing
	// at all: it does not chase, does not swing, and does not finish a wind-up it
	// had committed to. Checked before the stun below, because being stunned is a
	// state a creature comes back from and being dead is not, and because
	// ACataclysmEnemyCharacter::HandleDeath has already stopped its movement -- a
	// pass that then asked it to chase would be arguing with that.
	//
	// IT IS ONLY BRIEFLY TRUE. The creature is destroyed on the next tick, so
	// this catches the passes between the killing blow and its removal. Before
	// issue #517 there were no such passes, because nothing ever died.
	if (UCataclysmSkillEffects::IsDead(Driven))
	{
		WindingUpAbility = INDEX_NONE;
		WindUpPassesLeft = 0;
		CurrentTarget = nullptr;
		LastAction = ECataclysmBrainAction::Idle;
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
		WindUpPassesLeft = 0;

		// AND A CHARGE ALREADY IN FLIGHT STOPS TOO. Issue #499. A stunned
		// creature "cannot act at all" by the design's own words, and one that
		// kept travelling would be acting.
		//
		// THE COMMITMENT RULE DOES NOT PROTECT IT, which is worth saying because
		// it reads as though it might. That rule says a charge runs its full
		// distance whether or not the TARGET is still there -- it is what stops
		// the attack tracking the player -- and it says nothing about crowd
		// control. Reading it the other way would make a charge the one attack
		// in the game that interrupting cannot answer.
		if (ACataclysmEnemyCharacter* Charger =
				Cast<ACataclysmEnemyCharacter>(Driven))
		{
			Charger->CancelCharge();
		}

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

	// A CHARGE ALREADY IN FLIGHT OUTRANKS EVERYTHING BELOW, and the brain does
	// NOTHING while it runs. Issue #499.
	//
	// WHY IT IS NEEDED. ContinueWindUp returns true only while the creature is
	// winding UP; it clears the wind-up and returns on the pass that LANDS the
	// ability, and for a charge that is the pass the travel STARTS on. Without
	// this test every pass afterwards ran the ordinary logic four times a second
	// on a creature in flight: it turned to face its target, ordered a walk
	// toward it, and stopped movement when it came within reach. The project
	// owner saw the first of those -- "he turns around mid charge to face you as
	// he's flying past".
	//
	// DOING NOTHING IS THE WHOLE POINT, so there is deliberately no FaceTarget
	// here. The control rotation was pointed down the lane by UseAbilitiesOn
	// before the wind-up began and nothing has moved it since, so leaving it
	// alone is what keeps the creature facing the way it is travelling. Setting
	// it again from the target's CURRENT position is exactly the defect.
	//
	// THE TRAVEL ITSELF IS NOT DRIVEN FROM HERE. AdvanceCharge runs from the
	// character's Tick, per frame, because a charge covers metres inside one
	// quarter-second thinking pass.
	//
	// AFTER THE STUN TEST, NOT BEFORE, so a stun still cancels it.
	if (const ACataclysmEnemyCharacter* Charger =
			Cast<ACataclysmEnemyCharacter>(Driven))
	{
		if (Charger->IsCharging())
		{
			// STOPPED ONCE A PASS, for the same reason the stun branch does it:
			// StopMovement is not sticky, and a charge moves the creature with
			// SetActorLocation, so any velocity the movement component still
			// carries would fight it.
			StopMovement();

			LastAction = ECataclysmBrainAction::Charging;
			return LastAction;
		}
	}

	// A WIND-UP ALREADY RUNNING OUTRANKS EVERYTHING, including looking for a
	// target at all. See ContinueWindUp for why a committed attack finishes
	// whether or not what it was aimed at is still there.
	if (ContinueWindUp(Driven))
	{
		return LastAction;
	}

	AActor* Target = ChooseTarget();

	// A FIGHT STARTS WHEN A CREATURE NOTICES SOMEBODY IT COULD NOT SEE BEFORE.
	// That is the first of the five events section 6 writes on, and this is
	// the only moment in the game that answers to the description. Losing a
	// target and finding it again counts as a second fight starting, which is
	// right: the player walked away and came back.
	const bool bJustNoticedSomebody = Target != nullptr && CurrentTarget == nullptr;

	CurrentTarget = Target;

	if (bJustNoticedSomebody)
	{
		UCataclysmSaveWriter::NoteTriggerIn(GetWorld(),
											ECataclysmSaveTrigger::FightStarted);
	}

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
		// BACK TO FACING WHERE IT WALKS. An ability that made it turn on the
		// spot left the movement component pointed at the controller's
		// rotation, and a creature that then chases would walk forward while
		// still facing wherever it last aimed. Issue #457.
		FaceTravelDirection(Driven);

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

	// TURNS TO FACE, BUT DOES NOT WAIT TO BE FACING. Issue #457 asked for
	// directional ABILITIES to wait, and an ordinary swing is deliberately not
	// held to that: the Brute turns at 180 degrees per second and a player
	// circling at its reach sweeps 223, so a swing that waited for facing would
	// make a Brute unable to land any melee hit at all on a player who keeps
	// circling. That is a balance change nobody asked for, and it should be a
	// decision rather than a side effect of this one. Turning without waiting
	// fixes the thing that was actually wrong -- a creature visibly swinging at
	// nothing while its target stands behind it.
	FaceTarget(Driven, Target);

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

bool ACataclysmEnemyController::AbilityNeedsFacing(
	const FCataclysmEnemyAbility& Ability)
{
	// A PROJECTILE AND A CHARGE ARE THE DIRECTIONAL SHAPES. See the header for
	// why a Strike must not require facing: the Brute's Stomp is a full ring
	// precisely so that standing behind it is not the answer to it.
	//
	// A CHARGE MUST WAIT TO BE FACING, AND MORE SO THAN A SHOT. The lane is
	// fixed when the wind-up starts and the creature travels down it, so a
	// charge begun while turned away does not merely miss -- it carries the
	// creature further from its target than it started. Issue #491.
	return Ability.Shape == ECataclysmSkillShape::Projectile
		|| Ability.Shape == ECataclysmSkillShape::Movement;
}

float ACataclysmEnemyController::DegreesOffTarget(const AActor* Driven,
												  const AActor* Target)
{
	if (!Driven || !Target)
	{
		// Reads as "facing". A missing actor must not leave a creature turning
		// on the spot for ever waiting for an angle that can never be measured.
		return 0.0f;
	}

	FVector ToTarget = Target->GetActorLocation() - Driven->GetActorLocation();
	ToTarget.Z = 0.0f;
	if (ToTarget.IsNearlyZero())
	{
		// Standing on the same spot. There is no direction to face, so any
		// facing is as correct as any other.
		return 0.0f;
	}

	FVector Forward = Driven->GetActorForwardVector();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return 0.0f;
	}

	const float Cosine = FVector::DotProduct(ToTarget.GetSafeNormal(),
											 Forward.GetSafeNormal());
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cosine, -1.0f, 1.0f)));
}

void ACataclysmEnemyController::FaceTarget(ACataclysmCharacterBase* Driven,
										   const AActor* Target)
{
	if (!Driven || !Target)
	{
		return;
	}

	UCharacterMovementComponent* Movement = Driven->GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// THE ORDER MATTERS. PhysicsRotation checks bOrientRotationToMovement first
	// and only falls through to the controller's rotation when it is off, so
	// leaving it on would make every line below do nothing.
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = true;

	FVector ToTarget = Target->GetActorLocation() - Driven->GetActorLocation();
	ToTarget.Z = 0.0f;
	if (ToTarget.IsNearlyZero())
	{
		return;
	}

	// YAW ONLY. The creature stands upright whatever height its target is at,
	// and RotationRate carries zero for pitch and roll in any case, so a pitch
	// here would be a value nothing could ever turn toward.
	FRotator Facing = ToTarget.Rotation();
	Facing.Pitch = 0.0f;
	Facing.Roll = 0.0f;
	SetControlRotation(Facing);
}

void ACataclysmEnemyController::FaceTravelDirection(
	ACataclysmCharacterBase* Driven)
{
	if (!Driven)
	{
		return;
	}

	if (UCharacterMovementComponent* Movement = Driven->GetCharacterMovement())
	{
		Movement->bUseControllerDesiredRotation = false;
		Movement->bOrientRotationToMovement = true;
	}
}

FVector ACataclysmEnemyController::FloorUnder(
	const ACataclysmCharacterBase* Driven, const FVector& Point)
{
	// AN ACTOR'S LOCATION IS ITS CAPSULE CENTRE, so dropping by the capsule's
	// half height is what turns a point about a character into a point on the
	// ground it is standing on.
	//
	// FROM THE CREATURE'S OWN CAPSULE, not the target's, because everything that
	// uses this has to agree with everything else that uses it and the creature
	// is the one constant. On a floor that is not level this is the creature's
	// floor rather than the target's, which is the same assumption the markers
	// have always made.
	float HalfHeightCm = 0.0f;
	if (Driven)
	{
		if (const UCapsuleComponent* Capsule = Driven->GetCapsuleComponent())
		{
			HalfHeightCm = Capsule->GetScaledCapsuleHalfHeight();
		}
	}

	const float FloorZ = Driven
		? Driven->GetActorLocation().Z - HalfHeightCm
		: Point.Z;
	return FVector(Point.X, Point.Y, FloorZ);
}

FVector ACataclysmEnemyController::AimPointFor(
	const ACataclysmCharacterBase* Driven, const AActor* Target,
	const FCataclysmEnemyAbility& Ability)
{
	if (!Driven)
	{
		return FVector::ZeroVector;
	}

	// A CHARGE AIMS AT THE END OF ITS OWN LANE, NOT AT ITS TARGET, and that is
	// the whole difference between this shape and the other two. Issue #491.
	//
	// The design says the creature is committed and runs the full distance
	// whether or not anything is still there -- "it ends up ten metres past the
	// player, facing away, and covering that ground again ... is the window the
	// telegraph buys". So the point that both the marker and the travel are
	// built from has to be the far end of the range, not wherever the target
	// happened to be standing.
	//
	// ALONG THE CREATURE'S FACING, WHICH IS WHY IT WAITED TO BE FACING.
	// AbilityNeedsFacing returns true for this shape, so UseAbilitiesOn has
	// already turned the creature at its target and returned Turning until it
	// was pointed there. Read here, that forward vector IS the aim.
	if (Ability.Shape == ECataclysmSkillShape::Movement)
	{
		FVector Forward = Driven->GetActorForwardVector();
		Forward.Z = 0.0f;
		Forward = Forward.GetSafeNormal();

		return FloorUnder(Driven,
						  Driven->GetActorLocation()
							  + Forward * Ability.MaxRangeCm);
	}

	// ON THE FLOOR, NOT AT THE TARGET'S MIDDLE. Issue #471. A character's actor
	// location is its capsule centre, which for the player is 96 cm up, and this
	// value is BOTH what the marker is drawn at and what the shot is aimed at.
	// Left unflattened, every marker flattened it for itself and the shot did
	// not, so the Brute's lobbed rock ended its flight a metre above the circle
	// it had drawn. `docs/DECISIONS.md` records the rule it broke: a telegraphed
	// attack that marks a place must arrive at that place.
	return FloorUnder(Driven, Target ? Target->GetActorLocation()
									 : Driven->GetActorLocation());
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
	const FVector Feet = FloorUnder(Driven, Driven->GetActorLocation());

	// THE MARKER LASTS AS LONG AS THE WIND-UP REALLY DOES, NOT AS LONG AS IT
	// IS DESIGNED TO. An ability lands on a whole thinking pass, so a
	// telegraph that is not a whole number of passes really runs to the pass
	// after it -- the Brute's stomp is designed at 1.4 seconds and lands at
	// 1.5. Drawn for the designed figure, the marker's own lifespan took it
	// off the floor a tenth of a second before the ring it warned about went
	// off. Found while counting passes for issue #413, in code added the
	// same day for issue #396.
	const float ShownForSeconds =
		PassesForWindUp(Ability.WindUpSeconds) * ThinkIntervalSeconds;

	switch (Ability.Shape)
	{
	case ECataclysmSkillShape::Strike:
		// CENTRED ON THE CREATURE, because that is where a Strike hits from.
		// The Brute's stomp sweeps from its own location, so the circle drawn
		// here is the circle that sweep will use.
		WindUpMarker = ACataclysmTelegraphMarker::ShowCircle(
			Driven, Feet, Ability.MarkerRadiusCm, ShownForSeconds);
		return;

	case ECataclysmSkillShape::Projectile:
		if (Ability.bArcsOntoItsTarget)
		{
			// WHERE IT COMES DOWN, because a lobbed shot passes over everything
			// between here and there. Marking the lane would tell the player to
			// leave ground on which nothing is going to happen, and a marker
			// that is wrong once is a marker they stop reading. Issue #459.
			//
			// DRAWN AT THE AIM POINT ITSELF, NOT AT A FLATTENED COPY OF IT.
			// Issue #471. This used to read the X and Y and substitute the
			// creature's floor height, which was right for the marker and left
			// the SHOT still aimed at the target's capsule centre, 96 cm up.
			// The rock therefore finished its flight a metre above the circle
			// this drew. WindUpAimedAt is now on the floor when it is captured,
			// so there is one point rather than two that have to agree.
			WindUpMarker = ACataclysmTelegraphMarker::ShowCircle(
				Driven, WindUpAimedAt, Ability.MarkerRadiusCm, ShownForSeconds);
			return;
		}

		// FROM THE CREATURE TO WHERE IT AIMED. Both ends lie on the ground at
		// the creature's feet, so the lane lies on the floor rather than tilting
		// toward wherever the target's capsule centre happened to be. The far
		// end needs no flattening here since issue #471, because WindUpAimedAt
		// is captured on the floor.
		WindUpMarker = ACataclysmTelegraphMarker::ShowLine(
			Driven, Feet, WindUpAimedAt, Ability.MarkerRadiusCm, ShownForSeconds);
		return;

	case ECataclysmSkillShape::Movement:
		// THE GROUND THE CREATURE WILL RUN OVER. The design's telegraph table
		// gives the Movement shape "the path the enemy will travel, of width
		// 2 x Radius", which is the same lane a flat projectile draws and is
		// drawn by the same call.
		//
		// TO THE END OF THE LANE, NOT TO THE TARGET. WindUpAimedAt holds the far
		// end of the charge for a Movement ability rather than the target's
		// feet -- see UseAbilitiesOn -- because a charge runs its full range and
		// ends PAST whatever it was aimed at. A lane drawn only as far as the
		// player would stop short of where the creature finishes, so the marker
		// would be telling the truth about the beginning of the charge and not
		// about the end of it.
		//
		// AND IT IS THE SAME POINT THE CHARGE ITSELF RUNS TO, which is the
		// discipline issue #471 established after the Brute's rock landed a
		// metre above the circle it had drawn: one point, read twice, rather
		// than two that have to agree.
		WindUpMarker = ACataclysmTelegraphMarker::ShowLine(
			Driven, Feet, WindUpAimedAt, Ability.MarkerRadiusCm, ShownForSeconds);
		return;

	default:
		// SelfBuff, Summon, Debuff, Aura and None. The first three have no
		// marker in the design at all and are answered by interrupting rather
		// than by walking out of. Aura does have one in
		// sim/cataclysm_sim/enemy_abilities.py and no enemy in the project has
		// one yet, so it is not built; when one is, it lands here.
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

int32 ACataclysmEnemyController::PassesForWindUp(float WindUpSeconds)
{
	if (WindUpSeconds <= 0.0f)
	{
		// No telegraph at all. The caller lands the ability at once and never
		// reaches the count, but a zero here would mean "already over" rather
		// than "one pass", so it is answered explicitly.
		return 0;
	}

	// CEILING, NOT ROUNDING. Rounding a 1.4 second telegraph to five passes
	// would land the stomp at 1.25 -- a tenth of a second sooner than the
	// player was told they had. See the header.
	return FMath::Max(1,
		FMath::CeilToInt(WindUpSeconds / ThinkIntervalSeconds));
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

	// COUNTED DOWN, NOT COMPARED. See WindUpPassesLeft in the header for why
	// comparing clocks alone decided a whole quarter of a second by coin toss.
	if (WindUpPassesLeft > 0)
	{
		--WindUpPassesLeft;
	}

	// EITHER CONDITION LANDS IT, AND THE COUNT IS THE ONE THAT NORMALLY DOES.
	//
	// The count is what makes this deterministic. A wind-up begun on one pass
	// lands exactly so many passes later whatever the frame rate is doing,
	// because nothing in that sentence involves a clock.
	//
	// THE CLOCK IS A SAFETY NET FOR TWO CASES. A hitch long enough to skip
	// several thinking passes would otherwise hold an attack open past its own
	// telegraph while the count worked through. And every automation test in
	// this project moves the world clock by hand and calls Think directly
	// rather than letting the timer run -- see AdvanceWorldClock in
	// CataclysmEnemyBehaviourTests.cpp -- so without it no test could land an
	// ability at all.
	//
	// NEITHER CAN LAND ONE EARLY. The count reaches zero on the pass whose
	// nominal time is the deadline, and the clock condition is the deadline
	// itself, so the earlier of the two is before it by at most the length of
	// the frame that pass ran on. Issue #413.
	if (WindUpPassesLeft > 0 && Now < WindUpLandsAt)
	{
		LastAction = ECataclysmBrainAction::WindingUp;
		return true;
	}

	// IT LANDS WHERE IT WAS MARKED, not where the target is now. That is the
	// whole of why walking out of a telegraph works.
	const int32 Landing = WindingUpAbility;
	WindingUpAbility = INDEX_NONE;
	WindUpPassesLeft = 0;

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
	const int32 Phase = Driven->CurrentPhase();
	for (int32 Index = 0; Index < Abilities.Num(); ++Index)
	{
		const FCataclysmEnemyAbility& Ability = Abilities[Index];

		// A PHASE THE FIGHT HAS NOT REACHED YET. **At most, not equal to**:
		// phases ADD and never take away, so an ability from phase 2 is still
		// there in phase 3. The research this rests on is recorded with issue
		// #354 in docs/DECISIONS.md.
		//
		// EVERY CREATURE BUT THE BOSS IS IN PHASE 1 FOR EVER and every ability
		// defaults to phase 1, so this skips nothing for six of the seven and
		// needs no special case for them.
		if (Ability.Phase > Phase)
		{
			continue;
		}
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

	// TURN FIRST, THEN WIND UP. Issue #457, and the project owner chose this
	// shape over turning during the wind-up on 2026-08-08.
	//
	// WHAT IT COSTS THE CREATURE, WHICH IS THE POINT. The Brute turns at 180
	// degrees per second, so a player who gets directly behind it costs it up
	// to a full second of turning BEFORE a wind-up that is itself a second.
	// That is two seconds of free hits for outmanoeuvring it, and it is the
	// design's "can be outmaneuvered" being worth something rather than being a
	// number in a file.
	//
	// WHAT IT BUYS THE PLAYER. The ground marker is drawn below, after this,
	// so a marker is only ever drawn by a creature already pointed at what it
	// is about to hit. A telegraph that appears while the creature faces
	// elsewhere is a telegraph that has lied once, and once is enough for a
	// player to stop reading the next one.
	//
	// ONLY A DIRECTIONAL ABILITY WAITS. A Strike is a ring around the creature
	// and requiring facing for one would hand back exactly the answer its full
	// circle exists to remove. See AbilityNeedsFacing.
	if (AbilityNeedsFacing(Abilities[Chosen])
		&& DegreesOffTarget(Driven, Target) > FacingToleranceDegrees)
	{
		FaceTarget(Driven, Target);
		LastAction = ECataclysmBrainAction::Turning;
		return LastAction;
	}

	// STILL FACED EVEN WHEN IT DID NOT HAVE TO WAIT, so that a creature which
	// was already pointed the right way holds that facing through the wind-up
	// instead of drifting, and so that a Strike faces its target while it
	// stomps even though it does not wait to.
	FaceTarget(Driven, Target);

	if (Abilities[Chosen].WindUpSeconds > 0.0f)
	{
		WindingUpAbility = Chosen;
		WindUpLandsAt = Now + Abilities[Chosen].WindUpSeconds;
		WindUpPassesLeft = PassesForWindUp(Abilities[Chosen].WindUpSeconds);
		// ON THE FLOOR, NOT AT THE TARGET'S MIDDLE. Issue #471. A character's
		// actor location is its capsule centre, which for the player is 96 cm
		// up, and this value is BOTH what the marker is drawn at and what the
		// shot is aimed at. Left unflattened, every marker flattened it for
		// itself and the shot did not, so the Brute's lobbed rock ended its
		// flight a metre above the circle it had drawn. `docs/DECISIONS.md`
		// records the rule it broke: a telegraphed attack that marks a place
		// must arrive at that place.
		WindUpAimedAt = AimPointFor(Driven, Target, Abilities[Chosen]);

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

	// No wind-up, so it lands at once. Aimed through the same helper as the
	// telegraphed path for the reason issue #471 records: an ability that reads
	// the aim point must not get a different answer depending on whether it was
	// telegraphed.
	AbilityLastUsedAt[Chosen] = Now;
	Driven->UseEnemyAbility(Chosen, Target,
							AimPointFor(Driven, Target, Abilities[Chosen]));
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

	// BACK TO FACING WHERE IT WALKS, for the same reason the chase does it. A
	// creature that aimed an ability, lost its target and then wandered off
	// would otherwise walk sideways for ever, still pointed at where the player
	// used to be. Issue #457.
	FaceTravelDirection(Driven);

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
