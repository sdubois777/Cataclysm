// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmWoundBurst.generated.h"

class AActor;
class UCataclysmSkillTemplate;

/**
 * A creature that dies while a skill's wound still runs on it bursts. Issue #2885, the Death draft's N14, slice DS5:
 * `OnDeath=Burst; BurstPercentOfMaximumHealth=N; OnDeathRange=M` beside the row's `Ailment`.
 *
 * WHAT ASKS FOR IT. The Fist's Gravetouch, approved by the owner on 2026-11-24: "Lay a hand of grave-cold on one enemy
 * and open a wound that Bleeds. If it dies while the wound still bleeds, it bursts, dealing 10% of its maximum health to
 * every enemy within 4 meters." Its precedent in the draft is Diablo III's Exploding Palm (unverified: a search summary
 * of diablowiki.net, whose page answered 403) and The Ferryman vow's 10%.
 *
 * THE WOUND IS THE RUNNING AILMENT ITSELF, NOT A MARK BESIDE IT, a judgement of slice DS5. Slice DS2 sized this piece
 * as a mark the strike leaves, a component as `UCataclysmCurseSpread` is. But the design gives every ailment one stack
 * on a target (`TagAndReplaceAnyExisting`), so the bleed the strike laid is either still the one running -- refreshed,
 * perhaps, by a weaker or equal application, which keeps it running -- or it has been replaced by a stronger one from
 * another source, cleansed, or run out. The running effect already names the skill that laid it (its context's
 * ability, set by `UCataclysmSkillEffects::ApplyDamageOverTime`) and its instigator, so "while the wound still bleeds"
 * is read off it, and a component kept beside it could only disagree with it. A bleed replaced by a STRONGER one of
 * ANOTHER SOURCE'S is no longer this skill's wound and nothing bursts; one this caster laid with another skill (Severing
 * Rush) and that replaced it names that skill, which states no burst. The one-stack rule `UCataclysmKillFervour::
 * CarriesACurseLaidBy` reads for a curse, read for an ailment.
 *
 * WHAT THE BURST IS, judgements of slice DS5 beside the draft's figures:
 *   - THE DYING CREATURE'S OWN MAXIMUM HEALTH, read as it dies, times the row's share, sent as it stands: the share
 *     the sentence states, as Shared Ruin's death blast and the overkill explosion send theirs. No increase of the
 *     caster's raises it and no skill level scales it; the struck creatures' defences meet it.
 *   - DEALT IN THE CASTER'S NAME, at every enemy of the caster's within the row's own `OnDeathRange` of where the
 *     creature fell, the creature itself excluded. NOT WIDENED BY AREA OF EFFECT, as no death blast in the game is.
 *   - THE CONSEQUENCE OF A DEATH (`FCataclysmHitDelivery::bIsConsequenceOfADeath`), as the overkill explosion and
 *     Contagion's blast are: area damage, never a critical strike, no weapon sub-type, no leech, not retaliated, and
 *     it fires none of the caster's on-hit rows.
 *   - IT NAMES THE SKILL WHOSE WOUND IT IS (`FCataclysmHitDelivery::Skill`), so a death it causes is that skill's
 *     kill in the hit and death notices -- "it bursts" is the second half of Gravetouch's own sentence. It credits no
 *     skill experience row of its own, as the overkill explosion and Contagion's blast credit none: the wound's ticks
 *     already earned the skill its share of the creature it killed.
 *   - A BURST'S KILL CAN BURST IN TURN, when the creature it kills carries a wound of its own: that death comes back
 *     through `UCataclysmSkillEffects::MarkDead`, which refuses a death already being marked (issue #2871).
 *
 * NO STATE OF ITS OWN. Every figure is read off the skill the running effect names and off the dying creature.
 */
UCLASS()
class CATACLYSM_API UCataclysmWoundBurst : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Burst `Dying` if it carries a running ailment laid by a skill whose row states `OnDeath=Burst`, and answer how
	 * many enemies the burst struck.
	 *
	 * CALLED FROM `UCataclysmSkillEffects::MarkDead`, beside the curse spread and the buried weapon, before the death is
	 * recorded: the running effects are still on the dying creature to be read, and it is excluded from the search by
	 * name, as `UCataclysmCurseSpread::SpreadFromDying` excludes it.
	 *
	 * NOTHING UNLESS: a running effect on `Dying` grants the tag of the ailment its skill's row names, that skill states
	 * `OnDeath=Burst` with a share and a reach above nought, the effect's instigator is the skill's own caster, still
	 * here, and the caster was the dying creature's enemy. ONE BURST A DEATH, the first such wound found.
	 *
	 * @return the enemies the burst struck: nought for every death but a wounded one
	 */
	static int32 BurstFromDying(AActor* Dying);

	/**
	 * The skill whose wound `Dying` carries and whose row bursts it, or null: the test `BurstFromDying` makes before
	 * it deals anything. Public so a test can ask without a death.
	 */
	static const UCataclysmSkillTemplate* WoundThatBursts(const AActor* Dying);
};
