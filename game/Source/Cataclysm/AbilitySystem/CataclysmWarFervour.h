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

	/** Fervour for each critical strike the character deals. The Berserker's, and the Shadow's: THE DEATH LINE'S NAME,
	 *  `UCataclysmFervour::PerCriticalStrikeStat`, bound by reference since the bundle 3 merge (obligation 5), so the
	 *  two cannot name two stats. A reference to a static is bound before any code runs, whatever order the two files
	 *  are initialised in. */
	static const TCHAR* const& PerCriticalStrikeStat;

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
	 *  ("on every critical melee strike") adds only to a melee critical strike. HANDS ON TO
	 *  `UCataclysmFervour::GainOnCriticalStrike` with no character struck, since the Death trees' bundle 3 merge
	 *  (obligation 5): the two lines read one stat, and the game pays it once, from the Death line's reader. */
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
	 * NOR WHILE A RUNNING BUFF HOLDS FERVOUR: `UCataclysmSkillTemplate::ARunningBuffHoldsFervour` (the Sword's Cold
	 * Blood, "while it lasts your Fervour does not decay"), asked after the out-of-combat refusal, as the Ravager's
	 * decay asks it after its cheap refusals. Added by the Death skills merge (obligation 10), as agreed with the
	 * coordinating session on 2026-11-24.
	 *
	 * @return the change to the pool, nought or below
	 */
	static float OutOfCombatDecayStep(ACataclysmCharacterBase* Character, float SecondsInStep);

	//~ THE FERVOUR SPENDS, `plan_death_and_war.md`, 2026-11-25. Each is a figure read through the stat pipeline at the
	// moment it applies, so the condition its row is stated under ("While above 75 Fervour", "if you have at least 50
	// Fervour", "Two-handed weapons only") is judged then, and a spend takes no more than the pool holds.

	/**
	 * Iron Will (`Bulwark_basic_ic_resolve_001`): "While above 75 Fervour, blocked attacks automatically negate all
	 * damage from that hit, draining 20 Fervour per block. Per point: reduces the Fervour drain by 2." Fervour taken for
	 * each block that negated its blow, in points: the rows are 20 once and -2 a point, under the 75 Fervour condition.
	 */
	static const TCHAR* CostPerNegatedBlockStat;

	/** Take `CostPerNegatedBlockStat` from the pool, at most what it holds. Call it for a block that negated its blow,
	 *  where the block is announced and before the block's own Fervour gain. @return what was taken, nought or more */
	static float SpendOnNegatedBlock(UAbilitySystemComponent* AbilitySystem);

	/**
	 * Undying (`Berserker_leech_004`): "When you would be reduced to 0 HP, if you have at least 50 Fervour, you are
	 * instead reduced to 1 HP. All of your Fervour is instantly spent, restoring HP equal to 5% of your maximum HP per 10
	 * Fervour spent." The share of maximum health restored for each whole 10 Fervour spent; the row is stated under
	 * `class_resource_points_at_least` 50. THE 10 IS THE STAT'S UNIT, as 1% of maximum health is `fervour_from_damage`'s.
	 */
	static const TCHAR* LethalBlowHealthPerTenFervourStat;

	/**
	 * Undying's save: when `LethalBlowHealthPerTenFervourStat` holds, spend the whole pool and work out the health it
	 * restores, whole steps of 10 rounded down (the 2026-08-25 rule that a bonus counts whole steps).
	 * @return the health to restore after the blow leaves 1, or below nought when there is no save
	 */
	static float SpendAllOnLethalBlow(UAbilitySystemComponent* AbilitySystem);

	/**
	 * Cleave (`Berserker_twoh_004`): "Each enemy hit beyond the first costs 5 Fervour and generates 3 Fervour per point if
	 * Fervour was available to pay the cost." The cost for each enemy an attack strikes beyond the first, and the
	 * Fervour gained for each one paid for; both on `Type.Melee`, stated under `wielding_two_handed_weapon`.
	 */
	static const TCHAR* CostPerExtraEnemyHitStat;
	static const TCHAR* GainPerExtraEnemyHitPaidStat;

	/**
	 * Cleave's trade, once for an attack, before its first blow: for each enemy beyond the first, in turn, the cost is
	 * paid if the pool holds it and the gain then added (scaled by `class_resource_generation`, clamped); an enemy the
	 * pool cannot pay for gains nothing. Read with the attack's skill tags. @return the change to the pool
	 */
	static float TradeForEnemiesStruckTogether(UAbilitySystemComponent* AbilitySystem, const FGameplayTagContainer& SkillTags,
											   int32 EnemiesStruck);

	/**
	 * Blood Tithe (`Bulwark_basic_jg_resolve_001`): "Lose 5 Fervour for every 100 flat HP regenerated. ... At 6 points:
	 * The Fervour cost is reduced to 1 per 150 HP regenerated." The Fervour lost for each whole step of health
	 * regenerated, and the step in points of health: 5 and 100 at one point, 1 and 150 from six.
	 */
	static const TCHAR* LostToRegenerationStat;
	static const TCHAR* RegenerationPerLossStat;

	/**
	 * Blood Tithe's loss: for healing tagged as regeneration that restored `Restored` health, count it towards the step
	 * and take the loss for every whole step, carrying the rest to the next. Called by `UCataclysmRegeneration::TopUp`
	 * beside the Masochist's removal for healing, with what fit. @return what was taken, nought or more
	 */
	static float LoseForRegeneration(UAbilitySystemComponent* AbilitySystem, float Restored,
									 const FGameplayTagContainer& Healing);

	//~ RIPOSTE, VENGEFUL DISCHARGE AND CRIMSON TIDE, 2026-11-25, on the coordinating session's rulings of that day.

	/**
	 * Riposte (`Bulwark_keystone_ic_t1`): "After a successful block, if you have at least 25 Fervour, automatically unleash a
	 * counter-attack dealing damage equal to 75% of the damage just blocked, draining 25 Fervour. While at maximum Fervour
	 * this counter-attack also stuns the target for 1 second." The share of the damage blocked (stated under at least 25
	 * Fervour), the Fervour it costs, and the stun's seconds (stated under the pool at its maximum).
	 */
	static const TCHAR* RiposteShareOfBlockedStat;
	static const TCHAR* RiposteFervourCostStat;
	static const TCHAR* RiposteStunSecondsAtMaximumStat;

	/**
	 * Riposte as one action, after a block that removed something and names its attacker: both figures read FIRST, on the
	 * pool as the block left it, then the cost taken, the counter-attack dealt (as `reflect_blocked` delivers it:
	 * unscaled, no critical strike, no leech, no retaliation back) and the stun laid. So taking the 25 can never refuse
	 * the counter-attack it pays for, nor the stun the full pool earned. The coordinating session's ruling of 2026-11-25.
	 * @return the damage offered to the attacker
	 */
	static float RiposteOnBlock(UAbilitySystemComponent* AbilitySystem, AActor* Attacker, float DamageBlocked);

	/**
	 * Vengeful Discharge (`Bulwark_basic_tw_resolve_001`, the War rulings' R7): its own drain, the radius of the pulse
	 * the drain sets off when it empties the pool, and a flag that the pulse lays Bleed (from 5 points).
	 */
	static const TCHAR* PulseDrainPerSecondStat;
	static const TCHAR* PulseRadiusMetresStat;
	static const TCHAR* PulseAppliesBleedStat;

	/**
	 * One step of Vengeful Discharge's drain; when THIS drain takes the pool from above nought to nought, the retaliation
	 * pulse (`UCataclysmRetaliation::PayPulse`). Any other emptying of the pool sets nothing off: "When THIS drain reduces
	 * you to 0" (the coordinating session's ruling of 2026-11-25). Refuses a creature holding a player build.
	 * @return the change to the pool, nought or below
	 */
	static float RetaliationPulseDrainStep(ACataclysmCharacterBase* Character, float SecondsInStep);

	/**
	 * Crimson Tide (`Berserker_leech_001`): "Every 10 Fervour you spend or drain generates a heal equal to 1% of your
	 * maximum HP for every 2 points." The share of maximum health healed for each whole 10 spent or drained; 10 is the
	 * stat's unit, as it is Undying's.
	 */
	static const TCHAR* HealthPerTenFervourSpentStat;

	/**
	 * Count Fervour spent or drained towards Crimson Tide's heal, and heal for every whole 10, the rest carried. CALLED
	 * BY EVERY SPEND AND DRAIN, and by no decay (the coordinating session's ruling of 2026-11-25: "spends and drains
	 * count, decay does not"): `TakeFromPool`, Cleave's costs, `FCataclysmBerserkerStates`' drain and Berserking, this
	 * file's pulse drain, the Ultimate's Fervour cost (`UCataclysmGameplayAbility`) and a pool action's drain
	 * (`UCataclysmAbilitySystemComponent::ApplyPoolAction`). NOT the Masochist's and Ravager's spends in
	 * `UCataclysmFervour`, which the Death trees session owns; a Berserker holding a Demonic node beside Crimson Tide
	 * would not count those.
	 *
	 * NAMED FOR WHAT IT COUNTS, SPENDS AND DRAINS: renamed from `NoteFervourSpent` on 2026-11-25 at the coordinating
	 * session's request, because the Death trees' line records a skill's Fervour cost alone under that name, for the
	 * clock `seconds_after_fervour_spent` and the event `fervour_spent`. The two records differ on purpose.
	 * @return the health restored
	 */
	static float NoteFervourSpentOrDrained(UAbilitySystemComponent* AbilitySystem, float Spent);

	//~ SKULL SPLITTER (`Berserker_twoh_003`), on the coordinating session's rulings of 2026-11-25: "While in Wrath,
	// critical strikes drain an additional 3 Fervour per hit in exchange for applying a 2-second armor reduction of 10%
	// per point on the target. At 5 points: This armor reduction stacks up to 6 times instead of 4." The percent a stack
	// and the cost, each stated under `in_wrath` and `wielding_two_handed_weapon`; the cap (4, and 6 from 5 points, by
	// Max Points and Min Points); the seconds.
	static const TCHAR* CritArmorRemovedInWrathStat;
	static const TCHAR* CritArmorFervourCostStat;
	static const TCHAR* CritArmorStackCapStat;
	static const TCHAR* CritArmorSecondsStat;

	/**
	 * Skull Splitter at a critical strike on `Target`. With the percent and the seconds above nought and the pool able to
	 * pay the whole cost, the cost is taken and one stack placed on the target through `ReceivePlacedStack`, as an
	 * `enemy_armor_removed` row places one, so the creature's status line shows it as armour removed. A POOL THAT CANNOT
	 * PAY THE WHOLE COST PAYS NOTHING AND PLACES NOTHING: the ruling on question 1, "in exchange for".
	 * @return the percent of the stack placed, or nought
	 */
	static float SkullSplitterOnCriticalStrike(UAbilitySystemComponent* AbilitySystem, const AActor* Target,
											   const FGameplayTagContainer& SkillTags);

	//~ DEATHMARK (`Berserker_crit_004`): "Critical strikes apply a Deathmark for 5 seconds. Applying a Deathmark costs 10
	// Fervour, reduced by 1 per point. Marked enemies take +3% increased damage per point from all sources. If you have no
	// Fervour the mark still applies but deals no bonus damage." The mark's percent, its cost (10 once and -1 a point),
	// and its seconds.
	static const TCHAR* DeathmarkPercentStat;
	static const TCHAR* DeathmarkFervourCostStat;
	static const TCHAR* DeathmarkSecondsStat;

	/**
	 * Deathmark at a critical strike on `Target`. With the percent and the seconds above nought, a pool holding the whole
	 * cost pays it and the mark carries the percent; a pool holding less PAYS NOTHING AND THE MARK CARRIES NOUGHT (the
	 * ruling on question 2: less than the cost is none, as the sentence says for none). Either way the mark is laid, or
	 * refreshed, for its seconds (`UCataclysmAbilitySystemComponent::MarkWithDeathmark`). @return the percent the mark
	 * carries, or -1 when none was laid
	 */
	static float DeathmarkOnCriticalStrike(UAbilitySystemComponent* AbilitySystem, const AActor* Target,
										   const FGameplayTagContainer& SkillTags);

	//~ BLOOD PRICE (`Berserker_trunk_008`): "When your Fervour reaches 0 for any reason, your next melee attack within 3
	// seconds deals bonus damage equal to 5% per point of your maximum Fervour." The percent of the maximum and the
	// seconds.
	static const TCHAR* BloodPricePercentStat;
	static const TCHAR* BloodPriceSecondsStat;

	/**
	 * Blood Price as the pool empties, for any reason (the ruling on question 4: decay included): hold, for its seconds,
	 * damage of its percent of the maximum Fervour for the first melee blow that lands (question 5), which
	 * `UCataclysmSkillEffects::ApplyHit` adds before the increases and spends (`HoldBloodPrice`). @return the damage held,
	 * or nought
	 */
	static float ArmBloodPrice(UAbilitySystemComponent* AbilitySystem);

private:
	/** Take up to `Amount` from the pool, clamped at nought, and return what was taken. */
	static float TakeFromPool(UAbilitySystemComponent* AbilitySystem, float Amount);

	/** The one gain every function above makes: the rate `Stat` grants, read with `Tags`, scaled, clamped, and written
	 *  through `UCataclysmFervour::RaisePool` since the Death trees' bundle 3 merge. */
	static float GainByRate(UAbilitySystemComponent* AbilitySystem, const TCHAR* Stat, const FGameplayTagContainer& Tags);
};
