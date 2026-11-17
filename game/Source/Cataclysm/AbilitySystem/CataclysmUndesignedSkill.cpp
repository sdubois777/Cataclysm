// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmUndesignedSkill.h"
#include "Cataclysm.h"

UCataclysmUndesignedSkill::UCataclysmUndesignedSkill()
{
	// The slot is left at None on the class default on purpose. Which slot this
	// occupies is decided by the weapon that granted it, not by the class, and
	// UCataclysmAbilitySystemComponent::GiveAbilityInSlot stamps it per grant.
	Slot = ECataclysmAbilitySlot::None;
}

void UCataclysmUndesignedSkill::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Verbose rather than Warning. Pressing a key bound to an undesigned skill
	// is the expected state of the game today, not a fault, and at Warning every
	// press would add noise to a log people read for real problems.
	UE_LOG(LogCataclysm, Verbose,
		TEXT("'%s' has no behaviour yet; its slot is filled so the key reaches "
			 "something. See issue #36."),
		SkillName.IsEmpty() ? TEXT("an undesigned skill") : *SkillName);

	EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true,
			   /*bWasCancelled=*/false);
}
