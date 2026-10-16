// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmAreaShareOnAllies.generated.h"

class AActor;
class UAbilitySystemComponent;
class UWorld;
struct FCataclysmHitDelivery;
struct FGameplayEffectSpec;
struct FGameplayTagContainer;

/**
 * A share of a point-blank skill's own blow, struck at each ally inside the area the blow strikes.
 *
 * THE SENTENCE, from `game/Data/EnchantmentsNegative.csv`, row `Negative_Your_AOE_skills_deal_50_75_of_their_damage_to`:
 * "Your AOE skills deal 50%-75% of their damage to nearby allies as well as enemies". Ruled 2026-10-15 by the
 * coordinating session, R1 to R10 in the entry of that date in `docs/DECISIONS.md`.
 *
 * WHICH SKILLS (R2, R10): the wearer's skills whose row carries `Type.AOE.PointBlank`. The stat is asked with the
 * skill's own tags, and its row requires that tag, so a skill without it holds nothing.
 *
 * WHICH BLOWS (R2): each such skill's OWN INSTANT BLOW, at four places only: `UCataclysmStrikeSkill::SwingOnce`,
 * `UCataclysmStrikeSkill::PullTheWeaponFree`, and the Charge line and the Leap and Recall sphere in
 * `UCataclysmMovementSkill::ActivateAbility`. A ground zone's sweep and the two blows a row grants a movement skill
 * (`HitWhatThePathCrossed`, `ExplodeAtBothEnds`) share nothing.
 *
 * WHO (R1, R3): every actor on the wearer's side by `UCataclysmTeams` inside the same cone, sphere or line, the wearer
 * itself excluded, once per swing. Found here with `UCataclysmTargeting::FindAlliesInSphere` and the public
 * `IsInCone` and `IsInLine` tests, so the friendly search is the enemy one's mirror.
 *
 * HOW MUCH (R4): the blow `UCataclysmSkillEffects::ApplyHit` prices for that ally, as for an enemy standing there,
 * times the percent; the ally's own defences then meet it. See `MarkAsShare`.
 *
 * A SEPARATE CLASS OF STATIC FUNCTIONS, as `UCataclysmAllyDamageShare` is, so each site gains a search and a call
 * rather than the shapes and the delivery.
 */
UCLASS()
class CATACLYSM_API UCataclysmAreaShareOnAllies : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * `aoe_skill_damage_to_nearby_allies_percent`: the percent of a point-blank skill's blow, priced for an ally, that
	 * the ally inside its area is struck for. Flat. No gameplay attribute and no base, so it is on
	 * `UCataclysmPlayerClassStats::StatsWithNoAttribute()`. Nought or less strikes no ally.
	 * `AOE_SKILL_DAMAGE_TO_NEARBY_ALLIES_PERCENT_STAT` in `tools/generate_datatables.py` holds the same name.
	 */
	static const TCHAR* PercentStat;

	/**
	 * The percent this ability system's character holds for a skill of these tags, with a fallback of nought, held
	 * to 100: an ally is never struck for more than the blow. Nought for an ability system this project did not make.
	 *
	 * WITH THE SKILL'S OWN TAGS (R10), so the row's Required Tags `Type.AOE.PointBlank` keeps it to those skills.
	 */
	static float PercentHeldFor(const UAbilitySystemComponent* AbilitySystem, const FGameplayTagContainer& SkillTags);

	/**
	 * Every ally of `Wearer` whose location is inside the cone `UCataclysmTargeting::FindEnemiesInCone` searches with
	 * the same arguments, nearest first and with no limit. The wearer and the dead are left out.
	 */
	static TArray<AActor*> AlliesInCone(const UWorld* World, const AActor* Wearer, const FVector& Apex,
										const FVector& Forward, float RadiusCm, float AngleDegrees);

	/** Every ally of `Wearer` the sphere touches, as `UCataclysmTargeting::FindEnemiesInSphere` finds enemies. */
	static TArray<AActor*> AlliesInSphere(const UWorld* World, const AActor* Wearer, const FVector& Origin,
										  float RadiusCm);

	/**
	 * Every ally of `Wearer` whose location is within `HalfWidthCm` of the segment, searched around its middle as
	 * `UCataclysmTargeting::FindEnemiesInLine` searches, nearest the start first.
	 */
	static TArray<AActor*> AlliesInLine(const UWorld* World, const AActor* Wearer, const FVector& Start,
										const FVector& End, float HalfWidthCm);

	/**
	 * Make `Delivery`, already filled as the skill fills it for an enemy, the delivery of a share of `Percent`.
	 *
	 * IT SETS `FCataclysmHitDelivery::AreaSkillShareOnAnAllyPercent`, which `ApplyHit` reads to send that percent of
	 * the figure it priced and to spend neither stored-damage store (R7). DAMAGE ONLY (R5): never a critical strike,
	 * no ailment chance, no weapon sub-type (so no blunt stun), no leech, and never retaliated against. The riders
	 * `HitTargets` applies in its loop are not reached at all, because a share is never dealt through it.
	 */
	static void MarkAsShare(FCataclysmHitDelivery& Delivery, float Percent);

	/** Whether this effect is a share's blow: stamped with `UCataclysmSkillEffects::AreaSkillShareOnAnAllyDataName`. */
	static bool IsAShare(const FGameplayEffectSpec& Spec);
};
