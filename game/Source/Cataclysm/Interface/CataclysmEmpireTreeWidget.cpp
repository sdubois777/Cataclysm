// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmEmpireTreeWidget.h"
#include "Cataclysm.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Data/CataclysmDataRows.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Interface/CataclysmEmpireTreeLayout.h"
#include "Interface/CataclysmPassiveTreeLayout.h"
#include "Player/CataclysmEmpireTree.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerController.h"

const UDataTable* UCataclysmEmpireTreeWidget::NodeTable() const
{
	if (!Nodes)
	{
		// MUTABLE THROUGH A CONST FUNCTION, for the class tree screen's reason: loading the table is caching.
		const_cast<UCataclysmEmpireTreeWidget*>(this)->Nodes = UCataclysmEmpireTree::LoadNodeTable();
	}
	return Nodes;
}

const UDataTable* UCataclysmEmpireTreeWidget::EdgeTable() const
{
	if (!Edges)
	{
		const_cast<UCataclysmEmpireTreeWidget*>(this)->Edges = UCataclysmEmpireTree::LoadEdgeTable();
	}
	return Edges;
}

void UCataclysmEmpireTreeWidget::SetTables(const UDataTable* InNodeTable, const UDataTable* InEdgeTable)
{
	Nodes = InNodeTable;
	Edges = InEdgeTable;
}

void UCataclysmEmpireTreeWidget::SetWorldContextForTests(UObject* InContext)
{
	ContextForTests = InContext;
}

void UCataclysmEmpireTreeWidget::SetPanelSizeForTests(FVector2D Size)
{
	PanelSizeForTests = Size;
}

const UObject* UCataclysmEmpireTreeWidget::Context() const
{
	// THE TEST'S WORLD WHEN ONE WAS GIVEN, and this widget otherwise, whose world is the owning player's.
	return ContextForTests != nullptr ? ContextForTests.Get() : static_cast<const UObject*>(this);
}

bool UCataclysmEmpireTreeWidget::Held(FCataclysmHeldEmpireTree& OutHeld) const
{
	return UCataclysmGameInstance::EmpireTreeHeldBy(Context(), OutHeld);
}

const FCataclysmPassiveAllocation& UCataclysmEmpireTreeWidget::Allocation() const
{
	static const FCataclysmPassiveAllocation Nothing;
	FCataclysmHeldEmpireTree Tree;
	return Held(Tree) ? *Tree.Allocation : Nothing;
}

int32 UCataclysmEmpireTreeWidget::RespecDaysNow() const
{
	const UCataclysmEmpireRun* Run = UCataclysmGameInstance::EmpireRunFor(Context(), /*bStartIfNone*/ false);
	return Run != nullptr ? UCataclysmEmpireTree::RespecDays(Run->Surges != nullptr ? Run->Surges->LethalityRung : 0)
						  : 0;
}

bool UCataclysmEmpireTreeWidget::ShowBranch(const FString& Branch)
{
	if (!UCataclysmEmpireTreeLayout::Views().Contains(Branch))
	{
		return false;
	}

	ShownBranch = Branch;
	LastRefusal.Reset();
	LastTouched = NAME_None;
	ChoosingOptionFor = NAME_None;
	bChoosingExtraOption = false;
	bAwaitingTheSecondRespecPress = false;

	BuildGraph();
	FitToBranch();
	RefreshDisplay();
	return true;
}

TArray<FName> UCataclysmEmpireTreeWidget::ShownNodes() const
{
	return ShownBranch.IsEmpty() ? TArray<FName>() : UCataclysmEmpireTree::NodesIn(NodeTable(), ShownBranch);
}

bool UCataclysmEmpireTreeWidget::BuyPoint(FName Node)
{
	LastTouched = Node;
	bAwaitingTheSecondRespecPress = false;
	const bool bBought = UCataclysmGameInstance::BuyEmpireTreePoint(Context(), Node, LastRefusal);
	RefreshDisplay();
	return bBought;
}

bool UCataclysmEmpireTreeWidget::ChooseOption(FName Node, int32 Option)
{
	LastTouched = Node;
	bAwaitingTheSecondRespecPress = false;
	const bool bChosen = UCataclysmGameInstance::ChooseEmpireTreeOption(Context(), Node, Option, LastRefusal);
	RefreshDisplay();
	return bChosen;
}

bool UCataclysmEmpireTreeWidget::ChooseExtraOption(FName Node, int32 Option)
{
	LastTouched = Node;
	bAwaitingTheSecondRespecPress = false;
	const bool bTaken = UCataclysmGameInstance::ChooseEmpireTreeExtraOption(Context(), Node, Option, LastRefusal);
	RefreshDisplay();
	return bTaken;
}

bool UCataclysmEmpireTreeWidget::TouchNode(FName Node)
{
	bAwaitingTheSecondRespecPress = false;

	if (UCataclysmEmpireTree::AwaitsAnOptionChoice(NodeTable(), Allocation(), Node))
	{
		// DURING A WALK THE OPTIONS ARE NOT OFFERED: choosing one would be refused, so the click says why now, as a click
		// on any other node does through `BuyPoint`. The coordinating session's S2 of 2026-11-18.
		const FString WalkRefusal = UCataclysmGameInstance::EmpireTreeChangeRefusal(Context());
		if (!WalkRefusal.IsEmpty())
		{
			LastTouched = Node;
			ChoosingOptionFor = NAME_None;
			bChoosingExtraOption = false;
			LastRefusal = WalkRefusal;
			RefreshDisplay();
			return false;
		}

		// THE OPTIONS, NOT A POINT: a centre capstone takes no point until one is chosen, the class trees' issue #1064.
		LastTouched = Node;
		ChoosingOptionFor = Node;
		bChoosingExtraOption = false;
		const FCataclysmEmpireTreeNodeRow* Row = UCataclysmEmpireTree::FindNode(NodeTable(), Node);
		LastRefusal = FString::Printf(TEXT("%s has opened. Take one of its three options, in the list. The choice stands "
										   "until a respec."),
									  Row != nullptr ? *Row->NodeName : *Node.ToString());
		RefreshDisplay();
		return true;
	}

	// AN EARLIER MILESTONE WHILE IMPERIAL PROWESS'S EXTRA OPTION IS OPEN THERE: its options, offered as that extra one
	// (issue #2741 slice 8), and refused during a walk as a choice is.
	if (UCataclysmEmpireTree::OffersAnExtraOption(NodeTable(), UCataclysmEmpireTree::LoadEffectTable(), Allocation(),
												  Node))
	{
		LastTouched = Node;
		const FString WalkRefusal = UCataclysmGameInstance::EmpireTreeChangeRefusal(Context());
		if (!WalkRefusal.IsEmpty())
		{
			ChoosingOptionFor = NAME_None;
			bChoosingExtraOption = false;
			LastRefusal = WalkRefusal;
			RefreshDisplay();
			return false;
		}
		ChoosingOptionFor = Node;
		bChoosingExtraOption = true;
		const FCataclysmEmpireTreeNodeRow* Row = UCataclysmEmpireTree::FindNode(NodeTable(), Node);
		LastRefusal = FString::Printf(TEXT("%s lets %s take a second option. Take one in the list. It stands until a "
										   "respec."),
									  *UCataclysmEmpireTree::ExtraOptionGranterName(
										  NodeTable(), UCataclysmEmpireTree::LoadEffectTable()),
									  Row != nullptr ? *Row->NodeName : *Node.ToString());
		RefreshDisplay();
		return true;
	}

	// ANY OTHER NODE PUTS THE VIEWS BACK, which is also the way out of the options without deciding. Safe to rebuild the
	// list from here: this click came from a button on the graph, not from one of the list's own children.
	ChoosingOptionFor = NAME_None;
	bChoosingExtraOption = false;
	return BuyPoint(Node);
}

bool UCataclysmEmpireTreeWidget::PressRespec()
{
	if (!bAwaitingTheSecondRespecPress)
	{
		// A RESPEC THE RULES WOULD REFUSE SAYS SO NOW, and asks for no second press: during a walk, with nothing spent or
		// chosen, with no run to take the days from, or with no tree.
		FCataclysmHeldEmpireTree Tree;
		FString Refusal;
		if (!Held(Tree))
		{
			Refusal = Tree.Why;
		}
		else
		{
			Refusal = UCataclysmGameInstance::EmpireTreeChangeRefusal(Context());
			if (Refusal.IsEmpty())
			{
				Refusal = UCataclysmEmpireTree::RefusalForRespec(
					*Tree.Allocation, UCataclysmGameInstance::EmpireRunFor(Context(), /*bStartIfNone*/ false) != nullptr);
			}
		}

		LastRefusal = Refusal;
		bAwaitingTheSecondRespecPress = Refusal.IsEmpty();
		RefreshDisplay();
		return false;
	}

	bAwaitingTheSecondRespecPress = false;
	ChoosingOptionFor = NAME_None;
	bChoosingExtraOption = false;
	int32 Returned = 0;
	int32 Days = 0;
	const bool bRespecced = UCataclysmGameInstance::RespecEmpireTree(Context(), Returned, Days, LastRefusal);
	RefreshDisplay();
	return bRespecced;
}

FText UCataclysmEmpireTreeWidget::BankText() const
{
	FCataclysmHeldEmpireTree Tree;
	if (!Held(Tree))
	{
		return FText::FromString(Tree.Why);
	}
	return FText::FromString(UCataclysmEmpireTreeLayout::BankLine(*Tree.Bank, Tree.bTheCharactersOwn));
}

FText UCataclysmEmpireTreeWidget::BranchText() const
{
	if (ShownBranch.IsEmpty())
	{
		return FText::GetEmpty();
	}
	const FCataclysmPassiveAllocation& Spent = Allocation();
	return FText::FromString(UCataclysmEmpireTreeLayout::BranchLine(
		ShownBranch, UCataclysmEmpireTree::SpentInBranch(NodeTable(), Spent, ShownBranch),
		UCataclysmEmpireTree::SpentInTree(Spent)));
}

FText UCataclysmEmpireTreeWidget::RefusalText() const
{
	// DURING A WALK THE SCREEN SAYS SO WHEN NOTHING ELSE WAS REFUSED, the coordinating session's S2 of 2026-11-18.
	return FText::FromString(LastRefusal.IsEmpty() ? UCataclysmGameInstance::EmpireTreeChangeRefusal(Context())
												   : LastRefusal);
}

FText UCataclysmEmpireTreeWidget::RespecText() const
{
	return FText::FromString(UCataclysmEmpireTreeLayout::RespecLabel(RespecDaysNow(), bAwaitingTheSecondRespecPress));
}

FText UCataclysmEmpireTreeWidget::DescriptionText() const
{
	const FName Node = ChoosingOptionFor.IsNone() ? LastTouched : ChoosingOptionFor;
	const FCataclysmEmpireTreeNodeRow* Row = UCataclysmEmpireTree::FindNode(NodeTable(), Node);
	if (Row == nullptr)
	{
		return FText::GetEmpty();
	}

	FCataclysmHeldEmpireTree Tree;
	const bool bHeld = Held(Tree);
	const FCataclysmPassiveAllocation& Spent = Allocation();
	const FString Refusal = bHeld ? UCataclysmEmpireTree::RefusalForSpending(NodeTable(), EdgeTable(), Spent, Node,
																			 *Tree.Bank)
								  : FString();
	return FText::FromString(UCataclysmEmpireTreeLayout::NodeDetail(
		*Row, Spent.PointsIn(Node), Spent.ChosenOptionIn(Node), Refusal, Spent.ExtraOptionIn(Node),
		UCataclysmEmpireTree::ExtraOptionGranterName(NodeTable(), UCataclysmEmpireTree::LoadEffectTable())));
}

void UCataclysmEmpireTreeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (TitleLabel)
	{
		TitleLabel->SetText(Title);
	}

	if (RespecButton)
	{
		RespecButton->OnClicked.AddUniqueDynamic(this, &UCataclysmEmpireTreeWidget::HandleRespecClicked);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &UCataclysmEmpireTreeWidget::HandleCloseClicked);
	}

	// THE FIRST VIEW, the Architect branch, when none was chosen: the file's own first branch.
	if (ShownBranch.IsEmpty())
	{
		const TArray<FString> Views = UCataclysmEmpireTreeLayout::Views();
		if (Views.Num() > 0)
		{
			ShownBranch = Views[0];
		}
	}

	RefreshDisplay();
	FitToBranch();
}

void UCataclysmEmpireTreeWidget::RefreshDisplay()
{
	FillList();

	// THE GRAPH IS BUILT ONLY WHEN THE VIEW ON IT CHANGED, for the class tree screen's reason: a refresh happens on
	// every click, and rebuilding then would destroy the button whose click is still being dispatched.
	if (BuiltBranch != ShownBranch)
	{
		BuildGraph();
		FitToBranch();
	}
	PlaceGraph();

	if (BankLabel)
	{
		BankLabel->SetText(BankText());
	}
	if (BranchLabel)
	{
		BranchLabel->SetText(BranchText());
	}
	if (RefusalLabel)
	{
		RefusalLabel->SetText(RefusalText());
	}
	if (DescriptionLabel)
	{
		DescriptionLabel->SetText(DescriptionText());
	}
	if (RespecLabel)
	{
		RespecLabel->SetText(RespecText());
	}
}

UClass* UCataclysmEmpireTreeWidget::LoadedChoiceButtonClass()
{
	if (ResolvedChoiceButtonClass)
	{
		return ResolvedChoiceButtonClass;
	}

	ResolvedChoiceButtonClass = ChoiceButtonClass.LoadSynchronous();
	if (!ResolvedChoiceButtonClass && !bComplainedAboutTheChoiceButtonClass)
	{
		bComplainedAboutTheChoiceButtonClass = true;
		UE_LOG(LogCataclysm, Error,
			   TEXT("The empire upgrade tree screen draws nothing, because %s could not be loaded. Run  python "
					"tools/run_editor_python.py tools/generate_interface_assets.py  to build it."),
			   *ChoiceButtonClass.ToString());
	}
	return ResolvedChoiceButtonClass;
}

void UCataclysmEmpireTreeWidget::FillList()
{
	if (!BranchBox)
	{
		return;
	}
	UClass* ButtonClass = LoadedChoiceButtonClass();
	if (!ButtonClass)
	{
		return;
	}

	TArray<FName> Values;
	if (ChoosingOptionFor.IsNone())
	{
		for (const FString& View : UCataclysmEmpireTreeLayout::Views())
		{
			Values.Add(FName(*View));
		}
	}
	else
	{
		for (int32 Option = 1; Option <= UCataclysmEmpireTree::CapstoneOptions; ++Option)
		{
			Values.Add(OptionValue(Option));
		}
	}

	// REBUILT ONLY WHEN THE VALUES CHANGED, the class tree screen's rule: emptying the list from inside one of its own
	// children's click would tear down the button whose event is still being dispatched.
	bool bNeedsBuilding = BranchBox->GetChildrenCount() != Values.Num();
	for (int32 Index = 0; !bNeedsBuilding && Index < Values.Num(); ++Index)
	{
		const UCataclysmChoiceButton* Existing = Cast<UCataclysmChoiceButton>(BranchBox->GetChildAt(Index));
		bNeedsBuilding = !Existing || Existing->GetChoiceValue() != Values[Index];
	}
	if (bNeedsBuilding)
	{
		BranchBox->ClearChildren();
		for (const FName& Value : Values)
		{
			if (UCataclysmChoiceButton* Button = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
			{
				Button->SetChoice(Value, FText::FromName(Value), false, true);
				Button->OnChoiceClicked.AddDynamic(this, &UCataclysmEmpireTreeWidget::HandleBranchClicked);
				BranchBox->AddChild(Button);
			}
		}
	}

	const FCataclysmEmpireTreeNodeRow* Capstone = UCataclysmEmpireTree::FindNode(NodeTable(), ChoosingOptionFor);
	const TArray<FString> Names = Capstone != nullptr ? UCataclysmEmpireTree::OptionNamesOf(*Capstone) : TArray<FString>();
	const int32 Chosen = Allocation().ChosenOptionIn(ChoosingOptionFor);
	const int32 Extra = Allocation().ExtraOptionIn(ChoosingOptionFor);
	const FString Granter = Extra > 0
		? UCataclysmEmpireTree::ExtraOptionGranterName(NodeTable(), UCataclysmEmpireTree::LoadEffectTable())
		: FString();
	for (int32 Index = 0; Index < BranchBox->GetChildrenCount(); ++Index)
	{
		UCataclysmChoiceButton* Button = Cast<UCataclysmChoiceButton>(BranchBox->GetChildAt(Index));
		if (!Button)
		{
			continue;
		}
		const FName Value = Button->GetChoiceValue();
		const int32 Option = OptionFromValue(Value);
		if (Option > 0)
		{
			const FString Name = Names.IsValidIndex(Option - 1) ? Names[Option - 1] : FString();
			const FString WaitsOn = UCataclysmEmpireTree::OptionWaitsOn(NodeTable(), ChoosingOptionFor, Option);
			Button->SetChoice(Value,
							  FText::FromString(UCataclysmEmpireTreeLayout::OptionLabel(
								  Option, Name, Chosen == Option, WaitsOn, Extra == Option ? Granter : FString())),
							  Chosen == Option || Extra == Option, !Name.IsEmpty());
			Button->SetDimmed(!WaitsOn.IsEmpty());
			continue;
		}
		Button->SetChoice(Value, FText::FromString(UCataclysmEmpireTreeLayout::ViewLabel(Value.ToString())),
						  ShownBranch == Value.ToString(), true);
		Button->SetDimmed(false);
	}
}

FName UCataclysmEmpireTreeWidget::OptionValue(int32 Option)
{
	// A NAME NO VIEW CAN HAVE: a branch's name never begins with a space. `UCataclysmPassiveTreeWidget`'s scheme.
	return FName(*FString::Printf(TEXT(" Option%d"), Option));
}

int32 UCataclysmEmpireTreeWidget::OptionFromValue(FName Value)
{
	const FString Text = Value.ToString();
	if (!Text.StartsWith(TEXT(" Option")))
	{
		return 0;
	}
	return FCString::Atoi(*Text.RightChop(FString(TEXT(" Option")).Len()));
}

void UCataclysmEmpireTreeWidget::HandleBranchClicked(FName Value)
{
	const int32 Option = OptionFromValue(Value);
	if (Option > 0)
	{
		if (!ChoosingOptionFor.IsNone())
		{
			// THE LIST STAYS: its three values do not change, so it is relabelled in place, not emptied under the click.
			// Imperial Prowess's extra option goes through its own call (issue #2741 slice 8).
			if (bChoosingExtraOption)
			{
				ChooseExtraOption(ChoosingOptionFor, Option);
			}
			else
			{
				ChooseOption(ChoosingOptionFor, Option);
			}
		}
		return;
	}
	ShowBranch(Value.ToString());
}

void UCataclysmEmpireTreeWidget::HandleNodeClicked(FName Node)
{
	TouchNode(Node);
}

void UCataclysmEmpireTreeWidget::HandleNodeHovered(FName Node)
{
	// READING A NODE COSTS NOTHING, and not while a capstone's options are being read.
	if (!ChoosingOptionFor.IsNone())
	{
		return;
	}
	LastTouched = Node;
	if (DescriptionLabel)
	{
		DescriptionLabel->SetText(DescriptionText());
	}
}

void UCataclysmEmpireTreeWidget::HandleRespecClicked()
{
	PressRespec();
}

void UCataclysmEmpireTreeWidget::HandleCloseClicked()
{
	if (ACataclysmPlayerController* Controller = Cast<ACataclysmPlayerController>(GetOwningPlayer()))
	{
		Controller->ToggleEmpireTree();
	}
}

void UCataclysmEmpireTreeWidget::DescribeNodeButton(UCataclysmChoiceButton& Button, FName Node)
{
	const FCataclysmEmpireTreeNodeRow* Row = UCataclysmEmpireTree::FindNode(NodeTable(), Node);
	if (!Row)
	{
		return;
	}

	FCataclysmHeldEmpireTree Tree;
	const bool bHeld = Held(Tree);
	const FCataclysmPassiveAllocation& Spent = Allocation();
	const int32 Points = Spent.PointsIn(Node);

	// DIMMED WHEN IT CANNOT TAKE A POINT, AND STILL CLICKABLE, so a click says why -- the way the class tree screen
	// dims a class not chosen (`UCataclysmChoiceButton::SetDimmed`). A centre capstone awaiting its choice is not
	// dimmed: a click on it offers the options.
	const FString Refusal = bHeld ? UCataclysmEmpireTree::RefusalForSpending(NodeTable(), EdgeTable(), Spent, Node,
																			 *Tree.Bank)
								  : Tree.Why;
	// AND AN EARLIER MILESTONE WHERE IMPERIAL PROWESS'S EXTRA OPTION IS OPEN, which a click offers the same way (issue
	// #2741 slice 8).
	const bool bAwaitsChoice = UCataclysmEmpireTree::AwaitsAnOptionChoice(NodeTable(), Spent, Node)
		|| UCataclysmEmpireTree::OffersAnExtraOption(NodeTable(), UCataclysmEmpireTree::LoadEffectTable(), Spent, Node);

	Button.SetChoice(Node, FText::FromString(UCataclysmEmpireTreeLayout::NodeLabel(*Row, Points)), Points > 0, true);
	Button.SetDimmed(!Refusal.IsEmpty() && !bAwaitsChoice);
	Button.SetToolTipText(FText::FromString(bAwaitsChoice ? FString() : Refusal));
}

void UCataclysmEmpireTreeWidget::DescribeNodeButtonForTests(UCataclysmChoiceButton& Button, FName Node)
{
	DescribeNodeButton(Button, Node);
}

FVector2D UCataclysmEmpireTreeWidget::CanvasSize() const
{
	if (!PanelSizeForTests.IsNearlyZero())
	{
		return PanelSizeForTests;
	}
	if (GraphCanvas)
	{
		const FVector2D Measured = GraphCanvas->GetCachedGeometry().GetLocalSize();
		if (Measured.X > 1.0 && Measured.Y > 1.0)
		{
			return Measured;
		}
	}
	// A COMMON WINDOW until Slate has laid the panel out, the class tree screen's guess.
	return FVector2D(1600.0, 800.0);
}

void UCataclysmEmpireTreeWidget::FitToBranch()
{
	const FCataclysmTreeExtent Extent = UCataclysmEmpireTreeLayout::ExtentOf(NodeTable(), ShownBranch);
	if (!Extent.bAny)
	{
		return;
	}
	const FVector2D Size = CanvasSize();
	Focus = Extent.Centre();
	Zoom = UCataclysmPassiveTreeLayout::ZoomToFit(Extent, Size);
	FittedAgainst = Size;
	PlaceGraph();
}

void UCataclysmEmpireTreeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// FITTED AGAIN WHEN THE PANEL CHANGED SIZE, the class tree screen's issue #1078: the panel gives up the height the
	// labels under it take, and a zoom fitted to an older size hides nodes behind the panel's edge.
	const FVector2D Size = CanvasSize();
	if (!Size.Equals(FittedAgainst, 1.0))
	{
		FittedAgainst = Size;
		FitToBranch();
	}
}

void UCataclysmEmpireTreeWidget::ZoomBy(float Notches)
{
	Zoom = UCataclysmPassiveTreeLayout::ZoomAfterNotches(Zoom, Notches);
	PlaceGraph();
}

void UCataclysmEmpireTreeWidget::PanBy(FVector2D Pixels)
{
	Focus -= Pixels / UCataclysmPassiveTreeLayout::ClampZoom(Zoom);
	PlaceGraph();
}

void UCataclysmEmpireTreeWidget::BuildGraph()
{
	BuiltBranch = ShownBranch;
	BuiltNodeScale = UCataclysmEmpireTreeLayout::NodeScaleFor(NodeTable(), ShownBranch);
	NodeButtons.Reset();
	NodeButtonNames.Reset();
	EdgeImages.Reset();
	EdgeEnds.Reset();

	if (!GraphCanvas)
	{
		return;
	}
	GraphCanvas->ClearChildren();
	UClass* ButtonClass = LoadedChoiceButtonClass();
	if (!ButtonClass || ShownBranch.IsEmpty())
	{
		return;
	}

	// THE EDGES FIRST, SO THE NODES ARE DRAWN OVER THEM: a canvas draws its children in the order they were added.
	for (const TPair<FName, FName>& Edge : UCataclysmEmpireTreeLayout::EdgesIn(NodeTable(), EdgeTable(), ShownBranch))
	{
		UImage* Line = NewObject<UImage>(this);
		if (!Line)
		{
			continue;
		}
		Line->SetColorAndOpacity(EdgeColour);
		if (UCanvasPanelSlot* Placement = Cast<UCanvasPanelSlot>(GraphCanvas->AddChild(Line)))
		{
			Placement->SetAnchors(FAnchors(0.0f, 0.0f));
			Placement->SetAlignment(UCataclysmPassiveTreeLayout::EdgePivot());
			Placement->SetAutoSize(false);
		}
		Line->SetRenderTransformPivot(UCataclysmPassiveTreeLayout::EdgePivot());
		EdgeImages.Add(Line);
		EdgeEnds.Add(Edge);
	}

	for (const FName& Node : ShownNodes())
	{
		UCataclysmChoiceButton* Button = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass);
		if (!Button)
		{
			continue;
		}
		Button->OnChoiceClicked.AddDynamic(this, &UCataclysmEmpireTreeWidget::HandleNodeClicked);
		Button->OnChoiceHovered.AddDynamic(this, &UCataclysmEmpireTreeWidget::HandleNodeHovered);
		// A NAME TOO LONG FOR ITS NODE IS CUT, not drawn over the next; the whole of it is in the line under the view.
		Button->SetClipping(EWidgetClipping::ClipToBounds);
		if (UCanvasPanelSlot* Placement = Cast<UCanvasPanelSlot>(GraphCanvas->AddChild(Button)))
		{
			Placement->SetAnchors(FAnchors(0.0f, 0.0f));
			Placement->SetAlignment(FVector2D(0.5, 0.5));
			Placement->SetAutoSize(false);
		}
		NodeButtons.Add(Button);
		NodeButtonNames.Add(Node);
	}
}

void UCataclysmEmpireTreeWidget::PlaceGraph()
{
	if (!GraphCanvas)
	{
		return;
	}

	const FVector2D Size = CanvasSize();
	TMap<FName, FVector2D> Placed;
	Placed.Reserve(NodeButtonNames.Num());

	// THE WORDS SHRINK WITH THE BOX, up to the Blueprint's own size (`UCataclysmChoiceButton::SetLabelScale`), since a
	// node of 57 pixels at a fitted zoom cannot hold text of the size a list's button has.
	const float Scale = FMath::Min(1.0f, BuiltNodeScale * Zoom);

	for (int32 Index = 0; Index < NodeButtons.Num(); ++Index)
	{
		UCataclysmChoiceButton* Button = NodeButtons[Index];
		const FName Node = NodeButtonNames[Index];
		const FCataclysmEmpireTreeNodeRow* Row = UCataclysmEmpireTree::FindNode(NodeTable(), Node);
		if (!Button || !Row)
		{
			continue;
		}
		const FVector2D At = UCataclysmPassiveTreeLayout::ScreenPositionFor(FVector2D(Row->PositionX, Row->PositionY),
																			 Focus, Zoom, Size);
		Placed.Add(Node, At);
		if (UCanvasPanelSlot* Placement = Cast<UCanvasPanelSlot>(Button->Slot))
		{
			Placement->SetPosition(At);
			Placement->SetSize(FVector2D(UCataclysmPassiveTreeLayout::NodeWidthPx * BuiltNodeScale * Zoom,
										 UCataclysmPassiveTreeLayout::NodeHeightPx * BuiltNodeScale * Zoom));
		}
		Button->SetLabelScale(Scale);
		DescribeNodeButton(*Button, Node);
	}

	for (int32 Index = 0; Index < EdgeImages.Num(); ++Index)
	{
		UImage* Line = EdgeImages[Index];
		if (!Line || !EdgeEnds.IsValidIndex(Index))
		{
			continue;
		}
		const FVector2D* From = Placed.Find(EdgeEnds[Index].Key);
		const FVector2D* To = Placed.Find(EdgeEnds[Index].Value);
		if (!From || !To)
		{
			Line->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		Line->SetVisibility(ESlateVisibility::HitTestInvisible);
		FVector2D At;
		FVector2D Extent;
		float Angle = 0.0f;
		UCataclysmPassiveTreeLayout::EdgeGeometry(*From, *To, UCataclysmPassiveTreeLayout::EdgeThicknessPx * Zoom, At,
												  Extent, Angle);
		if (UCanvasPanelSlot* Placement = Cast<UCanvasPanelSlot>(Line->Slot))
		{
			Placement->SetPosition(At);
			Placement->SetSize(Extent);
		}
		Line->SetRenderTransformAngle(Angle);
	}
}

FReply UCataclysmEmpireTreeWidget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	ZoomBy(Event.GetWheelDelta());
	return FReply::Handled();
}

FReply UCataclysmEmpireTreeWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	// THE RIGHT BUTTON PANS; the left one buys, the class tree screen's rule.
	if (Event.GetEffectingButton() != EKeys::RightMouseButton)
	{
		return FReply::Unhandled();
	}
	bDragging = true;
	DragFrom = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UCataclysmEmpireTreeWidget::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!bDragging)
	{
		return FReply::Unhandled();
	}
	const FVector2D Now = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	PanBy(Now - DragFrom);
	DragFrom = Now;
	return FReply::Handled();
}

FReply UCataclysmEmpireTreeWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!bDragging || Event.GetEffectingButton() != EKeys::RightMouseButton)
	{
		return FReply::Unhandled();
	}
	bDragging = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply UCataclysmEmpireTreeWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	// F FITS THE VIEW AGAIN, the way back from a limb panned into.
	if (Event.GetKey() == EKeys::F)
	{
		FitToBranch();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, Event);
}
