// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/TextBlock.h"
#include "Data/CataclysmDataRows.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Interface/CataclysmEmpireMapWidget.h"
#include "Interface/CataclysmEmpireTreeLayout.h"
#include "Interface/CataclysmEmpireTreeWidget.h"
#include "Interface/CataclysmPassiveTreeLayout.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmEmpireTree.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayLoopLog.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"
#include "Tests/CataclysmTestRun.h"

/**
 * The empire upgrade tree's screen, and the empire map's button that opens it. Issue #2741 slice 2.
 *
 * `Cataclysm.EmpireTreeScreen.` IS A NEW GROUP and selects these and nothing else.
 *
 * HEADLESS, LIKE THE CLASS TREE SCREEN'S TESTS: the automation command passes `-nullrhi`, so a test can make
 * `UCataclysmEmpireTreeWidget` but not its Widget Blueprint, and every bound widget is null. What the screen says is
 * read from its own text functions, which write the labels, and from `UCataclysmEmpireTreeLayout`; what a click does is
 * driven through `TouchNode` and `PressRespec`, the functions the buttons call. Buying, choosing and respec go through
 * `UCataclysmGameInstance`, slice 1b's, against a world with a possessed player, built as slice 1b's tests build it.
 *
 * THE SLOT IS THE TEST'S OWN, for slice 1b's reason: the account slot of an automation test ends in `_AutomationTest`,
 * and each scene deletes it first. No scene begins the save writer, so nothing here writes a save.
 *
 * WHAT IS NOT HERE: the Widget Blueprint (the editor's compile of `WBP_EmpireTree` checks its bindings, and
 * `tools/tests/test_the_empire_tree_screen_is_generated_with_its_bindings.py` checks the generator adds them); opening
 * the screen through the player controller, which loads that Blueprint; the first view, the whole tree, which
 * `NativeConstruct` chooses and only a constructed Blueprint runs; what a bought node does (slices 3 to 5); and how the
 * drawing looks, which no headless test can see (the automation command passes `-nullrhi`).
 */
namespace CataclysmEmpireTreeScreenTest
{
	// The tree file's identifiers, as slice 1a's and 1b's tests name them.
	const FName StructuralIntegrity(TEXT("basic_0000000w"));	// Architect, a root, 10 points
	const FName FortifiedGates(TEXT("basic_arch_t1d"));			// <- Structural Integrity at 2, 8 points
	const FName MasonryTechniques(TEXT("basic_00000010"));		// <- Fortified Gates at 6, 15 points
	const FName MerchantGuild(TEXT("basic_tres_t1e"));		// Treasury, <- Coinage at 2; waits on a market
	// (Rough Draft, an Artisan root, was the node used here until #2741's crafting slice C4 built it, 2026-11-25.)
	const FName Foundations(TEXT("capstone_0000000m"));			// the centre, at 25: Aegis of Hope, The Delver, The Hoarder

	/** The one sentence buying, choosing and respec all say during a walk. */
	const TCHAR* const WalkRefusal = TEXT("The empire upgrade tree can be changed in the capital, not during a walk.");

	/** A world with this project's game instance and a possessed player of one partition. */
	struct FTreeScreenScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		FCataclysmCharacterPartition Partition;

		bool IsUsable() const { return World && Instance && State; }

		/** The scene's account record: the one the game instance holds for its partition. */
		UCataclysmAccountSave* Account() const { return Instance != nullptr ? Instance->AccountFor(Partition) : nullptr; }

		/** The run, or null when the scene has none. */
		UCataclysmEmpireRun* Run() const
		{
			return World != nullptr ? UCataclysmGameInstance::EmpireRunFor(World, /*bStartIfNone*/ false) : nullptr;
		}
	};

	/** Waits for any write to the slot to finish, then deletes it when it exists. */
	void DeleteTheSlot(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		if (UGameplayStatics::DoesSaveGameExist(SlotName, UCataclysmSaveWriter::UserIndex))
		{
			UGameplayStatics::DeleteGameInSlot(SlotName, UCataclysmSaveWriter::UserIndex);
		}
	}

	/**
	 * The scene: a Standard partition, offline, the test's account slot emptied first. `bWithARun` begins a run at the
	 * Standard rung, as a character's run is begun. The save writer is not begun, so nothing is written.
	 */
	FTreeScreenScene MakeScene(bool bSoloSelfFound, bool bWithARun)
	{
		FTreeScreenScene Out;
		Out.Partition.Lethality = ECataclysmLethality::Standard;
		Out.Partition.Population = ECataclysmPopulation::Offline;
		Out.Partition.bSoloSelfFound = bSoloSelfFound;

		const FString Slot = UCataclysmGameInstance::AccountSlotFor(Out.Partition);
		if (!Slot.IsEmpty())
		{
			DeleteTheSlot(Slot);
		}

		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}

		Out.Instance = NewObject<UCataclysmGameInstance>();
		Out.World->SetGameInstance(Out.Instance);

		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Out.State && Controller && Character)
		{
			Controller->SetPlayerState(Out.State);
			Controller->Possess(Character);
			Out.State->SetIdentity(TEXT("Treewatcher"), Out.Partition);
		}
		else
		{
			Out.State = nullptr;
		}

		if (bWithARun)
		{
			const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
			Out.Instance->BeginEmpireRun(/*Seed*/ 2741, ECataclysmSurgeMode::Static,
										 static_cast<int32>(ECataclysmLethality::Standard), /*DifficultyTier*/ 1);
		}
		return Out;
	}

	/** The screen, reading the scene's world, with the generated tables. Made with no Blueprint, as every test makes it. */
	UCataclysmEmpireTreeWidget* MakeScreen(const FTreeScreenScene& Scene)
	{
		UCataclysmEmpireTreeWidget* Screen = NewObject<UCataclysmEmpireTreeWidget>();
		if (Screen)
		{
			Screen->SetWorldContextForTests(Scene.World);
			Screen->SetPanelSizeForTests(FVector2D(1600.0, 900.0));
		}
		return Screen;
	}

	/** Twenty-five points in the Architect branch, the centre's first threshold: 10, 8 and 7 along one line of it. */
	void SpendTwentyFiveInArchitect(FCataclysmPassiveAllocation& Tree)
	{
		Tree.Add(StructuralIntegrity, 10);
		Tree.Add(FortifiedGates, 8);
		Tree.Add(MasonryTechniques, 7);
	}
}

// NO `using namespace` AT FILE SCOPE, for the reason `CataclysmEmpireTreeTests.cpp` gives: in a unity build it would
// reach the files compiled after this one. Each test opens the namespace inside its body.

// ---------------------------------------------------------------------------
// The words
// ---------------------------------------------------------------------------

/**
 * Every sentence the screen shows comes from `UCataclysmEmpireTreeLayout`, and these are its words: the six views, the
 * whole tree first and then the branches in the file's order with the centre named "The centre", each view's button
 * with its points, the bank (an account's, and a Solo Self-Found character's own), a view's points, a node's name and
 * its line, a capstone's options, and the Respec button's two faces. Prints "WORDS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenWordsTest,
	"Cataclysm.EmpireTreeScreen.TheLayoutSaysWhatTheScreenShows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenWordsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeScreenTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const FCataclysmEmpireTreeNodeRow* Integrity = UCataclysmEmpireTree::FindNode(Nodes, StructuralIntegrity);
	const FCataclysmEmpireTreeNodeRow* Capstone = UCataclysmEmpireTree::FindNode(Nodes, Foundations);
	if (!TestNotNull(TEXT("set-up: the generated node table loads"), Nodes)
		|| !TestNotNull(TEXT("set-up: with Structural Integrity in it"), Integrity)
		|| !TestNotNull(TEXT("set-up: and Foundations of the Empire"), Capstone))
	{
		return false;
	}

	const TArray<FString> Views = UCataclysmEmpireTreeLayout::Views();
	AddInfo(FString::Printf(TEXT("WORDS: the views are %s"), *FString::Join(Views, TEXT(", "))));
	TestEqual(TEXT("six views: the whole tree, then the four branches and the centre in the file's order"), Views,
			  TArray<FString>({ TEXT("Whole tree"), TEXT("Architect"), TEXT("Explorer"), TEXT("Treasury"), TEXT("Artisan"),
								TEXT("Centre") }));
	TestEqual(TEXT("the whole tree's view is named for a person"),
			  UCataclysmEmpireTreeLayout::ViewLabel(UCataclysmEmpireTreeLayout::WholeTreeView),
			  FString(TEXT("The whole tree")));
	TestEqual(TEXT("the centre's view too"), UCataclysmEmpireTreeLayout::ViewLabel(TEXT("Centre")),
			  FString(TEXT("The centre")));
	TestEqual(TEXT("a branch's view by its own name"), UCataclysmEmpireTreeLayout::ViewLabel(TEXT("Explorer")),
			  FString(TEXT("Explorer")));
	TestEqual(TEXT("a view's button says its points"), UCataclysmEmpireTreeLayout::ViewButtonLabel(TEXT("Architect"), 26),
			  FString(TEXT("Architect  26 points")));
	TestEqual(TEXT("one point is one point"), UCataclysmEmpireTreeLayout::ViewButtonLabel(TEXT("Centre"), 1),
			  FString(TEXT("The centre  1 point")));

	TestEqual(TEXT("the bank, with the whole points that can be spent"), UCataclysmEmpireTreeLayout::BankLine(3.5, false),
			  FString(TEXT("3.5 empire upgrade points banked: 3 to spend")));
	TestEqual(TEXT("a Solo Self-Found character's bank says it is its own"),
			  UCataclysmEmpireTreeLayout::BankLine(1.0, true),
			  FString(TEXT("1.0 empire upgrade points banked: 1 to spend; this character's own")));

	TestEqual(TEXT("a branch's points, one"), UCataclysmEmpireTreeLayout::BranchLine(TEXT("Centre"), 1, 26),
			  FString(TEXT("The centre: 1 point spent here, 26 in the whole tree")));
	TestEqual(TEXT("and more than one"), UCataclysmEmpireTreeLayout::BranchLine(TEXT("Architect"), 3, 3),
			  FString(TEXT("Architect: 3 points spent here, 3 in the whole tree")));
	TestEqual(TEXT("the whole tree's line says the tree's points once"),
			  UCataclysmEmpireTreeLayout::BranchLine(UCataclysmEmpireTreeLayout::WholeTreeView, 26, 26),
			  FString(TEXT("The whole tree: 26 points spent")));

	TestEqual(TEXT("a node's name beside its mark: its name and points"), UCataclysmEmpireTreeLayout::NodeLabel(*Integrity, 2),
			  FString(TEXT("Structural Integrity  2/10")));
	const FString CapstoneLine = UCataclysmEmpireTreeLayout::NodeDetail(*Capstone, 1, 2, FString());
	AddInfo(FString::Printf(TEXT("WORDS: \"%s\""), *CapstoneLine));
	TestEqual(TEXT("a capstone's line names its three options and which was chosen"), CapstoneLine,
			  FString(TEXT("Foundations of the Empire (The centre, 1 of 1): Choose one empire-wide capstone as you enter "
						   "Tier 1. Options: 1 The Aegis of Hope; 2 The Delver (chosen); 3 The Hoarder.")));
	TestTrue(TEXT("a node's line ends with why it cannot take a point"),
			 UCataclysmEmpireTreeLayout::NodeDetail(*Integrity, 0, 0, TEXT("No points to spend."))
				 .EndsWith(TEXT(". No points to spend.")));
	TestTrue(TEXT("and begins with the node, its view and its points"),
			 UCataclysmEmpireTreeLayout::NodeDetail(*Integrity, 0, 0, FString())
				 .StartsWith(TEXT("Structural Integrity (Architect, 0 of 10): ")));

	TestEqual(TEXT("an option chosen"), UCataclysmEmpireTreeLayout::OptionLabel(2, TEXT("The Delver"), true, FString()),
			  FString(TEXT("2  The Delver  (chosen)")));
	TestEqual(TEXT("an option that waits on a system the game does not have"),
			  UCataclysmEmpireTreeLayout::OptionLabel(3, TEXT("The Soul Forge"), false, TEXT("crafting")),
			  FString(TEXT("3  The Soul Forge  (waits on crafting)")));

	TestEqual(TEXT("the Respec button before it is pressed"), UCataclysmEmpireTreeLayout::RespecLabel(5, false),
			  FString(TEXT("Respec: 5 days of the run")));
	TestEqual(TEXT("and after its first press"), UCataclysmEmpireTreeLayout::RespecLabel(10, true),
			  FString(TEXT("Press again to respec: 10 days of the run")));
	return true;
}

/**
 * The three PLAY LOOP lines the game instance says when a point is bought, an option chosen and the tree respecced,
 * word for word as `docs/Play_Loop_Checklist.md` step 12 quotes them. Prints "LOG".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenLogLinesTest,
	"Cataclysm.EmpireTreeScreen.TheLogLinesNameWhatWasBoughtChosenAndRespecced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenLogLinesTest::RunTest(const FString&)
{
	const FString Bought =
		FCataclysmPlayLoopLog::TreePointLine(TEXT("Structural Integrity"), TEXT("Architect"), 1, 10, 0.0);
	const FString Chosen = FCataclysmPlayLoopLog::TreeOptionLine(TEXT("The Delver"), TEXT("Foundations of the Empire"));
	const FString One = FCataclysmPlayLoopLog::TreeRespecLine(1, 1.0, 5, 20, 25);
	const FString Many = FCataclysmPlayLoopLog::TreeRespecLine(26, 27.5, 10, 3, 13);
	AddInfo(FString::Printf(TEXT("LOG: \"%s\" / \"%s\" / \"%s\" / \"%s\""), *Bought, *Chosen, *One, *Many));

	TestEqual(TEXT("a point bought"), Bought,
			  FString(TEXT("bought a point in Structural Integrity (Architect): 1 of 10; 0.0 empire points banked")));
	TestEqual(TEXT("an option chosen"), Chosen, FString(TEXT("chose The Delver for Foundations of the Empire")));
	TestEqual(TEXT("a respec returning one point"), One,
			  FString(TEXT("respecced the empire upgrade tree: 1 point back, 1.0 points banked; it cost 5 days, day 20 "
						   "to day 25")));
	TestEqual(TEXT("and one returning more"), Many,
			  FString(TEXT("respecced the empire upgrade tree: 26 points back, 27.5 points banked; it cost 10 days, day 3 "
						   "to day 13")));
	return true;
}

// ---------------------------------------------------------------------------
// The whole tree as drawn
// ---------------------------------------------------------------------------

/**
 * The owner, 2026-11-27: "not the beautiful circle json with 4 quadrants we built with the passivetreebuilder". Over the
 * generated tables, the screen draws every node in every view and every edge of the file, and the marks at the whole
 * tree's one scale miss each other. Prints "WHOLE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenWholeTreeTest,
	"Cataclysm.EmpireTreeScreen.EveryViewDrawsEveryNodeAndEveryEdgeAndNoMarksOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenWholeTreeTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeScreenTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Edges = UCataclysmEmpireTree::LoadEdgeTable();
	UCataclysmEmpireTreeWidget* Screen = NewObject<UCataclysmEmpireTreeWidget>();
	if (!TestNotNull(TEXT("set-up: the generated node table loads"), Nodes)
		|| !TestNotNull(TEXT("set-up: and the edge table"), Edges) || !TestNotNull(TEXT("set-up: and the screen"), Screen))
	{
		return false;
	}

	// EVERY NODE, IN EVERY VIEW: a view points the drawing, it does not choose what is drawn.
	TArray<FString> Parts;
	for (const FString& View : UCataclysmEmpireTreeLayout::Views())
	{
		Screen->ShowBranch(View);
		Parts.Add(FString::Printf(TEXT("%s %d nodes"), *View, Screen->ShownNodes().Num()));
		TestEqual(FString::Printf(TEXT("the %s view draws every node of the table"), *View), Screen->ShownNodes().Num(),
				  Nodes->GetRowMap().Num());
	}
	TArray<FName> Drawn = Screen->ShownNodes();
	Drawn.Sort(FNameLexicalLess());
	TArray<FName> InTable = Nodes->GetRowNames();
	InTable.Sort(FNameLexicalLess());
	TestEqual(TEXT("and they are the table's nodes, each once"), Drawn, InTable);

	const TArray<TPair<FName, FName>> Lines = UCataclysmEmpireTreeLayout::EdgesOf(Nodes, Edges);
	TestEqual(TEXT("every edge of the file is drawn"), Lines.Num(), Edges->GetRowMap().Num());

	// THE MARKS AT THE WHOLE TREE'S ONE SCALE MISS EACH OTHER: two marks placed by their centres miss when they are clear
	// on either axis by half the two marks' sizes added. The tolerance is for the float the scale is held in.
	const float Scale = UCataclysmEmpireTreeLayout::MarkScaleFor(Nodes);
	int32 Overlaps = 0;
	for (int32 First = 0; First < InTable.Num(); ++First)
	{
		const FCataclysmEmpireTreeNodeRow* A = UCataclysmEmpireTree::FindNode(Nodes, InTable[First]);
		for (int32 Second = First + 1; Second < InTable.Num(); ++Second)
		{
			const FCataclysmEmpireTreeNodeRow* B = UCataclysmEmpireTree::FindNode(Nodes, InTable[Second]);
			if (A == nullptr || B == nullptr)
			{
				continue;
			}
			const FVector2D Half = (UCataclysmEmpireTreeLayout::MarkSizeFor(*A) + UCataclysmEmpireTreeLayout::MarkSizeFor(*B))
				* 0.5 * Scale;
			if (FMath::Abs(A->PositionX - B->PositionX) < Half.X - 0.01 && FMath::Abs(A->PositionY - B->PositionY) < Half.Y - 0.01)
			{
				++Overlaps;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("WHOLE: %s; %d edges; mark scale %.3f; %d overlapping pairs"),
							*FString::Join(Parts, TEXT(", ")), Lines.Num(), Scale, Overlaps));
	TestTrue(TEXT("the mark scale is above a half and at most one"), Scale > 0.5f && Scale <= 1.0f);
	TestEqual(TEXT("no two marks overlap at it"), Overlaps, 0);
	// THE CONTROL: at the full mark size, which the scale was worked out to avoid, two marks do overlap. Without this a
	// count of nought could come from a check that compares nothing.
	int32 OverlapsAtFullSize = 0;
	for (int32 First = 0; First < InTable.Num() && OverlapsAtFullSize == 0; ++First)
	{
		const FCataclysmEmpireTreeNodeRow* A = UCataclysmEmpireTree::FindNode(Nodes, InTable[First]);
		for (int32 Second = First + 1; Second < InTable.Num(); ++Second)
		{
			const FCataclysmEmpireTreeNodeRow* B = UCataclysmEmpireTree::FindNode(Nodes, InTable[Second]);
			if (A == nullptr || B == nullptr)
			{
				continue;
			}
			const FVector2D Half = (UCataclysmEmpireTreeLayout::MarkSizeFor(*A) + UCataclysmEmpireTreeLayout::MarkSizeFor(*B)) * 0.5;
			if (FMath::Abs(A->PositionX - B->PositionX) < Half.X && FMath::Abs(A->PositionY - B->PositionY) < Half.Y)
			{
				++OverlapsAtFullSize;
				break;
			}
		}
	}
	TestTrue(TEXT("CONTROL: at the full mark size two marks would overlap"), OverlapsAtFullSize > 0);
	return true;
}

/**
 * The positions are the tree file's, with no branch rescaled: two nodes in different quadrants -- Structural Integrity
 * in the Architect branch and Merchant Guild in the Treasury branch -- are as far apart on the panel, divided by the
 * zoom, as in the file, in every view. THE CONTROL: the views' zooms differ, so a view that rescaled its own branch
 * would change that distance; the whole tree's view is fitted further out than the Architect branch's. And a mark is
 * the same size, at the zoom, whichever view it is seen in. Prints "POSITIONS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenPositionsTest,
	"Cataclysm.EmpireTreeScreen.NodesKeepTheTreeFilesPositionsInEveryView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenPositionsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeScreenTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Decorations = UCataclysmEmpireTree::LoadDecorationTable();
	const FCataclysmEmpireTreeNodeRow* Integrity = UCataclysmEmpireTree::FindNode(Nodes, StructuralIntegrity);
	const FCataclysmEmpireTreeNodeRow* Guild = UCataclysmEmpireTree::FindNode(Nodes, MerchantGuild);
	UCataclysmEmpireTreeWidget* Screen = NewObject<UCataclysmEmpireTreeWidget>();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the decoration table"), Decorations)
		|| !TestNotNull(TEXT("set-up: Structural Integrity"), Integrity) || !TestNotNull(TEXT("set-up: Merchant Guild"), Guild)
		|| !TestNotNull(TEXT("set-up: the screen"), Screen))
	{
		return false;
	}
	TestEqual(TEXT("set-up: Structural Integrity is in the Architect quadrant"), Integrity->Branch, FString(TEXT("Architect")));
	TestEqual(TEXT("set-up: Merchant Guild in the Treasury quadrant"), Guild->Branch, FString(TEXT("Treasury")));

	const FVector2D Panel(1600.0, 900.0);
	Screen->SetPanelSizeForTests(Panel);
	const FVector2D InFile(Integrity->PositionX - Guild->PositionX, Integrity->PositionY - Guild->PositionY);

	TArray<FString> Parts;
	float WholeZoom = 0.0f;
	float ArchitectZoom = 0.0f;
	for (const FString& View : UCataclysmEmpireTreeLayout::Views())
	{
		Screen->ShowBranch(View);
		const float Zoom = Screen->GetZoom();
		const FVector2D OnPanel = Screen->NodeScreenPositionForTests(StructuralIntegrity)
			- Screen->NodeScreenPositionForTests(MerchantGuild);
		Parts.Add(FString::Printf(TEXT("%s zoom %.3f, apart (%.1f, %.1f) px"), *View, Zoom, OnPanel.X, OnPanel.Y));
		TestTrue(FString::Printf(TEXT("in the %s view the two are as far apart as in the file, at the zoom"), *View),
				 (OnPanel / Zoom).Equals(InFile, 0.01));
		WholeZoom = View == UCataclysmEmpireTreeLayout::WholeTreeView ? Zoom : WholeZoom;
		ArchitectZoom = View == TEXT("Architect") ? Zoom : ArchitectZoom;
	}
	AddInfo(FString::Printf(TEXT("POSITIONS: in the file (%.1f, %.1f); %s"), InFile.X, InFile.Y,
							*FString::Join(Parts, TEXT("; "))));
	TestTrue(TEXT("CONTROL: the views' zooms differ, the whole tree fitted further out than one branch"),
			 WholeZoom > 0.0f && WholeZoom < ArchitectZoom - 0.01f);

	Screen->ShowBranch(UCataclysmEmpireTreeLayout::WholeTreeView);
	// AND A MARK IS THE SAME SIZE IN EVERY VIEW AT THE SAME ZOOM: one scale for the whole tree.
	const FVector2D WholeMark = Screen->NodeMarkSizeForTests(StructuralIntegrity) / Screen->GetZoom();
	Screen->ShowBranch(TEXT("Architect"));
	TestTrue(TEXT("Structural Integrity's mark is the same size, at the zoom, in its branch's view"),
			 (Screen->NodeMarkSizeForTests(StructuralIntegrity) / Screen->GetZoom()).Equals(WholeMark, 0.001));
	return true;
}

/**
 * The whole tree's view holds the builder's outermost ring, which is wider than the nodes: the four points where the
 * 740 ring meets the axes are inside the panel, no nearer its edge than the whole view's margin less a pixel. THE
 * CONTROL: the nodes alone reach 630 below the middle and the ring 740, so a view fitted to the nodes alone puts the
 * ring's lowest point past the margin.
 *
 * AND THE RING FILLS THE PANEL a 1280 by 720 window gives it, about 1824 by 790 of Slate's units: at least 85% of its
 * height across. THE CONTROL: the class tree's 90 unit margin, which the whole view used until the packaged game showed
 * the ring 376 pixels across in a 720 pixel window (2026-11-27), leaves it about 71%. Prints "RING".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenRingTest,
	"Cataclysm.EmpireTreeScreen.TheWholeTreesViewHoldsItsOutermostRing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenRingTest::RunTest(const FString&)
{
	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	UCataclysmEmpireTreeWidget* Screen = NewObject<UCataclysmEmpireTreeWidget>();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the screen"), Screen))
	{
		return false;
	}
	const FVector2D Panel(1600.0, 900.0);
	Screen->SetPanelSizeForTests(Panel);
	Screen->ShowBranch(UCataclysmEmpireTreeLayout::WholeTreeView);

	const double Margin = UCataclysmEmpireTreeLayout::WholeTreeFitMarginPx - 1.0;
	TArray<FString> Parts;
	for (const FVector2D& Point : {FVector2D(740.0, 0.0), FVector2D(-740.0, 0.0), FVector2D(0.0, 740.0), FVector2D(0.0, -740.0)})
	{
		const FVector2D At = UCataclysmPassiveTreeLayout::ScreenPositionFor(Point, Screen->GetFocus(), Screen->GetZoom(), Panel);
		Parts.Add(FString::Printf(TEXT("(%.0f, %.0f) at (%.1f, %.1f)"), Point.X, Point.Y, At.X, At.Y));
		TestTrue(FString::Printf(TEXT("the ring's point (%.0f, %.0f) is inside the panel's margin"), Point.X, Point.Y),
				 At.X >= Margin && At.X <= Panel.X - Margin && At.Y >= Margin && At.Y <= Panel.Y - Margin);
	}
	AddInfo(FString::Printf(TEXT("RING: zoom %.3f; %s"), Screen->GetZoom(), *FString::Join(Parts, TEXT("; "))));

	// THE CONTROL, worked out rather than drawn: fitted to the nodes alone, AT THE SAME MARGIN, the ring's lowest point is
	// past the margin. (Window emp-w2's first try fitted this at the class tree's 90 unit margin and compared it with the
	// whole view's 24, so it showed nothing and failed; 2026-11-27.)
	const FCataclysmTreeExtent NodesOnly = UCataclysmEmpireTreeLayout::ExtentOf(Nodes, UCataclysmEmpireTreeLayout::WholeTreeView);
	const FVector2D Lowest = UCataclysmPassiveTreeLayout::ScreenPositionFor(
		FVector2D(0.0, 740.0), NodesOnly.Centre(),
		UCataclysmPassiveTreeLayout::ZoomToFit(NodesOnly, Panel, UCataclysmEmpireTreeLayout::WholeTreeFitMarginPx), Panel);
	TestTrue(FString::Printf(TEXT("CONTROL: fitted to the nodes alone, the ring's lowest point would be at %.1f, past %.1f"),
							 Lowest.Y, Panel.Y - Margin),
			 Lowest.Y > Panel.Y - Margin);

	// THE RING FILLS A 1280 BY 720 WINDOW'S PANEL: its diameter on the panel, against the panel's height.
	const FVector2D Window(1824.0, 790.0);
	Screen->SetPanelSizeForTests(Window);
	Screen->ShowBranch(TEXT("Architect"));
	Screen->ShowBranch(UCataclysmEmpireTreeLayout::WholeTreeView);
	const double Across = 2.0 * 740.0 * Screen->GetZoom();
	const FCataclysmTreeExtent Whole = UCataclysmEmpireTreeLayout::WithDecorations(
		UCataclysmEmpireTreeLayout::ExtentOf(Nodes, UCataclysmEmpireTreeLayout::WholeTreeView),
		UCataclysmEmpireTree::LoadDecorationTable());
	const double AcrossAtTheClassMargin = 2.0 * 740.0 * UCataclysmPassiveTreeLayout::ZoomToFit(Whole, Window);
	AddInfo(FString::Printf(TEXT("RING: at %.0f by %.0f the ring is %.1f across, %.1f%% of the height; at the class tree's "
								 "margin %.1f, %.1f%%"),
							Window.X, Window.Y, Across, 100.0 * Across / Window.Y, AcrossAtTheClassMargin,
							100.0 * AcrossAtTheClassMargin / Window.Y));
	TestTrue(TEXT("at a 1280 by 720 window's panel the ring is at least 85% of its height across"),
			 Across >= 0.85 * Window.Y);
	TestTrue(TEXT("CONTROL: at the class tree's margin it would be less"), AcrossAtTheClassMargin < 0.85 * Window.Y);

	// THE LINES' TIPS STAY ON THE PANEL: the panel clips what is past its edge.
	for (const FVector2D& Tip : {FVector2D(800.0, 0.0), FVector2D(-800.0, 0.0), FVector2D(0.0, 800.0), FVector2D(0.0, -800.0)})
	{
		const FVector2D At = UCataclysmPassiveTreeLayout::ScreenPositionFor(Tip, Screen->GetFocus(), Screen->GetZoom(), Window);
		TestTrue(FString::Printf(TEXT("the line's tip (%.0f, %.0f) is on the panel, at (%.1f, %.1f)"), Tip.X, Tip.Y, At.X, At.Y),
				 At.X >= 0.0 && At.X <= Window.X && At.Y >= 0.0 && At.Y <= Window.Y);
	}
	return true;
}

/**
 * Each kind of node is drawn in its own colour, the colours of the preview the owner approved on 2026-11-27: an ordinary
 * node grey, a keystone a lighter blue-grey, a capstone gold. The screen gives a node's button its kind's colour, and the
 * label keeps the state's colour. And the screen's background is the preview's near-black, opaque. THE CONTROL: a button
 * the screen never described keeps the Widget Blueprint's style, which drew every node of the packaged game alike.
 * Prints "MARKS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenMarkColoursTest,
	"Cataclysm.EmpireTreeScreen.EachKindOfMarkIsDrawnInItsOwnColour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenMarkColoursTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeScreenTest;

	FTreeScreenScene Scene = MakeScene(/*bSoloSelfFound*/ false, /*bWithARun*/ false);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	UCataclysmEmpireTreeWidget* Screen = MakeScreen(Scene);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()) || !TestNotNull(TEXT("set-up: the node table"), Nodes)
		|| !TestNotNull(TEXT("set-up: the screen"), Screen))
	{
		return false;
	}

	// ONE NODE OF EACH KIND, read from the table: the first keystone, and the named ordinary node and capstone.
	FName Keystone;
	for (const TPair<FName, uint8*>& Pair : Nodes->GetRowMap())
	{
		const FCataclysmEmpireTreeNodeRow* Row = reinterpret_cast<const FCataclysmEmpireTreeNodeRow*>(Pair.Value);
		if (Row && UCataclysmPassiveTreeLayout::MarkFor(Row->Kind) == ECataclysmTreeMark::Keystone)
		{
			Keystone = Pair.Key;
			break;
		}
	}
	if (!TestFalse(TEXT("set-up: the tree has a keystone"), Keystone.IsNone()))
	{
		return false;
	}

	TMap<FName, FColor> Drawn;
	TArray<FString> Parts;
	const TPair<FName, FColor> Expected[] = {
		{StructuralIntegrity, FColor(110, 120, 135)}, {Keystone, FColor(150, 160, 175)}, {Foundations, FColor(200, 176, 120)}};
	for (const TPair<FName, FColor>& Each : Expected)
	{
		UCataclysmChoiceButton* Button = NewObject<UCataclysmChoiceButton>();
		Screen->DescribeNodeButtonForTests(*Button, Each.Key);
		const FColor Colour = Button->GetMarkColour().ToFColor(/*bSRGB=*/true);
		Drawn.Add(Each.Key, Colour);
		Parts.Add(FString::Printf(TEXT("%s %s"), *Each.Key.ToString(), *Colour.ToHex()));
		TestTrue(FString::Printf(TEXT("%s's button is given a mark colour"), *Each.Key.ToString()), Button->HasMarkColour());
		TestEqual(FString::Printf(TEXT("%s is drawn in the preview's colour for its kind"), *Each.Key.ToString()), Colour,
				  Each.Value);
	}
	AddInfo(FString::Printf(TEXT("MARKS: %s"), *FString::Join(Parts, TEXT("; "))));
	TestNotEqual(TEXT("a capstone and a keystone read as different kinds"), Drawn.FindRef(Foundations), Drawn.FindRef(Keystone));
	TestNotEqual(TEXT("and a keystone and an ordinary node"), Drawn.FindRef(Keystone), Drawn.FindRef(StructuralIntegrity));

	TestEqual(TEXT("the background is the preview's near-black"),
			  UCataclysmEmpireTreeLayout::BackdropColour().ToFColor(/*bSRGB=*/true), FColor(14, 16, 22, 255));

	UCataclysmChoiceButton* NeverDescribed = NewObject<UCataclysmChoiceButton>();
	TestFalse(TEXT("CONTROL: a button the screen never described has no mark colour"), NeverDescribed->HasMarkColour());
	return true;
}

/**
 * The builder's drawn elements, as the screen draws them: six rings centred on the middle, each drawn through points on
 * itself; four lines from the middle along the axes; the quadrant names; and the builder's colours. Prints "DRAWN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenDecorationsTest,
	"Cataclysm.EmpireTreeScreen.TheRingsLinesAndLabelsAreTheBuildersOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenDecorationsTest::RunTest(const FString&)
{
	const UDataTable* Decorations = UCataclysmEmpireTree::LoadDecorationTable();
	if (!TestNotNull(TEXT("set-up: the decoration table loads"), Decorations))
	{
		return false;
	}

	int32 Rings = 0;
	int32 Lines = 0;
	TArray<FString> Words;
	TArray<float> Radii;
	for (const TPair<FName, uint8*>& Pair : Decorations->GetRowMap())
	{
		const FCataclysmEmpireTreeDecorationRow* Row = reinterpret_cast<const FCataclysmEmpireTreeDecorationRow*>(Pair.Value);
		if (!Row)
		{
			continue;
		}
		if (Row->Kind == TEXT("circle"))
		{
			++Rings;
			Radii.Add(Row->Radius);
			TestTrue(FString::Printf(TEXT("the %.0f ring is centred on the middle"), Row->Radius),
					 FMath::IsNearlyZero(Row->CentreX) && FMath::IsNearlyZero(Row->CentreY));
			const TArray<FVector2D> Points = UCataclysmEmpireTreeLayout::RingPoints(FVector2D(Row->CentreX, Row->CentreY),
																					Row->Radius);
			TestEqual(FString::Printf(TEXT("the %.0f ring is drawn through its pieces' ends"), Row->Radius), Points.Num(),
					  UCataclysmEmpireTreeLayout::RingSegments(Row->Radius) + 1);
			bool bAllOnTheRing = Points.Num() > 0;
			for (const FVector2D& Point : Points)
			{
				bAllOnTheRing &= FMath::IsNearlyEqual(Point.Size(), static_cast<double>(Row->Radius), 0.01);
			}
			TestTrue(FString::Printf(TEXT("every point of the %.0f ring is on it"), Row->Radius), bAllOnTheRing);
			TestTrue(FString::Printf(TEXT("and the %.0f ring closes on itself"), Row->Radius),
					 Points.Num() > 1 && Points[0].Equals(Points.Last(), 0.01));
		}
		else if (Row->Kind == TEXT("line"))
		{
			++Lines;
			const FVector2D End = UCataclysmEmpireTreeLayout::LineEnd(*Row);
			TestTrue(FString::Printf(TEXT("the line at %.0f degrees runs 800 along an axis"), Row->AngleDegrees),
					 FMath::IsNearlyEqual(End.Size(), 800.0, 0.01)
						 && (FMath::IsNearlyZero(End.X, 0.01) || FMath::IsNearlyZero(End.Y, 0.01)));
			if (FMath::IsNearlyEqual(Row->AngleDegrees, -90.0f))
			{
				TestTrue(TEXT("the line at -90 degrees points up the panel, to a negative y"), End.Equals(FVector2D(0.0, -800.0), 0.01));
			}
		}
		else
		{
			Words.Add(Row->Text);
		}
	}
	Radii.Sort();
	AddInfo(FString::Printf(TEXT("DRAWN: %d rings (%s), %d lines, %d labels: %s"), Rings,
							*FString::JoinBy(Radii, TEXT(", "), [](float Radius) { return FString::SanitizeFloat(Radius); }),
							Lines, Words.Num(), *FString::Join(Words, TEXT(", "))));
	TestEqual(TEXT("six rings"), Rings, 6);
	TestEqual(TEXT("four lines"), Lines, 4);
	for (const TCHAR* Quadrant : {TEXT("ARCHITECT"), TEXT("EXPLORER"), TEXT("TREASURY"), TEXT("ARTISAN")})
	{
		TestTrue(FString::Printf(TEXT("a label names the %s quadrant"), Quadrant), Words.Contains(Quadrant));
	}

	// THE BUILDER'S COLOURS, READ AS A WEB PAGE'S ARE: "#6366F1" at an opacity of 1 is that colour, and half opacity is
	// half alpha. THE CONTROL: a colour that does not read is white rather than black or nothing.
	const FLinearColor Architect = UCataclysmEmpireTreeLayout::ColourOf(TEXT("#6366F1"), 1.0f);
	TestEqual(TEXT("#6366F1 reads as that colour"), Architect.ToFColor(true), FColor(0x63, 0x66, 0xF1, 0xFF));
	TestEqual(TEXT("half opacity is half alpha"), UCataclysmEmpireTreeLayout::ColourOf(TEXT("#6366F1"), 0.5f).A, 0.5f);
	TestEqual(TEXT("CONTROL: a colour that does not read is white"), UCataclysmEmpireTreeLayout::ColourOf(TEXT("blue"), 1.0f),
			  FLinearColor::White);
	TestEqual(TEXT("a 15 unit label at a zoom of 0.4 is 6 pixels, 5 of Slate's points"),
			  UCataclysmEmpireTreeLayout::LabelFontSize(15.0f, 0.4f), 5);
	// TWO, SINCE THE PACKAGED GAME DREW NO UPRIGHT LINE AT 1.5 (2026-11-27): see `ThinnestDecorationPx`.
	TestEqual(TEXT("a ring 1 unit thick is never thinner than 2 panel units"),
			  UCataclysmEmpireTreeLayout::DecorationThicknessPx(1.0f, 0.4f), 2.0f);
	TestEqual(TEXT("and a thick one keeps its thickness at the zoom"),
			  UCataclysmEmpireTreeLayout::DecorationThicknessPx(10.0f, 0.4f), 4.0f);
	return true;
}

// ---------------------------------------------------------------------------
// A click
// ---------------------------------------------------------------------------

/**
 * A click on a node buys a point through the game instance: the account record holds it, the bank falls by one, and the
 * screen's bank, branch and description lines say so; the node's box shows it held. A node the rules refuse is drawn
 * dimmed and still clickable, its tool tip says why, and a click on it says why and changes nothing. Prints "CLICK".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenClickTest,
	"Cataclysm.EmpireTreeScreen.AClickBuysAPointThroughTheGameInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenClickTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeScreenTest;

	FTreeScreenScene Scene = MakeScene(/*bSoloSelfFound*/ false, /*bWithARun*/ false);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	UCataclysmAccountSave* Account = Scene.Account();
	UCataclysmEmpireTreeWidget* Screen = MakeScreen(Scene);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: with its account record"), Account)
		|| !TestNotNull(TEXT("set-up: and the screen"), Screen))
	{
		return false;
	}
	Account->EmpireUpgradePoints = 3.5;

	TestTrue(TEXT("the Architect branch can be shown"), Screen->ShowBranch(TEXT("Architect")));
	TestFalse(TEXT("a name no view has cannot"), Screen->ShowBranch(TEXT("Nowhere")));
	TestEqual(TEXT("which leaves Architect shown"), Screen->GetShownBranch(), FString(TEXT("Architect")));
	TestEqual(TEXT("the screen draws every node of the tree in a branch's view too"), Screen->ShownNodes().Num(),
			  UCataclysmEmpireTree::LoadNodeTable()->GetRowMap().Num());
	TestTrue(TEXT("Structural Integrity among them"), Screen->ShownNodes().Contains(StructuralIntegrity));
	TestEqual(TEXT("the bank before"), Screen->BankText().ToString(),
			  FString(TEXT("3.5 empire upgrade points banked: 3 to spend")));

	const bool bBought = Screen->TouchNode(StructuralIntegrity);
	UCataclysmChoiceButton* Held = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeNodeButtonForTests(*Held, StructuralIntegrity);

	AddInfo(FString::Printf(TEXT("CLICK: Structural Integrity %s; \"%s\" / \"%s\" / \"%s\""),
							bBought ? TEXT("bought") : TEXT("refused"), *Screen->BankText().ToString(),
							*Screen->BranchText().ToString(), *Screen->DescriptionText().ToString()));
	TestTrue(TEXT("a click on Structural Integrity buys a point"), bBought);
	TestEqual(TEXT("which the account record holds"), Account->EmpireTree.PointsIn(StructuralIntegrity), 1);
	TestTrue(TEXT("paid from its bank, 3.5 to 2.5"), Account->EmpireUpgradePoints == 2.5);
	TestEqual(TEXT("the screen's bank says so"), Screen->BankText().ToString(),
			  FString(TEXT("2.5 empire upgrade points banked: 2 to spend")));
	TestEqual(TEXT("and its branch line"), Screen->BranchText().ToString(),
			  FString(TEXT("Architect: 1 point spent here, 1 in the whole tree")));
	TestTrue(TEXT("and the line under the branch describes the node touched"),
			 Screen->DescriptionText().ToString().StartsWith(TEXT("Structural Integrity (Architect, 1 of 10): ")));
	TestTrue(TEXT("nothing was refused"), Screen->RefusalText().IsEmpty());
	TestTrue(TEXT("its box shows it held"), Held->IsChosen());
	TestFalse(TEXT("and not dimmed, with points left to spend"), Held->IsDimmed());

	// A NODE THE RULES REFUSE: Merchant Guild waits on a market. Its edge is shut as well, so its tool tip and the
	// screen's refusal, which name the market, are what show the system is the reason, not the dimmed box alone.
	UCataclysmChoiceButton* Refused = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeNodeButtonForTests(*Refused, MerchantGuild);
	Screen->ShowBranch(TEXT("Treasury"));
	const bool bRefusedBought = Screen->TouchNode(MerchantGuild);
	AddInfo(FString::Printf(TEXT("CLICK: Merchant Guild %s, \"%s\"; its tool tip \"%s\""),
							bRefusedBought ? TEXT("bought") : TEXT("refused"), *Screen->RefusalText().ToString(),
							*Refused->GetToolTipText().ToString()));
	TestTrue(TEXT("Merchant Guild's box is dimmed"), Refused->IsDimmed());
	TestTrue(TEXT("and still clickable"), Refused->IsAvailable());
	TestTrue(TEXT("its tool tip says it waits on a market"),
			 Refused->GetToolTipText().ToString().Contains(TEXT("a market")));
	TestFalse(TEXT("a click on it buys nothing"), bRefusedBought);
	TestTrue(TEXT("and the screen says why"), Screen->RefusalText().ToString().Contains(TEXT("a market")));
	TestEqual(TEXT("the tree is as it was"), Account->EmpireTree.Total(), 1);
	TestTrue(TEXT("and the bank"), Account->EmpireUpgradePoints == 2.5);
	return true;
}

/**
 * The coordinating session's S2 of 2026-11-18: buying, choosing and respec are all refused during a walk, one rule for
 * the three, and the screen says why. A purchase in a walk spends nothing -- through the game instance and through a
 * click on the screen; a click on a capstone that has opened offers no options, and says why; Respec is refused on its
 * first press and passes no day. With nothing refused the screen says the same sentence while the walk lasts. And once
 * the walk has ended, the same click buys: the refusal was the walk's. Prints "WALK".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenWalkTest,
	"Cataclysm.EmpireTreeScreen.APurchaseDuringAWalkIsRefusedAndSpendsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenWalkTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeScreenTest;

	FTreeScreenScene Scene = MakeScene(/*bSoloSelfFound*/ false, /*bWithARun*/ true);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	UCataclysmAccountSave* Account = Scene.Account();
	UCataclysmEmpireRun* Run = Scene.Run();
	UCataclysmEmpireTreeWidget* Screen = MakeScreen(Scene);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: with its account record"), Account)
		|| !TestTrue(TEXT("set-up: and a run with a clock"), Run != nullptr && Run->Clock != nullptr)
		|| !TestNotNull(TEXT("set-up: and the screen"), Screen))
	{
		return false;
	}
	SpendTwentyFiveInArchitect(Account->EmpireTree);
	Account->EmpireUpgradePoints = 3.0;
	if (!TestTrue(TEXT("set-up: Foundations of the Empire has opened and awaits a choice"),
				  UCataclysmEmpireTree::AwaitsAnOptionChoice(UCataclysmEmpireTree::LoadNodeTable(), Account->EmpireTree,
															 Foundations)))
	{
		return false;
	}

	// IN A WALK: the clock says a dungeon is being stood in, as entering one does.
	const int32 DayBefore = Run->Day();
	Run->Clock->EnterDungeon(/*DungeonId*/ 7);
	TestTrue(TEXT("CONTROL: the run says a walk is under way"), Run->IsAWalkUnderWay());

	// THROUGH THE GAME INSTANCE, which every way of buying goes through. MASONRY TECHNIQUES, which holds 7 of 15 and
	// could take a point outside a walk: Structural Integrity is full here, and a refusal for that would prove nothing.
	FString BuyReason;
	const bool bBoughtDirectly = UCataclysmGameInstance::BuyEmpireTreePoint(Scene.World, MasonryTechniques, BuyReason);
	FString ChooseReason;
	const bool bChoseDirectly = UCataclysmGameInstance::ChooseEmpireTreeOption(Scene.World, Foundations, 2, ChooseReason);

	// THROUGH THE SCREEN.
	Screen->ShowBranch(TEXT("Architect"));
	const FString Unprompted = Screen->RefusalText().ToString();
	const bool bClicked = Screen->TouchNode(MasonryTechniques);
	const FString AfterTheClick = Screen->RefusalText().ToString();
	Screen->ShowBranch(TEXT("Centre"));
	const bool bCapstoneClicked = Screen->TouchNode(Foundations);
	const FName Offered = Screen->GetCapstoneAwaitingAChoice();
	const FString AfterTheCapstone = Screen->RefusalText().ToString();
	const bool bRespecced = Screen->PressRespec();
	const bool bAwaitingAfterTheRespec = Screen->IsAwaitingTheSecondRespecPress();
	const FString AfterTheRespec = Screen->RefusalText().ToString();

	const int32 PointsInTheWalk = Account->EmpireTree.Total();
	const double BankInTheWalk = Account->EmpireUpgradePoints;
	const int32 ChosenInTheWalk = Account->EmpireTree.ChosenOptionIn(Foundations);
	const int32 DayInTheWalk = Run->Day();

	// AND ONCE THE WALK HAS ENDED.
	Run->Clock->LeaveDungeon();
	Screen->ShowBranch(TEXT("Architect"));
	const FString AfterTheWalk = Screen->RefusalText().ToString();
	const bool bClickedAfter = Screen->TouchNode(MasonryTechniques);

	AddInfo(FString::Printf(TEXT("WALK: bought directly %s (\"%s\"); chose directly %s (\"%s\"); unprompted \"%s\"; a "
								 "click %s, \"%s\"; the capstone %s, offering %s, \"%s\"; Respec %s, awaiting %s, \"%s\"; "
								 "%d point(s), %g banked, option %d, day %d to %d; after the walk \"%s\", a click %s"),
							bBoughtDirectly ? TEXT("yes") : TEXT("no"), *BuyReason,
							bChoseDirectly ? TEXT("yes") : TEXT("no"), *ChooseReason, *Unprompted,
							bClicked ? TEXT("bought") : TEXT("refused"), *AfterTheClick,
							bCapstoneClicked ? TEXT("took") : TEXT("refused"), *Offered.ToString(), *AfterTheCapstone,
							bRespecced ? TEXT("done") : TEXT("refused"), bAwaitingAfterTheRespec ? TEXT("yes") : TEXT("no"),
							*AfterTheRespec, PointsInTheWalk, BankInTheWalk, ChosenInTheWalk, DayBefore, DayInTheWalk,
							*AfterTheWalk, bClickedAfter ? TEXT("bought") : TEXT("refused")));

	TestFalse(TEXT("a purchase through the game instance during a walk is refused"), bBoughtDirectly);
	TestEqual(TEXT("saying it is changed in the capital"), BuyReason, FString(WalkRefusal));
	TestFalse(TEXT("so is choosing a capstone's option"), bChoseDirectly);
	TestEqual(TEXT("with the same sentence"), ChooseReason, FString(WalkRefusal));
	TestEqual(TEXT("the screen says it before anything is clicked"), Unprompted, FString(WalkRefusal));
	TestFalse(TEXT("a click on a node buys nothing"), bClicked);
	TestEqual(TEXT("and the screen says why"), AfterTheClick, FString(WalkRefusal));
	TestFalse(TEXT("a click on the capstone that has opened takes nothing"), bCapstoneClicked);
	TestTrue(TEXT("and offers no options"), Offered.IsNone());
	TestEqual(TEXT("saying why"), AfterTheCapstone, FString(WalkRefusal));
	TestFalse(TEXT("Respec is refused"), bRespecced);
	TestFalse(TEXT("on its first press, asking for no second"), bAwaitingAfterTheRespec);
	TestEqual(TEXT("saying why"), AfterTheRespec, FString(WalkRefusal));
	TestEqual(TEXT("NOTHING SPENT: the tree holds its 25 points"), PointsInTheWalk, 25);
	TestTrue(TEXT("and the bank its 3"), BankInTheWalk == 3.0);
	TestEqual(TEXT("and no option is chosen"), ChosenInTheWalk, 0);
	TestEqual(TEXT("and no day passed"), DayInTheWalk, DayBefore);

	TestTrue(TEXT("CONTROL: after the walk the screen refuses nothing"), AfterTheWalk.IsEmpty());
	TestTrue(TEXT("and the same click buys"), bClickedAfter);
	TestEqual(TEXT("an eighth point in Masonry Techniques"), Account->EmpireTree.PointsIn(MasonryTechniques), 8);
	TestTrue(TEXT("paid from the bank"), Account->EmpireUpgradePoints == 2.0);
	return true;
}

// ---------------------------------------------------------------------------
// A capstone's options
// ---------------------------------------------------------------------------

/**
 * A click on a centre capstone that has opened puts its three options in the list rather than buying, and says so;
 * its box is not dimmed and carries no refusal. Choosing an option goes through the game instance and stands; the next
 * click on the capstone buys its point. The option values the list's buttons carry come back as their numbers, and a
 * branch's name is no option. Prints "CAPSTONE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenCapstoneTest,
	"Cataclysm.EmpireTreeScreen.ACapstoneClickOffersItsOptionsAndAChoiceStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenCapstoneTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeScreenTest;

	TestEqual(TEXT("an option's value comes back as its number"),
			  UCataclysmEmpireTreeWidget::OptionFromValue(UCataclysmEmpireTreeWidget::OptionValue(3)), 3);
	TestEqual(TEXT("and a branch's name is no option"), UCataclysmEmpireTreeWidget::OptionFromValue(FName(TEXT("Centre"))),
			  0);

	FTreeScreenScene Scene = MakeScene(/*bSoloSelfFound*/ false, /*bWithARun*/ false);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	UCataclysmAccountSave* Account = Scene.Account();
	UCataclysmEmpireTreeWidget* Screen = MakeScreen(Scene);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: with its account record"), Account)
		|| !TestNotNull(TEXT("set-up: and the screen"), Screen))
	{
		return false;
	}
	SpendTwentyFiveInArchitect(Account->EmpireTree);
	Account->EmpireUpgradePoints = 2.0;

	Screen->ShowBranch(TEXT("Centre"));
	UCataclysmChoiceButton* Box = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeNodeButtonForTests(*Box, Foundations);

	const bool bOffered = Screen->TouchNode(Foundations);
	const FName Awaiting = Screen->GetCapstoneAwaitingAChoice();
	const FString Offer = Screen->RefusalText().ToString();
	const int32 PointsAfterTheOffer = Account->EmpireTree.PointsIn(Foundations);

	const bool bChosen = Screen->ChooseOption(Foundations, 2);
	const FString Described = Screen->DescriptionText().ToString();
	const bool bBought = Screen->TouchNode(Foundations);

	AddInfo(FString::Printf(TEXT("CAPSTONE: offered %s (%s), \"%s\"; chosen %s; \"%s\"; then a click %s, %d point(s), "
								 "option %d, %g banked"),
							bOffered ? TEXT("yes") : TEXT("no"), *Awaiting.ToString(), *Offer,
							bChosen ? TEXT("yes") : TEXT("no"), *Described, bBought ? TEXT("bought") : TEXT("refused"),
							Account->EmpireTree.PointsIn(Foundations), Account->EmpireTree.ChosenOptionIn(Foundations),
							Account->EmpireUpgradePoints));

	TestFalse(TEXT("the capstone's box is not dimmed while it awaits a choice"), Box->IsDimmed());
	TestTrue(TEXT("and carries no refusal"), Box->GetToolTipText().IsEmpty());
	TestTrue(TEXT("a click on it offers its options"), bOffered);
	TestEqual(TEXT("for Foundations of the Empire"), Awaiting, Foundations);
	TestTrue(TEXT("and says to take one"), Offer.StartsWith(TEXT("Foundations of the Empire has opened.")));
	TestEqual(TEXT("buying nothing"), PointsAfterTheOffer, 0);
	TestTrue(TEXT("the second option is chosen"), bChosen);
	TestEqual(TEXT("and the tree holds it"), Account->EmpireTree.ChosenOptionIn(Foundations), 2);
	TestTrue(TEXT("the line names it chosen"), Described.Contains(TEXT("2 The Delver (chosen)")));
	TestTrue(TEXT("the next click buys the capstone's point"), bBought);
	TestEqual(TEXT("which the tree holds"), Account->EmpireTree.PointsIn(Foundations), 1);
	TestTrue(TEXT("paid from the bank"), Account->EmpireUpgradePoints == 1.0);
	TestTrue(TEXT("and the options have left the list"), Screen->GetCapstoneAwaitingAChoice().IsNone());
	return true;
}

// ---------------------------------------------------------------------------
// Respec
// ---------------------------------------------------------------------------

/**
 * Respec takes two presses, for the class tree screen's reason that it costs days and cannot be undone: the first asks
 * for the second and says what it costs; any other press between cancels; the second returns every point to the bank
 * and takes the mode's days off the run. A respec the rules refuse -- nothing spent -- is refused on the first press,
 * asking for no second. Prints "RESPEC".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenRespecTest,
	"Cataclysm.EmpireTreeScreen.RespecTakesTwoPressesAndAnyOtherPressCancelsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenRespecTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeScreenTest;

	FTreeScreenScene Scene = MakeScene(/*bSoloSelfFound*/ false, /*bWithARun*/ true);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	UCataclysmAccountSave* Account = Scene.Account();
	UCataclysmEmpireRun* Run = Scene.Run();
	UCataclysmEmpireTreeWidget* Screen = MakeScreen(Scene);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: with its account record"), Account)
		|| !TestNotNull(TEXT("set-up: and a run"), Run)
		|| !TestNotNull(TEXT("set-up: and the screen"), Screen))
	{
		return false;
	}
	const int32 Days = UCataclysmEmpireTree::RespecDays(static_cast<int32>(ECataclysmLethality::Standard));
	Screen->ShowBranch(TEXT("Architect"));

	// NOTHING SPENT: refused on the first press.
	const bool bEmpty = Screen->PressRespec();
	const bool bEmptyAwaiting = Screen->IsAwaitingTheSecondRespecPress();
	const FString EmptyWhy = Screen->RefusalText().ToString();

	Account->EmpireTree.Add(StructuralIntegrity, 2);
	Account->EmpireUpgradePoints = 1.0;
	const int32 DayBefore = Run->Day();

	// A FIRST PRESS, THEN ANOTHER PRESS: cancelled.
	const bool bFirst = Screen->PressRespec();
	const bool bAwaiting = Screen->IsAwaitingTheSecondRespecPress();
	const FString Asking = Screen->RespecText().ToString();
	Screen->ShowBranch(TEXT("Explorer"));
	const bool bAwaitingAfterAnotherPress = Screen->IsAwaitingTheSecondRespecPress();
	const FString Cancelled = Screen->RespecText().ToString();
	const int32 PointsAfterTheCancel = Account->EmpireTree.Total();

	// A FIRST PRESS, THEN A CLICK ON A NODE: cancelled too, as well as buying what it buys.
	Screen->PressRespec();
	Screen->ShowBranch(TEXT("Architect"));
	Screen->PressRespec();
	Screen->TouchNode(StructuralIntegrity);
	const bool bAwaitingAfterAClick = Screen->IsAwaitingTheSecondRespecPress();

	// TWO PRESSES.
	const bool bAsked = Screen->PressRespec();
	const bool bDone = Screen->PressRespec();
	const int32 DayAfter = Run->Day();

	AddInfo(FString::Printf(TEXT("RESPEC: nothing spent %s, awaiting %s, \"%s\"; a first press %s, awaiting %s, \"%s\"; "
								 "another press leaves it awaiting %s, \"%s\"; a click leaves it awaiting %s; two presses "
								 "%s, %d point(s) and %g banked, day %d to %d (%d days)"),
							bEmpty ? TEXT("done") : TEXT("refused"), bEmptyAwaiting ? TEXT("yes") : TEXT("no"), *EmptyWhy,
							bFirst ? TEXT("done") : TEXT("asked"), bAwaiting ? TEXT("yes") : TEXT("no"), *Asking,
							bAwaitingAfterAnotherPress ? TEXT("yes") : TEXT("no"), *Cancelled,
							bAwaitingAfterAClick ? TEXT("yes") : TEXT("no"), bDone ? TEXT("done") : TEXT("refused"),
							Account->EmpireTree.Total(), Account->EmpireUpgradePoints, DayBefore, DayAfter, Days));

	TestFalse(TEXT("with nothing spent a press respecs nothing"), bEmpty);
	TestFalse(TEXT("and asks for no second"), bEmptyAwaiting);
	TestTrue(TEXT("saying there is nothing to respec"), EmptyWhy.Contains(TEXT("nothing to respec")));

	TestFalse(TEXT("a first press respecs nothing"), bFirst);
	TestTrue(TEXT("and waits for a second"), bAwaiting);
	TestEqual(TEXT("saying what it costs"), Asking,
			  FString::Printf(TEXT("Press again to respec: %d days of the run"), Days));
	TestFalse(TEXT("another press cancels it"), bAwaitingAfterAnotherPress);
	TestEqual(TEXT("and the button says so"), Cancelled, FString::Printf(TEXT("Respec: %d days of the run"), Days));
	TestEqual(TEXT("keeping the tree"), PointsAfterTheCancel, 2);
	TestFalse(TEXT("a click on a node cancels it as well"), bAwaitingAfterAClick);

	TestFalse(TEXT("of two presses, the first asks"), bAsked);
	TestTrue(TEXT("and the second respecs"), bDone);
	TestEqual(TEXT("every point is back"), Account->EmpireTree.Total(), 0);
	TestTrue(TEXT("in the bank: 1 left, the click's 1 spent and 3 returned"), Account->EmpireUpgradePoints == 3.0);
	TestEqual(TEXT("and the mode's days were taken off the run"), DayAfter, DayBefore + Days);
	TestFalse(TEXT("and it waits for nothing"), Screen->IsAwaitingTheSecondRespecPress());
	return true;
}

namespace CataclysmEmpireTreeScreenTest
{
	/** The PLAY LOOP lines said while it lives, in order, without their prefix. Issue #2857. */
	struct FRespecDaysLines : public FOutputDevice
	{
		TArray<FString> Lines;

		FRespecDaysLines()
		{
			if (GLog)
			{
				GLog->AddOutputDevice(this);
			}
		}

		virtual ~FRespecDaysLines() override
		{
			if (GLog)
			{
				GLog->RemoveOutputDevice(this);
			}
		}

		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const FString Line(Text);
			if (Category == FName(TEXT("LogCataclysm")) && Line.StartsWith(FCataclysmPlayLoopLog::Prefix))
			{
				Lines.Add(Line.RightChop(FCString::Strlen(FCataclysmPlayLoopLog::Prefix)));
			}
		}

		/** What was said, after the log is flushed: a line written off the game thread is buffered until then. */
		const TArray<FString>& Said()
		{
			if (GLog)
			{
				GLog->Flush();
			}
			return Lines;
		}
	};
}

/**
 * Issue #2857: the respec spends days in the capital as a forge craft does, and says the same about them. Its first
 * press says which dungeons' timers run out within its days, the city each would hit and the day; its second says
 * what the days did, on the screen and in a PLAY LOOP line after the respec's own, "for the respec, days 0 to 5:
 * dungeons resolved: ...". Until then the respec counted what resolved in a log line the PLAY LOOP filter hides, and
 * the screen said nothing. One ordinary dungeon is due in three days and every other timer in 60. Prints "RESPEC DAYS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenRespecDaysTest,
	"Cataclysm.EmpireTreeScreen.ARespecSaysWhichTimersItsDaysRunOutAndThenWhatTheyDid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenRespecDaysTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeScreenTest;

	FTreeScreenScene Scene = MakeScene(/*bSoloSelfFound*/ false, /*bWithARun*/ true);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	UCataclysmAccountSave* Account = Scene.Account();
	UCataclysmEmpireRun* Run = Scene.Run();
	UCataclysmEmpireTreeWidget* Screen = MakeScreen(Scene);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: with its account record"), Account)
		|| !TestTrue(TEXT("set-up: and a run with a clock and a map"),
					 Run != nullptr && Run->Clock != nullptr && Run->Map != nullptr)
		|| !TestNotNull(TEXT("set-up: and the screen"), Screen))
	{
		return false;
	}

	// ONE ORDINARY DUNGEON ON A STANDING CITY -- not a bonus dungeon, not a Void one, whose hit takes no defence -- due in
	// three days, inside the respec's five; every other timer in 60. The run's opening wave stands already.
	int32 Due = INDEX_NONE;
	for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
	{
		const FCataclysmCity* City = Run->Map->Find(Dungeon.CityId);
		if (Dungeon.Type == ECataclysmDungeonType::Basic && !Dungeon.bBonus && !Dungeon.AddsVoidStacks()
			&& City != nullptr && !City->bFallen)
		{
			Due = Dungeon.DungeonId;
			break;
		}
	}
	const FCataclysmDungeon* Dungeon = Due != INDEX_NONE ? Run->FindDungeon(Due) : nullptr;
	const FCataclysmCity* City = Dungeon != nullptr ? Run->Map->Find(Dungeon->CityId) : nullptr;
	if (!TestNotNull(TEXT("set-up: the opening wave stood an ordinary dungeon on a standing city"), City))
	{
		return false;
	}
	for (FCataclysmDungeonTimer& Timer : Run->Clock->Timers)
	{
		Timer.DaysUntilResolve = Timer.DungeonId == Due ? 3.0f : 60.0f;
	}
	const FString DueName = Run->NameOfDungeon(Due);
	const FString CityName = City->Name;
	const int32 Days = UCataclysmEmpireTree::RespecDays(static_cast<int32>(ECataclysmLethality::Standard));
	const int32 Today = Run->Day();

	Account->EmpireTree.Add(StructuralIntegrity, 1);
	Screen->ShowBranch(TEXT("Architect"));

	FRespecDaysLines Capture;
	const bool bAsked = Screen->PressRespec();
	const FString Warned = Screen->RefusalText().ToString();
	const bool bDone = Screen->PressRespec();
	const FString After = Screen->RefusalText().ToString();
	const TArray<FString> Said = Capture.Said();

	const FString Lead = FString::Printf(TEXT("for the respec, days %d to %d: "), Today, Today + Days);
	// THE DUNGEON BY ITS NUMBER AND NAME, which only the resolved part says, and its city among those hit, which a Siege's
	// daily share may also be.
	const FString Resolved = FString::Printf(TEXT("%d (%s)"), Due, *DueName);
	const FString Hit = FString::Printf(TEXT("%s lost "), *CityName);
	const int32 RespecAt = Said.IndexOfByPredicate([](const FString& Line)
	{
		return Line.StartsWith(TEXT("respecced the empire upgrade tree: "));
	});
	const int32 AccountAt = Said.IndexOfByPredicate([&Lead](const FString& Line) { return Line.StartsWith(Lead); });
	AddInfo(FString::Printf(TEXT("RESPEC DAYS: dungeon %d (%s) due in 3 on %s, respec %d days; asked %s \"%s\"; done %s "
								 "\"%s\"; said: %s"),
							Due, *DueName, *CityName, Days, bAsked ? TEXT("DONE") : TEXT("no"), *Warned,
							bDone ? TEXT("yes") : TEXT("NO"), *After, *FString::Join(Said, TEXT(" | "))));

	TestTrue(TEXT("set-up: the respec costs more than three days"), Days >= 3);
	TestFalse(TEXT("the first press asks"), bAsked);
	TestEqual(TEXT("naming the dungeon whose timer its days run out, the city it hits and the day"), Warned,
			  FString::Printf(TEXT("Within %d days, 1 dungeon's timer runs out: dungeon %d (%s) hits %s on day %d."), Days,
							  Due, *DueName, *CityName, Today + 3));
	TestTrue(TEXT("the second respecs"), bDone);
	TestTrue(TEXT("and the screen says what the days did: the dungeon resolved"),
			 After.StartsWith(FString::Printf(TEXT("Meanwhile, days %d to %d: "), Today, Today + Days))
				 && After.Contains(Resolved));
	TestTrue(TEXT("and the city it hit"), After.Contains(Hit));
	if (TestTrue(TEXT("a PLAY LOOP line says what the respec's days did"), Said.IsValidIndex(AccountAt)))
	{
		TestTrue(TEXT("naming the dungeon and the city"), Said[AccountAt].Contains(Resolved) && Said[AccountAt].Contains(Hit));
	}
	TestTrue(TEXT("after the respec's own line"), RespecAt != INDEX_NONE && AccountAt > RespecAt);
	return true;
}

// ---------------------------------------------------------------------------
// A Solo Self-Found character's own tree
// ---------------------------------------------------------------------------

/**
 * A Solo Self-Found character's screen shows and spends its own tree and bank, which its player state holds (slice
 * 1c), and says they are its own; the account record of its partition is not touched. Prints "OWN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenOwnTreeTest,
	"Cataclysm.EmpireTreeScreen.ASoloSelfFoundCharactersOwnTreeIsTheOneShown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenOwnTreeTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeScreenTest;

	FTreeScreenScene Scene = MakeScene(/*bSoloSelfFound*/ true, /*bWithARun*/ false);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	UCataclysmAccountSave* Account = Scene.Account();
	UCataclysmEmpireTreeWidget* Screen = MakeScreen(Scene);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: and the screen"), Screen))
	{
		return false;
	}
	Scene.State->SetOwnEmpireTree(2.0, FCataclysmPassiveAllocation());
	const double AccountBankBefore = Account != nullptr ? Account->EmpireUpgradePoints : -1.0;
	const int32 AccountTreeBefore = Account != nullptr ? Account->EmpireTree.Total() : -1;

	Screen->ShowBranch(TEXT("Architect"));
	const FString Before = Screen->BankText().ToString();
	const bool bBought = Screen->TouchNode(StructuralIntegrity);
	const FString After = Screen->BankText().ToString();

	AddInfo(FString::Printf(TEXT("OWN: \"%s\", a click %s, \"%s\"; its own tree %d point(s), %g banked; the account's "
								 "%d point(s), %g banked"),
							*Before, bBought ? TEXT("bought") : TEXT("refused"), *After,
							Scene.State->GetOwnEmpireTree().Total(), Scene.State->GetOwnEmpireUpgradePoints(),
							Account != nullptr ? Account->EmpireTree.Total() : -1,
							Account != nullptr ? Account->EmpireUpgradePoints : -1.0));

	TestEqual(TEXT("the screen shows the character's own bank and says so"), Before,
			  FString(TEXT("2.0 empire upgrade points banked: 2 to spend; this character's own")));
	TestTrue(TEXT("a click buys"), bBought);
	TestEqual(TEXT("in its own tree"), Scene.State->GetOwnEmpireTree().PointsIn(StructuralIntegrity), 1);
	TestTrue(TEXT("from its own bank"), Scene.State->GetOwnEmpireUpgradePoints() == 1.0);
	TestEqual(TEXT("which the screen says"), After,
			  FString(TEXT("1.0 empire upgrade points banked: 1 to spend; this character's own")));
	if (Account != nullptr)
	{
		TestEqual(TEXT("the account record's tree is not touched"), Account->EmpireTree.Total(), AccountTreeBefore);
		TestTrue(TEXT("nor its bank"), Account->EmpireUpgradePoints == AccountBankBefore);
	}
	return true;
}

// ---------------------------------------------------------------------------
// The empire map's button
// ---------------------------------------------------------------------------

/**
 * The "Empire upgrade tree" button is two cities wide and one tall, a margin in from the top and the right: the boss
 * dungeon marker's corner, mirrored. On the panels the marker's test uses it is inside the panel and covers no city,
 * nor the marker or the Walk box under it. Its label measures narrower than it at full size, as the marker's does;
 * that half is skipped, and says so, when there is no Slate renderer. Prints "TREE BUTTON".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenMapButtonPlaceTest,
	"Cataclysm.EmpireTreeScreen.TheMapButtonSitsInTheEmptyTopRightCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenMapButtonPlaceTest::RunTest(const FString&)
{
	// AT A SCALE OF ONE ON A 1600 BY 900 PANEL: 344 by 48, its centre 24 + 172 in from the right and 24 + 24 down.
	const FVector2D AtOne = UCataclysmEmpireMapLayout::EmpireTreeButtonPosition(1.0f, FVector2D(1600.0, 900.0));
	const FVector2D SizeAtOne = UCataclysmEmpireMapLayout::EmpireTreeButtonSize(1.0f);
	TestEqual(TEXT("at a scale of one it is two cities wide"), SizeAtOne.X, 344.0, 0.01);
	TestEqual(TEXT("and one tall"), SizeAtOne.Y, 48.0, 0.01);
	TestEqual(TEXT("its centre is a margin and half its width from the right"), AtOne.X, 1600.0 - 196.0, 0.01);
	TestEqual(TEXT("and a margin and half its height from the top"), AtOne.Y, 48.0, 0.01);

	UCataclysmEmpireMap* Map = NewObject<UCataclysmEmpireMap>();
	Map->Build();

	const FVector2D Panels[] = {
		UCataclysmEmpireMapLayout::DiamondSize()
			+ FVector2D(2.0 * UCataclysmEmpireMapLayout::FitMarginPx, 2.0 * UCataclysmEmpireMapLayout::FitMarginPx),
		FVector2D(1600.0, 900.0),
		FVector2D(700.0, 1200.0),
		FVector2D(1852.0, 528.0),
	};

	// TWO RECTANGLES PLACED BY THEIR CENTRES MISS EACH OTHER when they are clear on either axis by half of both.
	auto Clear = [](const FVector2D& AtA, const FVector2D& SizeA, const FVector2D& AtB, const FVector2D& SizeB)
	{
		const FVector2D Gap = (AtA - AtB).GetAbs();
		return Gap.X >= (SizeA.X + SizeB.X) * 0.5 - 0.01 || Gap.Y >= (SizeA.Y + SizeB.Y) * 0.5 - 0.01;
	};

	int32 Checked = 0;
	for (const FVector2D& Panel : Panels)
	{
		const float Scale = UCataclysmEmpireMapLayout::ScaleToFit(Panel);
		const FVector2D Box = UCataclysmEmpireMapLayout::CitySize(Scale);
		const FVector2D Button = UCataclysmEmpireMapLayout::EmpireTreeButtonSize(Scale);
		const FVector2D At = UCataclysmEmpireMapLayout::EmpireTreeButtonPosition(Scale, Panel);

		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel the button is inside the panel"), Panel.X, Panel.Y),
				 At.X - Button.X * 0.5 >= -0.01 && At.Y - Button.Y * 0.5 >= -0.01
					 && At.X + Button.X * 0.5 <= Panel.X + 0.01 && At.Y + Button.Y * 0.5 <= Panel.Y + 0.01);

		for (const FCataclysmCity& City : Map->Cities)
		{
			TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel the button covers no part of %s"), Panel.X, Panel.Y,
									 *City.Name),
					 Clear(UCataclysmEmpireMapLayout::PositionFor(City.R, City.C, Scale, Panel), Box, At, Button));
			++Checked;
		}

		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel it covers no part of the boss marker"), Panel.X, Panel.Y),
				 Clear(UCataclysmEmpireMapLayout::CataclysmDungeonMarkerPosition(Scale),
					   UCataclysmEmpireMapLayout::CataclysmDungeonMarkerSize(Scale), At, Button));
		TestTrue(FString::Printf(TEXT("on a %.0f by %.0f panel nor of the Walk box"), Panel.X, Panel.Y),
				 Clear(UCataclysmEmpireMapLayout::CataclysmDungeonWalkPosition(Scale),
					   UCataclysmEmpireMapLayout::CataclysmDungeonWalkSize(Scale), At, Button));
	}
	AddInfo(FString::Printf(TEXT("TREE BUTTON: %d city placements checked against the button"), Checked));
	TestEqual(TEXT("twenty-five cities on each of four panels"), Checked, 100);

	// ---- the label fits ---------------------------------------------------
	//
	// THE BOSS MARKER'S ARITHMETIC: the designed 16 points of `WBP_ChoiceButton`'s text block, the button's padding
	// allowed for as 16 pixels, at full size.
	if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("no Slate renderer, so no font can be measured; where the empire tree's button sits is still checked, "
				 "but whether its label FITS is not checked at all"));
		return true;
	}

	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const UTextBlock* Probe = NewObject<UTextBlock>();
	if (!TestNotNull(TEXT("a text block was created to read its default font"), Probe))
	{
		return false;
	}
	FSlateFontInfo Font = Probe->GetFont();
	Font.Size = 16;
	const float PaddingAllowancePx = 16.0f;

	const FString Words = UCataclysmEmpireMapLayout::EmpireTreeButtonText();
	const float Needed = static_cast<float>(Measure->Measure(Words, Font).X) + PaddingAllowancePx;
	AddInfo(FString::Printf(TEXT("TREE BUTTON: \"%s\" needs %.0f pixels of %.0f"), *Words, Needed,
							UCataclysmEmpireMapLayout::EmpireTreeButtonWidthPx));
	TestTrue(FString::Printf(TEXT("at full size, \"%s\" needs %.0f pixels and the button is %.0f wide"), *Words, Needed,
							 UCataclysmEmpireMapLayout::EmpireTreeButtonWidthPx),
			 Needed <= UCataclysmEmpireMapLayout::EmpireTreeButtonWidthPx);
	return true;
}

/**
 * A press of the map's "Empire upgrade tree" button asks for the tree screen -- counted, because a headless test has no
 * player controller to open it -- and is no city's press: the last city clicked is unchanged. A city's press asks for
 * no tree. The button's words are "Empire upgrade tree". Prints "TREE BUTTON PRESS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenMapButtonPressTest,
	"Cataclysm.EmpireTreeScreen.TheMapButtonAsksForTheTreeAndIsNoCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenMapButtonPressTest::RunTest(const FString&)
{
	TestEqual(TEXT("the button's words"), UCataclysmEmpireMapLayout::EmpireTreeButtonText(),
			  FString(TEXT("Empire upgrade tree")));

	UCataclysmEmpireMapWidget* Screen = NewObject<UCataclysmEmpireMapWidget>();
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	if (!TestNotNull(TEXT("set-up: the map screen was created"), Screen)
		|| !TestNotNull(TEXT("set-up: and a run"), Run))
	{
		return false;
	}
	const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten2; // Issue #2928: the Cataclysms this run faces are named.
	Run->Begin(3);
	Screen->SetRunForTests(Run);

	const int32 Before = Screen->EmpireTreeRequestCount();
	Screen->ClickEmpireTreeForTests();
	const int32 AfterTheButton = Screen->EmpireTreeRequestCount();
	const int32 CityAfterTheButton = Screen->LastClickedCityId();
	Screen->ClickCityForTests(0);
	const int32 AfterACity = Screen->EmpireTreeRequestCount();

	AddInfo(FString::Printf(TEXT("TREE BUTTON PRESS: requests %d, then %d after the button (city %d), %d after city 0 "
								 "(city %d)"),
							Before, AfterTheButton, CityAfterTheButton, AfterACity, Screen->LastClickedCityId()));
	TestEqual(TEXT("no request before the button is pressed"), Before, 0);
	TestEqual(TEXT("one when it is"), AfterTheButton, 1);
	TestEqual(TEXT("and no city was clicked"), CityAfterTheButton, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("CONTROL: a city's press asks for no tree"), AfterACity, 1);
	TestEqual(TEXT("and is that city's"), Screen->LastClickedCityId(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
