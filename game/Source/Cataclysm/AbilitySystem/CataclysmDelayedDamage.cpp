// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDelayedDamage.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "Character/CataclysmCharacterBase.h"
#include "GameplayTagContainer.h"

const TCHAR* UCataclysmDelayedDamage::SharePercentStat =
	TEXT("damage_taken_delayed_percent");

const TCHAR* UCataclysmDelayedDamage::SecondsStat =
	TEXT("damage_taken_delayed_seconds");

float UCataclysmDelayedDamage::AmountDelayed(float ToHealth, float SharePercent, float Seconds)
{
	if (ToHealth <= 0.0f || SharePercent <= 0.0f || Seconds <= 0.0f)
	{
		return 0.0f;
	}

	// NEVER MORE THAN WHAT WOULD HAVE REACHED HEALTH. A share above a hundred would take more off the blow than
	// the blow held, and the difference would be health handed back.
	return ToHealth * FMath::Clamp(SharePercent, 0.0f, 100.0f) / 100.0f;
}

float UCataclysmDelayedDamage::ShareThatWouldBeDelayed(UAbilitySystemComponent* AbilitySystem, float ToHealth,
													   bool bIsDamageOverTime, float* OutSeconds)
{
	if (OutSeconds)
	{
		*OutSeconds = 0.0f;
	}

	// A TICK IS NOT DELAYED, ruled 2026-10-12. Damage already spread over seconds is not spread again.
	if (bIsDamageOverTime || ToHealth <= 0.0f)
	{
		return 0.0f;
	}

	// NO WORLD, NO PARCEL, AND SO NO SHARE: `HoldDelayedDamage` refuses a parcel it has no clock for, and this
	// answers what `DelayShare` would really take.
	UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm || !Cataclysm->GetWorld())
	{
		return 0.0f;
	}

	// THE SHARE FIRST, so a character with no such row is asked for one stat and no more. No tags and no blow:
	// the rule is about every blow its wearer takes, whatever struck it.
	const float SharePercent = Cataclysm->StatForSkill(FName(SharePercentStat), FGameplayTagContainer(), 0.0f);
	if (SharePercent <= 0.0f)
	{
		return 0.0f;
	}
	const float Seconds = Cataclysm->StatForSkill(FName(SecondsStat), FGameplayTagContainer(), 0.0f);
	if (OutSeconds)
	{
		*OutSeconds = Seconds;
	}
	return AmountDelayed(ToHealth, SharePercent, Seconds);
}

float UCataclysmDelayedDamage::DelayShare(UAbilitySystemComponent* AbilitySystem, float ToHealth,
										  bool bIsDamageOverTime)
{
	// THE FIGURE THE TWO SAVES FROM A LETHAL BLOW WERE GIVEN, FROM THE SAME FUNCTION, so what a save judged and
	// what is taken here cannot come apart.
	float Seconds = 0.0f;
	const float Amount = ShareThatWouldBeDelayed(AbilitySystem, ToHealth, bIsDamageOverTime, &Seconds);
	UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (Amount <= 0.0f || !Cataclysm || !Cataclysm->HoldDelayedDamage(Amount, Seconds))
	{
		return 0.0f;
	}
	return Amount;
}

float UCataclysmDelayedDamage::DrainStep(AActor* Character)
{
	// A CORPSE PAYS NOTHING, the guard `UCataclysmHealthDebt::DrainIfDue` opens with and for its reason.
	if (!Character || UCataclysmSkillEffects::IsDead(Character))
	{
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Character));
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Health))
	{
		return 0.0f;
	}

	// WHAT EVERY PARCEL OWES FOR THE TIME SINCE THE STEP BEFORE, added together. The component reads the world's
	// clock, forgets the parcels of a dungeon that is not the one walked now, and removes each parcel as its last
	// part is taken.
	const float Due = AbilitySystem->TakeDelayedDamageDue();
	if (Due <= 0.0f)
	{
		return 0.0f;
	}

	const float Current = AbilitySystem->GetNumericAttribute(Health);
	if (Due < Current)
	{
		// A DIRECT WRITE TO HEALTH, as the health debt's drain makes. It reaches
		// `UCataclysmVitalAttributeSet::PostAttributeBaseChange`, so a threshold this crosses is noticed.
		AbilitySystem->ApplyModToAttribute(Health, EGameplayModOp::Additive, -Due);
		return Due;
	}

	// AND THIS STEP TAKES THE LAST OF THE HEALTH. Ruled 2026-10-12: the drained loss can kill.
	UE_LOG(LogCataclysm, Log,
		   TEXT("%s owed %.1f of delayed damage this step with %.1f health left and died of it."),
		   *Character->GetName(), Due, Current);

	// THE STORE GOES WITH THE CHARACTER, AND BEFORE THE HEALTH IS WRITTEN: a death clears it, ruled, and a
	// player's death writes its save record inside the write below.
	AbilitySystem->ClearDelayedDamage();

	// HEALTH TO NOUGHT AND THEN `HandleDeath`, the pair `UCataclysmHealthDebt::DrainWhileDebtExceedsHealth` uses.
	// The write reaches the death through the attribute hook for a character that is its ability system's avatar,
	// and `HandleDeath` refuses a second time.
	AbilitySystem->SetNumericAttributeBase(Health, 0.0f);
	if (ACataclysmCharacterBase* AsCharacter = Cast<ACataclysmCharacterBase>(Character))
	{
		AsCharacter->HandleDeath();
	}

	// WHAT THE LAST STEP REALLY TOOK: the health that was there.
	return FMath::Max(0.0f, Current);
}
