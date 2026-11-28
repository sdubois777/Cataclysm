// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmPassiveTreeWidget.h"
#include "Cataclysm.h"
#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmPassiveTree.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Interface/CataclysmPassiveTreeLayout.h"
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

void UCataclysmPassiveTreeWidget::SetPanelSizeForTests(FVector2D Size)
{
	PanelSizeForTests = Size;
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

	// A DIFFERENT TREE IS A DIFFERENT SET OF WIDGETS AND A DIFFERENT VIEW. The
	// trees are laid out on quite different parts of the authoring tool's
	// canvas -- the Masochist tree runs from x -2000 to 1600 and the Berserker
	// one from -909 to 712 -- so keeping the old focus would open the new tree
	// looking at empty space.
	BuildGraph();
	FitToTree();

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

bool UCataclysmPassiveTreeWidget::TouchNode(FName Node)
{
	const ACataclysmPlayerState* Player = State();
	static const FCataclysmPassiveAllocation Nothing;
	const FCataclysmPassiveAllocation& Allocation =
		Player ? Player->GetPassiveAllocation() : Nothing;

	if (UCataclysmPassiveTree::AwaitsAnOptionChoice(NodeTable(), Allocation,
													Node))
	{
		// THE OPTIONS, NOT A SPEND. See the header: a capstone in this state
		// cannot take a point until one is taken, so spending here would refuse
		// every time, which is what it did before issue #1064.
		LastTouched = Node;
		ChoosingOptionFor = Node;

		const FCataclysmPassiveNodeRow* Row =
			UCataclysmPassiveTree::FindNode(NodeTable(), Node);
		LastRefusal = FString::Printf(
			TEXT("%s has opened. Take one of its three options, on the left. "
				 "The choice is permanent."),
			Row ? *Row->NodeName : *Node.ToString());

		RefreshDisplay();
		return true;
	}

	// ANY OTHER NODE PUTS THE TREE NAMES BACK, which is also the way out of the
	// option list without deciding. It is safe to rebuild that list from here
	// because this click came from a button on the graph rather than from one of
	// the list's own children; see `ChoosingOptionFor` in the header.
	ChoosingOptionFor = NAME_None;
	return SpendInto(Node);
}

FText UCataclysmPassiveTreeWidget::PointsText() const
{
	const ACataclysmPlayerState* Player = State();
	if (!Player)
	{
		return FText::GetEmpty();
	}

	// THE WORDS ARE `UCataclysmPassiveTree::DescribePoints`, since 2026-10-09,
	// so that a test with no screen reads the line a player does. It prints the
	// points unspent, the points earned and the budget as this function did,
	// and adds how many spent points add nothing, and why, when more are spent
	// than earned.
	return FText::FromString(UCataclysmPassiveTree::DescribePoints(
		Player->GetPassiveAllocation(), Player->PassivePointsAvailable()));
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

	// THE CLASS THE CHARACTER CHOSE, OR ELSE THE FIRST TREE IT CAN ACTUALLY REACH, rather than the first
	// alphabetically. A Demonic character opening this screen wants the Masochist tree, not the Berserker one they
	// cannot spend in; and since 2026-11-26 a Soul Collector wants the Soul Collector tree, not the Necromancer one
	// sorted before it, whose starting node is also "Fervour" and read 0 of 1 (the owner's play-test).
	// `UCataclysmPassiveTree::TreeToOpenOn`.
	if (ShownTree.IsEmpty())
	{
		static const FCataclysmPassiveAllocation Nothing;
		const ACataclysmPlayerState* Player = State();
		ShownTree = UCataclysmPassiveTree::TreeToOpenOn(NodeTable(), Player ? Player->GetPassiveAllocation() : Nothing,
														Player ? Player->CarriedDamageTypes() : TArray<FName>());
	}

	RefreshDisplay();

	// FITTED AFTER THE FIRST REFRESH, NOT BEFORE. `RefreshDisplay` is what
	// builds the graph, and there is nothing to fit a view to until it has.
	FitToTree();
}

void UCataclysmPassiveTreeWidget::RefreshDisplay()
{
	// THE TREE LIST HOLDS A CAPSTONE'S THREE OPTIONS INSTEAD, WHILE ONE IS BEING
	// DECIDED. Issue #1064. It is the only other list of buttons on this screen,
	// and building a third for a decision made four times a tree would be more
	// machinery than the decision needs. A click on any node puts the tree names
	// back; see `ChoosingOptionFor` in the header for why the rebuild is safe in
	// that direction and not the other.
	TArray<FName> TreeValues;
	if (ChoosingOptionFor.IsNone())
	{
		for (const FString& Tree : AllTrees())
		{
			TreeValues.Add(FName(*Tree));
		}
	}
	else
	{
		for (int32 Option = 1; Option <= UCataclysmPassiveTree::CapstoneOptions;
			 ++Option)
		{
			TreeValues.Add(OptionValue(Option));
		}
	}
	FillPanel(TreeBox, TreeValues, /*bTrees=*/true);

	// THE GRAPH IS BUILT ONLY WHEN THE TREE ON IT CHANGED. Making 74 buttons and
	// 69 lines is work for a click; a refresh happens on every spend as well,
	// and rebuilding then would also destroy the button whose own click is still
	// being dispatched.
	if (BuiltTree != ShownTree)
	{
		BuildGraph();
		FitToTree();
	}
	PlaceGraph();

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
		// player reads a node's own words. Until 2026-11-25 it showed them
		// whether or not the node had an authored effect behind it, so a
		// player could not tell which sentences the character receives (issues
		// #936 and #939). Since the War vow options' second slice a node or an
		// option with no row is marked "(not built yet: it does nothing)":
		// the effect table is handed to both description functions below.
		//
		// AND FOR A CAPSTONE, THE NAMES OF ITS THREE OPTIONS. Issue #1076. This
		// read `Row->Description` until 2026-08-28, and a capstone's own
		// description is "Unlocks at N points spent. Choose one. The choice is
		// permanent", so the screen asked for a permanent decision between three
		// things it never named.
		//
		// THE NAMES AND NOT THE SENTENCES, EXCEPT WHILE ONE IS BEING DECIDED.
		// Issue #1078. This label is a sibling of the panel the tree is drawn
		// on, in a vertical box where that panel is the only child set to Fill,
		// so every line put here is a line of height taken off the tree. Seven
		// lines of option text shrank the tree to half the screen. While the
		// three options are actually on offer that is the right trade -- the
		// player is reading them, not clicking nodes -- and at every other
		// moment it is not.
		const bool bDeciding = !ChoosingOptionFor.IsNone();
		DescriptionLabel->SetText(FText::FromString(
			bDeciding
				? UCataclysmPassiveTree::FullDescriptionOf(NodeTable(),
														   ChoosingOptionFor,
														   UCataclysmPassiveTree::LoadEffectTable())
				: UCataclysmPassiveTree::ShortDescriptionOf(NodeTable(),
															LastTouched,
															UCataclysmPassiveTree::LoadEffectTable())));
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
			// A CAPSTONE'S THREE OPTIONS ARE IN THIS LIST WHILE ONE IS BEING
			// DECIDED, so its own names go on the buttons rather than the value
			// that carries them. Issue #1064.
			const int32 Option = OptionFromValue(Value);
			if (Option > 0)
			{
				const FCataclysmPassiveNodeRow* Capstone =
					UCataclysmPassiveTree::FindNode(NodeTable(),
													ChoosingOptionFor);
				const TArray<FString> Names = Capstone
					? UCataclysmPassiveTree::OptionNamesOf(*Capstone)
					: TArray<FString>();
				const FString Name = Names.IsValidIndex(Option - 1)
					? Names[Option - 1] : FString();

				// THE LABEL, MARKED WHEN THE OPTION HAS NO ROW. See `OptionLabelFor`.
				const FString Label = OptionLabelFor(
					NodeTable(), UCataclysmPassiveTree::LoadEffectTable(),
					ChoosingOptionFor, Option);

				// TAKEN IS MARKED, AND EVERY OPTION STAYS CLICKABLE. A player
				// who clicks one of the other two gets the refusal saying the
				// choice was permanent, which is the answer they are asking for.
				const int32 Chosen =
					Allocation.ChosenOptionIn(ChoosingOptionFor);
				Button->SetChoice(Value, FText::FromString(Label),
								  Chosen == Option, !Name.IsEmpty());
				Button->SetDimmed(false);
				Button->SetToolTipText(FText::GetEmpty());
				continue;
			}

			DescribeTreeButton(*Button, Value);
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

FString UCataclysmPassiveTreeWidget::OptionLabelFor(const UDataTable* NodeTable, const UDataTable* EffectTable,
													FName Capstone, int32 Option)
{
	const FCataclysmPassiveNodeRow* Row = UCataclysmPassiveTree::FindNode(NodeTable, Capstone);
	const TArray<FString> Names = Row ? UCataclysmPassiveTree::OptionNamesOf(*Row) : TArray<FString>();
	const FString Name = Names.IsValidIndex(Option - 1) ? Names[Option - 1] : FString();

	// A NAMELESS OPTION SAYS SO RATHER THAN SHOWING A BLANK BUTTON. This list never opens on a capstone naming no
	// option at all (the Saboteur's four until 2026-11-25, issue #935) -- `AwaitsAnOptionChoice` refuses a capstone
	// offering nothing -- so this is for a capstone that names one option and not another.
	if (Name.IsEmpty())
	{
		return FString::Printf(TEXT("%d  (nothing is written here)"), Option);
	}

	// AND A NAMED OPTION WITH NO ROW SAYS IT DOES NOTHING, before the permanent choice is made. The War vow options'
	// second slice, 2026-11-25. No table, nothing marked.
	const FString Label = FString::Printf(TEXT("%d  %s"), Option, *Name);
	return EffectTable && !UCataclysmPassiveTree::HasEffectRows(EffectTable, Capstone, Option)
		? UCataclysmPassiveTree::MarkedNotBuiltYet(Label) : Label;
}

FName UCataclysmPassiveTreeWidget::OptionValue(int32 Option)
{
	// A NAME NO TREE CAN HAVE, because the same list and the same click handler
	// carry both. `TreeNames` comes from the node table's `Tree` column, which
	// holds class names -- Masochist, Bulwark -- so a leading space is enough to
	// keep the two apart and cannot be produced by a tree.
	return FName(*FString::Printf(TEXT(" Option%d"), Option));
}

int32 UCataclysmPassiveTreeWidget::OptionFromValue(FName Value)
{
	const FString Text = Value.ToString();
	if (!Text.StartsWith(TEXT(" Option")))
	{
		return 0;
	}

	// Atoi answers zero for anything that is not a number, and zero is refused
	// by the caller, so a malformed value behaves as no option at all.
	return FCString::Atoi(*Text.RightChop(FString(TEXT(" Option")).Len()));
}

void UCataclysmPassiveTreeWidget::HandleTreeClicked(FName Tree)
{
	// THE SAME LIST CARRIES A CAPSTONE'S OPTIONS WHILE ONE IS BEING DECIDED, so
	// this handler has two jobs. Issue #1064.
	const int32 Option = OptionFromValue(Tree);
	if (Option > 0)
	{
		if (!ChoosingOptionFor.IsNone())
		{
			// THE LIST IS LEFT STANDING WHETHER THE CHOICE TOOK OR NOT. Its
			// three values do not change, so `FillPanel` relabels the buttons in
			// place rather than emptying the panel this click came out of. What
			// changes is that the taken option is now marked, and the other two
			// refuse with "the choice is permanent".
			ChooseOption(ChoosingOptionFor, Option);
		}
		return;
	}

	ChoosingOptionFor = NAME_None;
	ShowTree(Tree.ToString());
}

void UCataclysmPassiveTreeWidget::HandleNodeClicked(FName Node)
{
	// ONE LINE, AND WHAT IT CALLS IS PUBLIC ON PURPOSE. Issue #1064. This
	// function is bound to a button by reflection and no headless test can press
	// it, so the decision it stands for lives in `TouchNode` where a test can
	// reach it. Until that issue this line read `SpendInto(Node)`, and that is
	// the whole of why no capstone could be taken from this screen.
	TouchNode(Node);
}

void UCataclysmPassiveTreeWidget::HandleNodeHovered(FName Node)
{
	// ONLY THE DESCRIPTION MOVES. Hovering is not an act: it does not spend, it
	// does not clear the refusal from the last thing that was tried, and it does
	// not rebuild anything. Refreshing the whole screen on every mouse move
	// across 74 nodes would also be a great deal of work for a cursor passing
	// over one.
	LastTouched = Node;

	// THE NAME BESIDE THE MARK FOLLOWS THE CURSOR. Since 2026-11-24 the mark holds no name, so this is how a node is
	// read before it is clicked. It moves while a capstone's options are on offer too: it is one short line beside
	// the graph, and the description label below, which is what the player is reading then, is left alone.
	PlaceNodeName();

	// AND NOT WHILE A CAPSTONE'S THREE OPTIONS ARE ON OFFER. Issue #1078. The
	// player is part way through a permanent decision and the label is showing
	// what they are deciding between; replacing it because the cursor passed
	// over a node would take that away at the moment it is being read.
	if (!ChoosingOptionFor.IsNone())
	{
		return;
	}

	if (DescriptionLabel)
	{
		// THE SAME TEXT `RefreshDisplay` SHOWS WHEN NOTHING IS BEING DECIDED,
		// through the same function, so a node hovered and a node clicked read
		// alike. Issue #1076.
		DescriptionLabel->SetText(FText::FromString(
			UCataclysmPassiveTree::ShortDescriptionOf(NodeTable(), Node,
													  UCataclysmPassiveTree::LoadEffectTable())));
	}
}

FVector2D UCataclysmPassiveTreeWidget::CanvasSize() const
{
	// THE PANEL'S REAL SIZE ONCE SLATE HAS GIVEN IT ONE, and a stated guess
	// before that. A widget has no geometry until it has been laid out at least
	// once, and the first refresh happens in NativeConstruct, which is before
	// that. Placing a whole tree against a size of zero would put every node on
	// one point.
	// WHAT A TEST SAID IT IS, BEFORE ANYTHING ELSE. Issue #1078. A headless test
	// has no geometry at all, so without this the two answers below are the same
	// number for ever and a test cannot make the panel change size -- which is
	// the only thing worth checking about a refit. Zero means nothing was said.
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

	// A COMMON WINDOW, so the first frame is roughly right rather than absurd.
	// The next refresh, which any click causes, uses the real size.
	return FVector2D(1600.0, 800.0);
}

void UCataclysmPassiveTreeWidget::FitToTree()
{
	const FCataclysmTreeExtent Extent =
		UCataclysmPassiveTreeLayout::ExtentOf(NodeTable(), ShownTree);
	if (!Extent.bAny)
	{
		return;
	}

	const FVector2D Size = CanvasSize();
	Focus = Extent.Centre();
	Zoom = UCataclysmPassiveTreeLayout::ZoomToFit(Extent, Size);

	// WHAT THIS FIT WAS FOR. Issue #1078. `NativeTick` compares against it and
	// fits again when the panel is no longer that size, which is the whole of
	// what stops a shrinking panel hiding nodes behind its own edge.
	FittedAgainst = Size;

	PlaceGraph();
}

void UCataclysmPassiveTreeWidget::NativeTick(const FGeometry& MyGeometry,
											 float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// THE PANEL THE TREE IS DRAWN ON CHANGES SIZE WHILE THE SCREEN IS OPEN.
	// Issue #1078. It is the one child of the screen's vertical box with a Fill
	// size rule, so it gives up whatever height the two labels below it take,
	// and the description label grows with the text put in it. It also clips to
	// its own bounds, so anything the old zoom pushes outside is not drawn and
	// cannot be pressed.
	//
	// FITTED ONCE AND NEVER AGAIN, UNTIL THIS. `RefreshDisplay` fits only when a
	// different tree is shown. A panel that changed size afterwards kept the old
	// zoom, and the project owner could not click a capstone because it was
	// outside the panel's edge.
	//
	// AND IT WAS ALREADY WRONG ON THE FIRST FRAME. `CanvasSize` answers a stated
	// guess of 1600 by 800 until Slate has laid the panel out, and the first fit
	// happens in `NativeConstruct`, which is before that. Every window that is
	// not roughly that size has always been fitted against a number that was
	// never true.
	//
	// A COMPARISON RATHER THAN A FIT EVERY FRAME, so a player's own zoom and pan
	// survive. Zooming does not change the panel's size, so nothing here undoes
	// it; only the panel really changing size does.
	//
	// A WHOLE PIXEL OF TOLERANCE, because a layout that settles on 799.9997 one
	// frame and 800.0001 the next must not refit for ever.
	const FVector2D Size = CanvasSize();
	if (!Size.Equals(FittedAgainst, 1.0))
	{
		// RECORDED HERE AS WELL AS INSIDE THE FIT, because the fit returns early
		// when no tree is being shown. Without this line that case would scan
		// the node table again on every frame for as long as the screen is open.
		FittedAgainst = Size;
		FitToTree();
	}
}

void UCataclysmPassiveTreeWidget::ZoomBy(float Notches)
{
	Zoom = UCataclysmPassiveTreeLayout::ZoomAfterNotches(Zoom, Notches);
	PlaceGraph();
}

void UCataclysmPassiveTreeWidget::PanBy(FVector2D Pixels)
{
	// PIXELS OF THE PANEL, TURNED INTO UNITS OF THE TREE. Dragging by the same
	// number of pixels has to move the view by less of the tree when zoomed in,
	// or the tree would fly past at high zoom and crawl at low zoom.
	const float Safe = UCataclysmPassiveTreeLayout::ClampZoom(Zoom);
	Focus -= Pixels / Safe;
	PlaceGraph();
}

void UCataclysmPassiveTreeWidget::DescribeNodeButton(
	UCataclysmChoiceButton& Button, FName Node)
{
	const ACataclysmPlayerState* Player = State();
	static const FCataclysmPassiveAllocation Nothing;
	const FCataclysmPassiveAllocation& Allocation =
		Player ? Player->GetPassiveAllocation() : Nothing;
	const int32 Points = Player ? Player->PassivePointsAvailable() : 0;

	const FCataclysmPassiveNodeRow* Row =
		UCataclysmPassiveTree::FindNode(NodeTable(), Node);
	if (!Row)
	{
		return;
	}

	// THE SPENT COUNT ON THE MARK, AND NOTHING ELSE. Since 2026-11-24 a node is a mark sized by its kind, not a box
	// holding its name, so the shape a tree is laid out in reads; the name and count are drawn beside the mark of the
	// node hovered or selected (`PlaceNodeName`), and the refusal, a sentence, goes under the tree as it always did.
	// A node holding no points says nothing on its mark.
	const FString Label = UCataclysmPassiveTreeLayout::MarkCaption(Allocation.PointsIn(Node), Row->MaxPoints);

	// A NODE WHOSE POINTS ADD NOTHING IS STILL TOLD APART. Ruled 2026-10-09 (P6). More points are spent than earned,
	// and this node is one that gives some up. How many goes beside its name when it is hovered
	// (`NodeNameCaptionText`), which a mark has no room for, and the reason on the tool tip below.
	const int32 Idle =
		UCataclysmPassiveTree::PointsAddingNothingIn(Allocation, Points, Node);

	// AND A CAPSTONE THAT HAS OPENED IS CLICKABLE THOUGH IT CANNOT TAKE A POINT.
	// Issue #1064. Until then this asked only whether a point could go in, and
	// a capstone whose option is unchosen is refused, so the node the player had
	// just earned was drawn exactly like one they had not reached. Clicking it
	// now offers the three options, so it has to look like it can be clicked.
	// NOR A NODE IN A CLASS NOT CHOSEN FOR ITS DAMAGE TYPE. Issue #2064. The
	// tree's own rules do not know the character, so this is asked beside them.
	// The sentence goes on the node as its tool tip, since a node that cannot be
	// clicked never shows the refusal under the tree.
	const FString NotYourClass = ClassRefusalFor(Row->Tree);
	const bool bCanTake = NotYourClass.IsEmpty()
		&& (UCataclysmPassiveTree::RefusalForSpending(
				NodeTable(), EdgeTable(), Allocation, Node, Points).IsEmpty()
			|| UCataclysmPassiveTree::AwaitsAnOptionChoice(NodeTable(),
														   Allocation, Node));

	Button.SetChoice(Node, FText::FromString(Label),
					 Allocation.PointsIn(Node) > 0, bCanTake);

	// THE CLASS REFUSAL FIRST, AS IT WAS. A node marked above says why here when
	// no class refusal is being shown.
	FString ToolTip = NotYourClass;
	if (ToolTip.IsEmpty() && Idle > 0)
	{
		ToolTip = FString::Printf(
			TEXT("%d of the points in this node add%s nothing. More points are "
				 "spent than earned, because gear that granted points is no "
				 "longer worn. The points stay spent and count again when that "
				 "gear is worn."),
			Idle, Idle == 1 ? TEXT("s") : TEXT(""));
	}

	// AND, LAST, A NODE WITH NO ROW: its name marked "(not built yet: it does nothing)". The War vow options' second
	// slice, 2026-11-25. Not a capstone, whose own points carry no row by design: its options are marked where they are
	// offered. No table, nothing marked.
	const UDataTable* Effects =
		EffectTableForTests ? EffectTableForTests.Get() : UCataclysmPassiveTree::LoadEffectTable();
	if (ToolTip.IsEmpty() && Row->Kind != UCataclysmPassiveTree::CapstoneKind && Effects
		&& !UCataclysmPassiveTree::HasEffectRows(Effects, Node))
	{
		ToolTip = UCataclysmPassiveTree::MarkedNotBuiltYet(Row->NodeName);
	}
	Button.SetToolTipText(FText::FromString(ToolTip));
}

void UCataclysmPassiveTreeWidget::DescribeTreeButton(
	UCataclysmChoiceButton& Button, FName Tree)
{
	// A TREE THE CHARACTER CANNOT REACH IS STILL CLICKABLE, and shown
	// dimmed. Reading what another tree offers is how a player decides
	// which weapon to carry, so hiding them would make that choice
	// blind. Only spending is refused.
	Button.SetChoice(Tree, FText::FromName(Tree), ShownTree == Tree.ToString(),
					 /*bAvailable=*/true);

	// AND A CLASS NOT CHOSEN FOR ITS DAMAGE TYPE IS DIMMED, still clickable for
	// the same reason, with the reason on hover. Issue #2064.
	const FString NotYourClass = ClassRefusalFor(Tree.ToString());
	Button.SetDimmed(!NotYourClass.IsEmpty());
	Button.SetToolTipText(FText::FromString(NotYourClass));
}

void UCataclysmPassiveTreeWidget::DescribeButtonForTests(
	UCataclysmChoiceButton& Button, FName Value, bool bIsTree)
{
	if (bIsTree)
	{
		DescribeTreeButton(Button, Value);
	}
	else
	{
		DescribeNodeButton(Button, Value);
	}
}

FString UCataclysmPassiveTreeWidget::ClassRefusalFor(const FString& Tree) const
{
	const ACataclysmPlayerState* Player = State();
	if (!Player)
	{
		return FString();
	}
	// THE WORN WEAPONS' TYPES, as `ACataclysmPlayerState::ReachesTreeOf` asks.
	// Issue #857 layer 1.
	return UCataclysmPassiveTree::RefusalForClassChoice(
		NodeTable(), Player->GetPassiveAllocation(), Tree,
		Player->CarriedDamageTypes());
}

void UCataclysmPassiveTreeWidget::BuildGraph()
{
	if (!GraphCanvas)
	{
		return;
	}

	UClass* ButtonClass = LoadedChoiceButtonClass();
	if (!ButtonClass)
	{
		return;
	}

	GraphCanvas->ClearChildren();
	NodeButtons.Reset();
	NodeButtonNames.Reset();
	EdgeImages.Reset();
	EdgeEnds.Reset();
	NodeNameLabel = nullptr;
	BuiltTree = ShownTree;
	BuiltMarkScale = UCataclysmPassiveTreeLayout::MarkScaleFor(NodeTable(), ShownTree);

	if (ShownTree.IsEmpty())
	{
		return;
	}

	// THE EDGES FIRST, SO THE NODES SIT ON TOP OF THEM. A canvas panel draws its
	// children in the order they were added, so a line added after a node would
	// be drawn across the node's own words.
	for (const TPair<FName, FName>& Edge :
		 UCataclysmPassiveTree::EdgesIn(EdgeTable(), ShownTree))
	{
		UImage* Line = NewObject<UImage>(this);
		if (!Line)
		{
			continue;
		}

		Line->SetColorAndOpacity(EdgeColour);

		if (UCanvasPanelSlot* Placement =
				Cast<UCanvasPanelSlot>(GraphCanvas->AddChild(Line)))
		{
			// ANCHORED TO THE TOP LEFT AND ALIGNED TO THE MIDDLE OF ITS OWN LEFT
			// EDGE, which is what lets a rectangle be turned into a line: it is
			// placed at the near end and rotated about that end.
			Placement->SetAnchors(FAnchors(0.0f, 0.0f));
			Placement->SetAlignment(UCataclysmPassiveTreeLayout::EdgePivot());
			Placement->SetAutoSize(false);
		}

		Line->SetRenderTransformPivot(UCataclysmPassiveTreeLayout::EdgePivot());
		EdgeImages.Add(Line);
		EdgeEnds.Add(Edge);
	}

	for (const FName& Node : UCataclysmPassiveTree::NodesIn(NodeTable(), ShownTree))
	{
		UCataclysmChoiceButton* Button =
			CreateWidget<UCataclysmChoiceButton>(this, ButtonClass);
		if (!Button)
		{
			continue;
		}

		Button->OnChoiceClicked.AddDynamic(
			this, &UCataclysmPassiveTreeWidget::HandleNodeClicked);
		Button->OnChoiceHovered.AddDynamic(
			this, &UCataclysmPassiveTreeWidget::HandleNodeHovered);

		// A COUNT TOO WIDE FOR ITS MARK IS CUT, NOT DRAWN OVER THE NEXT ONE. A mark holds at most "12/12" since
		// 2026-11-24, and at a fitted zoom an ordinary mark is about a dozen pixels; the name and count are drawn in
		// full beside the mark of the node hovered or selected.
		//
		// SET HERE AND NOT IN THE CHOICE BUTTON ITSELF, because the constraint
		// comes from this screen. The character creator lays the same buttons
		// out in a list with room to spare and would gain nothing from it.
		Button->SetClipping(EWidgetClipping::ClipToBounds);

		if (UCanvasPanelSlot* Placement =
				Cast<UCanvasPanelSlot>(GraphCanvas->AddChild(Button)))
		{
			// ALIGNED TO ITS OWN MIDDLE, so the position a node is given is the
			// node's centre. Anchoring a node by its corner would slide it as
			// the view zoomed even when it was the thing being zoomed towards.
			Placement->SetAnchors(FAnchors(0.0f, 0.0f));
			Placement->SetAlignment(FVector2D(0.5, 0.5));
			Placement->SetAutoSize(false);
		}

		NodeButtons.Add(Button);
		NodeButtonNames.Add(Node);
	}

	// THE NAME OF THE NODE HOVERED OR SELECTED, ADDED LAST SO IT IS DRAWN OVER THE MARKS. Since 2026-11-24. Hit-test
	// invisible, so a click meant for a mark under it reaches the mark. Aligned to the middle of its own left edge, so
	// `NameCaptionPosition` puts it beside the mark, level with its centre.
	NodeNameLabel = NewObject<UTextBlock>(this);
	if (NodeNameLabel)
	{
		NodeNameLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UCanvasPanelSlot* Placement = Cast<UCanvasPanelSlot>(GraphCanvas->AddChild(NodeNameLabel)))
		{
			Placement->SetAnchors(FAnchors(0.0f, 0.0f));
			Placement->SetAlignment(FVector2D(0.0, 0.5));
			Placement->SetAutoSize(true);
		}
	}
}

FString UCataclysmPassiveTreeWidget::NodeNameCaptionText() const
{
	const FCataclysmPassiveNodeRow* Row = UCataclysmPassiveTree::FindNode(NodeTable(), LastTouched);
	// NOTHING WHEN NO NODE OF THE TREE SHOWN IS HOVERED OR SELECTED: changing tree clears `LastTouched`.
	if (!Row || Row->Tree != ShownTree)
	{
		return FString();
	}

	const ACataclysmPlayerState* Player = State();
	static const FCataclysmPassiveAllocation Nothing;
	const FCataclysmPassiveAllocation& Allocation = Player ? Player->GetPassiveAllocation() : Nothing;
	FString Caption = UCataclysmPassiveTreeLayout::NameCaption(Row->NodeName, Allocation.PointsIn(LastTouched),
															   Row->MaxPoints);

	// AND HOW MANY OF ITS POINTS ADD NOTHING, which the node's box said until 2026-11-24 (ruled 2026-10-09, P6).
	const int32 Idle = UCataclysmPassiveTree::PointsAddingNothingIn(
		Allocation, Player ? Player->PassivePointsAvailable() : 0, LastTouched);
	if (Idle > 0)
	{
		Caption += FString::Printf(TEXT("  (%d add%s nothing)"), Idle, Idle == 1 ? TEXT("s") : TEXT(""));
	}
	return Caption;
}

FVector2D UCataclysmPassiveTreeWidget::NodeNameCaptionPositionForTests() const
{
	const FCataclysmPassiveNodeRow* Row = UCataclysmPassiveTree::FindNode(NodeTable(), LastTouched);
	if (!Row || Row->Tree != ShownTree)
	{
		return FVector2D::ZeroVector;
	}
	// THE SCALE WORKED OUT HERE, since a headless test has no canvas and `BuildGraph` never reaches the line that keeps
	// it; the same function, so the same number.
	const float Scale = UCataclysmPassiveTreeLayout::MarkScaleFor(NodeTable(), ShownTree);
	const FVector2D Centre = UCataclysmPassiveTreeLayout::ScreenPositionFor(
		FVector2D(Row->PositionX, Row->PositionY), Focus, Zoom, CanvasSize());
	return UCataclysmPassiveTreeLayout::NameCaptionPosition(
		Centre, UCataclysmPassiveTreeLayout::MarkSizeForNode(NodeTable(), LastTouched) * Scale * Zoom);
}

void UCataclysmPassiveTreeWidget::PlaceNodeName()
{
	if (!NodeNameLabel)
	{
		return;
	}

	const FString Caption = NodeNameCaptionText();
	NodeNameLabel->SetText(FText::FromString(Caption));
	NodeNameLabel->SetVisibility(Caption.IsEmpty() ? ESlateVisibility::Collapsed
												   : ESlateVisibility::HitTestInvisible);

	const FCataclysmPassiveNodeRow* Row = UCataclysmPassiveTree::FindNode(NodeTable(), LastTouched);
	if (!Row || Caption.IsEmpty())
	{
		return;
	}
	if (UCanvasPanelSlot* Placement = Cast<UCanvasPanelSlot>(NodeNameLabel->Slot))
	{
		const FVector2D Centre = UCataclysmPassiveTreeLayout::ScreenPositionFor(
			FVector2D(Row->PositionX, Row->PositionY), Focus, Zoom, CanvasSize());
		Placement->SetPosition(UCataclysmPassiveTreeLayout::NameCaptionPosition(
			Centre, UCataclysmPassiveTreeLayout::MarkSizeForNode(NodeTable(), LastTouched) * BuiltMarkScale * Zoom));
	}
}

void UCataclysmPassiveTreeWidget::PlaceGraph()
{
	if (!GraphCanvas)
	{
		return;
	}

	const FVector2D Size = CanvasSize();
	const UDataTable* Table = NodeTable();

	// WHERE EACH NODE LANDS, WORKED OUT ONCE. Every edge needs the screen
	// position of both its ends, and most nodes are an end of more than one
	// edge, so asking per edge would repeat the same arithmetic.
	TMap<FName, FVector2D> Placed;
	Placed.Reserve(NodeButtonNames.Num());

	for (int32 Index = 0; Index < NodeButtons.Num(); ++Index)
	{
		UCataclysmChoiceButton* Button = NodeButtons[Index];
		const FName Node = NodeButtonNames[Index];
		const FCataclysmPassiveNodeRow* Row =
			UCataclysmPassiveTree::FindNode(Table, Node);
		if (!Button || !Row)
		{
			continue;
		}

		const FVector2D At = UCataclysmPassiveTreeLayout::ScreenPositionFor(
			FVector2D(Row->PositionX, Row->PositionY), Focus, Zoom, Size);
		Placed.Add(Node, At);

		if (UCanvasPanelSlot* Placement = Cast<UCanvasPanelSlot>(Button->Slot))
		{
			// THE NODE'S SIZE SCALES WITH THE VIEW, so zooming out really does
			// show more of the tree rather than the same number of full-sized
			// buttons in a smaller area.
			Placement->SetPosition(At);
			// THE NODE'S KIND DECIDES ITS MARK, THE TREE'S SPACING HOW FAR IT MAY BE SHRUNK, AND THE VIEW HOW BIG
			// THAT IS ON SCREEN. Since 2026-11-24: an ordinary node, a larger keystone and a wide vow, so the kinds
			// and the tree's shape read apart. No class tree crowds its marks today (`MarkScaleFor` is 1 for each).
			Placement->SetSize(UCataclysmPassiveTreeLayout::MarkSizeForNode(Table, Node) * BuiltMarkScale * Zoom);
		}

		// THE COUNT ON THE MARK SHRINKS WITH IT, in the proportion the mark's height bears to the empire tree's box,
		// whose label the choice button's designed font size was made for. Issue #1089 for why a screen asks.
		Button->SetLabelScale(static_cast<float>(UCataclysmPassiveTreeLayout::MarkSizeForNode(Table, Node).Y
												 / UCataclysmPassiveTreeLayout::NodeHeightPx)
							  * BuiltMarkScale * Zoom);

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
			// AN EDGE WHOSE ENDS ARE NOT BOTH ON THE CANVAS IS HIDDEN RATHER
			// THAN DRAWN FROM NOWHERE. The tables cannot produce one -- the
			// generator refuses an edge naming a node that is not in the same
			// tree -- but a line from the origin to a node would be a striking
			// thing to draw if they ever did.
			Line->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}

		Line->SetVisibility(ESlateVisibility::HitTestInvisible);

		FVector2D At;
		FVector2D Extent;
		float Angle = 0.0f;
		// THICKER THAN THE EMPIRE TREE'S, since 2026-11-24: with the nodes drawn as marks the lines carry the shape,
		// and a gap between two marks with no line reads as a gap only when the lines are plain to see.
		UCataclysmPassiveTreeLayout::EdgeGeometry(
			*From, *To, UCataclysmPassiveTreeLayout::MarkEdgeThicknessPx * Zoom,
			At, Extent, Angle);

		if (UCanvasPanelSlot* Placement = Cast<UCanvasPanelSlot>(Line->Slot))
		{
			Placement->SetPosition(At);
			Placement->SetSize(Extent);
		}
		Line->SetRenderTransformAngle(Angle);
	}

	// THE NAME BESIDE THE NODE HOVERED OR SELECTED MOVES WITH THE VIEW, and its count changes with a spend.
	PlaceNodeName();
}

FReply UCataclysmPassiveTreeWidget::NativeOnMouseWheel(const FGeometry& Geometry,
													   const FPointerEvent& Event)
{
	ZoomBy(Event.GetWheelDelta());
	return FReply::Handled();
}

FReply UCataclysmPassiveTreeWidget::NativeOnMouseButtonDown(
	const FGeometry& Geometry, const FPointerEvent& Event)
{
	// THE RIGHT BUTTON PANS, AND THE LEFT ONE DOES NOT. The left button spends a
	// point, and a screen where dragging over a node both moved the view and
	// spent a point would be unusable. Every node graph in the genre pans with
	// the right button or with a held middle button for the same reason.
	if (Event.GetEffectingButton() != EKeys::RightMouseButton)
	{
		return FReply::Unhandled();
	}

	bDragging = true;
	DragFrom = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UCataclysmPassiveTreeWidget::NativeOnMouseMove(const FGeometry& Geometry,
													  const FPointerEvent& Event)
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

FReply UCataclysmPassiveTreeWidget::NativeOnMouseButtonUp(
	const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!bDragging || Event.GetEffectingButton() != EKeys::RightMouseButton)
	{
		return FReply::Unhandled();
	}

	bDragging = false;
	return FReply::Handled().ReleaseMouseCapture();
}
