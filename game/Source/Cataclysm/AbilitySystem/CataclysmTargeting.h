// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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
 * WHAT COUNTS AS AN ENEMY IS CURRENTLY CRUDE. Any actor that has an ability
 * system component and is not the instigator. This project has no faction or
 * team concept yet, so there is nothing better to ask. It is right for the
 * vertical slice, where the only actors with an ability system are the player
 * and enemies, and it will be wrong the moment an ally exists. Issue #162.
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

	/** Whether this actor is something the instigator's skills may hit. */
	static bool IsHostileTo(const AActor* Actor, const AActor* Instigator);

	/** The actor's ability system component, or null if it has none. */
	static UAbilitySystemComponent* AbilitySystemOf(const AActor* Actor);

private:
	/**
	 * The shared body of the three Find functions: overlap a sphere, keep what
	 * Predicate accepts, sort by distance, then cut to MaxTargets.
	 *
	 * SORTED BEFORE CUTTING, and that ordering is the point. A cap applied to
	 * the raw overlap result would keep whichever actors the physics scene
	 * happened to return first, so Searing Hook's single target would be an
	 * arbitrary enemy in front of the player rather than the closest one.
	 */
	static TArray<AActor*> Gather(const UWorld* World, const AActor* Instigator,
								  const FVector& Origin, float SearchRadiusCm,
								  int32 MaxTargets,
								  TFunctionRef<bool(const FVector&)> Predicate);
};
