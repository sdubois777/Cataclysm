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
	//
	// A KNOCKDOWN COUNTS TOO, WHICH IS WHY THIS ASKS `CannotAct` AND NOT
	// `IsStunned`. Section VI of the design document puts stun and knockdown in
	// one row of its table of hard stops, so a creature that kept swinging from
	// the floor would be exactly the defect this line was written to stop.
	//
	// A PIN DOES NOT COUNT. A pinned character cannot move and can still fight,
	// so its automatic attack keeps swinging at whatever stays within reach.
	return !UCataclysmSkillEffects::CannotAct(Character);
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
	if (!Skill)
	{
		return 0.0f;
	}

	// AND A RUNNING BUFF MAY LENGTHEN IT. The Whip's Coil of Embers: "your
	// attack range is increased by 30%". The basic attack is the clearest thing
	// "attack range" can mean, and it is a separate read from the one in
	// `UCataclysmSkillTemplate::ScaledRadiusCm` because the two answer different
	// questions -- that one is how far a SKILL reaches, this one is how far the
	// automatic swing reaches and how far away it will start swinging at all.
	//
	// `ShouldSwingNow` READS THIS, so a character holding the coil begins
	// swinging at something further away rather than reaching it and missing.
	return Skill->Params.RadiusCm
		* (1.0f + UCataclysmSkillTemplate::HeldRangeIncreasePercent(
					  AbilitySystem ? AbilitySystem->GetAvatarActor() : nullptr)
					  / 100.0f);
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

// ---------------------------------------------------------------------------
// Swinging at one chosen target. Issue #1187
// ---------------------------------------------------------------------------

bool UCataclysmBasicAttack::TargetIsInReach(const AActor* Character,
											const AActor* Target, float ReachCm)
{
	if (!Character || !Target || ReachCm <= 0.0f)
	{
		return false;
	}

	// THE SAME THREE QUESTIONS THE SPHERE SEARCH ASKS, in the same order, so a
	// click cannot start a swing the automatic search would never have started.
	// `UCataclysmTargeting::FindEnemiesInSphere` refuses anything on the same
	// side and, since issue #570, anything already marked dead.
	if (!UCataclysmTargeting::IsHostileTo(Target, Character))
	{
		return false;
	}

	if (UCataclysmSkillEffects::IsDead(Target))
	{
		return false;
	}

	// MEASURED BETWEEN THE ACTORS' ORIGINS, which is what the sphere search does
	// too. Neither takes a capsule radius off, so a large creature is reached
	// from slightly further away than a small one in both.
	return FVector::Dist(Character->GetActorLocation(),
						 Target->GetActorLocation()) <= ReachCm;
}

bool UCataclysmBasicAttack::IntervalHasPassed(float LastSwingSeconds,
											  float NowSeconds,
											  float IntervalSeconds)
{
	if (IntervalSeconds <= 0.0f)
	{
		// No rate at all: holding nothing, or holding something that states no
		// attack speed. `SecondsBetweenSwings` says this reads as "never swing"
		// rather than as "swing continuously", and it is read that way here.
		return false;
	}

	// A FIRST SWING IS ALWAYS ALLOWED. A controller that has never swung holds a
	// last-swing time far enough in the past that the subtraction answers true,
	// and there is nothing to special-case.
	return (NowSeconds - LastSwingSeconds) >= IntervalSeconds;
}
