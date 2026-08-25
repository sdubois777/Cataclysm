// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"

const TCHAR* UCataclysmFervour::FromDamageStat = TEXT("fervour_from_damage");
const TCHAR* UCataclysmFervour::FromCostStat = TEXT("fervour_from_cost");
const TCHAR* UCataclysmFervour::LostToHealingStat =
	TEXT("fervour_lost_to_healing");

FGameplayTag UCataclysmFervour::RegenerationTag()
{
	// ErrorIfNotFound IS FALSE FOR THE REASON UCataclysmDamageCalculation GIVES
	// FOR ITS OWN TAGS: a test may run before the tag table is loaded, and an
	// empty tag matches nothing, which is the right answer there.
	return FGameplayTag::RequestGameplayTag(TEXT("Keyword.Regeneration"),
											/*ErrorIfNotFound=*/false);
}

float UCataclysmFervour::FervourFor(float HealthChanged, float MaxHealth,
									float RatePerPercent)
{
	if (HealthChanged <= 0.0f || MaxHealth <= 0.0f || RatePerPercent <= 0.0f)
	{
		return 0.0f;
	}

	// THE SHARE OF THE CHARACTER'S WHOLE HEALTH, IN PERCENT, TIMES THE RATE.
	// A character with 500 health and one with 5000 fill the bar at the same
	// speed relative to how much of themselves they have spent, which is what
	// "1 per 1% of maximum health" means and is why the rate is not per point.
	return HealthChanged / MaxHealth * 100.0f * RatePerPercent;
}

float UCataclysmFervour::RateFor(const UAbilitySystemComponent* AbilitySystem,
								 FName Stat,
								 const FGameplayTagContainer& Context)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	// NO CLASS RESOURCE ATTRIBUTE SET MEANS NO FERVOUR. Only the player state
	// carries one; every enemy and every minion in the game answers here. That
	// is a guard rather than a fault, and it is why nothing below has to ask
	// whether the thing it is looking at is a player.
	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	if (!Resource)
	{
		return 0.0f;
	}

	const TMap<FString, FGameplayAttribute>& ByName = RateAttributes();
	const FGameplayAttribute* Attribute = ByName.Find(Stat.ToString());
	if (!Attribute)
	{
		return 0.0f;
	}

	const float Plain = AbilitySystem->GetNumericAttribute(*Attribute);

	// THE SAME PIPELINE THE STAT WAS WORKED OUT BY, RUN AGAIN WITH THE HIT'S
	// TAGS. Without this the Masochist's Shared Agony node -- which increases
	// Fervour gained from damage over time specifically -- would be dropped
	// before it reached anything, because the attribute holds the value worked
	// out with no tags in hand. Issue #943 is the same gap on every other stat.
	if (const UCataclysmAbilitySystemComponent* Cataclysm =
			Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		return Cataclysm->StatForSkill(Stat, Context, Plain);
	}

	return Plain;
}

float UCataclysmFervour::GainFromDamage(UAbilitySystemComponent* AbilitySystem,
										float HealthLost,
										const FGameplayTagContainer& HitTags)
{
	return Move(AbilitySystem, FName(FromDamageStat), HealthLost, HitTags,
				/*Sign=*/1.0f);
}

float UCataclysmFervour::GainFromHealthCost(
	UAbilitySystemComponent* AbilitySystem, float HealthSpent)
{
	// NO TAGS. A health cost is paid by the character rather than delivered by
	// a hit, so there is nothing for it to carry and no node scopes to it.
	return Move(AbilitySystem, FName(FromCostStat), HealthSpent,
				FGameplayTagContainer(), /*Sign=*/1.0f);
}

float UCataclysmFervour::RemoveForHealing(
	UAbilitySystemComponent* AbilitySystem, float HealthRestored,
	const FGameplayTagContainer& HealingTags)
{
	return Move(AbilitySystem, FName(LostToHealingStat), HealthRestored,
				HealingTags, /*Sign=*/-1.0f);
}

bool UCataclysmFervour::HasAGenerator(
	const UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem
		|| !AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>())
	{
		return false;
	}

	// THE ATTRIBUTES AND NOT `RateFor`, because this asks whether the character
	// has a generator at all rather than what one is worth to a particular hit.
	// A rate that only exists on damage over time still counts.
	for (const FGameplayAttribute& Attribute :
			UCataclysmClassResourceAttributeSet::GetRateAttributes())
	{
		if (AbilitySystem->GetNumericAttribute(Attribute) > 0.0f)
		{
			return true;
		}
	}
	return false;
}

const TMap<FString, FGameplayAttribute>& UCataclysmFervour::RateAttributes()
{
	// BUILT ONCE. A gameplay attribute holds a pointer to a reflected property,
	// so this cannot be a compile-time constant, and it is read on every hit.
	static const TMap<FString, FGameplayAttribute> Map = {
		{FromDamageStat,
		 UCataclysmClassResourceAttributeSet::GetFervourFromDamageAttribute()},
		{FromCostStat,
		 UCataclysmClassResourceAttributeSet::GetFervourFromCostAttribute()},
		{LostToHealingStat,
		 UCataclysmClassResourceAttributeSet::GetFervourLostToHealingAttribute()},
	};
	return Map;
}

float UCataclysmFervour::Move(UAbilitySystemComponent* AbilitySystem,
							  FName Stat, float HealthChanged,
							  const FGameplayTagContainer& Context, float Sign)
{
	if (!AbilitySystem || HealthChanged <= 0.0f)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmVitalAttributeSet* Vitals =
		AbilitySystem->GetSet<UCataclysmVitalAttributeSet>();
	if (!Resource || !Vitals)
	{
		return 0.0f;
	}

	const float Amount = FervourFor(
		HealthChanged, Vitals->GetMaxHealth(),
		RateFor(AbilitySystem, Stat, Context));
	if (Amount <= 0.0f)
	{
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED HERE RATHER THAN LEFT TO THE ATTRIBUTE SET, which is what
	// `UCataclysmRegeneration::TopUp` does with the health, mana and shield
	// pools and for the same reason. `ApplyModToAttribute` writes a base value,
	// and whether that reaches `PreAttributeChange` depends on whether an
	// aggregator exists for the attribute, which depends on whether any gameplay
	// effect happens to be modifying it. A rule that holds only sometimes is not
	// a rule, so the clamp is applied to the number before it is written.
	const float Wanted = FMath::Clamp(Before + Sign * Amount, 0.0f,
									  Resource->GetMaxClassResource());
	const float Change = Wanted - Before;
	if (FMath::IsNearlyZero(Change))
	{
		// ALREADY FULL, OR ALREADY EMPTY. Not a fault: a Masochist at maximum
		// Fervour taking another hit is the ordinary case, and it is what two of
		// the tree's keystones are about.
		return 0.0f;
	}

	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);

	// MEASURED RATHER THAN ASSUMED, so a caller reporting what it did says what
	// really happened rather than what it asked for.
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}
