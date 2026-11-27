// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmDetonation.generated.h"

class AActor;
class ACataclysmMinion;
class UCataclysmSkillTemplate;

/**
 * A minion of the Necromancer's DETONATED, and everything the Charnel limb and the Second Rite's Living Bombs do to a
 * detonation. The finishing of the Death Cataclysm, slice C (2026-11-26): Lit Fuse, Made Room, Close Packed, Lingering
 * Rot, Bursting Ranks, Swollen, Ripened, Mass Grave, Last Breath and Living Bombs. `docs/DECISIONS.md` carries the
 * judgements.
 *
 * WHAT A DETONATION IS. A minion a skill sets off (`Detonates=1`, or any weapon skill under Lit Fuse), a minion released
 * to make room under Made Room, a minion that reaches its enemy under Living Bombs, and every minion Mass Grave's chain
 * sets off. Each goes through `Detonate`, which is the one place a detonation is told to its caster (`NoteMinionDied`,
 * Marrow Feast's "dies or is detonated"), blows up (`ACataclysmMinion::ExplodeSetOffBy`, 200% of the caster's weapon
 * damage as the skill's area hit), and then pays what the Charnel limb adds to it. NOT A DETONATION: a minion's death
 * explosion (Every One Bursts, `ACataclysmMinion::Explode`) and Ripened's "explode anyway", which the node itself sets
 * against "being detonated".
 *
 * ONLY A RAISED MINION, as `UCataclysmSkillTemplate::DetonateOwnMinionsWhere` has always taken: an `ACataclysmMinion`
 * that is no machine. A thrall is an enemy taken, with no explosion of its own (slice DS2's judgement, kept).
 *
 * NO STATE OF ITS OWN BUT A RE-ENTRY GUARD. Every figure is a stat on the summoner, read when it is needed.
 */
UCLASS()
class CATACLYSM_API UCataclysmDetonation : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------- the stats, each with no gameplay attribute

	/** Lit Fuse, `Necromancer_basic_ch_stem0`: above nought, every weapon skill detonates the minions in its area. */
	static const TCHAR* WeaponSkillsDetonateOwnMinionsStat;

	/** Made Room, `Necromancer_basic_ch_stem1`: above nought, a minion released to make room detonates as it goes. */
	static const TCHAR* DetonatesWhenReleasedStat;

	/**
	 * A detonation's damage, increased: Made Room's 2% a point, and Close Packed's, Bursting Ranks' and Swollen's
	 * scaled rows. Asked of the summoner with the minion's type tags AND THE MINION ITSELF, so a row scaled by
	 * `minion_seconds_active` or `minions_near_the_minion` reads the minion going off.
	 */
	static const TCHAR* DetonationDamageStat;

	/** A detonation's area, increased: Made Room's 2% a point. Widens the radius as area of effect widens a skill's. */
	static const TCHAR* DetonationAreaStat;

	/** Lingering Rot: the seconds a detonation's rotting ground lasts (1 for every 2 points), and what it deals each
	 * second, in per cent of the detonation's damage (10). Both must stand above nought. */
	static const TCHAR* RotSecondsStat;
	static const TCHAR* RotPercentStat;

	/** Ripened, `Necromancer_basic_ch_b2`: the chance in per cent that a minion whose lifespan ends explodes anyway. */
	static const TCHAR* ExplodesAtLifespanEndChanceStat;

	/** Mass Grave, `Necromancer_keystone_ch_kA`: the share each other minion within the radius detonates for (40), and
	 * that radius in metres (3). Both must stand above nought. */
	static const TCHAR* SpreadPercentStat;
	static const TCHAR* SpreadRadiusMetresStat;

	/** Last Breath, `Necromancer_keystone_ch_kC`: Fervour gained for a detonated minion, as a multiple of what it
	 * reserved (2, "twice"). */
	static const TCHAR* FervourPerReserveMultipleStat;

	/** Living Bombs, the Second Rite's option 2: above nought, a minion's attack is its detonation; the chance in per
	 * cent that a detonation raises a husk (50); and how many seconds that husk lasts (8). */
	static const TCHAR* DetonateOnContactStat;
	static const TCHAR* RaisesHuskChanceStat;
	static const TCHAR* HuskSecondsStat;

	/**
	 * Teeming Grave's stat, and Rise Together's (slice B), ONE MECHANISM FOR BOTH as the coordinating session ruled on
	 * 2026-11-26: the chance in per cent that a summon skill raising a minion raises one more. Read by
	 * `UCataclysmSummonSkill::RaiseOneMoreIfRolled`, with the skill's tags and the raised minion's, so a row naming a
	 * kind of minion reaches only it.
	 */
	static const TCHAR* ExtraRaiseChanceStat;

	/**
	 * The action event a detonation raises on its caster, AFTER the blast, with the detonated thing's tags and no
	 * character: Bursting Ranks' own stacks, "for each detonation of yours in the last 2 seconds, of a minion or of
	 * remains". See `NoteDetonation`.
	 */
	static const TCHAR* DetonatedEvent;

	// ---------------------------------------------------------------- the figures Living Bombs and Made Room need

	/**
	 * How near a Living Bomb must come to its enemy to go off, in centimetres beyond the two bodies: "detonate on
	 * contact". A judgement: half a metre, so a minion that reaches an enemy's body goes off and one that only aims at it
	 * from afar does not. A ranged minion's reach is cut to this, so it runs in as the vow says.
	 */
	static constexpr float ContactAllowanceCm = 50.0f;

	// ---------------------------------------------------------------- the reads

	/** A stat of `Summoner`'s, asked with `Tags`, or nought when it has no stat line. */
	static float SummonerStat(const AActor* Summoner, const TCHAR* Stat,
							  const FGameplayTagContainer& Tags = FGameplayTagContainer());

	/** Whether `Caster` holds Lit Fuse. */
	static bool WeaponSkillsDetonateOwnMinions(const AActor* Caster);

	/**
	 * What a detonation of `Minion` deals, as a multiplier: its summoner's `minion_detonation_damage`, asked with the
	 * minion's type tags and the minion itself. One when nothing is held. Taken on top of `minion_explosion_damage`,
	 * as `ACataclysmMinion::Explode` takes that on top of `minion_damage`.
	 */
	static float DamageMultiplierFor(const ACataclysmMinion* Minion);

	/** What a detonation of `Minion` reaches, as a multiplier of its radius: its summoner's `minion_detonation_area`. */
	static float AreaMultiplierFor(const ACataclysmMinion* Minion);

	/** Whether `Minion` is a Living Bomb: a raised minion, no machine, whose summoner holds the vow. */
	static bool IsALivingBomb(const ACataclysmMinion* Minion);

	/**
	 * `Minion`'s attack reach while it is a Living Bomb: what it reaches from its own body's edge cut to
	 * `ContactAllowanceCm`, so a ranged minion runs in. `Reach` otherwise, unchanged.
	 */
	static float AttackReachCmFor(const ACataclysmMinion* Minion, float Reach);

	// ---------------------------------------------------------------- the acts

	/**
	 * Detonate `Minion`: told to its summoner as a minion's death (`NoteMinionDied`), blown up as `SetOffBy`'s area hit
	 * at `SharePercent` of the detonation's figure (`ACataclysmMinion::ExplodeSetOffBy`), then, in this order, Last
	 * Breath's Fervour, Lingering Rot's ground, Living Bombs' husk, `NoteDetonation` (Bursting Ranks), and Mass Grave's
	 * chain at its share. Answers whether it went off.
	 *
	 * @param SetOffBy      the skill whose hit the blast is; null for the skill that raised the minion
	 *                      (`ACataclysmMinion::SummonedBy`), the judgement for Made Room ("with the radius and damage of
	 *                      the skill that raised it") and Living Bombs. With neither, the minion's own explosion
	 *                      (`ACataclysmMinion::Explode`), and nothing that needs the detonation's figure follows.
	 * @param SharePercent  100 for a whole detonation; Mass Grave's 40 for each it chains to
	 * @param bMaySpread    false for a detonation Mass Grave's chain made: the chain is one ring, a judgement
	 */
	static bool Detonate(ACataclysmMinion* Minion, UCataclysmSkillTemplate* SetOffBy, float SharePercent = 100.0f,
						 bool bMaySpread = true);

	/**
	 * Tell `Character` one of its detonations went off: raises `DetonatedEvent` on it, with `Tags`. PUBLIC FOR SLICE
	 * D's REMAINS, whose detonation is the other half of Bursting Ranks' "of a minion or of remains", as the coordinating
	 * session ruled on 2026-11-26. Nothing for a character with no ability system of ours.
	 */
	static void NoteDetonation(AActor* Character, const FGameplayTagContainer& Tags = FGameplayTagContainer());

	/**
	 * Made Room: `Minion`, being released to make room for another, detonates as it goes when its summoner holds the
	 * node. Answers whether it did; false leaves the caller to destroy it as before.
	 */
	static bool DetonateOnRelease(ACataclysmMinion* Minion);

	/**
	 * Ripened: `Minion`'s lifespan has ended and nothing kept it, so it explodes anyway at its summoner's chance. Its
	 * own explosion (`ACataclysmMinion::Explode`), at full damage, and NOT A DETONATION, so none of the detonation nodes
	 * answer it. Answers whether it exploded; false leaves it to go as before.
	 */
	static bool RipenAtLifespanEnd(ACataclysmMinion* Minion);

	/**
	 * Living Bombs: `Minion`, about to strike `Target`, detonates instead when it is a Living Bomb and stands within its
	 * contact reach of the enemy. Answers whether it went off; false leaves the blow to be struck as before.
	 */
	static bool DetonateOnContact(ACataclysmMinion* Minion, const AActor* Target);

	/**
	 * Raise a husk for `Commander` at `Where`, lasting `Seconds`: Living Bombs' "raise a husk that lasts 8 seconds where
	 * it went off". THE HUSK AS ITS RAISING ROW MAKES IT -- the one Death Summon row that raises a Husk
	 * (`UCataclysmRisingCurse::FindTheRowThatRaises`), for its reserve, its explosion radius and its taunt -- for the
	 * sentence's seconds. When the pool cannot hold its reserve, nothing rises. Null when nothing rose.
	 */
	static ACataclysmMinion* RaiseHuskAt(AActor* Commander, const FVector& Where, float Seconds);

	/** How many detonations `Detonate` has made since the world began, chained ones included. Read by tests. */
	static int32 DetonationsMade;

private:
	/** The minions of `Summoner`'s within `RadiusCm` of `Centre`, body included, other than `Except`. */
	static TArray<ACataclysmMinion*> RaisedMinionsAround(const AActor* Summoner, const FVector& Centre, float RadiusCm,
														 const ACataclysmMinion* Except);
};
