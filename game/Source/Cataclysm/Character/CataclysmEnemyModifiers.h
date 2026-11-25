// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmEnemyModifiers.generated.h"

class UDataTable;
struct FCataclysmEnemyModifierRow;
class UAbilitySystemComponent;
class ACataclysmBonePyreCharacter;
class ACataclysmEnemyCharacter;

/**
 * Which modifiers a spawned enemy carries.
 *
 * WHAT THIS ANSWERS, AND WHY IT DID NOT EXIST BEFORE. Nothing gave any creature
 * a modifier. `game/Data/EnemyModifiers.csv` held 79 rows from the design
 * workbook's import until Famine's Withering Blows made 80 (#1722),
 * `ACataclysmEnemyCharacter::ModifierRows` has held a list since issue #740 and `UCataclysmCreaturePanel` has printed it before a
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
	 *         cannot happen today: the smallest pool is 8 (four Cataclysms have 8) plus the 10
	 *         Generic ones, and the most anything draws is 5
	 */
	static TArray<FName> Draw(const UDataTable* EnemyModifierTable,
							  FName CataclysmType, int32 Count,
							  FRandomStream& Stream,
							  const TArray<FName>& Already);

	/**
	 * The rows of a `CataclysmType` creature's pool that duplicate an effect it has innately, sorted: every row it
	 * may draw whose modifier name is one of `InnateEffects`, compared without case. What
	 * `ACataclysmEnemyCharacter::DrawModifiersForRarity` adds to `Already` before it draws, so they are never drawn.
	 *
	 * THE DESIGN'S RULE, HELD AT THE DRAW: "An innate ability must not duplicate a modifier its own Cataclysm can
	 * roll." It was held in the design data alone while a creature's Cataclysm was its archetype's (Abyssal Aura and
	 * Infernal Brand are closed to Demonic creatures as innate abilities); a creature takes its dungeon's Cataclysm
	 * now, so a War dungeon's Succubus would otherwise roll `War_Commander`, the Commander its Dominion grants.
	 * 2026-11-25.
	 *
	 * @return empty for a creature with no innate effect, for a pool that names none of them, and for no table
	 */
	static TArray<FName> ClosedByInnateEffects(const UDataTable* EnemyModifierTable, FName CataclysmType,
											   const TArray<FString>& InnateEffects);

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
	 * There are 80 modifiers and these are the ones built so far. A creature
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
	static const TCHAR* InfernalBrandRow;
	static const TCHAR* WitheringBlowsRow;
	static const TCHAR* TouchOfNothingRow;
	static const TCHAR* BeguilingRow;
	static const TCHAR* InfernalSacrificeRow;
	static const TCHAR* UnholySigilsRow;
	static const TCHAR* SacrificialBondRow;
	static const TCHAR* InfernoChargeRow;
	/** Starving Aura, issue #2539: built in `AuraStep`, beside Abyssal Aura. */
	static const TCHAR* StarvingAuraRow;

	/**
	 * The eleven Death modifiers, written ahead of their build-and-test window on 2026-11-24 by the owner's approval
	 * ("yes, write the Death enemy modifiers ahead"). Each one's sentence is its row in `game/Data/EnemyModifiers.csv`;
	 * where each is built is said beside its numbers below. `docs/DECISIONS.md` carries the research and every figure
	 * the rows do not state.
	 */
	static const TCHAR* HauntingRow;
	static const TCHAR* GraveTouchRow;
	static const TCHAR* UndyingWillRow;
	static const TCHAR* SoulEaterRow;
	static const TCHAR* ShadowStepRow;
	static const TCHAR* SpectralShiftRow;
	static const TCHAR* LingeringDecayRow;
	static const TCHAR* FinalCurseRow;
	static const TCHAR* FuneralPyreRow;
	static const TCHAR* SoulTetherRow;
	static const TCHAR* MortalReminderRow;

	/**
	 * Eight of War's nine modifiers, written 2026-11-25 ahead of their build-and-test window, on the owner's rulings of
	 * 2026-11-24 for the Death and War plan (Q10 to Q12, "Your recommendations work for all of those death/war
	 * questions"). Each one's sentence is its row in `game/Data/EnemyModifiers.csv`; `docs/DECISIONS.md` carries the
	 * research and every figure the rows do not state.
	 *
	 * 1 ON 1 HAS NO CONSTANT BECAUSE IT IS NOT BUILT. Its arena -- a ring that holds the player walking and lets the
	 * player's movement skill cross it, as the owner ruled -- needs a hold on the player's own movement that no code in
	 * the project has; the 2026-11-25 entry in `docs/DECISIONS.md` describes the mechanism and leaves it. A creature
	 * carrying it behaves as it did before, and the hover panel still names it.
	 */
	static const TCHAR* CommanderRow;
	static const TCHAR* BattleCryRow;
	static const TCHAR* ArmoredJuggernautRow;
	static const TCHAR* DrumsOfWarRow;
	static const TCHAR* ShieldWallRow;
	static const TCHAR* ArtilleryBarrageRow;
	static const TCHAR* BattleScarredRow;
	static const TCHAR* PhalanxRow;

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
	 * scales a stun's seconds, a knockdown's seconds and a shove's
	 * centimetres by the stat and answers nothing at a hundred. Until
	 * 2026-09-05 the stat touched only the chance of an incidental blunt
	 * stun, so setting it to a hundred would have blocked almost nothing a
	 * player would notice; and until 2026-09-17 a knockdown asked nothing,
	 * so a player's knockdown still floored this creature. Issue #1815.
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
	 * Infernal Brand: the explosion at five stacks, as a multiple of the
	 * creature's own attack damage.
	 *
	 * THE ROW SAYS "a large amount of damage" AND NOTHING ELSE. The project
	 * owner chose five times a hit on 2026-09-05: one hit's worth banked per
	 * stack, which a player can reason about, and which scales with the
	 * creature rather than needing a figure per difficulty tier.
	 */
	static constexpr float InfernalBrandExplosionHits = 5.0f;

	/**
	 * Beguiling: how long a charm lasts, and how long before another.
	 *
	 * TWO SECONDS IS THE ROW'S OWN FIGURE. **The ten second cooldown is not**;
	 * the project owner added it on 2026-09-05 and it is what stops the
	 * modifier being permanent. The charm triggers on the creature TAKING
	 * damage, so without a cooldown a player attacking at any reasonable speed
	 * would re-apply it before it expired and never get out of it.
	 */
	static constexpr float BeguilingSeconds = 2.0f;
	static constexpr float BeguilingCooldownSeconds = 10.0f;

	/**
	 * Infernal Sacrifice: what killing one ally is worth, and how often.
	 *
	 * EVERY FIGURE IS A JUDGEMENT. The row says "Sacrifice nearby allies to
	 * heal yourself and gain temporary damage buffs" and states no healing, no
	 * damage and no interval.
	 *
	 * ONE ALLY AT A TIME RATHER THAN ALL OF THEM. A creature that ate its
	 * whole pack at once would remove the fight the player was having and
	 * replace it with a different one, with nothing to react to in between.
	 * One every six seconds is a decision the player can answer by killing the
	 * pack first or by killing this creature faster.
	 */
	static constexpr float SacrificeHealShareOfHealth = 0.25f;
	static constexpr float SacrificeIntervalSeconds = 6.0f;
	static constexpr float SacrificeBuffSeconds = 8.0f;

	/**
	 * Sacrificial Bond: what share of a hit the creature itself keeps.
	 *
	 * "All damage taken is redirected and divided among nearby allies", and
	 * the row states no share. Divided EVENLY between the creature and its
	 * allies, so one ally halves what it takes and three allies leave it a
	 * quarter. That reads the sentence as written and makes the answer obvious
	 * to a player: kill the pack, or pull this one away from it.
	 */
	static constexpr float SacrificialBondReach = 600.0f;

	/**
	 * Unholy Sigils: how big the sigil is, how long it lasts, and how often.
	 *
	 * "Create a large demonic sigil on the ground. Allies in this sigil cannot
	 * be killed." LARGE is the only figure the row gives, so the sigil is
	 * twice an aura's reach.
	 *
	 * IT LASTS AND THEN IT DOES NOT, which is what makes it answerable. A
	 * permanent sigil would mean a pack that cannot be killed at all; eight
	 * seconds every twenty means the player waits it out or kills the caster.
	 */
	static constexpr float SigilRadiusCm = 1200.0f;
	static constexpr float SigilSeconds = 8.0f;
	static constexpr float SigilIntervalSeconds = 20.0f;

	/**
	 * Inferno Charge: the charge itself.
	 *
	 * REUSES THE CHARGE EVERY CREATURE ALREADY HAS.
	 * `ACataclysmEnemyCharacter::BeginCharge` is on the base class and takes a
	 * point, a speed, a width, a damage share and a knockback. The Hellhound
	 * is already "an aggressive charger leaving fire trails", so the project
	 * owner chose on 2026-09-05 to reuse it rather than write a second charge
	 * a player would have to learn to read separately.
	 *
	 * THE TWO SECOND CHANNEL IS THE ROW'S OWN FIGURE. The speed, the width and
	 * the damage share are judgements.
	 *
	 * THE CHANNEL IS THE LAST TWO SECONDS OF THE TWELVE, so a charge still sets
	 * off every twelve seconds. The creature stands, the lane it will run is
	 * drawn on the floor, and the lane is fixed when the channel starts and
	 * does not follow the player -- the general telegraph rule. Issue #1560.
	 *
	 * THE PATH IT RAN BURNS AFTERWARDS, COPIED FROM THE HELLHOUND'S LANE: a
	 * quarter of a hit per second for four seconds, as wide as the charge, and
	 * of the creature's own damage type, because the row names none. The
	 * coordinating session ruled the copy on 2026-10-02, labelled a judgement.
	 *
	 * ONE METRE EITHER SIDE, RAISED FROM 90 CM ON 2026-10-02 SO THE LANE IS
	 * DRAWN. `ACataclysmTelegraphMarker::ShowLine` draws nothing narrower than
	 * `SmallestUsefulRadiusCm`, the design's rule that a smaller marker leaves
	 * nowhere to walk, and the channel exists to be read. The charge's hit
	 * lane, the drawn lane and the burning path stay this one figure. A
	 * judgement, ruled by the coordinating session.
	 */
	static constexpr float InfernoChannelSeconds = 2.0f;
	static constexpr float InfernoChargeSpeedCmPerSecond = 1800.0f;
	static constexpr float InfernoChargeHalfWidthCm = 100.0f;
	static constexpr float InfernoChargeDamagePercent = 150.0f;
	static constexpr float InfernoChargeIntervalSeconds = 12.0f;
	static constexpr float InfernoPathSeconds = 4.0f;
	static constexpr float InfernoPathPercent = 25.0f;
	static_assert(InfernoChannelSeconds < InfernoChargeIntervalSeconds,
				  "the channel is part of the interval, so it must be shorter");

	/**
	 * Keeps a phase step's direction off the modifier draw's stream.
	 *
	 * BOTH SEED FROM THE CREATURE AND THE CLOCK, so without a salt a creature
	 * phasing in the frame it was given its modifiers would draw its direction
	 * from the same numbers. The value itself means nothing.
	 */
	static constexpr int32 PhaseDrawSalt = 0x9A5E;


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

	/**
	 * What one tick of Hellfire Aura's burn deals, as a share of its carrier's weapon damage (the figure its own hits
	 * are built from, `UCataclysmSkillEffects::WeaponDamageOf`, which the Inferno Charge path and the Hellhound's lane
	 * already read for the ground they leave burning). Issue #2749, the enemy burn slice.
	 *
	 * 0.09 SINCE 2026-11-19; IT WAS THE STATUS TABLE'S FLAT 25 A TICK. The project owner died on floor 2 of a 5-floor
	 * Outpost dungeon at level 2 with 228 maximum health, most of it to this burn, which no tier, ring or floor scaled:
	 * at tier 1 it was 3 to 15 times a creature's hit. The coordinating session ruled the same day that an
	 * enemy-applied damage over time is sized from what applies it -- Path of Exile 2 sizes an ignite at 20% of the
	 * hit a second -- and set the share by the rule that chose tier 1's damage scale: the largest step of 0.01 at which
	 * a new character, levelling at half the kills, wins every floor of all 3,057 first-surge Outpost dungeons with at
	 * least 10% health left while standing in an Elite Abyssal Warden's aura on every floor, the strongest carrier. 0.1
	 * left 3 short; Path of Exile 2's 0.2 costs 36 of 597. A judgement. `docs/DECISIONS.md` has the measurement and
	 * `tools/tests/test_the_starting_character_survives_a_pack.py` the guard.
	 *
	 * THE BURN'S DURATION, ONE STACK AND THE PULSE ARE UNCHANGED: four seconds, refreshed every second while the target
	 * stays in reach. The player's own burns keep the table's 25 (the owner's ruling of 2026-08-24).
	 */
	static constexpr float HellfireAuraBurnShareOfWeaponDamage = 0.09f;

	/** What one tick of a carrier's Hellfire Aura burn deals before the target's defences: its weapon damage times
	 *  `HellfireAuraBurnShareOfWeaponDamage`, never below nought. */
	static float HellfireAuraBurnPerTick(float CarrierWeaponDamage);

	/**
	 * What one pulse of a medic's aura restores, as a percentage of each
	 * ally's own maximum health.
	 *
	 * FIVE PERCENT, AND IT IS A JUDGEMENT RATHER THAN A DERIVED FIGURE. The
	 * Field Medic row states no rate at all -- only that the medic "constantly
	 * heals all other enemies in a large radius" and that this "forces the
	 * player to prioritize a non-threatening enemy".
	 *
	 * THE GENRE OFFERS NO FIGURE TO TAKE. Diablo 4's affix list has nothing
	 * that continuously heals other monsters; its nearest is Life Link, which
	 * shares one pool between enemies rather than healing them. So there was
	 * no shipped number to read off and this was reasoned instead.
	 *
	 * WHAT GOVERNS IT, IN BOTH DIRECTIONS. It has to be large enough that
	 * ignoring the medic changes a fight, or the row's stated purpose fails.
	 * It has to be small enough that a pack does not become unkillable --
	 * **and that is the binding constraint, because one medic heals every
	 * ally in range at once.** With four creatures nearby, five percent each
	 * is twenty percent of a creature's health restored across the pack every
	 * second. The per-target figure is modest precisely because the pack
	 * multiplies it.
	 *
	 * IT WANTS TUNING AGAINST PLAY and is not claimed to be right. What is
	 * claimed is that it is in the range where the row's purpose can work.
	 */
	static constexpr float MedicHealPercentOfMaximumPerPulse = 5.0f;

	// ----------------------------------------------------------------------
	// The Death modifiers' numbers, 2026-11-24
	//
	// THE ROW'S OWN FIGURE WHERE IT GIVES ONE, and a judgement named in
	// `docs/DECISIONS.md` with its genre source where it does not. Whole numbers
	// wherever a player sees them.
	// ----------------------------------------------------------------------

	/**
	 * Grave Touch: "Deal more damage to players with less than 50% health."
	 *
	 * THE 50% IS THE ROW'S. The 50% MORE is a judgement: Path of Exile's Executioner monster modifier gives 15% more
	 * hit damage per 10% of life the target is missing, which is 75% at the row's line; half again is the smaller,
	 * readable figure, and the same size as Overpowered's always-critical hit on a target that is already low.
	 * HITS ONLY, as Executioner's stat is "hit damage": a tick of damage over time is not made larger.
	 */
	static constexpr float GraveTouchBelowHealthShare = 0.5f;
	static constexpr float GraveTouchDamageMultiplier = 1.5f;

	/**
	 * Mortal Reminder: "Attacks apply a debuff that prevents the player from being healed above their current HP for 4
	 * seconds." THE FOUR SECONDS AND THE CEILING ARE THE ROW'S. The ceiling is the health the hit left; a later hit
	 * sets it again, lower, and restarts the four seconds. Path of Exile 2's Hinder Aura monster modifier is the shape:
	 * "Prevents Recovery Above 50%".
	 */
	static constexpr float MortalReminderSeconds = 4.0f;

	/**
	 * Soul Tether: "Transfer a portion damage taken to the player as health degeneration over time."
	 *
	 * A TENTH OF THE HEALTH THE CREATURE LOSES, OVER FOUR SECONDS. The tenth is a judgement; the four seconds are Path
	 * of Exile's Corrupter monster modifier's, whose Corrupted Blood "lasts for 4 seconds". OF THE HEALTH LOST, NEVER
	 * OF THE BLOW: what reached the creature's health, cut to the health it held, so however hard the player hits, one
	 * creature sends back at most a tenth of its own health -- the owner's Thorns of Glass ruling of 2026-11-18 is that
	 * a creature's answer to a blow is not sized by the player's own damage.
	 */
	static constexpr float SoulTetherShareOfHealthLost = 0.1f;
	static constexpr float SoulTetherSeconds = 4.0f;

	/**
	 * Spectral Shift: "Phase in and out of existence, becoming immune to damage."
	 *
	 * OUT OF EXISTENCE FOR FIVE SECONDS IN EVERY FIFTEEN. The five are Diablo III's Shielding elite affix's ("5 sec
	 * duration", Maxroll); the fifteen are a judgement, so the creature can be hurt two thirds of the time. While out
	 * it takes no damage of any kind, as a shrouded creature does, and it goes on acting, as Diablo III's shielded
	 * elite and Diablo IV's Shielded do.
	 */
	static constexpr float SpectralShiftOutSeconds = 5.0f;
	static constexpr float SpectralShiftIntervalSeconds = 15.0f;
	static_assert(SpectralShiftOutSeconds < SpectralShiftIntervalSeconds,
				  "Spectral Shift must leave the creature in existence for part of every interval");

	/**
	 * Shadow Step: "Become invisible when not attacking."
	 *
	 * SEEN FROM THE MOMENT AN ATTACK BEGINS UNTIL ONE SECOND AFTER IT ENDS: an ordinary swing ordered and its blow
	 * waiting, an ability winding up, a charge, a channel. The second is a judgement, so the swing's end can be read
	 * before the creature fades. Invisible is what the vision system does to a creature beyond the player's sight:
	 * not drawn, no bar, not clickable, still struck by whatever reaches it.
	 */
	static constexpr float ShadowStepSeenSecondsAfterAnAttack = 1.0f;

	/**
	 * Lingering Decay: "Enemies leave behind necrotic zones that reduce healing to players standing in them."
	 *
	 * A ZONE EVERY FIVE SECONDS WHERE IT STANDS, LASTING TEN, CUTTING HEALING BY HALF. The 50% and the five seconds are
	 * the game's own figures for the same thing, Necrotic Ground's (`UCataclysmDungeonModifierEffects::
	 * NecroticGroundHealingLessPercent` and `NecroticGroundSecondsBetweenPatches`), and Starving Aura's cut is 50 too,
	 * so a player learns one figure for "healing cut". The radius is Necrotic Ground's patch, three metres. The ten
	 * seconds are a judgement: two zones at most from one creature at once.
	 */
	static constexpr float LingeringDecayHealingLessPercent = 50.0f;
	static constexpr float LingeringDecaySecondsBetweenZones = 5.0f;
	static constexpr float LingeringDecayZoneSeconds = 10.0f;
	static constexpr float LingeringDecayRadiusCm = 300.0f;

	/**
	 * How long `Status.Debuff.LingeringDecay` is laid for on a character of the players' side standing in a zone,
	 * re-laid on every quarter-second step while it stays: two steps, so the status goes within half a second of the
	 * character leaving. Display only; the cut itself is asked of where the character stands. The status row is the
	 * Debuffs sheet's, added 2026-11-25 on the owner's acceptance of 2026-11-24. A judgement, recorded.
	 */
	static constexpr float LingeringDecayStatusSeconds = 0.5f;

	/**
	 * Final Curse: "Upon death, curse the player for 5 seconds, causing them to take damage when using abilities."
	 *
	 * THE FIVE SECONDS ARE THE ROW'S. Each skill used under the curse, every slot but the basic attack, costs 5% of
	 * the user's maximum health as Death damage, met by their defences: Echo Chamber's self hit, which the
	 * coordinating session sized from maximum health on 2026-11-18 so "a small amount" holds at every level. Diablo
	 * II's Iron Maiden is the shape, a curse that hurts its bearer for acting.
	 */
	static constexpr float FinalCurseSeconds = 5.0f;
	static constexpr float FinalCurseMaximumHealthPercentPerUse = 5.0f;

	/**
	 * Funeral Pyre: "Summon deadly bone pyres when hit that explode if not destroyed quickly."
	 *
	 * ONE PYRE AT THE FEET OF WHOEVER STRUCK, AT MOST ONE EVERY EIGHT SECONDS, EXPLODING THREE SECONDS LATER for three
	 * of the creature's hits on everyone of the player's side within three metres, unless it is destroyed first. It
	 * holds a tenth of the creature's maximum health. Path of Exile's Crystal-skinned "fires crystals under players when
	 * they hit you"; Diablo IV's Frozen orbs explode "after 3 seconds" on an 8 second cooldown. The three metres, the
	 * three hits and the tenth are judgements, the hits below Infernal Brand's five because a pyre can be walked away
	 * from or broken.
	 */
	static constexpr float FuneralPyreCooldownSeconds = 8.0f;
	static constexpr float FuneralPyreFuseSeconds = 3.0f;
	static constexpr float FuneralPyreRadiusCm = 300.0f;
	static constexpr float FuneralPyreExplosionHits = 3.0f;
	static constexpr float FuneralPyreShareOfHealth = 0.1f;

	/**
	 * Undying Will: "Enemies resurrect after death with reduced health but increased damage output."
	 *
	 * ONCE, AT ONCE, WHERE IT FELL, AT ITS KIND AND RUNG, WITH HALF ITS MAXIMUM HEALTH AND 50% INCREASED DAMAGE. Built
	 * as Dead Rising and Divine Resurgence bring a creature back (`ACataclysmDungeonGameMode::NoteDeathForUndyingWill`):
	 * the half is Divine Resurgence's figure; the 50% is a judgement, in the increased bucket the row's word names.
	 */
	static constexpr float UndyingWillHealthShare = 0.5f;
	static constexpr float UndyingWillDamageIncreasedPercent = 50.0f;

	/**
	 * Haunting: "A spectral copy of this enemy is created on death", and the `Debuff_Haunting` status row's "A debuff
	 * applied by a spectral copy of an enemy that reduces all of a player's resistances".
	 *
	 * A COMMON OF ITS KIND AT HALF HEALTH, WHERE IT FELL, whose hits cut each of the target's resistances by 25 for five
	 * seconds. Diablo IV's Splitter "splits into 2 non-Elite mobs of the same type" and Diablo III's Illusionist copies
	 * have 5% of the original's life: so a Common, at Divine Resurgence's half. The 25 is Abyssal Aura's resistance
	 * cut and the five seconds are Battle Cry's, the two figures the status table already gives a resistance debuff.
	 */
	static constexpr float HauntingCopyHealthShare = 0.5f;
	static constexpr float HauntingResistanceCut = 25.0f;
	static constexpr float HauntingSeconds = 5.0f;

	// ----------------------------------------------------------------------
	// The War modifiers' numbers, 2026-11-25
	//
	// THE ROW'S OWN FIGURE WHERE IT GIVES ONE, the owner's ruling where one was
	// asked, and a judgement named in `docs/DECISIONS.md` with its genre source
	// where neither says. Whole numbers wherever a player sees them.
	// ----------------------------------------------------------------------

	/**
	 * Commander: "Inspires allies, increasing their movement speed and attack speed by 20%."
	 *
	 * IT GRANTS THE COMMANDER BUFF, `Buff_Commander`, which is that sentence already: its 20% is
	 * `ACataclysmEnemyCharacter::CommanderIncreasePercent` and `CommanderMultiplier` reads it. A second buff meaning
	 * the same thing would be two names for one effect, the reason Horde Leader gives.
	 *
	 * EIGHT METRES, THE BUFF'S OWN REACH, so one buff reaches as far whoever grants it: the Succubus's Dominion and the
	 * Commander's Aura dungeon rule (`UCataclysmDungeonModifierEffects::CommandersAuraRadiusCm`) both grant it to
	 * allies within 8 metres. Written here and held equal to that figure by
	 * `tools/tests/test_war_enemy_modifiers_match_their_rows.py`. A judgement, recorded.
	 *
	 * RE-GRANTED ON EVERY AURA PULSE, ONCE A SECOND, FOR TWO SECONDS, so an ally that stays keeps it and one that walks
	 * away loses it within two seconds: Abyssal Aura's and Starving Aura's two seconds, the figure the status table
	 * gives an enemy's aura for the same shape. One effect per tag, so standing beside two commanders is still 20%.
	 */
	static constexpr float CommanderReachCm = 800.0f;
	static constexpr float WarAuraGrantSeconds = 2.0f;

	/**
	 * Drums of War: "Increases nearby allies move speed and attack speed by 50%."
	 *
	 * THE 50% IS THE ROW'S, under its own buff, `Buff_Drums_of_War`, read by
	 * `ACataclysmEnemyCharacter::DrumsOfWarMultiplier`. HALF COMMANDER'S REACH, FOUR METRES: the plan's
	 * recommendation for Q12, which the owner accepted on 2026-11-24 ("Your recommendations work for all of those
	 * death/war questions"): keep both, Drums of War with half Commander's radius, so the stronger buff needs the pack
	 * close. Granted on the same pulse and for the same two seconds as Commander's.
	 */
	static constexpr float DrumsOfWarReachCm = CommanderReachCm * 0.5f;

	/**
	 * Armored Juggernaut: "Gain 50% armor".
	 *
	 * HALF AGAIN ITS OWN ARMOUR, a multiplier on the freshly computed figure as `MaxHealthMultiplier` is on health.
	 * Path of Exile 2's monster modifiers state armour the same way, as a share added in proportion ("monster
	 * additional strength ratio % for armour [50]", poe2db.tw/us/Monster_Modifiers). A KIND DESIGNED WITH NO ARMOUR
	 * GAINS NONE: the Imp's armour share is 0.0, and half again of nothing is nothing. A judgement, recorded.
	 */
	static constexpr float ArmoredJuggernautArmourMultiplier = 1.5f;

	/**
	 * Battle Scarred: "Become more dangerous, gaining increased critical strike chance and damage resistance."
	 *
	 * DOUBLE ITS OWN CRITICAL STRIKE CHANCE AND 30 MORE RESISTANCE, FROM THE START. Path of Exile's Deadeye monster
	 * modifier gives "100% increased Critical Strike Chance" (poedb.tw/us/Archnemesis), and Path of Exile 2's
	 * monster modifiers give "+30% to all Elemental Resistances" (poe2db.tw/us/Monster_Modifiers); a creature's
	 * resistance here is one figure against every type. "Become" is read as what the modifier does to the creature
	 * and not as a moment it changes, since the row names none. Judgements, recorded.
	 */
	static constexpr float BattleScarredCritChanceIncreasedPercent = 100.0f;
	static constexpr float BattleScarredResistance = 30.0f;

	/**
	 * Shield Wall: "Take 75% less damage from projectiles."
	 *
	 * THE WORDING THE OWNER ACCEPTED ON 2026-11-24 (the plan's Q10), replacing "from attacks from the front", by the
	 * owner's ruling of 2026-08-09: "we don't do positional weak points. That's too tedious in a diablo like arpg".
	 * A quarter of every projectile's contact and of
	 * its blast reaches the carrier, asked where every projectile lands (`ACataclysmProjectile::HitOne`); every other
	 * blow reaches it whole.
	 */
	static constexpr float ShieldWallProjectileDamageShare = 0.25f;

	/**
	 * Phalanx: "Gains 10% damage reduction for every other enemy within 5 meters."
	 *
	 * BOTH FIGURES ARE THE ROW'S. Damage reduction is the game's own stat of that name, step 5 of the damage
	 * calculation, so its 75% cap (`UCataclysmDamageCalculation::DamageReductionCap`) bounds it at eight allies. An
	 * "other enemy" is another creature of its side that can lose health: not a floor-rule object, nor one that
	 * cannot be hurt, the reading Sacrificial Bond's allies were ruled to on 2026-09-30 (#1559).
	 */
	static constexpr float PhalanxReachCm = 500.0f;
	static constexpr float PhalanxDamageReductionPerAlly = 10.0f;

	/**
	 * Battle Cry: "Emit a terrifying shout that reduces the player's resistances or armor for 5 seconds."
	 *
	 * EVERY RESISTANCE CUT BY 25, NEVER BELOW NOUGHT, FOR THE ROW'S FIVE SECONDS, under `Debuff_Battle_Cry`, whose
	 * row names the eight resistances it moves; resistances and not armour, a judgement: the row offers either, and
	 * one status row moves one kind of stat. The 25 is Abyssal Aura's and Haunting's cut, the figure the status table
	 * already gives an enemy's resistance debuff. A SHOUT EVERY EIGHT SECONDS, Path of Exile's Intimidating Cry's
	 * cooldown ("Cooldown Time: 8.00 sec", poedb.tw/us/Intimidating_Cry), so it is up five seconds in eight; and only
	 * when a target is within the six metres every enemy aura reaches, the clock waiting otherwise.
	 */
	static constexpr float BattleCryIntervalSeconds = 8.0f;
	static constexpr float BattleCrySeconds = 5.0f;
	static constexpr float BattleCryResistanceCut = 25.0f;
	static constexpr float BattleCryReachCm = AuraRadiusCm;
	static_assert(BattleCrySeconds < BattleCryIntervalSeconds,
				  "Battle Cry must lapse before the next shout, or the player is never out of it");

	/**
	 * Artillery Barrage: "Call down explosive projectiles in random areas near the player."
	 *
	 * A VOLLEY OF THREE SHELLS EVERY SIX SECONDS, each a three metre circle drawn on the floor at a random point within
	 * four metres of the player, landing a second and a half later for one and a half of the creature's hits on
	 * everyone of the player's side inside it. Diablo III's Mortar elite affix fires "3 projectiles per cast" with a
	 * "3 yards radius for direct hit" (maxroll.gg/d3/resources/elite-affixes); Path of Exile's Bombardier
	 * "Periodically Fires a Nova of Fire Mortars (6 second cooldown)" (poedb.tw/us/Archnemesis). The warning is
	 * derived as the Artillery Strike dungeon rule's is: twice the time a player at the base 400 cm a second takes to
	 * walk out of a circle from its middle. The four metres and the one and a half hits are judgements: a shell can
	 * fall on the player and three cannot cover the ground round them, and the hits are Inferno Charge's 150%, the
	 * creature's other drawn attack a player walks out of. Only when a target is within Inferno Charge's 20 metres.
	 */
	static constexpr float ArtilleryBarrageIntervalSeconds = 6.0f;
	static constexpr int32 ArtilleryBarrageShells = 3;
	static constexpr float ArtilleryBarrageShellRadiusCm = 300.0f;
	static constexpr float ArtilleryBarrageWarningSeconds = 1.5f;
	static constexpr float ArtilleryBarrageLandsWithinCm = 400.0f;
	static constexpr float ArtilleryBarrageShellHits = 1.5f;
	static constexpr float ArtilleryBarrageReachCm = 2000.0f;
	static_assert(ArtilleryBarrageWarningSeconds * 400.0f >= ArtilleryBarrageShellRadiusCm * 2.0f,
				  "a player at the base walk speed must be able to walk out of a shell's circle twice over");
	static_assert(ArtilleryBarrageWarningSeconds < ArtilleryBarrageIntervalSeconds,
				  "a volley must land before the next is called");

	/** Keeps a barrage's points off the other seeded draws of the same creature and clock, as `PhaseDrawSalt` does. */
	static constexpr int32 ArtilleryBarrageDrawSalt = 0xA27B;

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

	/**
	 * Whether the modifiers make the creature strike back at whatever hits it, once per hit, for its own attack damage:
	 * Thorns of Glass, "Strikes back at whatever hits it, once per hit, for its own attack damage." Issue #2749 (B2).
	 *
	 * THE OWNER'S RULING OF 2026-11-18, the option chosen: "When struck, a reflecting creature hits back for its own
	 * attack damage, sized by its strength like any other hit and reduced by your defences. Your own damage no longer
	 * matters to it." Until then the row read "Reflects 50% of all damage taken back to the attacker" and gave the
	 * creature 50 retaliation, so a level 1 character's own big hit came back at half its size; the owner died to one.
	 * Before that, until 2026-09-05, it reflected all of it on a creature of one health.
	 *
	 * A YES OR NO AND NOT A FIGURE: the strike back's size is the creature's own attack damage, which
	 * `UCataclysmRetaliation::StrikeBack` reads. It changes no health and no stat, so `ApplyModifierAttributes` writes
	 * nothing for it.
	 */
	static bool StrikesBack(const TArray<FName>& Rows);

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
	 * One more Infernal Brand on whatever this creature just hit, exploding at
	 * five.
	 *
	 * NOTHING ON A TARGET ALREADY DEAD, issue #2874: the blow that kills reaches
	 * this after the death, and a brand bursting on a corpse is what the owner's
	 * log showed. The same refusal is in the two below.
	 *
	 * @return the damage the explosion dealt, or zero when it did not fire
	 */
	static float BrandOnHit(AActor* Attacker, AActor* Target);

	/**
	 * One more Withering on whatever this creature just hit, if it carries
	 * Withering Blows; at ten, the target is slowed by the `Debuff_Wither` row's
	 * figure for the row's duration, and every stack is spent. Issue #1722.
	 *
	 * TWO STATUSES. From one stack to nine the target carries
	 * `Status.Debuff.Withering`, which does nothing and is what the count is
	 * shown beside; at ten it is taken off and `Status.Debuff.Wither`, the slow,
	 * is put on. The project owner's choice, 2026-10-30.
	 *
	 * @return whether this hit slowed the target
	 */
	static bool WitherOnHit(AActor* Attacker, AActor* Target);

	/**
	 * One more Touch of Nothing on whatever this creature just hit, if it carries Void's Touch of Nothing, up to
	 * five, with `Status.Debuff.TouchofNothing` put on for the stacks' window so the count can be shown. Each
	 * stack refuses one buff the target would gain: `UCataclysmStacks::NegatesTheNextBuff`. Issue #1722.
	 *
	 * @return whether a stack was granted
	 */
	static bool TouchOfNothingOnHit(AActor* Attacker, AActor* Target);

	/**
	 * Charms whoever just damaged this creature, if it carries Beguiling and
	 * the charm is off cooldown.
	 *
	 * @return whether a charm was applied
	 */
	static bool CharmWhoeverStruck(AActor* Struck, AActor* Striker);

	/**
	 * What share of a hit this creature keeps, after Sacrificial Bond has
	 * divided it among its nearby allies.
	 *
	 * AN ALLY IS A CREATURE THAT CAN LOSE HEALTH. Issue #1559, ruled 2026-09-30:
	 * a floor-rule object (a spire, a beacon, a carcass and the rest, every
	 * `ACataclysmFloorSourceCharacter`) is not an ally in the row's sense, and a
	 * creature that cannot be hurt or is shrouded would take a share and lose
	 * nothing. A creature standing in an Unholy Sigil still counts.
	 *
	 * @param OutSharing  filled with the allies the rest is divided among, when
	 *                    given. Empty when the creature keeps the whole hit
	 * @return one when it does not carry the modifier or has nobody to share
	 *         with, and one over the number sharing otherwise
	 */
	static float ShareOfDamageKept(AActor* Character, TArray<AActor*>* OutSharing = nullptr);

	/**
	 * Whether this character is standing in a sigil that will not let it die.
	 *
	 * ASKED OF THE THING ABOUT TO DIE rather than kept as a flag on it, so a
	 * creature that walks out of the sigil is killable on the next blow with
	 * nothing to clear.
	 */
	static bool IsProtectedBySigil(const AActor* Character);

	/**
	 * One step of the modifiers that act on a timer of their own: Infernal
	 * Sacrifice, Unholy Sigils and Inferno Charge; Death's Spectral Shift,
	 * Shadow Step and Lingering Decay, and a bone pyre's fuse; and War's
	 * Phalanx, Battle Cry and Artillery Barrage. A character of the players'
	 * side is given Lingering Decay's status here while it stands in a zone.
	 *
	 * ON THE SAME PER-CHARACTER STEP THE AURAS AND PHASEWALKER USE, for the
	 * reason every job on it gives: it already runs four times a second.
	 *
	 * @return how many of them acted this step
	 */
	static int32 TimedStep(AActor* Character, float StepSeconds);

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
	 * Heal this creature's living allies within the aura radius.
	 *
	 * WHAT ASKS FOR IT. The Field Medic dungeon modifier: "An elite 'Medic'
	 * enemy is present on each floor. It does not attack, but constantly heals
	 * all other enemies in a large radius." Issue #1648.
	 *
	 * THE CALLER DECIDES WHICH CREATURE IS A MEDIC, AND THAT IS DELIBERATE.
	 * `AuraStep` beside this gates on a creature's own modifier rows, read
	 * from `game/Data/EnemyModifiers.csv`. A medic is a **dungeon** modifier,
	 * from a different file and a different system, so no row on the creature
	 * says it is one. Putting the gate in the caller keeps the knowledge where
	 * it lives rather than teaching this file about dungeon rules.
	 *
	 * IT SHARES THE ONE AURA CLOCK ON THE CREATURE, so a creature carrying
	 * both this and a modifier aura pulses both together rather than at two
	 * different moments. One creature, one pulse.
	 *
	 * ALLIES AND NOT ITSELF. `FindAlliesInSphere` excludes the instigator,
	 * which is what the row asks for -- "all **other** enemies".
	 *
	 * IT DOES NOT KEEP THE CLOCK AND MUST NOT. `AuraStep` owns
	 * `SecondsSinceAuraPulse` and calls this when a pulse is due. An earlier
	 * draft kept its own copy of those three lines, which meant a creature
	 * that was both the medic and carried an aura advanced the one clock
	 * twice a step and fired everything at twice the intended rate.
	 *
	 * @param Character the medic. Anything that is not a creature heals
	 *                  nobody
	 * @return how many allies this pulse healed
	 */
	static int32 HealAlliesPulse(AActor* Character);

	// ----------------------------------------------------------------------
	// The Death modifiers, 2026-11-24
	// ----------------------------------------------------------------------

	/**
	 * What Grave Touch multiplies one hit by: `GraveTouchDamageMultiplier` when the rows carry it and the target holds
	 * less than half its maximum health, one otherwise. PURE, so the line can be checked without a world.
	 * `UCataclysmVitalAttributeSet` asks it for a hit and never for a tick.
	 */
	static float GraveTouchMultiplier(const TArray<FName>& Rows, float TargetHealth, float TargetMaximumHealth);

	/** Soul Eater: whether this creature's blows pass every energy shield by, to health. */
	static bool IgnoresEnergyShield(const TArray<FName>& Rows);

	/**
	 * Mortal Reminder, on a hit that took health: the target may not be healed above the health it holds now for
	 * `MortalReminderSeconds`. Kept on its ability system and read by `UCataclysmRegeneration::HealthHealingCeiling`,
	 * which every heal of health asks; `Status.Debuff.MortalReminder` is laid beside it for the same time since
	 * 2026-11-25, so the player's effects list names it. A dead target is given nothing, as Infernal Brand refuses
	 * one (#2874).
	 *
	 * @return whether a ceiling was set
	 */
	static bool MortalReminderOnHit(AActor* Attacker, AActor* Target);

	/** The health a Mortal Reminder holds this ability system's healing to now, or negative when none is running. */
	static float RecoveryCeilingOn(const UAbilitySystemComponent& AbilitySystem);

	/**
	 * Soul Tether, on health this creature lost to `Striker`: a tenth of it is laid on the striker as damage over time
	 * across four seconds, ADDED to whatever of an earlier tether is still to come. Never on itself, never on its own
	 * side, never from a medic, never on the dead.
	 *
	 * @param HealthLost  what the blow really took from this creature's health
	 * @return the damage newly laid on the striker, before its defences, or zero
	 */
	static float TetherOnHealthLost(AActor* Tethered, AActor* Striker, float HealthLost);

	/** Whether Spectral Shift has this creature out of existence now, so no damage reaches it. */
	static bool IsPhasedOut(const AActor* Character);

	/**
	 * Shadow Step: this creature has begun an attack, so it is seen now and for `ShadowStepSeenSecondsAfterAnAttack`
	 * after. Called by the brain where it orders a swing and where an ability begins; the timed step also sees a swing
	 * waiting, a wind-up, a charge and a channel. Safe for anything that is not a creature carrying it.
	 */
	static void RevealForShadowStep(AActor* Character);

	/** Whether Shadow Step has hidden this creature now. */
	static bool IsHiddenByShadowStep(const AActor* Character);

	/**
	 * What a creature's death does through the Death modifiers that act at it: Final Curse curses the player, and a
	 * creature Shadow Step hid is shown again, so its fall can be seen. And since 2026-11-25 a volley of War's
	 * Artillery Barrage still in the air is called off, its circles taken away. Called by `ACataclysmEnemyCharacter::
	 * HandleDeath` once the death is marked. Haunting and Undying Will act at a death too, from the dungeon game mode,
	 * because only it can place a creature.
	 *
	 * @return whether the player was cursed
	 */
	static bool NoteDeathOfACarrier(AActor* Dying);

	/**
	 * Final Curse, on a skill used: a cursed user pays `FinalCurseMaximumHealthPercentPerUse` of its maximum health as
	 * Death damage, met by its defences. Called by `UCataclysmCombatEvents::NoteSkillUsed`, the one place every skill
	 * paid for is announced.
	 *
	 * @param bWasTheBasicAttack  true for the basic attack, which the curse leaves alone
	 * @return the damage sent, before the user's defences, or zero
	 */
	static float FinalCurseOnSkillUse(AActor* User, bool bWasTheBasicAttack);

	/**
	 * Funeral Pyre, on a hit this creature took from `Striker`: one bone pyre at the striker's feet, at most once every
	 * `FuneralPyreCooldownSeconds`. Never from a medic, never for a dead creature, never at its own side.
	 *
	 * @return the pyre placed, or null
	 */
	static ACataclysmBonePyreCharacter* PyreWhenStruck(AActor* Struck, AActor* Striker);

	/**
	 * One step of a bone pyre's fuse. When it runs out the pyre explodes on everyone of the player's side within
	 * `FuneralPyreRadiusCm` and is removed; a pyre destroyed first never explodes, because a dead pyre takes no step.
	 *
	 * @return the number of characters the explosion struck, or zero when it did not go off this step
	 */
	static int32 PyreStep(ACataclysmBonePyreCharacter* Pyre, float StepSeconds);

	/**
	 * Haunting's copy, on a hit that took health: each of the target's resistances cut by `HauntingResistanceCut`,
	 * never below nought, for `HauntingSeconds` under `Status.Debuff.Haunting`. One application at a time: a second
	 * replaces the first.
	 *
	 * @return whether the cut was applied
	 */
	static bool HauntOnHit(AActor* Attacker, AActor* Target);

	/**
	 * Whether the dungeon should bring `Fallen` back through Undying Will, or leave a spectral copy of it through
	 * Haunting: it carries the row and its death pays, so a creature a rule brought back never does either. What the
	 * game mode spawns is then given its figures by the two functions after these.
	 */
	static bool UndyingWillRaises(const AActor* Fallen);
	static bool HauntingLeavesACopy(const AActor* Fallen);

	/** Half its maximum health and 50% increased damage: a creature Undying Will brought back. */
	static void MakeUndyingWillRisen(ACataclysmEnemyCharacter* Risen);

	/** Half its maximum health and the copy's mark, which its hits read: a Haunting copy. */
	static void MakeSpectralCopy(ACataclysmEnemyCharacter* Copy);

	/**
	 * Lingering Decay: how much less of each heal reaches `Healed` because it stands in a necrotic zone, in percent.
	 * The largest zone's figure, never a sum; nought for anything not of the players' side, which is the row's "players".
	 * `UCataclysmRegeneration::TopUp` adds it to the healing received reduction for health.
	 */
	static float LingeringDecayHealingLessPercentAt(const AActor* Healed);

	// ----------------------------------------------------------------------
	// The War modifiers, 2026-11-25
	//
	// Commander and Drums of War are granted on `AuraStep`'s pulse; Phalanx,
	// Battle Cry and Artillery Barrage run on `TimedStep`; Armored Juggernaut and
	// Battle Scarred are figures `ACataclysmEnemyCharacter::
	// ApplyStartingAttributes` writes; Shield Wall is asked where a projectile
	// lands. 1 On 1 is not built (see `CommanderRow`).
	// ----------------------------------------------------------------------

	/** Armored Juggernaut: what the modifiers multiply a creature's armour by, applied to the freshly computed base. */
	static float ArmourMultiplier(const TArray<FName>& Rows);

	/** Battle Scarred: what the modifiers multiply a creature's own critical strike chance by, one plus the increase. */
	static float CritChanceMultiplier(const TArray<FName>& Rows);

	/** Battle Scarred: the resistance the modifiers add to a creature's own, in points. */
	static float AddedResistance(const TArray<FName>& Rows);

	/**
	 * Shield Wall: the share of a projectile's damage that reaches `Target`, `ShieldWallProjectileDamageShare` for a
	 * creature carrying it and one for anything else. Asked by `ACataclysmProjectile::HitOne` for every contact and
	 * every blast.
	 */
	static float ShareOfProjectileDamageTaken(const AActor* Target);

	/**
	 * Phalanx: the damage reduction, in percent, that `Others` other creatures of its side within
	 * `PhalanxReachCm` give a carrier, before the damage calculation's own cap. PURE, so the arithmetic can be
	 * checked without a world.
	 */
	static float PhalanxDamageReductionFor(int32 Others);

	/** Phalanx: how many other creatures of `Character`'s side within `PhalanxReachCm` count, by the row's reading. */
	static int32 CountPhalanxAllies(const AActor* Character);

	/** Whether a volley of Artillery Barrage this creature called is in the air now. */
	static bool IsArtilleryBarrageInTheAir(const AActor* Character);

	/**
	 * Whether a row belongs to a Cataclysm a creature of this one may draw.
	 *
	 * PUBLIC BECAUSE A TEST ASKS IT DIRECTLY, and because it is the whole of the
	 * pool rule in one place: its own column, or Generic.
	 */
	static bool IsDrawableBy(const FCataclysmEnemyModifierRow& Row,
							 FName CataclysmType);
};
