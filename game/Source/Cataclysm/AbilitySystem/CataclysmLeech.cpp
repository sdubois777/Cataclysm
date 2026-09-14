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

	// ALL THREE ARE ASKED FOR RATHER THAN READ. Issue #947. The attribute is
	// worked out with every condition refused, so two authored enchantments
	// were dropped in silence: "While below 50% HP your leech is doubled" and
	// "While moving, your leech is increased by 20%-40%".
	//
	// AN EMPTY TAG CONTAINER, BECAUSE BOTH ROWS CARRY A CONDITION AND NOT A TAG,
	// and `StatForSkill` evaluates the character's own state whatever the tags
	// are. Scoping leech to a skill is a different problem and a harder one:
	// `NoteHit` is handed a damage figure and does not know which skill caused
	// the hit. Issue #947 names that as the one real obstacle in its list, and
	// nothing here removes it.
	//
	// TWO OF THE THREE UNBLOCK NO ROW AND ARE CHANGED FOR CONSISTENCY, which is
	// the same judgement the coordinating session ruled for the three damage
	// over time stats: three stats read the same way in one array, and leaving
	// two of them reading the attribute would leave the next reader to work out
	// whether the difference was deliberate.
	//
	// NO NULL CHECK ON `Cataclysm` HERE, BECAUSE THERE CANNOT BE ONE. The
	// function returned above if the cast failed. A ternary here would read as
	// though the pointer might be null and would be dead code saying so.
	const auto Asked = [Cataclysm](const TCHAR* Stat, float FromAttribute)
	{
		return Cataclysm->StatForSkill(FName(Stat), FGameplayTagContainer(),
									   FromAttribute);
	};

	const FSource Sources[] = {
		{ECataclysmLeechPool::Health,
		 Asked(TEXT("life_leech"), Vitals->GetLifeLeech())},
		{ECataclysmLeechPool::Mana,
		 Asked(TEXT("mana_leech"), Vitals->GetManaLeech())},
		{ECataclysmLeechPool::EnergyShield,
		 Asked(TEXT("energy_shield_leech"), Vitals->GetEnergyShieldLeech())},
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

void UCataclysmLeech::NoteRetaliation(UAbilitySystemComponent* Retaliator,
									  float DamageDealt)
{
	if (DamageDealt <= 0.0f)
	{
		return;
	}

	UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<UCataclysmAbilitySystemComponent>(Retaliator);
	if (!Cataclysm)
	{
		return;
	}

	const UCataclysmVitalAttributeSet* Vitals =
		Cataclysm->GetSet<UCataclysmVitalAttributeSet>();
	if (!Vitals)
	{
		return;
	}

	// THE SAME FIGURE `NoteHit` READS, AND THE SAME ARITHMETIC. The node says
	// leech "applies to your retaliation damage AS WELL AS to your attacks", so
	// the two paths have to agree on what the character's life leech is worth.
	// Reading it any other way here would make the sentence false.
	//
	// FLOORED AT ZERO for the reason `NoteHit` floors it: nothing states
	// negative life leech, and a negative figure would take health away.
	//
	// AND ASKED FOR RATHER THAN READ, FOR THE SENTENCE ABOVE TO STAY TRUE.
	// Issue #947. `NoteHit` now asks the pipeline for this stat, so a character
	// carrying "While below 50% HP your leech is doubled" would otherwise leech
	// the doubled figure from its attacks and the plain one from its
	// retaliation. That is a behavioural disagreement, not only a comment going
	// stale, and it is exactly what "the two paths have to agree" forbids.
	const float Amount = AmountFrom(
		DamageDealt,
		FMath::Max(0.0f, Cataclysm->StatForSkill(
							 FName(TEXT("life_leech")), FGameplayTagContainer(),
							 Vitals->GetLifeLeech())));
	if (Amount <= 0.0f)
	{
		return;
	}

	// HEALTH ONLY. See the header: the node names life leech and nothing else.
	FCataclysmLeechPayment Payment;
	Payment.Pool = ECataclysmLeechPool::Health;
	Payment.Remaining = Amount;
	Payment.SecondsLeft = PayoutSeconds;
	Cataclysm->AddLeechPayment(Payment);
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
