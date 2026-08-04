// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmWeaponSlotsComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmUndesignedSkill.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemInterface.h"
#include "Cataclysm.h"
#include "Engine/DataTable.h"

UCataclysmWeaponSlotsComponent::UCataclysmWeaponSlotsComponent()
{
	// Nothing to do per frame. Slots change when a weapon changes, which is an
	// event, not a poll.
	PrimaryComponentTick.bCanEverTick = false;

	UndesignedSkillClass = UCataclysmUndesignedSkill::StaticClass();
}

UCataclysmAbilitySystemComponent* UCataclysmWeaponSlotsComponent::GetAbilitySystem() const
{
	// Through the interface rather than by searching the owner for a component.
	// The player's ability system lives on the player state, not on the pawn,
	// so looking on the owning actor would find nothing.
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	return Cast<UCataclysmAbilitySystemComponent>(
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Owner));
}

int32 UCataclysmWeaponSlotsComponent::EquipWeaponType(const FString& NewWeaponType)
{
	// Taken back first, and unconditionally. Refilling without emptying would
	// leave the previous weapon's abilities granted alongside the new ones, and
	// two abilities carrying the same slot tag means one key firing both.
	UnequipWeapon();

	EquippedWeaponType = NewWeaponType;
	if (EquippedWeaponType.IsEmpty())
	{
		return 0;
	}

	if (!WeaponSkillTable)
	{
		WeaponSkillTable = UCataclysmWeaponSkills::LoadGeneratedTable();
	}

	if (!WeaponSkillTable)
	{
		// LoadGeneratedTable has already said why. Nothing more useful to add
		// here, and a character with no abilities and no explanation is exactly
		// what that logging exists to prevent.
		return 0;
	}

	AvailableSkills = UCataclysmWeaponSkills::SkillsFor(
		WeaponSkillTable, EquippedWeaponType, DamageType);

	if (AvailableSkills.IsEmpty())
	{
		// Expected for a weapon its damage type does not cover, such as a War
		// Wand. Verbose, not a warning: the design says not every damage type
		// has skills for every weapon type.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("The %s has no %s skills, so no slot was filled."),
			*EquippedWeaponType, *DamageType);
		return 0;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem = GetAbilitySystem();
	if (!AbilitySystem)
	{
		// Not an error. The skills are still recorded, and the component is
		// readable before the ability system exists -- possession order is not
		// something this component controls.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("The %s offers %d skills but there is no ability system to "
				 "grant them to yet."),
			*EquippedWeaponType, AvailableSkills.Num());
		return 0;
	}

	if (!IsValid(UndesignedSkillClass))
	{
		UE_LOG(LogCataclysm, Error,
			TEXT("No ability class to grant into a slot, so the %s filled none "
				 "of its %d slots."),
			*EquippedWeaponType, AvailableSkills.Num());
		return 0;
	}

	int32 Filled = 0;
	for (const FCataclysmWeaponSkill& Skill : AvailableSkills)
	{
		const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
			UndesignedSkillClass, Skill.Slot, /*Level=*/1, /*SourceObject=*/this);

		if (!Handle.IsValid())
		{
			continue;
		}

		GrantedHandles.AddAbility(Handle);
		++Filled;

		// Named on the granted instance rather than on the class, because one
		// class stands in for every undesigned skill and the name is what tells
		// them apart.
		if (FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle))
		{
			if (UCataclysmUndesignedSkill* Instance =
					Cast<UCataclysmUndesignedSkill>(Spec->GetPrimaryInstance()))
			{
				Instance->SkillName = Skill.Name;
				Instance->SkillDescription = Skill.Description;
			}
		}
	}

	UE_LOG(LogCataclysm, Verbose, TEXT("The %s filled %d of its %d slots."),
		*EquippedWeaponType, Filled, AvailableSkills.Num());

	return Filled;
}

void UCataclysmWeaponSlotsComponent::UnequipWeapon()
{
	if (UCataclysmAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
	{
		GrantedHandles.TakeFromAbilitySystem(AbilitySystem);
	}

	AvailableSkills.Reset();
	EquippedWeaponType.Reset();
}
