// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmAllyDamageShare.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"

const TCHAR* UCataclysmAllyDamageShare::PercentStat =
	TEXT("damage_done_to_nearby_allies_taken_percent");

float UCataclysmAllyDamageShare::PercentHeldBy(const UAbilitySystemComponent* AbilitySystem)
{
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm)
	{
		return 0.0f;
	}
	return FMath::Clamp(Cataclysm->StatForSkill(FName(PercentStat), FGameplayTagContainer(), 0.0f), 0.0f, 100.0f);
}

bool UCataclysmAllyDamageShare::IsAShare(const FGameplayEffectSpec& Spec)
{
	return Spec.GetSetByCallerMagnitude(FName(UCataclysmSkillEffects::ShareOfDamageDoneToAnAllyDataName),
										/*WarnIfNotFound=*/false, 0.0f) > 0.0f;
}

TArray<AActor*> UCataclysmAllyDamageShare::WearersBeside(const AActor* Struck,
														const UAbilitySystemComponent* StruckSystem)
{
	TArray<AActor*> Wearers;
	if (!IsValid(Struck)
		|| UCataclysmTeams::TeamOf(Struck) != UCataclysmTeams::IdFor(ECataclysmTeam::Players))
	{
		return Wearers;
	}

	// A WEARER STRUCK SHARES NOTHING (R4). Asked before the search, so a wearer's own blows cost no search.
	if (PercentHeldBy(StruckSystem) > 0.0f)
	{
		return Wearers;
	}

	// AROUND THE STRUCK BODY, FIVE METRES (R2), and on its side by `UCataclysmTeams::AttitudeBetween` (R1). The search
	// leaves the struck body out and the dead out, and counts a character whose body the sphere touches.
	for (AActor* Ally : UCataclysmTargeting::FindAlliesInSphere(Struck->GetWorld(), Struck,
			Struck->GetActorLocation(), UCataclysmAbilitySystemComponent::NearbyActionRadiusCm))
	{
		if (PercentHeldBy(UCataclysmTargeting::AbilitySystemOf(Ally)) > 0.0f)
		{
			Wearers.Add(Ally);
		}
	}
	return Wearers;
}

int32 UCataclysmAllyDamageShare::PayShares(const TArray<AActor*>& Wearers, const FGameplayEffectContextHandle& AllysBlow,
										   float TakenFromHealth, FName DamageType)
{
	AActor* Attacker = AllysBlow.GetInstigator();
	if (Wearers.IsEmpty() || TakenFromHealth <= 0.0f || !IsValid(Attacker)
		|| !UCataclysmTargeting::AbilitySystemOf(Attacker))
	{
		return 0;
	}

	// THE BLOW TO THE WEARER (R7). Not evaded: it is not aimed at the wearer and cannot be stepped out of, a judgement
	// of the writing session. Its attacker's critical strike, penetration, weapon, leech and ailment chances do not
	// reach it: they were spent on the ally. Never retaliated against, as ruled.
	FCataclysmHitDelivery Delivery;
	Delivery.bIsAShareOfDamageDoneToAnAlly = true;
	Delivery.DamageType = DamageType;
	Delivery.bCannotBeEvaded = true;
	Delivery.bCannotCriticallyStrike = true;
	Delivery.bCannotPenetrate = true;
	Delivery.bCarriesNoWeaponSubType = true;
	Delivery.bCannotLeech = true;
	Delivery.bCarriesNoAilmentChance = true;
	Delivery.bCannotBeRetaliatedAgainst = true;
	// AND THE ACTOR THAT DEALT THE ALLY'S BLOW, so `UCataclysmCombatEvents::AttackerOf` names for the share the same
	// attacker it named for that blow: a minion's own, or its summoner's while the summoner holds Conduit.
	Delivery.DealtBy = Cast<AActor>(AllysBlow.GetSourceObject());

	int32 Sent = 0;
	for (AActor* Wearer : Wearers)
	{
		if (!IsValid(Wearer) || UCataclysmSkillEffects::IsDead(Wearer))
		{
			continue;
		}
		const float Share =
			TakenFromHealth * PercentHeldBy(UCataclysmTargeting::AbilitySystemOf(Wearer)) / 100.0f;
		if (Share > 0.0f && UCataclysmSkillEffects::ApplyDirectDamage(Attacker, Wearer, Share, Delivery))
		{
			++Sent;
		}
	}
	return Sent;
}
