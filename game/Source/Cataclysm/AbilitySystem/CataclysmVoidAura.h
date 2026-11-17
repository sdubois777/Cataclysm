// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmVoidAura.generated.h"

class AActor;
class UCataclysmAbilitySystemComponent;
class UWorld;

/**
 * The Null Emperor set's aura of void energy. Ruled 2026-10-17 for its ten-piece bonus, "While you have at least 10
 * stacks of Void, you gain an aura of void energy that has a 50% chance to consume enemy projectiles";
 * `docs/DECISIONS.md` has the entry. The coordinating session's O9: consume enemy projectiles in flight within 3 m,
 * 50% rolled once per projectile, no blast, never the player's own.
 *
 * WHAT HOLDS IT. A wearer whose `ConsumeChanceStat` is above nought while it holds at least `StacksNeeded` kept stacks
 * named `StacksName`, read by `UCataclysmAbilitySystemComponent::KeptStacksHeldNamed`: the stacks the six-piece row
 * gains, known by their name and not by that row's key. READ NOW, EVERY TIME, so the aura ends the moment the count
 * falls below ten: the free next ability spends all ten (O7 c), and a death, a dungeon left and gear taken off read as
 * nought.
 *
 * WHAT IT DOES. `ACataclysmProjectile::Step` asks `AuraThatReaches` for every sub-step of a shot that deals damage. A
 * shot whose firer is hostile to a wearer, passing within `RadiusCm` of it on the ground plane, is rolled ONCE against
 * that wearer's chance; on success it ends where it is and delivers nothing. A failed roll is never rolled again for
 * that shot.
 *
 * THE REGISTER. Every ability system that gains a kept stack notes itself here (`NoteMayHoldIt`, from
 * `UCataclysmAbilitySystemComponent::GainKeptStack`), because nothing but a gain can bring a count to ten. A shot asks
 * only those, and of those only the ones in its own world whose aura is up now. The pattern of
 * `ACataclysmGroundZone::ProjectilePullZones`.
 */
UCLASS()
class CATACLYSM_API UCataclysmVoidAura : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * `void_aura_projectile_consume_chance`: the per cent chance the aura consumes an enemy shot, a flat 50 on the
	 * ten-piece's row. Asked with no tags: a shot is not one of the wearer's skills. No gameplay attribute and no base;
	 * it is in `UCataclysmPlayerClassStats::StatsWithNoAttribute()`. Nought or less is no aura.
	 */
	static const TCHAR* ConsumeChanceStat;

	/** What the six-piece row calls its stacks, the name its Ailment cell gives: "Void". */
	static const TCHAR* StacksName;

	/** "While you have at least 10 stacks of Void": the sentence's figure. */
	static constexpr int32 StacksNeeded = 10;

	/**
	 * How far the aura reaches from its wearer, in centimetres, on the ground plane. A DELEGATED NUMBER: the sentence
	 * states no radius, and the coordinating session ruled 3 m to be tuned against play (O9).
	 */
	static constexpr float RadiusCm = 300.0f;

	/** The wearer's chance now, nought to a hundred: its row's figure while it holds ten Void, and nought otherwise. */
	static float ConsumeChanceOf(const UCataclysmAbilitySystemComponent* Wearer);

	/** Whether the wearer's aura is up now: a chance above nought. */
	static bool IsUp(const UCataclysmAbilitySystemComponent* Wearer);

	/** Note that this ability system may come to hold the aura. Called by `GainKeptStack`; cheap to call again. */
	static void NoteMayHoldIt(UCataclysmAbilitySystemComponent* Wearer);

	/**
	 * The wearer whose aura the stretch `From` to `To` of a shot `Firer` fired passes within `RadiusCm` of, on the
	 * ground plane, in `World`, or null. Only a wearer `Firer` is hostile to (`UCataclysmTargeting::IsHostileTo`) and
	 * whose aura is up counts, so the wearer's own shots, an ally's and a minion's are never taken, and a dead wearer's
	 * aura takes nothing. The nearest such wearer when there are several. Null for no firer.
	 */
	static const UCataclysmAbilitySystemComponent* AuraThatReaches(const AActor* Firer, const UWorld* World,
																	const FVector& From, const FVector& To);

	/**
	 * One roll against `ChancePercent`: true when the shot is consumed. A chance of 100 or more always passes and one
	 * of nought or less never does. `Cataclysm.VoidAuraConsumeRoll` pins the 0-100 roll for a test; negative rolls for
	 * real.
	 */
	static bool RollConsumes(float ChancePercent);
};
