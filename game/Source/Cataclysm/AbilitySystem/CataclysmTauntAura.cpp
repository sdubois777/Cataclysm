// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmTauntAura.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTaunt.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"

const TCHAR* UCataclysmTauntAura::BelowHealthPercentStat = TEXT("taunt_aura_below_health_percent");
const TCHAR* UCataclysmTauntAura::EverySecondsStat = TEXT("taunt_aura_every_seconds");
const TCHAR* UCataclysmTauntAura::RadiusMetresStat = TEXT("taunt_aura_radius_metres");
const TCHAR* UCataclysmTauntAura::TauntSecondsStat = TEXT("taunt_aura_taunt_seconds");

namespace
{
	/**
	 * Every armed wearer's last beat in its world's seconds, keyed weakly. HELD HERE AND NOT ON THE ABILITY SYSTEM, so
	 * this bonus adds no member to that shared class, as `UCataclysmGroundPound` keeps its clocks. A gone wearer's
	 * entry is dropped when the next one is added.
	 */
	TMap<TWeakObjectPtr<UCataclysmAbilitySystemComponent>, float>& TauntAuraBeats()
	{
		static TMap<TWeakObjectPtr<UCataclysmAbilitySystemComponent>, float> Held;
		return Held;
	}
}

float UCataclysmTauntAura::StatOf(const UCataclysmAbilitySystemComponent* Wearer, const TCHAR* Stat)
{
	return Wearer ? Wearer->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f) : 0.0f;
}

int32 UCataclysmTauntAura::Step(AActor* Character)
{
	UCataclysmAbilitySystemComponent* Wearer =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Character));
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!Wearer || !World || UCataclysmSkillEffects::IsDead(Character))
	{
		return 0;
	}

	TMap<TWeakObjectPtr<UCataclysmAbilitySystemComponent>, float>& Beats = TauntAuraBeats();
	const TWeakObjectPtr<UCataclysmAbilitySystemComponent> Key(Wearer);

	// NOT ARMED, OR NO LONGER: the clock is forgotten. Any of the four at nought is a wearer without the bonus.
	const float Share = StatOf(Wearer, BelowHealthPercentStat);
	const float Every = StatOf(Wearer, EverySecondsStat);
	const float RadiusCm = StatOf(Wearer, RadiusMetresStat) * 100.0f;
	const float Seconds = StatOf(Wearer, TauntSecondsStat);
	if (Share <= 0.0f || Every <= 0.0f || RadiusCm <= 0.0f || Seconds <= 0.0f)
	{
		Beats.Remove(Key);
		return 0;
	}

	// BELOW THE SHARE, JUDGED AS THE ARMOUR ROW'S `health_below` IS (Q2): the pipeline's own predicate on the wearer's
	// own state, strictly below. Above it the last beat is kept, so the line crossed twice never beats early.
	if (!UCataclysmStatPipeline::ConditionHolds(ECataclysmStatCondition::HealthBelowPercent, Share,
												 Wearer->CurrentConditions()))
	{
		return 0;
	}

	// DUE: never beaten, or a whole interval since the last beat, less the allowance for a step that comes early.
	const float Now = World->GetTimeSeconds();
	if (const float* Last = Beats.Find(Key))
	{
		if (Now + BeatAllowanceSeconds < *Last + Every)
		{
			return 0;
		}
	}
	else
	{
		for (auto It = Beats.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}
	Beats.Add(Key, Now);

	// EVERY ENEMY WITHIN THE RADIUS, the search every nearby action makes, from the wearer (Q1), TAUNTED BY THE WEARER
	// (Q2). `ApplyTaunt` refuses a boss, an enemy another taunter holds and the rest, and notes crowd control on the
	// wearer for each that lands (Q3, Q4).
	int32 Landed = 0;
	for (AActor* Enemy :
		 UCataclysmTargeting::FindEnemiesInSphere(World, Character, Character->GetActorLocation(), RadiusCm))
	{
		Landed += UCataclysmTaunt::ApplyTaunt(Character, Enemy, Seconds) ? 1 : 0;
	}
	return Landed;
}
