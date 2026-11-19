// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmPassiveTree.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmEmpireTree.generated.h"

struct FCataclysmEmpireTreeNodeRow;
class UDataTable;

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
 * WHAT A BOUGHT NODE DOES IS NOT HERE. Slices 3 to 5 of #2741 add it; until then a node that can be bought does
 * nothing. Where the allocation is kept -- the account record of the lethality mode -- is slice 1b.
 */
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
