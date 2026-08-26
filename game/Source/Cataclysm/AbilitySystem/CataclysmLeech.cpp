// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmLeech.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For the tag that says a health top-up came from leech. Issue #1006.
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"

float UCataclysmLeech::AmountFrom(float DamageTaken, float LeechPercent)
{
	if (DamageTaken <= 0.0f || LeechPercent <= 0.0f)
	{
		return 0.0f;
	}

	return DamageTaken * LeechPercent / 100.0f;
}

float UCataclysmLeech::PaidInStep(const FCataclysmLeechPayment& Payment,
								  float SecondsInStep)
{
	if (Payment.Remaining <= 0.0f || Payment.SecondsLeft <= 0.0f
		|| SecondsInStep <= 0.0f)
	{
		return 0.0f;
	}

	// THE LAST STEP PAYS THE WHOLE BALANCE. Without this a payment would be
	// multiplied by a fraction for ever and never reach zero, and a character
	// would carry a growing list of payments each owing a millionth of a point.
	if (SecondsInStep >= Payment.SecondsLeft)
	{
		return Payment.Remaining;
	}

	return Payment.Remaining * SecondsInStep / Payment.SecondsLeft;
}

void UCataclysmLeech::NoteHit(UAbilitySystemComponent* Attacker,
							  float DamageTaken)
{
	if (DamageTaken <= 0.0f)
	{
		return;
	}

	UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<UCataclysmAbilitySystemComponent>(Attacker);
	if (!Cataclysm)
	{
		// An ability system this project did not make carries no payment list.
		// An enemy's plain melee attack goes through here too, and an enemy has
		// no leech, so this is the ordinary case rather than a fault.
		return;
	}

	const UCataclysmVitalAttributeSet* Vitals =
		Cataclysm->GetSet<UCataclysmVitalAttributeSet>();
	if (!Vitals)
	{
		return;
	}

	struct FSource
	{
		ECataclysmLeechPool Pool;
		float Percent;
	};

	const FSource Sources[] = {
		{ECataclysmLeechPool::Health, Vitals->GetLifeLeech()},
		{ECataclysmLeechPool::Mana, Vitals->GetManaLeech()},
		{ECataclysmLeechPool::EnergyShield, Vitals->GetEnergyShieldLeech()},
	};

	for (const FSource& Source : Sources)
	{
		const float Amount = AmountFrom(DamageTaken, Source.Percent);
		if (Amount <= 0.0f)
		{
			continue;
		}

		FCataclysmLeechPayment Payment;
		Payment.Pool = Source.Pool;
		Payment.Remaining = Amount;
		Payment.SecondsLeft = PayoutSeconds;
		Cataclysm->AddLeechPayment(Payment);
	}
}

void UCataclysmLeech::PayOutStep(AActor* Character, float SecondsInStep)
{
	if (SecondsInStep <= 0.0f)
	{
		return;
	}

	// NOTHING FOR THE DEAD, for the same reason regeneration refuses one:
	// ACataclysmEnemyCharacter::HandleDeath runs inside the gameplay effect
	// callback that dealt the killing blow, so there is a real window in which
	// a dead creature is still standing there with an ability system.
	if (UCataclysmSkillEffects::IsDead(Character))
	{
		return;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	if (!AbilitySystem
		|| !AbilitySystem->GetSet<UCataclysmVitalAttributeSet>()
		|| AbilitySystem->GetLeechPayments().IsEmpty())
	{
		return;
	}

	TArray<FCataclysmLeechPayment> StillOwing;
	StillOwing.Reserve(AbilitySystem->GetLeechPayments().Num());

	for (const FCataclysmLeechPayment& Payment : AbilitySystem->GetLeechPayments())
	{
		const float Paid = PaidInStep(Payment, SecondsInStep);
		if (Paid > 0.0f)
		{
			// THE SAME TOP-UP REGENERATION USES, shared rather than copied. A
			// second private copy of it would compile in a unity build and
			// collide the moment both files were clean, which is the fault a
			// duplicate helper caused once already.
			switch (Payment.Pool)
			{
			case ECataclysmLeechPool::Health:
			{
				// AND THE HEALTH TOP-UP SAYS IT IS LEECH. Issue #1006. It used
				// to carry no tag at all, which was enough while the only node
				// asking about the source of healing was Staunch: that one
				// requires the regeneration tag, so leech carrying nothing meant
				// the node did not apply. Wounds That Feed asks the other way
				// round -- "healing from Life Leech does not remove Fervour" --
				// and carrying nothing cannot answer that, because a future
				// healing source would carry nothing too and be caught by it.
				//
				// HEALTH ONLY, LIKE THE FERVOUR RULE IT FEEDS. Mana and the
				// energy shield have nothing to do with Fervour, and the two
				// cases below deliberately still carry nothing.
				FGameplayTagContainer Leech;
				Leech.AddTag(UCataclysmFervour::LeechTag());

				UCataclysmRegeneration::TopUp(
					*AbilitySystem,
					UCataclysmVitalAttributeSet::GetHealthAttribute(),
					UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Paid,
					Leech);
				break;
			}
			case ECataclysmLeechPool::Mana:
				UCataclysmRegeneration::TopUp(
					*AbilitySystem,
					UCataclysmVitalAttributeSet::GetManaAttribute(),
					UCataclysmVitalAttributeSet::GetMaxManaAttribute(), Paid);
				break;
			case ECataclysmLeechPool::EnergyShield:
				UCataclysmRegeneration::TopUp(
					*AbilitySystem,
					UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(),
					UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(),
					Paid);
				break;
			}
		}

		// KEPT ONLY WHILE SOMETHING IS STILL OWED. A payment whose time is up,
		// or whose balance has been paid, is dropped rather than left in the
		// list, so a character that stops fighting stops carrying anything.
		//
		// THE ENERGY SHIELD'S REFILL WAIT IS NOT CHECKED HERE, and that is
		// deliberate. Regeneration waits three seconds after damage before a
		// shield refills; leech is not regeneration, and a rule that stopped
		// leech while a character was being hit would stop it exactly when it
		// is meant to work.
		FCataclysmLeechPayment After = Payment;
		After.Remaining -= Paid;
		After.SecondsLeft -= SecondsInStep;
		if (After.Remaining > KINDA_SMALL_NUMBER && After.SecondsLeft > 0.0f)
		{
			StillOwing.Add(After);
		}
	}

	AbilitySystem->SetLeechPayments(MoveTemp(StillOwing));
}
