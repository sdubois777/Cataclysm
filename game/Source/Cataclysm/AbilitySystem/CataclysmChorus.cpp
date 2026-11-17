// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmChorus.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmFear.h"
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
	if (!Ability || Ability->GetDesignedSlot() == ECataclysmAbilitySlot::BasicAttack
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

	// EACH MINION'S REPEAT IS WORTH THE WHOLE SHARE: N minions add N x 30%.
	// The owner's decision of 2026-09-25; the header says so.
	const float Each = Sent * SharePercent / 100.0f;

	int32 Repeats = 0;
	for (AActor* Minion : Minions)
	{
		// ONE KILLED THE TARGET, SO THE REST HAVE NOTHING LEFT TO STRIKE.
		if (UCataclysmSkillEffects::IsDead(Target))
		{
			break;
		}
		// A STUNNED, KNOCKED-DOWN OR FEARED CREATURE MAKES NO REPEAT (#2563): "Your minions
		// repeat each skill you cast" is each one acting, its hits its own (owner, 2026-09-17).
		// Brain's order, as Compel (#2560). Uncounted; 30% is each's (2026-09-25), not passed on.
		const AActor* Striker = Minion;
		FVector FleeSource;
		if (UCataclysmSkillEffects::CannotAct(Striker)
			|| UCataclysmFear::FleeSourceOf(Striker, FleeSource))
		{
			continue;
		}
		if (UCataclysmSkillEffects::ApplyDirectDamage(
				Minion, Target, Each, ACataclysmMinion::OwnBlowDelivery(Minion)))
		{
			++Repeats;
		}
	}
	return Repeats;
}
