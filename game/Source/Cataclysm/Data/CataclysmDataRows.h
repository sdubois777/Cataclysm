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

	/** Architect, Explorer, Treasurer or Artisan. Some carry a trailing asterisk
	 *  whose meaning is not recorded anywhere. One row has no branch at all:
	 *  a one-time empire-wide purchase rather than a city upgrade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Upgrade")
	FString Branch;

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
