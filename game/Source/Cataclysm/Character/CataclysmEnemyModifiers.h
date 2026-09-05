// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmEnemyModifiers.generated.h"

class UDataTable;
struct FCataclysmEnemyModifierRow;

/**
 * Which modifiers a spawned enemy carries.
 *
 * WHAT THIS ANSWERS, AND WHY IT DID NOT EXIST BEFORE. Nothing gave any creature
 * a modifier. `game/Data/EnemyModifiers.csv` has held 79 rows since the design
 * workbook was imported, `ACataclysmEnemyCharacter::ModifierRows` has held a
 * list since issue #740 and `UCataclysmCreaturePanel` has printed it before a
 * fight, and the list was empty on every creature the game ever spawned unless
 * somebody typed one in by hand. So the design's rule had never run. Issue #742.
 *
 * THE COUNT IS THE RARITY STEP, WHICH IS NOT A COINCIDENCE.
 * `docs/Cataclysm_GDD_v2.md` gives the count per rung as 0, 1, 2, 3, 4, 5 for
 * Common, Elite, Legendary, Herald, Boss and Cataclysm Boss, and
 * `game/Data/EnemyRarities.csv` numbers those same six rungs 0 to 5. So the
 * count is the step and no second table is needed. `CountForRarityStep` is
 * where that is written down, and a test checks it against the design's figures
 * one rung at a time rather than against the step it is derived from.
 *
 * THE POOL IS THE CREATURE'S OWN CATACLYSM PLUS GENERIC, AND THE DESIGN SAYS SO.
 * Issue #742 records this as an open design question; it is not one.
 * `docs/Cataclysm_GDD_v2.md` states it in the enemy ability section: "an enemy
 * carries one modifier per rarity above Common, drawn from its own Cataclysm's
 * column and the Generic one." A Demonic creature therefore draws from 18 rows,
 * the 8 Demonic ones and the 10 Generic ones, and can never roll a War or a
 * Void modifier.
 *
 * A STATIC OVER A TABLE AND A STREAM, so it can be tested. This is the shape
 * `UCataclysmEnemyRarity::RollRarityStep` uses and the reason it gives holds
 * here: the automation test command passes `-nullrhi` and spawning an actor
 * needs a world, so a draw that lived on the game mode could only be checked by
 * playing.
 *
 * THE POOL IS SORTED BEFORE ANYTHING IS DRAWN FROM IT, and that is load
 * bearing rather than tidy. A `UDataTable` is a map, so walking one gives no
 * guaranteed order, and an unsorted walk would hand the same seed a different
 * modifier on a different run. `UCataclysmEnemyRarity::SpawnableSteps` sorts for
 * the same reason.
 *
 * WHAT IT DOES NOT DO. It draws names and nothing else. **Whether a modifier
 * does anything is a separate question and mostly the answer is still no**: the
 * effects are being built one at a time, and a creature carrying a name whose
 * effect is not built yet behaves exactly as it did before. The hover panel
 * shows what a creature is supposed to be doing either way, which is what makes
 * the gap visible rather than silent.
 */
UCLASS()
class CATACLYSM_API UCataclysmEnemyModifiers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The column every creature draws from as well as its own.
	 *
	 * TEN OF THE 79 ROWS, and they are the ones that describe no element:
	 * Titanic Resolve is 50% more health, Overpowered always crits, Unyielding
	 * is immune to crowd control. A Demonic creature drawing three modifiers
	 * takes them from 18 rows rather than from 8.
	 */
	static const TCHAR* GenericCataclysm;

	/** The enemy modifier table, loaded once. Null when it cannot be read. */
	static const UDataTable* LoadEnemyModifierTable();

	/**
	 * How many modifiers a creature of this rarity carries.
	 *
	 * @return the step itself, floored at zero. Common carries none.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Enemy")
	static int32 CountForRarityStep(int32 RarityStep);

	/**
	 * Every row key a creature of this Cataclysm may draw, sorted.
	 *
	 * @param CataclysmType  the creature's own, as `ACataclysmEnemyCharacter::
	 *                       DamageType` holds it: `Demonic`, `War`, `Void`
	 * @return its own column and the Generic one, in row-key order. Empty when
	 *         the table is missing.
	 */
	static TArray<FName> PoolFor(const UDataTable* EnemyModifierTable,
								 FName CataclysmType);

	/**
	 * Draw modifiers a creature does not already carry.
	 *
	 * NO DUPLICATES, WHICH IS A JUDGEMENT AND NOT READ OFF THE DESIGN. The
	 * project owner decided it on 2026-09-05. Neither Diablo II's nor Path of
	 * Exile's public documentation states a rule either way, so there was
	 * nothing to copy. The reasoning recorded in `docs/DECISIONS.md` is that
	 * three copies of Hellfire Aura read to a player as one aura that hurts
	 * more, which teaches them nothing about what the creature is doing, and the
	 * hover panel would print the same line three times.
	 *
	 * @param Count    how many to draw. Zero or fewer draws none
	 * @param Already  what the creature carries. Never drawn again
	 * @return up to `Count` distinct keys. Fewer when the pool runs out, which
	 *         cannot happen today: the smallest pool is Famine's 7 plus the 10
	 *         Generic ones, and the most anything draws is 5
	 */
	static TArray<FName> Draw(const UDataTable* EnemyModifierTable,
							  FName CataclysmType, int32 Count,
							  FRandomStream& Stream,
							  const TArray<FName>& Already);

	/** One row by key, or null when the table or the key is missing. */
	static const FCataclysmEnemyModifierRow* FindRow(
		const UDataTable* EnemyModifierTable, FName Key);

	// ----------------------------------------------------------------------
	// The modifiers whose effects are built
	// ----------------------------------------------------------------------

	/**
	 * The row keys of the modifiers that do something.
	 *
	 * NAMED CONSTANTS RATHER THAN STRINGS AT THE CALL SITE, so a row renamed in
	 * the design workbook breaks in one place and is found by
	 * `Cataclysm.EnemyModifiers.EveryModifierWithAnEffectIsStillInTheTable`
	 * rather than by a creature quietly losing an effect.
	 *
	 * THIS LIST IS SHORT AND THAT IS THE STATE OF THE WORK, not an oversight.
	 * There are 79 modifiers and these are the ones built so far. A creature
	 * carrying any other name behaves exactly as it did before, and the hover
	 * panel shows the name either way, which is what keeps the gap visible.
	 */
	static const TCHAR* TitanicResolveRow;
	static const TCHAR* OverpoweredRow;
	static const TCHAR* BloodthirstyRow;
	static const TCHAR* ThornsOfGlassRow;
	static const TCHAR* HellfireAuraRow;
	static const TCHAR* UnyieldingRow;
	static const TCHAR* AbyssalAuraRow;
	static const TCHAR* RelentlessRow;
	static const TCHAR* ShielderRow;
	static const TCHAR* PerfectAimRow;
	static const TCHAR* HordeLeaderRow;
	static const TCHAR* PhasewalkerRow;

	/** Whether a creature carries a named modifier. */
	static bool Carries(const TArray<FName>& Rows, const TCHAR* RowKey);

	// ----------------------------------------------------------------------
	// The numbers, each traceable to its own row's description
	// ----------------------------------------------------------------------

	/** Titanic Resolve: "50% more health". */
	static constexpr float TitanicResolveHealthMultiplier = 1.5f;

	/** Overpowered: "Always crits". */
	static constexpr float OverpoweredCritChance = 100.0f;

	/** Bloodthirsty: "Heal for 10% of the damage dealt to the player". */
	static constexpr float BloodthirstyLeechPercent = 10.0f;

	/**
	 * Unyielding: "Immunity to crowd control effects".
	 *
	 * A HUNDRED IS IMMUNE, which is what
	 * `UCataclysmSkillEffects::AfterCrowdControlResistance` makes true: it
	 * scales a stun's seconds and a shove's centimetres by the stat and
	 * answers nothing at a hundred. Until 2026-09-05 the stat touched only
	 * the chance of an incidental blunt stun, so setting it to a hundred
	 * would have blocked almost nothing a player would notice.
	 */
	static constexpr float UnyieldingCrowdControlResistance = 100.0f;

	/**
	 * Relentless: "Regenerates health over time, which no other creature
	 * does."
	 *
	 * A SHARE OF THE CREATURE'S OWN MAXIMUM HEALTH PER SECOND, not a flat
	 * figure. A creature's health grows about twentyfold across the eight
	 * difficulty tiers and again with its rarity, so any flat number would be
	 * a real heal on a tier 1 Common and nothing at all on a tier 8 Herald.
	 *
	 * TWO PER CENT IS A JUDGEMENT. The row states no rate. Two per cent a
	 * second is fifty seconds to heal from nothing to full, which is slow
	 * enough that it never wins a fight the player is engaged in and fast
	 * enough that disengaging and coming back is punished. It is a constant to
	 * tune against real play rather than an argued figure.
	 *
	 * NO DELAY AFTER DAMAGE, which the row used to state and the game does not
	 * have: health regeneration here is continuous, unlike the energy shield.
	 * The project owner chose on 2026-09-05 to correct the row rather than
	 * build a delay for one modifier.
	 */
	static constexpr float RelentlessHealthPerSecondShare = 0.02f;

	/**
	 * Shielder: "an additional shield equal to 50% of maximum health".
	 *
	 * ADDED TO WHATEVER SHIELD THE ARCHETYPE ALREADY GIVES, because five of
	 * the seven designed creatures have a fraction of exactly zero and the two
	 * that do not should keep theirs.
	 *
	 * IT RECHARGES ON THE GAME'S OWN THREE SECOND DELAY. The row said five,
	 * and `UCataclysmRegeneration::ShieldRefillDelaySeconds` is three for
	 * everything in the game. The project owner chose on 2026-09-05 to correct
	 * the row rather than give one modifier a delay of its own.
	 */
	static constexpr float ShielderShareOfHealth = 0.5f;

	/**
	 * Horde Leader: "On death, all nearby enemies become enraged, increasing
	 * their attack speed and movement speed for 5 seconds."
	 *
	 * IT GRANTS THE COMMANDER BUFF, WHICH ALREADY EXISTS AND ALREADY SAYS
	 * THIS. `game/Data/StatusEffects.csv` describes Commander as "All nearby
	 * allies gain 20% increased movement speed and attack speed", and the
	 * Succubus already grants it. A second buff meaning the same thing would
	 * be two names for one effect.
	 */
	static constexpr float HordeLeaderSeconds = 5.0f;

	/**
	 * Phasewalker: "Teleport short distances every few seconds."
	 *
	 * BOTH FIGURES ARE JUDGEMENTS. The row states neither a distance nor an
	 * interval. Three metres is far enough to break a melee player's spacing
	 * and short enough that the creature is still the thing you were fighting;
	 * four seconds is often enough to be the creature's character and rare
	 * enough not to make it impossible to hit.
	 */
	static constexpr float PhasewalkerDistanceCm = 300.0f;
	static constexpr float PhasewalkerIntervalSeconds = 4.0f;

	/**
	 * Keeps a phase step's direction off the modifier draw's stream.
	 *
	 * BOTH SEED FROM THE CREATURE AND THE CLOCK, so without a salt a creature
	 * phasing in the frame it was given its modifiers would draw its direction
	 * from the same numbers. The value itself means nothing.
	 */
	static constexpr int32 PhaseDrawSalt = 0x9A5E;

	/**
	 * Thorns of Glass: "Reflects 50% of all damage taken back to the attacker."
	 *
	 * IT USED TO BE 100% AND ONE POINT OF HEALTH. The project owner changed it
	 * on 2026-09-05, in the Enemy Modifiers sheet of
	 * `docs/All_Things_Cataclysm.xlsx`, which is where the row comes from. The
	 * old wording made the creature killable by anything that touched it and
	 * made reflected damage the only thing about it; the new one is a
	 * modifier a creature carries alongside its own health.
	 *
	 * SO IT CHANGES NO HEALTH AT ALL NOW, and `ForcedMaxHealth` went with it:
	 * nothing in the game forces a creature's maximum health any more.
	 */
	static constexpr float ThornsOfGlassRetaliationPercent = 50.0f;

	/**
	 * How far an aura modifier reaches, in centimetres.
	 *
	 * SIX METRES, DECIDED BY THE PROJECT OWNER ON 2026-09-05, and it is the
	 * number this game already uses for an aura: the Masochist's Beacon of
	 * Despair applies its debuff to enemies within 6 metres. One number for
	 * every aura means a player learns the distance once. No design document
	 * states a radius for an enemy aura.
	 */
	static constexpr float AuraRadiusCm = 600.0f;

	/**
	 * How often an aura pulses, in seconds.
	 *
	 * ONE SECOND, WHICH IS A JUDGEMENT. Hellfire Aura says it "deals constant
	 * fire damage" and states no interval. It applies Burn, which
	 * `game/Data/StatusEffects.csv` gives a duration of 4 seconds, so a pulse
	 * every second keeps a player standing in the aura alight without the aura
	 * being a fresh burn several times a second. The per-character step runs
	 * four times a second, so this is a gate on that rather than a timer.
	 */
	static constexpr float AuraPulseIntervalSeconds = 1.0f;

	// ----------------------------------------------------------------------
	// What the carried modifiers do to a creature's stats
	// ----------------------------------------------------------------------

	/**
	 * What the modifiers multiply a creature's maximum health by.
	 *
	 * A MULTIPLIER APPLIED TO THE FRESHLY COMPUTED BASE, not to whatever the
	 * attribute holds. `ACataclysmEnemyCharacter::ApplyStartingAttributes` runs
	 * again every time a spawner sets anything, and a multiplier applied to the
	 * current value would compound on every call.
	 */
	static float MaxHealthMultiplier(const TArray<FName>& Rows);

	/** The critical strike chance the modifiers force, or negative for none. */
	static float ForcedCritChance(const TArray<FName>& Rows);

	/** Life leech the modifiers grant, as a percentage of damage dealt. */
	static float LifeLeechPercent(const TArray<FName>& Rows);

	/** Retaliation the modifiers grant, as a percentage of damage taken. */
	static float RetaliationPercent(const TArray<FName>& Rows);

	/** Crowd control resistance the modifiers grant, as a percentage. */
	static float CrowdControlResistancePercent(const TArray<FName>& Rows);

	/** Health regenerated per second, as a share of maximum health. */
	static float HealthRegenShareOfMaximum(const TArray<FName>& Rows);

	/** Extra energy shield the modifiers grant, as a share of maximum health. */
	static float EnergyShieldShareOfHealth(const TArray<FName>& Rows);

	/**
	 * Whether this creature's attacks cannot be dodged. Perfect Aim.
	 *
	 * A QUESTION ABOUT THE ATTACKER AND NOT THE DEFENDER, which is what makes
	 * it a flag rather than a stat. Evasion is rolled on the target, so the
	 * only way an attacker can refuse it is for the blow to say so.
	 */
	static bool AttacksCannotBeDodged(const TArray<FName>& Rows);

	/**
	 * Buffs this creature's nearby allies, if it carries Horde Leader.
	 *
	 * CALLED WHEN THE CREATURE DIES, which is the only moment the modifier
	 * describes. Safe for anything that is not a creature carrying it.
	 *
	 * @return how many allies were buffed
	 */
	static int32 RallyAlliesOnDeath(AActor* Character);

	/**
	 * One step of Phasewalker, moving the creature if it is due.
	 *
	 * ON THE SAME PER-CHARACTER STEP THE AURAS USE, for the reason every job
	 * on it gives: it already runs four times a second.
	 *
	 * @return whether the creature moved
	 */
	static bool PhaseStep(AActor* Character, float StepSeconds);

	/**
	 * Whether enough time has passed for an aura to pulse again.
	 *
	 * PURE, SO IT CAN BE CHECKED WITHOUT A WORLD. The pulse itself reaches out
	 * and touches whoever is standing nearby, which no headless test can watch;
	 * whether it is due is arithmetic and this is it.
	 */
	static bool AuraPulseIsDue(float SecondsSinceLastPulse);

	/**
	 * One step of every aura modifier a creature carries.
	 *
	 * A JOB ON THE PER-CHARACTER STEP RATHER THAN A TIMER OF ITS OWN, which is
	 * the reason `UCataclysmContagion::AuraStep` gives for the Masochist's aura:
	 * `ACataclysmCharacterBase::RegenerationStep` already runs four times a
	 * second, and a timer per creature is one more thing to cancel when one
	 * dies. This is the sixth job on that step.
	 *
	 * SAFE FOR ANYTHING THAT IS NOT AN ENEMY, and it has to be, because the step
	 * it hangs off runs for the player as well. A player character, a creature
	 * with no modifiers and a dead creature all return immediately.
	 *
	 * @return how many targets were touched. Zero when nothing was due
	 */
	static int32 AuraStep(AActor* Character, float StepSeconds);

	/**
	 * Whether a row belongs to a Cataclysm a creature of this one may draw.
	 *
	 * PUBLIC BECAUSE A TEST ASKS IT DIRECTLY, and because it is the whole of the
	 * pool rule in one place: its own column, or Generic.
	 */
	static bool IsDrawableBy(const FCataclysmEnemyModifierRow& Row,
							 FName CataclysmType);
};
