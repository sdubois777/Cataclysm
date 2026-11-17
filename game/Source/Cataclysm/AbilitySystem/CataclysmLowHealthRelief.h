// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmLowHealthRelief.generated.h"

class AActor;
class UCataclysmAbilitySystemComponent;

/**
 * What a character gains from its health dropping very low.
 *
 * WHAT IT IS FOR. The second sentence of the Masochist's Rock Bottom, the first
 * option of The Second Vow:
 *
 *   Dropping below 20% health clears all outstanding debt and grants 50
 *   Fervour, no more than once every 30 seconds.
 *
 * Issue #1069. The option's FIRST sentence is elsewhere: a health cost that
 * cannot be paid becomes debt, which happens where a cost is charged, in
 * `UCataclysmSkillTemplate::PayHealthCost`.
 *
 * IT IS THE SECOND CROSSING IN THE GAME AND THE SHAPE IS COPIED FROM THE FIRST.
 * `UCataclysmDamageConversion` watches for a drop below half health for The
 * Breaking Point, and everything below is the same three parts it has: a memory
 * of where health was, so a crossing can be told from a state; a rule read off
 * the character, so nothing happens for anybody without the option; and a
 * cooldown kept as a timestamp rather than as a timer.
 *
 * A SEPARATE THRESHOLD AND A SEPARATE MEMORY FROM THAT ONE. The two nodes watch
 * different lines -- half health and a fifth of it -- so a character holding
 * both crosses them at different moments and each needs its own record of where
 * health was. One shared memory would make the second crossing invisible.
 *
 * A CLASS OF STATIC FUNCTIONS, like `UCataclysmDamageConversion`,
 * `UCataclysmHealthDebt` and `UCataclysmFervour`, and it does neither of the two
 * things it triggers: `UCataclysmHealthDebt::ClearOnDroppingLow` clears the debt
 * and `UCataclysmFervour::GainOnDroppingLow` fills the bar. Those are the files
 * that own a debt and a pool, and one place writing each is the rule both of
 * them already state. This is only the trigger.
 */
UCLASS()
class CATACLYSM_API UCataclysmLowHealthRelief : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The share of maximum health the character has to drop below.
	 *
	 * A CONSTANT AND NOT A CONDITION ON A ROW, which is the same call
	 * `UCataclysmDamageConversion::HealthShare` makes and for the same reason.
	 * The option says "DROPPING below 20% health", which is an EVENT. A state
	 * condition would fire again on every scratch taken while low, and a
	 * character sitting at 5% health would clear its debt continuously.
	 */
	static constexpr float HealthShare = 0.2f;

	/**
	 * The least time between one crossing being honoured and the next, in
	 * seconds.
	 *
	 * MEASURED FROM ONE OCCURRENCE TO THE NEXT, which is what "no more than
	 * once every 30 seconds" says in ordinary English, and the same reading
	 * `UCataclysmDamageConversion::CooldownSeconds` takes of the same phrase.
	 * There is no window here for it to be measured from the end of.
	 */
	static constexpr float CooldownSeconds = 30.0f;

	/**
	 * Whether this character has the option at all.
	 *
	 * EITHER OF ROCK BOTTOM'S TWO EFFECTS IS ENOUGH. The option grants both
	 * rows, so in the game today the two answers never differ; asking for
	 * either means a later node granting only one of them still works, and
	 * means a character with only one of them does not silently burn the
	 * cooldown on a crossing that does nothing.
	 *
	 * ASKED THROUGH THE STAT PIPELINE RATHER THAN READ OFF THE ATTRIBUTES,
	 * which is the standing rule for anything a later node might put a
	 * condition on. Neither row carries one today, so both routes agree.
	 */
	static bool RuleApplies(const UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Notice that a character's health has moved, and take the relief if it has
	 * just crossed below the threshold.
	 *
	 * CALLED FROM `UCataclysmVitalAttributeSet::NotifyHealthChanged`, beside the
	 * damage conversion's own crossing, because that function already fires on
	 * EVERY write to health. Every route that lowers health -- a blow, a health
	 * cost, a debt falling due -- comes through it, so this does not have to
	 * know which one did it.
	 *
	 * THE CROSSING AND NOT THE STATE, and the memory that makes that possible is
	 * `UCataclysmAbilitySystemComponent::WasAboveLowHealth`. It is written on
	 * every call whether or not the character has the option, so a character
	 * that respecs into it does not believe it has been low all along.
	 *
	 * @return how much was cleared plus how much Fervour arrived, which is zero
	 *         for every character in the game without the option, for one whose
	 *         health did not cross, and for one still inside its cooldown
	 */
	static float NoteHealthChanged(AActor* Character);
};
