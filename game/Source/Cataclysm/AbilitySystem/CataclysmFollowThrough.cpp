// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmFollowThrough.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Engine/World.h"

const TCHAR* UCataclysmFollowThrough::EverySecondsStat =
	TEXT("melee_kill_repeats_attack_every_seconds");

namespace
{
	UCataclysmAbilitySystemComponent* SystemOf(const AActor* Character)
	{
		return Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	}

	float EverySeconds(const UCataclysmAbilitySystemComponent* System)
	{
		return System
			? System->StatForSkill(FName(UCataclysmFollowThrough::EverySecondsStat),
								   FGameplayTagContainer(), 0.0f)
			: 0.0f;
	}
}

bool UCataclysmFollowThrough::IsOwnMeleeKill(const FCataclysmDeathNotice& Notice,
											 const AActor* Character)
{
	// A TICK IS NOT "A MELEE ATTACK", even one a melee skill's bleed finished:
	// the kill notice keeps the killing blow's melee flag either way, so the
	// tick is ruled out on its own flag.
	return Character && Notice.Killer == Character && Notice.bIsMelee
		&& !Notice.bByDamageOverTime;
}

UCataclysmSkillTemplate* UCataclysmFollowThrough::SkillNamed(
	UCataclysmAbilitySystemComponent* System, FName Name,
	FGameplayAbilitySpecHandle& OutHandle)
{
	OutHandle = FGameplayAbilitySpecHandle();
	if (!System || Name.IsNone())
	{
		return nullptr;
	}

	// BY NAME, BECAUSE THE KILL NOTICE CARRIES ONLY THE NAME. Every skill a
	// character is granted is instanced per actor, so the spec's one instance is
	// the skill that killed.
	for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
	{
		UCataclysmSkillTemplate* Skill =
			Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
		if (Skill && FName(*Skill->SkillName) == Name)
		{
			OutHandle = Spec.Handle;
			return Skill;
		}
	}
	return nullptr;
}

bool UCataclysmFollowThrough::NoteMeleeKill(AActor* Character, FName KillingSkillName)
{
	UCataclysmAbilitySystemComponent* System = SystemOf(Character);
	const float Every = EverySeconds(System);
	if (Every <= 0.0f || !System->MayFollowThrough())
	{
		return false;
	}

	FGameplayAbilitySpecHandle Handle;
	const UCataclysmSkillTemplate* Skill = SkillNamed(System, KillingSkillName, Handle);
	if (!Skill || Skill->Slot == ECataclysmAbilitySlot::Movement)
	{
		return false;
	}

	// IT WAITS NO LONGER THAN THE KEYSTONE'S OWN INTERVAL. A use that has not
	// ended by then is not one the repeat can be said to follow "immediately",
	// and the clock is left unspent.
	const UWorld* World = Character->GetWorld();
	System->NotePendingFollowThrough(Handle, World->GetTimeSeconds() + Every);
	return true;
}

bool UCataclysmFollowThrough::MakePendingRepeat(AActor* Character)
{
	UCataclysmAbilitySystemComponent* System = SystemOf(Character);
	if (!System || !System->PendingFollowThrough().IsValid())
	{
		return false;
	}

	const UWorld* World = Character->GetWorld();
	FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(System->PendingFollowThrough());
	UCataclysmSkillTemplate* Skill =
		Spec ? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance()) : nullptr;
	if (!World || !Skill || World->GetTimeSeconds() > System->PendingFollowThroughUntil())
	{
		System->ClearPendingFollowThrough();
		return false;
	}

	// NOT UNTIL THE KILLING USE HAS ENDED. The repeat is a second activation of
	// the same ability, which the engine refuses while the first is running.
	if (Spec->IsActive())
	{
		return false;
	}

	const FGameplayAbilitySpecHandle Handle = System->PendingFollowThrough();
	System->ClearPendingFollowThrough();

	// A CHARACTER THAT CANNOT ACT MAKES NO REPEAT, and it is not kept for later:
	// a repeat after a stun has ended would not be "immediately".
	if (UCataclysmSkillEffects::CannotAct(Character))
	{
		return false;
	}

	// THE NEAREST LIVING ENEMY WITHIN THE SKILL'S OWN REACH. The basic attack's
	// reach is the one its swing uses; every other skill's is its range, or its
	// radius when it states no range.
	const float ReachCm = Skill->Slot == ECataclysmAbilitySlot::BasicAttack
		? UCataclysmBasicAttack::ReachCmOf(System)
		: Skill->RequirementReachCm();
	const FVector From = Character->GetActorLocation();
	const AActor* Nearest = nullptr;
	float NearestSquared = TNumericLimits<float>::Max();
	for (const AActor* Enemy :
		 UCataclysmTargeting::FindEnemiesInSphere(World, Character, From, ReachCm))
	{
		const float Squared = FVector::DistSquared(From, Enemy->GetActorLocation());
		if (Squared < NearestSquared)
		{
			NearestSquared = Squared;
			Nearest = Enemy;
		}
	}
	if (!Nearest)
	{
		// NOTHING TO REPEAT AT, AND THE CLOCK IS NOT SPENT, ruled 2026-09-24:
		// the next melee kill may still repeat.
		return false;
	}

	// THE CLOCK IS SPENT BEFORE THE ACTIVATION, so a kill the repeat itself makes
	// cannot earn another within the interval. It is put back if the activation
	// is refused, since only a repeat that happens spends it.
	const float ClockBefore = System->NoteFollowedThrough(EverySeconds(System));
	Skill->bFreeRepeat = true;
	Skill->FreeRepeatAim = Nearest->GetActorLocation();
	if (!System->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/true))
	{
		Skill->bFreeRepeat = false;
		System->RestoreFollowThroughClock(ClockBefore);
		return false;
	}
	return true;
}
