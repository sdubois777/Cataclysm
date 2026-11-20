// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmPassiveTree.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmEmpireTree.generated.h"

struct FCataclysmDungeon;
struct FCataclysmEmpireTreeNodeRow;
class UCataclysmEmpireRun;
class UDataTable;

/**
 * What the empire upgrade tree's bought player nodes add up to, before the dungeon being walked is known. Issue #2741
 * slice 5a; `UCataclysmEmpireTree::PlayerBonusesFor` fills it from the allocation.
 *
 * EVERY SHARE IS A FRACTION, as the effect table holds it: 0.05 is 5%. Magic find becomes that many percentage points of
 * FLAT magic find, because its base is nought and an increase on it would scale only flat magic find; loot quantity,
 * movement speed and damage become increases. `UCataclysmEmpireTree::PlayerStatModifiersFor` does both.
 *
 * THE EXPLORER BRANCH'S FLAT FIGURES ARE HELD APART FROM EVERY OTHER BRANCH'S, because One-Day Specialist doubles "all
 * Explorer Loot modifiers" and nothing else: The Hoarder, a centre capstone's option, is the other. Every figure that
 * grows with the dungeon or the run belongs to an Explorer node, which `tools/tests/test_empire_tree_player_nodes.py`
 * holds, so those are doubled whole.
 */
struct CATACLYSM_API FCataclysmEmpireTreePlayerBonuses
{
	/** Flat magic find and increased loot quantity from nodes outside the Explorer branch, and inside it. */
	float MagicFind = 0.0f;
	float ExplorerMagicFind = 0.0f;
	float LootQuantity = 0.0f;
	float ExplorerLootQuantity = 0.0f;

	/** Increased movement speed, inside a dungeon: Pathfinding. */
	float MovementSpeed = 0.0f;

	/** Loot quantity for each active Cataclysm type: Spoils of War. */
	float LootQuantityPerActiveType = 0.0f;

	/** Loot quantity for each whole two days a walk is shorter than its default, to its cap: Speed Runner. */
	float LootQuantityPerTwoDaysRemoved = 0.0f;
	float TwoDaysRemovedCap = 0.0f;

	/** Loot quantity for each whole day a walk is shorter than its default, to its cap: Efficiency Premium. */
	float LootQuantityPerDayRemoved = 0.0f;
	float DayRemovedCap = 0.0f;

	/** Magic find for each floor removed from a dungeon's default depth: Quality over Quantity. */
	float MagicFindPerFloorRemoved = 0.0f;

	/** Magic find for each distinct modifier on the floor being walked: Dungeon Expertise. */
	float MagicFindPerModifierActive = 0.0f;

	/** Magic find for each floor cleared in the dungeon being walked: Manifest Wealth. */
	float MagicFindPerFloorCleared = 0.0f;

	/** Magic find for each whole five dungeons cleared this run: Dungeon Legend. */
	float MagicFindPerFiveClears = 0.0f;

	/** Loot quantity and magic find for each standing city holding a bought Explorer city upgrade: The Cartographer's
	 *  Blessing. */
	float LootQuantityPerExplorerCity = 0.0f;
	float MagicFindPerExplorerCity = 0.0f;

	/** One-Day Specialist: the walk, in days, at which the Explorer branch's loot figures are doubled; 0 without it. */
	int32 ExplorerLootDoubledAtWalkDays = 0;

	/** Increased attack and spell damage, inside a dungeon, for each dungeon kind defeated this run: Dungeon Mastery. */
	float DamagePerKindDefeated = 0.0f;

	// ---- Slice 5b: Relentless Pursuit and the four drop rules. Issue #2741, the coordinating session's D5 and D6. ----

	/** Relentless Pursuit's loot quantity for each dungeon cleared since the last death, and its cap on what they bring
	 *  (D5: +100%), held beside it as Speed Runner's is. An Explorer figure, so One-Day Specialist doubles it after the
	 *  cap. */
	float LootQuantityPerCleanClear = 0.0f;
	float CleanClearCap = 0.0f;

	/** Opportunistic Looting's chance for an Elite's kill to drop one more gear item, and Plunderer's Eye's for any kill
	 *  to drop one more crafting material: Explorer chances, doubled by One-Day Specialist. */
	float EliteExtraItemChance = 0.0f;
	float ExtraMaterialChance = 0.0f;

	/** Scavenger's chance for each tier 1 to 3 crafting material a kill drops to be a stack of two. An Artisan node, so
	 *  One-Day Specialist does not double it. */
	float LowTierMaterialExtraChance = 0.0f;

	/** The High Roller's chance, for each whole ten floors added to the dungeon, of a Boss or Cataclysm Boss dropping one
	 *  more gear item of Legendary or rarer. An Explorer chance. */
	float BossLegendaryChancePerTenFloorsAdded = 0.0f;

	// ---- Slice 6a: the gold a kill drops, and a city upgrade's price. The coordinating session's G1 to G3, P1 and P2
	// of 2026-11-20. ----

	/** Coinage's and Minting Rights' increase to the gold a kill drops, added (G1). */
	float GoldIncreased = 0.0f;

	/** The Midas Touch's "50% more": a multiplier of its own, after the increase (G1). */
	float GoldMore = 0.0f;

	/** Untouchable Wealth's increase for each city not fallen; Wartime Economy's for each active Cataclysm type;
	 *  Profiteering's for each fallen city not erased; Prosperity Index's for each whole 10,000 citizens of the standing
	 *  cities; Fiscal Policy's for each whole 5 upgrades they hold (G3). Each added into the one increase. */
	float GoldPerStandingCity = 0.0f;
	float GoldPerActiveType = 0.0f;
	float GoldPerFallenCity = 0.0f;
	float GoldPerTenThousandCitizens = 0.0f;
	float GoldPerFiveCityUpgrades = 0.0f;

	/** Municipal Bonds' and Public Works' reduction of every city upgrade's price, and War Funding's for an upgrade
	 *  bought on a city that holds none (P2). They add, held at `UpgradePriceReductionCap` (P1). Price Controls is
	 *  slice 6b's (its P3 reading was withdrawn). */
	float UpgradePriceReduced = 0.0f;
	float FirstUpgradePriceReduced = 0.0f;

	// ---- Slice 6b, the coordinating session's B1 to B5 of 2026-11-20. ----

	/** Price Controls' reduction for an upgrade bought on a city a dungeon has damaged this run (B1). */
	float SurgeDamagedCityUpgradePriceReduced = 0.0f;

	/** Stimulus Package's "Gold drops doubled": 1 while the run's population has fallen below half (B2). */
	float GoldDoubledBelowHalfPopulation = 0.0f;

	/** Cleanup Crew's share of the walk's gold left lying, paid at a clear (B3). */
	float UnpickedGoldShareAtClear = 0.0f;

	/** Salvage Rights' tier 1 gold for every floor cleared, paid at a clear (B4); Investment Funds' increase of it (B5). */
	float GoldPerFloorAtClear = 0.0f;
	float ClearPayoutIncreased = 0.0f;

	// ---- Slice 6c, the coordinating session's B6 and B8 of 2026-11-20. ----

	/** The Gilded Path's magic find for each whole 10,000 tier 1 gold held, uncapped by its text; Venture Capital's for
	 *  each whole 50,000, held at its cap (B6). */
	float MagicFindPerTenThousandGoldHeld = 0.0f;
	float MagicFindPerFiftyThousandGoldHeld = 0.0f;
	float MagicFindFromGoldCap = 0.0f;

	/** Pax Imperialis' tier 1 gold a day for each whole 1,000 citizens; Wealth Interest's share of the gold held every
	 *  20th day and its tier 1 cap a payment (B8). */
	float GoldPerThousandCitizensDaily = 0.0f;
	float GoldInterestEveryTwentyDays = 0.0f;
	float GoldInterestCap = 0.0f;

	// ---- Slice 6d, the owner's ruling of 2026-11-20 on Tax Collection (D8). ----

	/** Tax Collection's share of the standing cities' citizens, paid as tier 1 gold every 10th day. */
	float GoldShareOfPopulationEveryTenDays = 0.0f;
};

/**
 * What the dungeon being walked, and its run, say to the player nodes. Issue #2741 slice 5a. Filled by
 * `UCataclysmEmpireTree::DungeonStateFor` from the run, the bound dungeon and the floor; every field is a count or a day
 * figure the readings of the coordinating session's plan acceptance of 2026-11-19 name, each a judgement.
 */
struct CATACLYSM_API FCataclysmEmpireTreeDungeonState
{
	/** How many Cataclysms are active in the run. */
	int32 ActiveTypes = 0;

	/** The dungeon's walk at the starting rate -- its floors, a day each, two for a Cow Level -- and what it costs. */
	float DefaultWalkDays = 0.0f;
	float WalkDays = 0.0f;

	/** Floors below the dungeon's default depth, the depth before city upgrades and the tree; never below nought. */
	int32 FloorsRemoved = 0;

	/** Distinct modifiers on the floor being walked. */
	int32 ModifiersActive = 0;

	/** Floors cleared in the dungeon being walked: the floor number less one. */
	int32 FloorsCleared = 0;

	/** Dungeons cleared this run, every kind. */
	int32 DungeonsCleared = 0;

	/** Standing cities holding at least one bought Explorer city upgrade. */
	int32 ExplorerCities = 0;

	/** Of the four dungeon kinds -- Basic, Quest, Fallen City, Cataclysm -- how many have been defeated this run. */
	int32 KindsDefeated = 0;

	/** Dungeons cleared since the character last died in this run (`UCataclysmEmpireRun::ClearsSinceLastDeath`), which
	 *  Relentless Pursuit counts. Slice 5b. */
	int32 ClearsSinceLastDeath = 0;

	/** Floors the dungeon has beyond its default depth, never fewer than none: the mirror of `FloorsRemoved`, and what
	 *  The High Roller counts "floors added to a dungeon" as. Slice 5b. */
	int32 FloorsAdded = 0;

	/** Slice 6a's counts over the empire's cities (G3): those not fallen; those fallen and not erased, which can still
	 *  be reclaimed; the citizens of the standing cities, as `UCataclysmEmpireMap::TotalPopulation` counts them; and the
	 *  upgrades the standing cities hold. */
	int32 StandingCities = 0;
	int32 FallenCitiesNotErased = 0;
	float Citizens = 0.0f;
	int32 StandingCityUpgrades = 0;

	/** `UCataclysmEmpireRun::bPopulationFellBelowHalf`, which Stimulus Package reads. Slice 6b (B2). */
	bool bPopulationBelowHalf = false;

	/** The gold the character playing holds, and the tier's gold scale (R10), which The Gilded Path and Venture Capital
	 *  read. Slice 6c (B6). Filled by the dungeon game mode at a kill; none and 1 elsewhere. */
	int64 GoldHeld = 0;
	double GoldScale = 1.0;
};

/**
 * What the empire upgrade tree pays on a day that passes, in gold. Issue #2741 slice 6c, the coordinating session's B8
 * of 2026-11-20: Pax Imperialis every day, Wealth Interest every 20th; and slice 6d's Tax Collection every 10th (D8).
 * Worked out by `UCataclysmEmpireTree::DailyIncomeFor` and paid by the game instance through
 * `UCataclysmEmpireRun::OnDayPassed`.
 */
struct CATACLYSM_API FCataclysmEmpireTreeDailyIncome
{
	int64 Pax = 0;
	int64 Interest = 0;
	int64 Tax = 0;

	int64 Total() const { return Pax + Interest + Tax; }
};

/**
 * What the empire upgrade tree pays at a dungeon's clear, in gold. Issue #2741 slice 6b, the coordinating session's B3
 * and B4 of 2026-11-20: Cleanup Crew's tenth of the walk's gold left lying, and Salvage Rights' gold for every floor,
 * scaled to the tier (R10) and raised by Investment Funds and the increases G2 sends to a payout. Worked out by
 * `UCataclysmEmpireTree::ClearPayFor` before the clear removes the dungeon, and paid by
 * `ACataclysmDungeonGameMode::ClearEmpireDungeon` once the clear stands.
 */
struct CATACLYSM_API FCataclysmEmpireTreeClearPay
{
	int64 Salvage = 0;
	int64 Cleanup = 0;

	int64 Total() const { return Salvage + Cleanup; }
};

/**
 * The rules that decide where an empire upgrade point may go. Issue #2741, slice 1a.
 *
 * THE EMPIRE UPGRADE TREE is the permanent tree an account buys with the empire upgrade points a defeated dungeon
 * awards (`docs/Cataclysm_GDD_v2.md`, "Empire-Wide Upgrades"). Its node graph is
 * `docs/Empire_Development_Tree_Final.json`, which reaches the game as `game/Data/EmpireTreeNodes.csv` and
 * `EmpireTreeEdges.csv`.
 *
 * THE CLASS TREES' RULES, IN THE CLASS TREES' SHAPE. `UCataclysmPassiveTree` is the precedent: statics over plain
 * values and the generated tables, so the automation tests reach all of it with no screen. The allocation is the same
 * struct, `FCataclysmPassiveAllocation` -- node, points, chosen option -- because the empire tree records exactly that.
 *
 * THE RULES:
 *
 *   a node holds at most its own MaxPoints
 *   a node opens when ANY ONE of the edges into it is satisfied: the source holds the edge's `RequiredPoints`. A node
 *     with no edge into it is open. The coordinating session's C2 of 2026-11-18, which is the class trees' rule
 *   a keystone's edge asks for its parent in full, which the source file already writes
 *   a centre capstone opens at 25, 50, 100 or 200 points spent in the WHOLE TREE, and is taken by choosing one of
 *     three options and then spending its one point
 *   a branch capstone opens at 200 points spent in ITS OWN BRANCH (the coordinating session, Q2, 2026-11-18)
 *   a node, or a centre capstone's option, whose system the game does not have is refused, and the refusal names the
 *     system: the owner's scope ruling of 2026-11-18
 *   a point costs one empire upgrade point from the bank, which holds fractions; only whole points are spent
 *   a respec returns every point to the bank and clears the four centre choices, and costs the lethality mode's death
 *     cost in days (Q3)
 *
 * WHAT A BOUGHT NODE DOES IS ADDED UP HERE AND DONE WHERE EACH NUMBER IS READ: `CityBonusesFor` for the empire's
 * cities and dungeons (#2741 slices 3 and 4), which the run reads; `PlayerBonusesFor` for the player inside a dungeon
 * (slice 5a), which the dungeon game mode puts on the character. Where the allocation is kept -- the account record of
 * the lethality mode, or a Solo Self-Found character's own -- is slices 1b and 1c.
 */
/**
 * The four chances the empire upgrade tree's drop rules give a kill in the dungeon being walked, each a share from 0 to
 * 1 and held at 1. Issue #2741 slice 5b, each reading the coordinating session's D6 and E1 of 2026-11-19. Worked out by
 * `UCataclysmEmpireTree::DropRulesFor`, read at a kill through `ACataclysmDungeonGameMode::EmpireTreeDropRulesIn`, and
 * rolled by `UCataclysmDropSpawner::SpawnTheTreesExtraDropsFor` and the material roll. All nought, the default, rolls
 * nothing and draws nothing from the kill's stream.
 */
struct CATACLYSM_API FCataclysmEmpireTreeDropRules
{
	/** Opportunistic Looting: an Elite's kill drops one more gear item, rolled as its others. */
	float EliteExtraItemChance = 0.0f;

	/** Plunderer's Eye: any kill drops one more crafting material. */
	float ExtraMaterialChance = 0.0f;

	/** Scavenger: each tier 1 to 3 crafting material the kill drops is a stack of two. */
	float LowTierMaterialExtraChance = 0.0f;

	/** The High Roller: a Boss's or Cataclysm Boss's kill drops one more gear item of Legendary or rarer. */
	float BossLegendaryChance = 0.0f;

	/**
	 * WHAT EACH GOLD PILE THE KILL DROPS IS MULTIPLIED BY, slice 6a (G1): one plus every increase, times one plus The
	 * Midas Touch's "more". Exactly 1 with no gold node, and never a roll: `UCataclysmDropSpawner::SpawnGoldFor`
	 * multiplies each pile's amount after the pile's own draws, so it draws nothing and `RollsAnything` leaves it out.
	 */
	float GoldMultiplier = 1.0f;

	/** The magic find, in percentage points, the gold held gives this kill: The Gilded Path and Venture Capital (slice 6c,
	 *  B6). Nothing drawn. */
	float MagicFindFromGold = 0.0f;

	bool RollsAnything() const
	{
		return EliteExtraItemChance > 0.0f || ExtraMaterialChance > 0.0f || LowTierMaterialExtraChance > 0.0f
			|| BossLegendaryChance > 0.0f;
	}
};

UCLASS()
class CATACLYSM_API UCataclysmEmpireTree : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** How many options a centre capstone offers. */
	static constexpr int32 CapstoneOptions = 3;

	/** What one point of the tree costs, in empire upgrade points. */
	static constexpr double PointCost = 1.0;

	/**
	 * How much of a point the bank may be short and still pay for one.
	 *
	 * THE BANK IS A SUM OF FRACTIONS -- an ordinary dungeon awards 1 times the share of the population living -- so three
	 * awards of a third add to a number a hair below one. The empire map forgives the same noise before it rounds the
	 * bank down to a tenth (`UCataclysmEmpireMapLayout::EmpirePointsText`), and a bank the map shows as "1.0" pays for a
	 * point here.
	 */
	static constexpr double BankNoise = 1.0e-6;

	/** The branch the four tier-milestone capstones in the middle belong to. */
	static const FString CentreBranch;

	/** The `Kind` column's three values. */
	static const FString BasicKind;
	static const FString KeystoneKind;
	static const FString CapstoneKind;

	/** The generated node table, loaded once and kept. Null when missing. */
	static const UDataTable* LoadNodeTable();

	/** The generated edge table, the same way. */
	static const UDataTable* LoadEdgeTable();

	/** The generated effect table, `DT_EmpireTreeEffects`, the same way. Issue #2741 slice 3a. */
	static const UDataTable* LoadEffectTable();

	/**
	 * What an allocation's bought city nodes add up to, for the empire layer to read. Issue #2741 slice 3a.
	 *
	 * ONE PASS OVER THE EFFECT TABLE AND NO NODE NAMED: each row says which node or option it belongs to, what it
	 * changes and by how much a point (`FCataclysmEmpireTreeEffectRow`). A node's points come from the allocation; an
	 * option acts while it is the capstone's chosen one and the capstone's point is bought. Golden Age and Bastion
	 * Spirit count whole steps of 20 points in another branch (C3) and stop at their caps (S3, S4).
	 *
	 * @param NodeTable the node table, which says which branch each node is in
	 * @param EffectTable the effect table
	 * @param Allocation what has been bought
	 * @return no bonuses at all with either table missing, which is the game before slice 3a
	 */
	static FCataclysmEmpireTreeBonuses CityBonusesFor(const UDataTable* NodeTable, const UDataTable* EffectTable,
													  const FCataclysmPassiveAllocation& Allocation);

	/**
	 * The words three player nodes count in, which the effect table has no column for: Speed Runner's "per 2 full
	 * days", Dungeon Legend's "per 5 unique dungeons" and One-Day Specialist's "doubled". Issue #2741 slice 5a;
	 * `tools/tests/test_empire_tree_player_nodes.py` finds each in its node's text.
	 */
	static constexpr int32 SpeedRunnerDaysAStep = 2;
	static constexpr int32 DungeonLegendClearsAStep = 5;
	static constexpr float OneDaySpecialistFactor = 2.0f;

	/** And slice 5b's: The High Roller's "For every 10 floors added to a dungeon", counted in whole steps (C3). */
	static constexpr int32 HighRollerFloorsAStep = 10;

	/** And slice 6a's: Prosperity Index's "for every 10,000 citizens" and Fiscal Policy's "for every 5 active City
	 *  upgrades", each counted in whole steps (G3). `tools/tests/test_empire_tree_gold_nodes.py` finds both in the
	 *  nodes' texts. */
	static constexpr int32 ProsperityCitizensAStep = 10000;
	static constexpr int32 FiscalPolicyUpgradesAStep = 5;

	/** And slice 6c's: The Gilded Path's "per 10,000 Gold", Venture Capital's "Every 50,000 Gold", both in tier 1 gold
	 *  (R10); Pax Imperialis' "For every 1,000 surviving citizens"; Wealth Interest's "Every 20 days". Each counted in
	 *  whole steps. `tools/tests/test_empire_tree_gold_held.py` finds each in its node's text. */
	static constexpr int32 GildedPathGoldAStep = 10000;
	static constexpr int32 VentureCapitalGoldAStep = 50000;
	static constexpr int32 PaxCitizensAStep = 1000;
	static constexpr int32 WealthInterestDaysAStep = 20;

	/** And slice 6d's: Tax Collection's "Every 10 days". `tools/tests/test_empire_tree_treasury_and_tax.py` finds it in
	 *  the node's text. */
	static constexpr int32 TaxCollectionDaysAStep = 10;

	/**
	 * THE MOST A CITY UPGRADE'S PRICE IS REDUCED BY, so a price never falls below a quarter. The reductions add, as every
	 * "reduced" does in the design's pipeline, and at full points they reach 125% (Municipal Bonds 45%, Public Works 20%,
	 * War Funding 20%, and slice 6b's Price Controls 40%). THE COORDINATING SESSION'S P1 OF 2026-11-20, A JUDGEMENT under the owner's delegation of
	 * unstated numbers; 75% is also the cap on the player's damage reduction.
	 */
	static constexpr float UpgradePriceReductionCap = 0.75f;

	/**
	 * What an allocation's bought player nodes add up to. Issue #2741 slice 5a. The same pass over the effect table as
	 * `CityBonusesFor`, reading the rows that one leaves alone: a node's points from the allocation, an option's one
	 * point while it is chosen, whole steps of another branch (C3) and a cap. The caps of Speed Runner and Efficiency
	 * Premium are on what the days bring, so they are held beside the figure rather than applied here.
	 *
	 * @return nothing at all with either table missing
	 */
	static FCataclysmEmpireTreePlayerBonuses PlayerBonusesFor(const UDataTable* NodeTable, const UDataTable* EffectTable,
															  const FCataclysmPassiveAllocation& Allocation);

	/**
	 * What the run and the dungeon being walked say to the player nodes. Issue #2741 slice 5a, each reading a judgement
	 * of the coordinating session's acceptance of 2026-11-19:
	 *
	 *   the default walk     the floors, a day each, two for a Cow Level; the walk is `EffectiveWalkDaysFor`
	 *   the default depth    Basic and Quest: `Floors - UpgradeFloors`, the city's and the tree's floors being one net
	 *                        figure; Fallen City: the more of its bosses and its tier's least floors; the boss dungeon:
	 *                        its base and the floors the Basic clears added. A Hollow's own shrinking is not a removal
	 *   floors cleared       the floor number less one
	 *   kinds defeated       Basic, Quest and Cataclysm from their own counts; Fallen City as the clears left over
	 *   Explorer cities      standing cities holding an upgrade whose row is in the Explorer branch
	 *   floors added         the floors beyond the default depth, at least none (slice 5b, The High Roller)
	 *   clean clears         the run's `ClearsSinceLastDeath` (slice 5b, Relentless Pursuit)
	 *
	 * @param FloorModifiers  the modifiers on the floor being walked, counted once each
	 * @param FloorNumber     the floor being walked, counted from 1
	 */
	static FCataclysmEmpireTreeDungeonState DungeonStateFor(const UCataclysmEmpireRun& Run, const FCataclysmDungeon& Dungeon,
															const TArray<FName>& FloorModifiers, int32 FloorNumber);

	/** Whether One-Day Specialist doubles the Explorer branch's loot figures here: it is bought, the walk is its days and
	 *  the default walk was longer. */
	static bool DoublesExplorerLoot(const FCataclysmEmpireTreePlayerBonuses& Bonuses,
									const FCataclysmEmpireTreeDungeonState& State);

	/** Flat magic find, in percentage points; increased loot quantity, movement speed and attack and spell damage, in
	 *  percent. What `PlayerStatModifiersFor` puts on the player. */
	static float MagicFindFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses, const FCataclysmEmpireTreeDungeonState& State);
	static float LootQuantityFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses,
								 const FCataclysmEmpireTreeDungeonState& State);
	static float MovementSpeedFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses);
	static float DamageFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses, const FCataclysmEmpireTreeDungeonState& State);

	/**
	 * The stat lines the player nodes put on the character inside a dungeon. Issue #2741 slice 5a. From the source
	 * `ECataclysmModifierSource::EmpireTree`: flat `magic_find`; increased `loot_quantity`, `movement_speed`,
	 * `attack_damage` and `spell_damage` -- Dungeon Mastery's "damage" is the two, as a passive node's "increased
	 * damage" is (2026-08-25). A figure of nought puts no line on, so a tree with no player node gives an empty map.
	 */
	static TMap<FName, TArray<FCataclysmStatModifier>> PlayerStatModifiersFor(
		const FCataclysmEmpireTreePlayerBonuses& Bonuses, const FCataclysmEmpireTreeDungeonState& State);

	/**
	 * The four chances the drop rules give a kill here. Issue #2741 slice 5b (D6): Opportunistic Looting's and
	 * Plunderer's Eye's per point; Scavenger's per point; The High Roller's for each whole ten floors added (C3). The
	 * three Explorer chances are doubled by One-Day Specialist where it doubles the branch's loot figures, and every
	 * chance is held at 1.
	 */
	static FCataclysmEmpireTreeDropRules DropRulesFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses,
													  const FCataclysmEmpireTreeDungeonState& State);

	/**
	 * What a kill's gold piles are multiplied by. Issue #2741 slice 6a (G1 to G3): one plus Coinage's and Minting
	 * Rights' increase and the four counted increases, times one plus The Midas Touch's "more". Exactly 1 with no gold
	 * node. `DropRulesFor` puts it in the rules a kill reads.
	 */
	static float GoldMultiplierFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses,
								   const FCataclysmEmpireTreeDungeonState& State);

	/**
	 * What a city upgrade's price is multiplied by on this city. Issue #2741 slice 6a (P1, P2): one less Municipal
	 * Bonds' and Public Works' reduction, War Funding's when the city holds no upgrade, and slice 6b's Price Controls'
	 * when a dungeon has damaged the city this run (`FCataclysmCity::bTookSurgeDamage`, B1), the sum held at
	 * `UpgradePriceReductionCap`. Exactly 1 with no price node, or with no map or city.
	 */
	static float UpgradePriceFactorFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses, const UCataclysmEmpireMap* Map,
									   int32 CityId);

	/**
	 * What the tree pays at the clear of a dungeon of `Floors` floors. Issue #2741 slice 6b. SALVAGE RIGHTS (B4): its
	 * tier 1 gold a point for every floor, times `GoldScale` (R10: the tier's gold pile over tier 1's), times one plus
	 * Investment Funds' increase (B5) and the "from dungeons" and "all sources" increases (G2: Wartime Economy,
	 * Profiteering, Untouchable Wealth, Fiscal Policy, Prosperity Index); The Midas Touch and Stimulus Package are a
	 * kill's alone. CLEANUP CREW (B3): its share of the gold dropped less the gold picked up, rounded down, and nothing
	 * multiplies it, because the piles already were.
	 */
	/**
	 * The run-wide part of the state the tree reads: the active Cataclysm types, the counts over the cities slice 6a's
	 * nodes read, and the run's Stimulus Package flag. `DungeonStateFor` starts from it, and a day's income reads it with
	 * no dungeon. Issue #2741 slice 6c.
	 */
	static FCataclysmEmpireTreeDungeonState EmpireStateFor(const UCataclysmEmpireRun& Run);

	/**
	 * The magic find, in percentage points, the gold held gives a kill (B6): The Gilded Path's per point for each whole
	 * 10,000, uncapped by its text, and Venture Capital's per point for each whole 50,000, held at its cap, the gold
	 * taken in tier 1 gold (`State.GoldHeld` over `State.GoldScale`, R10).
	 */
	static float MagicFindFromGoldFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses,
									  const FCataclysmEmpireTreeDungeonState& State);

	/**
	 * What the tree pays on day `Day` (B8): Pax Imperialis' gold a day for each whole 1,000 citizens of the standing
	 * cities, on every 20th day Wealth Interest's share of the gold held, held at its cap, and on every 10th day Tax
	 * Collection's share of the standing cities' citizens (slice 6d, D8); each in tier 1 gold times `GoldScale` (R10),
	 * and then raised by the "all sources" increases G2 sends to income (Untouchable Wealth, Fiscal Policy, Prosperity
	 * Index). Rounded to whole gold.
	 */
	static FCataclysmEmpireTreeDailyIncome DailyIncomeFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses,
														  const FCataclysmEmpireTreeDungeonState& State, int32 Day,
														  int64 GoldHeld, double GoldScale);

	static FCataclysmEmpireTreeClearPay ClearPayFor(const FCataclysmEmpireTreePlayerBonuses& Bonuses,
													const FCataclysmEmpireTreeDungeonState& State, int32 Floors,
													double GoldScale, int64 GoldDropped, int64 GoldPickedUp);

	/** The four branches, in the order the source file's metadata names them, and then the centre. */
	static TArray<FString> BranchNames();

	/** One node's row, or null. */
	static const FCataclysmEmpireTreeNodeRow* FindNode(const UDataTable* NodeTable, FName Node);

	/** Which branch a node is in, or empty for a node that does not exist. */
	static FString BranchOf(const UDataTable* NodeTable, FName Node);

	/**
	 * Every node of one branch, in the order a screen should show them: top to bottom on the authoring tool's canvas,
	 * then left to right, then by row name, as `UCataclysmPassiveTree::NodesIn` orders a class tree.
	 */
	static TArray<FName> NodesIn(const UDataTable* NodeTable, const FString& Branch);

	/** How many points the allocation holds in one branch. */
	static int32 SpentInBranch(const UDataTable* NodeTable, const FCataclysmPassiveAllocation& Allocation,
							   const FString& Branch);

	/** How many points the allocation holds anywhere in the tree, capstones included. */
	static int32 SpentInTree(const FCataclysmPassiveAllocation& Allocation) { return Allocation.Total(); }

	/**
	 * How many whole points a bank pays for: the bank rounded down, after forgiving `BankNoise`. Never below nought.
	 */
	static int32 WholePointsIn(double Bank);

	/**
	 * Whether the edges into this node allow it: true when it has none, and when any one of them is satisfied.
	 *
	 * TRUE FOR EVERY CAPSTONE. The three edges chaining the centre capstones require nothing, and the branch capstones
	 * have none; a capstone's threshold is asked separately.
	 */
	static bool EdgesAllow(const UDataTable* NodeTable, const UDataTable* EdgeTable,
						   const FCataclysmPassiveAllocation& Allocation, FName Node);

	/**
	 * How many points must be spent, and where, before a capstone opens; and how many are. False for a node that is
	 * not a capstone.
	 *
	 * @param OutWhere  "the tree" for a centre capstone, "the Architect branch" and so on for a branch one
	 */
	static bool CapstoneThreshold(const UDataTable* NodeTable, const FCataclysmPassiveAllocation& Allocation,
								  FName Node, int32& OutThreshold, int32& OutSpent, FString& OutWhere);

	/** What a node waits on, as the table words it after "it needs"; empty when it can be bought. */
	static FString WaitsOn(const UDataTable* NodeTable, FName Node);

	/** What one of a centre capstone's options waits on, the same way; empty when it can be chosen or does not exist. */
	static FString OptionWaitsOn(const UDataTable* NodeTable, FName Node, int32 Option);

	/**
	 * Why a point cannot go into this node, or empty when it can.
	 *
	 * A REASON RATHER THAN A BOOLEAN, so the screen and the log say the same thing. In this order, the first that
	 * applies: no such node; the node waits on a system; it is full; the bank cannot pay for a point; a capstone that has
	 * not opened, or whose option has not been chosen; nothing leading to it is invested in far enough.
	 *
	 * WHAT A NODE WAITS ON COMES BEFORE THE BANK, deliberately, and before the class trees' order would put it. It is
	 * true whatever the bank holds and for as long as the system is missing, so it is the thing worth telling a player
	 * looking at the node; "no points" would hide it until they had some.
	 */
	static FString RefusalForSpending(const UDataTable* NodeTable, const UDataTable* EdgeTable,
									  const FCataclysmPassiveAllocation& Allocation, FName Node, double Bank);

	/**
	 * Put one point into a node and take its cost from the bank.
	 *
	 * REFUSED WHOLE, changing nothing, with `OutReason` saying why. The bank never goes below nought: a bank short of a
	 * point by no more than `BankNoise` pays for it and is left at nought.
	 */
	static bool Spend(const UDataTable* NodeTable, const UDataTable* EdgeTable,
					  FCataclysmPassiveAllocation& Allocation, FName Node, double& Bank, FString& OutReason);

	/** The three options a centre capstone offers, empty entries and all. */
	static TArray<FString> OptionNamesOf(const FCataclysmEmpireTreeNodeRow& Row);

	/**
	 * Why one of a centre capstone's options cannot be chosen, or empty when it can.
	 *
	 * In this order: no such node; not a centre capstone; no such option; the option waits on a system; another option
	 * is already chosen (a choice stands until a respec); the capstone has not opened.
	 */
	static FString RefusalForChoosingOption(const UDataTable* NodeTable, const FCataclysmPassiveAllocation& Allocation,
											FName Node, int32 Option);

	/** Choose one of a centre capstone's three options. Choosing costs nothing; the capstone's point is spent after. */
	static bool ChooseOption(const UDataTable* NodeTable, FCataclysmPassiveAllocation& Allocation, FName Node,
							 int32 Option, FString& OutReason);

	/**
	 * Whether this node is a centre capstone that has opened and is waiting to be told which option to take: the
	 * question `UCataclysmPassiveTree::AwaitsAnOptionChoice` answers for a class tree, and for the reason it gives.
	 * False when every option it offers waits on a system, because then there is no choice a player could make.
	 */
	static bool AwaitsAnOptionChoice(const UDataTable* NodeTable, const FCataclysmPassiveAllocation& Allocation,
									 FName Node);

	//~ Respec. The coordinating session's Q3 of 2026-11-18.

	/**
	 * What a respec costs, in days of the run being played: the lethality mode's death cost, 5, 10 or 15.
	 *
	 * THE DESIGN CALLS IT "a tuning value" and gives no number (`docs/Cataclysm_GDD_v2.md`, "Empire-Wide Upgrades").
	 * The mode's death cost is the scale the game already prices a setback in, and a harder mode pays more. A judgement
	 * of the coordinating session, 2026-11-18.
	 *
	 * @param LethalityRung 0 Standard, 1 Hardcore, 2 Heretic, as `UCataclysmDayClock::DeathDayCostFor` takes it
	 */
	static int32 RespecDays(int32 LethalityRung);

	/**
	 * Why the tree cannot be respecced now, or empty when it can: refused when the allocation holds nothing at all, and
	 * when there is no run to take the days from.
	 *
	 * "NOTHING AT ALL", NOT "NO POINTS". A centre capstone's option is chosen before its point is spent, and a choice
	 * stands until a respec, so an allocation holding a choice and no points is one a player may need to respec.
	 *
	 * @param bRunBeingPlayed  whether there is a run to take the days from. A respec with no run is refused rather
	 *                         than given free.
	 */
	static FString RefusalForRespec(const FCataclysmPassiveAllocation& Allocation, bool bRunBeingPlayed);

	/**
	 * Return every spent point to the bank and forget every node and every choice.
	 *
	 * THE DAYS ARE NOT TAKEN HERE. They come off the run being played, which this class does not hold; the caller takes
	 * `RespecDays` from it in the same act. Refused, changing nothing, for the reasons `RefusalForRespec` gives.
	 *
	 * @return how many points went back to the bank
	 */
	static int32 Respec(FCataclysmPassiveAllocation& Allocation, double& Bank, bool bRunBeingPlayed,
						FString& OutReason);
};
