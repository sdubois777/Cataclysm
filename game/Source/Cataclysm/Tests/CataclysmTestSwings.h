// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystemComponent.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Finishing the swings a caster's skills are waiting on, in a test world. Issue #2811.
 *
 * WHY THIS EXISTS. A skill that swings waits for the swing to connect before its blow lands
 * (`UCataclysmSkillTemplate::WhenTheSwingConnects`, issue #1133), on a timer in the world's timer manager, which no
 * automation test's world runs by itself. The player's tests finish such a swing by moving the world's clock with
 * `CataclysmTestWorld::RunClock` until `IsWaitingForTheSwingToConnect` says no (`CataclysmBasicReachTests.cpp`,
 * `ReachFinishTheSwing`). Since issue #2811 the Corrupted Stalker and the Worn Residue double swing as the player does,
 * and their blows start through `UCataclysmTriggeredSkill::Trigger` on a one-use grant the test never holds, so this
 * finds every waiting skill of the caster on its ability system instead of being handed one.
 *
 * THE WAIT IS FINISHED, NEVER SKIPPED: the clock is moved and the timer fires the blow, as a running game would.
 */
namespace CataclysmTestSwings
{
	/** Whether any skill `Caster` holds is waiting for its swing to connect. */
	inline bool IsWaitingForASwing(const AActor* Caster)
	{
		const UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Caster);
		if (!Abilities)
		{
			return false;
		}
		for (const FGameplayAbilitySpec& Spec : Abilities->GetActivatableAbilities())
		{
			const UCataclysmSkillTemplate* Template = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
			if (Template && Template->IsWaitingForTheSwingToConnect())
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Moves `World`'s clock on a twentieth of a second at a time, firing its timers, until no skill of `Caster` waits
	 * for its swing, for at most five seconds. Whether every swing connected.
	 *
	 * A TWENTIETH AND NOT A TENTH, so the clock stops at most a twentieth past the blow: the timers it fires on the way
	 * include every character's regeneration step, a quarter of a second apart.
	 */
	inline bool FinishTheSwingsOf(UWorld* World, const AActor* Caster)
	{
		for (int32 Step = 0; Step < 100 && IsWaitingForASwing(Caster); ++Step)
		{
			CataclysmTestWorld::RunClock(World, 0.05f);
		}
		return !IsWaitingForASwing(Caster);
	}
}
