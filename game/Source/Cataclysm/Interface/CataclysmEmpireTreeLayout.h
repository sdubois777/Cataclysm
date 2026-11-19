// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interface/CataclysmPassiveTreeLayout.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmEmpireTreeLayout.generated.h"

struct FCataclysmEmpireTreeNodeRow;
class UDataTable;

/**
 * The empire upgrade tree screen's arithmetic and words, as plain functions over the generated tables. Issue #2741,
 * slice 2.
 *
 * THE CLASS TREE SCREEN'S SPLIT, AND MOST OF ITS ARITHMETIC. `UCataclysmPassiveTreeLayout` places a class tree's nodes
 * on a canvas, and everything in it that is plain geometry -- fitting a zoom, placing a point, turning a line, the
 * zoom's range -- is used as it is. The two things it does over a table read the class trees' row, so this class does
 * them over the empire tree's: a branch's extent, and the largest a node may be drawn in it.
 *
 * ONE BRANCH AT A TIME, a judgement of slice 2 (S3, agreed by the coordinating session on 2026-11-18). Measured that
 * day: the whole tree is about 1,257 by 1,373 units and its closest pair allows a node only 0.263 of the class trees'
 * node size, so drawn whole no name could be read. One branch spans about 590 by 560 and allows 0.28 to 0.38; the four
 * centre capstones are a fifth view of their own.
 *
 * EVERY WORD THE SCREEN SHOWS IS BUILT HERE, so a test reads the words a player does: the automation command passes
 * `-nullrhi`, and no headless test can see a screen.
 */
UCLASS()
class CATACLYSM_API UCataclysmEmpireTreeLayout : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The five views the screen's list offers, in `UCataclysmEmpireTree::BranchNames`'s order: four branches, then
	 *  the centre's four capstones. */
	static TArray<FString> Views();

	/** What a view's button says: the branch's name, or "The centre" for the centre's capstones. */
	static FString ViewLabel(const FString& Branch);

	/** The rectangle one branch's nodes occupy. `bAny` is false for a branch with no nodes. */
	static FCataclysmTreeExtent ExtentOf(const UDataTable* NodeTable, const FString& Branch);

	/**
	 * The largest a node may be drawn in one branch without two overlapping, as a factor of
	 * `UCataclysmPassiveTreeLayout::NodeWidthPx` and `NodeHeightPx`: `UCataclysmPassiveTreeLayout::NodeScaleFor`'s rule
	 * -- two boxes miss each other when clear on either axis, the smallest over every pair, capped at one -- over the
	 * empire tree's rows. 1 for a branch with fewer than two nodes.
	 */
	static float NodeScaleFor(const UDataTable* NodeTable, const FString& Branch);

	/** The edges whose two ends are both in one branch, as (source, target). No edge in the tree crosses branches;
	 *  the generator's tests pin that. */
	static TArray<TPair<FName, FName>> EdgesIn(const UDataTable* NodeTable, const UDataTable* EdgeTable,
											   const FString& Branch);

	/**
	 * The bank, as the screen's first line says it: "12.5 empire upgrade points banked: 12 to spend", the bank rounded
	 * down to a tenth as the map shows it and the whole points a node can take. A Solo Self-Found character's adds
	 * "; this character's own", since its tree is shared with no other character.
	 */
	static FString BankLine(double Banked, bool bTheCharactersOwn);

	/** The view being looked at, and the points in it and in the whole tree: "Architect: 26 points spent here, 26 in
	 *  the whole tree". "1 point", not "1 points". */
	static FString BranchLine(const FString& Branch, int32 SpentHere, int32 SpentInTree);

	/** What a node's box says: its name and points, "Structural Integrity  3/10", as a class tree's node says it. */
	static FString NodeLabel(const FCataclysmEmpireTreeNodeRow& Row, int32 Points);

	/**
	 * What the line under the branch says about one node: its name, branch and points, the tree file's description,
	 * a centre capstone's three options with the chosen one marked, and -- when it is not empty -- why a point cannot
	 * go in now (`UCataclysmEmpireTree::RefusalForSpending`'s sentence).
	 */
	static FString NodeDetail(const FCataclysmEmpireTreeNodeRow& Row, int32 Points, int32 ChosenOption,
							  const FString& Refusal);

	/** What one of a centre capstone's options says in the list: "2  The Delver", "(chosen)" after the one taken, and
	 *  "waits on ..." after one that waits on a system. */
	static FString OptionLabel(int32 Option, const FString& Name, bool bChosen, const FString& WaitsOn);

	/**
	 * What the Respec button says. "Respec: 5 days of the run" until it is pressed; "Press again to respec: 5 days of the
	 * run" while the second press is awaited. The two presses are the coordinating session's S1 of 2026-11-18: a respec
	 * costs days and cannot be undone.
	 */
	static FString RespecLabel(int32 Days, bool bAwaitingTheSecondPress);
};
