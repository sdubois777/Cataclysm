// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmRegeneration.h"
// For emptying Fervour when health comes back. Issue #954.
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"

void UCataclysmRegeneration::TopUp(UAbilitySystemComponent& AbilitySystem,
								   const FGameplayAttribute& Pool,
								   const FGameplayAttribute& Maximum,
								   float Gain,
								   const FGameplayTagContainer& Healing)
{
	if (Gain <= 0.0f)
	{
		return;
	}

	const float Current = AbilitySystem.GetNumericAttribute(Pool);
	float Ceiling = AbilitySystem.GetNumericAttribute(Maximum);

	// AND A CHARACTER MAY BE FORBIDDEN TO BE HEALED ALL THE WAY UP. Issue
	// #988. The Masochist's Point of No Return keystone reads "You cannot be
	// healed above 50% of your maximum health, but you deal 25% more damage."
	//
	// HERE RATHER THAN AT EACH CALLER, because this is the one place health
	// regeneration and life leech both restore health, and the node says
	// "cannot be healed" rather than naming one of them.
	//
	// HEALTH ONLY. The node says health, and mana and the energy shield come
	// through this same function.
	//
	// A RESPAWN IS NOT HEALING AND IS NOT CAPPED.
	// `ACataclysmPlayerCharacter::Respawn` writes health back with
	// `SetNumericAttributeBase` rather than through here, so it is untouched.
	// That is the right answer -- a respawn is a new life -- and issue #956 is
	// the open question about what else that direct write should do.
	//
	// THE STAT IS A REDUCTION OF THE CEILING, so zero leaves the ceiling where
	// it was and no character without the node is changed by a single number.
	if (Pool == UCataclysmVitalAttributeSet::GetHealthAttribute())
	{
		const float Reduction = FMath::Clamp(
			AbilitySystem.GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetHealingCeilingReductionAttribute()),
			0.0f, 100.0f);
		Ceiling *= (100.0f - Reduction) / 100.0f;
	}

	// A POOL WITH NO MAXIMUM IS NOT A POOL. A class with no energy shield is a
	// design position rather than an error state, and its shield maximum is
	// zero; adding to it would be adding to something that does not exist, and
	// the clamp would throw the value away anyway.
	//
	// IT NOW CATCHES A SECOND CASE, and both want the same answer. Issue #988. A
	// health ceiling reduced by the full hundred per cent leaves a ceiling of
	// zero, and a character already at or above a reduced ceiling is simply not
	// healed. Neither is a fault, and neither loses anything: what would have
	// been restored had nowhere to go.
	if (Ceiling <= 0.0f || Current >= Ceiling)
	{
		return;
	}

	const float Restored = FMath::Min(Gain, Ceiling - Current);
	AbilitySystem.ApplyModToAttribute(Pool, EGameplayModOp::Additive, Restored);

	// AND HEALTH COMING BACK EMPTIES FERVOUR. Issue #954. The Masochist's
	// starting node states it: "healing removes Fervour at the same rate, 1 per
	// 1% of maximum health restored, so your health regeneration is what empties
	// it rather than a timer".
	//
	// HEALTH ONLY. Mana and the energy shield have nothing to do with it, and
	// the design's rule names health.
	//
	// WHAT FIT, NOT WHAT WAS OFFERED. Healing that overflowed a full health bar
	// restored nothing, so it removes nothing. Without that a Masochist standing
	// at full health would have its bar drained by a regeneration rate that was
	// putting nothing anywhere.
	if (Pool == UCataclysmVitalAttributeSet::GetHealthAttribute())
	{
		UCataclysmFervour::RemoveForHealing(&AbilitySystem, Restored, Healing);
	}
}

float UCataclysmRegeneration::GainPerStep(float RatePerSecond,
										  float SecondsInStep)
{
	if (RatePerSecond <= 0.0f || SecondsInStep <= 0.0f)
	{
		return 0.0f;
	}

	return RatePerSecond * SecondsInStep;
}

bool UCataclysmRegeneration::ShieldMayRefill(float SecondsSinceLastDamage)
{
	return SecondsSinceLastDamage >= ShieldRefillDelaySeconds;
}

void UCataclysmRegeneration::ApplyStep(AActor* Character, float SecondsInStep,
									   float SecondsSinceLastDamage)
{
	if (!Character || SecondsInStep <= 0.0f)
	{
		return;
	}

	// A CORPSE DOES NOT HEAL. An enemy is destroyed on the tick AFTER it dies,
	// because ACataclysmEnemyCharacter::HandleDeath runs inside the gameplay
	// effect callback that dealt the killing blow, so there is a real window in
	// which a dead creature is still standing there with an ability system.
	if (UCataclysmSkillEffects::IsDead(Character))
	{
		return;
	}

	UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Character);
	if (!AbilitySystem
		|| !AbilitySystem->GetSet<UCataclysmVitalAttributeSet>())
	{
		return;
	}

	// THE HEALTH STEP SAYS IT IS REGENERATION, and the other two do not bother
	// because Fervour reads health alone. The Masochist's Staunch node reduces
	// "the Fervour removed by your own health regeneration" specifically, which
	// is narrower than healing, so the source has to travel with the amount.
	// Leech pays through the same function and carries nothing, which is what
	// keeps that node off it. Issue #954.
	FGameplayTagContainer Regeneration;
	Regeneration.AddTag(UCataclysmFervour::RegenerationTag());

	TopUp(*AbilitySystem, UCataclysmVitalAttributeSet::GetHealthAttribute(),
		  UCataclysmVitalAttributeSet::GetMaxHealthAttribute(),
		  GainPerStep(AbilitySystem->GetNumericAttribute(
						  UCataclysmVitalAttributeSet::GetHealthRegenAttribute()),
					  SecondsInStep),
		  Regeneration);

	TopUp(*AbilitySystem, UCataclysmVitalAttributeSet::GetManaAttribute(),
		  UCataclysmVitalAttributeSet::GetMaxManaAttribute(),
		  GainPerStep(AbilitySystem->GetNumericAttribute(
						  UCataclysmVitalAttributeSet::GetManaRegenAttribute()),
					  SecondsInStep));

	// THE SHIELD WAITS AND THE OTHER TWO DO NOT. Three seconds since the
	// character last took damage, restarted by taking damage again inside the
	// window, and damage over time restarts it as well. All three rules are
	// stated in the Energy Shield section of docs/Cataclysm_GDD_v2.md.
	if (ShieldMayRefill(SecondsSinceLastDamage))
	{
		TopUp(*AbilitySystem,
			  UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(),
			  UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(),
			  GainPerStep(
				  AbilitySystem->GetNumericAttribute(
					  UCataclysmVitalAttributeSet::GetEnergyShieldRegenAttribute()),
				  SecondsInStep));
	}
}
