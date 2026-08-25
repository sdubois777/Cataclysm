// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmWeaponSlotsComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmUndesignedSkill.h"
#include "Items/CataclysmItem.h"
#include "Data/CataclysmDataRows.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemInterface.h"
#include "Cataclysm.h"
#include "Engine/DataTable.h"
#include "GameplayTagsManager.h"

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

FString UCataclysmWeaponSlotsComponent::GetEquippedSubType() const
{
	if (EquippedWeaponType.IsEmpty())
	{
		return FString();
	}

	const UDataTable* Bases = ItemBaseTable
		? ItemBaseTable.Get()
		: UCataclysmItemModifiers::LoadBaseTable();
	if (!Bases)
	{
		return FString();
	}

	FString Found;
	Bases->ForeachRow<FCataclysmItemBaseRow>(
		TEXT("UCataclysmWeaponSlotsComponent::GetEquippedSubType"),
		[&](const FName&, const FCataclysmItemBaseRow& Row)
		{
			if (Found.IsEmpty()
				&& Row.WeaponType.Equals(EquippedWeaponType, ESearchCase::IgnoreCase))
			{
				Found = Row.SubType;
			}
		});

	return Found;
}

FString UCataclysmWeaponSlotsComponent::SubTypeOf(const AActor* Actor)
{
	if (!Actor)
	{
		return FString();
	}

	if (const UCataclysmWeaponSlotsComponent* Slots =
			Actor->FindComponentByClass<UCataclysmWeaponSlotsComponent>())
	{
		return Slots->GetEquippedSubType();
	}

	// No weapon slots at all, which is every enemy: they attack from their own
	// attack damage rather than from a weapon. Empty is the answer every hit gave
	// before this existed, so nothing about an enemy's hit changes.
	return FString();
}

FString UCataclysmWeaponSlotsComponent::DamageTypeOf(const AActor* Actor)
{
	if (!Actor)
	{
		return FString();
	}

	if (const UCataclysmWeaponSlotsComponent* Slots =
			Actor->FindComponentByClass<UCataclysmWeaponSlotsComponent>())
	{
		return Slots->GetDamageType();
	}

	// No weapon slots at all, which is every enemy. See the header for why
	// empty is the right answer rather than a gap. Issue #975.
	return FString();
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

	AvailableSkills.Reset();

	// THE BASIC ATTACK COMES FROM THE WEAPON, NOT FROM THE MATRIX, so it is
	// collected first and separately. Two things follow from that and both are
	// intended. It does not depend on the matrix loading, and it does not depend
	// on the damage type: a War Wand has no matrix skills at all, because the
	// design says not every damage type covers every weapon, and it still swings.
	// Issue #524.
	if (!ItemBaseTable)
	{
		ItemBaseTable = UCataclysmItemModifiers::LoadBaseTable();
	}

	FCataclysmWeaponSkill Basic = UCataclysmWeaponSkills::BasicAttackFor(
		ItemBaseTable, EquippedWeaponType);
	if (Basic.Slot != ECataclysmAbilitySlot::None)
	{
		// THE ELEMENT TAG IS ADDED HERE BECAUSE ONLY THIS COMPONENT KNOWS IT.
		// One Item Bases row serves a weapon across every damage type it can
		// roll, so the row cannot state one; the equipped weapon's rolled type
		// is what DamageType holds.
		//
		// WITHOUT IT THE BASIC ATTACK IS THE ONE SLOT NO SCOPED MODIFIER
		// REACHES. UCataclysmStatPipeline::ModifierApplies asks whether the
		// skill in hand carries every tag a modifier requires, and every gear
		// increase the design has is scoped to an element. Burning Wrath grants
		// increased damage scoped to Element.Demonic; untagged, the basic attack
		// would be the only one of the seven granted skills it did not increase,
		// and it is the 100% weapon damage anchor the other six are percentages
		// of. Nothing reports that: ModifierApplies simply returns false.
		//
		// ErrorIfNotFound is false so a damage type with no registered tag adds
		// nothing rather than raising during equip, matching what
		// CataclysmAbilitySlots::Tag and UCataclysmSkillTemplate::ElementTag do.
		if (!DamageType.IsEmpty())
		{
			const FGameplayTag Element =
				UGameplayTagsManager::Get().RequestGameplayTag(
					FName(*FString::Printf(TEXT("Element.%s"), *DamageType)),
					/*ErrorIfNotFound=*/false);
			if (Element.IsValid())
			{
				Basic.Tags.AddTag(Element);
			}
			else
			{
				UE_LOG(LogCataclysm, Warning,
					TEXT("There is no Element.%s tag, so the %s's basic attack "
						 "carries no element and no scoped gear modifier will "
						 "reach it."),
					*DamageType, *EquippedWeaponType);
			}
		}

		AvailableSkills.Add(Basic);
	}

	if (!WeaponSkillTable)
	{
		WeaponSkillTable = UCataclysmWeaponSkills::LoadGeneratedTable();
	}

	if (WeaponSkillTable)
	{
		AvailableSkills.Append(UCataclysmWeaponSkills::SkillsFor(
			WeaponSkillTable, EquippedWeaponType, DamageType));
	}
	else
	{
		// LoadGeneratedTable has already said why. Not returned on any more,
		// because the basic attack does not come from that table and a character
		// who can still swing is better than one who cannot.
		UE_LOG(LogCataclysm, Warning,
			TEXT("The %s has no skill matrix to read, so only its basic attack "
				 "is available."),
			*EquippedWeaponType);
	}

	// THE ATTACK DAMAGE ATTRIBUTE IS NOT WRITTEN HERE, AND USED TO BE. This
	// component wrote it from the equipped weapon TYPE, which is how issue #840
	// happened: a second worn weapon changed nothing and an upgrade level never
	// applied. Issue #845 then found that a weapon's attack damage AFFIXES were
	// being gathered and dropped, because two things would otherwise write the
	// same attribute and the last one to run would win.
	//
	// UCataclysmPlayerClassStats::ApplyTo owns it now, and it is the only
	// writer. A weapon's damage reaches it as an ordinary flat modifier from
	// UCataclysmEquipmentComponent::GatherModifiers, alongside every affix on
	// every other piece. This component's job is which SKILLS exist.
	//
	// THE BASE CRITICAL STRIKE CHANCE HAS NOW MOVED WITH THEM TOO. Issue #894.
	// It is still the skill's rather than the character's, which is what the
	// design says, but the four affixes naming it could not scale a value this
	// component SET on every equip. It arrives as a base override from
	// UCataclysmEquipmentComponent::StatBasesFromWeapons instead, alongside the
	// swing rate, and UCataclysmPlayerClassStats::ApplyTo is the only writer of
	// all three.

	if (AvailableSkills.IsEmpty())
	{
		// A weapon its damage type does not cover AND which arms nobody, which
		// today is only the Shield. Verbose, not a warning: the design says not
		// every damage type has skills for every weapon type, and issue #619 says
		// a Shield composes no hit.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("The %s has no %s skills and no basic attack, so no slot was "
				 "filled."),
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
			Template->SkillTags = Skill.Tags;

			// AND ITS OWN CRITICAL STRIKE CHANCE, stamped here rather than
			// written onto the character, because six skills are granted at once
			// and the character has one CritChance attribute to hold them in.
			// Each hit carries the chance of the skill that dealt it instead.
			// Issue #657.
			Template->CritChancePercent = Skill.CritChancePercent;

			// AND WHAT IT IS WORTH, WAITS AND COSTS. A slot is a key and a
			// skill is worth what it is worth wherever it is put, decided
			// 2026-08-22. Each is -1 on every row today, and -1 is what the
			// ability already treats as "take the slot's figure", so this
			// changes nothing until a number is written. Issue #836.
			Template->DamagePercentOverride = Skill.DamagePercent;
			Template->CooldownOverride = Skill.Cooldown;
			Template->ManaCostOverride = Skill.ManaCost;
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

	// ASKED OF THE MATRIX SLOTS, NOT OF THE TOTAL, and that distinction keeps
	// this warning working. Since issue #524 the basic attack is granted from the
	// weapon base whatever the damage type covers, so counting it would let a
	// starting weapon with no designed skills at all report one filled slot and
	// stay silent.
	bool bAnyDesignedSkill = false;
	for (const FCataclysmWeaponSkill& Skill : AvailableSkills)
	{
		if (Skill.Slot != ECataclysmAbilitySlot::BasicAttack)
		{
			bAnyDesignedSkill = true;
			break;
		}
	}

	if (!bAnyDesignedSkill)
	{
		// LOUD, BECAUSE THIS IS A CHARACTER WHO CAN ONLY SWING. It means the
		// starting weapon type is one the damage type does not cover, or is
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

void UCataclysmWeaponSlotsComponent::UnequipWeapon()
{
	if (UCataclysmAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
	{
		GrantedHandles.TakeFromAbilitySystem(AbilitySystem);
	}

	AvailableSkills.Reset();
	EquippedWeaponType.Reset();

	// NO ATTRIBUTE IS WRITTEN HERE, AND THREE USED TO BE. Taking a weapon off
	// has to leave the character hitting for nothing, swinging at nothing and
	// critically striking never, rather than keeping whatever they last held.
	// All three are now the job of UCataclysmPlayerClassStats::ApplyTo, which
	// recomputes every stat from what is worn, and
	// UCataclysmEquipmentComponent::RefreshAttributes runs it whenever
	// equipment changes. Attack damage and attack speed moved in issue #845 and
	// critical strike chance in issue #894.
}
