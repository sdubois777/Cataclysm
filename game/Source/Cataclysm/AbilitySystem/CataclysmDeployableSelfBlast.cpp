// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDeployableSelfBlast.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAreaShareOnAllies.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"

const TCHAR* UCataclysmDeployableSelfBlast::FlagStat = TEXT("deployable_blasts_strike_self");

bool UCataclysmDeployableSelfBlast::HeldBy(const AActor* Wearer)
{
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Wearer));
	return Cataclysm && Cataclysm->StatForSkill(FName(FlagStat), FGameplayTagContainer(), 0.0f) > 0.0f;
}

bool UCataclysmDeployableSelfBlast::IsTheWearersDeployable(const AActor* Wearer, const AActor* CastFrom)
{
	// ASKED OF THE ACTOR AND NOT OF A NAME (B5): a skill gadget, and any machine of War's, carries `Type.Deployable` in
	// its type row's tags. A Tyrant's Minion casts from its own place too and is no deployable.
	const ACataclysmMinion* Deployable = Cast<const ACataclysmMinion>(CastFrom);
	return Wearer && Deployable && Deployable->IsDeployable() && Deployable->Summoner.Get() == Wearer;
}

AActor* UCataclysmDeployableSelfBlast::WearerInSphere(const UWorld* World, AActor* Wearer, const AActor* CastFrom,
													  const FVector& Origin, float RadiusCm)
{
	// THE CHEAP QUESTIONS FIRST, so every blow of every character without the flag costs one stat read.
	if (!World || RadiusCm <= 0.0f || !IsTheWearersDeployable(Wearer, CastFrom) || UCataclysmSkillEffects::IsDead(Wearer)
		|| !HeldBy(Wearer))
	{
		return nullptr;
	}

	// THE ENEMY SEARCH'S OWN OVERLAP: a sphere against pawns, and the wearer among what it touches. The search leaves
	// its instigator out, and the wearer is that instigator, so it is asked here rather than there.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(CataclysmDeployableSelfBlast), /*bInTraceComplex=*/false);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
									FCollisionShape::MakeSphere(RadiusCm), Query);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (Overlap.GetActor() == Wearer)
		{
			return Wearer;
		}
	}
	return nullptr;
}

AActor* UCataclysmDeployableSelfBlast::WearerInCone(const UWorld* World, AActor* Wearer, const AActor* CastFrom,
													const FVector& Apex, const FVector& Forward, float RadiusCm,
													float AngleDegrees)
{
	// THE SPHERE OF THE CONE'S RADIUS, THEN `IsInCone` ON THE WEARER'S LOCATION, which is the predicate
	// `FindEnemiesInCone` hands its search.
	AActor* Inside = WearerInSphere(World, Wearer, CastFrom, Apex, RadiusCm);
	return Inside && UCataclysmTargeting::IsInCone(Apex, Forward, Inside->GetActorLocation(), RadiusCm, AngleDegrees)
		? Inside
		: nullptr;
}

float UCataclysmDeployableSelfBlast::StrikeWithASkillsBlow(AActor* Wearer, float DamagePercent,
														   const FGameplayTagContainer& SkillTags,
														   const FCataclysmHitDelivery& Delivery)
{
	if (!Wearer || DamagePercent <= 0.0f)
	{
		return 0.0f;
	}

	// THE ENEMY'S DELIVERY, AND THEN THE SHARE'S MARK AT THE WHOLE OF THE FIGURE (B3, B4). The instigator is the wearer,
	// as for every blow of the cast, so `ApplyHit` prices it with the wearer's numbers and, through the delivery's skill
	// or `CastBy`, as the gadget's.
	FCataclysmHitDelivery AtTheWearer = Delivery;
	UCataclysmAreaShareOnAllies::MarkAsShare(AtTheWearer, 100.0f);
	return UCataclysmSkillEffects::ApplyHit(Wearer, Wearer, DamagePercent, SkillTags, AtTheWearer);
}

bool UCataclysmDeployableSelfBlast::StrikeWithADeathBlast(AActor* Deployable, AActor* Wearer, float Amount,
														  const FCataclysmHitDelivery& Delivery)
{
	if (!Deployable || !Wearer || Amount <= 0.0f)
	{
		return false;
	}

	// THE ENEMY'S DELIVERY AND THE SHARE'S MARK, as above. `ApplyDirectDamage` takes the figure as it stands, so the
	// mark's percent is only its stamp here: the figure is already the whole blast.
	FCataclysmHitDelivery AtTheWearer = Delivery;
	UCataclysmAreaShareOnAllies::MarkAsShare(AtTheWearer, 100.0f);
	return UCataclysmSkillEffects::ApplyDirectDamage(Deployable, Wearer, Amount, AtTheWearer);
}
