// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmStigmata.generated.h"

class AActor;

/**
 * Setting an enemy alight sets the one who did it alight too. The Masochist's
 * Stigmata, `basic_fl_stem0`, as reworked by the project owner's approval of
 * 2026-11-24:
 *
 *   When you set an enemy alight, you are set alight as well for 4 seconds,
 *   taking 1% of your maximum health per second. +3% increased damage over time
 *   per point.
 *
 * WHY IT EXISTS. The Flagellant limb is built on the debuffs its holder carries
 * -- nine nodes, three of them keystones, and four vow options count them -- and
 * nothing in the limb or in the Fist put one on the Masochist. The audit of
 * 2026-11-24 counted 49 of the 56 Demonic skill rows setting something alight,
 * so a Flagellant holding this carries one debuff whenever it fights. The audit of 2026-11-24 (Q3) recommended that
 * the burn hurt, and the owner accepted it: "Stigmata's self-burn hurts, 1% of
 * maximum health a second". A burn that dealt nothing would leave Shared Agony,
 * Contagious Torment and the class's own "health lost" generator with nothing to
 * read.
 *
 * A REAL BURN ON ITS HOLDER, laid by `UCataclysmSkillEffects::ApplyDamageOverTime`
 * with the holder as both instigator and target, so everything that reads a
 * debuff on a character reads it: `UCataclysmDebuffs` counts it, its ticks are
 * damage over time the holder's own "damage over time taken" rows reduce, Shared
 * Agony's `Keyword.DoT` Fervour rate fills from them, and Contagious Torment
 * rolls on each. Its holder's Symphony of Pain and Vessel of Plagues lengthen it,
 * as they lengthen any debuff on it.
 *
 * NOT SCALED BY ITS HOLDER'S OWN DAMAGE OVER TIME STATS. "taking 1% of your
 * maximum health per second" is a figure the sentence states outright, so the
 * three stats that make an enemy's burn hurt more are not asked; Stigmata's own
 * "+3% increased damage over time" makes the holder's burns on ENEMIES hurt
 * more, not this one. The holder's resistances and its damage over time taken
 * rows still apply where the tick arrives, as they do to any burn on it.
 *
 * ONE BURN, REFRESHED AND NOT STACKED. A second application finds the first
 * running at the same strength and only gives it its duration again, which is
 * the one-burn rule `ApplyDamageOverTime` keeps for every burn. A stronger burn
 * an enemy laid on the holder stands, and this one only refreshes its time.
 *
 * NEVER RECURSIVE. The self-burn's instigator is its target, and only a burn
 * laid on ANOTHER character asks this, so setting itself alight is not "setting
 * an enemy alight". A copy a spread makes and converted damage do not ask either.
 */
UCLASS()
class CATACLYSM_API UCataclysmStigmata : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Above nought, setting an enemy alight sets this character alight too. A
	 * flat 1 a point on Stigmata's row, read as a switch: the figures below are
	 * the mechanism's own, as The Breaking Point's 50%, 5 seconds and 10 second
	 * cooldown are constants rather than rows.
	 */
	static const TCHAR* SelfBurnStat;

	/**
	 * What the self-burn takes each second, in percent of the holder's maximum
	 * health. The owner's answer to the audit's Q3, 2026-11-24. Below the
	 * Masochist's base regeneration at every level (2% a second at level 1,
	 * 1.49% at level 100), so a Flagellant standing in its own fire still heals.
	 */
	static constexpr float PercentOfMaximumHealthPerSecond = 1.0f;

	/** How long one self-burn lasts before its holder's duration rows, in seconds. */
	static constexpr float Seconds = 4.0f;

	/**
	 * What one tick of the self-burn takes from a holder with this much health.
	 * Pure arithmetic, so the figure can be checked without a character.
	 * Nought for no maximum.
	 */
	static float TickFor(float MaxHealth);

	/**
	 * `Instigator` has just set `Target` alight. If it holds Stigmata, set it
	 * alight too.
	 *
	 * CALLED BY `UCataclysmSkillEffects::ApplyDamageOverTime` for every damage
	 * over time laid on another character that is not a spread's copy or
	 * converted damage, before the target's own effect lands; it answers false at
	 * once for anything that is not a burn.
	 *
	 * @param EffectTag  what was laid; only a burn, or a tag under it, counts
	 * @return whether a self-burn was laid or refreshed
	 */
	static bool AfterSettingAlight(AActor* Instigator, AActor* Target,
								   const FGameplayTag& EffectTag);
};
