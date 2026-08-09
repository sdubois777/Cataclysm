// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "CataclysmEnemyController.generated.h"

class ACataclysmCharacterBase;
class ACataclysmTelegraphMarker;
struct FCataclysmEnemyAbility;

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

	/**
	 * Stunned. Not acting at all until it wears off.
	 *
	 * THIS IS THE FIFTH WIND-UP RULE ARRIVING. The entry above says interrupting
	 * a wind-up cancels it and that nothing could interrupt anything; a stun is
	 * the first thing that can, and Think abandons a wind-up in progress rather
	 * than resuming it, so a stomp interrupted half a second in does not land.
	 *
	 * Appended, like Roaming and WindingUp, because this is a UENUM and
	 * inserting renumbers every value after it.
	 */
	Stunned,

	/**
	 * Standing still and turning to face the target, before a directional
	 * ability it is not yet pointed at may begin.
	 *
	 * WHY IT IS ITS OWN STATE RATHER THAN PART OF Attacking. A creature that is
	 * turning has chosen what to do and cannot do it yet, which is neither
	 * "walking toward you" nor "hitting you". A test needs to tell a Brute that
	 * is turning apart from a Brute that has stalled, and so does anyone reading
	 * a log.
	 *
	 * Appended, like Roaming, WindingUp and Stunned, because this is a UENUM and
	 * inserting renumbers every value after it.
	 */
	Turning,
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
	 * ACataclysmBruteCharacter::PlayOneShot makes when it records
	 * LastPlayedAnimation before asking anything to play it. This is the
	 * decision and Think is the application. A
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

	/**
	 * How many thinking passes a telegraph of this length takes.
	 *
	 * ROUNDED UP, NEVER DOWN, AND THAT IS THE PLAYER'S GUARANTEE. The design
	 * states a telegraph as the time the player has to walk clear, so an
	 * attack must never land sooner than it. A wind-up that is not a whole
	 * number of passes therefore takes the pass after it: the Brute's 1.4
	 * second stomp takes six passes and lands at 1.5, which is what it already
	 * did before this was counted rather than compared.
	 *
	 * AT LEAST ONE, so that an ability with a very short telegraph is still
	 * committed to for a pass rather than landing on the pass that began it.
	 *
	 * Static and public so a test can check the arithmetic without arranging a
	 * world in which each length happens.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|AI")
	static int32 PassesForWindUp(float WindUpSeconds);

	/** Which ability it is winding up, or -1. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	int32 WindingUpAbility = -1;

	/** World seconds at which the ability being wound up lands. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float WindUpLandsAt = 0.0f;

	/**
	 * Thinking passes still to go before the ability being wound up lands.
	 *
	 * WHY A COUNT AND NOT JUST THE CLOCK ABOVE. Issue #413. Landing on "the
	 * first pass whose clock has passed WindUpLandsAt" sounds exact and is not.
	 * A timer callback runs on the first FRAME past its deadline, so every pass
	 * carries up to a frame of overshoot, and the overshoot on the pass that
	 * starts a wind-up is not the overshoot on the pass that should land it. The
	 * comparison then turns a difference of a few milliseconds into a difference
	 * of a whole quarter of a second.
	 *
	 * IT ONLY SHOWS WHERE A TELEGRAPH SITS ON A PASS BOUNDARY, which is exactly
	 * where the Brute's rock throw sits. Its wind-up is 1.000 second and a pass
	 * is 0.250, so the deadline and the pass coincide. Simulating the engine's
	 * own timer arithmetic over 500 jittery frames between 50 and 70 frames a
	 * second landed it on the LATER pass 246 times out of 500: half at 1.00 and
	 * half at 1.25. The stomp never moved, because its 1.400 second wind-up sits
	 * 0.1 clear of the 1.500 boundary, which is far more than a frame.
	 *
	 * WHY IT SURVIVED. It lands at exactly 1.00 whenever a quarter second is a
	 * whole number of frames, which is true at 30, 60, 120 and 144 frames a
	 * second held steady. Every controlled test sees the good case.
	 *
	 * WHAT THE COUNT DOES. PassesForWindUp turns the telegraph into a number of
	 * passes once, when the wind-up starts. Counting them down cannot be
	 * affected by frame timing at all, so the same telegraph always takes the
	 * same number of passes.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	int32 WindUpPassesLeft = 0;

	/**
	 * Where the ability being wound up was aimed when it started.
	 *
	 * FIXED AT THE START AND NOT FOLLOWED, which is the design rule that makes
	 * walking out of a telegraph work: "the area is fixed when the wind-up
	 * starts and does not follow the player".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	FVector WindUpAimedAt = FVector::ZeroVector;

	/**
	 * The ground marker drawn for the wind-up in progress, or null.
	 *
	 * ON THE CONTROLLER RATHER THAN ON THE CHARACTER, and that is what makes
	 * every enemy telegraph rather than only the ones that remember to. The
	 * controller already owns the whole wind-up: it decides which ability
	 * starts, fixes the point it is aimed at, knows when it lands, and knows
	 * when a stun abandons it. A character's BeginEnemyAbilityWindUp is a hook a
	 * subclass may override without calling its parent -- the Brute's does --
	 * so drawing from there would be a rule each enemy could silently opt out
	 * of.
	 *
	 * WEAK, because the marker carries its own lifespan as a backstop and can
	 * go away underneath this.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	TWeakObjectPtr<ACataclysmTelegraphMarker> WindUpMarker;

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

	/**
	 * The same point, moved down to the floor the creature is standing on.
	 *
	 * WHY EVERYTHING GOES THROUGH ONE FUNCTION. Issue #471. A character's actor
	 * location is its capsule centre -- 96 cm up for the player, 110 for a Brute
	 * -- and an aim point is a place on the GROUND. Before this, each marker
	 * flattened the aim point for itself and the thing the marker promised did
	 * not, so the Brute's lobbed rock ended its flight a metre above the circle
	 * that had been drawn for it. Two expressions that have to agree will stop
	 * agreeing; one expression cannot.
	 *
	 * FROM THE CREATURE'S CAPSULE rather than the target's, because every caller
	 * has to get the same answer and the creature is the one constant.
	 */
	static FVector FloorUnder(const ACataclysmCharacterBase* Driven,
							  const FVector& Point);

	/**
	 * Draw the ground marker for an ability that is starting its wind-up.
	 *
	 * Does nothing for an ability whose shape has no marker, and nothing for one
	 * whose marked area is under the design's one metre floor. Both are ordinary
	 * outcomes rather than failures; see ACataclysmTelegraphMarker.
	 */
	void ShowWindUpMarker(ACataclysmCharacterBase* Driven,
						  const FCataclysmEnemyAbility& Ability);

	/**
	 * Take the wind-up's marker away.
	 *
	 * Called when the attack lands and when a stun abandons the wind-up. Safe
	 * when there is no marker, which is most of the time.
	 */
	void DismissWindUpMarker();

	/** Start an ability if one is in range and off cooldown. */
	ECataclysmBrainAction UseAbilitiesOn(ACataclysmCharacterBase* Driven,
										 AActor* Target, float DistanceCm);

public:

	/**
	 * How far off a creature may be pointed and still be counted as facing its
	 * target, in degrees.
	 *
	 * DERIVED, NOT PICKED. The widest a directional attack may miss by and still
	 * cover what it was aimed at is set by the attack's own geometry: at a range
	 * of R with a marked half-width of W, being off by an angle A displaces the
	 * marked area by R * sin(A), so the target leaves it once
	 * sin(A) > W / R. The Brute's rock throw is the longest-ranged directional
	 * attack in the project at 1000 cm with a 210 cm half-width, which gives
	 * 12.1 degrees. This is 10, which clears it with a little to spare.
	 *
	 * `Cataclysm.Enemy.TheFacingToleranceCoversEveryDirectionalAbility` checks
	 * every ability of every enemy against its own geometry, so an attack added
	 * later that is longer-ranged or narrower than the rock throw fails rather
	 * than quietly firing while pointed away from what it is aimed at.
	 *
	 * IT CANNOT BE ZERO. A creature turns a whole number of degrees per frame
	 * and a target moves between frames, so "exactly facing" is a condition that
	 * would come true only by coincidence and the creature would turn for ever.
	 */
	static constexpr float FacingToleranceDegrees = 10.0f;

	/**
	 * Whether this ability has to be pointed at anything to be worth using.
	 *
	 * ONLY A PROJECTILE DOES, of the shapes any enemy has today. A Strike marks
	 * a circle around the creature itself: the Brute's Stomp is written
	 * `Angle=360` in `sim/cataclysm_sim/enemy_abilities.py` and the reason given
	 * there is that a ring "stops the answer to a Brute being stand behind it
	 * and ignore the marker". Making a ring wait to be faced would hand that
	 * answer straight back, so a Strike must never require facing.
	 *
	 * Public and static so a test can ask the question without a world.
	 */
	static bool AbilityNeedsFacing(const FCataclysmEnemyAbility& Ability);

	/**
	 * The angle between where the creature is pointed and where the target is,
	 * in degrees, ignoring height.
	 *
	 * HEIGHT IS IGNORED FOR THE SAME REASON REACH IGNORES IT. Facing is a
	 * floor-plane question and capsule centres sit at different heights, so a
	 * 3D angle would report a Brute standing against the player as pointed
	 * downward at them rather than at them.
	 *
	 * Returns 0 when either actor is missing, which reads as "facing" and is the
	 * safe direction: a missing actor must not leave a creature turning for ever.
	 */
	static float DegreesOffTarget(const AActor* Driven, const AActor* Target);

protected:

	/**
	 * Point the creature at the target and let it turn there at its own rate.
	 *
	 * HOW THIS WORKS, BECAUSE THE OBVIOUS ROUTE SILENTLY DOES NOTHING.
	 * `AAIController::SetFocus` is the usual way to make an AI look at
	 * something, and it would have no effect here: `SetFocus` is only read by
	 * `AAIController::UpdateControlRotation`, which runs from the controller's
	 * `Tick`, and this controller sets `PrimaryActorTick.bCanEverTick = false`
	 * because it thinks on a timer instead. Nothing would ever turn.
	 *
	 * So the control rotation is set directly. `UCharacterMovementComponent::
	 * PhysicsRotation` reads `Controller->GetDesiredRotation()`, which is
	 * `GetControlRotation()`, and turns toward it by `RotationRate * DeltaTime`
	 * every movement tick. That gives smooth turning at the creature's designed
	 * rate with no per-frame work in this class, and the rotation only has to be
	 * refreshed on a thinking pass.
	 *
	 * `bOrientRotationToMovement` OVERRIDES `bUseControllerDesiredRotation` in
	 * `PhysicsRotation`, in that order, so the two are swapped rather than both
	 * set. See `FaceTravelDirection` for the other half.
	 */
	void FaceTarget(ACataclysmCharacterBase* Driven, const AActor* Target);

	/**
	 * Go back to facing whichever way the creature is walking.
	 *
	 * Called whenever it starts moving again, because the constructor's setting
	 * is what makes a walking monster point where it is going, and `FaceTarget`
	 * turns it off.
	 */
	void FaceTravelDirection(ACataclysmCharacterBase* Driven);

	/**
	 * Whether the anchor has been recorded. Distinct from the anchor being the
	 * zero vector, which is a legitimate place to stand.
	 */
	bool bHasRoamAnchor = false;

};
