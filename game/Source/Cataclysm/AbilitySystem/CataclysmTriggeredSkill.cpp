// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmTriggeredSkill.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Items/CataclysmWeaponSlotsComponent.h"

/** Which skill of the pool a held trigger takes. Ruled 2026-10-06. */
static TAutoConsoleVariable<int32> CVarTriggerHeldSkillPick(
	TEXT("Cataclysm.TriggerHeldSkillPick"), -1,
	TEXT("Pins which skill of its pool a trigger-held-skill enchantment takes, by index. Negative picks for real."),
	ECVF_Default);

bool UCataclysmTriggeredSkill::Trigger(AActor* Character, const FCataclysmWeaponSkill& Skill, const FVector& Aim,
									   float DamageShare, bool bPaysCost)
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
	Template->bFreeRepeatPaysCost = bPaysCost;
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

TArray<FCataclysmWeaponSkill> UCataclysmTriggeredSkill::HeldSkillsToTrigger(const AActor* Character, FName UsedSkill,
																			 bool bSpells)
{
	TArray<FCataclysmWeaponSkill> Pool;
	const UCataclysmWeaponSlotsComponent* Slots =
		Character ? Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>() : nullptr;
	if (!Slots)
	{
		return Pool;
	}

	const UDataTable* SlotTable = UCataclysmSkillSlots::LoadGeneratedTable();
	for (const FCataclysmWeaponSkill& Skill : Slots->GetAvailableSkills())
	{
		if (FName(*Skill.Name) == UsedSkill || !RepeatsFromARow(Skill))
		{
			continue;
		}
		if (bSpells)
		{
			if (UCataclysmSkillEffects::IsSpell(Skill.Tags))
			{
				Pool.Add(Skill);
			}
			continue;
		}
		// THE ROW'S OWN COOLDOWN WHEN IT STATES ONE, AND ITS SLOT'S WHEN IT DOES NOT, as the granted skill reads it.
		const float Cooldown =
			Skill.Cooldown >= 0.0f ? Skill.Cooldown : UCataclysmSkillSlots::NumbersFor(SlotTable, Skill.Slot).Cooldown;
		if (Cooldown > 0.0f)
		{
			Pool.Add(Skill);
		}
	}
	return Pool;
}

bool UCataclysmTriggeredSkill::MakePendingHeldTrigger(AActor* Character)
{
	UCataclysmAbilitySystemComponent* System =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Character));
	if (!System || System->PendingHeldTriggerUsedSkill().IsNone())
	{
		return false;
	}

	// TAKEN AND CLEARED BEFORE ANYTHING IS STARTED, so whatever happens next there is one trigger for one use.
	const FName Used = System->PendingHeldTriggerUsedSkill();
	const FVector Aim = System->PendingHeldTriggerAim();
	const bool bSpell = System->PendingHeldTriggerWantsASpell();
	const bool bCooldownSkill = System->PendingHeldTriggerWantsACooldownSkill();
	System->ClearPendingHeldTrigger();

	const auto PickFrom = [](const TArray<FCataclysmWeaponSkill>& Pool)
	{
		const int32 Pinned = CVarTriggerHeldSkillPick.GetValueOnAnyThread();
		return Pool[Pinned >= 0 ? Pinned % Pool.Num() : FMath::RandRange(0, Pool.Num() - 1)];
	};

	// THE SPELL ROW WINS WHEN A SPELL IS HELD, and then it is the only trigger of this use: one it cannot pay for is
	// refused and nothing else happens. Ruled 2026-10-06.
	if (bSpell)
	{
		const TArray<FCataclysmWeaponSkill> Spells = HeldSkillsToTrigger(Character, Used, /*bSpells=*/true);
		if (!Spells.IsEmpty())
		{
			return Trigger(Character, PickFrom(Spells), Aim, /*DamageShare=*/1.0f, /*bPaysCost=*/true);
		}
	}
	if (bCooldownSkill)
	{
		const TArray<FCataclysmWeaponSkill> WithACooldown = HeldSkillsToTrigger(Character, Used, /*bSpells=*/false);
		if (!WithACooldown.IsEmpty())
		{
			return Trigger(Character, PickFrom(WithACooldown), Aim);
		}
	}
	return false;
}
