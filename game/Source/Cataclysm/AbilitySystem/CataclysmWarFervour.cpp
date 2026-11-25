// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmWarFervour.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "AbilitySystemComponent.h"

const TCHAR* FCataclysmWarFervour::PerHitTakenStat = TEXT("fervour_per_hit_taken");
const TCHAR* FCataclysmWarFervour::PerBlockStat = TEXT("fervour_per_block");
const TCHAR* FCataclysmWarFervour::PerCriticalStrikeStat = TEXT("fervour_per_critical_strike");
const TCHAR* FCataclysmWarFervour::PerTrapPlacedStat = TEXT("fervour_per_trap_placed");
const TCHAR* FCataclysmWarFervour::PerGadgetPlacedStat = TEXT("fervour_per_gadget_placed");
const TCHAR* FCataclysmWarFervour::PerTrapTriggeredStat = TEXT("fervour_per_trap_triggered");
const TCHAR* FCataclysmWarFervour::PerDeviceHitStat = TEXT("fervour_per_device_hit");
const TCHAR* FCataclysmWarFervour::OutOfCombatDecayPerSecondStat = TEXT("fervour_decay_out_of_combat_per_second");

float FCataclysmWarFervour::GainByRate(UAbilitySystemComponent* AbilitySystem, const TCHAR* Stat,
									   const FGameplayTagContainer& Tags)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no pool to fill, which is every enemy.
		return 0.0f;
	}

	// THROUGH THE PIPELINE WITH THE EVENT'S TAGS, FALLBACK NOUGHT, as every rate in `UCataclysmFervour` is read: a row
	// that carries a condition or a scope is never folded into an attribute, so a plain read would miss it.
	const float Rate = Cataclysm->StatForSkill(FName(Stat), Tags, 0.0f);
	if (Rate <= 0.0f)
	{
		// EVERY CHARACTER WITHOUT THE STARTING NODE THAT GRANTS THIS RATE.
		return 0.0f;
	}

	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// SCALED BY HOW FAST THE CLASS RESOURCE GENERATES (ruled 2026-10-07), AND CLAMPED BEFORE IT IS WRITTEN, to the most
	// the pool may hold after its reserves: `UCataclysmFervour::GainForEnemiesHit`'s shape, line for line.
	const float Change =
		FMath::Clamp(Before + UCataclysmAbilitySystemComponent::ClassResourceGainScaled(AbilitySystem, Rate),
					 0.0f, Resource->MaximumClassResourceAsked())
		- Before;
	if (FMath::IsNearlyZero(Change))
	{
		return 0.0f;
	}

	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float FCataclysmWarFervour::GainOnHitTaken(UAbilitySystemComponent* AbilitySystem)
{
	return GainByRate(AbilitySystem, PerHitTakenStat, FGameplayTagContainer());
}

float FCataclysmWarFervour::GainOnBlock(UAbilitySystemComponent* AbilitySystem)
{
	return GainByRate(AbilitySystem, PerBlockStat, FGameplayTagContainer());
}

float FCataclysmWarFervour::GainOnCriticalStrike(UAbilitySystemComponent* AbilitySystem,
												 const FGameplayTagContainer& SkillTags)
{
	return GainByRate(AbilitySystem, PerCriticalStrikeStat, SkillTags);
}

float FCataclysmWarFervour::GainOnTrapPlaced(UAbilitySystemComponent* AbilitySystem,
											 const FGameplayTagContainer& TrapTags)
{
	return GainByRate(AbilitySystem, PerTrapPlacedStat, TrapTags);
}

float FCataclysmWarFervour::GainOnGadgetPlaced(UAbilitySystemComponent* AbilitySystem,
											   const FGameplayTagContainer& GadgetTags)
{
	return GainByRate(AbilitySystem, PerGadgetPlacedStat, GadgetTags);
}

float FCataclysmWarFervour::GainOnTrapTriggered(UAbilitySystemComponent* AbilitySystem,
												const FGameplayTagContainer& TrapTags)
{
	return GainByRate(AbilitySystem, PerTrapTriggeredStat, TrapTags);
}

float FCataclysmWarFervour::GainOnDeviceHit(UAbilitySystemComponent* AbilitySystem,
											const FGameplayTagContainer& DeviceTags)
{
	return GainByRate(AbilitySystem, PerDeviceHitStat, DeviceTags);
}

float FCataclysmWarFervour::OutOfCombatDecayStep(ACataclysmCharacterBase* Character, float SecondsInStep)
{
	if (!Character || SecondsInStep <= 0.0f)
	{
		return 0.0f;
	}

	// NOR A CREATURE HOLDING A PLAYER BUILD, the Corrupted Stalker, for the reason `UCataclysmFervour::DecayStep` gives
	// (issue #1308): it is raised with its Ultimate's Fervour, and a War build's starting node would drain it on its way
	// to the player.
	if (const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Character);
		Enemy && Enemy->bHoldsAPlayerBuild)
	{
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Character));
	if (!Cataclysm)
	{
		return 0.0f;
	}
	const UCataclysmClassResourceAttributeSet* Resource = Cataclysm->GetSet<UCataclysmClassResourceAttributeSet>();
	if (!Resource)
	{
		return 0.0f;
	}

	// THE RATE FIRST, the cheapest question and the one that refuses every character without the Bulwark's or the
	// Berserker's starting node.
	const float PerSecond = Cataclysm->StatForSkill(FName(OutOfCombatDecayPerSecondStat), FGameplayTagContainer(), 0.0f);
	if (PerSecond <= 0.0f)
	{
		return 0.0f;
	}

	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = Cataclysm->GetNumericAttribute(Pool);
	if (Before <= 0.0f)
	{
		return 0.0f;
	}

	// OUT OF COMBAT: AT OR ABOVE NOUGHT, which `SecondsOutOfCombat` answers only once `CombatLapseSeconds` (3) have
	// passed since the last blow dealt or taken. "After 3 seconds out of combat" is read as that moment, 3 seconds after
	// the last blow, and not 3 seconds after the lapse (6 after the blow). A judgement: the clock's own comment counts the
	// lapse as the 3 seconds.
	if (Cataclysm->SecondsOutOfCombat() < 0.0f)
	{
		return 0.0f;
	}

	// (THE RUNNING-BUFF HOLD BELONGS HERE once `UCataclysmSkillTemplate::ARunningBuffHoldsFervour` is on the same
	// branch; see the header.)

	// CLAMPED BEFORE IT IS WRITTEN, as `UCataclysmFervour::DecayStep` is, and not scaled by `class_resource_generation`:
	// a decay is not a gain.
	const float Change =
		FMath::Clamp(Before - PerSecond * SecondsInStep, 0.0f, Resource->MaximumClassResourceAsked()) - Before;
	if (FMath::IsNearlyZero(Change))
	{
		return 0.0f;
	}

	Cataclysm->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	return Cataclysm->GetNumericAttribute(Pool) - Before;
}
