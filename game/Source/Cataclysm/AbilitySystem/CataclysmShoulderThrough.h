// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CataclysmShoulderThrough.generated.h"

class AActor;

/**
 * Shoulder Through, the Ravager's `Ravager_capstone_100` option 3: "Moving into
 * an enemy pushes it aside and deals your melee damage to it." Issue #1515.
 *
 * RULED 2026-09-25, UNDER THE OWNER'S DELEGATION:
 *
 *   - MOVING INTO AN ENEMY is the character asking to move -- its movement
 *     component's acceleration, not the ground it covered -- while a living
 *     hostile stands within the two collision radii and `ContactMarginCm` and
 *     within `ConeDegrees` of that direction. Acceleration, because both
 *     capsules block each other and a character pressing into an enemy covers no
 *     ground at all. Ordinary walking only: the player controller does not ask
 *     while a movement skill carries the character or while it is walking to an
 *     enemy it clicked, which is an attack order and not a walk.
 *   - THE PUSH IS DISPLACEMENT, `UCataclysmSkillEffects::ApplyPushAside`, so
 *     immunity, crowd control resistance, the halving rule and the 1 second
 *     stagger all apply. Perpendicular to the movement, toward the side the
 *     enemy's centre lies on, and to the right when it is dead ahead;
 *     `PushCm` far. The distance is a judgement.
 *   - THE DAMAGE is one ordinary melee hit at 100% of the character's weapon
 *     damage. It is not a skill use: no cost, no skill_use, no next-use charge.
 *     So a kill by it names no skill, and Follow Through has nothing to repeat.
 *   - ONCE PER ENEMY PER `SecondsPerEnemy`, a judgement matching the stagger.
 *
 * GENRE: Diablo IV's Barbarian Charge, "Rush forward and push enemies before
 * swinging your weapon, dealing [180%] damage and Knocking Back enemies", a
 * skill on a 17 second cooldown. A push and a blow on contact are shipped; a
 * passive that does it on walking is this design's own.
 */
UCLASS()
class CATACLYSM_API UCataclysmShoulderThrough : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Above zero means the option is held. Its row will carry 1. */
	static const TCHAR* Stat;

	/** How far an enemy is pushed aside, in centimetres, before the halving
	 *  rule. Enough to clear the character's path past both bodies. */
	static constexpr float PushCm = 150.0f;

	/** How long one enemy is left alone after being pushed. */
	static constexpr float SecondsPerEnemy = 1.0f;

	/** The gap between two collision radii that still counts as touching. */
	static constexpr float ContactMarginCm = 10.0f;

	/** How far either side of the movement an enemy may stand and be moved
	 *  into, in degrees. */
	static constexpr float ConeDegrees = 45.0f;

	/**
	 * The nearest living hostile `Character` is moving into along `Direction`,
	 * or null. Asks nothing about the option or the limit.
	 */
	static AActor* EnemyMovedInto(const AActor* Character, const FVector& Direction);

	/**
	 * One frame of walking along `Direction`: when the option is held and an
	 * enemy is moved into that the limit allows, push it aside and strike it.
	 *
	 * @return the enemy pushed, or null
	 */
	static AActor* Step(AActor* Character, const FVector& Direction);
};
