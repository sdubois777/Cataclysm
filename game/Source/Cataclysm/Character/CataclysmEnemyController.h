// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "CataclysmEnemyController.generated.h"

class ACataclysmCharacterBase;

/** What one pass of the controller's thinking decided to do. */
UENUM(BlueprintType)
enum class ECataclysmBrainAction : uint8
{
	/** Nothing hostile within sight, and this character does not roam. Standing still. */
	Idle,

	/** Something hostile is in sight but out of reach. Walking toward it. */
	Chasing,

	/** In reach. Hitting it, or waiting for the attack interval to elapse. */
	Attacking,

	/**
	 * Nothing hostile within sight, and this character roams. Walking to a
	 * point it picked for itself, or standing at one for a moment.
	 *
	 * LAST IN THE LIST RATHER THAN BESIDE Idle, WHICH IT READS LIKE. This is a
	 * UENUM, so the numeric values are part of how it is saved and replicated,
	 * and every existing test compares `static_cast<int32>` of one of these
	 * against another. Inserting a value in the middle silently renumbers
	 * Chasing and Attacking. Appending cannot.
	 */
	Roaming,

	/**
	 * Committed to a telegraphed ability and standing still until it lands.
	 *
	 * The design gives the wind-up five rules: the area is fixed when it starts
	 * and does not follow the player, the player may do anything during it,
	 * leaving the area avoids the attack completely, the enemy is committed
	 * once it starts, and interrupting cancels it. The first four are here. The
	 * fifth is not, because nothing in the project can interrupt anything yet.
	 *
	 * Appended, like Roaming, because this is a UENUM and inserting renumbers
	 * every value after it.
	 */
	WindingUp,
};

/**
 * Decides who a monster or a summoned imp attacks, and walks it there.
 *
 * WHY IN C++ RATHER THAN A BEHAVIOUR TREE. A behaviour tree and its blackboard
 * are binary `.uasset` files. Every other rule in this project is text a pull
 * request can show a diff of, and every other behaviour is covered by an
 * automation test that runs headless. Four states -- idle, roam, chase, attack
 * -- expressed as a tree would be assets nobody could review and nothing could
 * test, to say what this file says in text. A behaviour tree earns its cost when
 * the logic is deep enough that designers need to edit it without a programmer,
 * and issue #39's seven enemies are the point at which that is worth revisiting.
 *
 * ROAMING IS WHERE THAT COST STARTS TO SHOW. It added a state, a timer and an
 * anchor, and the states now have transitions between them rather than being a
 * flat choice each pass. One more feature of that size -- the wind-up and
 * telegraph issue #371 needs, most likely -- is the point to weigh the tree
 * again rather than assume the answer is still no.
 *
 * IT THINKS ON A TIMER, NOT ON TICK. Four times a second. A dungeon floor can
 * hold a great many monsters, and asking "who is nearest" sixty times a second
 * for each of them is a sphere overlap per monster per frame for an answer that
 * does not change that fast. The same reasoning already applies to
 * ACataclysmGroundZone's sweep.
 *
 * ROAMING IS OPT-IN, AND THAT IS THE POINT. A character roams only when its
 * RoamRadiusCm() is above zero, and the default on ACataclysmCharacterBase is
 * zero. So a monster that has not asked to roam still stands still with nothing
 * in sight, exactly as before, and a summoned imp does not wander away from the
 * fight its summoner made it for. Today only the Brute asks.
 *
 * WHAT IT DOES NOT DO. It has no memory: it re-picks the nearest target every
 * pass rather than staying with one, so two monsters equally distant can swap
 * between them. It has no leash: a monster that has noticed the player follows
 * for as long as the player stays within its sight radius, however far that
 * takes it from where it started. Roaming gives it somewhere to go back to --
 * the anchor below -- but nothing pulls it back while it can still see a
 * target. Diablo II gives each monster its own vision distance and Path of
 * Exile monsters break off and return when the player gets far enough; the
 * second of those is still missing.
 */
UCLASS()
class CATACLYSM_API ACataclysmEnemyController : public AAIController
{
	GENERATED_BODY()

public:
	ACataclysmEnemyController();

	/** Seconds between one pass of the thinking and the next. */
	static constexpr float ThinkIntervalSeconds = 0.25f;

	/**
	 * How near the target the walk aims for, as a fraction of the attack reach.
	 *
	 * Short of the reach rather than exactly at it, so that a target which
	 * shuffles slightly does not put the pawn just outside its own reach and
	 * make it stop and start.
	 */
	static constexpr float ApproachFractionOfReach = 0.8f;

	/**
	 * Extra centimetres allowed on the reach test, so that "touching" counts as
	 * "in reach".
	 *
	 * WITHOUT THIS THE BRUTE CANNOT RELIABLY ATTACK, reported from a play
	 * session on 2026-08-07 as "he doesn't actually attack when he reaches me".
	 *
	 * Its reach is 90 cm and that is exactly the sum of the two capsule radii,
	 * 48 and 42, because the model derives contact reach that way. Two capsules
	 * cannot overlap, so the closest the Brute can physically stand is also
	 * 90 cm. The chase therefore always ends at the single distance where the
	 * comparison has no margin, and a collision solver does not leave two
	 * bodies at exactly their touching distance -- it leaves them a hair
	 * outside it. A hair outside 90 is not within 90, so it chases for ever,
	 * pressed against the player, never attacking.
	 *
	 * NO OTHER ENEMY HAS THIS PROBLEM, which is why it went unnoticed. Every
	 * other reach in the project is 200 cm or more against the same capsules,
	 * so all of them have over a metre of margin. The Brute is the only one
	 * whose designed reach IS the contact distance.
	 *
	 * A JUDGEMENT AT TWO CENTIMETRES. It has to exceed the separation a
	 * collision solver leaves, which is well under a centimetre, and it has to
	 * stay far below anything the design distinguishes -- the closest two
	 * designed reaches are 90 and 200 cm apart. Two centimetres is 2% of the
	 * Brute's reach. It does NOT change the designed number, which stays 90 in
	 * both the model and the header; it changes only how exactly the engine
	 * insists on it. Issue #373 is the earlier half of this same problem.
	 */
	static constexpr float ContactToleranceCm = 2.0f;

	/**
	 * The least a roam leg is worth walking, as seconds of the character's own
	 * movement.
	 *
	 * WHY A MINIMUM AT ALL, reported from the same play session as the Brute
	 * taking "weird half steps one at a time, waits a few seconds, moves
	 * again". A roam target is a random reachable point, and nothing stopped it
	 * being a point the character is already standing on. When that happens it
	 * counts as arrived on the very next pass, without moving, and then stands
	 * through the full pause before drawing again. The result is a character
	 * that mostly stands still and twitches.
	 *
	 * ONE SECOND OF WALKING, so it scales with the character rather than being
	 * a distance picked for the Brute. At the Brute's 250 cm/s that is 250 cm,
	 * comfortably clear of the 50 cm arrival radius and well inside the 600 cm
	 * it may roam, so points that qualify always exist.
	 */
	static constexpr float ShortestWorthwhileRoamLegSeconds = 1.0f;

	/**
	 * How many times to re-draw a roam target that is too close before giving
	 * up and taking the last one anyway.
	 *
	 * Bounded because the draw can fail to find a distant point legitimately --
	 * a character boxed into a small room has nowhere far to go -- and looping
	 * until it succeeds would hang the game rather than produce a short walk.
	 */
	static constexpr int32 RoamTargetDrawAttempts = 6;

	/**
	 * Seconds a roaming character stands at a point it reached before choosing
	 * the next one.
	 *
	 * A JUDGEMENT, NOT A DESIGN FIGURE, and the design document has nothing to
	 * offer here: it contains no notice radius, patrol path, roam behaviour or
	 * leash distance for any of the seven enemies. Two seconds is long enough
	 * that the pause reads as the creature stopping rather than as a hitch in
	 * the pathing, and short enough that a Brute is not standing still for most
	 * of the time a player is watching it.
	 *
	 * Fixed rather than a range on purpose. A range would make two Brutes drift
	 * out of step, which is the reason to want one, but it also makes the
	 * behaviour untestable without either seeding the random stream or
	 * asserting on a band. It is the first thing to try if roaming reads as
	 * mechanical once there is more than one Brute on screen.
	 */
	static constexpr float RoamPauseSeconds = 2.0f;

	/**
	 * How near a roam target counts as having arrived, in centimetres.
	 *
	 * The character movement component stops short of a destination by its own
	 * acceptance radius, so a test for exact arrival never fires and the
	 * character stands on its target for ever. This is the same figure passed
	 * to the move request, so the two agree by construction.
	 */
	static constexpr float RoamAcceptanceRadiusCm = 50.0f;

	/**
	 * How much longer than the straight line a roam leg is allowed to take
	 * before the character gives up on it and picks somewhere else.
	 *
	 * Three, because a path around obstacles is longer than the straight line
	 * and a character also spends time turning at each corner. This is the
	 * safety net's generosity, not a pacing figure: a leg that finishes normally
	 * ends by arriving, long before the deadline. Too tight and a Brute
	 * abandons walks it was going to complete; too loose and a stuck one stands
	 * still for longer than a player would take to notice.
	 */
	static constexpr float RoamLegGenerosity = 3.0f;

	/**
	 * The least time a roam leg gets, in seconds, however short it is.
	 *
	 * Without a floor, a leg of a few centimetres would get a deadline of
	 * almost zero and be abandoned on the pass after it started.
	 */
	static constexpr float RoamLegMinimumSeconds = 2.0f;

	/**
	 * Choose a target, walk toward it, and hit it when it is in reach. With
	 * nothing to chase, roam if this character roams and stand still if not.
	 *
	 * Public and callable so that tests can run one pass without waiting for a
	 * timer, in the same way ACataclysmMinion::AttackOnce is.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|AI")
	ECataclysmBrainAction Think();

	/**
	 * Where to wander to next, or false if this character cannot pick a point.
	 *
	 * SEPARATE FROM THE STATE MACHINE ON PURPOSE, the same split
	 * ACataclysmBruteCharacter::AnimationForGroundSpeed makes from
	 * DriveLocomotion. This is the decision and Think is the application. A
	 * test can call this in any world and get a definite answer, which matters
	 * a great deal here: every automation test world is built by
	 * UWorld::CreateWorld and has no navigation mesh at all, so the navigation
	 * system's own answer is unavailable in exactly the place the behaviour
	 * most needs checking.
	 *
	 * THE NAVIGATION SYSTEM FIRST, A CIRCLE AROUND THE ANCHOR SECOND. With a
	 * navigation mesh, UNavigationSystemV1::GetRandomReachablePointInRadius
	 * returns somewhere the character can actually walk to, which is the
	 * answer that respects walls. Without one it falls back to a random point
	 * in the circle, which is what the chase already does in the same
	 * situation. The fallback is bounded where the chase's is not: the point is
	 * always within RoamRadiusCm of the anchor, so the worst it can do is walk
	 * the character a known distance from where it started, rather than
	 * following a target to anywhere at all.
	 *
	 * @param OutTarget  set to the chosen point when this returns true
	 * @return false when the character does not roam, or has no anchor yet
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|AI")
	bool ChooseRoamTarget(FVector& OutTarget) const;

	/**
	 * The move request used to walk at something it is chasing.
	 *
	 * SEPARATE SO A TEST CAN READ ITS FLAGS, which is the only way to check the
	 * thing this function exists to get right. What it does cannot be seen from
	 * outside otherwise: the request is handed to the path following component
	 * and the only visible consequence is where the character stops, which
	 * needs a navigation mesh no automation test world has.
	 *
	 * WHAT IT GETS RIGHT, AND WHY IT IS NOT THE DEFAULT. FAIMoveRequest is
	 * constructed with bReachTestIncludesAgentRadius and
	 * bReachTestIncludesGoalRadius both TRUE, verified in the engine source at
	 * Runtime/AIModule/Private/AITypes.cpp. That adds BOTH capsule radii to the
	 * acceptance radius, so a request that reads as "stop within 72 cm" really
	 * means "stop within 72 + 48 + 42 = 162 cm".
	 *
	 * For every other enemy that is invisible: a training dummy reaches 200 cm
	 * and 162 is inside it. For the Brute, whose reach is 90 cm, it means the
	 * chase ends a metre and a half short and it never attacks at all --
	 * reported from a play session on 2026-08-07 as "he doesn't attack when he
	 * reaches me". Both flags are turned off so the acceptance radius means the
	 * centre-to-centre distance the rest of this class measures in.
	 *
	 * The resulting 72 cm is closer than two capsules can physically get, so
	 * the request is never satisfied and the character presses up against its
	 * target. That is intended: arriving is not what triggers the attack. The
	 * reach test in Think does, and it calls StopMovement when it fires.
	 */
	FAIMoveRequest MakeChaseMoveRequest(AActor* Target, float ReachCm) const;

	/**
	 * Where it was when this controller took it over. Roaming is around here.
	 *
	 * AN ANCHOR RATHER THAN WHEREVER IT HAPPENS TO BE STANDING. Without one,
	 * each roam leg starts from the end of the last, which is a random walk: a
	 * Brute left alone long enough arrives anywhere the navigation mesh
	 * reaches. Anchoring to the spawn point bounds the whole behaviour to a
	 * circle, which is also what makes the sandbox safe -- see RoamRadiusCm on
	 * ACataclysmBruteCharacter for the arithmetic.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	FVector RoamAnchor = FVector::ZeroVector;

	/** Where it is currently walking to while roaming. Meaningless otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	FVector RoamTarget = FVector::ZeroVector;

	/** Whether RoamTarget is a place it is actually going. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	bool bHasRoamTarget = false;

	/**
	 * World seconds until which a roaming character stands at the point it
	 * reached. Zero means it is not pausing. Read by tests.
	 *
	 * PUBLIC SO THE PAUSE CAN BE CHECKED WITHOUT ADVANCING TIME. An automation
	 * test world built by UWorld::CreateWorld is never ticked, so
	 * GetTimeSeconds does not move and a test cannot wait two seconds for the
	 * pause to end. Reading the deadline proves the pause was set for the right
	 * length; that the character stays put until then is checked by calling
	 * Think again and finding it has not chosen a new target.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float RoamPauseUntil = 0.0f;

	/**
	 * World seconds by which the current roam leg must have finished, after
	 * which the character gives up on it and chooses somewhere else. Read by
	 * tests.
	 *
	 * The safety net against a walk that ends without arriving. See the comment
	 * on it in Roam for why this is a deadline rather than the path following
	 * component's own status.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float RoamLegDeadline = 0.0f;

	/**
	 * The nearest thing the possessed pawn may attack, or null.
	 *
	 * Nearest rather than chosen. Which side something is on is
	 * UCataclysmTeams's question, and it is what makes a maddened monster's
	 * neighbours legal targets: Madness makes an actor hostile to everything, so
	 * this search starts returning them without knowing anything about Madness.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|AI")
	AActor* ChooseTarget() const;

	/** What the last pass of Think decided. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	ECataclysmBrainAction LastAction = ECataclysmBrainAction::Idle;

	/** What the last pass of Think was going after. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	TObjectPtr<AActor> CurrentTarget;

	/** How many times it has told its pawn to attack. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	int32 AttacksOrdered = 0;

	/**
	 * Which entry of the pawn's EnemyAbilities it would use against a target at
	 * the given distance, or -1 for none of them.
	 *
	 * -1 MEANS THE ORDINARY ATTACK, not "do nothing". The ordinary attack has no
	 * cooldown, so an enemy in reach always has something to do; an enemy out of
	 * reach with nothing available keeps walking.
	 *
	 * SEPARATE FROM Think SO A TEST CAN ASK IT DIRECTLY, at any distance, with
	 * any cooldown state, without arranging a world in which that distance
	 * happens. It is the same split ChooseRoamTarget makes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|AI")
	int32 ChooseAbility(float DistanceCm) const;

	/** Whether that ability has finished cooling down. Read by tests. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|AI")
	bool IsAbilityReady(int32 Index) const;

	/** Which ability it is winding up, or -1. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	int32 WindingUpAbility = -1;

	/** World seconds at which the ability being wound up lands. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float WindUpLandsAt = 0.0f;

	/**
	 * Where the ability being wound up was aimed when it started.
	 *
	 * FIXED AT THE START AND NOT FOLLOWED, which is the design rule that makes
	 * walking out of a telegraph work: "the area is fixed when the wind-up
	 * starts and does not follow the player".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	FVector WindUpAimedAt = FVector::ZeroVector;

	/** How many abilities it has used. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	int32 AbilitiesUsed = 0;

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The possessed pawn as the class this controller knows how to drive. */
	ACataclysmCharacterBase* Body() const;

private:
	/**
	 * Roam, or stand still if this character does not roam.
	 *
	 * The whole of what Think does when there is nothing to chase. Separate
	 * only so that Think stays readable as four cases rather than three cases
	 * and a paragraph.
	 */
	ECataclysmBrainAction Roam();

	FTimerHandle ThinkTimer;

	/** World seconds at the last attack, so the interval can be honoured. */
	float LastAttackTime = 0.0f;

	/** False until the first attack, so the first one is never made to wait. */
	bool bHasAttacked = false;

	/**
	 * World seconds at which each ability was last used, indexed as
	 * EnemyAbilities is. Grown to fit on first use.
	 *
	 * A LARGE NEGATIVE START, so that every ability is off cooldown when a
	 * creature spawns rather than all of them waiting out one cooldown from a
	 * world time of zero. Using zero would have been the obvious choice and
	 * would make a Brute spawned into a fresh world unable to stomp for its
	 * first five seconds.
	 */
	TArray<float> AbilityLastUsedAt;

	/** Start each ability far enough in the past to be ready immediately. */
	static constexpr float NeverUsed = -1000000.0f;

	/**
	 * Carry on a wind-up already in progress, landing it if it is due.
	 *
	 * Called before the target search, because a committed attack finishes
	 * whether or not what it was aimed at is still there.
	 *
	 * @return true when it handled this pass, so Think should stop.
	 */
	bool ContinueWindUp(ACataclysmCharacterBase* Driven);

	/** Start an ability if one is in range and off cooldown. */
	ECataclysmBrainAction UseAbilitiesOn(ACataclysmCharacterBase* Driven,
										 AActor* Target, float DistanceCm);

	/**
	 * Whether the anchor has been recorded. Distinct from the anchor being the
	 * zero vector, which is a legitimate place to stand.
	 */
	bool bHasRoamAnchor = false;

};
