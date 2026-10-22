// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmDeployableSelfBlast.generated.h"

class AActor;
class UWorld;
struct FCataclysmHitDelivery;
struct FGameplayTagContainer;

/**
 * A wearer's own deployable's blast striking the wearer, when the wearer stands inside it.
 *
 * THE SENTENCE, from `game/Data/EnchantmentsNegative.csv`, row `Negative_Your_deployables_and_traps_deal_full_damage_to_y`:
 * "Your deployables and traps deal full damage to you if you are within their blast radius". Ruled 2026-10-19 by the
 * coordinating session, B1 to B6 in the entry of that date in `docs/DECISIONS.md`. THE DEPLOYABLE HALF FIRST (Q1); THE
 * TRAP HALF SINCE TRAPS LAYER 7 (ruled 2026-10-22, K1 to K6): a standing skill trap is a deployable of its wearer's and
 * was reached from traps layer 1, and a shot or a landing of a skill trap already used up is found by its identity,
 * `IsTheWearersUsedUpTrap`.
 *
 * WHICH BLOWS (B2): every INSTANT AREA blow of a skill the wearer's deployable casts -- an area strike or a nova
 * (`UCataclysmStrikeSkill::SwingOnce`), a projectile's landing in a radius (`UCataclysmProjectileSkill::Land`) and its
 * explosion (`ACataclysmProjectile::Finish`) -- and a deployable's own death blast (`ACataclysmMinion::Explode`,
 * `ACataclysmMinion::DeathBlast`). Not a single-target blow, and not a ground zone's repeating ticks. NAMED AT EACH
 * SITE, as the 2026-10-15 precedent named its four; the search for enemies is not hooked.
 *
 * WHICH DEPLOYABLES (B5): the actor the use was cast from, or the actor whose blast it is, when
 * `ACataclysmMinion::IsDeployable` answers yes and its summoner is the wearer. So it would reach War's deployables if
 * they were ever reachable; it is tested on the skill gadget only.
 *
 * WHO (B2): the wearer and nobody else, when the area touches its body, by the enemy search's own measure: the sphere
 * overlaps the wearer's pawn collision, and a cone's test is on the wearer's location, as `UCataclysmTargeting::Gather`
 * makes them. BY THE ACTOR AND NOT BY ITS SIDE, so a maddened wearer is still the wearer.
 *
 * HOW MUCH (B3): the figure an enemy standing where the wearer stands would take, then the wearer's own defences. Each
 * site builds the delivery it builds for an enemy and hands it here.
 *
 * WHAT IT IS (B4): a blow dealt by the wearer's deployable to the wearer, raising what a blow taken raises and never
 * answered as a hit dealt, UNDER THE 2026-10-15 OWN-SIDE RULINGS FOR A SHARE: it carries the share's mark,
 * `UCataclysmAreaShareOnAllies::MarkAsShare`, at the whole of the figure. See `StrikeWithASkillsBlow`.
 */
UCLASS()
class CATACLYSM_API UCataclysmDeployableSelfBlast : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * `deployable_blasts_strike_self` (B1): a flag. Flat 1; read as above nought. No gameplay attribute and no base, so
	 * it is on `UCataclysmPlayerClassStats::StatsWithNoAttribute()`. `DEPLOYABLE_BLASTS_STRIKE_SELF_STAT` in
	 * `tools/generate_datatables.py` holds the same name.
	 */
	static const TCHAR* FlagStat;

	/**
	 * Whether `Wearer` holds the flag. ASKED WITH NO TAGS: the rule is about every blast of every deployable of the
	 * wearer's, and a death blast has no skill whose tags could scope it. False for an actor whose ability system this
	 * project did not make.
	 */
	static bool HeldBy(const AActor* Wearer);

	/**
	 * Whether `CastFrom` is a deployable whose summoner is `Wearer` (B5): the actor a use was cast from
	 * (`UCataclysmSkillTemplate::FreeRepeatCastBy`, or `FCataclysmHitDelivery::CastBy` on what outlives the use), or the
	 * actor whose own blast it is.
	 */
	static bool IsTheWearersDeployable(const AActor* Wearer, const AActor* CastFrom);

	/**
	 * TRAPS LAYER 7 (K1, ruled 2026-10-22): whether `CastBy` names a skill trap of `Wearer`'s ALREADY USED UP, so a
	 * shot or a landing of its cast that strikes after it is gone is still its blast. FOUND BY IDENTITY among the
	 * wearer's used-up traps (`UCataclysmAbilitySystemComponent::SpentSkillTrapNamed`, traps layer 3b), which only a
	 * trap of the wearer's own is ever written into, so one found there is a deployable whose summoner was the wearer, as
	 * `IsTheWearersDeployable` asks of a standing one. False for an empty pointer, which every use no deployable cast
	 * carries, and for a skill gadget gone, which is never kept (P5, 2026-10-16).
	 */
	static bool IsTheWearersUsedUpTrap(const AActor* Wearer, const TWeakObjectPtr<AActor>& CastBy);

	/**
	 * `Wearer`, when it holds the flag, is alive, `CastFrom` is its deployable, and the sphere touches its body; or null.
	 * THE SPHERE `UCataclysmTargeting::FindEnemiesInSphere` overlaps for the same blow, so the wearer is inside exactly
	 * when an enemy standing there would be. OR, SINCE TRAPS LAYER 7, `UsedUpCastBy` names a skill trap of the wearer's
	 * already used up (`IsTheWearersUsedUpTrap`): the identity of what the use was cast by, which a site whose blow can
	 * land after its trap is gone hands over; empty by default, which asks nothing more.
	 */
	static AActor* WearerInSphere(const UWorld* World, AActor* Wearer, const AActor* CastFrom, const FVector& Origin,
								  float RadiusCm,
								  const TWeakObjectPtr<AActor>& UsedUpCastBy = TWeakObjectPtr<AActor>());

	/**
	 * `Wearer` inside the cone `UCataclysmTargeting::FindEnemiesInCone` searches with the same arguments, on the same
	 * terms as `WearerInSphere`; or null.
	 */
	static AActor* WearerInCone(const UWorld* World, AActor* Wearer, const AActor* CastFrom, const FVector& Apex,
								const FVector& Forward, float RadiusCm, float AngleDegrees);

	/**
	 * Strike `Wearer` with a skill's blow of `DamagePercent`, `Delivery` being the delivery the site built for an enemy
	 * of the same blow. `UCataclysmSkillEffects::ApplyHit` prices it as for an enemy standing there (B3), and the
	 * wearer's own defences then meet it.
	 *
	 * THE SHARE'S MARK AT A HUNDRED PERCENT (B4): `UCataclysmAreaShareOnAllies::MarkAsShare`. So it is damage only and
	 * never a critical strike, spends neither stored-damage store, raises no `deployable_hit`, none of the wearer's
	 * `hit_dealt`, `first_hit_dealt` or `critical_strike`, and no `kill` if it kills; the floor rules refuse it as they
	 * refuse a share. What a blow taken raises on the wearer, `hit_taken` among it, it raises.
	 *
	 * @return what was sent, before the wearer's defences
	 */
	static float StrikeWithASkillsBlow(AActor* Wearer, float DamagePercent, const FGameplayTagContainer& SkillTags,
									   const FCataclysmHitDelivery& Delivery);

	/**
	 * Strike `Wearer` with a deployable's own death blast of `Amount`, dealt by `Deployable` with the delivery it deals
	 * an enemy, through `UCataclysmSkillEffects::ApplyDirectDamage` as the enemies' blows are, with the share's mark
	 * as `StrikeWithASkillsBlow` gives it.
	 *
	 * @return whether it was dealt
	 */
	static bool StrikeWithADeathBlast(AActor* Deployable, AActor* Wearer, float Amount,
									  const FCataclysmHitDelivery& Delivery);
};
