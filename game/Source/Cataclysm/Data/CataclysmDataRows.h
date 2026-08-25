// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "CataclysmDataRows.generated.h"

/**
 * Row types for the DataTables generated from the design workbook.
 *
 * Each struct here matches one CSV in game/Data/, produced by
 * tools/generate_datatables.py from docs/All_Things_Cataclysm.xlsx.
 *
 * The property names ARE the CSV column headers. Renaming one here without
 * renaming it in the generator makes the column import as its default value --
 * silently, with no error. An automation test loads every CSV through its struct
 * so that mismatch fails the build instead.
 *
 * Several fields are FString where a number or an enum would be nicer. That is
 * deliberate: the source data is not yet consistent enough to be typed, and
 * coercing it would mean inventing values. Each case is commented and tracked.
 */

/** One dungeon modifier, weighted per Cataclysm. Source: Dungeon Modifiers. */
USTRUCT(BlueprintType)
struct FCataclysmDungeonModifierRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon Modifier")
	FString CataclysmType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon Modifier")
	FString ModifierName;

	/** Selection weight. Higher is more common. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon Modifier")
	float Weight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon Modifier")
	FString Description;
};

/**
 * One cell of the weapon-and-damage-type skill matrix. Source: Weapon Skills.
 *
 * Most rows currently have an empty SkillName and SkillDescription: only the War
 * damage type has been designed. The rows exist so the matrix's shape is
 * explicit, and so an undesigned combination is visible rather than absent.
 */
USTRUCT(BlueprintType)
struct FCataclysmWeaponSkillRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Skill")
	FString WeaponType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Skill")
	FString DamageType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Skill")
	FString Slot;

	/** Empty until the skill is designed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Skill")
	FString SkillName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Skill")
	FString SkillDescription;

	/** Comma-separated gameplay tags. Every one is checked at generation time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Skill")
	FString Tags;

	/**
	 * Which shared template runs this skill: Strike, Projectile, SelfBuff,
	 * Movement, Summon, Aura or Debuff. Empty means the skill has a name and a
	 * description but no behaviour yet.
	 *
	 * DELIBERATELY NOT READ OFF Tags, which already carries Type.Projectile and
	 * the rest. Two reasons. The tags do not decide it -- Molten Cleave carries
	 * Type.AOE.PointBlank, Type.Strike and Type.AOE.Persistent at once, and
	 * Infernal Plunge is a leap and carries no tag saying so. And the tags have
	 * a job already: UCataclysmStatPipeline::ModifierApplies scopes every gear
	 * increase by the tags of the skill in hand, so dispatching on them too
	 * would mean adding a tag to fix a shape silently changed which gear
	 * applied to it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Skill")
	FString Shape;

	/**
	 * The template's numbers, as `Key=Value` separated by semicolons.
	 *
	 * A bag rather than a column each, because the seven shapes read different
	 * numbers and the union of them is over a dozen, of which a row fills two or
	 * three. Path of Exile stores per-skill numbers the same way, as named stat
	 * entries rather than columns. The generator refuses a key the shape does
	 * not read, so a misspelling fails generation instead of reading as zero.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Skill")
	FString ShapeParams;

	/**
	 * This skill's own base critical strike chance, or -1 to take the default.
	 *
	 * THE DESIGN PUTS THIS ON THE SKILL. Its stat source table says "the skill
	 * being used" supplies critical strike chance, and the sentence after it is
	 * "A character has no critical strike chance in the abstract."
	 * See `docs/Cataclysm_GDD_v2.md` lines 858 and 866.
	 *
	 * -1 MEANS THE ROW SAYS NOTHING, and that is the ordinary case: every one of
	 * the 398 rows is blank today, so every skill in the game takes the 5%
	 * default in `UCataclysmWeaponSlotsComponent::DefaultSkillCritChancePercent`.
	 *
	 * ZERO CANNOT MEAN THAT, which is the whole reason a sentinel is needed. The
	 * decision of 2026-08-04 recorded in `docs/DECISIONS.md` says the 5% is "a
	 * default and not a floor: a skill that states 1% gets 1%, which is what lets
	 * a skill be designed to crit less than average". A skill built never to
	 * critically strike states 0, so 0 has to be a real answer. Issue #657.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Skill")
	float CritChancePercent = -1.0f;

	/**
	 * What one use of this skill deals, as a percentage of weapon damage.
	 * -1 takes the figure for whichever slot it is in.
	 *
	 * A SLOT IS A KEY AND A SKILL IS WORTH WHAT IT IS WORTH. The project
	 * owner decided on 2026-08-22 that any skill may go in any slot. Damage,
	 * cooldown and mana cost used to come only from the slot, so the same
	 * skill would have been worth 250% of weapon damage on the right mouse
	 * button and 400% on R -- its power following the key rather than the
	 * skill. `docs/DECISIONS.md` has the reasoning.
	 *
	 * EVERY ROW IS -1 TODAY AND THAT IS DELIBERATE. The mechanism landed
	 * before the numbers so that nothing changed until one is written; -1
	 * falls back to the slot's figure, which is what the game did before.
	 * Writing the 112 designed skills' numbers is the rest of issue #836.
	 *
	 * ZERO CANNOT MEAN "SAYS NOTHING", which is why the sentinel is -1 here
	 * as it is for CritChancePercent above. A Support skill deals 0% of
	 * weapon damage by design; the slot's own table says so.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Skill")
	float DamagePercent = -1.0f;

	/** Seconds before this skill may be used again. -1 takes the slot's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Skill")
	float Cooldown = -1.0f;

	/** Mana one use costs at character level 100. -1 takes the slot's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Skill")
	float ManaCost = -1.0f;
};

/**
 * One enchantment roll. Source: Enchantments.
 *
 * Positives and negatives are separate tables because they roll independently:
 * a strong positive is not guaranteed to arrive with a weak negative. IsNegative
 * records which table a row came from, so both can be searched as one set.
 */
USTRUCT(BlueprintType)
struct FCataclysmEnchantmentRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enchantment")
	FString Effect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enchantment")
	FString EnchantmentType;

	/** 1 is rare and powerful, 4 is common and modest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enchantment")
	float Weight = 0.0f;

	/** Comma-separated tags deciding which items this can appear on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enchantment")
	FString Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enchantment")
	bool IsNegative = false;
};

/** One enemy modifier. Source: Enemy Modifiers, which is stored as a matrix. */
USTRUCT(BlueprintType)
struct FCataclysmEnemyModifierRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Modifier")
	FString CataclysmType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Modifier")
	FString ModifierName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Modifier")
	FString Description;
};

/** A buff, debuff or damage-over-time effect. Source: Buffs, Debuffs, DoTs. */
USTRUCT(BlueprintType)
struct FCataclysmStatusEffectRow : public FTableRowBase
{
	GENERATED_BODY()

	/** "Buff", "Debuff" or "DoT". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	FString EffectKind;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	FString EffectName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	FString Description;

	/**
	 * Seconds the effect lasts. Zero means the design has not stated one.
	 *
	 * It was added because every one of the sixteen designed Demonic skills
	 * applies burn and the design stated neither how long it lasts nor what it
	 * deals, so "sets each one alight" applied an effect made of nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	float DurationSeconds = 0.0f;

	/**
	 * What ONE TICK deals, as a plain amount.
	 *
	 * The base of five of the six damage over time effects: Bleed, Poison,
	 * Disease, Burn and Necrosis. The base tick is one second, so Burn's 4
	 * seconds and 25 is 25 damage every second for four seconds, which is 100
	 * altogether before the attacker's three damage over time stats.
	 *
	 * A FLAT AMOUNT RATHER THAN A PERCENT OF THE HIT, chosen by the project
	 * owner on 2026-08-24. A percent of the hit multiplies twice: the hit itself
	 * grows about fifteenfold across the eight difficulty tiers, and the three
	 * damage over time stats multiply on top of that, so a percent-of-hit burn
	 * reaches thirteen times a Common enemy's health from one application at
	 * twelve affix slots at tier 8. A flat amount grows only with those stats,
	 * which very nearly track enemy health on their own, so it stays level.
	 * `docs/DECISIONS.md` carries the measurements.
	 *
	 * Zero means the design has not stated one, and an effect worth zero
	 * applies nothing rather than applying silently for no damage.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	float FlatDamagePerTick = 0.0f;

	/**
	 * What ONE TICK deals, as a percent of the hit that applied it.
	 *
	 * NOTHING STATES ONE AS OF 2026-08-24. This was Burn's base until the
	 * decision recorded on FlatDamagePerTick above moved it to a flat amount.
	 * It is kept rather than removed because a skill stating its own effect is
	 * the obvious future caller, and because removing it would churn this
	 * struct, the generator and two automation tests to no purpose.
	 *
	 * NOT A TOTAL SPREAD ACROSS THE DURATION, which is what this said and what
	 * the engine did until 2026-08-24. Under that reading raising the tick rate
	 * divided the same total into more, smaller ticks, so Damage over Time
	 * Frequency could not be worth anything -- and the design's stated reason
	 * for having three separate damage over time stats is that all three
	 * multiply. `docs/DECISIONS.md` carries that decision too.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	float PercentOfHit = 0.0f;

	/**
	 * The effect's own magnitude, in whatever unit its description names.
	 *
	 * Cripple's 30% slow, Weaken's 20% damage reduction, Shred's 10 resistance,
	 * Necrosis's 100% denial of healing. Zero means the effect has no strength
	 * axis at all, which is true of five of the six damage over time effects and
	 * of Madness and Stun.
	 *
	 * Added for issue #904, along with the three fields below. Before it, four
	 * player-applied debuffs stated their strength only in prose in Description
	 * and everything reading the table saw zero.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	float Strength = 0.0f;

	/**
	 * Where Strength stops rising and the magnitude extends the duration
	 * instead. Cripple and Weaken cap at 80, Necrosis at 100 -- and Necrosis
	 * starts at its cap, so its magnitude extends the duration from the first
	 * point rather than ever raising the strength.
	 *
	 * ZERO MEANS NO NUMERIC CAP, NOT A CAP OF ZERO. Shred is the reason the
	 * distinction matters: its cap is the target's own resistance reaching zero,
	 * which is a property of whatever it is applied to rather than a number
	 * belonging to the effect, so it cannot be written here at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	float StrengthCap = 0.0f;

	/**
	 * Where DurationSeconds stops rising. Zero means no cap.
	 *
	 * Only Stun has one, at 3 seconds, and Stun is the one effect whose scaling
	 * stops dead rather than rolling over into something else: its magnitude IS
	 * its duration, so there is nothing left to roll into. The design gives the
	 * reason -- a stun as long as the 5 second immunity window would hold a
	 * target for ever.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	float DurationCap = 0.0f;

	/**
	 * What one tick deals as a percent of the target's CURRENT health, for an
	 * effect measured against what it is applied to rather than against the hit.
	 *
	 * Only Void Splinter uses it, at 1% a second over 4 seconds. Because it is
	 * a share of current rather than maximum health it falls as the target does
	 * and can never finish anything off on its own.
	 *
	 * IT CANNOT GO THROUGH THE ORDINARY DAMAGE OVER TIME PATH, which computes one
	 * fixed amount per tick up front. A share of current health is a different
	 * amount every tick because current health changes between them. That is
	 * part of why nothing implements Void Splinter yet. Issue #915 also records
	 * that the damage over time stats multiply this percentage, which at twelve
	 * affix slots would remove about three quarters of a boss's health from one
	 * application.
	 *
	 * A SEPARATE FIELD RATHER THAN A STRING NAMING WHICH BASIS APPLIES. A
	 * misspelled basis would silently read as one of the other two and apply the
	 * wrong arithmetic with nothing reporting an error; a number cannot be
	 * misspelled. An effect states exactly one of FlatDamagePerTick,
	 * PercentOfHit and this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	float PercentOfCurrentHealth = 0.0f;
};

/**
 * A gem and its value at each of the eight rarity tiers. Source: Gems.
 *
 * All eight tiers have a value. In the sheet the Everyday value is written into
 * the effect text -- "10% chance to apply void splinter" means Everyday is 10%
 * -- and the seven numeric columns continue the series from there. The generator
 * extracts it, so all eight arrive here as numbers.
 *
 * Values are fractions: 0.3 is 30%.
 */
USTRUCT(BlueprintType)
struct FCataclysmGemRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gem")
	FString GemName;

	/** States the Everyday value in prose; also carried as Everyday below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gem")
	FString Effect;

	/** "Attack", "Defense" and so on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gem")
	FString GemType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gem") float Everyday = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gem") float Quality = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gem") float Superb = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gem") float Masterful = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gem") float Legendary = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gem") float Mythical = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gem") float Ascendant = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gem") float Cataclysmic = 0.0f;
};

/**
 * How a city upgrade tier value should be read.
 *
 * The source sheet has no column saying which kind a cell is; the generator
 * infers it from the cell's notation and the effect text. See parse_tier in
 * tools/generate_datatables.py.
 */
UENUM(BlueprintType)
enum class ECataclysmTierValueKind : uint8
{
	/** Empty cell: the upgrade has no value at this tier. */
	None			UMETA(DisplayName = "None"),

	/** A percentage increase, stored as a fraction. 0.3 means 30%. */
	Percent			UMETA(DisplayName = "Percent"),

	/** A flat improvement in whatever unit the effect names: days, floors,
	 *  a count of dungeons. */
	Flat			UMETA(DisplayName = "Flat"),

	/** Multiplies the effect. 3 means three times. */
	Multiplier		UMETA(DisplayName = "Multiplier"),

	/** Two values at once. The effect reads "every X days ... Y%", and the tier
	 *  improves both: IntervalDays falls and Value rises. */
	IntervalPercent	UMETA(DisplayName = "Interval and Percent"),
};

/**
 * One city upgrade and its tier 2 and tier 3 scaling. Source: City Upgrades.
 *
 * Each tier carries the raw cell text alongside the parsed values, so nothing is
 * lost if the generator's inference is ever wrong for a row.
 */
USTRUCT(BlueprintType)
struct FCataclysmCityUpgradeRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Architect, Explorer, Treasurer or Artisan. Empty when undecided; see
	 *  BranchUndecided. The trailing asterisk used in the source sheet is
	 *  stripped here and carried as IsOneTimeUse instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade")
	FString Branch;

	/** True for the one upgrade whose branch has not been chosen yet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade")
	bool BranchUndecided = false;

	/**
	 * Fires once and is spent, rather than being a standing improvement.
	 *
	 * These do not scale, so their tier fields are usually empty. The one
	 * upgrade with no branch is the extreme case: no tiers at all, and intended
	 * as a last resort rather than a city improvement.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade")
	bool IsOneTimeUse = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade")
	FString Effect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade|Tier 2")
	FString Tier2Raw;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade|Tier 2")
	FString Tier2Kind;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade|Tier 2")
	float Tier2Value = 0.0f;

	/** Only meaningful when Tier2Kind is IntervalPercent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade|Tier 2")
	float Tier2IntervalDays = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade|Tier 3")
	FString Tier3Raw;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade|Tier 3")
	FString Tier3Kind;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade|Tier 3")
	float Tier3Value = 0.0f;

	/** Only meaningful when Tier3Kind is IntervalPercent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade|Tier 3")
	float Tier3IntervalDays = 0.0f;
};

/**
 * One item base within a slot, and what it inherently grants. Source: Item Bases.
 *
 * THE IMPLICIT BELONGS TO THE BASE, NOT THE SLOT. A chest built for armour and
 * one built for evasion are different bases in the same slot, and choosing
 * between them is a defensive layer committed to before any affix is involved.
 *
 * VALUES ARE THE STATED ONES: the fully upgraded (+10) figures the design
 * document quotes. For a two-handed weapon that is the figure BEFORE the
 * two-handed multiplier doubles it, so a Greatsword reads 78 here and supplies
 * 156 in play. Anything reading these must apply ValueMultiplier itself; see
 * TWO_HANDED_MULTIPLIER in sim/cataclysm_sim/affixes.py for why it is 2.
 */
USTRUCT(BlueprintType)
struct FCataclysmItemBaseRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base")
	FString BaseName;

	/** Head, Chest, Shoulders, Gloves, Pants, Boots, Belt, Ring, Necklace,
	 *  Relic or Weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base")
	FString Slot;

	/** 1 or 2 for a weapon, 0 for anything else. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base|Weapon")
	int32 Hands = 0;

	/** Piercing, Slashing, Blunt or Magic. Empty for anything but a weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base|Weapon")
	FString SubType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base|Weapon")
	FString WeaponType;

	/** THE MOST damage types this weapon can ever hold, not how many it holds.
	 *  Four for a one-hander, eight for a two-hander. How many a particular
	 *  weapon holds, and which ones, is rolled when the item drops, from one up
	 *  to the lower of this number and the difficulty tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base|Weapon")
	int32 MaxDamageTypes = 0;

	/**
	 * How many cells wide and high the piece is in the carried bag.
	 *
	 * From the footprint table in `docs/Inventory_Screen_Design.md`, which
	 * #854 settled: a ring is 1 by 1, a chest 2 by 3, a two-handed weapon 2 by
	 * 6. Issue #855.
	 *
	 * EVERY BASE HAS ONE, and tools/generate_datatables.py refuses to write
	 * the file if any is missing or below one. A zero would read as a piece
	 * that takes no room rather than as a base somebody forgot.
	 *
	 * NOTHING READS THESE YET. The carried bag is still a flat array of 48
	 * slots where every item takes one, and rebuilding it to pack rectangles
	 * is the rest of #855. These are the data that has to exist first.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base")
	int32 CellsWide = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base")
	int32 CellsHigh = 1;

	/**
	 * Attacks per second before any increase. Zero on anything that is not a
	 * weapon.
	 *
	 * The design document says the equipped weapon is where this base comes
	 * from. Without it every increased attack speed affix in the game multiplies
	 * zero and is worth nothing, which was issue #120.
	 *
	 * DELIBERATELY NOT AN IMPLICIT. A two-handed weapon doubles every implicit
	 * it carries, which is correct for damage and would be nonsense here: a
	 * Greatsword would swing twice as fast as a Sword. Path of Exile and Last
	 * Epoch both treat a weapon's rate as an intrinsic property listed apart
	 * from its modifiers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base|Weapon")
	float AttackSpeed = 0.0f;

	/**
	 * The basic attack's shape -- "Strike" for a melee weapon, "Projectile" for a
	 * ranged one -- with that shape's parameters in the cell beside it.
	 *
	 * THE BASIC ATTACK LIVES ON THE WEAPON, NOT IN THE SKILL MATRIX. Every other
	 * slot names a designed skill per weapon type AND damage type, which is what
	 * the 398 rows of game/Data/WeaponSkills.csv are. The basic attack does not
	 * vary by damage type -- the design document calls it weapon damage itself --
	 * so it sits here as 13 entries rather than 75 near-identical matrix rows.
	 * Decided 2026-08-15 on issue #524, and putting it here keeps the design
	 * document's statement that the matrix holds one skill per NON-BASIC slot
	 * true.
	 *
	 * EMPTY ON A WEAPON THAT GRANTS NO ATTACK DAMAGE, which is the Shield: there
	 * is no hit to compose from it. Issue #619. tools/generate_datatables.py
	 * refuses both mistakes that matter -- an armed weapon with no basic attack,
	 * and an unarmed one that states one.
	 *
	 * NO RIDERS IN THE PARAMETERS. A basic attack is 100% weapon damage and
	 * nothing else, which is what makes it the anchor every other slot's
	 * percentage is measured against. A burn or a patch of ground here would move
	 * the anchor and quietly change what every other slot's percentage means.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base|Weapon")
	FString BasicShape;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base|Weapon")
	FString BasicShapeParams;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base") FString Implicit1Stat;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base") FString Implicit1Kind;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base") float Implicit1Value = 0.0f;

	/** Empty when the base grants only one implicit, which most do. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base") FString Implicit2Stat;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base") FString Implicit2Kind;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Base") float Implicit2Value = 0.0f;
};

/**
 * One rollable affix. Source: Affixes.
 *
 * FOUR KINDS SHARE ONE TABLE, distinguished by AffixKind, because they are one
 * pool as far as a drop is concerned:
 *
 *   Stat        one stat, flat or increased, worth TopValue at tier 7
 *   Resistance  a family covering Breadth damage types at TopValue each
 *   Ailment     a TopValue% chance to apply Ailment, which Gem also applies
 *   Hybrid      HybridPart1 and HybridPart2 at a reduced share each
 *
 * Fields that do not apply to a kind are empty or zero. That is deliberate
 * rather than untidy: splitting into four tables would mean a drop had to roll
 * against four pools and know their relative weights.
 *
 * PREFIXES AND SUFFIXES ARE SEPARATE POOLS, two of each per piece. A stat that
 * appears as a prefix never appears as a suffix, which is what stops one item
 * carrying four of whatever is strongest.
 */
USTRUCT(BlueprintType)
struct FCataclysmAffixRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FString AffixName;

	/** "Stat", "Resistance", "Ailment" or "Hybrid". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FString AffixKind;

	/** "prefix" or "suffix". Two of each per piece, from separate pools. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FString Position;

	/** The character sheet stat this grants. Empty for Resistance and Hybrid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FString Stat;

	/** "flat" or "increased", deciding which bucket of the stat pipeline this
	 *  enters. An affix is never a "more" multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FString ValueKind;

	/** The tier 7 value at gear level +10. Lower tiers are N/7 of it, and every
	 *  tier is a range reaching 25% below its top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	float TopValue = 0.0f;

	/** How many damage types a Resistance family covers: 1, 2 or all 8. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	int32 Breadth = 0;

	/** Which effect an Ailment affix applies. Empty for the other kinds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FString Ailment;

	/** The gem that applies the same effect, and more strongly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FString Gem;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix") FString HybridPart1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix") FString HybridPart2;

	/** Comma-separated slot names. Every one is checked at generation time
	 *  against the slots the item bases actually occupy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FString AllowedSlots;

	/** The word this affix gives an item's name, for a suffix only.
	 *
	 *  An item is called `<rarity> <base> of <word>`, and the word comes from
	 *  its own strongest suffix affix, so "of the Leech" says the piece really
	 *  carries life leech. The first word is the rarity, so a prefix has nowhere
	 *  in the name to appear and carries none. The generator refuses a suffix
	 *  without a word, a prefix with one, and two affixes sharing a word. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FString NameWord;
};

/**
 * One of the eight gear rarities: how often it drops, and what a drop of it
 * arrives carrying. Source: Gear Rarity.
 *
 * THE ROW KEY IS THE RARITY'S OWN NAME, and it is also the name of the matching
 * ECataclysmRarity entry. That is deliberate and it is the whole join between
 * this table and the engine. A DataTable is a map with no inherent row order, so
 * the ladder has to come from somewhere: FCataclysmEnemyRarityRow carries a Step
 * column for exactly that reason. Gear rarity does not need one, because
 * ECataclysmRarity already states the order and RarityComposition in
 * CataclysmItem.cpp is already indexed by it. A second copy of the ladder here
 * would be a number to keep in step rather than a fact.
 *
 * `tools/tests/test_generated_loot_tables_match_the_model.py` asserts the eight
 * row names are exactly the eight enum entries, in order, so a renamed rarity
 * fails on a pull request rather than silently looking up nothing.
 *
 * RARITY IS STILL NOT A FIELD ON AN ITEM. This table says what a rarity means
 * for a DROP. What an item that already exists IS remains computed from the four
 * slots it filled; see UCataclysmItemValues::RarityOf.
 */
USTRUCT(BlueprintType)
struct FCataclysmGearRarityRow : public FTableRowBase
{
	GENERATED_BODY()

	/** "Everyday" through "Cataclysmic". The same text as the row key, so the
	 *  table reads on its own without resolving the key against the enum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gear Rarity")
	FString Rarity;

	/** This rarity's share of every reachable rung when a drop rolls. Falls as
	 *  rarity rises: 15625 for Everyday down to 1 for Cataclysmic, summing to
	 *  25,531, so one drop in 25,531 is Cataclysmic with no magic find. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gear Rarity")
	float DropWeight = 0.0f;

	/** The upgrade level a piece must be before it can be this rarity: +4 for
	 *  Legendary, +6 Mythical, +8 Ascendant, +10 Cataclysmic, and 0 for the four
	 *  below them.
	 *
	 *  A FLOOR ON A DROP, NOT A FILTER. A drop that rolls Legendary arrives at
	 *  +4 or better rather than being downgraded, which is what lets magic find
	 *  do its job at a low difficulty tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gear Rarity")
	int32 GearLevelGate = 0;

	/** The least Cataclysmic Residue a drop of this rarity carries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gear Rarity")
	float ResidueOnDropLowest = 0.0f;

	/** The most. The bands of neighbouring rarities overlap on purpose: a lucky
	 *  Superb piece arrives cheaper to improve than an unlucky Masterful one,
	 *  and residue is a cost rather than a reward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gear Rarity")
	float ResidueOnDropHighest = 0.0f;

	/**
	 * The colour an item of this rarity has its name drawn in.
	 *
	 * THIS IS HOW A PLAYER READS A RARITY OFF THE FLOOR. A drop is shown as its
	 * own name rather than as a model, so the colour is doing the work an item
	 * silhouette does in other games.
	 *
	 * LINEAR, CONVERTED FROM THE sRGB THE WORKBOOK HOLDS, the same way
	 * FCataclysmElementVisualRow's colours are. A colour picker shows sRGB and
	 * an FLinearColor is linear, so feeding the raw figures through would render
	 * a visibly different colour from the one that was chosen.
	 *
	 * COLOUR IS NOT ALLOWED TO BE THE ONLY CHANNEL. The Interface Colour section
	 * of docs/Cataclysm_GDD_v2.md requires the drop marker to differ by shape or
	 * motion as well, so that a player who cannot separate two hues can still
	 * separate two rarities.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gear Rarity")
	FLinearColor Colour = FLinearColor::White;
};

/**
 * The most gem sockets a piece in one gear slot can have. Source: Item Sockets.
 *
 * TWO ROWS FOR A WEAPON, keyed Weapon_1H and Weapon_2H, because its maximum
 * depends on how many hands it takes: three for a one-hander and six for a
 * two-hander, so two one-handed weapons match one two-hander exactly. Every
 * other slot has one row and a Hands of 0, which is how the item base table
 * writes a non-weapon too.
 *
 * POTION SLOTS ARE NOT IN THIS TABLE. Four of them carry one socket each, but
 * they are consumables rather than gear and nothing rolls one as a drop. They
 * are the difference between the 41 sockets here and the 45 the design states
 * across all equipment.
 */
USTRUCT(BlueprintType)
struct FCataclysmItemSocketRow : public FTableRowBase
{
	GENERATED_BODY()

	/** "Head", "Chest", ... or "Weapon". Matches FCataclysmItemBaseRow::Slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Sockets")
	FString Slot;

	/** 1 or 2 for a weapon, 0 for everything else. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Sockets")
	int32 Hands = 0;

	/** The ceiling, not the count. A drop rolls uniformly from no sockets up to
	 *  this, so a tier 1 Chest can arrive with all six and an Add Socket craft
	 *  has something to do. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item Sockets")
	int32 MaxSockets = 0;
};

/**
 * How heavily one affix tier is weighted when a drop rolls an affix.
 * Source: Affix Tiers.
 *
 * EACH TIER IS HALF AS LIKELY AS THE ONE BELOW: 64, 32, 16, 8, 4, 2, 1. So a T7
 * affix is one in 127 at difficulty tier 8, where all seven are reachable, and
 * half of every affix that drops is a T1.
 *
 * WHICH TIERS ARE REACHABLE is a separate question, decided by the difficulty
 * tier plus one and capped at T7. This table only says how the reachable ones
 * are weighted against each other.
 *
 * AND CRAFTING HAS NO TIER GATE, which is why a rare high tier is not punishing:
 * a drop is the raw material and the Potency Crystal is how a build reaches T7.
 */
USTRUCT(BlueprintType)
struct FCataclysmAffixTierRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 1 to 7. Also the row key, written "T1" through "T7". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix Tier")
	int32 Tier = 0;

	/** This tier's share of every reachable tier's weight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix Tier")
	float DropWeight = 0.0f;
};

/**
 * One class's value for one stat: its level 1 base and its per-level gain.
 * Source: Class Stats.
 *
 * THE DEFAULT LINE IS A ROW SET, NOT A SPECIAL CASE. A class named "Default"
 * carries the stat line every class inherits, and each real class overrides only
 * the stats that express its identity. There are 33 stats and 24 classes
 * planned, so writing every class out in full would be 792 rows of which almost
 * all would repeat.
 *
 * That shape is also what the design means by a class: the three War trees each
 * commit to three or four stats and ignore the rest, so a class is defined as
 * much by what it refuses as by what it takes.
 *
 * Resolve a class's value for a stat by looking for that class's row, then the
 * Default row, then zero. UCataclysmClassStats does it.
 */
USTRUCT(BlueprintType)
struct FCataclysmClassStatRow : public FTableRowBase
{
	GENERATED_BODY()

	/** "Default", or a class name such as "Ravager". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Class Stats")
	FString ClassName;

	/** A character sheet stat name, such as "max_health". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Class Stats")
	FString Stat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Class Stats")
	float Base = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Class Stats")
	float PerLevel = 0.0f;
};

/**
 * One minion type's own stat block. Source: Minion Types.
 *
 * A MINION IS NOT A PERCENTAGE OF ITS SUMMONER. Each named type carries its own
 * health, damage, attack interval, movement, reach and threat, and the summoner's
 * LEVEL is the only thing that raises them. Gear does not cross unless a modifier
 * names minions.
 *
 * WHY THIS IS A TABLE AND NOT NUMBERS IN THE SKILL ROW. Two skills can produce
 * the same creature -- Summon Imp and Open the Rift both make a lesser imp -- so
 * numbers held on the skill would exist twice and drift apart. One skill can also
 * produce two kinds, as Iron Fortress deploys ballistae and spike traps, which a
 * flat list of skill parameters cannot express at all. A skill decides how many,
 * how often and how long; this decides what the thing is.
 *
 * Health and damage read Base + PerLevel * SummonerLevel, the same shape
 * FCataclysmClassStatRow uses for character stats.
 */
USTRUCT(BlueprintType)
struct FCataclysmMinionTypeRow : public FTableRowBase
{
	GENERATED_BODY()

	/** "Creature" or "Machine". Decides which attribute scales its damage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Types")
	FString Family;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Types")
	float BaseHealth = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Types")
	float HealthPerLevel = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Types")
	float BaseDamage = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Types")
	float DamagePerLevel = 0.0f;

	/** Seconds between this minion's attacks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Types")
	float AttackIntervalSeconds = 0.0f;

	/** Metres per second, matching movement_speed on the character sheet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Types")
	float MoveSpeed = 0.0f;

	/**
	 * How much attention this minion draws, as a percentage. A turret sits near
	 * zero and an imp at 100, which is how a decoy and a turret are one number
	 * rather than two behaviours.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Types")
	float ThreatPercent = 0.0f;

	/** How far it can hit, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Types")
	float ReachCm = 0.0f;

	/** How far it notices an enemy, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Types")
	float NoticeRadiusCm = 0.0f;

	/** "Nearest" or "Furthest". The Ballista deliberately picks the furthest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Types")
	FString TargetMode;

	/**
	 * What this minion answers to, comma separated. Every row carries
	 * Type.Minion, exactly one of Minion.Creature or Minion.Machine, and
	 * whatever narrower tags describe how it fights.
	 *
	 * SCALING IS LOOKED UP BY TAG, not written here. FCataclysmMinionScalingRow
	 * says what one point of an attribute grants a minion carrying a tag, so a
	 * minion can answer to more than one attribute and to more than one stat
	 * without this row changing shape.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Types")
	FString Tags;
};

/**
 * What one point of an attribute grants a minion carrying a tag.
 * Source: Minion Scaling.
 *
 * WHY NOT ROWS IN THE ATTRIBUTES TABLE. That one is (attribute, stat, percent
 * per point) with no tag, and everything reading it sums every attribute that
 * names a stat. One shared "minion damage" entry would let a summoner's Agility
 * raise a summoned creature, which the design forbids: the attribute is declared
 * per minion type. The tag is what makes the scoping real.
 *
 * It is the same shape a tag-scoped modifier already uses -- a stat, an amount,
 * and the tags it requires -- so the minion gear affixes need no new machinery.
 */
USTRUCT(BlueprintType)
struct FCataclysmMinionScalingRow : public FTableRowBase
{
	GENERATED_BODY()

	/** A primary attribute name, such as "spirit". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Scaling")
	FString Attribute;

	/** Only minions carrying this tag are raised by it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Scaling")
	FString RequiresTag;

	/** Which of the minion's own stats this raises: "damage" or "health". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Scaling")
	FString Stat;

	/** Percent per point. 1.0 means 100 points doubles the stat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minion Scaling")
	float PercentPerPoint = 0.0f;
};

/**
 * What one point of an attribute is worth. Source: Attributes.
 *
 * ATTRIBUTES ONLY EVER SCALE. A point adds to a stat's sum of increases and the
 * sum multiplies the base, so a point does nothing on a stat with no base. That
 * is the design working rather than failing: it is why a class that wants to
 * scale a stat with attributes has to be given a base for it first.
 *
 * Values are PERCENT PER POINT, matching the stat pipeline: Vitality reads 2,
 * meaning 60 points of it contribute 120 percentage points of increase.
 */
USTRUCT(BlueprintType)
struct FCataclysmAttributeEffectRow : public FTableRowBase
{
	GENERATED_BODY()

	/** One of the eight: Agility, Ferocity, Constitution, Vitality, Mind,
	 *  Spirit, Efficacy or Luck. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	FString Attribute;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	FString Stat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	float PercentPerPoint = 0.0f;
};

/**
 * What a skill in one of the seven slots is worth, waits, and costs.
 * Source: Skill Slots.
 *
 * PER SLOT RATHER THAN PER SKILL, and that is deliberate. No designed skill
 * states its own cooldown or mana cost, so a column on the Weapon Skills sheet
 * would be 77 copies of seven values. A skill states its own figure only when
 * it differs, which is what Skull Splitter does at 500% weapon damage.
 *
 * WHY IT EXISTS AT ALL. These numbers lived only in the Python model, where
 * nothing in the game could reach them, so no ability could honour a cooldown
 * or a cost. Meanwhile the design already had a cooldown reduction formula, an
 * attribute scaling it, an affix granting it and 41 enchantments mentioning it,
 * all dividing zero. That is issue #155.
 *
 * MANA COST IS QUOTED AT LEVEL 100, as every other figure in this project is,
 * and scales down with character level along the default mana progression. It
 * is the same number for every class, which is what makes a larger mana pool
 * buy more casts rather than a proportionally larger price per cast.
 */
USTRUCT(BlueprintType)
struct FCataclysmSkillSlotRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Basic, Heavy, Special, Support, Aura, Ultimate or Movement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Slot")
	FString Slot;

	/** Percent of weapon damage one use deals. For the Aura, per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Slot")
	float DamagePercent = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Slot")
	float DamageLowest = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Slot")
	float DamageHighest = 0.0f;

	/** Seconds before the skill can be used again, before any reduction.
	 *  Zero only for the Basic Attack, which is automatic, and the Aura,
	 *  which is a toggle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Slot")
	float Cooldown = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Slot")
	float CooldownLowest = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Slot")
	float CooldownHighest = 0.0f;

	/** Mana one use costs at level 100. For the Aura, per second while on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Slot")
	float ManaCost = 0.0f;

	/** Mana restored per hit, at level 100. Only the Basic Attack has any:
	 *  it is automatic, so this is income for being in a fight rather than a
	 *  generator the player has to press. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Slot")
	float ManaOnHit = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Slot")
	FString Note;
};

/** A crafting material. Source: Crafting. */
USTRUCT(BlueprintType)
struct FCataclysmCraftingMaterialRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting") FString MaterialName;

	/**
	 * Which material tier this belongs to, 1 to 5, or 0 for a crafting action.
	 *
	 * THE CRAFTING SHEET HOLDS TWO KINDS OF ROW. Twenty-seven are materials a
	 * player can hold and the rest are actions a player can take -- "Reroll
	 * Affix Value", "Add Socket" -- which have no tier because they are not
	 * things that drop. Zero is what says so, and it is what stops a drop
	 * rolling an action.
	 *
	 * READ OFF TierAndSource AT GENERATION rather than here. That cell is prose
	 * -- "Tier 3 (Rare). Drop from Dungeon Bosses/Elites." -- and the generator
	 * already parses it to check each tier's material count, so publishing the
	 * number costs nothing and saves the engine parsing English at runtime.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting") int32 Tier = 0;

	/**
	 * How far this stone upgrades a piece of gear, 1 to 10, or 0 for anything
	 * that is not an upgrade stone.
	 *
	 * THIS IS NOT THE TIER. The tier above is the rarity band the stone drops
	 * in; this is what it does when it is used. The ten stones were spread two
	 * to a band, so a band holds two different levels and neither number can be
	 * worked out from the other.
	 *
	 * READ OFF THE NAME AT GENERATION, like the tier and for the same reason.
	 * The stones are named "Upgrade Stone +1" through "+10", so the level is
	 * already stated and the engine would otherwise parse a name for every
	 * material that drops.
	 *
	 * WHAT READS IT: UCataclysmDropRoll::RollMaterial, which will not drop a
	 * stone above what the difficulty tier being played allows.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting") int32 UpgradeLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting") FString TierAndSource;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting") FString PrimaryUse;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting") FString Functions;

	/** Which cost the material affects, for example "Gold Cost Multiplier". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting") FString CRMetric;

	/** Written as a human-readable expression, for example "(CR / 50) + 1".
	 *  Not machine-evaluated; the Forge implementation must encode it in code. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting") FString Formula;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crafting") FString Outcome;
};

/**
 * What one damage type's effects look like. Source: Element Visuals.
 *
 * EIGHT ROWS INSTEAD OF EIGHT COPIES OF EVERY EFFECT. Section 5 of
 * docs/Niagara_Conventions.md is the reason this table exists: there are eight
 * effect shapes and eight damage types, and authoring one asset per pair is 64
 * assets that all have to be changed together. Authoring one asset per shape and
 * reading its colours from here is 8 assets and these 8 rows. A Niagara system
 * looks up the row for the damage type it was spawned with and takes its colours
 * from it, so nothing in the system knows which damage type it is drawing.
 *
 * THE ROW KEY IS THE TAG'S LEAF. `Element.Demonic` keys the row `Demonic`, so
 * anything holding the tag can reach the row with FGameplayTag::GetTagLeafName
 * and no second lookup table.
 *
 * THE COLOURS ARE LINEAR AND THE DESIGN DOCUMENT'S ARE sRGB. Section XIII of
 * docs/Cataclysm_GDD_v2.md writes `#FF7A2E` because that is what a colour picker
 * shows. tools/generate_datatables.py converts each one on the way into the CSV,
 * the same conversion ACataclysmTelegraphMarker::ResolveColour does with
 * FLinearColor::FromSRGBColor. Feeding a material the sRGB figures directly
 * renders a visibly different colour from the one that was designed.
 *
 * WHY THE SECONDARY IS NOT DECORATION. Each damage type's effects are seen most
 * often against that damage type's own environment -- Demonic effects on lava,
 * Celestial effects on gold. An effect coloured like its damage type is at its
 * least readable in the environment that damage type generates, so the dark
 * anchor is what carries the contrast when the primary matches the floor. The
 * design's rule behind both figures is that a world surface may not exceed 30%
 * brightness and an effect's primary may not fall below 60%.
 */
USTRUCT(BlueprintType)
struct FCataclysmElementVisualRow : public FTableRowBase
{
	GENERATED_BODY()

	/** One of the eight Element.* tags declared in
	 *  game/Config/Tags/CataclysmTags.ini. It is the key, and every declared
	 *  damage type has exactly one row: tools/generate_datatables.py refuses to
	 *  generate this table if one is missing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Element Visuals")
	FGameplayTag ElementTag;

	/**
	 * EVERY DEFAULT BELOW IS A VALUE THE TABLE NEVER CARRIES, deliberately.
	 *
	 * A CSV column whose name does not match a property imports as that
	 * property's default with no error at all, so a default equal to the real
	 * data makes a test asserting the real data vacuous. No designed primary is
	 * pure white, no designed secondary is pure black, and the generator refuses
	 * a scale of zero outright -- so a row showing any of these did not import,
	 * and Cataclysm.Data.ElementVisualsCarryTheDesignedValues says so.
	 */

	/** The hue the effect reads as. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Element Visuals")
	FLinearColor PrimaryColour = FLinearColor::White;

	/** The dark anchor that stays legible when the primary matches the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Element Visuals")
	FLinearColor SecondaryColour = FLinearColor::Black;

	/**
	 * How far above the system's own value the effect glows.
	 *
	 * Gameplay-critical effects break physically based rendering and ambient
	 * ones do not, and this column is where that break is quantified per damage
	 * type. All eight rows read 1.0 today, meaning the Niagara system's authored
	 * value is used unchanged; they are tuned once the first system exists.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Element Visuals")
	float EmissiveMultiplier = 0.0f;

	/** Denser for Pestilence, sparser for Void, once these are tuned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Element Visuals")
	float SpawnRateScale = 0.0f;

	/** Fast for War, slow-drifting for Famine, once these are tuned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Element Visuals")
	float VelocityScale = 0.0f;
};

/**
 * What KIND of creature an enemy is. Source: sim/cataclysm_sim/enemy_stats.py.
 *
 * NOT THE WORKBOOK, unlike every struct above. The enemy stat block is designed
 * in the simulation package, where its own self-checks live, and
 * tools/generate_datatables.py builds this table straight out of it. Editing
 * EnemyArchetypes.csv by hand achieves nothing: the next run overwrites it and
 * continuous integration fails in the meantime.
 *
 * TWO LAYERS, AND THEY OWN DIFFERENT THINGS. This struct is the profile, which
 * does not change with rarity: how fast the creature is, what it resists, how
 * often it crits, how wide its body is. FCataclysmEnemyRarityRow is the other
 * layer and scales magnitude only. A Legendary Imp is a bigger Imp; it is not a
 * different creature.
 *
 * THE THREE SHARE FIELDS ARE MULTIPLIERS ON A SCORE-SCALED BASE, not values. An
 * enemy's health is the encounter's Power Score times the rarity's HealthPerScore
 * times this archetype's HealthShare. A share is meaningless on its own, which is
 * why nothing here is a health figure.
 *
 * DISTANCES AND SPEEDS ARE IN METRES, as the model states them and as
 * ClassStats.csv already states the player's movement speed. Multiply by
 * ACataclysmPlayerCharacter's CentimetresPerMetre before giving one to the
 * movement component.
 *
 * ONE RESISTANCE FIGURE COVERS ALL EIGHT DAMAGE TYPES. That is deliberate and
 * the model's header explains it: player damage is adaptive, so a per-type enemy
 * profile changes no outcome. The PLAYER still has all eight defensively.
 */
USTRUCT(BlueprintType)
struct FCataclysmEnemyArchetypeRow : public FTableRowBase
{
	GENERATED_BODY()

	/** "Brute", "Imp", and so on. "Baseline" is not a creature anyone fights:
	 *  it exists so the rarity ladder can be read with every share at 1. Its
	 *  Role column says so. Do not spawn it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	FString ArchetypeName;

	/** The design document's own words for what this creature is for. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	FString Role;

	/** Which Cataclysm this enemy belongs to, and also the damage type it
	 *  deals, which decides which of the player's eight resistances applies
	 *  when it lands a hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	FString Cataclysm;

	/** Multiplier on the rarity's health per point of score. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float HealthShare = 1.0f;

	/** Multiplier on the rarity's damage per point of score. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float DamageShare = 1.0f;

	/** Multiplier on the rarity's armour per point of score. Zero means the
	 *  creature is unarmoured whatever its rarity, which is the Imp. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float ArmorShare = 1.0f;

	/** Seconds between attacks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float AttackIntervalSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float CritChancePercent = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float CritMultiplierPercent = 150.0f;

	/** How fast it moves before it has noticed the player. Zero is a creature
	 *  that never moves, which is the Corrupted Sentinel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float MoveSpeedMetresPerSecond = 4.5f;

	/** How fast it moves once it has noticed the player. ZERO IS A SENTINEL
	 *  MEANING "the same as MoveSpeed", not a creature that stops when it sees
	 *  you. Every enemy but the Brute reads zero here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float ChaseSpeedMetresPerSecond = 0.0f;

	/** Chance to avoid a direct attack outright. Direct attacks only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float EvasionPercent = 0.0f;

	/** Energy shield as a fraction of this enemy's own health, so it scales
	 *  with rarity through health rather than carrying a figure of its own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float EnergyShieldFraction = 0.0f;

	/** How wide the creature is. It decides how many of a swarm can stand
	 *  around one player at once, which is the whole of the Imp's design. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float BodyRadiusMetres = 0.48f;

	/** How fast it can turn on the spot. This, not footspeed, is what the
	 *  design means when it says a creature can be outmanoeuvred: a player
	 *  circling at the Brute's reach sweeps 223 degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float TurnRateDegreesPerSecond = 480.0f;

	/** Percent of all incoming damage resisted, whatever its type. Held below
	 *  70 by the model, because everything above the cap in
	 *  UCataclysmDamageCalculation::EffectiveResistance would change no outcome.
	 *
	 *  THAT IS NOT WHAT STOPS A CREATURE REACHING IMMUNITY, and this comment
	 *  used to say it was. The rule "no combination of defensive layers reaches
	 *  immunity" is about the combination, and armour at its own 75% cap times
	 *  resistance just under 70% already stops 92.5% of a hit with neither field
	 *  over its limit. The model checks the combination separately, against a
	 *  ceiling of what a geared player stops. See ENEMY_MITIGATION_CEILING in
	 *  sim/cataclysm_sim/enemy_stats.py. Issue #483. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Archetype")
	float ResistancePercent = 0.0f;
};

/**
 * How much of an encounter's Power Score each stat is worth, per rarity.
 * Source: sim/cataclysm_sim/enemy_stats.py, the same as the archetype table.
 *
 * RARITY SCALES MAGNITUDE AND NOTHING ELSE. Attack interval, criticals,
 * movement and resistance are on FCataclysmEnemyArchetypeRow instead, because
 * they say what kind of creature this is rather than how big it is.
 *
 * HOW TO READ ONE. An enemy's health is
 *
 *     Score * Rarity.HealthPerScore * Archetype.HealthShare
 *
 * and damage and armour follow the same shape. The multiplier is already raised
 * to the power of the rarity's step, so nothing needs an exponent at runtime.
 *
 * NOTE THE LIST IS SIX RARITIES WITH NO "Rare". It has Herald and Cataclysm
 * Boss, matching scoring.RARITY_WEIGHTS, which is authoritative. The design
 * document's older list is superseded; see issue #30.
 */
/**
 * What a kill of one enemy rarity drops. Source: Enemy Drops.
 *
 * THE ROW KEY IS THE SAME AS FCataclysmEnemyRarityRow's, so the two tables join
 * on it: "Common" through "Cataclysm_Boss". That one carries what the creature
 * IS -- how much health and damage its score buys -- and this one carries what
 * killing it gives.
 *
 * ITEMS DROP FROM ENEMIES RATHER THAN FROM FLOORS, decided by the project owner
 * on 2026-08-18. A floor's total is therefore whatever its enemies happened to
 * be, which the dungeon generator decides rather than this table.
 */
USTRUCT(BlueprintType)
struct FCataclysmEnemyDropRow : public FTableRowBase
{
	GENERATED_BODY()

	/** "Common", "Elite", "Legendary", "Herald", "Boss", "Cataclysm Boss". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Drops")
	FString EnemyRarity;

	/** How far above Common this rarity sits. 0 for Common, 5 for Cataclysm
	 *  Boss, matching the Step on FCataclysmEnemyRarityRow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Drops")
	int32 Step = 0;

	/**
	 * How many gear items this kill is EXPECTED to drop, before loot quantity.
	 *
	 * AN EXPECTED COUNT RATHER THAN A CHANCE, which matters at both ends. A
	 * chance cannot exceed one and a Cataclysm Boss drops twelve; and 0.16 for a
	 * Common enemy rounds to nothing, so the fraction is rolled as a probability
	 * instead. See UCataclysmDropRoll::RollDropCount.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Drops")
	float GearDrops = 0.0f;

	/** The same, for crafting materials, which drop on a separate roll. Twice
	 *  the gear figure: a craft consumes a material and gear is kept. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Drops")
	float MaterialDrops = 0.0f;

	/**
	 * How much magic find this kill adds to its own drops, as an added
	 * percentage. It is ADDED to the player's own rather than multiplied by it.
	 *
	 * THIS IS HOW A RARER ENEMY DROPS BETTER GEAR. Expressing it as magic find
	 * means no new mechanic: the rarity cascade already multiplies every rung by
	 * magic find. A direct mapping was considered and has no form, because there
	 * are six enemy rarities and eight gear rarities.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Drops")
	float MagicFind = 0.0f;
};

/**
 * How heavily one crafting material tier is weighted on a drop.
 * Source: Material Tiers.
 *
 * EACH TIER IS FOUR TIMES RARER THAN THE ONE BELOW: 256, 64, 16, 4, 1. So an
 * Extremely Rare material is one material drop in 341.
 *
 * NO DIFFICULTY TIER CAP, UNLIKE GEAR RARITY. The design gates gear rarity, gem
 * rarity, upgrade stones and weapon damage types on the difficulty tier and says
 * nothing about materials, so a shallow dungeon can produce an Extremely Rare
 * one. Rarely, and that is a windfall.
 */
USTRUCT(BlueprintType)
struct FCataclysmMaterialTierRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 1 to 5. Also the row key, written "T1" through "T5". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Tier")
	int32 Tier = 0;

	/** "Common", "Uncommon", "Rare", "Very Rare", "Extremely Rare", as the
	 *  Crafting sheet names them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Tier")
	FString TierName;

	/** This tier's share of every tier's weight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Tier")
	float DropWeight = 0.0f;

	/**
	 * How many crafting materials share this tier.
	 *
	 * NOT DECORATION. How often a NAMED material drops is the tier's share
	 * divided by this, and Purified Essence -- the only thing that clears the
	 * Consumption Threshold -- is one of the five in the top tier, which puts
	 * it at one material drop in 1,705. 1,023 is what the weights above were
	 * chosen against, when three shared the top tier; the ten upgrade stones
	 * added on 2026-08-23 for issue #852 are what changed it.
	 *
	 * AND IT IS NO LONGER A SINGLE FIGURE, because two of those five are the +9
	 * and +10 stones and the difficulty tier caps which stones may drop. See
	 * UCataclysmDropRoll::RollMaterial, which works it through tier by tier.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Tier")
	int32 Materials = 0;

	/**
	 * The colour this tier's name is drawn in on the dungeon floor.
	 *
	 * ITS OWN HUE FAMILY, NOT FIVE OF THE GEAR COLOURS. A material's name and a
	 * gear item's name lie on the same floor, so borrowing five of the eight
	 * gear rarity colours would put a Rare material and a Masterful sword on
	 * screen in the same blue. All five tiers sit in a cyan family the gear
	 * ramp does not use, which is how Path of Exile separates its currency from
	 * its equipment -- one tan for the whole category. Decided by the project
	 * owner on 2026-08-19; docs/DECISIONS.md carries the measurements.
	 *
	 * STATED AS sRGB IN THE WORKBOOK and converted to linear when
	 * game/Data/MaterialTiers.csv is generated, the same way the gear rarity
	 * colours and the damage-type colours are.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Tier")
	FLinearColor Colour = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct FCataclysmEnemyRarityRow : public FTableRowBase
{
	GENERATED_BODY()

	/** "Common", "Elite", "Legendary", "Herald", "Boss", "Cataclysm Boss". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Rarity")
	FString RarityName;

	/** How far above Common this rarity sits. Common is 0, Cataclysm Boss 5.
	 *  Carried so the ladder's order survives into the engine: a DataTable is
	 *  a map and its rows have no inherent order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Rarity")
	int32 Step = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Rarity")
	float HealthPerScore = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Rarity")
	float DamagePerScore = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Rarity")
	float ArmorPerScore = 0.0f;

	/**
	 * How much bigger this rarity's body is than a Common one. Issue #849.
	 *
	 * NOT A SHARE OF ANYTHING, unlike the three figures above. Those are parts
	 * of an encounter's Power Score and have to be divided by Common's to give
	 * a multiplier; this one already is one, and Common is 1.
	 *
	 * A FIFTH BIGGER EACH STEP, so a Cataclysm Boss is 2.49 times a Common.
	 * `BODY_SCALE_PER_STEP` in `sim/cataclysm_sim/enemy_stats.py` states the
	 * rule and its comment carries the arithmetic against the dungeon's
	 * narrowest corridor. It was half as big again a step until issue #885:
	 * at 20% every rung fits that corridor, and at 50% the top one did not.
	 *
	 * DEFAULTS TO 1 AND NOT 0. A row that failed to load must leave a creature
	 * its own size rather than shrinking it to nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Rarity")
	float BodyScale = 1.0f;

	/**
	 * How common this rarity is among the enemies on one dungeon floor.
	 *
	 * A SHARE OF A FLOOR'S POPULATION, NOT AN INDEPENDENT CHANCE. The five that
	 * spawn sum to exactly 1. The Dungeon Score Formula section of
	 * `docs/Cataclysm_GDD_v2.md` states them and says outright what they are:
	 * "The five weights are how common each rarity is, and they sum to 1."
	 *
	 * CATACLYSM BOSS IS 0 AND IS NOT ROLLED. The same section says it "does not
	 * appear on an ordinary floor". It is placed, one at the end of a Cataclysm
	 * dungeon, so a weight for it would be a chance of meeting a second one.
	 *
	 * THE SAME FIVE NUMBERS THE DUNGEON SCORE USES, and deliberately. A floor's
	 * score is that floor's rarity spread collapsed by how common each rarity
	 * is, so a second set of shares would let the difficulty a dungeon is priced
	 * at drift from the difficulty it actually presents. Issue #508.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Rarity")
	float SpawnWeight = 0.0f;
};

/**
 * One node of one class passive tree.
 *
 * WHERE IT COMES FROM. `docs/Berserker_Class_Tree_Final.json` and its three
 * siblings, which are exported from `C:\Projects\PassiveTreeCreator` and are the
 * design. `tools/generate_datatables.py` turns all four into this one table, and
 * `tools/tests/test_class_passive_trees.py` checks the source files against the
 * design document's stated rules.
 *
 * WHY A ROW NAME IS A TREE AND A NODE TOGETHER. A node identifier is unique
 * only within its tree. Fourteen are shared by more than one of the four --
 * `capstone_25` is in all of them -- so `Berserker_capstone_25` is the key and
 * `capstone_25` is kept separately as `NodeId` for anyone comparing against the
 * source file.
 *
 * WHAT A NODE DOES IS NOT HERE, AND THAT IS THE GAP RATHER THAN AN OMISSION.
 * `Description` is a sentence written for a player to read: "Damage taken from
 * damage over time effects is reduced by 1% per point." There is no stat name
 * and no number anywhere in the source files, so nothing can apply a spent
 * point to a character. Issue #936 has the three routes for authoring it and a
 * recommendation. Everything else about a passive point -- earning it, spending
 * it, the rules that bound where it may go, and saving it -- does work.
 */
USTRUCT(BlueprintType)
struct FCataclysmPassiveNodeRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Berserker, Bulwark, Saboteur or Masochist. The other twenty class trees
	 *  do not exist yet; issue #24. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	FString Tree;

	/** The identifier the source file gives this node, without the tree. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	FString NodeId;

	/**
	 * `basic`, `keystone` or `capstone`.
	 *
	 * THE THREE BEHAVE DIFFERENTLY AND NOT ONLY COSMETICALLY. A basic node holds
	 * several points and gives a per-point bonus. A keystone holds one point,
	 * changes a rule, and needs its parent node filled completely rather than
	 * partly. A capstone holds one point, opens at a number of points spent in
	 * the whole tree rather than by any path, and is a choice between three
	 * options rather than a bonus.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	FString Kind;

	/** What the node is called. Unique within its tree. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	FString NodeName;

	/** What it does, in words. See the note above about there being no numbers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	FString Description;

	/** The most points this node can hold. One for a keystone and a capstone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	int32 MaxPoints = 0;

	/**
	 * How many points must be spent anywhere in this tree before a capstone
	 * opens: 25, 50, 100 or 200.
	 *
	 * ZERO FOR A BASIC NODE AND A KEYSTONE, and that is not a missing value.
	 * Those open when the edges leading to them allow, which is a question about
	 * a path rather than about a total.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	int32 Threshold = 0;

	/**
	 * Where the node sits on the authoring tool's canvas.
	 *
	 * CARRIED ACROSS SO A SCREEN CAN DRAW THE TREE IN ITS AUTHORED SHAPE, which
	 * is the only reason it is here. The layout is a design decision made in the
	 * authoring tool -- which limb a node is on says as much about the tree as
	 * the node's own words -- so a screen that laid the nodes out again would be
	 * discarding that.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	float PositionX = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	float PositionY = 0.0f;

	/**
	 * The three choices a capstone offers, as three pairs.
	 *
	 * THREE PAIRS OF COLUMNS RATHER THAN A SECOND TABLE, because the design
	 * fixes the count: "Player chooses one of three options per tier." A table
	 * would be the right shape for a number that could change and the wrong one
	 * for a number that cannot.
	 *
	 * EMPTY FOR EVERY NODE THAT IS NOT A CAPSTONE, and empty for the Saboteur's
	 * four capstones as well, which offer none in the source file even though
	 * each says to choose one of three. Issue #935. A capstone with no options
	 * cannot be taken and the screen says so, rather than offering a choice
	 * between nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree|Capstone")
	FString Option1Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree|Capstone")
	FString Option1Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree|Capstone")
	FString Option2Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree|Capstone")
	FString Option2Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree|Capstone")
	FString Option3Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree|Capstone")
	FString Option3Description;
};

/**
 * One dependency between two nodes of the same class passive tree.
 *
 * AN EDGE IS A REQUIREMENT AND NOT A LINE. An edge from A to B carrying
 * `RequiredPoints: 6` means B cannot take its first point until A holds six.
 * Where the two nodes sit is on the node rows; nothing here is about drawing.
 *
 * A KEYSTONE'S EDGE ASKS FOR ITS PARENT IN FULL. The design document states it
 * as a rule of its own: keystones "require full investment in a parent node", so
 * a keystone's incoming edge requires exactly its source's MaxPoints.
 * `tools/tests/test_class_passive_trees.py` holds the source files to that.
 *
 * CAPSTONES HAVE NO EDGES AT ALL, deliberately, and the same test asserts it. A
 * capstone tier is reached by total points spent in the tree rather than along
 * any path, so wiring one into the web would be a second, contradictory rule
 * about when it opens.
 */
USTRUCT(BlueprintType)
struct FCataclysmPassiveEdgeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	FString Tree;

	/** The node that must be invested in. A row name in the node table. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	FString Source;

	/** The node that opens once it is. A row name in the node table. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	FString Target;

	/** How many points the source must hold before the target opens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Tree")
	int32 RequiredPoints = 0;
};

/**
 * What one passive node grants, as a stat modifier the pipeline understands.
 *
 * KEYED BY THE NODE'S ROW NAME in `game/Data/PassiveNodes.csv`, so a row here
 * and the node it is about share a key. The node carries the words a player
 * reads; this carries the numbers the game applies.
 *
 * A NODE WITH NO ROW GRANTS NOTHING, and that is the ordinary case rather than
 * an error. 26 of the 293 nodes have one. Most of the rest are not stat
 * modifiers at all -- they change a rule, generate a class resource, or apply
 * only in a condition the three buckets cannot express -- and issue #939
 * measures that gap exactly and lists what each group would need.
 *
 * AUTHORED IN THE DESIGN WORKBOOK, which the project owner chose on 2026-08-25
 * over adding fields to the separate tree authoring tool's schema. The `Passive
 * Effects` sheet of `docs/All_Things_Cataclysm.xlsx` is where a person edits
 * them, and `tools/generate_datatables.py` refuses to write the file when a row
 * names a node that does not exist, a stat nothing supplies, or an undeclared
 * tag.
 */
USTRUCT(BlueprintType)
struct FCataclysmPassiveEffectRow : public FTableRowBase
{
	GENERATED_BODY()

	/** A stat name as `game/Data/ClassStats.csv` and `Attributes.csv` spell it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Effect")
	FString Stat;

	/**
	 * Which of the three buckets the value lands in: `flat`, `increased` or
	 * `more`.
	 *
	 * READ OFF THE NODE'S OWN WORDING RATHER THAN CHOSEN. A percentage of a
	 * stat is an increase; a description that says "(multiplicative)" is a more
	 * multiplier, which the design permits on a passive node since issue #344.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Effect")
	FString ValueKind;

	/**
	 * How much one point in this node is worth.
	 *
	 * PER POINT, NOT IN TOTAL. A node holding ten points grants ten times this,
	 * which is what "per point" means in every description that has one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Effect")
	float ValuePerPoint = 0.0f;

	/**
	 * Tags a skill must carry for this to apply to it. Empty applies to
	 * everything.
	 *
	 * ONE ROW USES IT TODAY: the Saboteur's Bigger Traps, whose description
	 * scopes its area of effect "for traps", carries `Type.Trap`. Eight more
	 * nodes are scoped to melee attacks and cannot be expressed yet, because
	 * the tag vocabulary has no tag for melee. Issue #939.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passive Effect")
	FString RequiredTags;
};
