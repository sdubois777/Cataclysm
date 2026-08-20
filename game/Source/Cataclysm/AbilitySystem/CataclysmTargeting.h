// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "CataclysmTargeting.generated.h"

class UAbilitySystemComponent;
class UWorld;

/**
 * Finding what a skill hits.
 *
 * NOTHING IN THIS PROJECT COULD DO THIS BEFORE. There was no trace, no overlap
 * and no target selection anywhere in game/Source/, so every ability could spend
 * mana and start a cooldown and could not touch anything. That is why issue #37
 * is not "sixteen ActivateAbility bodies": the layer they all stand on had to be
 * built first, and building it once is what makes the templates shared.
 *
 * THE GEOMETRY IS SEPARATE FROM THE WORLD QUERY, deliberately. IsInCone and
 * IsInLine take numbers and can be tested exhaustively without spawning
 * anything; the Find functions do a world overlap and then filter with them. It
 * is the same split as UCataclysmDamageCalculation, and for the same reason: the
 * part most likely to be subtly wrong is the arithmetic.
 *
 * WHAT COUNTS AS AN ENEMY IS THREE SEPARATE QUESTIONS, and keeping them apart is
 * the point. First, can this actor hold damage at all -- does it have an ability
 * system component? That is what makes scenery not a target. Second, is it
 * already dead? Third, which side is it on? `UCataclysmTeams` answers the last
 * and nothing here duplicates it, so an ally is found by asking for a different
 * attitude rather than by a second copy of the search.
 *
 * THE DEAD QUESTION WAS ADDED LAST, by issue #570, and it was measured rather
 * than anticipated: a play session recorded fifty-six attacks landing on a
 * player already at zero health, each dealing exactly nothing, because nothing
 * a creature could ask told it the target was finished.
 */
UCLASS()
class CATACLYSM_API UCataclysmTargeting : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Whether a point falls inside a cone standing at Origin and facing Forward.
	 *
	 * AngleDegrees is the FULL width, not the half-angle, because that is how
	 * the design writes it: Molten Cleave's "wide cone" is 120 degrees across,
	 * meaning 60 either side. An angle of 360 or more is a ring and every point
	 * within the radius is inside it.
	 *
	 * The test is done in the horizontal plane only. Every skill in this design
	 * is aimed by a top-down cursor on ground the character stands on, so
	 * including height would make a cone miss an enemy standing on a step.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Targeting")
	static bool IsInCone(const FVector& Origin, const FVector& Forward,
						 const FVector& Point, float RadiusCm, float AngleDegrees);

	/**
	 * Whether a point falls within HalfWidthCm of the segment Start to End.
	 *
	 * Used for the skills written as lines rather than cones: Infernal Lance
	 * pierces "every enemy in a 12 meter line", and Cinder Rush hits "any
	 * enemies in your path". A point beyond either end is outside, so a charge
	 * does not hit something it stopped short of.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Targeting")
	static bool IsInLine(const FVector& Start, const FVector& End,
						 const FVector& Point, float HalfWidthCm);

	/**
	 * Every enemy within RadiusCm of Origin, nearest first.
	 *
	 * @param MaxTargets  zero means no limit
	 */
	static TArray<AActor*> FindEnemiesInSphere(const UWorld* World,
											   const AActor* Instigator,
											   const FVector& Origin,
											   float RadiusCm,
											   int32 MaxTargets = 0);

	/** Every enemy inside the cone, nearest first. See IsInCone for the shape. */
	static TArray<AActor*> FindEnemiesInCone(const UWorld* World,
											 const AActor* Instigator,
											 const FVector& Origin,
											 const FVector& Forward,
											 float RadiusCm,
											 float AngleDegrees,
											 int32 MaxTargets = 0);

	/** Every enemy along the segment, nearest to Start first. */
	static TArray<AActor*> FindEnemiesInLine(const UWorld* World,
											 const AActor* Instigator,
											 const FVector& Start,
											 const FVector& End,
											 float HalfWidthCm,
											 int32 MaxTargets = 0);

	/**
	 * Every ally within RadiusCm of Origin, nearest first. Excludes the
	 * instigator itself.
	 *
	 * FOR THE ALLY HALF OF AN AURA. Conflagration and Blood and Iron both state
	 * a benefit for allies standing in them -- "allies within it deal 8%
	 * increased fire damage" -- and before there were sides there was no way to
	 * ask who those were. The benefit itself is a buff magnitude, which is issue
	 * #166 and is still not applied; this is the half that finds who to apply it
	 * to.
	 *
	 * @param MaxTargets  zero means no limit
	 */
	/**
	 * Everything in a lane that can hold damage, whichever side it is on, and
	 * including the actor the lane belongs to.
	 *
	 * THE ONLY SEARCH IN THIS FILE THAT DOES NOT TAKE A SIDE, and there is
	 * exactly one thing in the design that needs it: the Hellhound's Hellrush
	 * leaves a burning lane, and `ABILITIES['Hellhound']` in
	 * `sim/cataclysm_sim/enemy_abilities.py` says "The fire burns other enemies
	 * and the Hellhound itself". The roster in `docs/Cataclysm_GDD_v2.md` says
	 * the same of no other creature.
	 *
	 * IT STILL REFUSES SCENERY AND THE DEAD. Something that cannot hold damage
	 * is not in a search of any kind, and a corpse is not either -- both for the
	 * reasons `MatchesAttitude` records. What it drops is the question of sides
	 * and nothing else.
	 *
	 * @param Origin  the actor the lane belongs to. **It is a legal result**,
	 *                which is the whole point and is the opposite of every
	 *                other search here.
	 */
	static TArray<AActor*> FindEveryoneInLine(const UWorld* World,
											  const AActor* Origin,
											  const FVector& Start, const FVector& End,
											  float HalfWidthCm);

	static TArray<AActor*> FindAlliesInSphere(const UWorld* World,
											  const AActor* Instigator,
											  const FVector& Origin,
											  float RadiusCm,
											  int32 MaxTargets = 0);

	/** Whether this actor is something the instigator's skills may hit. */
	static bool IsHostileTo(const AActor* Actor, const AActor* Instigator);

	/**
	 * Whether this actor is on the instigator's side and can be helped.
	 *
	 * NOT SIMPLY THE OPPOSITE OF IsHostileTo. An actor with no ability system
	 * is neither: scenery is not an enemy, and it is not an ally that an aura
	 * can buff either. The instigator is not its own ally, for the same reason
	 * it is not its own enemy -- a search that returns the caster would make
	 * "allies within it" include the person casting it, which the design writes
	 * as a separate clause where it means it.
	 */
	static bool IsFriendlyTo(const AActor* Actor, const AActor* Instigator);

	/** The actor's ability system component, or null if it has none. */
	static UAbilitySystemComponent* AbilitySystemOf(const AActor* Actor);

private:
	/**
	 * Whether an actor is a legal result for a search of the given attitude.
	 *
	 * The one place the two questions -- can it hold damage, and which side is
	 * it on -- are asked together. IsHostileTo and IsFriendlyTo are this with
	 * the attitude fixed.
	 */
	static bool MatchesAttitude(const AActor* Actor, const AActor* Instigator,
								ETeamAttitude::Type Wanted);

	/**
	 * The shared body of the four Find functions: overlap a sphere, keep what
	 * has the wanted attitude and what Predicate accepts, sort by distance, then
	 * cut to MaxTargets.
	 *
	 * SORTED BEFORE CUTTING, and that ordering is the point. A cap applied to
	 * the raw overlap result would keep whichever actors the physics scene
	 * happened to return first, so Searing Hook's single target would be an
	 * arbitrary enemy in front of the player rather than the closest one.
	 */
	/**
	 * @param bEveryone  ignore `Wanted` and the instigator's own exclusion, and
	 *                   keep anything that can hold damage. One caller,
	 *                   `FindEveryoneInLine`. It is a parameter rather than a
	 *                   second copy of this body because the two differ in
	 *                   exactly two lines, and a copy would be the place a fix
	 *                   to one of them failed to reach.
	 */
	static TArray<AActor*> Gather(const UWorld* World, const AActor* Instigator,
								  const FVector& Origin, float SearchRadiusCm,
								  int32 MaxTargets, ETeamAttitude::Type Wanted,
								  TFunctionRef<bool(const FVector&)> Predicate,
								  bool bEveryone = false);
};
