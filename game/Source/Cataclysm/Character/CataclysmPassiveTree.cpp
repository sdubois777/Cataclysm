// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmCharacterCreation.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

const FString UCataclysmPassiveTree::BasicKind = TEXT("basic");
const FString UCataclysmPassiveTree::KeystoneKind = TEXT("keystone");
const FString UCataclysmPassiveTree::CapstoneKind = TEXT("capstone");

namespace
{
	const TCHAR* NodeAssetPath = TEXT("/Game/Data/DT_PassiveNodes.DT_PassiveNodes");
	const TCHAR* EdgeAssetPath = TEXT("/Game/Data/DT_PassiveEdges.DT_PassiveEdges");
	const TCHAR* EffectAssetPath =
		TEXT("/Game/Data/DT_PassiveEffects.DT_PassiveEffects");

	/**
	 * Kept alive deliberately, the same way the class stat table is. Nothing
	 * else references the asset, so garbage collection would otherwise be free
	 * to take it back and the next question would pay the load again.
	 */
	TWeakObjectPtr<const UDataTable> CachedNodes;
	TWeakObjectPtr<const UDataTable> CachedEdges;
	TWeakObjectPtr<const UDataTable> CachedEffects;

	const UDataTable* LoadAndKeep(const TCHAR* Path,
								  TWeakObjectPtr<const UDataTable>& Cache)
	{
		if (Cache.IsValid())
		{
			return Cache.Get();
		}

		const UDataTable* Table = LoadObject<UDataTable>(nullptr, Path);
		if (Table)
		{
			const_cast<UDataTable*>(Table)->AddToRoot();
			Cache = Table;
		}
		return Table;
	}

	/** Every row of the node table, or nothing. */
	void ForEachNode(const UDataTable* Table,
					 const TFunctionRef<void(FName, const FCataclysmPassiveNodeRow&)>& Visit)
	{
		if (!Table)
		{
			return;
		}

		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			if (const FCataclysmPassiveNodeRow* Row =
					reinterpret_cast<const FCataclysmPassiveNodeRow*>(Pair.Value))
			{
				Visit(Pair.Key, *Row);
			}
		}
	}

	/** A comma-separated tag list, trimmed, with empties dropped. */
	TArray<FString> SplitTags(const FString& Text)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","), /*InCullEmpty=*/true);
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
		}
		Parts.RemoveAll([](const FString& Part) { return Part.IsEmpty(); });
		return Parts;
	}

	/** Every row of the edge table, or nothing. */
	void ForEachEdge(const UDataTable* Table,
					 const TFunctionRef<void(const FCataclysmPassiveEdgeRow&)>& Visit)
	{
		if (!Table)
		{
			return;
		}

		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			if (const FCataclysmPassiveEdgeRow* Row =
					reinterpret_cast<const FCataclysmPassiveEdgeRow*>(Pair.Value))
			{
				Visit(*Row);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// The allocation
// ---------------------------------------------------------------------------

int32 FCataclysmPassiveAllocation::Total() const
{
	int32 Sum = 0;
	for (const FCataclysmSpentNode& Spent : Nodes)
	{
		Sum += Spent.Points;
	}
	return Sum;
}

int32 FCataclysmPassiveAllocation::PointsIn(FName Node) const
{
	for (const FCataclysmSpentNode& Spent : Nodes)
	{
		if (Spent.Node == Node)
		{
			return Spent.Points;
		}
	}
	return 0;
}

int32 FCataclysmPassiveAllocation::ChosenOptionIn(FName Node) const
{
	for (const FCataclysmSpentNode& Spent : Nodes)
	{
		if (Spent.Node == Node)
		{
			return Spent.ChosenOption;
		}
	}
	return 0;
}

void FCataclysmPassiveAllocation::Add(FName Node, int32 Points)
{
	for (FCataclysmSpentNode& Spent : Nodes)
	{
		if (Spent.Node == Node)
		{
			Spent.Points += Points;
			return;
		}
	}

	FCataclysmSpentNode& Added = Nodes.AddDefaulted_GetRef();
	Added.Node = Node;
	Added.Points = Points;
}

void FCataclysmPassiveAllocation::SetChosenOption(FName Node, int32 Option)
{
	for (FCataclysmSpentNode& Spent : Nodes)
	{
		if (Spent.Node == Node)
		{
			Spent.ChosenOption = Option;
			return;
		}
	}

	// AN ENTRY WITH NO POINTS, deliberately. Choosing a capstone's option and
	// putting the point in are two separate acts and either may come first, so
	// the entry has to be able to exist holding only one of them.
	FCataclysmSpentNode& Added = Nodes.AddDefaulted_GetRef();
	Added.Node = Node;
	Added.ChosenOption = Option;
}

// ---------------------------------------------------------------------------
// Reading the tables
// ---------------------------------------------------------------------------

const UDataTable* UCataclysmPassiveTree::LoadNodeTable()
{
	return LoadAndKeep(NodeAssetPath, CachedNodes);
}

const UDataTable* UCataclysmPassiveTree::LoadEdgeTable()
{
	return LoadAndKeep(EdgeAssetPath, CachedEdges);
}

const UDataTable* UCataclysmPassiveTree::LoadEffectTable()
{
	return LoadAndKeep(EffectAssetPath, CachedEffects);
}

TArray<FString> UCataclysmPassiveTree::TreeNames(const UDataTable* NodeTable)
{
	TSet<FString> Seen;
	ForEachNode(NodeTable, [&Seen](FName, const FCataclysmPassiveNodeRow& Row)
	{
		if (!Row.Tree.IsEmpty())
		{
			Seen.Add(Row.Tree);
		}
	});

	// GATHERED AND SORTED, NOT TAKEN IN ROW ORDER. A DataTable keeps its rows in
	// a TMap and that order is an implementation detail, so a screen listing the
	// trees would otherwise show them in a different order after a re-import.
	// `UCataclysmClassStats::ClassNames` does the same for the same reason.
	TArray<FString> Out = Seen.Array();
	Out.Sort();
	return Out;
}

TArray<FName> UCataclysmPassiveTree::NodesIn(const UDataTable* NodeTable,
											 const FString& Tree)
{
	TArray<TPair<FName, const FCataclysmPassiveNodeRow*>> Found;
	ForEachNode(NodeTable, [&](FName Key, const FCataclysmPassiveNodeRow& Row)
	{
		if (Row.Tree == Tree)
		{
			Found.Add({Key, &Row});
		}
	});

	Found.Sort([](const TPair<FName, const FCataclysmPassiveNodeRow*>& Left,
				  const TPair<FName, const FCataclysmPassiveNodeRow*>& Right)
	{
		// TOP TO BOTTOM, THEN LEFT TO RIGHT, THEN BY ROW NAME. The third is not
		// decoration: without it two nodes at the same point on the canvas would
		// order differently between runs, and the four trees each stack several
		// nodes on one vertical line.
		if (Left.Value->PositionY != Right.Value->PositionY)
		{
			return Left.Value->PositionY < Right.Value->PositionY;
		}
		if (Left.Value->PositionX != Right.Value->PositionX)
		{
			return Left.Value->PositionX < Right.Value->PositionX;
		}
		return Left.Key.LexicalLess(Right.Key);
	});

	TArray<FName> Out;
	Out.Reserve(Found.Num());
	for (const TPair<FName, const FCataclysmPassiveNodeRow*>& Each : Found)
	{
		Out.Add(Each.Key);
	}
	return Out;
}

TArray<TPair<FName, FName>> UCataclysmPassiveTree::EdgesIn(
	const UDataTable* EdgeTable, const FString& Tree)
{
	TArray<TPair<FName, FName>> Out;
	ForEachEdge(EdgeTable, [&](const FCataclysmPassiveEdgeRow& Edge)
	{
		if (Edge.Tree == Tree)
		{
			Out.Add({FName(*Edge.Source), FName(*Edge.Target)});
		}
	});

	Out.Sort([](const TPair<FName, FName>& Left, const TPair<FName, FName>& Right)
	{
		if (Left.Key != Right.Key)
		{
			return Left.Key.LexicalLess(Right.Key);
		}
		return Left.Value.LexicalLess(Right.Value);
	});
	return Out;
}

const FCataclysmPassiveNodeRow* UCataclysmPassiveTree::FindNode(
	const UDataTable* NodeTable, FName Node)
{
	if (!NodeTable || Node.IsNone())
	{
		return nullptr;
	}

	return NodeTable->FindRow<FCataclysmPassiveNodeRow>(
		Node, TEXT("UCataclysmPassiveTree::FindNode"), /*bWarnIfMissing=*/false);
}

FString UCataclysmPassiveTree::TreeOf(const UDataTable* NodeTable, FName Node)
{
	const FCataclysmPassiveNodeRow* Row = FindNode(NodeTable, Node);
	return Row ? Row->Tree : FString();
}

int32 UCataclysmPassiveTree::SpentInTree(
	const UDataTable* NodeTable, const FCataclysmPassiveAllocation& Allocation,
	const FString& Tree)
{
	int32 Sum = 0;
	for (const FCataclysmSpentNode& Spent : Allocation.Nodes)
	{
		if (Spent.Points > 0 && TreeOf(NodeTable, Spent.Node) == Tree)
		{
			Sum += Spent.Points;
		}
	}
	return Sum;
}

bool UCataclysmPassiveTree::EdgesAllow(
	const UDataTable* NodeTable, const UDataTable* EdgeTable,
	const FCataclysmPassiveAllocation& Allocation, FName Node)
{
	const FCataclysmPassiveNodeRow* Row = FindNode(NodeTable, Node);
	if (!Row)
	{
		return false;
	}

	// A CAPSTONE HAS NO EDGES BY DESIGN, so asking about its edges must not be
	// the same as finding none unsatisfied. Whether it is open is its threshold's
	// question and is asked separately.
	if (Row->Kind == CapstoneKind)
	{
		return true;
	}

	const FString Key = Node.ToString();
	bool bAnyEdge = false;
	bool bAnySatisfied = false;

	ForEachEdge(EdgeTable, [&](const FCataclysmPassiveEdgeRow& Edge)
	{
		if (Edge.Target != Key)
		{
			return;
		}

		bAnyEdge = true;
		if (Allocation.PointsIn(FName(*Edge.Source)) >= Edge.RequiredPoints)
		{
			bAnySatisfied = true;
		}
	});

	// NO EDGE AT ALL MEANS OPEN, which is how a tree is started: each of the four
	// has exactly one such node and it is the one that unlocks the class
	// resource.
	//
	// ANY SATISFIED EDGE OPENS IT, NOT ALL OF THEM. Five nodes across the four
	// trees have two incoming edges, and two edges into one node are two routes
	// to it rather than two requirements. Reading it as "all" would make those
	// five need both parents, which nothing in the design asks for and which can
	// make a node unreachable.
	return !bAnyEdge || bAnySatisfied;
}

TArray<FString> UCataclysmPassiveTree::OptionNamesOf(
	const FCataclysmPassiveNodeRow& Row)
{
	return {Row.Option1Name, Row.Option2Name, Row.Option3Name};
}

FString UCataclysmPassiveTree::RefusalForSpending(
	const UDataTable* NodeTable, const UDataTable* EdgeTable,
	const FCataclysmPassiveAllocation& Allocation, FName Node,
	int32 PointsAvailable)
{
	const FCataclysmPassiveNodeRow* Row = FindNode(NodeTable, Node);
	if (!Row)
	{
		return FString::Printf(TEXT("%s is not a passive node."),
							   *Node.ToString());
	}

	if (Allocation.Total() >= PointsAvailable)
	{
		return FString::Printf(
			TEXT("No passive points left. %d earned and all of them spent."),
			PointsAvailable);
	}

	if (Allocation.PointsIn(Node) >= Row->MaxPoints)
	{
		return FString::Printf(TEXT("%s is full at %d point%s."), *Row->NodeName,
							   Row->MaxPoints, Row->MaxPoints == 1 ? TEXT("") : TEXT("s"));
	}

	if (Row->Kind == CapstoneKind)
	{
		const int32 Spent = SpentInTree(NodeTable, Allocation, Row->Tree);
		if (Spent < Row->Threshold)
		{
			return FString::Printf(
				TEXT("%s opens at %d points spent in the %s tree. %d so far."),
				*Row->NodeName, Row->Threshold, *Row->Tree, Spent);
		}

		// THE SABOTEUR'S FOUR OFFER NONE, and its own descriptions say to choose
		// one of three. Taking a capstone that offers nothing would spend a
		// point on a choice that was never written. Issue #935.
		const TArray<FString> Options = OptionNamesOf(*Row);
		const bool bAnyOption = Options.ContainsByPredicate(
			[](const FString& Name) { return !Name.IsEmpty(); });
		if (!bAnyOption)
		{
			return FString::Printf(
				TEXT("%s offers no options to choose from, so it cannot be "
					 "taken. Issue #935."),
				*Row->NodeName);
		}

		if (Allocation.ChosenOptionIn(Node) <= 0)
		{
			return FString::Printf(TEXT("Choose one of %s's three options first."),
								   *Row->NodeName);
		}

		return FString();
	}

	if (!EdgesAllow(NodeTable, EdgeTable, Allocation, Node))
	{
		return FString::Printf(
			TEXT("%s is shut. Nothing leading to it is invested in far enough."),
			*Row->NodeName);
	}

	return FString();
}

bool UCataclysmPassiveTree::Spend(const UDataTable* NodeTable,
								  const UDataTable* EdgeTable,
								  FCataclysmPassiveAllocation& Allocation,
								  FName Node, int32 PointsAvailable,
								  FString& OutReason)
{
	OutReason = RefusalForSpending(NodeTable, EdgeTable, Allocation, Node,
								   PointsAvailable);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	Allocation.Add(Node, 1);
	return true;
}

FString UCataclysmPassiveTree::RefusalForChoosingOption(
	const UDataTable* NodeTable, const FCataclysmPassiveAllocation& Allocation,
	FName Node, int32 Option)
{
	const FCataclysmPassiveNodeRow* Row = FindNode(NodeTable, Node);
	if (!Row)
	{
		return FString::Printf(TEXT("%s is not a passive node."),
							   *Node.ToString());
	}

	if (Row->Kind != CapstoneKind)
	{
		return FString::Printf(TEXT("%s is not a capstone, so it offers no "
									"choice."), *Row->NodeName);
	}

	const TArray<FString> Options = OptionNamesOf(*Row);
	const bool bAnyOption = Options.ContainsByPredicate(
		[](const FString& Name) { return !Name.IsEmpty(); });
	if (!bAnyOption)
	{
		return FString::Printf(
			TEXT("%s offers no options at all. Issue #935."), *Row->NodeName);
	}

	if (Option < 1 || Option > CapstoneOptions)
	{
		return FString::Printf(TEXT("Choose 1, 2 or 3, not %d."), Option);
	}

	if (Options[Option - 1].IsEmpty())
	{
		return FString::Printf(TEXT("%s has no option %d."), *Row->NodeName,
							   Option);
	}

	// THE CHOICE IS PERMANENT AND THE DESIGN SAYS SO OUTRIGHT. Every capstone's
	// own description ends "The choice is permanent." Changing it is what the
	// Trainer's respec is for, which resets the whole tree rather than one node.
	const int32 Already = Allocation.ChosenOptionIn(Node);
	if (Already > 0 && Already != Option)
	{
		return FString::Printf(
			TEXT("%s is already committed to %s, and the choice is permanent. "
				 "The Trainer respecs the whole tree."),
			*Row->NodeName, *Options[Already - 1]);
	}

	// THE THRESHOLD IS CHECKED HERE TOO, so choosing cannot run ahead of the
	// tier being reached. Otherwise a player could decide all four capstones at
	// 25 points and spend into them later with no decision left to make.
	const int32 Spent = SpentInTree(NodeTable, Allocation, Row->Tree);
	if (Spent < Row->Threshold)
	{
		return FString::Printf(
			TEXT("%s opens at %d points spent in the %s tree. %d so far."),
			*Row->NodeName, Row->Threshold, *Row->Tree, Spent);
	}

	return FString();
}

bool UCataclysmPassiveTree::ChooseOption(const UDataTable* NodeTable,
										 FCataclysmPassiveAllocation& Allocation,
										 FName Node, int32 Option,
										 FString& OutReason)
{
	OutReason = RefusalForChoosingOption(NodeTable, Allocation, Node, Option);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	Allocation.SetChosenOption(Node, Option);
	return true;
}

FString UCataclysmPassiveTree::DescribeNode(
	const UDataTable* NodeTable, const UDataTable* EdgeTable,
	const FCataclysmPassiveAllocation& Allocation, FName Node,
	int32 PointsAvailable)
{
	const FCataclysmPassiveNodeRow* Row = FindNode(NodeTable, Node);
	if (!Row)
	{
		return FString();
	}

	const int32 Held = Allocation.PointsIn(Node);
	FString Line = FString::Printf(TEXT("%s    %d / %d"), *Row->NodeName, Held,
								   Row->MaxPoints);

	if (Row->Kind == CapstoneKind)
	{
		const int32 Chosen = Allocation.ChosenOptionIn(Node);
		if (Chosen >= 1 && Chosen <= CapstoneOptions)
		{
			Line += FString::Printf(TEXT("    %s"),
									*OptionNamesOf(*Row)[Chosen - 1]);
		}
	}

	// THE REFUSAL IS PART OF THE LINE RATHER THAN A SEPARATE TOOL TIP. What a
	// player needs to know about a shut node is why it is shut, and a node that
	// is full says so here instead, which is the same question answered.
	const FString Refusal = RefusalForSpending(NodeTable, EdgeTable, Allocation,
											   Node, PointsAvailable);
	if (!Refusal.IsEmpty())
	{
		Line += FString::Printf(TEXT("    %s"), *Refusal);
	}

	return Line;
}

bool UCataclysmPassiveTree::TreeIsReachable(const FString& Tree,
											const TArray<FName>& DamageTypes)
{
	const FName AsClass(*Tree);
	for (const FName& DamageType : DamageTypes)
	{
		if (UCataclysmCharacterCreation::ClassesFor(DamageType).Contains(AsClass))
		{
			return true;
		}
	}
	return false;
}

TArray<FString> UCataclysmPassiveTree::ReachableTrees(
	const UDataTable* NodeTable, const TArray<FName>& DamageTypes)
{
	TArray<FString> Out;
	for (const FString& Tree : TreeNames(NodeTable))
	{
		if (TreeIsReachable(Tree, DamageTypes))
		{
			Out.Add(Tree);
		}
	}
	return Out;
}

TMap<FName, TArray<const FCataclysmPassiveEffectRow*>>
UCataclysmPassiveTree::EffectsByNode(const UDataTable* EffectTable)
{
	TMap<FName, TArray<const FCataclysmPassiveEffectRow*>> Out;
	if (!EffectTable)
	{
		return Out;
	}

	for (const TPair<FName, uint8*>& Row : EffectTable->GetRowMap())
	{
		const FCataclysmPassiveEffectRow* Effect =
			reinterpret_cast<const FCataclysmPassiveEffectRow*>(Row.Value);
		if (Effect && !Effect->Node.IsEmpty())
		{
			Out.FindOrAdd(FName(*Effect->Node)).Add(Effect);
		}
	}

	// SORTED BY STAT NAME, SO ONE NODE'S ROWS COME BACK IN THE SAME ORDER EVERY
	// TIME. A DataTable keeps its rows in a TMap and that order is a hash order.
	// Nothing here depends on the order -- the three buckets sum and multiply the
	// same either way -- but a caller reading a node's first effect should not
	// get a different answer depending on where a name happened to land.
	for (TPair<FName, TArray<const FCataclysmPassiveEffectRow*>>& Pair : Out)
	{
		Pair.Value.Sort([](const FCataclysmPassiveEffectRow& Left,
						   const FCataclysmPassiveEffectRow& Right)
		{
			return Left.Stat < Right.Stat;
		});
	}

	return Out;
}

TArray<const FCataclysmPassiveEffectRow*> UCataclysmPassiveTree::EffectsFor(
	const UDataTable* EffectTable, FName Node)
{
	const TMap<FName, TArray<const FCataclysmPassiveEffectRow*>> ByNode =
		EffectsByNode(EffectTable);
	if (const TArray<const FCataclysmPassiveEffectRow*>* Found = ByNode.Find(Node))
	{
		return *Found;
	}
	return {};
}

int32 UCataclysmPassiveTree::AccumulateInto(
	TMap<FName, TArray<FCataclysmStatModifier>>& Totals,
	const FCataclysmPassiveAllocation& Allocation, const UDataTable* NodeTable,
	const UDataTable* EffectTable, const TArray<FName>& DamageTypes)
{
	if (!NodeTable || !EffectTable)
	{
		return 0;
	}

	// WHICH TREES ARE DOING ANYTHING, ASKED ONCE. A character can have points in
	// four trees and the answer is the same for every node of a tree, so asking
	// per node would be the same question up to 230 times.
	TMap<FString, bool> TreeIsOn;

	// EVERY EFFECT ROW GROUPED BY THE NODE IT IS ABOUT, BUILT ONCE. Issue #953.
	//
	// A KEYED LOOKUP CANNOT DO THIS ANY MORE. Until #953 the DataTable's row name
	// WAS the node name, so one FindRow answered the question. A node may now
	// have several rows -- the Masochist's starting node grants three Fervour
	// rates -- so the row names carry a `#1` suffix and the node is a column.
	// The table is read in full once here rather than scanned once per spent
	// node, which a character can have 230 of.
	const TMap<FName, TArray<const FCataclysmPassiveEffectRow*>> EffectsForNode =
		EffectsByNode(EffectTable);

	int32 Added = 0;
	for (const FCataclysmSpentNode& Spent : Allocation.Nodes)
	{
		if (Spent.Points <= 0)
		{
			continue;
		}

		const FCataclysmPassiveNodeRow* Node = FindNode(NodeTable, Spent.Node);
		if (!Node)
		{
			// A NODE THE TABLE NO LONGER HAS. A saved allocation can name one
			// after a tree is re-authored, and skipping it is the only safe
			// answer: the points stay in the record and grant nothing.
			continue;
		}

		bool* Known = TreeIsOn.Find(Node->Tree);
		if (!Known)
		{
			Known = &TreeIsOn.Add(Node->Tree,
								  TreeIsActive(Node->Tree, DamageTypes));
		}
		if (!*Known)
		{
			// THE PROJECT OWNER'S RULE OF 2026-08-25, doing its work. Points in
			// a tree no equipped weapon reaches stay spent and grant nothing
			// until a weapon carrying its damage type is worn again.
			continue;
		}

		const TArray<const FCataclysmPassiveEffectRow*>* Effects =
			EffectsForNode.Find(Spent.Node);
		if (!Effects)
		{
			// MOST NODES ARE HERE AND IT IS NOT A FAULT. A minority of the 293
			// have an authored effect; the rest are rule changes, resource
			// generation and conditional bonuses that are not stat modifiers
			// under any authoring scheme. Issue #939.
			continue;
		}

		// ALL OF THEM, NOT THE FIRST. A node granting two stats grants both.
		for (const FCataclysmPassiveEffectRow* Effect : *Effects)
		{
			FCataclysmStatModifier Modifier;
			Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
			Modifier.Value = Effect->ValuePerPoint * Spent.Points;

			if (Effect->ValueKind.Equals(TEXT("more"), ESearchCase::IgnoreCase))
			{
				Modifier.Bucket = ECataclysmStatBucket::More;
			}
			else if (Effect->ValueKind.Equals(TEXT("flat"),
											  ESearchCase::IgnoreCase))
			{
				Modifier.Bucket = ECataclysmStatBucket::Flat;
			}
			else
			{
				Modifier.Bucket = ECataclysmStatBucket::Increased;
			}

			for (const FString& Tag : SplitTags(Effect->RequiredTags))
			{
				Modifier.RequiredTags.AddTag(
					FGameplayTag::RequestGameplayTag(FName(*Tag),
													 /*ErrorIfNotFound=*/false));
			}

			Totals.FindOrAdd(FName(*Effect->Stat)).Add(Modifier);
			++Added;
		}
	}

	return Added;
}

TMap<FName, TArray<FCataclysmStatModifier>> UCataclysmPassiveTree::ModifiersFor(
	const FCataclysmPassiveAllocation& Allocation, const UDataTable* NodeTable,
	const UDataTable* EffectTable, const TArray<FName>& DamageTypes)
{
	TMap<FName, TArray<FCataclysmStatModifier>> Out;
	AccumulateInto(Out, Allocation, NodeTable, EffectTable, DamageTypes);
	return Out;
}
