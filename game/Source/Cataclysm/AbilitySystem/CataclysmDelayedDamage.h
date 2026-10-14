// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmDelayedDamage.generated.h"

class AActor;
class UAbilitySystemComponent;

/**
 * A share of the damage that would reach a character's health, taken later instead of now.
 *
 * THE SENTENCE, from `game/Data/EnchantmentsPositive.csv`: "25%-50% of damage taken is dealt over 4-8 seconds".
 * Ruled 2026-10-12 by the coordinating session under the owner's delegation. The share is taken off a blow where the
 * blow is about to be written to health and is added to a store on the character. The store is drained out of
 * health over the row's seconds.
 *
 * A STORE AND NOT A BLEED, ruled. It is not an ailment, is not a debuff, is not cleansed, and does not count as
 * bleeding. The Masochist's Breaking Point (`UCataclysmDamageConversion`) turns a blow into a Bleed, and a damage
 * over time effect keeps the stronger application and does not add, so blows turned into one Bleed do not add up.
 * This store adds up.
 *
 * EACH BLOW'S SHARE IS ITS OWN PARCEL, drained evenly over the seconds the row stated when the blow was taken,
 * counted from that moment. Parcels overlap, and what a step takes is the sum of what each parcel owes for the time
 * since the step before. The parcels are kept by `UCataclysmAbilitySystemComponent::HoldDelayedDamage`.
 *
 * A SEPARATE CLASS OF STATIC FUNCTIONS, as `UCataclysmHealthDebt` is, and for its reason: the hook that drains it,
 * `ACataclysmCharacterBase::RegenerationStep`, runs on a timer no automation test can watch fire, so the job is a
 * function a test can call.
 *
 * FOUR PLACES TOUCH IT.
 *
 *   taking     `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`, where a blow's damage reaches health after
 *              mana, the energy shield and the temporary absorb have taken theirs.
 *   draining   `ACataclysmCharacterBase::RegenerationStep`, the step the health debt drains on.
 *   clearing   `UCataclysmAbilitySystemComponent::ClearWhatDeathEnds`, where the health debt is cleared, and
 *              `DrainStep` itself when its own loss kills.
 *   showing    `UCataclysmCombatOverlay::OwedTextFor`, above the player's own bars.
 */
UCLASS()
class CATACLYSM_API UCataclysmDelayedDamage : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * `damage_taken_delayed_percent`: the share of what would reach health that is taken later, as a percent. Flat.
	 * No gameplay attribute and no base, so it is on `UCataclysmPlayerClassStats::StatsWithNoAttribute()`. Nought
	 * or less delays nothing.
	 */
	static const TCHAR* SharePercentStat;

	/**
	 * `damage_taken_delayed_seconds`: the seconds over which a delayed share is drained. Flat. No gameplay
	 * attribute and no base, so it is on `UCataclysmPlayerClassStats::StatsWithNoAttribute()`. Nought or less
	 * delays nothing: a character holding the share and no seconds takes every blow whole and at once.
	 */
	static const TCHAR* SecondsStat;

	/**
	 * How much of `ToHealth` is taken later, for this share and these seconds.
	 *
	 * PURE ARITHMETIC AND NO ABILITY SYSTEM. Nought when any of the three is nought or less. The share is held to
	 * 100, so never more than `ToHealth`.
	 */
	static float AmountDelayed(float ToHealth, float SharePercent, float Seconds);

	/**
	 * Take the character's share off what is about to be written to its health, and hold it in its store.
	 *
	 * `ToHealth` IS WHAT WOULD REACH HEALTH: what is left of the blow after its mitigation and after mana, the
	 * energy shield and the temporary absorb took theirs, and NOT cut to the health the character holds. So a blow
	 * that would have emptied health can leave the character alive, holding a store that then empties it.
	 *
	 * A TICK OF DAMAGE OVER TIME IS NOT DELAYED, ruled: `bIsDamageOverTime` answers nought.
	 *
	 * THE TWO STATS ARE ASKED WITH NO TAGS AND A FALLBACK OF NOUGHT. The share first, so a character with no such
	 * row is asked for one stat and no more.
	 *
	 * NOTHING IS DELAYED WHEN THE PARCEL CANNOT BE HELD, which is a component with no world to read a clock from.
	 * The blow then lands whole.
	 *
	 * @return how much was taken off and held; the caller writes the rest to health
	 */
	static float DelayShare(UAbilitySystemComponent* AbilitySystem, float ToHealth, bool bIsDamageOverTime);

	/**
	 * What `DelayShare` would take off `ToHealth`, WITH NOTHING HELD. `DelayShare` is written on it.
	 *
	 * FOR THE TWO SAVES FROM A LETHAL BLOW, ruled 2026-10-13 by the coordinating session under the owner's
	 * delegation: with the row worn, a save judges the blow after the delayed share is taken off, so that it is
	 * never spent on a blow that would not have killed. The saves stand in
	 * `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` far above the place the share is taken, so they ask
	 * here what it will be. A BLOW A SAVE DID CUT IS NOT HANDED TO `DelayShare` AT ALL: nothing of it is delayed.
	 *
	 * NOUGHT WHERE `DelayShare` TAKES NOTHING: a tick, nothing arriving, a character without both stats, and a
	 * component with no world, for which no parcel can be held.
	 *
	 * @param OutSeconds when given, the seconds the row states, and nought when the share was not asked for
	 * @return how much `DelayShare` would take off and hold
	 */
	static float ShareThatWouldBeDelayed(UAbilitySystemComponent* AbilitySystem, float ToHealth,
										 bool bIsDamageOverTime, float* OutSeconds = nullptr);

	/**
	 * Drain what the character's parcels owe by now out of its health. Called every regeneration step.
	 *
	 * A DIRECT LOSS OF HEALTH. It is written to the health attribute and not sent as damage, so it meets no
	 * evasion, block, armour, resistance, damage reduction, mana, energy shield or temporary absorb: the share was
	 * taken after all of those. It is also not stopped by a window in which the character takes no damage, and
	 * neither save from a lethal blow looks at it, because all three sit where a blow is resolved.
	 *
	 * IT CAN KILL. When what is due is at least the health held, the store is cleared, health is written to nought
	 * and the death is asked for: the pair `UCataclysmHealthDebt::DrainWhileDebtExceedsHealth` uses. THE KILLER IS
	 * WHOEVER THE CHARACTER'S LAST BLOW ON RECORD NAMES, which is how a death by any direct loss of health is
	 * credited (`UCataclysmCombatEvents::NoteDeath`); nobody when no blow is on record.
	 *
	 * A CORPSE PAYS NOTHING, as `UCataclysmHealthDebt::DrainIfDue` says of one.
	 *
	 * @return how much health was really taken this step
	 */
	static float DrainStep(AActor* Character);
};
