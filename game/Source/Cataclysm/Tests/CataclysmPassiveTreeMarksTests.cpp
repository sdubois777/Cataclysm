// Copyright Stephen Dubois. All Rights Reserved.

/**
 * THE PASSIVE TREE SCREEN DRAWS EACH NODE AS A MARK SIZED BY ITS KIND. Since 2026-11-24, for the class-tree sigils:
 * the owner wants every class tree laid out as an emblem of its class, and while every node was the same 150 by 44
 * labelled box, the boxes hid the shape the nodes made.
 *
 * An ordinary node is a small square, a keystone a larger one and a vow (the tree files' "capstone") a wide bar; a
 * spent node shows its count on its mark; the node hovered or last selected shows its name and count beside its mark;
 * edges are 10 px at a zoom of one, not 3. Spending, refusing, tool tips, the vow's choice, the fit and the zoom are
 * as they were, and their tests are in `CataclysmPassiveTreeTests.cpp`.
 *
 * WHAT IS DRAWN IS NOT TESTED, AND CANNOT BE: the automation command passes `-nullrhi`. What decides it is --
 * `UCataclysmPassiveTreeLayout`'s arithmetic, and what the screen says -- on the generated tables the game loads.
 */

#include "Misc/AutomationTest.h"

#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Interface/CataclysmPassiveTreeLayout.h"
#include "Interface/CataclysmPassiveTreeWidget.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

#if WITH_AUTOMATION_TESTS

namespace CataclysmPassiveMarksTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	const FName Fervour(TEXT("Masochist_basic_spine_000"));
	const FName FervoursChild(TEXT("Masochist_basic_spine_001"));
	const FName CommunionOfPain(TEXT("Masochist_keystone_spine_001"));
	const FName TheFirstVow(TEXT("Masochist_capstone_25"));

	/** A possessed player, as `CataclysmPassiveTreeTests.cpp`'s screen tests make one. */
	ACataclysmPlayerState* SpawnPossessedPlayer(UWorld* World, APlayerController*& OutController)
	{
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState || !Controller || !Character)
		{
			return nullptr;
		}
		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Character);
		OutController = Controller;
		return PlayerState;
	}

	/** The screen with no Blueprint, its player state and panel given, the Masochist tree shown. */
	UCataclysmPassiveTreeWidget* MasochistScreen(FAutomationTestBase& Test, APlayerController* Controller,
												 ACataclysmPlayerState* State)
	{
		UCataclysmPassiveTreeWidget* Screen = NewObject<UCataclysmPassiveTreeWidget>(Controller);
		if (!Test.TestNotNull(TEXT("set-up: the screen was created"), Screen))
		{
			return nullptr;
		}
		Screen->SetPlayerStateForTests(State);
		Screen->SetPanelSizeForTests(FVector2D(2400.0, 900.0));
		if (!Test.TestTrue(TEXT("set-up: the Masochist tree can be shown"), Screen->ShowTree(TEXT("Masochist"))))
		{
			return nullptr;
		}
		return Screen;
	}
}

// --------------------------------------------------------------------------------------------------------------------
// PM1. A mark is sized by its node's kind, and the three kinds read apart.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveMarkByKindTest,
	"Cataclysm.PassiveMarks.AMarkIsSizedByItsNodesKindAndTheThreeKindsReadApart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "keystone" and "capstone" (a vow) by the tree files' words, anything else ordinary. An ordinary mark is 28 by 28, a
 * keystone's 48 by 48, larger both ways, and a vow's 72 by 40, wider than tall and wider than a keystone's, so the
 * kinds differ in shape and not only in size. The Bulwark tree's 74 nodes take 55 ordinary marks, 15 keystones' and 4
 * vows', as `PassiveNodes.csv` holds them. Edges are 10 px at a zoom of one. CONTROL: the empire tree's box (150 by 44)
 * and its 3 px edge are unchanged, since that screen still reads them. Prints "MARKS BY KIND".
 */
bool FCataclysmPassiveMarkByKindTest::RunTest(const FString&)
{
	using Layout = UCataclysmPassiveTreeLayout;
	TestEqual(TEXT("\"basic\" is an ordinary mark"), static_cast<int32>(Layout::MarkFor(TEXT("basic"))),
			  static_cast<int32>(ECataclysmTreeMark::Ordinary));
	TestEqual(TEXT("\"keystone\" a keystone's"), static_cast<int32>(Layout::MarkFor(TEXT("keystone"))),
			  static_cast<int32>(ECataclysmTreeMark::Keystone));
	TestEqual(TEXT("\"capstone\" a vow's"), static_cast<int32>(Layout::MarkFor(TEXT("capstone"))),
			  static_cast<int32>(ECataclysmTreeMark::Vow));
	TestEqual(TEXT("a kind no tree writes is ordinary"), static_cast<int32>(Layout::MarkFor(TEXT("unheard of"))),
			  static_cast<int32>(ECataclysmTreeMark::Ordinary));

	const FVector2D Ordinary = Layout::MarkSizePx(ECataclysmTreeMark::Ordinary);
	const FVector2D Keystone = Layout::MarkSizePx(ECataclysmTreeMark::Keystone);
	const FVector2D Vow = Layout::MarkSizePx(ECataclysmTreeMark::Vow);
	TestTrue(TEXT("an ordinary mark is 28 by 28"), Ordinary.Equals(FVector2D(28.0, 28.0)));
	TestTrue(TEXT("a keystone's is 48 by 48"), Keystone.Equals(FVector2D(48.0, 48.0)));
	TestTrue(TEXT("a vow's is 72 by 40"), Vow.Equals(FVector2D(72.0, 40.0)));
	TestTrue(TEXT("a keystone's is larger than an ordinary one both ways"),
			 Keystone.X > Ordinary.X && Keystone.Y > Ordinary.Y);
	TestTrue(TEXT("a vow's is wider than it is tall, and wider than a keystone's"), Vow.X > Vow.Y && Vow.X > Keystone.X);

	const UDataTable* Table = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("set-up: the generated node table loads"), Table))
	{
		return false;
	}
	int32 Counts[3] = {0, 0, 0};
	int32 Disagree = 0;
	for (const FName& Node : UCataclysmPassiveTree::NodesIn(Table, TEXT("Bulwark")))
	{
		const FCataclysmPassiveNodeRow* Row = UCataclysmPassiveTree::FindNode(Table, Node);
		const ECataclysmTreeMark Mark = Layout::MarkFor(Row ? Row->Kind : FString());
		++Counts[static_cast<int32>(Mark)];
		Disagree += Layout::MarkSizeForNode(Table, Node).Equals(Layout::MarkSizePx(Mark)) ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("MARKS BY KIND: Bulwark %d ordinary, %d keystone, %d vow; %d node(s) sized otherwise"),
							Counts[0], Counts[1], Counts[2], Disagree));
	TestEqual(TEXT("the Bulwark tree has 55 ordinary marks"), Counts[0], 55);
	TestEqual(TEXT("15 keystones'"), Counts[1], 15);
	TestEqual(TEXT("and 4 vows'"), Counts[2], 4);
	TestEqual(TEXT("and every node's mark is its kind's"), Disagree, 0);

	FVector2D At;
	FVector2D Extent;
	float Angle = 0.0f;
	Layout::EdgeGeometry(FVector2D(0.0, 0.0), FVector2D(100.0, 0.0), Layout::MarkEdgeThicknessPx * 0.5f, At, Extent,
						 Angle);
	TestEqual(TEXT("an edge is 10 px at zoom one, so 5 at a half"), static_cast<float>(Extent.Y), 5.0f, 0.001f);
	TestEqual(TEXT("CONTROL: the height a mark's count is scaled against is still the old box's 44"), Layout::NodeHeightPx,
			  44.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PM2. Marks are shrunk only as far as a tree's closest pair needs, and no real tree needs it.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveMarkScaleTest,
	"Cataclysm.PassiveMarks.MarksShrinkOnlyAsFarAsTheClosestPairNeedsAndNoRealTreeNeedsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A made tree: two ordinary nodes 20 units apart side by side need 28, so the tree's marks shrink to 20 / 28; a keystone
 * 30 units under an ordinary node needs (28 + 48) / 2 = 38, which allows 30 / 38, more, so the side-by-side pair binds.
 * A tree of one node is drawn at 1. Then every class tree in `PassiveNodes.csv`: its marks are drawn at 1 -- measured on
 * 2026-11-24, where the old box rule held the Bulwark tree to 0.49 -- and no two of its marks overlap at that scale.
 * Prints "MARK SCALE".
 */
bool FCataclysmPassiveMarkScaleTest::RunTest(const FString&)
{
	using Layout = UCataclysmPassiveTreeLayout;
	UDataTable* Made = NewObject<UDataTable>();
	Made->RowStruct = FCataclysmPassiveNodeRow::StaticStruct();
	const TArray<FString> Problems = Made->CreateTableFromCSVString(TEXT(
		"Name,Tree,NodeId,Kind,NodeName,Description,MaxPoints,Threshold,PositionX,PositionY,Option1Name,Option1Description,"
		"Option2Name,Option2Description,Option3Name,Option3Description\r\n"
		"Close_a,Close,a,basic,A,A.,1,0,0,0,,,,,,\r\n"
		"Close_b,Close,b,basic,B,B.,1,0,20,0,,,,,,\r\n"
		"Close_k,Close,k,keystone,K,K.,1,0,200,30,,,,,,\r\n"
		"Close_c,Close,c,basic,C,C.,1,0,200,0,,,,,,\r\n"
		"Alone_a,Alone,a,basic,A,A.,1,0,0,0,,,,,,\r\n"));
	for (const FString& Problem : Problems)
	{
		AddError(Problem);
	}
	TestEqual(TEXT("two ordinary marks 20 apart side by side shrink the tree's marks to 20 / 28"),
			  Layout::MarkScaleFor(Made, TEXT("Close")), 20.0f / 28.0f, 0.0001f);
	TestEqual(TEXT("a tree of one node is drawn at 1"), Layout::MarkScaleFor(Made, TEXT("Alone")), 1.0f);

	const UDataTable* Table = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("set-up: the generated node table loads"), Table))
	{
		return false;
	}
	FString Scales;
	int32 Overlaps = 0;
	const TArray<FString> Trees = UCataclysmPassiveTree::TreeNames(Table);
	for (const FString& Tree : Trees)
	{
		const float Scale = Layout::MarkScaleFor(Table, Tree);
		Scales += FString::Printf(TEXT("%s %.3f; "), *Tree, Scale);
		TestEqual(*FString::Printf(TEXT("%s's marks are drawn at 1"), *Tree), Scale, 1.0f);
		const TArray<FName> Nodes = UCataclysmPassiveTree::NodesIn(Table, Tree);
		for (int32 First = 0; First < Nodes.Num(); ++First)
		{
			const FCataclysmPassiveNodeRow* A = UCataclysmPassiveTree::FindNode(Table, Nodes[First]);
			for (int32 Second = First + 1; Second < Nodes.Num(); ++Second)
			{
				const FCataclysmPassiveNodeRow* B = UCataclysmPassiveTree::FindNode(Table, Nodes[Second]);
				if (!A || !B)
				{
					continue;
				}
				const FVector2D Apart = (FVector2D(A->PositionX, A->PositionY) - FVector2D(B->PositionX, B->PositionY)).GetAbs();
				const FVector2D Half = (Layout::MarkSizeForNode(Table, Nodes[First])
										+ Layout::MarkSizeForNode(Table, Nodes[Second])) * 0.5 * Scale;
				Overlaps += Apart.X < Half.X - 0.001 && Apart.Y < Half.Y - 0.001 ? 1 : 0;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("MARK SCALE: %s%d pair(s) overlapping"), *Scales, Overlaps));
	TestTrue(TEXT("set-up: there are class trees to measure"), Trees.Num() >= 6);
	TestEqual(TEXT("and no two marks of any of them overlap"), Overlaps, 0);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PM3. Hovering a node shows its name and count beside its mark.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveMarkHoverNameTest,
	"Cataclysm.PassiveMarks.HoveringANodeShowsItsNameAndCountBesideItsMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * CONTROL: with nothing hovered the screen shows no name. Hovering Fervour shows "Fervour  0/1" beside its ordinary
 * mark, its left edge 8 px past the mark's right edge, level with the mark's centre; hovering Communion of Pain shows
 * its name further from its centre, by half the keystone's larger mark. Hovering does not spend. Showing another tree
 * clears the name. Prints "HOVER NAME".
 */
bool FCataclysmPassiveMarkHoverNameTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveMarksTest;
	using Layout = UCataclysmPassiveTreeLayout;
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APlayerController* Controller = nullptr;
	ACataclysmPlayerState* State = SpawnPossessedPlayer(World, Controller);
	if (!TestNotNull(TEXT("set-up: a possessed player"), State))
	{
		return false;
	}
	UCataclysmPassiveTreeWidget* Screen = MasochistScreen(*this, Controller, State);
	const UDataTable* Table = UCataclysmPassiveTree::LoadNodeTable();
	const FCataclysmPassiveNodeRow* FervourRow = UCataclysmPassiveTree::FindNode(Table, Fervour);
	const FCataclysmPassiveNodeRow* KeystoneRow = UCataclysmPassiveTree::FindNode(Table, CommunionOfPain);
	if (!Screen || !TestNotNull(TEXT("set-up: Fervour is in the table"), FervourRow)
		|| !TestNotNull(TEXT("set-up: and Communion of Pain"), KeystoneRow))
	{
		return false;
	}

	TestTrue(TEXT("CONTROL: with nothing hovered no name is shown"), Screen->NodeNameCaptionText().IsEmpty());

	Screen->HoverNodeForTests(Fervour);
	const FVector2D FervourAt = Layout::ScreenPositionFor(FVector2D(FervourRow->PositionX, FervourRow->PositionY),
														  Screen->GetFocus(), Screen->GetZoom(), Screen->PanelSize());
	const FVector2D FervourName = Screen->NodeNameCaptionPositionForTests();
	const FVector2D FervourMark = Layout::MarkSizePx(ECataclysmTreeMark::Ordinary) * Screen->GetZoom();
	AddInfo(FString::Printf(TEXT("HOVER NAME: \"%s\" at (%.1f, %.1f), its mark at (%.1f, %.1f) %.1f wide, zoom %.3f"),
							*Screen->NodeNameCaptionText(), FervourName.X, FervourName.Y, FervourAt.X, FervourAt.Y,
							FervourMark.X, Screen->GetZoom()));
	TestEqual(TEXT("**hovering Fervour shows its name and count**"), Screen->NodeNameCaptionText(),
			  FString(TEXT("Fervour  0/1")));
	TestEqual(TEXT("beside its mark: 8 px past its right edge"), FervourName.X, FervourAt.X + FervourMark.X * 0.5 + 8.0,
			  0.01);
	TestEqual(TEXT("level with its centre"), FervourName.Y, FervourAt.Y, 0.01);
	TestEqual(TEXT("and hovering spent nothing"), State->GetPassiveAllocation().Total(), 0);

	Screen->HoverNodeForTests(CommunionOfPain);
	const FVector2D KeystoneAt = Layout::ScreenPositionFor(FVector2D(KeystoneRow->PositionX, KeystoneRow->PositionY),
														   Screen->GetFocus(), Screen->GetZoom(), Screen->PanelSize());
	TestEqual(TEXT("hovering a keystone names it"), Screen->NodeNameCaptionText(),
			  Layout::NameCaption(KeystoneRow->NodeName, 0, KeystoneRow->MaxPoints));
	TestEqual(TEXT("further from its centre, by half its larger mark"),
			  Screen->NodeNameCaptionPositionForTests().X - KeystoneAt.X, 48.0 * Screen->GetZoom() * 0.5 + 8.0, 0.01);

	Screen->ShowTree(TEXT("Bulwark"));
	TestTrue(TEXT("showing another tree clears the name"), Screen->NodeNameCaptionText().IsEmpty());
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PM4. A spent node shows its count on its mark; an unspent one shows nothing; the node spent is named.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveMarkSpentCountTest,
	"Cataclysm.PassiveMarks.ASpentNodeShowsItsCountOnItsMarkAndAnUnspentOneShowsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * CONTROL: before anything is spent, Fervour's mark says nothing. Spending a point into it through the screen: its mark
 * says "1/1" and is drawn as holding points; its child, unspent, says nothing and is not; and the node just spent, the
 * one selected, is named beside its mark with its new count, "Fervour  1/1". A vow's mark says nothing until it holds a
 * point, as any other. Prints "SPENT COUNT".
 */
bool FCataclysmPassiveMarkSpentCountTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveMarksTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APlayerController* Controller = nullptr;
	ACataclysmPlayerState* State = SpawnPossessedPlayer(World, Controller);
	if (!TestNotNull(TEXT("set-up: a possessed player"), State))
	{
		return false;
	}
	UCataclysmPassiveTreeWidget* Screen = MasochistScreen(*this, Controller, State);
	if (!Screen)
	{
		return false;
	}

	UCataclysmChoiceButton* Before = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*Before, Fervour, false);
	TestTrue(TEXT("CONTROL: before a spend, Fervour's mark says nothing"), Before->GetLabel().IsEmpty());

	if (!TestTrue(TEXT("set-up: Fervour takes a point through the screen"), Screen->SpendInto(Fervour)))
	{
		AddError(Screen->RefusalText().ToString());
		return false;
	}

	UCataclysmChoiceButton* Spent = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*Spent, Fervour, false);
	UCataclysmChoiceButton* Child = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*Child, FervoursChild, false);
	UCataclysmChoiceButton* Vow = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*Vow, TheFirstVow, false);
	AddInfo(FString::Printf(TEXT("SPENT COUNT: Fervour's mark \"%s\", its child's \"%s\", The First Vow's \"%s\"; "
								 "named \"%s\""),
							*Spent->GetLabel().ToString(), *Child->GetLabel().ToString(), *Vow->GetLabel().ToString(),
							*Screen->NodeNameCaptionText()));
	TestEqual(TEXT("**Fervour's mark says its count**"), Spent->GetLabel().ToString(), FString(TEXT("1/1")));
	TestTrue(TEXT("and is drawn as holding points"), Spent->IsChosen());
	TestTrue(TEXT("its unspent child's mark says nothing"), Child->GetLabel().IsEmpty());
	TestFalse(TEXT("and is not drawn as holding points"), Child->IsChosen());
	TestTrue(TEXT("a vow's mark holding no point says nothing either"), Vow->GetLabel().IsEmpty());
	TestEqual(TEXT("the node just spent is named beside its mark with its new count"), Screen->NodeNameCaptionText(),
			  FString(TEXT("Fervour  1/1")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
