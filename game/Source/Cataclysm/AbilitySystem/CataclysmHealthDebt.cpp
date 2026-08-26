// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmHealthDebt.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"

const TCHAR* UCataclysmHealthDebt::DeferredShareStat =
	TEXT("deferred_health_cost_share");

float UCataclysmHealthDebt::DeferredSharePercent(
	const UAbilitySystemComponent* AbilitySystem)
{
	using Resource = UCataclysmClassResourceAttributeSet;
	const FGameplayAttribute Share =
		Resource::GetDeferredHealthCostShareAttribute();

	// AN ABILITY SYSTEM WITHOUT THE SET DEFERS NOTHING. Every player carries the
	// class resource set; an enemy's ability system does not, and an enemy using
	// a skill goes through the same cost function.
	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Share))
	{
		return 0.0f;
	}

	// The attribute is already held between 0 and 100 by the set's
	// PreAttributeChange, so this guards only a value written before that ran.
	return FMath::Clamp(AbilitySystem->GetNumericAttribute(Share), 0.0f, 100.0f);
}

float UCataclysmHealthDebt::AmountDeferred(float Cost, float SharePercent)
{
	if (Cost <= 0.0f || SharePercent <= 0.0f)
	{
		return 0.0f;
	}

	// NEVER MORE THAN THE COST. A share above a hundred would otherwise defer
	// more than was charged, and the difference would be health handed back.
	return Cost * FMath::Clamp(SharePercent, 0.0f, 100.0f) / 100.0f;
}

void UCataclysmHealthDebt::Defer(UAbilitySystemComponent* AbilitySystem,
								 float Amount)
{
	if (!AbilitySystem || Amount <= 0.0f)
	{
		return;
	}

	const FGameplayAttribute Owed =
		UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Owed))
	{
		return;
	}

	// ADDED TO WHAT IS ALREADY OWED RATHER THAN REPLACING IT. A character can
	// cast twice before the first debt falls due, and the design treats what is
	// owed as one amount: the Rolling Debt node speaks of "the delay on what is
	// owed", singular.
	AbilitySystem->ApplyModToAttribute(Owed, EGameplayModOp::Additive, Amount);

	if (UCataclysmAbilitySystemComponent* Cataclysm =
			Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		Cataclysm->NoteHealthDebtDueIn(DelaySeconds);
	}
}

float UCataclysmHealthDebt::SettleIfDue(AActor* Character)
{
	// A CORPSE PAYS NOTHING, for the reason `UCataclysmRegeneration::ApplyStep`
	// skips one: an enemy is destroyed on the step AFTER it dies, so there is a
	// real window in which a dead creature is still standing there with an
	// ability system and a debt.
	if (!Character || UCataclysmSkillEffects::IsDead(Character))
	{
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	if (!AbilitySystem || !AbilitySystem->IsHealthDebtDue())
	{
		return 0.0f;
	}

	const FGameplayAttribute Owed =
		UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Owed))
	{
		return 0.0f;
	}

	const float Amount = AbilitySystem->GetNumericAttribute(Owed);
	if (Amount <= 0.0f)
	{
		// OWED NOTHING BUT MARKED DUE, which a debt settled some other way could
		// leave behind. Clearing the due time here stops it being asked again on
		// every step for the rest of the character's life.
		AbilitySystem->ClearHealthDebtDue();
		return 0.0f;
	}

	// THE WHOLE DEBT AT ONCE. The design says the cost "is taken 3 seconds
	// later", which is one payment rather than a trickle.
	//
	// NO FLOOR, AND THAT IS DELIBERATE. The floor that keeps a cost taken from
	// current health from emptying it (issue #986) applies when the cost is
	// worked out, not when a deferred part of it settles. By then it is a debt,
	// and the design is explicit that a debt may kill: The Reckoning reads "If
	// your debt ever exceeds your current health, you die." That keystone's own
	// lethal rule is not built here, but nothing should be added that would
	// contradict it later.
	AbilitySystem->ApplyModToAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute(),
		EGameplayModOp::Additive, -Amount);

	// THE DEBT IS CLEARED WHETHER OR NOT THE HEALTH WAS THERE. A character that
	// could not afford it has still been charged; whether being unable to afford
	// it kills them is issue #971's question and The Reckoning's sentence, and
	// neither is answered here.
	AbilitySystem->SetNumericAttributeBase(Owed, 0.0f);
	AbilitySystem->ClearHealthDebtDue();

	UE_LOG(LogCataclysm, Verbose,
		   TEXT("%s settled a health debt of %.1f."),
		   *Character->GetName(), Amount);

	return Amount;
}
