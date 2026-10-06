// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmTriggeredSkill.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Items/CataclysmWeaponSlotsComponent.h"

bool UCataclysmTriggeredSkill::Trigger(AActor* Character, const FCataclysmWeaponSkill& Skill, const FVector& Aim,
									   float DamageShare)
{
	UCataclysmAbilitySystemComponent* System =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Character));
	const TSubclassOf<UCataclysmGameplayAbility> AbilityClass = UCataclysmWeaponSkills::TemplateFor(Skill.Shape);
	if (!System || !AbilityClass || !System->IsOwnerActorAuthoritative())
	{
		return false;
	}

	// NO SLOT TAG ON THE SPEC, which is the whole of "no key finds it". See the class comment.
	FGameplayAbilitySpec Spec(AbilityClass, /*Level=*/1);
	Spec.SourceObject = Character;
	const FGameplayAbilitySpecHandle Handle = System->GiveAbility(Spec);

	// NO INSTANCE MEANS THE GRANT WAS DEFERRED: this was called inside another ability's activation. The pending
	// grant is taken back, or it would arrive later as a skill nothing stamped and nothing removes.
	FGameplayAbilitySpec* Granted = System->FindAbilitySpecFromHandle(Handle);
	UCataclysmSkillTemplate* Template =
		Granted ? Cast<UCataclysmSkillTemplate>(Granted->GetPrimaryInstance()) : nullptr;
	if (!Template)
	{
		System->ClearAbility(Handle);
		return false;
	}

	Template->Slot = Skill.Slot;
	UCataclysmWeaponSkills::StampOnto(*Template, Skill);
	Template->bFreeRepeat = true;
	Template->FreeRepeatAim = Aim;
	Template->FreeRepeatDamageShare = DamageShare;
	if (!System->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/true))
	{
		System->ClearAbility(Handle);
		return false;
	}

	// AFTER THE ACTIVATION AND NOT BEFORE: the engine refuses to activate a spec already marked for removal. A skill
	// that ended inside its own activation is removed here and now; one still running is removed when it ends.
	System->SetRemoveAbilityOnEnd(Handle);
	return true;
}

bool UCataclysmTriggeredSkill::RepeatsFromARow(const FCataclysmWeaponSkill& Skill)
{
	return UCataclysmDungeonModifierEffects::WildMagicLeavesOut(Skill) == ECataclysmWildMagicLeftOut::InThePool
		&& Skill.Shape != ECataclysmSkillShape::SelfBuff;
}

bool UCataclysmTriggeredSkill::MakePendingRepeat(AActor* Character)
{
	UCataclysmAbilitySystemComponent* System =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Character));
	if (!System || System->PendingRepeatSkill().IsNone())
	{
		return false;
	}

	// TAKEN AND CLEARED BEFORE ANYTHING IS STARTED, so whatever happens next there is one repeat for one use.
	const FName Named = System->PendingRepeatSkill();
	const FVector Aim = System->PendingRepeatAim();
	const float Share = System->PendingRepeatShare();
	System->ClearPendingRepeat();

	// THE BASIC ATTACK COMES FROM THE WEAPON AND NOT FROM THE TABLE. Copied, because starting it may change what the
	// character holds.
	if (Named == FName(UCataclysmWeaponSkills::BasicAttackName))
	{
		const FCataclysmWeaponSkill* Held = HeldBasicAttack(Character);
		if (!Held)
		{
			return false;
		}
		const FCataclysmWeaponSkill Basic = *Held;
		return RepeatsFromARow(Basic) && Trigger(Character, Basic, Aim, Share);
	}

	for (const FCataclysmWeaponSkill& Skill : UCataclysmWeaponSkills::SkillsOfDamageType(
			 UCataclysmWeaponSkills::LoadGeneratedTable(), UCataclysmWeaponSlotsComponent::DamageTypeOf(Character)))
	{
		if (FName(*Skill.Name) == Named)
		{
			return RepeatsFromARow(Skill) && Trigger(Character, Skill, Aim, Share);
		}
	}
	return false;
}

const FCataclysmWeaponSkill* UCataclysmTriggeredSkill::HeldBasicAttack(const AActor* Character)
{
	const UCataclysmWeaponSlotsComponent* Slots =
		Character ? Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>() : nullptr;
	return Slots ? Slots->GetAvailableSkills().FindByPredicate([](const FCataclysmWeaponSkill& Skill)
	{
		return Skill.Slot == ECataclysmAbilitySlot::BasicAttack;
	}) : nullptr;
}

FGameplayTagContainer UCataclysmTriggeredSkill::BasicAttackUseTags(const AActor* Character,
																   const FGameplayTagContainer* OwnTags)
{
	FGameplayTagContainer Asked = OwnTags ? *OwnTags : FGameplayTagContainer();
	const FCataclysmWeaponSkill* Held = HeldBasicAttack(Character);
	FGameplayTag ByShape;
	if (Held && Held->Shape == ECataclysmSkillShape::Strike)
	{
		ByShape = UCataclysmDamageCalculation::MeleeTag();
	}
	else if (Held && Held->Shape == ECataclysmSkillShape::Projectile)
	{
		ByShape = UCataclysmDamageCalculation::RangedTag();
	}
	if (ByShape.IsValid())
	{
		Asked.AddTag(ByShape);
	}
	return Asked;
}
