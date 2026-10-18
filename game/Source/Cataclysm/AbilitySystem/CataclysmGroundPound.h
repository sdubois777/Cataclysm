// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmGroundPound.generated.h"

class AActor;
class UCataclysmAbilitySystemComponent;

/**
 * Brute's Heart's Ground Pound. Ruled for its ten-piece bonus, "Every 30 seconds, you can unleash a powerful "Ground
 * Pound" that deals 500% of your armor as damage to all enemies in a large radius and stuns them for 3 seconds. The
 * cooldown of this ability is reduced by 0.5 seconds per kill"; `docs/DECISIONS.md` has the entry. The owner, 2026-10-17
 * (#5): it FIRES BY ITSELF once ready and an enemy is inside its radius, with no key. The coordinating session,
 * 2026-10-18: 8 m (Q1); the ordinary stun, 3 s (Q2); each kill credited to the wearer takes 0.5 s off the time left,
 * never below nought, and a kill while ready banks nothing (Q3); the blow exactly as `smite_nearby_by_armor` deals its
 * blow (Q4); first ready 30 s after the tenth piece goes on (Q5).
 *
 * WHAT ARMS IT. A wearer whose `EverySecondsStat` is above nought. Every figure is a row's, read now, every time, with
 * no tags: none of them belongs to one of the wearer's skills. All five are in
 * `UCataclysmPlayerClassStats::StatsWithNoAttribute()`.
 *
 * THE CLOCK. `Step` runs on the regeneration step, four times a second, for every living character
 * (`ACataclysmCharacterBase`). The first step that finds a wearer armed starts its clock: ready `EverySecondsStat`
 * seconds later. A step on which the wearer is not armed forgets the clock, so putting the tenth piece on again starts
 * it again from the full interval. THE SECONDS ARE THE WORLD'S, in combat or out of it: not the combat seconds a timed
 * row counts.
 *
 * THE FIRING RULE. Once ready, the first step that finds an enemy of the wearer within `RadiusMetresStat` of it fires,
 * and not before; a ready wearer with no enemy near holds its pound for as long as that lasts. Firing restarts the
 * clock at the full interval BEFORE the blow lands, so a kill the pound makes takes time off the next one.
 *
 * THE BLOW. To every enemy the search found, `ArmourPercentStat` per cent of the wearer's armour, asked as
 * `UCataclysmAbilitySystemComponent::ActOnNearby` asks it for `smite_nearby_by_armor` and dealt by the same call,
 * `UCataclysmSkillEffects::ApplyDirectDamage` with no delivery: dealt by the wearer, not area damage (so it can be
 * evaded), able to critically strike, and retaliated against, as that blow is. THEN THE STUN, `StunSecondsStat`
 * seconds, through `UCataclysmSkillEffects::ApplyStun` as a designed stun, so every rule of the game's stun applies
 * but the damage threshold: crowd control resistance shortens it, a boss is never stunned, an enemy stunned in the last
 * five seconds is not stunned again. An enemy the blow killed is not stunned.
 *
 * A KILL. The clock listens to the wearer's own `OnActionEvent` for `kill`, which `ACataclysmPlayerCharacter` raises
 * only for a death it is credited with. Each takes `SecondsOffPerKillStat` off the time left, never below nought; a
 * kill while ready changes nothing.
 */
UCLASS()
class CATACLYSM_API UCataclysmGroundPound : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** `ground_pound_every_seconds`: the seconds from one pound to the time it is ready again, a flat 30. */
	static const TCHAR* EverySecondsStat;

	/** `ground_pound_percent_of_armor`: the per cent of the wearer's armour each enemy struck takes, a flat 500. */
	static const TCHAR* ArmourPercentStat;

	/** `ground_pound_radius_metres`: how far from the wearer an enemy is struck, a flat 8. Nought fires nothing. */
	static const TCHAR* RadiusMetresStat;

	/** `ground_pound_stun_seconds`: the stun on each enemy struck, a flat 3. Nought stuns nothing. */
	static const TCHAR* StunSecondsStat;

	/** `ground_pound_seconds_off_per_kill`: the seconds each kill takes off the time left, a flat 0.5. */
	static const TCHAR* SecondsOffPerKillStat;

	/** One of the five stats above, asked of `Wearer` with no tags. Nought for no wearer. */
	static float StatOf(const UCataclysmAbilitySystemComponent* Wearer, const TCHAR* Stat);

	/**
	 * Start, keep or forget `Character`'s clock, and fire its pound if it is ready and an enemy is near. Run by the
	 * regeneration step; a test moves the world's clock by hand and calls this directly. Refuses a corpse.
	 *
	 * @return how many enemies the pound struck: nought when it did not fire
	 */
	static int32 Step(AActor* Character);

	/** The seconds until `Wearer`'s pound is ready, nought when it is, and -1 when it has no clock. */
	static float SecondsUntilReady(const UCataclysmAbilitySystemComponent* Wearer);

	/** A kill credited to `Wearer`: takes `SecondsOffPerKillStat` off the time left. Heard from `OnActionEvent`. */
	static void NoteKill(UCataclysmAbilitySystemComponent* Wearer);
};
