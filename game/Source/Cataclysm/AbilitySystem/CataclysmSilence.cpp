// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSilence.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Character/CataclysmCharacterBase.h"
#include "GameplayTagsManager.h"

FGameplayTag UCataclysmSilence::SilencedTag()
{
	// THE ONE PLACE THE NAME IS WRITTEN. The workbook's Tags sheet carries it; until it does, this is an invalid tag
	// and no character is ever silenced.
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("State.Silenced")), /*ErrorIfNotFound=*/false);
}

bool UCataclysmSilence::ApplySilence(AActor* Instigator, AActor* Target, float Seconds)
{
	if (!Cast<ACataclysmCharacterBase>(Target) || Seconds <= 0.0f)
	{
		return false;
	}

	// A HEALTH CEILING THE SILENCING CHARACTER CAN CARRY, asked first, as for a fear. Ruled 2026-10-17 (O4): silence is
	// crowd control for "You cannot apply CC effects to enemies above 50% HP". A refusal tags nothing and notes no
	// crowd control on the applier.
	if (UCataclysmSkillEffects::CrowdControlRefusedByHealthCeiling(Instigator, Target))
	{
		return false;
	}

	// CROWD-CONTROL RESISTANCE NEXT, AS FOR A FEAR: it shortens the silence in proportion, and at 100% or more there is
	// nothing left to apply.
	Seconds = UCataclysmSkillEffects::HeldSecondsAfterCrowdControlResistance(Target, Seconds);
	if (Seconds <= 0.0f)
	{
		return false;
	}

	// NO STUN WINDOW AND NO BOSS RULE, AND THAT IS THE RULING. O4: silence neither takes nor is refused by the 5 second
	// window stun, knockdown and fear share. O2: a boss is silenced for the full duration, as a pin affects one. A
	// refresh moves the running silence's start and does not stack it (O5), which `ApplyTagForDuration` does for every
	// tag it lays.
	if (!UCataclysmSkillEffects::ApplyTagForDuration(Instigator, Target, SilencedTag(), Seconds))
	{
		return false;
	}

	// AND IT IS CROWD CONTROL APPLIED, for the rows that read that event (O4). On whoever the act belongs to, as a
	// fear's is.
	if (UCataclysmAbilitySystemComponent* Applier = Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(UCataclysmCombatEvents::AttackerOf(Instigator))))
	{
		Applier->NoteCrowdControlApplied();
	}
	return true;
}

bool UCataclysmSilence::IsSilenced(const AActor* Actor)
{
	return UCataclysmSkillEffects::HasTag(Actor, SilencedTag());
}
