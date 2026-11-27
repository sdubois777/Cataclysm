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
 * the screen through the player controller, which loads that Blueprint; the first view, the Architect branch, which
 * `NativeConstruct` chooses and only a constructed Blueprint runs; and what a bought node does (slices 3 to 5).
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
 * Every sentence the screen shows comes from `UCataclysmEmpireTreeLayout`, and these are its words: the five views in
 * the file's order with the centre named "The centre", the bank (an account's, and a Solo Self-Found character's own),
 * a branch's points, a node's box and its line, a capstone's options, and the Respec button's two faces. Prints
 * "WORDS".
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
	TestEqual(TEXT("five views, the four branches and the centre, in the file's order"), Views,
			  TArray<FString>({ TEXT("Architect"), TEXT("Explorer"), TEXT("Treasury"), TEXT("Artisan"), TEXT("Centre") }));
	TestEqual(TEXT("the centre's view is named for a person"), UCataclysmEmpireTreeLayout::ViewLabel(TEXT("Centre")),
			  FString(TEXT("The centre")));
	TestEqual(TEXT("a branch's view by its own name"), UCataclysmEmpireTreeLayout::ViewLabel(TEXT("Explorer")),
			  FString(TEXT("Explorer")));

	TestEqual(TEXT("the bank, with the whole points that can be spent"), UCataclysmEmpireTreeLayout::BankLine(3.5, false),
			  FString(TEXT("3.5 empire upgrade points banked: 3 to spend")));
	TestEqual(TEXT("a Solo Self-Found character's bank says it is its own"),
			  UCataclysmEmpireTreeLayout::BankLine(1.0, true),
			  FString(TEXT("1.0 empire upgrade points banked: 1 to spend; this character's own")));

	TestEqual(TEXT("a branch's points, one"), UCataclysmEmpireTreeLayout::BranchLine(TEXT("Centre"), 1, 26),
			  FString(TEXT("The centre: 1 point spent here, 26 in the whole tree")));
	TestEqual(TEXT("and more than one"), UCataclysmEmpireTreeLayout::BranchLine(TEXT("Architect"), 3, 3),
			  FString(TEXT("Architect: 3 points spent here, 3 in the whole tree")));

	TestEqual(TEXT("a node's box: its name and points"), UCataclysmEmpireTreeLayout::NodeLabel(*Integrity, 2),
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
// The branches as drawn
// ---------------------------------------------------------------------------

/**
 * Over the generated tables: every view holds nodes, every edge drawn in a view joins two of that view's nodes, and
 * the views between them draw every edge in the file, so none is lost by drawing one branch at a time. Each view's
 * node scale is above nought and at most one, and at that scale no two of its node boxes overlap. Prints "BRANCHES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeScreenBranchesTest,
	"Cataclysm.EmpireTreeScreen.EveryBranchHoldsItsOwnNodesAndEdgesAndFits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeScreenBranchesTest::RunTest(const FString&)
{
	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Edges = UCataclysmEmpireTree::LoadEdgeTable();
	if (!TestNotNull(TEXT("set-up: the generated node table loads"), Nodes)
		|| !TestNotNull(TEXT("set-up: and the edge table"), Edges))
	{
		return false;
	}

	int32 EdgesDrawn = 0;
	int32 NodesDrawn = 0;
	TArray<FString> Parts;
	for (const FString& View : UCataclysmEmpireTreeLayout::Views())
	{
		const TArray<FName> InView = UCataclysmEmpireTree::NodesIn(Nodes, View);
		const TArray<TPair<FName, FName>> Lines = UCataclysmEmpireTreeLayout::EdgesIn(Nodes, Edges, View);
		const FCataclysmTreeExtent Extent = UCataclysmEmpireTreeLayout::ExtentOf(Nodes, View);
		const float Scale = UCataclysmEmpireTreeLayout::NodeScaleFor(Nodes, View);
		Parts.Add(FString::Printf(TEXT("%s %d nodes, %d edges, scale %.3f"), *View, InView.Num(), Lines.Num(), Scale));
		NodesDrawn += InView.Num();
		EdgesDrawn += Lines.Num();

		TestTrue(FString::Printf(TEXT("%s holds nodes"), *View), InView.Num() > 0);
		TestTrue(FString::Printf(TEXT("%s has an extent"), *View), Extent.bAny);
		for (const TPair<FName, FName>& Line : Lines)
		{
			TestTrue(FString::Printf(TEXT("%s's edge %s to %s joins two of its own nodes"), *View,
									 *Line.Key.ToString(), *Line.Value.ToString()),
					 InView.Contains(Line.Key) && InView.Contains(Line.Value));
		}

		TestTrue(FString::Printf(TEXT("%s's node scale is above nought and at most one"), *View),
				 Scale > 0.0f && Scale <= 1.0f);

		// NO TWO BOXES OVERLAP AT THAT SCALE: two boxes placed by their centres miss each other when they are clear on
		// either axis by a whole box's width or height. The tolerance is for the float the scale is held in.
		int32 Overlaps = 0;
		for (int32 First = 0; First < InView.Num(); ++First)
		{
			const FCataclysmEmpireTreeNodeRow* A = UCataclysmEmpireTree::FindNode(Nodes, InView[First]);
			for (int32 Second = First + 1; Second < InView.Num(); ++Second)
			{
				const FCataclysmEmpireTreeNodeRow* B = UCataclysmEmpireTree::FindNode(Nodes, InView[Second]);
				if (A == nullptr || B == nullptr)
				{
					continue;
				}
				const double GapX = FMath::Abs(A->PositionX - B->PositionX);
				const double GapY = FMath::Abs(A->PositionY - B->PositionY);
				if (GapX < UCataclysmPassiveTreeLayout::NodeWidthPx * Scale - 0.01
					&& GapY < UCataclysmPassiveTreeLayout::NodeHeightPx * Scale - 0.01)
				{
					++Overlaps;
				}
			}
		}
		TestEqual(FString::Printf(TEXT("no two of %s's boxes overlap at its scale"), *View), Overlaps, 0);
	}
	AddInfo(FString::Printf(TEXT("BRANCHES: %s; %d of %d nodes and %d of %d edges drawn"),
							*FString::Join(Parts, TEXT("; ")), NodesDrawn, Nodes->GetRowMap().Num(), EdgesDrawn,
							Edges->GetRowMap().Num()));
	TestEqual(TEXT("every node is in one view"), NodesDrawn, Nodes->GetRowMap().Num());
	TestEqual(TEXT("and every edge is drawn in one, none crossing between branches"), EdgesDrawn,
			  Edges->GetRowMap().Num());
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
	TestEqual(TEXT("the screen draws the branch's nodes"), Screen->ShownNodes(),
			  UCataclysmEmpireTree::NodesIn(UCataclysmEmpireTree::LoadNodeTable(), TEXT("Architect")));
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
