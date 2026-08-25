// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmPassiveTreeWidget.h"
#include "Cataclysm.h"
#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmPassiveTree.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Player/CataclysmPlayerState.h"
#include "Engine/DataTable.h"
#include "GameFramework/PlayerController.h"

const UDataTable* UCataclysmPassiveTreeWidget::NodeTable() const
{
	if (!Nodes)
	{
		// MUTABLE THROUGH A CONST FUNCTION, deliberately, and for the reason the
		// character creator's does the same: loading the generated table is
		// caching rather than a change of state.
		const_cast<UCataclysmPassiveTreeWidget*>(this)->Nodes =
			UCataclysmPassiveTree::LoadNodeTable();
	}
	return Nodes;
}

const UDataTable* UCataclysmPassiveTreeWidget::EdgeTable() const
{
	if (!Edges)
	{
		const_cast<UCataclysmPassiveTreeWidget*>(this)->Edges =
			UCataclysmPassiveTree::LoadEdgeTable();
	}
	return Edges;
}

void UCataclysmPassiveTreeWidget::SetTables(const UDataTable* InNodeTable,
											const UDataTable* InEdgeTable)
{
	Nodes = InNodeTable;
	Edges = InEdgeTable;
}

void UCataclysmPassiveTreeWidget::SetPlayerStateForTests(
	ACataclysmPlayerState* InState)
{
	StateForTests = InState;
}

ACataclysmPlayerState* UCataclysmPassiveTreeWidget::State() const
{
	// THE OWNING PLAYER FIRST, ALWAYS. The test seam is only reached when there
	// is no owning player at all, which is the case a headless test is in and
	// nothing in a running game ever is.
	if (APlayerController* Controller = GetOwningPlayer())
	{
		return Controller->GetPlayerState<ACataclysmPlayerState>();
	}

	return StateForTests;
}

TArray<FString> UCataclysmPassiveTreeWidget::AllTrees() const
{
	return UCataclysmPassiveTree::TreeNames(NodeTable());
}

TArray<FString> UCataclysmPassiveTreeWidget::ReachableTrees() const
{
	const ACataclysmPlayerState* Player = State();
	return Player ? Player->ReachableTrees() : TArray<FString>();
}

bool UCataclysmPassiveTreeWidget::ShowTree(const FString& Tree)
{
	if (!AllTrees().Contains(Tree))
	{
		return false;
	}

	ShownTree = Tree;

	// THE REFUSAL IS CLEARED BY CHANGING TREE. It was about a node in the tree
	// being left, so keeping it would leave a sentence on screen about something
	// the player is no longer looking at.
	LastRefusal.Reset();
	LastTouched = NAME_None;

	RefreshDisplay();
	return true;
}

TArray<FName> UCataclysmPassiveTreeWidget::ShownNodes() const
{
	return ShownTree.IsEmpty() ? TArray<FName>()
							   : UCataclysmPassiveTree::NodesIn(NodeTable(),
																ShownTree);
}

bool UCataclysmPassiveTreeWidget::SpendInto(FName Node)
{
	LastTouched = Node;

	ACataclysmPlayerState* Player = State();
	if (!Player)
	{
		LastRefusal = TEXT("This character has no player state, so it has "
						   "nowhere to keep passive points.");
		RefreshDisplay();
		return false;
	}

	const bool bSpent = Player->SpendPassivePoint(Node, LastRefusal);
	RefreshDisplay();
	return bSpent;
}

bool UCataclysmPassiveTreeWidget::ChooseOption(FName Node, int32 Option)
{
	LastTouched = Node;

	ACataclysmPlayerState* Player = State();
	if (!Player)
	{
		LastRefusal = TEXT("This character has no player state.");
		RefreshDisplay();
		return false;
	}

	const bool bChosen = Player->ChoosePassiveOption(Node, Option, LastRefusal);
	RefreshDisplay();
	return bChosen;
}

FText UCataclysmPassiveTreeWidget::PointsText() const
{
	const ACataclysmPlayerState* Player = State();
	if (!Player)
	{
		return FText::GetEmpty();
	}

	const int32 Earned = Player->PassivePointsAvailable();
	const int32 Unspent = Player->PassivePointsUnspent();

	// THE BUDGET IS PART OF THE LINE. 230 is what every class tree is designed
	// against and what a player is planning towards, and a bare "38 earned"
	// says nothing about how far through that is.
	return FText::FromString(FString::Printf(
		TEXT("Passive points    %d unspent of %d earned    the budget is %d"),
		Unspent, Earned, UCataclysmPassivePoints::Budget));
}

FText UCataclysmPassiveTreeWidget::TreeText() const
{
	if (ShownTree.IsEmpty())
	{
		return FText::FromString(TEXT("Choose a tree."));
	}

	const ACataclysmPlayerState* Player = State();
	const int32 Spent = Player
		? UCataclysmPassiveTree::SpentInTree(NodeTable(),
											 Player->GetPassiveAllocation(),
											 ShownTree)
		: 0;

	const bool bReachable = ReachableTrees().Contains(ShownTree);

	// A TREE THAT CANNOT BE SPENT IN SAYS SO AND SAYS WHY. The points already in
	// it stay there and stop applying -- the project owner's rule of 2026-08-25 --
	// so a player looking at a tree they have invested in and can no longer
	// reach needs to be told that rather than left to guess.
	// "1 point spent", not "1 points spent". This line is on screen the whole
	// time the tree is open, so the slip would be read every time.
	return FText::FromString(FString::Printf(
		TEXT("%s    %d point%s spent%s"), *ShownTree, Spent,
		Spent == 1 ? TEXT("") : TEXT("s"),
		bReachable
			? TEXT("")
			: TEXT("    no equipped weapon reaches this tree, so nothing in it "
				   "applies")));
}

void UCataclysmPassiveTreeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (TitleLabel)
	{
		TitleLabel->SetText(Title);
	}

	// THE FIRST TREE THE CHARACTER CAN ACTUALLY REACH, rather than the first
	// alphabetically. A Demonic character opening this screen wants the
	// Masochist tree, not the Berserker one they cannot spend in.
	if (ShownTree.IsEmpty())
	{
		const TArray<FString> Reachable = ReachableTrees();
		const TArray<FString> Every = AllTrees();
		if (Reachable.Num() > 0)
		{
			ShownTree = Reachable[0];
		}
		else if (Every.Num() > 0)
		{
			ShownTree = Every[0];
		}
	}

	RefreshDisplay();
}

void UCataclysmPassiveTreeWidget::RefreshDisplay()
{
	TArray<FName> TreeValues;
	for (const FString& Tree : AllTrees())
	{
		TreeValues.Add(FName(*Tree));
	}
	FillPanel(TreeBox, TreeValues, /*bTrees=*/true);
	FillPanel(NodeBox, ShownNodes(), /*bTrees=*/false);

	if (PointsLabel)
	{
		PointsLabel->SetText(PointsText());
	}

	if (TreeLabel)
	{
		TreeLabel->SetText(TreeText());
	}

	if (RefusalLabel)
	{
		RefusalLabel->SetText(RefusalText());
	}

	if (DescriptionLabel)
	{
		// WHAT THE LAST NODE TOUCHED SAYS IT DOES. This is the only place a
		// player reads a node's own words, and it is worth saying plainly that
		// the character does not receive them: no node's effect exists as data.
		// Issue #936.
		const FCataclysmPassiveNodeRow* Row =
			UCataclysmPassiveTree::FindNode(NodeTable(), LastTouched);
		DescriptionLabel->SetText(
			Row ? FText::FromString(Row->Description) : FText::GetEmpty());
	}
}

UClass* UCataclysmPassiveTreeWidget::LoadedChoiceButtonClass()
{
	if (ResolvedChoiceButtonClass)
	{
		return ResolvedChoiceButtonClass;
	}

	ResolvedChoiceButtonClass = ChoiceButtonClass.LoadSynchronous();
	if (!ResolvedChoiceButtonClass && !bComplainedAboutTheChoiceButtonClass)
	{
		// ONCE, NOT ON EVERY CLICK. The refresh runs whenever anything is
		// spent, and a log line per click would bury whatever else the log was
		// going to say.
		bComplainedAboutTheChoiceButtonClass = true;
		UE_LOG(LogCataclysm, Error,
			   TEXT("The passive tree screen draws nothing, because %s could "
					"not be loaded. Run  python tools/run_editor_python.py "
					"tools/generate_interface_assets.py  to build it."),
			   *ChoiceButtonClass.ToString());
	}

	return ResolvedChoiceButtonClass;
}

void UCataclysmPassiveTreeWidget::FillPanel(UPanelWidget* Panel,
											const TArray<FName>& Values,
											bool bTrees)
{
	if (!Panel)
	{
		return;
	}

	UClass* ButtonClass = LoadedChoiceButtonClass();
	if (!ButtonClass)
	{
		return;
	}

	// REBUILT ONLY WHEN THE LIST ITSELF CHANGED, which for the node panel is
	// whenever the tree being looked at changes. Emptying a panel from inside a
	// button's own click handler would tear down the Slate widget whose event is
	// still being dispatched, which is why the tree list -- whose buttons are the
	// ones that cause the node list to change -- is never rebuilt after the
	// first time.
	bool bNeedsBuilding = Panel->GetChildrenCount() != Values.Num();
	if (!bNeedsBuilding)
	{
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			const UCataclysmChoiceButton* Existing =
				Cast<UCataclysmChoiceButton>(Panel->GetChildAt(Index));
			if (!Existing || Existing->GetChoiceValue() != Values[Index])
			{
				bNeedsBuilding = true;
				break;
			}
		}
	}

	if (bNeedsBuilding)
	{
		Panel->ClearChildren();
		for (const FName& Value : Values)
		{
			UCataclysmChoiceButton* Button =
				CreateWidget<UCataclysmChoiceButton>(this, ButtonClass);
			if (!Button)
			{
				continue;
			}

			Button->SetChoice(Value, FText::FromName(Value), false, true);

			// TWO CALLS RATHER THAN ONE WITH A CHOSEN FUNCTION. `AddDynamic` is
			// a macro that stringifies the function's name to find it by
			// reflection, so the name has to be written out literally.
			if (bTrees)
			{
				Button->OnChoiceClicked.AddDynamic(
					this, &UCataclysmPassiveTreeWidget::HandleTreeClicked);
			}
			else
			{
				Button->OnChoiceClicked.AddDynamic(
					this, &UCataclysmPassiveTreeWidget::HandleNodeClicked);
			}

			Panel->AddChild(Button);
		}
	}

	const ACataclysmPlayerState* Player = State();
	const int32 PointsAvailable = Player ? Player->PassivePointsAvailable() : 0;
	static const FCataclysmPassiveAllocation Nothing;
	const FCataclysmPassiveAllocation& Allocation =
		Player ? Player->GetPassiveAllocation() : Nothing;
	for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
	{
		UCataclysmChoiceButton* Button =
			Cast<UCataclysmChoiceButton>(Panel->GetChildAt(Index));
		if (!Button)
		{
			continue;
		}

		const FName Value = Button->GetChoiceValue();

		if (bTrees)
		{
			// A TREE THE CHARACTER CANNOT REACH IS STILL CLICKABLE, and shown
			// dimmed. Reading what another tree offers is how a player decides
			// which weapon to carry, so hiding them would make that choice
			// blind. Only spending is refused.
			Button->SetChoice(Value, FText::FromName(Value),
							  ShownTree == Value.ToString(),
							  /*bAvailable=*/true);
			continue;
		}

		const FString Line = UCataclysmPassiveTree::DescribeNode(
			NodeTable(), EdgeTable(), Allocation, Value, PointsAvailable);

		// THE WHOLE LINE IS THE LABEL, refusal and all. A node that cannot take
		// a point still shows why, which is the information a player is looking
		// for when they click one.
		const bool bCanTake = UCataclysmPassiveTree::RefusalForSpending(
			NodeTable(), EdgeTable(), Allocation, Value,
			PointsAvailable).IsEmpty();

		Button->SetChoice(Value, FText::FromString(Line),
						  Allocation.PointsIn(Value) > 0, bCanTake);
	}
}

void UCataclysmPassiveTreeWidget::HandleTreeClicked(FName Tree)
{
	ShowTree(Tree.ToString());
}

void UCataclysmPassiveTreeWidget::HandleNodeClicked(FName Node)
{
	SpendInto(Node);
}
