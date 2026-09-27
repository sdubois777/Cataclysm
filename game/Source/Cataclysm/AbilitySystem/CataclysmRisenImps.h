// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmRisenImps.generated.h"

class AActor;
class ACataclysmMinion;

/**
 * An enemy that dies carrying a curse a Ritualist laid on it rises as a lesser
 * imp that fights for that Ritualist. Issue #1479.
 *
 * THE RITUALIST'S STARTING NODE, `Ritualist_basic_spine_000`: "An enemy that
 * dies carrying a curse you laid on it rises as a lesser imp that fights for you
 * for 20 seconds, if you hold fewer imps than Summon Imp allows at once." Added
 * 2026-09-27 under the owner's ruling that the Ritualist is a Wand and Staff
 * caster, so a Wand Ritualist has minions from what the Wand does.
 *
 * THE IMP IS SUMMON IMP'S, READ FROM ITS ROW. Its type, lifetime, burning and
 * explosion radius come from `Demonic_Staff_Special` in the generated weapon
 * skill table, and so does its cap, through `UCataclysmSummonSkill::CapFor`, so
 * The Swarm and the minion-count enchantments move this cap exactly as they move
 * Summon Imp's. The count is shared: `UCataclysmCommand::MinionsOfTypeCommandedBy`
 * counts every imp the player holds, from either source.
 *
 * A CURSE IS ANY `Status.Debuff.*` EFFECT WHOSE INSTIGATOR HOLDS THE RULE. The
 * rule is the stat `curse_death_raises_imp` above zero, which only the node
 * grants, so a curse laid by a creature, a trap or a minion raises nothing: none
 * of them holds the stat. A refresh of a running curse keeps its first applier,
 * because `UCataclysmSkillEffects::ApplyTagForDuration` refreshes the running
 * application rather than replacing it.
 *
 * IT RESERVES NO FERVOUR. The ruling that a summon needs unreserved Fervour
 * governs skills that state a `FervourReserve`; this is not a skill. Its limits
 * are the shared cap and the 20 seconds.
 *
 * AT THE CAP NOTHING RISES AND NOTHING IS DESTROYED. The sentence says "if you
 * hold fewer imps", so a full hand is a refusal, not an eviction; only a press
 * of Summon Imp destroys the oldest.
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
	 * Raise the imp for a creature that has just died, if a curse and the cap
	 * allow it. Called once per death from `ACataclysmEnemyCharacter::HandleDeath`.
	 *
	 * @return the imp, or null when nothing rose
	 */
	static ACataclysmMinion* RiseOnDeath(AActor* Dying);

	/** Whether this actor is a risen imp still inside its "Risen" label time. */
	static bool ShowsRisen(const AActor* Actor);
};
