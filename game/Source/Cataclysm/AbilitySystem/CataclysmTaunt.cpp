// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmTaunt.h"

#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	/** One running taunt: who taunted, and when it ends in its world's seconds. */
	struct FCataclysmTauntHeld
	{
		TWeakObjectPtr<AActor> Taunter;
		float EndsAtSeconds = 0.0f;
	};

	/**
	 * Every running taunt, keyed weakly by the creature taunted. A gone creature's entry is dropped when the next taunt
	 * is placed; an ended one when it is next asked about.
	 */
	TMap<TWeakObjectPtr<AActor>, FCataclysmTauntHeld>& TauntsHeld()
	{
		static TMap<TWeakObjectPtr<AActor>, FCataclysmTauntHeld> Held;
		return Held;
	}

	float TauntWorldSecondsOf(const AActor* Actor)
	{
		const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
		return World ? World->GetTimeSeconds() : 0.0f;
	}

	/**
	 * The taunt running on `Target`, or null. ONE THAT HAS ENDED IS FORGOTTEN HERE: its seconds have passed, its
	 * taunter is dead or gone (Q1), or the creature itself is dead.
	 */
	FCataclysmTauntHeld* RunningTauntOn(const AActor* Target)
	{
		if (!IsValid(Target))
		{
			return nullptr;
		}
		const TWeakObjectPtr<AActor> Key(const_cast<AActor*>(Target));
		FCataclysmTauntHeld* Held = TauntsHeld().Find(Key);
		if (!Held)
		{
			return nullptr;
		}
		const AActor* Taunter = Held->Taunter.Get();
		if (!IsValid(Taunter) || UCataclysmSkillEffects::IsDead(Taunter) || UCataclysmSkillEffects::IsDead(Target)
			|| TauntWorldSecondsOf(Target) >= Held->EndsAtSeconds)
		{
			TauntsHeld().Remove(Key);
			return nullptr;
		}
		return Held;
	}
}

bool UCataclysmTaunt::ApplyTaunt(AActor* Taunter, AActor* Target, float Seconds)
{
	if (!Cast<ACataclysmCharacterBase>(Target) || !IsValid(Taunter) || Seconds <= 0.0f
		|| UCataclysmSkillEffects::IsDead(Taunter) || UCataclysmSkillEffects::IsDead(Target)
		|| !UCataclysmTargeting::IsHostileTo(Target, Taunter))
	{
		return false;
	}

	// BOSSES ARE IMMUNE, ruled 2026-10-17 (#6): the Boss and Cataclysm Boss rungs, the test `ApplyStun` makes.
	if (const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Target))
	{
		if (Enemy->IsBoss())
		{
			return false;
		}
	}

	// ANOTHER TAUNTER IS REFUSED UNTIL THE RUNNING TAUNT ENDS, Q2. The same taunter goes on below and restarts it.
	if (const FCataclysmTauntHeld* Running = RunningTauntOn(Target))
	{
		if (Running->Taunter.Get() != Taunter)
		{
			return false;
		}
	}

	// THE HEALTH CEILING OF WHOEVER THE TAUNTER FIGHTS FOR, as a stun's (#6: "crowd control (the health ceiling and
	// resistance)"). A Warhound is its summoner's set bonus and carries no rows of its own, so the summoner's ceiling
	// is the one asked; a taunter that follows nobody is asked its own.
	AActor* Applier = UCataclysmCommand::CommanderOf(Taunter);
	if (UCataclysmSkillEffects::CrowdControlRefusedByHealthCeiling(Applier ? Applier : Taunter, Target))
	{
		return false;
	}

	// A SKILL THE TARGET IS RUNNING MAY REFUSE IT, as it may a stun: `Immune=Taunt`, or `Immune=CrowdControl`.
	if (UCataclysmSkillTemplate::IsImmuneTo(Target, TEXT("Taunt")))
	{
		return false;
	}

	// CROWD-CONTROL RESISTANCE SHORTENS IT, as it does a stun, and at 100% or more nothing is left (#6).
	Seconds = UCataclysmSkillEffects::HeldSecondsAfterCrowdControlResistance(Target, Seconds);
	if (Seconds <= 0.0f)
	{
		return false;
	}

	// GONE CREATURES' ENTRIES ARE DROPPED BEFORE ONE IS ADDED, so the register holds only the living.
	for (auto It = TauntsHeld().CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	FCataclysmTauntHeld& Held = TauntsHeld().FindOrAdd(TWeakObjectPtr<AActor>(Target));
	Held.Taunter = Taunter;
	Held.EndsAtSeconds = TauntWorldSecondsOf(Target) + Seconds;
	return true;
}

AActor* UCataclysmTaunt::TaunterOf(const AActor* Target)
{
	const FCataclysmTauntHeld* Held = RunningTauntOn(Target);
	return Held ? Held->Taunter.Get() : nullptr;
}

float UCataclysmTaunt::SecondsLeftOn(const AActor* Target)
{
	const FCataclysmTauntHeld* Held = RunningTauntOn(Target);
	return Held ? FMath::Max(0.0f, Held->EndsAtSeconds - TauntWorldSecondsOf(Target)) : 0.0f;
}
