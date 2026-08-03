// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "CataclysmItem.generated.h"

struct FCataclysmAffixRow;
struct FCataclysmItemBaseRow;
class UDataTable;

/**
 * The eight item rarities, weakest first.
 *
 * RARITY IS NOT STORED ON AN ITEM. It is a label for what fills the item's four
 * slots, so it is computed rather than set: an item carrying an enchantment is a
 * Legendary, and one carrying three regular affixes is a Superb. See
 * UCataclysmItemValues::RarityOf.
 */
UENUM(BlueprintType)
enum class ECataclysmRarity : uint8
{
	Everyday	UMETA(DisplayName = "Everyday"),
	Quality		UMETA(DisplayName = "Quality"),
	Superb		UMETA(DisplayName = "Superb"),
	Masterful	UMETA(DisplayName = "Masterful"),
	Legendary	UMETA(DisplayName = "Legendary"),
	Mythical	UMETA(DisplayName = "Mythical"),
	Ascendant	UMETA(DisplayName = "Ascendant"),
	Cataclysmic	UMETA(DisplayName = "Cataclysmic"),
};

/**
 * One affix as it sits on a particular item.
 *
 * The affix itself -- what stat it grants, whether it is flat or increased, its
 * top value and which slots it may roll on -- lives in game/Data/Affixes.csv.
 * What belongs to the item is only which affix it got, at what tier, and where
 * in that tier's band it landed.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmRolledAffix
{
	GENERATED_BODY()

	/** Row name in the Affixes table. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cataclysm|Item")
	FName Affix;

	/** 1 to 7. Tier N is worth N sevenths of the affix's top value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cataclysm|Item")
	int32 Tier = 1;

	/**
	 * Where in the tier's band the roll landed. 0 is the floor, 1 is the top.
	 *
	 * Stored rather than the resolved number, because the value also depends on
	 * the piece's upgrade level and on whether the piece is a two-handed weapon,
	 * both of which change after the item exists.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cataclysm|Item")
	float Roll = 1.0f;
};

/**
 * One item: which base it is, how far it has been upgraded, and what it rolled.
 *
 * WHAT IS NOT HERE. Rarity, because it is computed from the contents. The affix
 * values, because they depend on the upgrade level and the base. Sockets and
 * gems, which are issue #46, and enchantments, which are issue #45 -- only the
 * COUNT of enchantments is here, because rarity cannot be computed without it.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmItem
{
	GENERATED_BODY()

	/** Row name in the ItemBases table. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cataclysm|Item")
	FName Base;

	/** The +0 to +10 upgrade level. It multiplies every affix and implicit on
	 *  the piece, by about 3.52 at +10. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cataclysm|Item")
	int32 GearLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cataclysm|Item")
	TArray<FCataclysmRolledAffix> Affixes;

	/**
	 * How many of the four slots hold an enchantment rather than an affix.
	 *
	 * The enchantments themselves are not modelled yet. The count is, because
	 * an item carrying one is a Legendary and rarity cannot be read without it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cataclysm|Item")
	int32 EnchantmentCount = 0;
};

/**
 * What an item is worth, ported from `sim/cataclysm_sim/affixes.py`.
 *
 * Static functions over plain numbers, like UCataclysmDamageCalculation and
 * UCataclysmStatPipeline, so every curve can be tested by passing values in.
 *
 * THREE THINGS MULTIPLY AN AFFIX, and they are independent of each other:
 *
 *   TIER      seven of them, on a linear curve. Tier N is worth N sevenths of
 *             the affix's top value. Linear rather than front-loaded, because a
 *             front-loaded curve hands over most of an affix's value in the
 *             first few tiers and makes the later ones easy to skip.
 *
 *   ROLL      every tier is a range reaching 25% below its top, so that the
 *             crafting materials which reroll and perfect a value have
 *             something to do. Bands overlap by exactly one tier: a perfect T6
 *             roll can beat a poor T7 one, and can never beat a T5.
 *
 *   UPGRADE   the piece's +0 to +10 level multiplies everything on it, by about
 *             3.52 at +10. Every affix value stated in the design document is
 *             the +10 figure, so a +0 piece gives a fraction of it.
 *
 * A two-handed weapon then doubles the result, which is what balances its four
 * affix slots against the eight a dual wielder holds.
 */
UCLASS()
class CATACLYSM_API UCataclysmItemValues : public UObject
{
	GENERATED_BODY()

public:
	/** Affixes have seven tiers, because the crafting material that raises them
	 *  levels an affix to the seventh. */
	static constexpr int32 MaxAffixTier = 7;

	/** A piece upgrades from +0 to +10. */
	static constexpr int32 MaxGearLevel = 10;

	/** Four slots, holding regular affixes and enchantments between them. */
	static constexpr int32 SlotsPerPiece = 4;

	/** At most two of the four may be prefixes, and at most two suffixes. */
	static constexpr int32 PrefixesPerPiece = 2;
	static constexpr int32 SuffixesPerPiece = 2;

	/**
	 * How far below its top a tier's band reaches. 0.25 is 25%.
	 *
	 * Without a band the crafting materials that reroll and perfect a value are
	 * dead content, because a tier would have exactly one possible value.
	 */
	static constexpr float RollBandFraction = 0.25f;

	/**
	 * Each upgrade level adds this share. The same constant the Power Score
	 * model uses, rather than a second copy of it, so gear level cannot mean two
	 * different things in two places.
	 */
	static constexpr float GearLevelFactor = 0.25245807054891267f;

	/**
	 * What a two-handed weapon multiplies its own implicits and affixes by.
	 *
	 * DERIVED, NOT CHOSEN. Two one-handed weapons hold eight affix slots against
	 * a two-hander's four, so 2 is the figure that makes the two loadouts worth
	 * the same in affixes -- which the design requires, because the Power Score
	 * model counts two one-handed weapons as one equipped piece so that dual
	 * wielding is not worth free power.
	 *
	 * It reaches the implicits as well as the affixes. Without that the
	 * two-hander loses outright: two one-handed bases SUM their damage, so an
	 * Axe and a Sword give 86 against a Greatsword's stated 78.
	 */
	static constexpr float TwoHandedMultiplier = 2.0f;

	/** What a piece at this upgrade level does to everything it carries. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static float GearLevelMultiplier(int32 GearLevel);

	/** Tier N is worth N sevenths of an affix's top value. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static float TierFraction(int32 Tier);

	/** The lowest and highest an affix can roll at a tier, at +10. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static void TierBand(float TopValue, int32 Tier, float& OutLow, float& OutHigh);

	/**
	 * One affix's value: its tier band, where in it the roll landed, the
	 * piece's upgrade level, and whether the piece is a two-handed weapon.
	 *
	 * The stated top values are the +10 figures, so this divides by the +10
	 * multiplier before applying the piece's own.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static float AffixValue(float TopValue, int32 Tier, float Roll,
							int32 GearLevel, bool bTwoHanded = false);

	/** An implicit's value. It does not roll, so it has no tier and no band. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static float ImplicitValue(float StatedValue, int32 GearLevel,
							   bool bTwoHanded = false);

	/**
	 * What an item carrying this many enchantments and affixes IS.
	 *
	 * The definition of rarity, not a lookup on a stored field. Returns false
	 * for a combination no item can have -- an enchantment with slots left
	 * empty, or more than four filled.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static bool RarityOf(int32 EnchantmentCount, int32 AffixCount,
						 ECataclysmRarity& OutRarity);

	/** How many regular affixes a piece of this rarity carries. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static int32 AffixSlotsFor(ECataclysmRarity Rarity);

	/** How many of the four slots hold an enchantment. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static int32 EnchantmentsFor(ECataclysmRarity Rarity);

	/** A slot count divided into prefixes and suffixes, at its most even. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static void PrefixSuffixSplit(int32 Slots, int32& OutPrefixes,
								  int32& OutSuffixes);
};

/**
 * Turning an item into the modifiers a character's stats are computed from.
 *
 * Separated from the value curves above because this one needs the data tables
 * and those need nothing. Every function here takes the tables as arguments
 * rather than finding them, so a test can build its own.
 */
UCLASS()
class CATACLYSM_API UCataclysmItemModifiers : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Every modifier one item contributes: its base's implicits and its rolled
	 * affixes, in the form UCataclysmStatPipeline evaluates.
	 *
	 * AN ITEM NEVER PRODUCES A "MORE" MULTIPLIER. Affixes and implicits are
	 * flat or increased; the multiplicative bucket belongs to gems, passive
	 * keystones and enchantments. The pipeline enforces that too, but producing
	 * one here would be caught only as a warning at evaluation time.
	 */
	static TArray<FCataclysmStatModifier> ModifiersFor(
		const FCataclysmItem& Item,
		const UDataTable* BaseTable,
		const UDataTable* AffixTable);

	/** The rarity an item is, from what fills its slots. */
	static bool RarityOfItem(const FCataclysmItem& Item,
							 ECataclysmRarity& OutRarity);

	/** Whether the base is a two-handed weapon, which doubles its values. */
	static bool IsTwoHanded(const FCataclysmItem& Item,
							const UDataTable* BaseTable);
};
