// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Items/CataclysmItem.h"
#include "CataclysmDropRoll.generated.h"

struct FCataclysmAffixRow;
struct FCataclysmEnemyDropRow;
struct FCataclysmGearRarityRow;
struct FCataclysmItemBaseRow;
struct FCataclysmMaterialTierRow;
class UDataTable;

/**
 * What a drop is: which rarity it rolls, how many sockets it has, and what it is
 * called. Ported from `sim/cataclysm_sim/loot.py` and
 * `sim/cataclysm_sim/naming.py`, which are where the rules were argued out.
 *
 * IN ITS OWN FILE RATHER THAN BESIDE UCataclysmItemValues, which is where the
 * rest of the item lives. Two reasons. Everything in `CataclysmItem.h` is a pure
 * function of its arguments, and every function here draws random numbers, which
 * is a different thing to test and a different thing to reason about. And these
 * read three data tables that nothing else in the game reads yet.
 *
 * RANDOMNESS COMES IN AS AN FRandomStream RATHER THAN FROM FMath::Rand, so a
 * test can seed it and get the same drop twice. `loot.py` takes a
 * `random.Random` for the same reason.
 *
 * THE TABLES COME IN AS ARGUMENTS, the way UCataclysmItemModifiers takes them,
 * so a test can build its own from the CSV rather than needing the imported
 * asset. The Load* helpers are for the game.
 *
 * WHAT THIS DOES NOT DECIDE. Which item base drops, how many items a kill or a
 * floor produces, which affixes are drawn, and what a socket is filled with.
 * Those are the later parts of issue #44, and gems are issue #46.
 */
UCLASS()
class CATACLYSM_API UCataclysmDropRoll : public UObject
{
	GENERATED_BODY()

public:
	/** The eight difficulty tiers. Mirrors affixes.DIFFICULTY_TIERS. */
	static constexpr int32 DifficultyTiers = 8;

	/**
	 * How far above the difficulty tier's own rarity a drop may roll.
	 *
	 * The same one-above the affix tier gate uses. With the cap sitting exactly
	 * on the tier, the best thing a dungeon can produce is something the player
	 * can already make, so the only reason to run one is quantity.
	 */
	static constexpr int32 RaritiesAboveDifficulty = 1;

	/** The same plus one, for which affix tiers a drop may reach. */
	static constexpr int32 AffixTiersAboveDifficulty = 1;

	/** How many rarities there are. Eight, one per ECataclysmRarity entry. */
	static constexpr int32 RarityCount = 8;

	// -----------------------------------------------------------------------
	// The tables
	// -----------------------------------------------------------------------

	static const TCHAR* GearRarityTableAssetPath;
	static const TCHAR* ItemSocketTableAssetPath;
	static const TCHAR* AffixTierTableAssetPath;

	/** The gear rarity table, or null with the reason logged. */
	static const UDataTable* LoadGearRarityTable();

	/** The socket maximum table, or null with the reason logged. */
	static const UDataTable* LoadItemSocketTable();

	/** The affix tier weight table, or null with the reason logged. */
	static const UDataTable* LoadAffixTierTable();

	/**
	 * The GearRarity row name for a rarity: the ECataclysmRarity entry's own
	 * name, which is what the generator writes as the key.
	 *
	 * That join is the reason the table carries no ladder column of its own; see
	 * FCataclysmGearRarityRow. A Python test compares the two lists.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Drop")
	static FName RowNameFor(ECataclysmRarity Rarity);

	/** One rarity's row, or null when the table is missing it. */
	static const FCataclysmGearRarityRow* RarityRow(const UDataTable* GearRarityTable,
													ECataclysmRarity Rarity);

	// -----------------------------------------------------------------------
	// Which rarity a drop rolls
	// -----------------------------------------------------------------------

	/**
	 * The highest rarity a drop may roll at a difficulty tier.
	 *
	 * Gear rarity equals the difficulty tier, plus the one-above that makes a
	 * drop worth reading, capped at Cataclysmic. So tiers 7 and 8 both reach
	 * Cataclysmic, the same way affix tiers 6, 7 and 8 all reach T7.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Drop")
	static ECataclysmRarity BestRarityOnADrop(int32 DifficultyTier);

	/** The upgrade level a piece must reach before it can be this rarity. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Drop")
	static int32 GearLevelGateFor(const UDataTable* GearRarityTable,
								  ECataclysmRarity Rarity);

	/** The lowest and highest residue a drop of this rarity can carry. */
	static bool ResidueBandFor(const UDataTable* GearRarityTable,
							   ECataclysmRarity Rarity,
							   float& OutLowest, float& OutHighest);

	/**
	 * The residue one drop of this rarity arrives with.
	 *
	 * Uniform inside the band and a whole number of points, because residue is
	 * counted in points everywhere else: the craft day penalty is `CR / 100`
	 * rounded down and the gold multiplier is `(CR / 50) + 1`.
	 */
	static float RollResidue(const UDataTable* GearRarityTable,
							 ECataclysmRarity Rarity, FRandomStream& Stream);

	/**
	 * The chance the cascade stops at this rung, given that it reached it.
	 *
	 * THE RUNG'S WEIGHT AS A SHARE OF EVERYTHING AT OR BELOW IT. That is what
	 * turns a table of weights into a cascade: stopping at rung R with chance
	 * w[R]/S[R], where S[R] is the weight of rungs 1 to R, leaves S[R-1]/S[R] to
	 * carry on. Multiplied down from the top, every rung ends with w[R]/S[N] --
	 * its own share of the whole reachable ladder, which is what a weight means.
	 *
	 * MAGIC FIND MULTIPLIES IT, which is Path of Exile's stated behaviour: +100%
	 * increased item rarity gives twice as many of every rarity above the floor.
	 * Saturating at 1 is the only ceiling.
	 *
	 * @param MagicFind  an added percentage with a baseline of 0
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Drop")
	static float RarityStepChance(const UDataTable* GearRarityTable,
								  ECataclysmRarity Rarity, float MagicFind);

	/**
	 * What fraction of drops at this tier is each rarity, indexed by
	 * ECataclysmRarity. Always RarityCount long; rarities out of reach are 0.
	 *
	 * EXACT RATHER THAN SAMPLED, so a test can check the shape without rolling
	 * ten thousand drops and then arguing about noise. RollRarity draws from
	 * exactly this distribution, and a test compares the two.
	 */
	static void RarityDistribution(const UDataTable* GearRarityTable,
								   int32 DifficultyTier, float MagicFind,
								   TArray<float>& OutShares);

	/**
	 * The rarity one drop rolls.
	 *
	 * Walks the rungs from the rarest down and stops at the first that succeeds,
	 * which is the cascade itself rather than a lookup into the distribution
	 * above. Written that way on purpose: the test that the two agree is what
	 * proves the exact distribution describes what actually happens.
	 */
	static ECataclysmRarity RollRarity(const UDataTable* GearRarityTable,
									   int32 DifficultyTier, float MagicFind,
									   FRandomStream& Stream);

	// -----------------------------------------------------------------------
	// How many sockets it has
	// -----------------------------------------------------------------------

	/**
	 * The most sockets this item base can have.
	 *
	 * From the base rather than from the slot alone, because a weapon's maximum
	 * depends on how many hands it takes.
	 *
	 * @return -1 when the slot has no stated maximum, which is a data fault
	 *         rather than a base that holds none
	 */
	static int32 MaxSocketsFor(const UDataTable* SocketTable,
							   const FCataclysmItemBaseRow& Base);

	/**
	 * How many sockets one drop of this base arrives with.
	 *
	 * UNIFORM FROM NONE UP TO THE BASE'S MAXIMUM, chosen by the project owner on
	 * 2026-08-18. Capping the roll by the difficulty tier the way Diablo 2 and
	 * Path of Exile cap it by item level was put to them and declined, as was
	 * weighting the roll toward fewer sockets. So a socket count carries no
	 * progression: a tier 1 Chest can drop with all six.
	 *
	 * A DROP WITH NO SOCKETS IS NOT A RUINED ITEM. The Add Socket craft --
	 * Shattered Core, 15 residue, 3 days -- only has something to do because
	 * drops arrive below their maximum.
	 *
	 * @return 0 when the base has no stated maximum, so a data fault produces a
	 *         plain item rather than a crash
	 */
	static int32 RollSockets(const UDataTable* SocketTable,
							 const FCataclysmItemBaseRow& Base,
							 FRandomStream& Stream);

	// -----------------------------------------------------------------------
	// What tier its affixes roll at
	// -----------------------------------------------------------------------

	/**
	 * The highest affix tier a drop may reach at a difficulty tier.
	 *
	 * Every tier at or below it stays in the pool, so a deep drop is better on
	 * average without being predictable. Path of Exile gates modifier tiers on
	 * item level the same way, and item level expands which tiers are available
	 * rather than removing the low ones.
	 *
	 * IT DOES NOT CAP CRAFTING, which has no tier gate at all. That is why a
	 * rare high tier is a windfall rather than the route.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Drop")
	static int32 MaxAffixTierOnADrop(int32 DifficultyTier);


	// -----------------------------------------------------------------------
	// What a kill drops
	// -----------------------------------------------------------------------

	/** A character with no bonuses. Loot quantity is a percentage of what would
	 *  otherwise drop, so 100 leaves it unchanged. */
	static constexpr float BaselineLootQuantity = 100.0f;

	static const TCHAR* EnemyDropTableAssetPath;
	static const TCHAR* MaterialTierTableAssetPath;

	/** What each enemy rarity drops, or null with the reason logged. */
	static const UDataTable* LoadEnemyDropTable();

	/** The crafting material tier weights, or null with the reason logged. */
	static const UDataTable* LoadMaterialTierTable();

	/**
	 * One enemy rarity's drop row, or null.
	 *
	 * @param EnemyRarity  the row key, which is the same key
	 *        `FCataclysmEnemyRarityRow` uses: "Common" through "Cataclysm_Boss".
	 *        The two tables join on it.
	 */
	static const FCataclysmEnemyDropRow* EnemyDropRow(
		const UDataTable* EnemyDropTable, FName EnemyRarity);

	/**
	 * How many gear items a kill of this rarity is expected to drop.
	 *
	 * @param LootQuantity  a percentage with a baseline of 100, so 400
	 *        quadruples it. Every source of it in the design is an increase.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Drop")
	static float ExpectedGearDrops(const UDataTable* EnemyDropTable,
								   FName EnemyRarity, float LootQuantity);

	/** The same, for crafting materials, which drop on a separate roll. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Drop")
	static float ExpectedMaterialDrops(const UDataTable* EnemyDropTable,
									   FName EnemyRarity, float LootQuantity);

	/**
	 * The magic find a kill of this rarity adds to its own drops.
	 *
	 * ADDED TO THE PLAYER'S OWN rather than multiplied by it, which is what
	 * Path of Exile does with its own sources.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Drop")
	static float MagicFindFrom(const UDataTable* EnemyDropTable,
							   FName EnemyRarity);

	/**
	 * Turn an expected number of drops into how many actually fall.
	 *
	 * THE WHOLE PART IS CERTAIN AND THE FRACTION IS A PROBABILITY, so an
	 * expected 3.7 gives three and a 70% chance of a fourth. Rounding 0.16 to
	 * the nearest whole number instead would make a Common enemy drop nothing,
	 * ever.
	 */
	static int32 RollDropCount(float Expected, FRandomStream& Stream);

	/**
	 * The tier one crafting material drops at, 1 to 5, or 0 when the table is
	 * missing.
	 *
	 * MAGIC FIND RAISES IT, WHICH DEPARTS FROM THE GENRE. Path of Exile's item
	 * rarity does not affect currency at all. It applies here because the enemy
	 * rarity contribution exists so a harder enemy is more rewarding, and
	 * materials are half of what a kill gives.
	 *
	 * A CONSEQUENCE WORTH KNOWING: at 500% magic find the second rung saturates
	 * to certainty and nothing falls through to the commonest tier, so a
	 * Cataclysm Boss drops no Common materials at all. The ordinary supply comes
	 * from ordinary enemies, which add none.
	 *
	 * WHICH MATERIAL WITHIN THE TIER IS AN EQUAL CHANCE, and this does not make
	 * it: the names are in the CraftingMaterials table.
	 */
	static int32 RollMaterialTier(const UDataTable* MaterialTierTable,
								  float MagicFind, FRandomStream& Stream);

	/** What fraction of material drops is each tier, indexed by tier minus one.
	 *  Empty when the table is missing. */
	static void MaterialTierDistribution(const UDataTable* MaterialTierTable,
										 float MagicFind,
										 TArray<float>& OutShares);

	/**
	 * The tier one affix rolls at on a drop at this difficulty tier.
	 *
	 * Each tier is half as likely as the one below, from the affix tier table.
	 *
	 * @return 1 when the table is missing, which is the floor rather than a
	 *         failure: an affix with no tier is not an affix
	 */
	static int32 RollAffixTier(const UDataTable* AffixTierTable,
							   int32 DifficultyTier, FRandomStream& Stream);
};

/**
 * What a dropped item is called.
 *
 * `<rarity> <base name> of <word>`, with the word from the item's own strongest
 * suffix affix. Ported from `sim/cataclysm_sim/naming.py`.
 *
 * THE FORMAT WAS SET BY THE PROJECT OWNER on 2026-08-18, issue #695: "Quality
 * Item Type of Interesting word that fits the item. For example, Everyday Short
 * Sword of Malice or Mythic Robes of The Night."
 *
 * THE WORD COMES FROM THE ITEM'S OWN AFFIXES, not from a flavour list. That is
 * what Diablo 2 and Path of Exile do for magic items, and it means the name
 * tells a player something true rather than being decoration.
 *
 * A NAME IS AN FString AND NOT AN FText, deliberately and temporarily. It is
 * assembled from a rarity, a base name and an affix word, all three of which
 * come out of the design workbook in English and none of which is a localisable
 * asset yet. Localisation is issue #60, and it has to reach the tables before it
 * can reach this.
 */
UCLASS()
class CATACLYSM_API UCataclysmItemName : public UObject
{
	GENERATED_BODY()

public:
	/** How a rarity is spelt in a name. The ECataclysmRarity entry's name. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static FString RarityWord(ECataclysmRarity Rarity);

	/**
	 * The word one affix gives an item's name, or empty when it gives none.
	 *
	 * Only a suffix has one. The first word of a name is the rarity, so a prefix
	 * has nowhere to appear, and the generator refuses to write a word on one.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static FString WordFor(const UDataTable* AffixTable, FName Affix);

	/**
	 * The affix on this item whose word the name uses.
	 *
	 * STRONGEST MEANS HIGHEST TIER, THEN HIGHEST ROLL, THEN FIRST ON THE ITEM.
	 * All three are needed and the third matters most: without a final tie-break
	 * the same item could be called two different things on two runs, because
	 * affixes are stored in the order they were drawn and two of them can share
	 * a tier and a roll.
	 *
	 * TIER BEFORE ROLL, because a tier is worth a seventh of the affix's top
	 * value and a roll is worth at most a quarter of one tier. A perfect T6 can
	 * beat a poor T7 in value, but the name follows the tier, which is the
	 * number a player reads off the item.
	 *
	 * @return false when the item carries no suffix affix at all
	 */
	static bool StrongestSuffix(const FCataclysmItem& Item,
								const UDataTable* AffixTable,
								FCataclysmRolledAffix& OutAffix);

	/**
	 * What a rolled item is called.
	 *
	 * WHEN AN ITEM HAS NO SUFFIX AFFIX THE NAME STOPS AFTER THE BASE. An
	 * Everyday piece carries one affix and it may be a prefix, so `Everyday
	 * Short Sword` is a whole name. Also the project owner's choice, and it is
	 * what Diablo 2 does. The missing words are themselves a signal that the
	 * item is thin.
	 *
	 * @return an empty string when the base is not in the table, since an item
	 *         whose base cannot be found has no name to give
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Item")
	static FString NameOf(const FCataclysmItem& Item,
						  const UDataTable* BaseTable,
						  const UDataTable* AffixTable);
};
