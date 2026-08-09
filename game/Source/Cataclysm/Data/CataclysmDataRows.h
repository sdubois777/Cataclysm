// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
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
	 * Only Burn carries a value today. It was added because every one of the
	 * sixteen designed Demonic skills applies burn and the design stated neither
	 * how long it lasts nor what it deals, so "sets each one alight" applied an
	 * effect made of nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	float DurationSeconds = 0.0f;

	/**
	 * What the whole effect is worth, as a percent of the hit that applied it.
	 *
	 * Spread evenly across DurationSeconds. Zero means the design has not stated
	 * one, and an effect worth zero applies nothing rather than applying
	 * silently for no damage.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status Effect")
	float PercentOfHit = 0.0f;
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

	/** Percent of all incoming damage resisted, whatever its type. Capped
	 *  below 70 by the model, because no combination of defensive layers may
	 *  reach immunity. */
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
};
