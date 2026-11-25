// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmWarFervour.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
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
const TCHAR* FCataclysmWarFervour::PerHitDealtStat = TEXT("fervour_per_hit_dealt");
const TCHAR* FCataclysmWarFervour::CostPerNegatedBlockStat = TEXT("fervour_cost_per_negated_block");
const TCHAR* FCataclysmWarFervour::LethalBlowHealthPerTenFervourStat = TEXT("health_percent_per_ten_fervour_on_lethal_blow");
const TCHAR* FCataclysmWarFervour::CostPerExtraEnemyHitStat = TEXT("fervour_cost_per_extra_enemy_hit");
const TCHAR* FCataclysmWarFervour::GainPerExtraEnemyHitPaidStat = TEXT("fervour_per_extra_enemy_hit_paid");
const TCHAR* FCataclysmWarFervour::LostToRegenerationStat = TEXT("fervour_lost_to_regeneration");
const TCHAR* FCataclysmWarFervour::RegenerationPerLossStat = TEXT("regeneration_per_fervour_loss");
const TCHAR* FCataclysmWarFervour::OutOfCombatDecayPerSecondStat = TEXT("fervour_decay_out_of_combat_per_second");

const TArray<const TCHAR*>& FCataclysmWarFervour::GainStats()
{
	static const TArray<const TCHAR*> Stats = {PerHitTakenStat,		 PerBlockStat,		  PerCriticalStrikeStat,
											   PerTrapPlacedStat,	 PerGadgetPlacedStat, PerTrapTriggeredStat,
											   PerDeviceHitStat,	 PerHitDealtStat};
	return Stats;
}

float FCataclysmWarFervour::TakeFromPool(UAbilitySystemComponent* AbilitySystem, float Amount)
{
	if (!AbilitySystem || Amount <= 0.0f || !AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>())
	{
		return 0.0f;
	}
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);
	// AT MOST WHAT THE POOL HOLDS: a spend here is never refused for being short (unlike an Ultimate's cost); it takes
	// what there is, as each sentence's "draining" does.
	const float Taken = FMath::Min(Amount, FMath::Max(0.0f, Before));
	if (Taken <= 0.0f)
	{
		return 0.0f;
	}
	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, -Taken);
	return Before - AbilitySystem->GetNumericAttribute(Pool);
}

float FCataclysmWarFervour::SpendOnNegatedBlock(UAbilitySystemComponent* AbilitySystem)
{
	const UCataclysmAbilitySystemComponent* Cataclysm = Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm)
	{
		return 0.0f;
	}
	// THROUGH THE PIPELINE WITH NO TAGS, so the 75 Fervour condition is judged on the pool before the cost comes out of
	// it: the same pool that let the block negate.
	return TakeFromPool(AbilitySystem,
						Cataclysm->StatForSkill(FName(CostPerNegatedBlockStat), FGameplayTagContainer(), 0.0f));
}

float FCataclysmWarFervour::SpendAllOnLethalBlow(UAbilitySystemComponent* AbilitySystem)
{
	const UCataclysmAbilitySystemComponent* Cataclysm = Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm || !AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>())
	{
		return -1.0f;
	}
	// THE ROW IS STATED UNDER "AT LEAST 50 FERVOUR", so a character holding less reads nought and is not saved.
	const float PercentPerTen =
		Cataclysm->StatForSkill(FName(LethalBlowHealthPerTenFervourStat), FGameplayTagContainer(), 0.0f);
	if (PercentPerTen <= 0.0f)
	{
		return -1.0f;
	}
	const float Spent = TakeFromPool(
		AbilitySystem,
		AbilitySystem->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute()));
	const float MaximumHealth =
		FMath::Max(0.0f, AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()));
	// WHOLE STEPS OF 10, ROUNDED DOWN: 73 Fervour is seven steps.
	return FMath::FloorToFloat(Spent / 10.0f) * PercentPerTen / 100.0f * MaximumHealth;
}

float FCataclysmWarFervour::TradeForEnemiesStruckTogether(UAbilitySystemComponent* AbilitySystem,
														  const FGameplayTagContainer& SkillTags, int32 EnemiesStruck)
{
	const UCataclysmAbilitySystemComponent* Cataclysm = Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem ? AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>() : nullptr;
	if (!Cataclysm || !Resource || EnemiesStruck <= 1)
	{
		return 0.0f;
	}
	const float Cost = Cataclysm->StatForSkill(FName(CostPerExtraEnemyHitStat), SkillTags, 0.0f);
	if (Cost <= 0.0f)
	{
		return 0.0f;
	}
	const float Gain = UCataclysmAbilitySystemComponent::ClassResourceGainScaled(
		AbilitySystem, FMath::Max(0.0f, Cataclysm->StatForSkill(FName(GainPerExtraEnemyHitPaidStat), SkillTags, 0.0f)));

	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);
	const float Most = Resource->MaximumClassResourceAsked();
	// EACH ENEMY IN TURN, PAID THEN GAINED: "Each enemy hit beyond the first costs 5 Fervour and generates 3 Fervour per
	// point if Fervour was available to pay the cost." An enemy the pool cannot pay for gains nothing, and nor can one
	// after it, since the pool has not moved.
	float Held = Before;
	for (int32 Extra = 1; Extra < EnemiesStruck; ++Extra)
	{
		if (Held < Cost)
		{
			break;
		}
		Held = FMath::Min(Held - Cost + Gain, Most);
	}
	const float Change = Held - Before;
	if (FMath::IsNearlyZero(Change))
	{
		return 0.0f;
	}
	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float FCataclysmWarFervour::LoseForRegeneration(UAbilitySystemComponent* AbilitySystem, float Restored,
												const FGameplayTagContainer& Healing)
{
	UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	// REGENERATION ONLY, by the tag `UCataclysmRegeneration`'s step puts on it: a potion or a leech is not "HP
	// regenerated".
	if (!Cataclysm || Restored <= 0.0f || !Healing.HasTag(UCataclysmFervour::RegenerationTag()))
	{
		return 0.0f;
	}
	const float Loss = Cataclysm->StatForSkill(FName(LostToRegenerationStat), FGameplayTagContainer(), 0.0f);
	const float Step = Cataclysm->StatForSkill(FName(RegenerationPerLossStat), FGameplayTagContainer(), 0.0f);
	if (Loss <= 0.0f || Step <= 0.0f)
	{
		return 0.0f;
	}
	const int32 Steps = Cataclysm->CountRegenerationSteps(Restored, Step);
	return Steps > 0 ? TakeFromPool(AbilitySystem, Loss * static_cast<float>(Steps)) : 0.0f;
}

bool FCataclysmWarFervour::HasARate(const UAbilitySystemComponent* AbilitySystem)
{
	const UCataclysmAbilitySystemComponent* Cataclysm = Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm)
	{
		return false;
	}
	// ANY ROW AT ALL, for the reason `UCataclysmFervour::HasAGenerator` gives for its attributes: a rate that applies only
	// to melee blows, or only in Frenzy, still makes this a character that can fill its pool.
	for (const TCHAR* Stat : GainStats())
	{
		if (const FCataclysmStatInputs* Inputs = Cataclysm->GetStatInputs(FName(Stat)))
		{
			for (const FCataclysmStatModifier& Modifier : Inputs->Modifiers)
			{
				if (Modifier.Value > 0.0f)
				{
					return true;
				}
			}
		}
	}
	return false;
}

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

float FCataclysmWarFervour::GainOnHitDealt(UAbilitySystemComponent* AbilitySystem,
										   const FGameplayTagContainer& SkillTags)
{
	return GainByRate(AbilitySystem, PerHitDealtStat, SkillTags);
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
