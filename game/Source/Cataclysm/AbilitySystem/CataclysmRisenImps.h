// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmRisenImps.generated.h"

class AActor;
class ACataclysmMinion;
struct FGameplayTagContainer;

/**
 * An enemy that dies carrying a curse a Ritualist laid on it rises as a lesser
 * imp that fights for that Ritualist. Issue #1479.
 *
 * THE RITUALIST'S STARTING NODE, `Ritualist_basic_spine_000`: "An enemy that
 * dies carrying a curse you laid on it rises as a lesser imp that fights for you
 * for 20 seconds, if you hold fewer imps than Summon Imp allows at once." (the
 * node's words before issue #2787, which removed that cap). Added
 * 2026-09-27 under the owner's ruling that the Ritualist is a Wand and Staff
 * caster, so a Wand Ritualist has minions from what the Wand does.
 *
 * THE IMP IS SUMMON IMP'S, READ FROM ITS ROW. Its type, lifetime, burning,
 * explosion radius and reserve come from `Demonic_Staff_Special` in the
 * generated weapon skill table. It shared Summon Imp's cap until issue #2787
 * removed every count cap on what reserves the class resource; its reserve is
 * now the only limit on how many rise.
 *
 * A CURSE IS ANY `Status.Debuff.*` EFFECT WHOSE INSTIGATOR HOLDS THE RULE. The
 * rule is the stat `curse_death_raises_imp` above zero, which only the node
 * grants, so a curse laid by a creature, a trap or a minion raises nothing: none
 * of them holds the stat. A refresh of a running curse keeps its first applier,
 * because `UCataclysmSkillEffects::ApplyTagForDuration` refreshes the running
 * application rather than replacing it.
 *
 * IT RESERVES SUMMON IMP'S FERVOUR, READ FROM THE SAME ROW, since issue #2787.
 * The owner, 2026-11-20: "the minions I create as a Ritualist aren't holding my
 * special resource in reserve, which they should be. All gadgets/traps/minions
 * should hold some reserve." That replaced the rulings of 2026-09-27 and
 * 2026-09-30 that a risen imp reserves nothing because it is not a skill. So a
 * risen imp holds back 10, or 5 under Crowned, and when the pool cannot hold
 * that, nothing rises and nothing is destroyed.
 *
 */
UCLASS()
class CATACLYSM_API UCataclysmRisenImps : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The rule's stat. A flag: the node's row sets it to one. */
	static const TCHAR* RisesStat;

	/**
	 * The weapon skill the imp is read from: Summon Imp, the Demonic Staff's
	 * Special (`Demonic_Staff_Special`), found by weapon, damage type and name.
	 */
	static const TCHAR* ImpSkillWeapon;
	static const TCHAR* ImpSkillDamageType;
	static const TCHAR* ImpSkillName;

	/** How long "Risen" shows on a risen imp's status line. */
	static constexpr float RisenLabelSeconds = 2.0f;

	/**
	 * Who a dying creature rises for: the first holder of the rule who laid a
	 * `Status.Debuff.*` effect still running on it. Null when nobody did.
	 */
	static AActor* CurserOf(const AActor* Dying);

	/**
	 * Whether `Carrier` has a `Status.Debuff.*` effect running that `Curser` laid: its instigator is `Curser`. The
	 * question `target_carries_your_curse` asks, for the demonic tree rework of 2026-11-24 (Subjugator, Long
	 * Servitude).
	 *
	 * THE SAME DEFINITION OF A CURSE AS `CurserOf`, every `Status.Debuff.*` (ruled 2026-09-27), and THE SAME RECORD OF
	 * WHO LAID IT, the effect's instigator. It does NOT ask the rule `curse_death_raises_imp`: "your curse" is about
	 * who laid it, and a curse is yours whether or not its carrier will rise. False for either actor null, a carrier
	 * with no ability system, or one carrying only other people's curses.
	 */
	static bool CarriesCurseLaidBy(const AActor* Carrier, const AActor* Curser);

	/**
	 * Raise the imp for a creature that has just died, if a curse and the
	 * curser's pool allow it. Called once per death from
	 * `ACataclysmEnemyCharacter::HandleDeath`.
	 *
	 * AND MORE THAN ONE WHEN THE CURSER HOLDS `ExtraImpsStat`: Open Channel, the First Pact's third option since the
	 * demonic tree rework of 2026-11-24, "An enemy that dies carrying your curse raises two imps rather than one". Each
	 * imp reserves its own Fervour and is refused on its own when the pool has no room, so a pool with room for one
	 * raises one.
	 *
	 * @return the first imp, or null when nothing rose
	 */
	static ACataclysmMinion* RiseOnDeath(AActor* Dying);

	/**
	 * How many imps a cursed death raises beyond the first, on the curser. Open Channel's row is a flat 1. Whole imps,
	 * rounded down; nought or less raises the one the starting node raises. No gameplay attribute and no base. The
	 * demonic tree rework of 2026-11-24.
	 */
	static const TCHAR* ExtraImpsStat;

	/** How far from the first an extra imp from one death stands, so two are not made in one place. */
	static constexpr float ExtraImpSpacingCm = 100.0f;

	/**
	 * Make one of Summon Imp's kind for `Commander` at `Where`, read from Summon Imp's row as a risen imp is: its type,
	 * lifetime, burning and explosion radius, reserving Summon Imp's Fervour after Crowned and every other reduction.
	 * Null, and nothing made, when the pool has no room for that reserve, when the row cannot be found, or when the
	 * commander is gone.
	 *
	 * WHAT EVERY RITUALIST RULE THAT MAKES A MINION WITH NO SUMMON SKILL IN HAND USES, the demonic tree rework of
	 * 2026-11-24 and the owner's answer that day: "Press-Ganged, Rekindled and the new The Swarm work for a Wand
	 * Ritualist (Dominion stays Staff-only)". A risen imp (`RiseOnDeath`), Press-Ganged's replacement and The Swarm's
	 * kill when no summon skill is held (`UCataclysmSummonSkill::ReplaceLost`, `SummonAtAKillFor`).
	 *
	 * @param bRisen  whether it rose from a cursed death, which shows "Risen" over it (`RisenAtSeconds`)
	 */
	static ACataclysmMinion* SummonImpKindFor(AActor* Commander, const FVector& Where, bool bRisen);

	/** Whether this actor is a risen imp still inside its "Risen" label time. */
	static bool ShowsRisen(const AActor* Actor);

	/**
	 * How many seconds an imp raised by a necrosis kill lasts, on the killer.
	 * Issue #1833 group E part 4b: "Enemies killed by necrosis rise as temporary
	 * minions for 5-10 seconds". Above zero IS the rule being held. No gameplay
	 * attribute.
	 */
	static const TCHAR* NecrosisRiseSecondsStat;

	/**
	 * The Fervour an imp raised by a necrosis kill reserves while it stands, before Crowned. Issue #2787, a number
	 * under the owner's delegation of unstated numbers: half a summoned imp's 10, because it is the same creature and
	 * lasts 5-10 seconds against Summon Imp's 20.
	 */
	static constexpr float NecrosisImpFervourReserve = 5.0f;

	/**
	 * Raise an imp for `Killer` where an enemy it killed with necrosis fell.
	 *
	 * NECROSIS IS READ OFF THE KILLING TAGS, which a lethal tick fills with the
	 * ailment's own tag (`Keyword.DoT.Necrosis`). The killing SKILL'S tags say
	 * what the skill was and not what killed, so they are not asked.
	 *
	 * THE STATED SECONDS ARE ITS LIFETIME, scaled by `minion_duration` as every
	 * summon's is (ruled 2026-10-02).
	 * It is Summon Imp's kind, the one precedent for a raised minion.
	 *
	 * @param Victim       what died; nothing rises unless it was hostile to the killer
	 * @param Where        where it fell
	 * @param KillingTags  the death notice's killing tags, which may be null
	 * @return the imp, or null when the rule is not held, the kill was not
	 *         necrosis, the victim was no enemy, the type could not be found, or
	 *         the killer's pool has no room for `NecrosisImpFervourReserve`
	 */
	static ACataclysmMinion* RiseOnNecrosisKill(AActor* Killer, const AActor* Victim,
												const FVector& Where,
												const FGameplayTagContainer* KillingTags);
};
