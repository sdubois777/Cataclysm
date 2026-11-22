// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Player/CataclysmEmpireTree.h"

/**
 * The empire upgrade tree's buying rules, over the real generated tables. Issue #2741, slice 1a.
 *
 * THE REAL TREE AND NOT A HAND-MADE ONE. Every test reads `DT_EmpireTreeNodes` and `DT_EmpireTreeEdges`, so the rules
 * are tested against the graph a player buys from, and a node named here that the tree file loses fails here. Node
 * identifiers are the tree file's; each is named beside its constant.
 *
 * WHAT IS NOT HERE: where the allocation is kept and where the bank is spent from (the account record, slice 1b), and
 * what a bought node does (slices 3 to 5).
 */

namespace CataclysmEmpireTreeTestNodes
{
	// Architect.
	const FName StructuralIntegrity(TEXT("basic_0000000w"));	// a root, 10 points
	const FName FortifiedGates(TEXT("basic_arch_t1d"));			// <- Structural Integrity at 2, 8 points
	const FName EnduringFoundations(TEXT("basic_arch_t1e"));	// a root, 10 points
	const FName MasonryTechniques(TEXT("basic_00000010"));		// <- Fortified Gates at 6, or Enduring Foundations at 8
	const FName UrbanPlanning(TEXT("basic_0000000x"));			// <- Enduring Foundations at 2
	const FName Foundation(TEXT("basic_0000000v"));				// <- Urban Planning at 2, 10 points
	const FName Scaffolding(TEXT("keystone_0000000z"));			// <- Foundation full; buyable since issue #2832
	const FName MetropolitanGrowth(TEXT("basic_00000016"));		// 10 points
	const FName CivicPride(TEXT("basic_arch_civic_pride"));		// <- Metropolitan Growth at 8; waits on population growth
	const FName RoyalEngineersBlessing(TEXT("capstone_0000001i"));	// the Architect branch's capstone

	// Explorer.
	const FName Pacing(TEXT("basic_00000029"));					// a root, 10 points
	const FName FleetFooted(TEXT("keystone_0000002b"));			// <- Pacing full

	// Artisan. (Coinage was the Treasury root used here until gold existed: issue #2741 slice 6 builds it.)
	const FName RoughDraft(TEXT("basic_arts_t1d"));			// a root; waits on crafting

	// The centre.
	const FName Foundations(TEXT("capstone_0000000m"));			// 25: Aegis of Hope, The Delver, The Hoarder
	const FName EdictsOfPower(TEXT("capstone_0000000n"));		// 50: option 3, The Soul Forge, waits on crafting
	const FName ImperialZenith(TEXT("capstone_0000000p"));		// 200: option 1, Imperial Prowess, built in slice 8

	/**
	 * Puts `Points` into one branch's ordinary nodes, in the branch's own order, ignoring every rule. For a test whose
	 * subject is a threshold and not the path to it: how many points the tree holds is all a threshold reads.
	 */
	void FillBranch(const UDataTable* Nodes, FCataclysmPassiveAllocation& Allocation, const FString& Branch,
					int32 Points)
	{
		for (const FName Node : UCataclysmEmpireTree::NodesIn(Nodes, Branch))
		{
			const FCataclysmEmpireTreeNodeRow* Row = UCataclysmEmpireTree::FindNode(Nodes, Node);
			if (Points <= 0)
			{
				return;
			}
			if (!Row || Row->Kind == UCataclysmEmpireTree::CapstoneKind)
			{
				continue;
			}
			const int32 Taken = FMath::Min(Points, Row->MaxPoints - Allocation.PointsIn(Node));
			if (Taken > 0)
			{
				Allocation.Add(Node, Taken);
				Points -= Taken;
			}
		}
	}
}

// NO `using namespace` AT FILE SCOPE: in a unity build it would reach the test files compiled after this one, and a
// name such as `Foundation` or `Pacing` there would become ambiguous. Each test opens the namespace inside its body.

// ---------------------------------------------------------------------------
// The tables
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeTablesTest,
	"Cataclysm.EmpireTree.TheTablesHoldEveryNodeInItsBranch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeTablesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireTreeTestNodes;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Edges = UCataclysmEmpireTree::LoadEdgeTable();
	if (!TestNotNull(TEXT("the node table loads"), Nodes) || !TestNotNull(TEXT("the edge table loads"), Edges))
	{
		return false;
	}

	TestEqual(TEXT("159 nodes"), Nodes->GetRowMap().Num(), 159);
	TestEqual(TEXT("158 edges"), Edges->GetRowMap().Num(), 158);

	// THE BRANCHES THE GENERATOR READ FROM WHERE EACH NODE SITS: 40, 39, 38 and 38 with each branch's capstone, and
	// the four tier-milestone capstones in the middle.
	TestEqual(TEXT("Architect"), UCataclysmEmpireTree::NodesIn(Nodes, TEXT("Architect")).Num(), 40);
	TestEqual(TEXT("Explorer"), UCataclysmEmpireTree::NodesIn(Nodes, TEXT("Explorer")).Num(), 39);
	TestEqual(TEXT("Treasury"), UCataclysmEmpireTree::NodesIn(Nodes, TEXT("Treasury")).Num(), 38);
	TestEqual(TEXT("Artisan"), UCataclysmEmpireTree::NodesIn(Nodes, TEXT("Artisan")).Num(), 38);
	TestEqual(TEXT("the centre"), UCataclysmEmpireTree::NodesIn(Nodes, UCataclysmEmpireTree::CentreBranch).Num(), 4);
	TestEqual(TEXT("Foundation is an Architect node"), UCataclysmEmpireTree::BranchOf(Nodes, Foundation),
			  FString(TEXT("Architect")));
	TestEqual(TEXT("Pacing is an Explorer node"), UCataclysmEmpireTree::BranchOf(Nodes, Pacing),
			  FString(TEXT("Explorer")));
	return true;
}

// ---------------------------------------------------------------------------
// What opens a node
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEdgeTest,
	"Cataclysm.EmpireTree.ANodeOpensWhenItsParentHoldsWhatTheEdgeAsks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEdgeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireTreeTestNodes;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Edges = UCataclysmEmpireTree::LoadEdgeTable();
	if (!TestNotNull(TEXT("the tables load"), Nodes) || !TestNotNull(TEXT("the edges load"), Edges))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	TestTrue(TEXT("a node with no edge into it is open"),
			 UCataclysmEmpireTree::RefusalForSpending(Nodes, Edges, Allocation, StructuralIntegrity, 5.0).IsEmpty());

	Allocation.Add(StructuralIntegrity, 1);
	const FString Shut = UCataclysmEmpireTree::RefusalForSpending(Nodes, Edges, Allocation, FortifiedGates, 5.0);
	TestTrue(TEXT("its child is shut with one point in it"), Shut.Contains(TEXT("is shut")));

	Allocation.Add(StructuralIntegrity, 1);
	TestTrue(TEXT("and open at the edge's two"),
			 UCataclysmEmpireTree::RefusalForSpending(Nodes, Edges, Allocation, FortifiedGates, 5.0).IsEmpty());

	// A KEYSTONE ASKS FOR ITS PARENT FULL, which the file writes into the edge.
	FCataclysmPassiveAllocation Keystone;
	Keystone.Add(Pacing, 9);
	TestFalse(TEXT("Fleet Footed is shut at nine of Pacing's ten"),
			  UCataclysmEmpireTree::EdgesAllow(Nodes, Edges, Keystone, FleetFooted));
	Keystone.Add(Pacing, 1);
	TestTrue(TEXT("and open at ten"), UCataclysmEmpireTree::EdgesAllow(Nodes, Edges, Keystone, FleetFooted));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeAnyParentTest,
	"Cataclysm.EmpireTree.ANodeWithTwoParentsOpensWhenEitherIsSatisfied",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeAnyParentTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireTreeTestNodes;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Edges = UCataclysmEmpireTree::LoadEdgeTable();
	if (!TestNotNull(TEXT("the tables load"), Nodes) || !TestNotNull(TEXT("the edges load"), Edges))
	{
		return false;
	}

	// MASONRY TECHNIQUES: Fortified Gates at 6, or Enduring Foundations at 8. The coordinating session's C2.
	FCataclysmPassiveAllocation Neither;
	Neither.Add(FortifiedGates, 5);
	Neither.Add(EnduringFoundations, 7);
	TestFalse(TEXT("shut with each parent one short"),
			  UCataclysmEmpireTree::EdgesAllow(Nodes, Edges, Neither, MasonryTechniques));

	FCataclysmPassiveAllocation First;
	First.Add(FortifiedGates, 6);
	TestTrue(TEXT("open through Fortified Gates alone"),
			 UCataclysmEmpireTree::EdgesAllow(Nodes, Edges, First, MasonryTechniques));

	FCataclysmPassiveAllocation Second;
	Second.Add(EnduringFoundations, 8);
	TestTrue(TEXT("open through Enduring Foundations alone"),
			 UCataclysmEmpireTree::EdgesAllow(Nodes, Edges, Second, MasonryTechniques));
	return true;
}

// ---------------------------------------------------------------------------
// The scope ruling: a node whose system is missing is refused
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeWaitsOnTest,
	"Cataclysm.EmpireTree.ANodeWhoseSystemIsMissingIsRefusedAndSaysWhich",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeWaitsOnTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireTreeTestNodes;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Edges = UCataclysmEmpireTree::LoadEdgeTable();
	if (!TestNotNull(TEXT("the tables load"), Nodes) || !TestNotNull(TEXT("the edges load"), Edges))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	double Bank = 50.0;
	FString Reason;
	TestFalse(TEXT("Rough Draft, a root with a full bank, is refused"),
			  UCataclysmEmpireTree::Spend(Nodes, Edges, Allocation, RoughDraft, Bank, Reason));
	TestEqual(TEXT("and the refusal names crafting and its issue"), Reason,
			  FString(TEXT("Rough Draft cannot be bought yet: it needs crafting, which the game does not have yet "
						   "(issue #47).")));
	TestEqual(TEXT("the bank is untouched"), Bank, 50.0);
	TestEqual(TEXT("nothing is spent"), Allocation.Total(), 0);

	// AND EVEN WITH EVERY EDGE INTO IT SATISFIED. Civic Pride since issue #2832, which built Scaffolding, the node this
	// used to read: a city upgrade's build time is no longer missing.
	Allocation.Add(MetropolitanGrowth, 8);
	const FString Waiting = UCataclysmEmpireTree::RefusalForSpending(Nodes, Edges, Allocation, CivicPride, 50.0);
	TestTrue(TEXT("Civic Pride, with Metropolitan Growth at 8, still waits on a population growth rate"),
			 Waiting.Contains(TEXT("a population growth rate")));
	TestTrue(TEXT("Civic Pride's edge is satisfied, so only its system refuses it"),
			 UCataclysmEmpireTree::EdgesAllow(Nodes, Edges, Allocation, CivicPride));

	// AND SCAFFOLDING, WITH FOUNDATION FULL, CAN BE BOUGHT since issue #2832.
	Allocation.Add(Foundation, 10);
	TestTrue(TEXT("Scaffolding, with Foundation full, waits on nothing now"),
			 UCataclysmEmpireTree::RefusalForSpending(Nodes, Edges, Allocation, Scaffolding, 50.0).IsEmpty());

	TestTrue(TEXT("a node that can be bought waits on nothing"),
			 UCataclysmEmpireTree::WaitsOn(Nodes, Foundation).IsEmpty());
	return true;
}

// ---------------------------------------------------------------------------
// The bank
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeBankTest,
	"Cataclysm.EmpireTree.APointCostsOneWholePointFromTheBank",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeBankTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireTreeTestNodes;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Edges = UCataclysmEmpireTree::LoadEdgeTable();
	if (!TestNotNull(TEXT("the tables load"), Nodes) || !TestNotNull(TEXT("the edges load"), Edges))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	double Bank = 2.5;
	FString Reason;
	TestTrue(TEXT("the first point is bought"),
			 UCataclysmEmpireTree::Spend(Nodes, Edges, Allocation, Pacing, Bank, Reason));
	TestTrue(TEXT("and the second"), UCataclysmEmpireTree::Spend(Nodes, Edges, Allocation, Pacing, Bank, Reason));
	TestFalse(TEXT("and not a third from half a point"),
			  UCataclysmEmpireTree::Spend(Nodes, Edges, Allocation, Pacing, Bank, Reason));
	TestEqual(TEXT("the refusal says why"), Reason,
			  FString(TEXT("No empire upgrade point to spend: 0.5 banked, and a node takes whole points.")));
	TestEqual(TEXT("the half stays banked"), Bank, 0.5);
	TestEqual(TEXT("Pacing holds two"), Allocation.PointsIn(Pacing), 2);

	// A BANK A HAIR SHORT OF A POINT PAYS FOR IT, as the empire map shows it as 1.0, and is left at nought.
	double Noisy = 1.0 - 1.0e-9;
	TestTrue(TEXT("a bank a billionth short of a point buys one"),
			 UCataclysmEmpireTree::Spend(Nodes, Edges, Allocation, Pacing, Noisy, Reason));
	TestEqual(TEXT("and is left at nought, not below it"), Noisy, 0.0);

	TestEqual(TEXT("0.9 pays for no point"), UCataclysmEmpireTree::WholePointsIn(0.9), 0);
	TestEqual(TEXT("12.5 pays for twelve"), UCataclysmEmpireTree::WholePointsIn(12.5), 12);
	TestEqual(TEXT("a negative bank pays for none"), UCataclysmEmpireTree::WholePointsIn(-3.0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeFullTest,
	"Cataclysm.EmpireTree.AFullNodeTakesNoMorePoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeFullTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireTreeTestNodes;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Edges = UCataclysmEmpireTree::LoadEdgeTable();
	if (!TestNotNull(TEXT("the tables load"), Nodes) || !TestNotNull(TEXT("the edges load"), Edges))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Pacing, 10);
	TestEqual(TEXT("Pacing is full at ten"),
			  UCataclysmEmpireTree::RefusalForSpending(Nodes, Edges, Allocation, Pacing, 5.0),
			  FString(TEXT("Pacing is full at 10 points.")));
	return true;
}

// ---------------------------------------------------------------------------
// Capstones
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCentreTest,
	"Cataclysm.EmpireTree.ACentreCapstoneOpensAtItsThresholdInTheWholeTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCentreTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireTreeTestNodes;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Edges = UCataclysmEmpireTree::LoadEdgeTable();
	if (!TestNotNull(TEXT("the tables load"), Nodes) || !TestNotNull(TEXT("the edges load"), Edges))
	{
		return false;
	}

	// 24 POINTS IN TWO BRANCHES. The whole tree counts, not one branch.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Pacing, 10);
	Allocation.Add(StructuralIntegrity, 10);
	Allocation.Add(EnduringFoundations, 4);

	FString Reason;
	TestFalse(TEXT("an option cannot be chosen at 24"),
			  UCataclysmEmpireTree::ChooseOption(Nodes, Allocation, Foundations, 1, Reason));
	TestEqual(TEXT("and the refusal counts the whole tree"), Reason,
			  FString(TEXT("Foundations of the Empire opens at 25 points spent in the tree. 24 so far.")));
	TestFalse(TEXT("nor is a decision awaited"),
			  UCataclysmEmpireTree::AwaitsAnOptionChoice(Nodes, Allocation, Foundations));

	Allocation.Add(EnduringFoundations, 1);
	TestTrue(TEXT("at 25 a decision is awaited"),
			 UCataclysmEmpireTree::AwaitsAnOptionChoice(Nodes, Allocation, Foundations));
	TestEqual(TEXT("its point waits for the choice"),
			  UCataclysmEmpireTree::RefusalForSpending(Nodes, Edges, Allocation, Foundations, 5.0),
			  FString(TEXT("Choose one of Foundations of the Empire's three options first.")));

	TestTrue(TEXT("The Aegis of Hope is chosen"),
			 UCataclysmEmpireTree::ChooseOption(Nodes, Allocation, Foundations, 1, Reason));
	TestFalse(TEXT("and no decision is awaited after it"),
			  UCataclysmEmpireTree::AwaitsAnOptionChoice(Nodes, Allocation, Foundations));

	double Bank = 1.0;
	TestTrue(TEXT("the capstone's point is spent"),
			 UCataclysmEmpireTree::Spend(Nodes, Edges, Allocation, Foundations, Bank, Reason));
	TestEqual(TEXT("it holds its one point and its choice"), Allocation.PointsIn(Foundations), 1);
	TestEqual(TEXT("option 1"), Allocation.ChosenOptionIn(Foundations), 1);

	// A CHOICE STANDS UNTIL A RESPEC.
	TestFalse(TEXT("another option is refused"),
			  UCataclysmEmpireTree::ChooseOption(Nodes, Allocation, Foundations, 2, Reason));
	TestEqual(TEXT("and the refusal names the choice and the way to free it"), Reason,
			  FString(TEXT("Foundations of the Empire is already committed to The Aegis of Hope. A respec of the "
						   "empire tree frees the choice.")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeOptionWaitsTest,
	"Cataclysm.EmpireTree.AnOptionWhoseSystemIsMissingCannotBeChosen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeOptionWaitsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireTreeTestNodes;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the table loads"), Nodes))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	FillBranch(Nodes, Allocation, TEXT("Architect"), 200);
	TestEqual(TEXT("200 points are spent"), Allocation.Total(), 200);

	FString Reason;
	TestFalse(TEXT("The Soul Forge, Edicts of Power's third, cannot be chosen"),
			  UCataclysmEmpireTree::ChooseOption(Nodes, Allocation, EdictsOfPower, 3, Reason));
	TestEqual(TEXT("and the refusal names crafting"), Reason,
			  FString(TEXT("The Soul Forge cannot be chosen yet: it needs crafting, which the game does not have yet "
						   "(issue #47).")));
	TestTrue(TEXT("The Collector's Decree, its second, can"),
			 UCataclysmEmpireTree::RefusalForChoosingOption(Nodes, Allocation, EdictsOfPower, 2).IsEmpty());

	// IMPERIAL PROWESS WAITED ON A RULING (Q5) UNTIL THE PROJECT OWNER'S OF 2026-11-21, and slice 8 of #2741 builds it:
	// all three options of The Imperial Zenith can be chosen now.
	TestTrue(TEXT("Imperial Prowess can be chosen"),
			 UCataclysmEmpireTree::RefusalForChoosingOption(Nodes, Allocation, ImperialZenith, 1).IsEmpty());
	TestTrue(TEXT("and The Imperial Zenith awaits a decision"),
			 UCataclysmEmpireTree::AwaitsAnOptionChoice(Nodes, Allocation, ImperialZenith));
	TestFalse(TEXT("an option that does not exist is refused"),
			  UCataclysmEmpireTree::ChooseOption(Nodes, Allocation, ImperialZenith, 4, Reason));
	TestFalse(TEXT("an ordinary node offers no choice"),
			  UCataclysmEmpireTree::ChooseOption(Nodes, Allocation, Foundation, 1, Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeBranchCapstoneTest,
	"Cataclysm.EmpireTree.ABranchCapstoneOpensAtTwoHundredInItsOwnBranch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeBranchCapstoneTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireTreeTestNodes;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Edges = UCataclysmEmpireTree::LoadEdgeTable();
	if (!TestNotNull(TEXT("the tables load"), Nodes) || !TestNotNull(TEXT("the edges load"), Edges))
	{
		return false;
	}

	// 199 IN THE ARCHITECT BRANCH AND 100 ELSEWHERE: the other branch does not count.
	FCataclysmPassiveAllocation Allocation;
	FillBranch(Nodes, Allocation, TEXT("Architect"), 199);
	FillBranch(Nodes, Allocation, TEXT("Explorer"), 100);
	TestEqual(TEXT("199 in the branch"), UCataclysmEmpireTree::SpentInBranch(Nodes, Allocation, TEXT("Architect")),
			  199);
	TestEqual(TEXT("299 in the tree"), UCataclysmEmpireTree::SpentInTree(Allocation), 299);
	TestEqual(TEXT("The Royal Engineer's Blessing is shut at 199 in its branch"),
			  UCataclysmEmpireTree::RefusalForSpending(Nodes, Edges, Allocation, RoyalEngineersBlessing, 5.0),
			  FString(TEXT("The Royal Engineer's Blessing opens at 200 points spent in the Architect branch. 199 so "
						   "far.")));

	FillBranch(Nodes, Allocation, TEXT("Architect"), 1);
	TestTrue(TEXT("and open at 200, with no option to choose"),
			 UCataclysmEmpireTree::RefusalForSpending(Nodes, Edges, Allocation, RoyalEngineersBlessing, 5.0)
				 .IsEmpty());
	TestFalse(TEXT("a branch capstone awaits no choice"),
			  UCataclysmEmpireTree::AwaitsAnOptionChoice(Nodes, Allocation, RoyalEngineersBlessing));
	return true;
}

// ---------------------------------------------------------------------------
// Respec
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeRespecTest,
	"Cataclysm.EmpireTree.ARespecReturnsEveryPointAndClearsEveryChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeRespecTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireTreeTestNodes;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the table loads"), Nodes))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	FillBranch(Nodes, Allocation, TEXT("Explorer"), 30);
	Allocation.Add(Foundations, 1);
	Allocation.SetChosenOption(Foundations, 2);
	double Bank = 0.5;

	FString Reason;
	TestEqual(TEXT("31 points come back"), UCataclysmEmpireTree::Respec(Allocation, Bank, true, Reason), 31);
	TestTrue(TEXT("with no refusal"), Reason.IsEmpty());
	TestEqual(TEXT("onto the half already banked"), Bank, 31.5);
	TestEqual(TEXT("nothing is left in the tree"), Allocation.Nodes.Num(), 0);
	TestEqual(TEXT("and no choice"), Allocation.ChosenOptionIn(Foundations), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeRespecRefusedTest,
	"Cataclysm.EmpireTree.ARespecIsRefusedWithNothingInTheTreeOrNoRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeRespecRefusedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireTreeTestNodes;

	FCataclysmPassiveAllocation Empty;
	double Bank = 3.0;
	FString Reason;
	TestEqual(TEXT("an empty tree returns nothing"), UCataclysmEmpireTree::Respec(Empty, Bank, true, Reason), 0);
	TestEqual(TEXT("and says why"), Reason,
			  FString(TEXT("Nothing is spent or chosen in the empire upgrade tree, so there is nothing to respec.")));

	FCataclysmPassiveAllocation Spent;
	Spent.Add(Pacing, 4);
	TestEqual(TEXT("with no run, nothing comes back"), UCataclysmEmpireTree::Respec(Spent, Bank, false, Reason), 0);
	TestEqual(TEXT("and says why"), Reason,
			  FString(TEXT("A respec of the empire upgrade tree costs days of a run, and no run is being played.")));
	TestEqual(TEXT("the points stay where they were"), Spent.PointsIn(Pacing), 4);
	TestEqual(TEXT("and the bank as it was"), Bank, 3.0);

	// A CHOICE WITH NO POINT BEHIND IT CAN BE RESPECCED: a choice stands until a respec, so this is how it is undone.
	FCataclysmPassiveAllocation ChosenOnly;
	ChosenOnly.SetChosenOption(Foundations, 3);
	TestTrue(TEXT("a choice alone may be respecced"),
			 UCataclysmEmpireTree::RefusalForRespec(ChosenOnly, true).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeRespecDaysTest,
	"Cataclysm.EmpireTree.ARespecCostsTheModesDeathCostInDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeRespecDaysTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireTreeTestNodes;

	TestEqual(TEXT("Standard"), UCataclysmEmpireTree::RespecDays(0), 5);
	TestEqual(TEXT("Hardcore"), UCataclysmEmpireTree::RespecDays(1), 10);
	TestEqual(TEXT("Heretic"), UCataclysmEmpireTree::RespecDays(2), 15);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
