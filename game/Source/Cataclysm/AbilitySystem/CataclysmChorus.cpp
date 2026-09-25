// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmChorus.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"

const TCHAR* UCataclysmChorus::Stat = TEXT("minions_repeat_your_skills");

int32 UCataclysmChorus::Repeat(AActor* Caster, AActor* Target, float Sent,
							   const UGameplayAbility* Skill)
{
	// A SKILL, AND NOT THE BASIC ATTACK, the one slot `skill_use` leaves out
	// too. A hit with no skill behind it is not "a skill you cast".
	const UCataclysmGameplayAbility* Ability = Cast<UCataclysmGameplayAbility>(Skill);
	if (!Ability || Ability->Slot == ECataclysmAbilitySlot::BasicAttack
		|| Sent <= 0.0f || !IsValid(Target) || UCataclysmSkillEffects::IsDead(Target))
	{
		return 0;
	}

	const UCataclysmAbilitySystemComponent* Theirs =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Caster));
	if (!Theirs
		|| Theirs->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f) <= 0.0f)
	{
		return 0;
	}

	const TArray<AActor*> Minions = UCataclysmCommand::ThingsCommandedBy(Caster);
	if (Minions.IsEmpty())
	{
		return 0;
	}

	// SPLIT, SO THE CHORUS AS A WHOLE IS WORTH THE SAME AT TWO MINIONS AS AT
	// NINE. Ruled 2026-09-25; the header says why.
	const float Each = Sent * SharePercent / 100.0f / Minions.Num();

	int32 Repeats = 0;
	for (AActor* Minion : Minions)
	{
		// ONE KILLED THE TARGET, SO THE REST HAVE NOTHING LEFT TO STRIKE.
		if (UCataclysmSkillEffects::IsDead(Target))
		{
			break;
		}
		if (UCataclysmSkillEffects::ApplyDirectDamage(
				Minion, Target, Each, ACataclysmMinion::OwnBlowDelivery(Minion)))
		{
			++Repeats;
		}
	}
	return Repeats;
}
