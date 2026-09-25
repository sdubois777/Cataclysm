// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CataclysmChorus.generated.h"

class AActor;
class UGameplayAbility;

/**
 * Chorus, the Ritualist's `Ritualist_capstone_200` option 3: "Your minions
 * repeat each skill you cast, dealing 30% of its damage." Issue #1515.
 *
 * RULED 2026-09-25, UNDER THE OWNER'S DELEGATION, except the first point, which
 * is the owner's own decision:
 *
 *   - EACH MINION DEALS `SharePercent` OF THE SKILL'S DAMAGE: with N living
 *     minions a hit gains N x 30%. The owner, 2026-09-25: "No it should be 30%
 *     each", overturning the coordinating session's ruling that the minions
 *     share 30% between them. Minions and thralls both count, as
 *     `UCataclysmCommand::ThingsCommandedBy` lists them.
 *   - EVERY SKILL BUT THE BASIC ATTACK, as the `skill_use` event counts them.
 *   - HITS ONLY, each enemy a hit of the cast landed on, nothing else: no burn,
 *     no rider, no displacement, no zone, no summoning. Hits a skill deals
 *     after its cast -- an aura's pulses, a rift's collapse, a buried axe's
 *     leap -- are not repeated; their callers do not ask.
 *   - EACH REPEAT IS THE MINION'S OWN BLOW, through `ApplyDirectDamage` with the
 *     minion's own delivery, worth `SharePercent` of what the caster's hit SENT. The
 *     target mitigates it. The Conduit keystone makes it count as the caster's
 *     the way it does any minion blow.
 *   - A REPEAT IS NOT A SKILL USE, so nothing repeats a repeat and Follow
 *     Through does not act on a kill by one.
 *
 * GENRE: Path of Exile's Mirage Archer, "Summon a Mirage Archer which uses that
 * Skill" at (31-40)% less damage, and Spell Totem, "a totem that casts the
 * spell for you" at (50-55)% less. One helper using your skill for a share of
 * it is shipped; many helpers each repeating it for that share is this design's
 * own.
 */
UCLASS()
class CATACLYSM_API UCataclysmChorus : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Above zero means the option is held. Its row will carry 1. */
	static const TCHAR* Stat;

	/** What each minion's repeat of one hit is worth, in percent of what
	 *  the hit sent. */
	static constexpr float SharePercent = 30.0f;

	/**
	 * After `Caster`'s hit from `Skill` landed on `Target` and sent `Sent`: each
	 * of the caster's living minions strikes `Target` for its share, if the
	 * caster holds the option, the skill is not the basic attack and the target
	 * still stands.
	 *
	 * @return how many repeats were dealt
	 */
	static int32 Repeat(AActor* Caster, AActor* Target, float Sent,
						const UGameplayAbility* Skill);
};
