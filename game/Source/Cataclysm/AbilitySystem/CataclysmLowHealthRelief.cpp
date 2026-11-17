// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmLowHealthRelief.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmHealthDebt.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"

bool UCataclysmLowHealthRelief::RuleApplies(
	const UCataclysmAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return false;
	}

	using Resource = UCataclysmClassResourceAttributeSet;
	const FGameplayAttribute Cleared =
		Resource::GetDebtClearedOnDroppingLowAttribute();
	const FGameplayAttribute Fervour =
		Resource::GetFervourOnDroppingLowAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Cleared))
	{
		// NO CLASS RESOURCE SET MEANS NO CAPSTONE OPTION, which is every enemy
		// in the game, and every character goes through the same health write.
		return false;
	}

	// NO SKILL TAGS AND NO SKILL COST. Nothing is being cast; this happens
	// because health moved.
	//
	// AND THE FALLBACK IS EACH ATTRIBUTE'S OWN VALUE RATHER THAN ZERO, which is
	// the distinction `UCataclysmDamageConversion::RuleApplies` draws.
	// `StatForSkill` answers with the fallback when no stat line has been
	// recorded for this character at all, which is ordinary rather than a
	// fault. Neither row carries a condition or a tag, so both ARE folded into
	// their attributes, and passing zero would throw the answer away in exactly
	// the case the fallback exists for.
	const FGameplayTagContainer NoTags;
	return AbilitySystem->StatForSkill(
			   FName(UCataclysmHealthDebt::ClearedOnDroppingLowStat), NoTags,
			   AbilitySystem->GetNumericAttribute(Cleared)) > 0.0f
		|| AbilitySystem->StatForSkill(
			   FName(UCataclysmFervour::OnDroppingLowStat), NoTags,
			   AbilitySystem->GetNumericAttribute(Fervour)) > 0.0f;
}

float UCataclysmLowHealthRelief::NoteHealthChanged(AActor* Character)
{
	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const UCataclysmVitalAttributeSet* Vitals =
		AbilitySystem->GetSet<UCataclysmVitalAttributeSet>();
	if (!Vitals)
	{
		return 0.0f;
	}

	const float Maximum = Vitals->GetMaxHealth();
	if (Maximum <= 0.0f)
	{
		// NO MAXIMUM MEANS NO SHARE OF IT. A character part way through being
		// set up has a maximum of zero for a frame, and dividing by it would
		// answer that everything is below the threshold. The same refusal
		// `UCataclysmDamageConversion::NoteHealthChanged` makes.
		return 0.0f;
	}

	const bool bAboveNow = Vitals->GetHealth() > Maximum * HealthShare;
	const bool bWasAbove = AbilitySystem->WasAboveLowHealth();

	// REMEMBERED FIRST AND UNCONDITIONALLY, so a character that takes this
	// option later has an accurate answer the moment it does. Recording it only
	// when the rule applies would leave one that respecs into the option
	// believing it had been low all along, and its next crossing would be
	// invisible.
	AbilitySystem->NoteAboveLowHealth(bAboveNow);

	if (bAboveNow || !bWasAbove)
	{
		// NOT A CROSSING. Either health is still above the line, or it was
		// already at or below it and the character has simply been hit again.
		return 0.0f;
	}

	if (!RuleApplies(AbilitySystem))
	{
		// EVERY CHARACTER IN THE GAME WITHOUT THAT CAPSTONE OPTION. Asked
		// before the clock is, so nobody else ever touches the timestamp.
		return 0.0f;
	}

	if (!AbilitySystem->MayTakeLowHealthRelief())
	{
		// CROSSED AGAIN INSIDE THE THIRTY SECONDS. A Masochist that keeps
		// paying health crosses this line often, and the option says it is
		// honoured no more than once every 30 seconds.
		return 0.0f;
	}

	// RECORDED BEFORE THE TWO EFFECTS AND NOT AFTER THEM, and it is recorded
	// even when both come to nothing. The option says the crossing may be
	// honoured once every 30 seconds; a character that owed nothing and had a
	// full bar has still had its crossing honoured, and looking at the result to
	// decide whether it counted would be a rule the sentence does not state.
	AbilitySystem->NoteLowHealthReliefTaken(CooldownSeconds);

	const float Cleared = UCataclysmHealthDebt::ClearOnDroppingLow(Character);
	const float Gained = UCataclysmFervour::GainOnDroppingLow(AbilitySystem);

	UE_LOG(LogCataclysm, Verbose,
		   TEXT("%s dropped below %.0f%% health, clearing a debt of %.1f and "
				"gaining %.1f Fervour."),
		   *GetNameSafe(Character), HealthShare * 100.0f, Cleared, Gained);

	return Cleared + Gained;
}
