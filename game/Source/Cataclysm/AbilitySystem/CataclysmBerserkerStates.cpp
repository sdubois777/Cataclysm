// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmBerserkerStates.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmWarFervour.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"

const TCHAR* FCataclysmBerserkerStates::WrathOnFullFervourStat = TEXT("wrath_on_full_fervour");
const TCHAR* FCataclysmBerserkerStates::FrenzyOnFullFervourStat = TEXT("frenzy_on_full_fervour");
const TCHAR* FCataclysmBerserkerStates::BerserkingDamagePerFervourStat = TEXT("berserking_damage_per_fervour");
const TCHAR* FCataclysmBerserkerStates::DrainPerSecondStat = TEXT("fervour_drain_per_second");
const TCHAR* FCataclysmBerserkerStates::WrathText = TEXT("Wrath");
const TCHAR* FCataclysmBerserkerStates::FrenzyText = TEXT("Frenzy");
const TCHAR* FCataclysmBerserkerStates::BerserkingReadyText = TEXT("Berserking ready");

bool FCataclysmBerserkerStates::Judge(UCataclysmAbilitySystemComponent* Cataclysm)
{
	if (!Cataclysm)
	{
		return false;
	}
	const UCataclysmClassResourceAttributeSet* Resource = Cataclysm->GetSet<UCataclysmClassResourceAttributeSet>();
	if (!Resource)
	{
		// No class resource set means no pool to reach a maximum, which is every enemy.
		return false;
	}

	const ECataclysmBerserkerState Was = Cataclysm->BerserkerState();
	const bool bWasArmed = Cataclysm->IsBerserkingArmed();
	const float Held = Cataclysm->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
	const float Maximum = Resource->MaximumClassResourceAsked();
	const bool bFull = Maximum > 0.0f && Held >= Maximum;

	// NOTHING TO DECIDE for a character in no state, not armed, and short of a full pool: nothing can end, and nothing
	// can begin. Most calls stop here, so the three rows below are asked only when an answer could change.
	if (Was == ECataclysmBerserkerState::None && !bWasArmed && !bFull)
	{
		return false;
	}

	// EACH FLAG THROUGH THE PIPELINE WITH NO TAGS, FALLBACK NOUGHT, so the weapon rule its row is stated under decides
	// it. Berserking's row is scoped to melee, so it is asked with the melee tag: the question is whether the character
	// holds it at all.
	const FGameplayTagContainer NoTags;
	const bool bWrathAllowed = Cataclysm->StatForSkill(FName(WrathOnFullFervourStat), NoTags, 0.0f) > 0.0f;
	const bool bFrenzyAllowed = Cataclysm->StatForSkill(FName(FrenzyOnFullFervourStat), NoTags, 0.0f) > 0.0f;
	const bool bBerserkingHeld =
		Cataclysm->StatForSkill(FName(BerserkingDamagePerFervourStat),
								FGameplayTagContainer(UCataclysmDamageCalculation::MeleeTag()), 0.0f) > 0.0f;

	// A STATE ENDS AT AN EMPTY POOL, as each sentence says ("Wrath ends when Fervour reaches 0"), AND WHEN ITS FLAG NO
	// LONGER HOLDS: a two-handed character that takes up two weapons is no longer one Wrath is for. A judgement.
	ECataclysmBerserkerState Now = Was;
	if ((Now == ECataclysmBerserkerState::Wrath && (Held <= 0.0f || !bWrathAllowed))
		|| (Now == ECataclysmBerserkerState::Frenzy && (Held <= 0.0f || !bFrenzyAllowed)))
	{
		Now = ECataclysmBerserkerState::None;
	}

	// AT A FULL POOL WITH NO STATE, ONE BEGINS, OR BERSERKING ARMS. "When you reach maximum Fervour" is read as being at
	// the maximum when asked: the pool's own change notice asks the moment it fills, and the regeneration step asks four
	// times a second after that, which catches a pool the notice judged full against a different maximum.
	bool bArmed = bWasArmed;
	if (Now == ECataclysmBerserkerState::None && bFull)
	{
		if (bWrathAllowed)
		{
			Now = ECataclysmBerserkerState::Wrath;
		}
		else if (bFrenzyAllowed)
		{
			Now = ECataclysmBerserkerState::Frenzy;
		}
		else if (bBerserkingHeld)
		{
			bArmed = true;
		}
	}

	// BERSERKING FIRES ONLY OUTSIDE WRATH AND FRENZY (Q21), and not for a character that no longer holds it.
	if (Now != ECataclysmBerserkerState::None || !bBerserkingHeld)
	{
		bArmed = false;
	}

	if (Now != Was)
	{
		Cataclysm->SetBerserkerState(Now);

		// AND ENTERING FRENZY IS AN EVENT, raised after the state is set so a row acting on it reads the character in
		// Frenzy. The War vow options' second slice, 2026-11-25: Opportunist's "Entering Frenzy immediately resets the
		// cooldown of your movement skill" is a row on it, ruled that day over a row on `resource_full` while in Frenzy,
		// which would also fire on every refill during one. Once per entry: a Frenzy that goes on raises nothing more,
		// and Wrath raises nothing, since no row asks. A pool filled by a row's own action enters Frenzy without it,
		// because `ActOnEvent` acts at depth one; `resource_full` is dropped there the same way.
		if (Now == ECataclysmBerserkerState::Frenzy)
		{
			Cataclysm->ActOnEvent(FName(TEXT("frenzy_entered")));
		}
	}
	if (bArmed != Cataclysm->IsBerserkingArmed())
	{
		Cataclysm->SetBerserkingArmed(bArmed);
	}
	return Now != Was || bArmed != bWasArmed;
}

float FCataclysmBerserkerStates::DrainStep(ACataclysmCharacterBase* Character, float SecondsInStep)
{
	if (!Character || SecondsInStep <= 0.0f)
	{
		return 0.0f;
	}

	// NOR A CREATURE HOLDING A PLAYER BUILD, the Corrupted Stalker, for the reason `UCataclysmFervour::DecayStep` gives
	// (issue #1308).
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

	float Moved = 0.0f;
	const float PerSecond = Cataclysm->StatForSkill(FName(DrainPerSecondStat), FGameplayTagContainer(), 0.0f);
	if (PerSecond > 0.0f)
	{
		const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
		const float Before = Cataclysm->GetNumericAttribute(Pool);
		if (Before > 0.0f)
		{
			// CLAMPED BEFORE IT IS WRITTEN, as both decays are.
			const float Change =
				FMath::Clamp(Before - PerSecond * SecondsInStep, 0.0f, Resource->MaximumClassResourceAsked()) - Before;
			if (!FMath::IsNearlyZero(Change))
			{
				Cataclysm->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
				Moved = Cataclysm->GetNumericAttribute(Pool) - Before;
				// A DRAIN, for Crimson Tide (2026-11-25).
				FCataclysmWarFervour::NoteFervourSpentOrDrained(Cataclysm, -Moved);
			}
		}
	}

	// AND THE STATE JUDGED AGAIN, after the drain: the drain's own write is judged by the pool's change notice on a
	// player, and this covers what moves no pool (a weapon swap) and a character with no notice bound.
	Judge(Cataclysm);
	return Moved;
}

float FCataclysmBerserkerStates::SpendBerserking(UAbilitySystemComponent* AbilitySystem,
												 const FGameplayTagContainer& SkillTags, int32 TargetsStruck)
{
	UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm || !Cataclysm->IsBerserkingArmed() || TargetsStruck <= 0)
	{
		return 0.0f;
	}
	if (Cataclysm->BerserkerState() != ECataclysmBerserkerState::None)
	{
		// Q21. `Judge` disarms on entering a state, so this is only a guard.
		Cataclysm->SetBerserkingArmed(false);
		return 0.0f;
	}

	// WITH THE ATTACK'S OWN TAGS, so the row's `Type.Melee` scope decides whether this attack is the one.
	const float PerFervour = Cataclysm->StatForSkill(FName(BerserkingDamagePerFervourStat), SkillTags, 0.0f);
	if (PerFervour <= 0.0f)
	{
		return 0.0f;
	}

	// DISARMED BEFORE THE POOL IS WRITTEN, so the change notice the write raises judges a character already spent.
	Cataclysm->SetBerserkingArmed(false);
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Consumed = Cataclysm->GetNumericAttribute(Pool);
	if (Consumed <= 0.0f)
	{
		return 0.0f;
	}
	Cataclysm->ApplyModToAttribute(Pool, EGameplayModOp::Additive, -Consumed);
	// A SPEND, for Crimson Tide (2026-11-25).
	FCataclysmWarFervour::NoteFervourSpentOrDrained(Cataclysm, Consumed);
	return PerFervour * Consumed;
}

FString FCataclysmBerserkerStates::StateTextFor(const AActor* Actor)
{
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<UCataclysmAbilitySystemComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor));
	if (!Cataclysm)
	{
		return FString();
	}
	switch (Cataclysm->BerserkerState())
	{
	case ECataclysmBerserkerState::Wrath:
		return WrathText;
	case ECataclysmBerserkerState::Frenzy:
		return FrenzyText;
	case ECataclysmBerserkerState::None:
		break;
	}
	return Cataclysm->IsBerserkingArmed() ? FString(BerserkingReadyText) : FString();
}
