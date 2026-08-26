// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDamageConversion.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"

const TCHAR* UCataclysmDamageConversion::ActiveStat =
	TEXT("damage_to_bleeding_on_low_health");
const TCHAR* UCataclysmDamageConversion::WindowStat =
	TEXT("damage_to_bleeding_window");

namespace
{
	/** The character's ability system, as this project's own type. */
	UCataclysmAbilitySystemComponent* ConversionSystemOf(AActor* Character)
	{
		return Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	}

	/** Whether the node is taken at all, asked through the stat pipeline. */
	bool RuleApplies(UCataclysmAbilitySystemComponent* System)
	{
		const FGameplayAttribute Flag = UCataclysmClassResourceAttributeSet
			::GetDamageToBleedingOnLowHealthAttribute();

		// ASKED RATHER THAN READ, which is the standing rule for anything a
		// later node might put a condition on. A conditional row is never folded
		// into an attribute, so a plain read would answer zero for ever and
		// nothing at run time would report it.
		//
		// AND THE FALLBACK IS THE ATTRIBUTE'S OWN VALUE, WHICH IS NOT WHAT THE
		// FERVOUR FLAG BESIDE IT PASSES. `StatForSkill` answers with the
		// fallback when no stat line has been recorded for this character at
		// all, which is ordinary: an enemy never gets one, and a player has none
		// until the first refresh. `fervour_loss_suppressed` passes zero there
		// because every row that sets it requires a tag and is therefore never
		// in the attribute. THIS row carries no tags and no condition, so it IS
		// folded in, and passing zero would throw the answer away in exactly the
		// cases the fallback exists for.
		return System->StatForSkill(
			FName(UCataclysmDamageConversion::ActiveStat),
			FGameplayTagContainer(),
			System->GetNumericAttribute(Flag)) > 0.0f;
	}
}

void UCataclysmDamageConversion::NoteHealthChanged(AActor* Character)
{
	UCataclysmAbilitySystemComponent* System = ConversionSystemOf(Character);
	if (!System)
	{
		return;
	}

	const UCataclysmVitalAttributeSet* Vitals =
		System->GetSet<UCataclysmVitalAttributeSet>();
	if (!Vitals)
	{
		return;
	}

	const float Maximum = Vitals->GetMaxHealth();
	if (Maximum <= 0.0f)
	{
		// NO MAXIMUM MEANS NO SHARE OF IT. A character part way through being
		// set up has a maximum of zero for a frame, and dividing by it would
		// answer that everything is below half.
		return;
	}

	const bool bAboveHalfNow = Vitals->GetHealth() > Maximum * HealthShare;
	const bool bWasAbove = System->WasAboveHalfHealth();

	// REMEMBERED FIRST AND UNCONDITIONALLY, so that a character who never had
	// the node still has an accurate answer the moment they take one. Recording
	// it only when the rule applies would leave a character who respecs into
	// this node believing they had been below half all along.
	System->NoteAboveHalfHealth(bAboveHalfNow);

	if (bAboveHalfNow || !bWasAbove)
	{
		// NOT A CROSSING. Either they are still above half, or they were already
		// at or below it and have simply been hit again.
		return;
	}

	if (!RuleApplies(System) || !System->MayStartDamageConversion())
	{
		return;
	}

	// READ OFF THE ATTRIBUTE, WHICH IS SAFE ONLY BECAUSE THE ROW ON IT CARRIES
	// NO CONDITION AND NO SCALE. See the attribute's own comment: if a later
	// node ever conditions this stat, this read has to become a `StatForSkill`
	// call or that row will be dropped in silence.
	const float Window = System->GetNumericAttribute(
		UCataclysmClassResourceAttributeSet::GetDamageToBleedingWindowAttribute());

	System->NoteDamageConversionStarted(Window, CooldownSeconds);

	UE_LOG(LogCataclysm, Verbose,
		   TEXT("%s dropped below half health and will turn damage into "
				"Bleeding for %.2f seconds."),
		   *GetNameSafe(Character), Window);
}

float UCataclysmDamageConversion::ConvertIfActive(
	AActor* Character, float ToHealth, bool bIsAlreadyDamageOverTime)
{
	if (ToHealth <= 0.0f || bIsAlreadyDamageOverTime)
	{
		// A TICK OF SOMETHING ALREADY SPREAD OVER TIME IS NEVER CONVERTED. The
		// Bleeding this creates arrives as damage like anything else, so
		// converting it again would convert it for ever and nothing would ever
		// reach health.
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* System = ConversionSystemOf(Character);
	if (!System || !System->IsConvertingDamageToBleeding())
	{
		return 0.0f;
	}

	// THE WHOLE OF IT, NOT A SHARE. "Converts ALL damage you take."
	const bool bApplied = UCataclysmSkillEffects::ApplyDamageOverTime(
		Character, Character, ToHealth / BleedingSeconds, BleedingSeconds,
		UCataclysmDamageCalculation::DamageOverTimeTag(),
		/*bScalesWithInstigator=*/false);

	if (!bApplied)
	{
		// NOTHING WAS CONVERTED, SO THE CALLER STILL TAKES IT OFF HEALTH. The
		// alternative -- reporting it converted anyway -- would delete the
		// damage outright and make the node an immunity.
		UE_LOG(LogCataclysm, Warning,
			   TEXT("%s should have turned %.1f damage into Bleeding and could "
					"not, so it is being taken off health instead."),
			   *GetNameSafe(Character), ToHealth);
		return 0.0f;
	}

	// THE CHARACTER IS ITS OWN INSTIGATOR, AND IT DOES NOT SCALE WITH ANYONE.
	// The node transforms what YOU take rather than adding an attack of the
	// enemy's, so a kill by this Bleeding is not credited to a creature that
	// dealt the blow seconds earlier and may already be dead. Scaling is off for
	// the same reason: the Bleeding must be worth exactly what was converted,
	// not what the converter's damage-over-time stats would make of it.
	return ToHealth;
}
