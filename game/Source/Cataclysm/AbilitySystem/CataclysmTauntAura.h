// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmTauntAura.generated.h"

class AActor;
class UCataclysmAbilitySystemComponent;

/**
 * Brute's Heart's taunt aura. Ruled for its six-piece bonus, "When your health falls below 50%, you gain a powerful
 * aura that taunts all nearby enemies and increases your armor and resistances by 50%"; `docs/DECISIONS.md` has the
 * entry. The owner, 2026-10-17 (#6): Warlord's Will 6's taunt status "also completes Brute's Heart 6's taunt". The
 * coordinating session, 2026-10-18: 5 m (Q1); every 1 s while health is below 50%, as the armour row reads it, each
 * enemy within 5 m is taunted by the wearer for 3 s (Q2); `UCataclysmTaunt` unchanged, so every taunt that lands,
 * placed or restarted, notes crowd control on the wearer (Q3), and bosses, resistance and an enemy another taunter
 * holds are as for Warlord's Will 6 (Q4). The armour half is a row of its own; THE RESISTANCES HALF IS NOT BUILT.
 *
 * WHAT ARMS IT. A wearer whose four stats below are all above nought. Every figure is a row's, read now, every time,
 * with no tags: none of them belongs to one of the wearer's skills. All four are in
 * `UCataclysmPlayerClassStats::StatsWithNoAttribute()`.
 *
 * BELOW THE SHARE, AS THE ARMOUR ROW READS IT. `BelowHealthPercentStat` is judged with the pipeline's own
 * `health_below` predicate on the wearer's own state (`UCataclysmStatPipeline::ConditionHolds` with
 * `CurrentConditions`): strictly below, and refused when the health cannot be read. So the aura and the set's armour
 * row, `armor` increased 50 under `health_below` 50, are on and off at the same instant.
 *
 * THE BEAT. `Step` runs on the regeneration step, four times a second, for every living character
 * (`ACataclysmCharacterBase`). A wearer below the share beats at once if it has not beaten within the last
 * `EverySecondsStat` seconds, and then every `EverySecondsStat` seconds while it stays below. A beat taunts every enemy
 * of the wearer within `RadiusMetresStat` of it for `TauntSecondsStat` seconds, through `UCataclysmTaunt::ApplyTaunt`
 * with the wearer as the taunter. A beat that finds nobody still counts. ABOVE THE SHARE the wearer does not beat but
 * keeps the time of its last beat, so crossing the line back and forth never beats faster than the interval. A step on
 * which the wearer is not armed forgets it.
 *
 * WHAT A TAUNT DOES, AND WHAT REFUSES ONE, IS `UCataclysmTaunt`'S. The same taunter restarts the seconds, so a wearer
 * staying below the share keeps every enemy in reach taunted; once above it, each taunt runs out its seconds. A boss,
 * an enemy another taunter holds (a Warhound's, Q2 of Warlord's Will 6), the wearer's health ceiling, an immunity the
 * enemy's running skill states and crowd-control resistance each refuse or shorten it there, and each taunt that
 * lands raises `crowd_control` on the wearer's ability system.
 */
UCLASS()
class CATACLYSM_API UCataclysmTauntAura : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** `taunt_aura_below_health_percent`: the share of maximum health the wearer must be strictly below, a flat 50. */
	static const TCHAR* BelowHealthPercentStat;

	/** `taunt_aura_every_seconds`: the seconds from one beat to the next, a flat 1. */
	static const TCHAR* EverySecondsStat;

	/** `taunt_aura_radius_metres`: how far from the wearer an enemy is taunted, a flat 5. */
	static const TCHAR* RadiusMetresStat;

	/** `taunt_aura_taunt_seconds`: the taunt each beat places or restarts, a flat 3. */
	static const TCHAR* TauntSecondsStat;

	/**
	 * How early a step may come and still take a beat, in seconds. The regeneration step is a timer, so a step meant
	 * to fall on the beat can arrive a hair before it, and without this the beat would slip a whole step. Less than
	 * half a step, so no step a quarter of a second early takes one.
	 */
	static constexpr float BeatAllowanceSeconds = 0.05f;

	/** One of the four stats above, asked of `Wearer` with no tags. Nought for no wearer. */
	static float StatOf(const UCataclysmAbilitySystemComponent* Wearer, const TCHAR* Stat);

	/**
	 * Beat `Character`'s aura if it is armed, below the share and due. Run by the regeneration step; a test moves the
	 * world's clock by hand and calls this directly. Refuses a corpse.
	 *
	 * @return how many taunts the beat landed, placed or restarted: nought when it did not beat or landed none
	 */
	static int32 Step(AActor* Character);
};
