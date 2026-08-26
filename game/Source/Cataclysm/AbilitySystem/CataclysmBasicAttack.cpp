// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For the attack speed attribute the swing rate falls back to. Issue #1002.
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "GameFramework/Actor.h"

namespace
{
	/**
	 * The granted ability sitting in the basic attack slot, or null.
	 *
	 * MATCHED ON THE SPEC'S OWN TAGS RATHER THAN ON THE ABILITY CLASS, and
	 * HasTagExact rather than HasTag, which is the same rule
	 * UCataclysmAbilitySystemComponent::AbilityInputTagPressed follows. The slot
	 * is a property of the pairing of a weapon with an ability, not of the
	 * ability class: one placeholder class stands in for several slots.
	 */
	const FGameplayAbilitySpec* FindBasicAttackSpec(
		const UAbilitySystemComponent* AbilitySystem)
	{
		if (!AbilitySystem)
		{
			return nullptr;
		}

		const FGameplayTag SlotTag =
			CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::BasicAttack);
		if (!SlotTag.IsValid())
		{
			return nullptr;
		}

		for (const FGameplayAbilitySpec& Spec :
			 AbilitySystem->GetActivatableAbilities())
		{
			if (Spec.Ability
				&& Spec.GetDynamicSpecSourceTags().HasTagExact(SlotTag))
			{
				return &Spec;
			}
		}

		return nullptr;
	}
}

float UCataclysmBasicAttack::SecondsBetweenSwings(float AttackSpeedPerSecond)
{
	if (AttackSpeedPerSecond <= 0.0f)
	{
		// Holding nothing, or holding something that states no rate. The caller
		// must read this as "never swing" rather than as "swing continuously".
		return 0.0f;
	}

	return FMath::Max(FastestSwingSeconds, 1.0f / AttackSpeedPerSecond);
}

float UCataclysmBasicAttack::SecondsBetweenSwingsFor(
	const UCataclysmAbilitySystemComponent* AbilitySystem)
{
	const FGameplayAttribute Speed =
		UCataclysmCombatAttributeSet::GetAttackSpeedAttribute();
	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Speed))
	{
		// No combat attribute set means no rate to read. Zero is "never swing",
		// which is what a character holding nothing already gets.
		return 0.0f;
	}

	// THE ATTRIBUTE IS THE FALLBACK AND NOT THE ANSWER. Issue #1002.
	// `StatForSkill` returns it unchanged for a character with no attack speed
	// modifier recorded at all -- every enemy, and every player before its first
	// stat refresh -- so nothing without a state-dependent node behaves
	// differently from before this existed.
	//
	// NO SKILL TAGS, because the basic attack is not one of the tree's skills:
	// it is what the character does between them. A modifier scoped to a tag
	// therefore does not reach it, which is the same answer it got when this was
	// a plain read of the attribute.
	return SecondsBetweenSwings(AbilitySystem->StatForSkill(
		FName(TEXT("attack_speed")), FGameplayTagContainer(),
		AbilitySystem->GetNumericAttribute(Speed)));
}

bool UCataclysmBasicAttack::MaySwing(const AActor* Character)
{
	if (!Character)
	{
		return false;
	}

	if (UCataclysmSkillEffects::IsDead(Character))
	{
		return false;
	}

	// THE SAME TEST THE PLAYER CONTROLLER USES TO REFUSE A SKILL AND A STEP. The
	// design defines a stun as the target being unable to act at all, so an
	// automatic attack carrying on through one would leave a stunned character
	// with exactly one thing it could still do.
	return !UCataclysmSkillEffects::IsStunned(Character);
}

float UCataclysmBasicAttack::ReachCmOf(
	const UAbilitySystemComponent* AbilitySystem)
{
	const FGameplayAbilitySpec* Spec = FindBasicAttackSpec(AbilitySystem);
	if (!Spec)
	{
		return 0.0f;
	}

	// THE PRIMARY INSTANCE, NOT Spec->Ability, AND THE DIFFERENCE IS THE WHOLE
	// FUNCTION. Spec->Ability is the class default object, and a skill's shape
	// is not on the class: it is parsed out of the weapon's row and written onto
	// the granted instance by UCataclysmWeaponSlotsComponent, which sets
	// Spec->GetPrimaryInstance()->Params. Reading the class default answers 0
	// for every weapon in the game, so the basic attack would have found nothing
	// in reach and never swung -- the same shape of defect as the attack speed
	// this feature was waiting on. The test that caught it is
	// Cataclysm.BasicAttack.ALandedBasicAttackPutsTheManaOnTheCharacter.
	//
	// THE FALLBACK IS NOT DEAD CODE. GetPrimaryInstance answers null for an
	// ability granted as InstancedPerExecution or NonInstanced, and those keep
	// their shape on the class because there is no instance to put it on.
	const UGameplayAbility* Instance = Spec->GetPrimaryInstance();
	const UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(
		Instance ? Instance : Spec->Ability.Get());

	// THE SHIELD IS THE CASE WITH NO BASIC ATTACK AT ALL, and it is handled
	// above by finding no spec: UCataclysmWeaponSkills::BasicAttackFor leaves
	// the slot as None for a base with an empty shape, so nothing is granted.
	return Skill ? Skill->Params.RadiusCm : 0.0f;
}

bool UCataclysmBasicAttack::SomethingInReach(const AActor* Character,
											 float ReachCm)
{
	if (!Character || ReachCm <= 0.0f)
	{
		return false;
	}

	// THE SAME SEARCH THE SKILL ITSELF WILL DO, so the two cannot disagree about
	// what counts as a target. UCataclysmTargeting refuses anything on the same
	// side and, since issue #570, anything already marked dead -- so a corpse
	// lying in reach does not keep a character swinging at it.
	return UCataclysmTargeting::FindEnemiesInSphere(
			   Character->GetWorld(), Character,
			   Character->GetActorLocation(), ReachCm)
		.Num() > 0;
}

bool UCataclysmBasicAttack::ShouldSwingNow(const AActor* Character,
										   float ReachCm)
{
	return MaySwing(Character) && SomethingInReach(Character, ReachCm);
}

bool UCataclysmBasicAttack::Swing(
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	const FGameplayAbilitySpec* Spec = FindBasicAttackSpec(AbilitySystem);
	if (!Spec)
	{
		return false;
	}

	if (Spec->IsActive())
	{
		// The previous swing has not finished. Starting a second copy would let
		// a weapon whose animation is longer than its interval overlap with
		// itself, which is not what a rate means.
		return false;
	}

	// REMOTE ACTIVATION LEFT ON, for the same reason ProcessAbilityInput leaves
	// it on: abilities in this project default to ServerInitiated, and the
	// engine turns a client's TryActivateAbility on such an ability into a
	// server call rather than refusing it.
	return AbilitySystem->TryActivateAbility(Spec->Handle,
											 /*bAllowRemoteActivation=*/true);
}
