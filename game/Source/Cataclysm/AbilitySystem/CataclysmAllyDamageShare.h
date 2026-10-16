// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmAllyDamageShare.generated.h"

class AActor;
class UAbilitySystemComponent;
struct FGameplayEffectContextHandle;
struct FGameplayEffectSpec;

/**
 * A share of what a blow did to an ally, taken by each wearer standing beside it.
 *
 * THE SENTENCE, from `game/Data/EnchantmentsNegative.csv`, row `Negative_You_take_5_15_damage_done_to_nearby_allies`:
 * "You take 5%-15% damage done to nearby allies". Ruled 2026-10-15 by the coordinating session, R1 to R8 in the
 * entry of that date in `docs/DECISIONS.md`.
 *
 * IN ADDITION, NOT INSTEAD. The ally takes its whole blow; each wearer within 5 m of it then takes the row's percent
 * of what reached the ally's health, as a blow of its own.
 *
 * WHO IS AN ALLY IS DECIDED BY TEAM (R1): `UCataclysmTargeting::FindAlliesInSphere`, which asks
 * `UCataclysmTeams::AttitudeBetween`. So a minion, a gadget, an imp, a thrall, a warzone soldier on the player's side
 * and a second player are each an ally, and a maddened one is not while the madness lasts.
 *
 * WHERE (R3): `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`, in its damage branch, which every blow and
 * every tick written to the Damage attribute passes. A loss of health written directly -- Sacrificial Bond's shares,
 * an execute, the delayed-damage drain, Sacrificial Ward, Infernal Sacrifice -- never passes there and is never
 * shared (R6).
 *
 * THE BLOW TO THE WEARER (R7) is sent by `UCataclysmSkillEffects::ApplyDirectDamage` in the name of whoever struck
 * the ally, carrying the ally's blow's damage type and `FCataclysmHitDelivery::bIsAShareOfDamageDoneToAnAlly`. See
 * `PayShares`.
 *
 * A SEPARATE CLASS OF STATIC FUNCTIONS, as `UCataclysmDelayedDamage` is, so the attribute set gains three calls
 * rather than the search and the delivery.
 */
UCLASS()
class CATACLYSM_API UCataclysmAllyDamageShare : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * `damage_done_to_nearby_allies_taken_percent`: the percent of what reaches a nearby ally's health that its
	 * wearer takes. Flat. No gameplay attribute and no base, so it is on
	 * `UCataclysmPlayerClassStats::StatsWithNoAttribute()`. Nought or less takes nothing.
	 * `DAMAGE_DONE_TO_NEARBY_ALLIES_TAKEN_PERCENT_STAT` in `tools/generate_datatables.py` holds the same name.
	 */
	static const TCHAR* PercentStat;

	/**
	 * The percent this ability system's character holds, asked with no tags and a fallback of nought, and held to
	 * 100: a wearer never takes more than reached the ally. Nought for an ability system this project did not make.
	 *
	 * NO TAGS (R8): the row is about every blow any ally beside the wearer takes, and no skill is in hand.
	 */
	static float PercentHeldBy(const UAbilitySystemComponent* AbilitySystem);

	/** Whether this effect is a share's blow: stamped with `UCataclysmSkillEffects::ShareOfDamageDoneToAnAllyDataName`. */
	static bool IsAShare(const FGameplayEffectSpec& Spec);

	/**
	 * Every character within `UCataclysmAbilitySystemComponent::NearbyActionRadiusCm` of `Struck` that is on its side
	 * and holds the percent, nearest first (R2, R4).
	 *
	 * EMPTY, AND NOTHING IS SEARCHED, when `Struck` is missing, when it is not of the players' team, or when it holds
	 * the percent itself: a wearer struck shares nothing with another wearer (R4). The team test is a judgement of
	 * the writing session: nothing of the monsters' side can wear a row, so a blow on a creature is never searched
	 * from.
	 *
	 * @param Struck        the struck character's body: the pawn for a player, which the search is centred on
	 * @param StruckSystem  its ability system, which for a player is on the player state
	 */
	static TArray<AActor*> WearersBeside(const AActor* Struck, const UAbilitySystemComponent* StruckSystem);

	/**
	 * Send each of `Wearers` its share of `TakenFromHealth`: the percent it holds now of that figure, as one blow.
	 *
	 * IN THE NAME OF WHOEVER STRUCK THE ALLY: the instigator of `AllysBlow`, and its source object as the actor that
	 * dealt it, so the share is credited exactly as the ally's blow was and a death it causes names that attacker.
	 * NOTHING IS SENT WHEN THAT INSTIGATOR IS GONE or has no ability system, a judgement of the writing session: the
	 * share would otherwise need a sender the ally's blow did not have.
	 *
	 * OF `DamageType`, the type the ally's blow was met by, so the wearer's resistance of that type meets it; none for
	 * an untyped blow, which then asks its attacker as every blow does.
	 *
	 * THE DELIVERY: not evaded, never a critical strike, no penetration, no weapon sub-type, no leech, no ailment
	 * chance, never retaliated against, and marked `bIsAShareOfDamageDoneToAnAlly`. Armour, resistance, block,
	 * damage reduction, the wearer's `damage_taken` and its three pools meet it as they meet any blow.
	 *
	 * A WEARER THAT HAS DIED SINCE IT WAS FOUND IS SKIPPED, asked again for each, as
	 * `UCataclysmSkillEffects::ExplodeForOverkill` asks.
	 *
	 * @return how many shares were sent
	 */
	static int32 PayShares(const TArray<AActor*>& Wearers, const FGameplayEffectContextHandle& AllysBlow,
						   float TakenFromHealth, FName DamageType);
};
