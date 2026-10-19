// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmTimeLockRift.generated.h"

class AActor;
class ACataclysmGroundZone;
class UCataclysmAbilitySystemComponent;

/**
 * Chronomancer's Time-Lock's temporal rift. Ruled for its ten-piece bonus, "Every 30 seconds, a "temporal rift"
 * appears around you. All enemies that enter the rift are frozen in time for 5 seconds and take 500% more damage from
 * all sources. Every enemy caught in it extends the duration by 1s"; `docs/DECISIONS.md` has the entry. The owner,
 * 2026-10-08: the freeze "acts like a stun"; 2026-10-17 (#10): the rift lengthens 1 s per enemy caught, at most 10 s,
 * and an enemy already caught in that rift is not frozen again. The coordinating session, 2026-10-19: the whole length
 * at most 10 s (K1); 5 s base (K2); 5 m radius (K3); placed where the wearer stands and fixed there, on a 30 s clock
 * that once ready waits for an enemy within the radius (K4); every enemy inside when it appears and every one entering
 * while it lasts, once per rift, stunned through `ApplyStun` by the wearer, a refused stun still caught (K5, K6); x6 on
 * all damage taken while that stun lasts, none for a boss the stun refused (K7); the stun only (K8); drawn as a ground
 * zone, nothing on the HUD (K9).
 *
 * WHAT ARMS IT. A wearer whose `EverySecondsStat` is above nought. Every figure is a row's, read now, every time, with
 * no tags: none of them belongs to one of the wearer's skills. All seven are in
 * `UCataclysmPlayerClassStats::StatsWithNoAttribute()`.
 *
 * THE CLOCK, AS THE GROUND POUND'S. `Step` runs on the regeneration step, four times a second, for every living
 * character. The first step that finds a wearer armed starts its clock: ready `EverySecondsStat` seconds later, in the
 * world's seconds. A step on which the wearer is not armed forgets the clock, and ends a rift standing.
 *
 * WHEN IT APPEARS. Once ready, on the first step that finds an enemy of the wearer within `RadiusMetresStat` of where
 * the wearer stands, and not before. The clock restarts as it appears. "Within" is the ground zone's own rule: the
 * enemy's centre, along the ground, as the rift will count it once it is there, so the enemy that called it is caught.
 *
 * THE RIFT. An `ACataclysmGroundZone` of the wearer's at that place, which stays there, deals nothing and lays nothing,
 * marked `bIsNotAPersistentArea`. Its length is `BaseSecondsStat` and `SecondsPerEnemyStat` more for each enemy
 * caught, the WHOLE of it at most `MostSecondsStat`; the zone is told its length again whenever that grows, and is
 * drawn for what is left.
 *
 * WHO IS CAUGHT. On the step it appears and on every step while it lasts, every enemy inside it not caught by this rift
 * before: once per rift, so one that stays, or leaves and comes back, is not caught again. Each is stunned for
 * `StunSecondsStat` through `UCataclysmSkillEffects::ApplyStun` by the wearer as a designed stun, so every rule of the
 * game's stun holds but the damage threshold; a stun it refuses, a boss's among them, still counts as caught and
 * still lengthens the rift. For a stun that lands, `UCataclysmDamageTakenMultiplier` holds one plus
 * `DamageTakenMorePercentStat` per cent on the enemy for the seconds the stun was laid for, read off the stun itself.
 */
UCLASS()
class CATACLYSM_API UCataclysmTimeLockRift : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** `time_lock_rift_every_seconds`: the seconds from one rift to the time the next is ready, a flat 30. */
	static const TCHAR* EverySecondsStat;

	/** `time_lock_rift_radius_metres`: the rift's radius, and how near an enemy calls it, a flat 5. */
	static const TCHAR* RadiusMetresStat;

	/** `time_lock_rift_seconds`: the rift's length before any enemy is caught, a flat 5. Nought places none. */
	static const TCHAR* BaseSecondsStat;

	/** `time_lock_rift_seconds_per_enemy`: the seconds each enemy caught adds to its length, a flat 1. */
	static const TCHAR* SecondsPerEnemyStat;

	/** `time_lock_rift_most_seconds`: the most its whole length may be, a flat 10. Nought puts no bound on it. */
	static const TCHAR* MostSecondsStat;

	/** `time_lock_rift_stun_seconds`: the stun on each enemy caught, a flat 5. Nought stuns nothing. */
	static const TCHAR* StunSecondsStat;

	/** `time_lock_rift_damage_taken_more_percent`: how much more damage a frozen enemy takes, a flat 500. */
	static const TCHAR* DamageTakenMorePercentStat;

	/** One of the seven stats above, asked of `Wearer` with no tags. Nought for no wearer. */
	static float StatOf(const UCataclysmAbilitySystemComponent* Wearer, const TCHAR* Stat);

	/**
	 * Start, keep or forget `Character`'s clock, place its rift if it is ready and an enemy is near, and catch every
	 * enemy inside a rift of its that stands. Run by the regeneration step; a test moves the world's clock by hand and
	 * calls this directly. Refuses a corpse.
	 *
	 * @return how many enemies this step caught
	 */
	static int32 Step(AActor* Character);

	/** The seconds until `Wearer`'s next rift is ready, nought when it is, and -1 when it has no clock. */
	static float SecondsUntilReady(const UCataclysmAbilitySystemComponent* Wearer);

	/** `Wearer`'s rift while it stands, or null. */
	static ACataclysmGroundZone* RiftOf(const UCataclysmAbilitySystemComponent* Wearer);

	/** The whole length of `Wearer`'s standing rift, from when it appeared, or nought when none stands. */
	static float RiftLengthSeconds(const UCataclysmAbilitySystemComponent* Wearer);

	/** How many enemies `Wearer`'s standing rift has caught, or nought when none stands. */
	static int32 CaughtBy(const UCataclysmAbilitySystemComponent* Wearer);
};
