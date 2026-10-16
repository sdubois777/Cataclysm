// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmAreaShareOnAllies.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"

const TCHAR* UCataclysmAreaShareOnAllies::PercentStat =
	TEXT("aoe_skill_damage_to_nearby_allies_percent");

float UCataclysmAreaShareOnAllies::PercentHeldFor(const UAbilitySystemComponent* AbilitySystem,
												  const FGameplayTagContainer& SkillTags)
{
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm)
	{
		return 0.0f;
	}
	return FMath::Clamp(Cataclysm->StatForSkill(FName(PercentStat), SkillTags, 0.0f), 0.0f, 100.0f);
}

TArray<AActor*> UCataclysmAreaShareOnAllies::AlliesInCone(const UWorld* World, const AActor* Wearer,
														  const FVector& Apex, const FVector& Forward,
														  float RadiusCm, float AngleDegrees)
{
	// THE ENEMY CONE'S OWN TEST ON THE FRIENDLY SIDE: the sphere of the cone's radius, then `IsInCone` on each body's
	// location, which is the predicate `FindEnemiesInCone` hands its search.
	TArray<AActor*> Inside = UCataclysmTargeting::FindAlliesInSphere(World, Wearer, Apex, RadiusCm);
	Inside.RemoveAll([&](const AActor* Ally)
	{
		return !UCataclysmTargeting::IsInCone(Apex, Forward, Ally->GetActorLocation(), RadiusCm, AngleDegrees);
	});
	return Inside;
}

TArray<AActor*> UCataclysmAreaShareOnAllies::AlliesInSphere(const UWorld* World, const AActor* Wearer,
															const FVector& Origin, float RadiusCm)
{
	return UCataclysmTargeting::FindAlliesInSphere(World, Wearer, Origin, RadiusCm);
}

TArray<AActor*> UCataclysmAreaShareOnAllies::AlliesInLine(const UWorld* World, const AActor* Wearer,
														  const FVector& Start, const FVector& End,
														  float HalfWidthCm)
{
	// AROUND THE MIDDLE, HALF THE LENGTH PLUS THE WIDTH, as `FindEnemiesInLine` overlaps, then `IsInLine` on each
	// body's location, and nearest the start first as it sorts.
	const FVector Middle = (Start + End) * 0.5f;
	const float SearchRadius = FVector::Dist(Start, End) * 0.5f + HalfWidthCm;
	TArray<AActor*> Inside = UCataclysmTargeting::FindAlliesInSphere(World, Wearer, Middle, SearchRadius);
	Inside.RemoveAll([&](const AActor* Ally)
	{
		return !UCataclysmTargeting::IsInLine(Start, End, Ally->GetActorLocation(), HalfWidthCm);
	});
	Inside.Sort([&Start](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), Start) < FVector::DistSquared(B.GetActorLocation(), Start);
	});
	return Inside;
}

void UCataclysmAreaShareOnAllies::MarkAsShare(FCataclysmHitDelivery& Delivery, float Percent)
{
	Delivery.AreaSkillShareOnAnAllyPercent = FMath::Clamp(Percent, 0.0f, 100.0f);
	// DAMAGE ONLY (R5), and no crit (R5): nothing the blow carries reaches the ally but its damage.
	//
	// THE WEAPON SUB-TYPE IS KEPT. Ruled 2026-10-16 by the coordinating session: Slashing's more damage to health,
	// Magic's energy shield stripping and Piercing's armour ignored are how the blow is priced against a target (R4),
	// so the ally meets them as an enemy standing there would. Only the blunt weapon's stun, which is an effect and
	// not damage, is refused, in the damage branch of `UCataclysmVitalAttributeSet`, by the share's stamp.
	Delivery.bCannotCriticallyStrike = true;
	Delivery.bCarriesNoAilmentChance = true;
	Delivery.bCannotLeech = true;
	// AND NOTHING COMES BACK FROM THE ALLY FOR IT, a judgement of the writing session: a share is not a blow the
	// ally can answer.
	Delivery.bCannotBeRetaliatedAgainst = true;
}

bool UCataclysmAreaShareOnAllies::IsAShare(const FGameplayEffectSpec& Spec)
{
	return Spec.GetSetByCallerMagnitude(FName(UCataclysmSkillEffects::AreaSkillShareOnAnAllyDataName),
										/*WarnIfNotFound=*/false, 0.0f) > 0.0f;
}
