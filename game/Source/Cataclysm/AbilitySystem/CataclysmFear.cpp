// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmFear.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "GameplayTagsManager.h"

FGameplayTag UCataclysmFear::FearedTag()
{
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("State.Feared")), /*ErrorIfNotFound=*/false);
}

FGameplayTag UCataclysmFear::FearImmuneTag()
{
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("State.FearImmune")), /*ErrorIfNotFound=*/false);
}

bool UCataclysmFear::ApplyFear(AActor* Instigator, AActor* Target, float Seconds,
							   const FVector& From)
{
	ACataclysmCharacterBase* Character = Cast<ACataclysmCharacterBase>(Target);
	if (!Character || Seconds <= 0.0f)
	{
		return false;
	}

	// CROWD-CONTROL RESISTANCE FIRST, AS FOR A STUN: it shortens the fear in
	// proportion, and at 100% or more there is nothing left to apply.
	Seconds = UCataclysmSkillEffects::HeldSecondsAfterCrowdControlResistance(Target, Seconds);
	if (Seconds <= 0.0f)
	{
		return false;
	}

	// THE WINDOW STUN AND KNOCKDOWN SHARE, AND BOSS IMMUNITY, the two rules fear
	// takes as madness does. Diablo III: "Bosses are immune to any kind of Charm,
	// Fear, Hex, Knockback or Taunt effect"; Diablo IV: "Fear has no effect on
	// bosses".
	if (RefusedByTheRedirectionRules(Target))
	{
		return false;
	}

	// DIRGE RESONANCE'S "FEAR IMMUNITY", and any skill that names fear.
	if (UCataclysmSkillEffects::HasTag(Target, FearImmuneTag())
		|| UCataclysmSkillTemplate::IsImmuneTo(Target, TEXT("Fear")))
	{
		return false;
	}

	// THE SOURCE BEFORE THE TAG, so nothing that reacts to the tag reads a stale
	// point.
	Character->FearSource = From;
	if (!UCataclysmSkillEffects::ApplyTagForDuration(Instigator, Target, FearedTag(), Seconds))
	{
		return false;
	}
	OpenTheSharedWindow(Instigator, Target);

	// AND IT IS CROWD CONTROL APPLIED, for the rows that read that event.
	if (UCataclysmAbilitySystemComponent* Applier = Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(UCataclysmCombatEvents::AttackerOf(Instigator))))
	{
		Applier->NoteCrowdControlApplied();
	}
	return true;
}

bool UCataclysmFear::RefusedByTheRedirectionRules(const AActor* Target)
{
	if (UCataclysmSkillEffects::HasTag(Target, UCataclysmSkillEffects::StunImmuneTag()))
	{
		return true;
	}
	const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Target);
	return Enemy && Enemy->IsBoss();
}

void UCataclysmFear::OpenTheSharedWindow(AActor* Instigator, AActor* Target)
{
	UCataclysmSkillEffects::ApplyTagForDuration(Instigator, Target,
												UCataclysmSkillEffects::StunImmuneTag(),
												UCataclysmSkillEffects::StunImmunityWindowSeconds);
}

bool UCataclysmFear::IsFeared(const AActor* Actor)
{
	return UCataclysmSkillEffects::HasTag(Actor, FearedTag());
}

bool UCataclysmFear::FleeSourceOf(const AActor* Character, FVector& OutFrom)
{
	if (IsFeared(Character))
	{
		if (const ACataclysmCharacterBase* Base = Cast<ACataclysmCharacterBase>(Character))
		{
			OutFrom = Base->FearSource;
			return true;
		}
	}
	if (const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Character))
	{
		return Enemy->FleeSourceNow(OutFrom);
	}
	return false;
}

FVector UCataclysmFear::FleeGoalFor(const AActor* Character, const FVector& From)
{
	if (!Character)
	{
		return From;
	}
	const FVector Here = Character->GetActorLocation();
	FVector Away = Here - From;
	Away.Z = 0.0f;
	if (!Away.Normalize())
	{
		Away = Character->GetActorForwardVector();
		Away.Z = 0.0f;
		Away.Normalize();
	}
	return Here + Away * FleeStepMetres * 100.0f;
}

FVector UCataclysmFear::FleeDirectionFor(const AActor* Character)
{
	FVector From;
	if (!Character || !FleeSourceOf(Character, From))
	{
		return FVector::ZeroVector;
	}
	return (FleeGoalFor(Character, From) - Character->GetActorLocation()).GetSafeNormal2D();
}
