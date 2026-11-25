// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UAbilitySystemComponent;
class ACataclysmCharacterBase;

/**
 * How the three War classes fill Fervour, and how two of them lose it. `plan_death_and_war.md` slice C (B26 in its
 * list), 2026-11-24: "A character in War trees alone cannot cast an Ultimate", because no War tree filled Fervour and
 * every class's Ultimate costs 50.
 *
 * THE STARTING NODES' OWN WORDS:
 *   - Bulwark: "Taking hits, blocking and killing enemies generate Fervour. While you have this, your Fervour decays
 *     slowly out of combat." The owner's Q21 answer of 2026-11-24 sized the decay: 5 a second after 3 seconds out of
 *     combat.
 *   - Berserker: "Your critical strikes generate Fervour. Gain 1 Fervour per critical strike. While you have this, your
 *     Fervour decays at 10 per second after 3 seconds out of combat."
 *   - Saboteur: "Placing a trap or a gadget generates Fervour, and so does one of them dealing damage or triggering. It
 *     adds no rule about emptying, so your Fervour does not decay on its own."
 *
 * A FILE OF ITS OWN AND NOT `UCataclysmFervour`'s, at the coordinating session's request of 2026-11-24: the Death trees
 * session is adding per-kill Fervour to `CataclysmFervour.cpp` at the same time, and the two must not collide.
 *
 * EACH GAIN IS A FLAT RATE A STARTING NODE GRANTS, read through the stat pipeline with the event's tags (fallback
 * nought, so a scoped or conditioned row is read where it applies) and written by the rule every gain in
 * `UCataclysmFervour` keeps: scaled by `class_resource_generation`, clamped to the most the pool may hold after its
 * reserves (`UCataclysmClassResourceAttributeSet::MaximumClassResourceAsked`), and measured. Nought for every character
 * without the row, which is every character until a point is spent in the node.
 *
 * A PLAIN CLASS OF STATIC FUNCTIONS, not a UCLASS: nothing in Blueprint calls these, and none of them is a UFUNCTION.
 */
class CATACLYSM_API FCataclysmWarFervour
{
public:
	/** Fervour for each hit taken that landed and was not a damage over time tick. The Bulwark's. */
	static const TCHAR* PerHitTakenStat;

	/** Fervour for each block. The Bulwark's. */
	static const TCHAR* PerBlockStat;

	/** Fervour for each critical strike the character deals. The Berserker's. */
	static const TCHAR* PerCriticalStrikeStat;

	/** Fervour for each skill trap the character places. The Saboteur's. */
	static const TCHAR* PerTrapPlacedStat;

	/** Fervour for each gadget the character places: a skill gadget, or a War machine that is not a trap. */
	static const TCHAR* PerGadgetPlacedStat;

	/** Fervour for each time one of the character's skill traps triggers. The Saboteur's. */
	static const TCHAR* PerTrapTriggeredStat;

	/** Fervour for each landed blow one of the character's traps, gadgets or machines deals. The Saboteur's. */
	static const TCHAR* PerDeviceHitStat;

	/**
	 * Fervour for each blow the character itself lands. The Berserker's Frenzy, "all melee hits generate 2 additional
	 * Fervour", as a row scoped to `Type.Melee` and stated under `in_frenzy`. The Berserker's states, 2026-11-25.
	 */
	static const TCHAR* PerHitDealtStat;

	/** How much Fervour leaves a second once the character has been out of combat for `CombatLapseSeconds`. The
	 *  Bulwark's 5 and the Berserker's 10. */
	static const TCHAR* OutOfCombatDecayPerSecondStat;

	//~ THE GADGET-PLACED EVENT. `gadget_placed` is raised on a character's ability system when it places a gadget,
	// with the gadget's type tags and the gadget as the event's character, by `ACataclysmMinion::PlaceSkillGadget` and,
	// for a War machine that is not a trap, `UCataclysmDeployableSkill::DeployOne`. NEW WITH THIS SLICE: nothing raised
	// one before, and two Saboteur nodes wait on it: Razor Instincts ("+3% evasion chance per point for 3 seconds after
	// placing a trap or gadget") and Forward Operating Base ("When you deploy a gadget, all other active gadgets are
	// pulled to the most recent gadget"). Clockless: placing is something the player DID, as `trap_placed` is. Written
	// as a literal at each call, `FName(TEXT("gadget_placed"))`, which `tools/tests/test_pool_action_names_match_the_engine.py`
	// reads to hold every fired event to the generator's list.

	/** Every gain stat above, in one list. */
	static const TArray<const TCHAR*>& GainStats();

	/**
	 * Whether the character holds any row granting one of `GainStats`, whatever its tags or condition: a War Fervour
	 * generator. For the Fervour bar, which `UCataclysmCombatOverlay::FervourOf` draws only for a character with a
	 * generator. FOUND 2026-11-25 with the Berserker's states: `UCataclysmFervour::HasAGenerator` asks attributes, and
	 * none of these stats has one, so a War character holding its starting node drew no bar. Here and not in
	 * `UCataclysmFervour`, which the Death trees session is editing (the coordinating session's request of 2026-11-24).
	 */
	static bool HasARate(const UAbilitySystemComponent* AbilitySystem);

	/** The Bulwark's gain on a hit taken. Call it for a blow that landed and was not a damage over time tick. */
	static float GainOnHitTaken(UAbilitySystemComponent* AbilitySystem);

	/** The Bulwark's gain on a block. */
	static float GainOnBlock(UAbilitySystemComponent* AbilitySystem);

	/** The Berserker's gain on a critical strike it dealt, read with the blow's skill tags, so a row scoped to melee
	 *  ("on every critical melee strike") adds only to a melee critical strike. */
	static float GainOnCriticalStrike(UAbilitySystemComponent* AbilitySystem, const FGameplayTagContainer& SkillTags);

	/** The Saboteur's gain on placing a trap, read with the trap's type tags. */
	static float GainOnTrapPlaced(UAbilitySystemComponent* AbilitySystem, const FGameplayTagContainer& TrapTags);

	/** The Saboteur's gain on placing a gadget, read with the gadget's type tags. */
	static float GainOnGadgetPlaced(UAbilitySystemComponent* AbilitySystem, const FGameplayTagContainer& GadgetTags);

	/** The Saboteur's gain on one of its traps triggering, read with the trap's type tags. */
	static float GainOnTrapTriggered(UAbilitySystemComponent* AbilitySystem, const FGameplayTagContainer& TrapTags);

	/** The Saboteur's gain on a landed blow by one of its traps, gadgets or machines, read with that blow's tags. */
	static float GainOnDeviceHit(UAbilitySystemComponent* AbilitySystem, const FGameplayTagContainer& DeviceTags);

	/**
	 * The gain on a blow the character itself landed, read with the blow's skill tags, so a row scoped to melee adds
	 * only to a melee blow. Called where `hit_dealt` is raised, `ACataclysmPlayerCharacter::OnSomethingWasHit`, which
	 * refuses damage over time ticks, evaded blows and blows dealt by anyone else first. The Berserker's states.
	 */
	static float GainOnHitDealt(UAbilitySystemComponent* AbilitySystem, const FGameplayTagContainer& SkillTags);

	/**
	 * One step of the Bulwark's and the Berserker's decay: `OutOfCombatDecayPerSecondStat` times the step, once
	 * `UCataclysmAbilitySystemComponent::SecondsOutOfCombat` is at or above nought, which is 3 seconds
	 * (`CombatLapseSeconds`) after the last blow the character dealt or took. That clock's own comment asks the
	 * Berserker's decay to read it rather than a 3 of its own.
	 *
	 * NOT THE RAVAGER'S `UCataclysmFervour::DecayStep`, whose grace is about an enemy standing near, not about combat.
	 * The two are separate stats, so a character holding both rows meets both rules.
	 *
	 * NOT YET ASKING WHETHER A RUNNING BUFF HOLDS FERVOUR. `UCataclysmSkillTemplate::ARunningBuffHoldsFervour` (the
	 * Sword's Cold Blood, "while it lasts your Fervour does not decay") is on `feat/death-skills-2885` and not on the
	 * branch this was written on. Agreed with the coordinating session on 2026-11-24: whichever of the two merges into
	 * `development` second adds the one call here, before the pool is read, and a test for it. `docs/DECISIONS.md`
	 * records the obligation.
	 *
	 * @return the change to the pool, nought or below
	 */
	static float OutOfCombatDecayStep(ACataclysmCharacterBase* Character, float SecondsInStep);

private:
	/** The one gain every function above makes: the rate `Stat` grants, read with `Tags`, scaled, clamped, written. */
	static float GainByRate(UAbilitySystemComponent* AbilitySystem, const TCHAR* Stat, const FGameplayTagContainer& Tags);
};
