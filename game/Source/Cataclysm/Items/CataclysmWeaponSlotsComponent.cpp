// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmWeaponSlotsComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmUndesignedSkill.h"
#include "Items/CataclysmItem.h"
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

	// THE NUMBER EVERY SKILL IS A PERCENTAGE OF, and until issue #173 nothing
	// set it: UCataclysmCombatAttributeSet::AttackDamage was initialised to zero
	// and never changed, so a Heavy Attack at 250% of it dealt nothing and the
	// burn rider, being a share of the hit, applied nothing either.
	//
	// Set before the abilities are granted, so a skill activated on the same
	// frame as the equip already sees it.
	ApplyWeaponDamage();

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
		// THE ROW DECIDES WHICH CLASS IS GRANTED. A row naming a shape gets that
		// shape's shared template; a row with no shape still gets the
		// placeholder, which is all 61 War rows and every undesigned Demonic
		// one. That is what makes adding a skill of an existing shape a workbook
		// edit and no C++ at all, which is issue #37's second acceptance
		// criterion.
		TSubclassOf<UCataclysmGameplayAbility> AbilityClass =
			UCataclysmWeaponSkills::TemplateFor(Skill.Shape);
		if (!AbilityClass)
		{
			AbilityClass = UndesignedSkillClass;
		}

		const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
			AbilityClass, Skill.Slot, /*Level=*/1, /*SourceObject=*/this);

		if (!Handle.IsValid())
		{
			continue;
		}

		GrantedHandles.AddAbility(Handle);
		++Filled;

		// Stamped on the granted INSTANCE rather than on the class, because one
		// class stands for every skill of that shape. Two characters holding
		// different Projectile skills share UCataclysmProjectileSkill and differ
		// only in what is written here.
		FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
		UGameplayAbility* Instance = Spec ? Spec->GetPrimaryInstance() : nullptr;

		if (UCataclysmSkillTemplate* Template = Cast<UCataclysmSkillTemplate>(Instance))
		{
			Template->SkillName = Skill.Name;
			Template->SkillDescription = Skill.Description;
			Template->Params = Skill.Params;
		}
		else if (UCataclysmUndesignedSkill* Placeholder =
					Cast<UCataclysmUndesignedSkill>(Instance))
		{
			Placeholder->SkillName = Skill.Name;
			Placeholder->SkillDescription = Skill.Description;
		}
	}

	UE_LOG(LogCataclysm, Verbose, TEXT("The %s filled %d of its %d slots."),
		*EquippedWeaponType, Filled, AvailableSkills.Num());

	return Filled;
}

int32 UCataclysmWeaponSlotsComponent::EquipStartingWeapon()
{
	if (StartingWeaponType.IsEmpty())
	{
		// A deliberate choice to begin holding nothing is legitimate, so this is
		// not a warning. It is also what a character creator would produce
		// before the player has chosen.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("No starting weapon type is set, so no slot was filled."));
		return 0;
	}

	const int32 Filled = EquipWeaponType(StartingWeaponType);

	if (Filled == 0)
	{
		// LOUD, BECAUSE THIS IS A CHARACTER WHO CANNOT USE ANY ABILITY. It means
		// the starting weapon type is one the damage type does not cover, or is
		// misspelled -- and the symptom is a game that runs normally and does
		// nothing when a skill key is pressed, which is exactly how issue #169
		// went unnoticed.
		UE_LOG(LogCataclysm, Warning,
			TEXT("The starting weapon is a %s and the damage type is %s, which "
				 "grants no skills at all. Check StartingWeaponType against the "
				 "Weapon Skills sheet of docs/All_Things_Cataclysm.xlsx."),
			*StartingWeaponType, *DamageType);
	}

	return Filled;
}

void UCataclysmWeaponSlotsComponent::ApplyWeaponDamage()
{
	UCataclysmAbilitySystemComponent* AbilitySystem = GetAbilitySystem();
	if (!AbilitySystem)
	{
		return;
	}

	// AN ABILITY SYSTEM WITHOUT COMBAT ATTRIBUTES IS A LEGITIMATE ACTOR, and
	// writing to an attribute it does not have raises an engine ensure --
	// "Unable to get attribute set for attribute AttackDamage" -- rather than
	// failing quietly. The player and every enemy carry the combat set; a bare
	// ability system holder, which several tests build, does not.
	const FGameplayAttribute Attribute =
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Attribute))
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("The ability system has no combat attribute set, so the %s's "
				 "damage was not applied."),
			EquippedWeaponType.IsEmpty() ? TEXT("weapon") : *EquippedWeaponType);
		return;
	}

	if (!ItemBaseTable)
	{
		ItemBaseTable = UCataclysmItemModifiers::LoadBaseTable();
	}

	// SET, NOT ADDED, which is what makes swapping weapons safe. The character
	// holds one weapon, so its damage replaces whatever the last one supplied
	// rather than accumulating -- and an empty type sets zero, which is what
	// holding nothing is worth.
	const float Damage = EquippedWeaponType.IsEmpty()
		? 0.0f
		: UCataclysmItemModifiers::WeaponDamageForType(
			ItemBaseTable, EquippedWeaponType, WeaponGearLevel);

	AbilitySystem->SetNumericAttributeBase(Attribute, Damage);

	if (Damage <= 0.0f && !EquippedWeaponType.IsEmpty())
	{
		// A weapon that supplies no damage makes every skill deal nothing, and
		// the symptom is a game that runs normally and kills nothing.
		UE_LOG(LogCataclysm, Warning,
			TEXT("The %s supplies no attack damage, so every skill will deal "
				 "zero. Check its attack_damage implicit in the Item Bases "
				 "sheet of docs/All_Things_Cataclysm.xlsx."),
			*EquippedWeaponType);
	}
	else if (EquippedWeaponType.IsEmpty())
	{
		// Every equip clears the previous weapon first, so this runs on the way
		// through. Named separately because "The  supplies 0.0" reads as a bug.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("Holding nothing, so there is no attack damage."));
	}
	else
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("The %s supplies %.1f attack damage at gear level %d."),
			*EquippedWeaponType, Damage, WeaponGearLevel);
	}
}

void UCataclysmWeaponSlotsComponent::UnequipWeapon()
{
	if (UCataclysmAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
	{
		GrantedHandles.TakeFromAbilitySystem(AbilitySystem);
	}

	AvailableSkills.Reset();
	EquippedWeaponType.Reset();

	// After clearing the type, so it sets zero. A character holding nothing has
	// no weapon damage; leaving the last weapon's figure behind would mean an
	// unarmed character kept hitting for whatever they last held.
	ApplyWeaponDamage();
}
