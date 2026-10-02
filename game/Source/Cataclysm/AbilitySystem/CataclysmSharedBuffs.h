// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmSharedBuffs.generated.h"

class AActor;
class UCataclysmAbilitySystemComponent;

/**
 * Gives the allies near a character the damage its buffs grant it, and takes
 * it back. Issue #1833 group E part 4a, three enchantments, ruled 2026-10-02
 * under the owner's delegation:
 *
 *   "Applying a buff to yourself also applies it to all allies within 8 meters"
 *       `self_buff_shared_within_metres`
 *   "Your support ability affects all allies within 15 meters instead of just yourself"
 *       `support_buff_shared_within_metres`
 *   "Nearby allies gain 10-20% more damage"
 *       `nearby_allies_more_damage`, within the 5 m every "nearby" row reads
 *
 * WHAT IS COPIED IS THE MORE DAMAGE A SELF BUFF GRANTS, AND NOTHING ELSE. A
 * running self buff's `GrantedIncrease`, with its `GrantedScope`. Its status
 * tag, its immunities and its own machinery -- Martyr's Ember's store, Coil of
 * Embers' reach, Slipstream's refund -- act on the caster and are not stats,
 * so they stay with the caster. Five of the nine built support buffs grant no
 * More damage, and these rows give their allies nothing.
 *
 * THE ROUTE IS THE AURA'S. `UCataclysmAuraSkill::HelpAlliesInside` already
 * writes a modifier onto each ally's own ability system and takes it back when
 * the ally leaves, and a minion's blow reads that list (issue #1771). A copy
 * here is the same kind of modifier, so it reaches every ally the same way: a
 * minion, a thrall, another player.
 *
 * RUN FROM THE REGENERATION STEP, every quarter second, on every character. A
 * character carrying none of the three stats and holding no copies costs three
 * stat lookups. Ruled 2026-10-02 in place of a one-second timer.
 */
UCLASS()
class CATACLYSM_API UCataclysmSharedBuffs : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Bring the copies `Wearer` has given into line with who stands where now:
	 * give one to each ally that has arrived, change one whose buff's value has
	 * moved, take back one whose ally has left, died or gone, or whose buff has
	 * ended. A dead wearer takes every copy back.
	 *
	 * @return how many copies the wearer holds afterwards.
	 */
	static int32 Step(AActor* Wearer);

	/**
	 * Take back every copy `Wearer` has given. Called by a dead wearer's step
	 * and when its ability system ends play, so no copy outlives its giver.
	 *
	 * @return how many were taken back.
	 */
	static int32 TakeBackAll(UCataclysmAbilitySystemComponent* Wearer);
};
