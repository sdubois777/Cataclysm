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
 * One affix that could roll on a piece, with its damage types already chosen.
 *
 * WHY THE DAMAGE TYPES ARE PICKED BEFORE THE DRAW RATHER THAN AFTER. A
 * resistance family says how MANY damage types it covers and the item says
 * which. Two families that both landed on Fire occupy the same stat group and
 * may not sit on one item, and the draw is what enforces that -- so it has to
 * know which types each candidate landed on before it chooses between them.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmAffixCandidate
{
	GENERATED_BODY()

	/** Row name in the Affixes table. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Drop")
	FName Affix;

	/** Which damage types a resistance family landed on. Empty for every other
	 *  kind, and also empty for a family covering all eight, because there is
	 *  no choice to make -- the same convention FCataclysmRolledAffix uses. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Drop")
	TArray<FName> DamageTypes;
};


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

	/**
	 * How far above the difficulty tier the best upgrade stone that can drop is.
	 *
	 * TWO, NOT ONE, AND THE DOCUMENT SAYS SO OUTRIGHT: "Gear level is tier + 2
	 * capped at +10, which clears every rarity gate in section VI and reaches
	 * exactly +10 at tier 8." Its difficulty tier table restates it a step at a
	 * time, tier 1 giving "+3 upgrade level" through to tier 8 giving "+10".
	 *
	 * WHY TWO WHERE RARITY AND AFFIXES GET ONE. Those two gate a single roll,
	 * and this gates a ladder: upgrading consumes two stones, and two stones of
	 * a level combine into one of the next, so the stones a player finds are
	 * spent well below the level they are aiming at. Two also makes the ladder
	 * end where the game does -- tier 8 is the deepest tier, so a one-above cap
	 * would mean the +10 stone never drops anywhere.
	 */
	static constexpr int32 UpgradeLevelsAboveDifficulty = 2;

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

	/** The affix table, or null with the reason logged. Every affix a drop can
	 *  roll, across the four kinds. */
	static const UDataTable* LoadAffixTable();

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
	// Rolling a whole item
	// -----------------------------------------------------------------------

	/**
	 * Every stat group one affix would occupy on an item.
	 *
	 * TWO AFFIXES MAY NOT SHARE A GROUP, which is what stops one piece carrying
	 * four different ways to grant the same stat. The rule, matching
	 * `affixes.groups_of`:
	 *
	 *   Stat        one group, "<stat>.<flat or increased>"
	 *   Ailment     one group, "ailment.<effect>"
	 *   Hybrid      one per part, each read as the stat affix it names
	 *   Resistance  one per damage type it covers, "resistance_<type>.flat"
	 *
	 * A HYBRID'S PARTS ARE LOOKED UP BY NAME, because the Affixes table stores
	 * them as the affix names they are rather than as row keys.
	 */
	static void GroupsOf(const UDataTable* AffixTable,
						 const FCataclysmAffixRow& Affix,
						 const TArray<FName>& DamageTypes,
						 TSet<FString>& OutGroups);

	/**
	 * Every affix that could roll on one slot in one position, each with its
	 * resistance damage types already drawn.
	 *
	 * @param Position  "prefix" or "suffix". They are separate pools, and a stat
	 *                  appearing as one never appears as the other.
	 */
	static void CandidatesFor(const UDataTable* AffixTable, const FString& Slot,
							  const FString& Position, FRandomStream& Stream,
							  TArray<FCataclysmAffixCandidate>& OutCandidates);

	/**
	 * Draw `Count` affixes at random, never two from one group.
	 *
	 * @return false when the candidates cannot supply that many distinct
	 *         groups, which is a fault in the pool rather than an unlucky roll
	 */
	static bool DrawWithoutRepeatingAGroup(
		const UDataTable* AffixTable,
		const TArray<FCataclysmAffixCandidate>& Candidates, int32 Count,
		FRandomStream& Stream, TArray<FCataclysmAffixCandidate>& OutDrawn);

	/**
	 * How one drop's affix slots divide into prefixes and suffixes.
	 *
	 * AN ODD COUNT PICKS A SIDE AT RANDOM. `UCataclysmItemValues::
	 * PrefixSuffixSplit` returns the even shape, and a drop is where the other
	 * way has to actually happen: without this every three-affix item in the
	 * game would carry two prefixes and one suffix, a bias nobody chose.
	 */
	static void SplitForADrop(int32 Slots, FRandomStream& Stream,
							  int32& OutPrefixes, int32& OutSuffixes);

	/** Which gear slot a drop is for. Every slot the item bases occupy is
	 *  equally likely; see the Python model's `roll_slot` for why that is not
	 *  the same as every WORN position being equally likely. */
	static FString RollSlot(const UDataTable* BaseTable, FRandomStream& Stream);

	/** A random base within one slot, all equally likely. Returns NAME_None
	 *  when the slot has no bases. */
	static FName RollBase(const UDataTable* BaseTable, const FString& Slot,
						  FRandomStream& Stream);

	/**
	 * Roll one whole item for a gear slot at a difficulty tier.
	 *
	 * THE ORDER IS RARITY, THEN CONTENTS. The rarity says how many enchantments
	 * and how many regular affixes fill the four slots; the affixes are then
	 * drawn to that count. Rolling the two counts independently would produce
	 * something that is not a rarity most of the time.
	 *
	 * THE UPGRADE LEVEL IS THE FLOOR ITS RARITY FORCES and nothing more. A
	 * Legendary drops at +4 because it could not be a Legendary below that.
	 *
	 * @return false when a table is missing or the pool cannot fill the item
	 */
	static bool RollItem(const UDataTable* BaseTable,
						 const UDataTable* AffixTable,
						 const UDataTable* GearRarityTable,
						 const UDataTable* SocketTable,
						 const UDataTable* AffixTierTable,
						 const FString& Slot, int32 DifficultyTier,
						 float MagicFind, FRandomStream& Stream,
						 FCataclysmItem& OutItem);

	// -----------------------------------------------------------------------
	// What a kill drops
	// -----------------------------------------------------------------------

	/** A character with no bonuses. Loot quantity is a percentage of what would
	 *  otherwise drop, so 100 leaves it unchanged. */
	static constexpr float BaselineLootQuantity = 100.0f;

	static const TCHAR* EnemyDropTableAssetPath;
	static const TCHAR* MaterialTierTableAssetPath;
	static const TCHAR* CraftingMaterialTableAssetPath;

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
	 * A POISSON DRAW, SO THE COUNT VARIES FOR EVERY ENEMY. Decided by the
	 * project owner on 2026-08-19: "item count should vary for every enemy".
	 * Issue #725. `roll_count` in `sim/cataclysm_sim/loot.py` is the model this
	 * mirrors and carries the full reasoning.
	 *
	 * WHAT IT REPLACED. The count used to be the whole part of the expected
	 * value plus one more with probability equal to the fraction, so all the
	 * randomness lived in the fraction and a rate that was a whole number had
	 * none: a Boss at 5.0 dropped exactly five items every kill and a Cataclysm
	 * Boss exactly twelve. Four of the six enemy rarities were fixed.
	 *
	 * THE MEAN IS UNCHANGED, which is why no number in `game/Data/EnemyDrops.csv`
	 * moved. A Poisson draw averages the value it is given.
	 *
	 * IT CAN ANSWER ZERO FOR ANY RATE, including a Boss's, about 0.7% of the
	 * time. No floor is applied here; whether a boss should be guaranteed one
	 * item is a separate question and issue #725 records it as open.
	 *
	 * KNUTH'S METHOD, which needs only a uniform random number and so uses the
	 * same stream as everything else. It draws about `Expected + 1` numbers, so
	 * the most expensive call in the project -- a Cataclysm Boss's 24 expected
	 * materials -- draws about 25. It terminates because `FRandomStream::FRand`
	 * answers in [0, 1), so the running product strictly decreases and reaches
	 * the limit in finite steps. The product and the limit are doubles rather
	 * than floats so that a large expected value, which a big loot quantity
	 * bonus could produce, does not underflow the limit to zero and turn a
	 * bounded loop into a long one.
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

	/** The crafting material table, loaded once. Null when it cannot be read. */
	static const UDataTable* LoadCraftingMaterialTable();

	/**
	 * Which crafting material drops, given the material tier it rolled and the
	 * difficulty tier being played.
	 *
	 * @param MaterialTier   the rarity band, 1 to 5, from RollMaterialTier
	 * @param DifficultyTier which of the eight tiers is being played, which caps
	 *                       the upgrade stones this may produce
	 * @return the row key in `game/Data/CraftingMaterials.csv`, or NAME_None
	 *         when the band holds nothing this tier may have.
	 *
	 * THE TWO TIERS ARE DIFFERENT THINGS and the parameters are in the order the
	 * roll happens: the material tier comes out of RollMaterialTier, and the
	 * difficulty tier comes from the world. Passing them the wrong way round
	 * compiles, so `AMaterialTierIsNotADifficultyTier` pins the difference.
	 *
	 * EQUAL CHANCE AMONG WHAT THE BAND ALLOWS. `roll_material_tier` in
	 * `sim/cataclysm_sim/loot.py` picks the band and says the choice of material
	 * within it belongs to "whoever holds that table", which is this.
	 *
	 * SO THE UPGRADE STONE CAP CHANGES THE ODDS OF EVERYTHING ELSE, because
	 * excluding a stone leaves fewer materials to share the band. Five materials
	 * share tier 5, of which two are the +9 and +10 stones, so Purified Essence
	 * -- the only thing that clears the Consumption Threshold -- is one material
	 * drop in 1,023 at tiers 1 to 6 where neither stone may drop, one in 1,364
	 * at tier 7 where +9 may, and one in 1,705 at tier 8 where both may. 1,023
	 * is the figure the tier weights were originally chosen against; the sim
	 * models the uncapped 1,705, and `MATERIALS_IN_TIER` in loot.py says why it
	 * rose. THE TOOL THE DESIGN LEANS ON GOT COMMONER WHERE IT IS NEEDED MOST,
	 * which is the shallow tiers, and that is a consequence of the cap rather
	 * than a thing the cap was chosen for.
	 *
	 * EVERY BAND KEEPS AT LEAST THREE NON-STONE MATERIALS, so no cap can empty
	 * one. `validate_upgrade_stone_levels` in `tools/generate_datatables.py`
	 * guards the stone ladder itself.
	 *
	 * THE CRAFTING SHEET HOLDS ACTIONS AS WELL AS MATERIALS, and they carry a
	 * tier of 0 so that nothing can drop "Reroll Affix Value".
	 */
	static FName RollMaterial(const UDataTable* CraftingMaterialTable,
							  int32 MaterialTier, int32 DifficultyTier,
							  FRandomStream& Stream);

	/**
	 * The highest upgrade stone a drop may produce at a difficulty tier.
	 *
	 * Tier + 2, capped at +10, which is the design document's rule word for
	 * word. Tier 1 gives +3 and tier 8 gives +10.
	 *
	 * IT DOES NOT CAP CRAFTING, exactly as the affix gate does not. Two stones
	 * of a level combine into one of the next, so a player at a low tier can
	 * still build a stone above what drops there -- it just costs the climb.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Drop")
	static int32 MaxUpgradeStoneOnADrop(int32 DifficultyTier);

	/**
	 * What a material of this tier is called, as a player reads it.
	 *
	 * The material's own name from the table, so "Purified Essence" rather than
	 * "Tier 5 material". Empty when the row is missing.
	 */
	static FString MaterialNameOf(const UDataTable* CraftingMaterialTable,
								  FName Material);

	/**
	 * Which tier a named crafting material is, 1 to 5.
	 *
	 * THE OPPOSITE DIRECTION FROM THE ROLL, which is why it did not exist until
	 * something carried a material rather than dropped one. RollMaterialTier
	 * picks the tier and RollMaterial then picks a material inside it, so a drop
	 * never has to ask; a carried slot stores only the material's name, so the
	 * inventory screen does. Issue #731.
	 *
	 * @return 0 for a material the table does not hold, and 0 for the nineteen
	 *         crafting ACTIONS on that sheet, which carry a tier of 0 so that
	 *         nothing can drop "Reroll Affix Value". Zero means "no tier" rather
	 *         than a failure.
	 */
	static int32 MaterialTierOf(const UDataTable* CraftingMaterialTable,
								FName Material);

	/** The colour a material of this tier has its name drawn in, or white when
	 *  the table is missing it. */
	static FLinearColor MaterialColourFor(const UDataTable* MaterialTierTable,
										  int32 Tier);

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
