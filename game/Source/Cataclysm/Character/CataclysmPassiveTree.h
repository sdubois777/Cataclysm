// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmPassiveTree.generated.h"

struct FCataclysmPassiveNodeRow;
class UDataTable;

/**
 * One node a character has put points into.
 *
 * KEYED BY THE NODE'S ROW NAME in `game/Data/PassiveNodes.csv`, which is the
 * tree and the node identifier together -- `Masochist_basic_spine_005`. A node
 * identifier alone is unique only within its tree.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmSpentNode
{
	GENERATED_BODY()

	// SaveGame ON EVERY FIELD, because this struct is reached from
	// UCataclysmCharacterSave and the save writer walks only properties carrying
	// that marker. Without it the record serialises an empty object, the fixture
	// and the record disagree, and a character's whole tree is silently lost on
	// every save. FCataclysmAttributePoints carries the same warning and it is
	// the same trap. Issue #50.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Cataclysm|Passives")
	FName Node;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Cataclysm|Passives")
	int32 Points = 0;

	/**
	 * Which of a capstone's three options was taken: 1, 2 or 3.
	 *
	 * ZERO FOR EVERYTHING ELSE AND FOR A CAPSTONE NOT YET DECIDED. Zero is not
	 * an option number, so it cannot be mistaken for one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Cataclysm|Passives")
	int32 ChosenOption = 0;
};

/**
 * Everything a character has spent its passive points on.
 *
 * AN ARRAY RATHER THAN A MAP, and that is about the save format rather than
 * about taste. Every container already on `UCataclysmCharacterSave` is an array,
 * the writer is known to handle them, and a map's serialisation is a shape
 * nothing in this project has tried. The lists are short -- a character can
 * touch at most 230 nodes and in practice far fewer -- so a linear search is not
 * worth avoiding.
 *
 * ONLY NODES THAT HOLD SOMETHING ARE IN IT. A node with no points has no entry
 * rather than an entry saying zero, so an untouched character's record is empty.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmPassiveAllocation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Cataclysm|Passives")
	TArray<FCataclysmSpentNode> Nodes;

	/** How many points are spent altogether, across every tree. */
	int32 Total() const;

	/** How many sit in one node. Zero for a node with no entry. */
	int32 PointsIn(FName Node) const;

	/** Which capstone option was taken, or 0 for none. */
	int32 ChosenOptionIn(FName Node) const;

	/** Add to a node, creating its entry if it has none. */
	void Add(FName Node, int32 Points);

	/** Record which of a capstone's three options was taken. */
	void SetChosenOption(FName Node, int32 Option);

	/** Forget everything, so every point can be spent again. */
	void Clear() { Nodes.Reset(); }
};

/**
 * The rules that decide where a passive point may go.
 *
 * WHY IT IS A SEPARATE CLASS FROM THE SCREEN, which is the reason
 * `UCataclysmCharacterCreation` is separate from its widget: the automation test
 * command in `tools/unreal_build.py` passes `-nullrhi`, so nothing that reaches
 * a screen can be watched by a test. Everything here is a static function over
 * plain values and a data table, so all of it is covered.
 *
 * THE FOUR RULES, ALL FROM `docs/Cataclysm_GDD_v2.md`:
 *
 *   a node holds at most its own MaxPoints
 *   an edge from A to B carrying `RequiredPoints: 6` means B is shut until A
 *     holds six. A node with several incoming edges opens when ANY of them is
 *     satisfied, because two edges into one node are two routes to it
 *   a keystone's edge asks for its parent in full -- "Keystones ... Require full
 *     investment in a parent node" -- which needs no rule of its own here,
 *     because the source files already set those edges to the parent's MaxPoints
 *   a capstone opens at 25, 50, 100 or 200 points spent in ITS OWN TREE, by
 *     total rather than by any path, and is one choice of three
 *
 * WHAT A SPENT POINT IS WORTH IS NOT HERE AND DOES NOT EXIST. A node's effect is
 * a sentence written for a player to read and there is no number anywhere in the
 * source files. Issue #936. So a character can earn, spend and save points, and
 * receives nothing for them.
 */
UCLASS()
class CATACLYSM_API UCataclysmPassiveTree : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** How many options a capstone offers. Fixed by the design at three. */
	static constexpr int32 CapstoneOptions = 3;

	/** The `Kind` column's three values. */
	static const FString BasicKind;
	static const FString KeystoneKind;
	static const FString CapstoneKind;

	/** The generated node table, loaded once and kept. Null when missing. */
	static const UDataTable* LoadNodeTable();

	/** The generated edge table, the same way. */
	static const UDataTable* LoadEdgeTable();

	/** The generated effect table: what a node grants. Null when missing. */
	static const UDataTable* LoadEffectTable();

	/** Every tree the node table names, in alphabetical order. */
	static TArray<FString> TreeNames(const UDataTable* NodeTable);

	/**
	 * Every node of one tree, in the order a screen should show them.
	 *
	 * SORTED BY WHERE THEY SIT ON THE AUTHORING TOOL'S CANVAS, top to bottom
	 * and then left to right, rather than by row name. The layout is a design
	 * decision -- which limb a node is on says as much as its words -- so
	 * reading down the list follows the tree rather than the alphabet.
	 */
	static TArray<FName> NodesIn(const UDataTable* NodeTable, const FString& Tree);

	/** One node's row, or null. */
	static const FCataclysmPassiveNodeRow* FindNode(const UDataTable* NodeTable,
													FName Node);

	/** Which tree a node belongs to, or empty for a node that does not exist. */
	static FString TreeOf(const UDataTable* NodeTable, FName Node);

	/** How many points this allocation has put into one tree. */
	static int32 SpentInTree(const UDataTable* NodeTable,
							 const FCataclysmPassiveAllocation& Allocation,
							 const FString& Tree);

	/**
	 * Whether the paths leading to this node are open.
	 *
	 * TRUE FOR A NODE WITH NO INCOMING EDGE, which is how a tree is started:
	 * each of the four has exactly one such node and it is the one that unlocks
	 * the class's resource.
	 *
	 * TRUE FOR A CAPSTONE, which has no edges at all by design. Whether a
	 * capstone is open is a question about its threshold and is asked separately.
	 */
	static bool EdgesAllow(const UDataTable* NodeTable,
						   const UDataTable* EdgeTable,
						   const FCataclysmPassiveAllocation& Allocation,
						   FName Node);

	/**
	 * Why a point cannot go into this node, or empty when it can.
	 *
	 * A REASON RATHER THAN A BOOLEAN, so the screen and the console command say
	 * the same thing and a player learns which of the five refusals it was.
	 *
	 * @param PointsAvailable  what the character has earned altogether. The
	 *                         allocation says how many are already spent.
	 */
	static FString RefusalForSpending(const UDataTable* NodeTable,
									  const UDataTable* EdgeTable,
									  const FCataclysmPassiveAllocation& Allocation,
									  FName Node, int32 PointsAvailable);

	/**
	 * Put one point into a node.
	 *
	 * REFUSED WHOLE rather than clamped, with `OutReason` saying which refusal
	 * it was, for the reason `ACataclysmPlayerState::SpendAttributePoints`
	 * refuses: quietly doing nothing still reads as success.
	 */
	static bool Spend(const UDataTable* NodeTable, const UDataTable* EdgeTable,
					  FCataclysmPassiveAllocation& Allocation, FName Node,
					  int32 PointsAvailable, FString& OutReason);

	/** The three options a capstone offers, empty entries and all. */
	static TArray<FString> OptionNamesOf(const FCataclysmPassiveNodeRow& Row);

	/**
	 * Why one of a capstone's options cannot be taken, or empty when it can.
	 *
	 * THE SABOTEUR'S FOUR CAPSTONES OFFER NONE, so every option is refused on
	 * them and the reason says so rather than reporting a bad option number.
	 * Issue #935.
	 */
	static FString RefusalForChoosingOption(const UDataTable* NodeTable,
											const FCataclysmPassiveAllocation& Allocation,
											FName Node, int32 Option);

	/** Take one of a capstone's three options. */
	static bool ChooseOption(const UDataTable* NodeTable,
							 FCataclysmPassiveAllocation& Allocation,
							 FName Node, int32 Option, FString& OutReason);

	/** What a screen prints on one node: its points, its cap and its state. */
	static FString DescribeNode(const UDataTable* NodeTable,
								const UDataTable* EdgeTable,
								const FCataclysmPassiveAllocation& Allocation,
								FName Node, int32 PointsAvailable);

	//~ Which trees a character can reach, and what happens when it cannot.

	/**
	 * Whether a character carrying these damage types can spend in this tree.
	 *
	 * A TREE IS NAMED AFTER ITS CLASS, and a damage type unlocks its three
	 * classes: `docs/Cataclysm_GDD_v2.md` says "Each damage type unlocks three
	 * class passive trees" and "Players can spec into one class per damage type
	 * available on their weapon". So `Masochist` is reachable exactly when the
	 * character carries Demonic, and the three War trees when it carries War.
	 * `UCataclysmCharacterCreation::ClassesFor` is the one list of which classes
	 * belong to which damage type, and this asks it rather than holding a second.
	 */
	static bool TreeIsReachable(const FString& Tree,
								const TArray<FName>& DamageTypes);

	/** Every tree in the table those damage types reach, in the table's order. */
	static TArray<FString> ReachableTrees(const UDataTable* NodeTable,
										  const TArray<FName>& DamageTypes);

	/**
	 * Whether a tree a character has invested in is currently doing anything.
	 *
	 * THE PROJECT OWNER DECIDED THIS ON 2026-08-25 and `docs/DECISIONS.md`
	 * carries it with the sources. Taking off the weapon that unlocked a tree
	 * does NOT refund the points spent in it. They stay spent and everything the
	 * tree grants stops applying, until an equipped weapon carries its damage
	 * type again.
	 *
	 * THE ALTERNATIVE WAS REJECTED FOR A REASON. Refunding on unequip -- which
	 * is what Path of Exile 1 does when a cluster jewel is unsocketed -- would
	 * make a weapon swap an unlimited free respec, and this design already sells
	 * a class passive respec at the Trainer for a cost in days.
	 *
	 * THERE IS NOTHING TO SWITCH OFF YET, and that is why this is a question
	 * rather than a mechanism. A node's effect does not exist as data at all,
	 * issue #936, so no character receives anything from any tree, dormant or
	 * not. This is the question a caller applying those effects will ask, and it
	 * is here now so the rule is written down where the effects will be applied.
	 */
	static bool TreeIsActive(const FString& Tree, const TArray<FName>& DamageTypes)
	{
		return TreeIsReachable(Tree, DamageTypes);
	}

	//~ What the spent points are worth.

	/**
	 * Add what a character's spent passive points grant to its running totals.
	 *
	 * WHERE AN AFFIX JOINS, AND THE SAME SHAPE.
	 * `UCataclysmItemModifiers::AccumulateInto` does exactly this for a worn
	 * item, and `UCataclysmPlayerClassStats::ApplyTo` runs the result. A passive
	 * node is another authored source of the same three buckets, so it needed no
	 * new machinery in the pipeline at all.
	 *
	 * A DORMANT TREE CONTRIBUTES NOTHING, which is the project owner's decision
	 * of 2026-08-25 doing its work. Points spent in a tree no equipped weapon
	 * reaches stay spent and are skipped here, so the character loses what the
	 * tree granted and keeps the points.
	 *
	 * TIMES THE POINTS IN THE NODE. Every authored value is per point, which is
	 * what every description that has one says.
	 *
	 * MOST NODES ADD NOTHING BECAUSE MOST NODES HAVE NO ROW. 26 of the 293 do.
	 * Issue #939 measures that gap and says what each of the remaining groups
	 * would need. A node with no row is skipped silently, which is correct: it
	 * is not a fault, it is a node whose numbers nobody has authored.
	 *
	 * @param DamageTypes  what the character's equipped weapons carry. An empty
	 *                     array reaches no tree at all, so nothing is added.
	 * @return how many modifiers were added, so a caller can tell "nothing
	 *         applied" from "nothing was spent"
	 */
	static int32 AccumulateInto(TMap<FName, TArray<FCataclysmStatModifier>>& Totals,
								const FCataclysmPassiveAllocation& Allocation,
								const UDataTable* NodeTable,
								const UDataTable* EffectTable,
								const TArray<FName>& DamageTypes);

	/** The same, as a fresh map. For a caller that has no running totals. */
	static TMap<FName, TArray<FCataclysmStatModifier>> ModifiersFor(
		const FCataclysmPassiveAllocation& Allocation,
		const UDataTable* NodeTable, const UDataTable* EffectTable,
		const TArray<FName>& DamageTypes);
};
