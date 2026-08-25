// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
// For the map from a stat name to the attribute it drives. Issue #954.
#include "Character/CataclysmPlayerClassStats.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmPassiveTreeLayout.h"
#include "Interface/CataclysmPassiveTreeWidget.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"

#include <limits>

/**
 * Passive points: earning them, spending them, and the rules that bound them.
 *
 * WHAT THESE GUARD. Issue #50 and `docs/Cataclysm_GDD_v2.md` section XII. The
 * design has said 230 passive points since the document was written, every class
 * tree file carries `pointBudget: 230`, and until this nothing anywhere counted,
 * stored or spent one.
 *
 * THE ONE THAT MATTERS MOST IS THE ARITHMETIC ONE.
 * `TheThreeAwardsAddUpToTheStatedBudget` recomputes 150 from levelling and 80
 * from the eight bosses and checks they make exactly the 230 the trees are
 * designed against. If any of the three award rules is ever retuned, that is the
 * test that notices the budget no longer adds up -- and the budget is what every
 * tree's shape was chosen for.
 *
 * WHAT A SPENT POINT IS WORTH IS COVERED FOR THE 26 NODES THAT HAVE A NUMBER,
 * and for no others. `game/Data/PassiveEffects.csv` gives a stat, a bucket and a
 * value per point for 26 of the 293 nodes; the remaining 267 say what they do in
 * a sentence written for a player and carry no number anywhere a machine can
 * read. Issues #936 and #939. Of the 26, the one that names a required tag
 * reaches nobody -- see `ATagScopedNodeGrantsNothingYet` and issue #943.
 *
 * WHAT IS DELIBERATELY NOT COVERED. Anything a person can see: the automation
 * command passes `-nullrhi`, so `WBP_PassiveTree` cannot be loaded and no widget
 * draws. The screen's logic is reached by constructing
 * `UCataclysmPassiveTreeWidget` with no Blueprint at all, which is why every
 * bound pointer in it is checked before it is used.
 */

namespace CataclysmPassiveTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/**
	 * A small tree this test controls, in the shape the real ones have.
	 *
	 * BUILT RATHER THAN LOADED, so a case can be set up that the real trees do
	 * not contain -- a capstone with no options, a node with two ways in -- and
	 * so that re-authoring a tree in the separate editing tool cannot silently
	 * change what these tests mean. The real trees are exercised instead by
	 * `tools/tests/test_class_passive_trees.py`, which checks them against the
	 * design document, and by the tests at the bottom of this file that read the
	 * generated tables.
	 *
	 * IT IS TWO TREES, `Ravager` AND `Bulwark`, and that is what makes the
	 * reachability tests possible: one is Demonic and one is War, so a character
	 * carrying one damage type can reach exactly one of them.
	 */
	UDataTable* MakeNodeTable(FAutomationTestBase& Test)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmPassiveNodeRow::StaticStruct();

		const TArray<FString> Problems = Table->CreateTableFromCSVString(TEXT(
			"Name,Tree,NodeId,Kind,NodeName,Description,MaxPoints,Threshold,PositionX,PositionY,Option1Name,Option1Description,Option2Name,Option2Description,Option3Name,Option3Description\r\n"
			// The root: no edge leads to it, which is how a tree is started.
			"Ravager_root,Ravager,root,basic,Wrath,Unlocks Wrath.,1,0,0,0,,,,,,\r\n"
			"Ravager_mid,Ravager,mid,basic,Cruelty,+2% per point.,5,0,0,100,,,,,,\r\n"
			// A keystone whose edge asks for its parent in full.
			"Ravager_stone,Ravager,stone,keystone,Butchery,Changes a rule.,1,0,0,200,,,,,,\r\n"
			// Two ways in: either satisfied edge opens it.
			"Ravager_joined,Ravager,joined,basic,Confluence,+1% per point.,3,0,100,100,,,,,,\r\n"
			"Ravager_side,Ravager,side,basic,Sidepath,+1% per point.,2,0,-100,100,,,,,,\r\n"
			// A node whose bonus depends on the character's health. Issue #959.
			//
			// PLACED ON A CORNER THE TREE ALREADY OCCUPIES, at (-100, 200),
			// because `ATreesExtentIsTheRectangleItsNodesOccupy` measures the
			// rectangle these nodes fill and a node outside it would change what
			// that test is about. Adding one anywhere new fails it, which is the
			// test doing its job.
			"Ravager_low,Ravager,low,basic,Cornered,While at or below 20% health.,8,0,-100,200,,,,,,\r\n"
			// A capstone with three options, opening at four points spent.
			"Ravager_cap,Ravager,cap,capstone,First Vow,Choose one.,1,4,300,0,Iron,Take iron.,Blood,Take blood.,Ash,Take ash.\r\n"
			// A capstone with none, which is what the Saboteur tree has. #935.
			"Ravager_empty,Ravager,empty,capstone,Silent Vow,Choose one of three.,1,4,300,100,,,,,,\r\n"
			// A second tree, of a different damage type.
			"Bulwark_root,Bulwark,root,basic,Resolve,Unlocks Resolve.,1,0,0,0,,,,,,\r\n"));

		for (const FString& Problem : Problems)
		{
			Test.AddError(Problem);
		}
		return Problems.Num() == 0 ? Table : nullptr;
	}

	UDataTable* MakeEdgeTable(FAutomationTestBase& Test)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmPassiveEdgeRow::StaticStruct();

		const TArray<FString> Problems = Table->CreateTableFromCSVString(TEXT(
			"Name,Tree,Source,Target,RequiredPoints\r\n"
			"Ravager_e1,Ravager,Ravager_root,Ravager_mid,1\r\n"
			// FULL INVESTMENT IN THE PARENT, which is the keystone rule. Cruelty
			// holds five, so five is what the keystone's edge asks for.
			"Ravager_e2,Ravager,Ravager_mid,Ravager_stone,5\r\n"
			"Ravager_e3,Ravager,Ravager_root,Ravager_side,1\r\n"
			// Two edges into one node, asking different amounts.
			"Ravager_e4,Ravager,Ravager_mid,Ravager_joined,4\r\n"
			"Ravager_e5,Ravager,Ravager_side,Ravager_joined,2\r\n"));

		for (const FString& Problem : Problems)
		{
			Test.AddError(Problem);
		}
		return Problems.Num() == 0 ? Table : nullptr;
	}

	/** Spend into a node the given number of times, reporting the first refusal. */
	bool SpendMany(FAutomationTestBase& Test, const UDataTable* NodeTable,
				   const UDataTable* EdgeTable,
				   FCataclysmPassiveAllocation& Allocation, const TCHAR* Node,
				   int32 Times, int32 PointsAvailable)
	{
		for (int32 Each = 0; Each < Times; ++Each)
		{
			FString Reason;
			if (!UCataclysmPassiveTree::Spend(NodeTable, EdgeTable, Allocation,
											  FName(Node), PointsAvailable,
											  Reason))
			{
				Test.AddError(FString::Printf(
					TEXT("point %d of %d into %s was refused: %s"), Each + 1,
					Times, Node, *Reason));
				return false;
			}
		}
		return true;
	}

	/** A possessed player character in this world, or null. */
	ACataclysmPlayerCharacter* SpawnPossessedPlayer(UWorld* World)
	{
		ACataclysmPlayerState* PlayerState =
			World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);

		if (!PlayerState || !Controller || !Character)
		{
			return nullptr;
		}

		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Character);
		return Character;
	}
}

// ---------------------------------------------------------------------------
// How many points a character has
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveBudgetAddsUpTest,
	"Cataclysm.Passives.TheThreeAwardsAddUpToTheStatedBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveBudgetAddsUpTest::RunTest(const FString&)
{
	// THE DESIGN'S OWN ARITHMETIC, RECOMPUTED. Section XII gives three award
	// rules and states a budget of 230 elsewhere, and the two are only equal
	// because the numbers happen to work out. Every class tree file carries
	// `pointBudget: 230` and every tree's shape was chosen against it, so a
	// retune of any of the three rules that stops them adding up matters.
	TestEqual(TEXT("levelling to 100 gives 150"),
			  UCataclysmPassivePoints::FromLevel(100), 150);
	TestEqual(TEXT("the eight unique bosses give 80"),
			  UCataclysmPassivePoints::FromBossKills(
				  UCataclysmPassivePoints::UniqueBosses), 80);
	TestEqual(TEXT("and together they are exactly the stated budget"),
			  UCataclysmPassivePoints::Available(
				  100, UCataclysmPassivePoints::UniqueBosses),
			  UCataclysmPassivePoints::Budget);

	// THE BOSSES ARE REQUIRED, WHICH THE PROJECT OWNER CONFIRMED ON 2026-08-25.
	// A character that reaches level 100 without fighting one tops out at 150
	// and never reaches the budget the trees are designed against.
	TestEqual(TEXT("a character that fights no boss tops out at 150"),
			  UCataclysmPassivePoints::Available(100, 0), 150);
	TestTrue(TEXT("which is short of the budget"),
			 UCataclysmPassivePoints::Available(100, 0)
				 < UCataclysmPassivePoints::Budget);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassivePerLevelTest,
	"Cataclysm.Passives.OnePointALevelAndFiveMoreEveryTenth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassivePerLevelTest::RunTest(const FString&)
{
	// A LEVEL 1 CHARACTER HAS ONE POINT, not zero. The design says "per level"
	// and the first level is a level.
	TestEqual(TEXT("level 1"), UCataclysmPassivePoints::FromLevel(1), 1);

	// THE TENTH LEVEL IS WHERE THE FIRST BONUS LANDS, so nine and ten differ by
	// six rather than by one. Getting that boundary wrong by one level costs ten
	// points over a whole climb and nothing else would report it.
	TestEqual(TEXT("level 9, before the first bonus"),
			  UCataclysmPassivePoints::FromLevel(9), 9);
	TestEqual(TEXT("level 10, with the first bonus"),
			  UCataclysmPassivePoints::FromLevel(10), 15);
	TestEqual(TEXT("level 11 adds only its own point"),
			  UCataclysmPassivePoints::FromLevel(11), 16);
	TestEqual(TEXT("level 20, with two bonuses"),
			  UCataclysmPassivePoints::FromLevel(20), 30);

	// CLAMPED RATHER THAN REFUSED, because the level can come from a console
	// variable somebody typed at.
	TestEqual(TEXT("level 0 is treated as level 1"),
			  UCataclysmPassivePoints::FromLevel(0),
			  UCataclysmPassivePoints::FromLevel(1));
	TestEqual(TEXT("above the maximum is treated as the maximum"),
			  UCataclysmPassivePoints::FromLevel(500),
			  UCataclysmPassivePoints::FromLevel(100));

	// AND A BOSS COUNT CANNOT EXCEED THE EIGHT THERE ARE, so a record edited by
	// hand cannot hand a character more than the budget.
	TestEqual(TEXT("more boss kills than there are bosses is clamped"),
			  UCataclysmPassivePoints::FromBossKills(99), 80);
	TestEqual(TEXT("and a negative count is nothing"),
			  UCataclysmPassivePoints::FromBossKills(-4), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveFirstBossOnlyTest,
	"Cataclysm.Passives.ABossPaysOnlyTheFirstTimeItIsBeaten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveFirstBossOnlyTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("a player state was spawned"), State))
	{
		return false;
	}

	const int32 Before = State->PassivePointsAvailable();

	TestTrue(TEXT("the first defeat counts"),
			 State->RecordCataclysmBossDefeat(FName(TEXT("Demonic"))));
	TestEqual(TEXT("and is worth ten points"),
			  State->PassivePointsAvailable(), Before + 10);

	// THE WHOLE REASON THE BOSSES ARE STORED BY NAME. A count could be raised
	// eight times by beating one boss eight times, which would be 80 points from
	// one fight.
	TestFalse(TEXT("beating the same boss again counts for nothing"),
			  State->RecordCataclysmBossDefeat(FName(TEXT("Demonic"))));
	TestEqual(TEXT("and grants nothing"), State->PassivePointsAvailable(),
			  Before + 10);

	TestTrue(TEXT("a different boss does count"),
			 State->RecordCataclysmBossDefeat(FName(TEXT("War"))));
	TestEqual(TEXT("and is worth another ten"),
			  State->PassivePointsAvailable(), Before + 20);

	return true;
}

// ---------------------------------------------------------------------------
// Where a point may go
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveEdgesGateNodesTest,
	"Cataclysm.Passives.ANodeIsShutUntilSomethingLeadingToItIsInvestedIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveEdgesGateNodesTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	UDataTable* EdgeTable = MakeEdgeTable(*this);
	if (!NodeTable || !EdgeTable)
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	constexpr int32 Plenty = 100;

	// THE ROOT HAS NO EDGE LEADING TO IT AND IS THEREFORE OPEN. Each of the four
	// real trees has exactly one such node and it is the one that unlocks the
	// class resource.
	TestTrue(TEXT("the root is open with nothing spent"),
			 UCataclysmPassiveTree::RefusalForSpending(
				 NodeTable, EdgeTable, Allocation, FName(TEXT("Ravager_root")),
				 Plenty).IsEmpty());

	TestFalse(TEXT("but the node past it is shut"),
			  UCataclysmPassiveTree::RefusalForSpending(
				  NodeTable, EdgeTable, Allocation, FName(TEXT("Ravager_mid")),
				  Plenty).IsEmpty());

	if (!SpendMany(*this, NodeTable, EdgeTable, Allocation,
				   TEXT("Ravager_root"), 1, Plenty))
	{
		return false;
	}

	TestTrue(TEXT("and open once the root holds its one point"),
			 UCataclysmPassiveTree::RefusalForSpending(
				 NodeTable, EdgeTable, Allocation, FName(TEXT("Ravager_mid")),
				 Plenty).IsEmpty());

	// A NODE IS FULL AT ITS OWN MAXIMUM AND NOT ONE MORE.
	if (!SpendMany(*this, NodeTable, EdgeTable, Allocation, TEXT("Ravager_mid"),
				   5, Plenty))
	{
		return false;
	}
	const FString Full = UCataclysmPassiveTree::RefusalForSpending(
		NodeTable, EdgeTable, Allocation, FName(TEXT("Ravager_mid")), Plenty);
	TestFalse(TEXT("a sixth point into a five-point node is refused"),
			  Full.IsEmpty());
	TestTrue(TEXT("and the refusal says it is full"), Full.Contains(TEXT("full")));

	// A KEYSTONE NEEDS ITS PARENT IN FULL, which is a rule the design states
	// separately and which the edge's own RequiredPoints already carries.
	TestTrue(TEXT("the keystone opens once its parent is full"),
			 UCataclysmPassiveTree::RefusalForSpending(
				 NodeTable, EdgeTable, Allocation, FName(TEXT("Ravager_stone")),
				 Plenty).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveEitherRouteOpensANodeTest,
	"Cataclysm.Passives.ANodeWithTwoWaysInOpensWhenEitherIsSatisfied",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveEitherRouteOpensANodeTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	UDataTable* EdgeTable = MakeEdgeTable(*this);
	if (!NodeTable || !EdgeTable)
	{
		return false;
	}

	// FIVE NODES ACROSS THE FOUR REAL TREES HAVE TWO INCOMING EDGES. Two edges
	// into one node are two routes to it rather than two requirements; reading
	// it as "all" would make those five need both parents, which nothing in the
	// design asks for and which can make a node unreachable.
	FCataclysmPassiveAllocation Allocation;
	constexpr int32 Plenty = 100;

	if (!SpendMany(*this, NodeTable, EdgeTable, Allocation,
				   TEXT("Ravager_root"), 1, Plenty))
	{
		return false;
	}

	TestFalse(TEXT("neither route is satisfied yet"),
			  UCataclysmPassiveTree::EdgesAllow(NodeTable, EdgeTable, Allocation,
												FName(TEXT("Ravager_joined"))));

	// THE CHEAPER ROUTE ALONE. Sidepath asks two, where the other route asks
	// four, so this satisfies one edge and leaves the other short.
	if (!SpendMany(*this, NodeTable, EdgeTable, Allocation,
				   TEXT("Ravager_side"), 2, Plenty))
	{
		return false;
	}

	TestTrue(TEXT("one satisfied route is enough"),
			 UCataclysmPassiveTree::EdgesAllow(NodeTable, EdgeTable, Allocation,
											   FName(TEXT("Ravager_joined"))));
	TestEqual(TEXT("and the other route is genuinely still short"),
			  Allocation.PointsIn(FName(TEXT("Ravager_mid"))), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveCapstoneTest,
	"Cataclysm.Passives.ACapstoneOpensOnPointsSpentInItsOwnTreeAndIsAChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveCapstoneTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	UDataTable* EdgeTable = MakeEdgeTable(*this);
	if (!NodeTable || !EdgeTable)
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	constexpr int32 Plenty = 100;
	const FName Capstone(TEXT("Ravager_cap"));

	FString Reason;
	TestFalse(TEXT("the capstone is shut below its threshold"),
			  UCataclysmPassiveTree::ChooseOption(NodeTable, Allocation,
												  Capstone, 1, Reason));
	TestTrue(TEXT("and the reason names the threshold"),
			 Reason.Contains(TEXT("4 points")));

	// FOUR POINTS SPENT IN THE TREE, BY ANY ROUTE. A capstone tier is reached by
	// a total rather than along a path, which is why capstones have no edges.
	if (!SpendMany(*this, NodeTable, EdgeTable, Allocation,
				   TEXT("Ravager_root"), 1, Plenty)
		|| !SpendMany(*this, NodeTable, EdgeTable, Allocation,
					  TEXT("Ravager_mid"), 3, Plenty))
	{
		return false;
	}
	TestEqual(TEXT("four points are in the tree"),
			  UCataclysmPassiveTree::SpentInTree(NodeTable, Allocation,
												 TEXT("Ravager")), 4);

	// THE POINT CANNOT GO IN BEFORE THE CHOICE IS MADE, because a capstone is a
	// choice rather than a bonus and spending first would leave nothing to
	// decide.
	const FString Undecided = UCataclysmPassiveTree::RefusalForSpending(
		NodeTable, EdgeTable, Allocation, Capstone, Plenty);
	TestFalse(TEXT("the capstone will not take a point undecided"),
			  Undecided.IsEmpty());
	TestTrue(TEXT("and asks for a choice"), Undecided.Contains(TEXT("Choose")));

	TestTrue(TEXT("an option can be taken at the threshold"),
			 UCataclysmPassiveTree::ChooseOption(NodeTable, Allocation, Capstone,
												 2, Reason));
	TestEqual(TEXT("and is remembered"), Allocation.ChosenOptionIn(Capstone), 2);

	// THE CHOICE IS PERMANENT AND EVERY CAPSTONE'S DESCRIPTION SAYS SO. Changing
	// it is what the Trainer's respec is for, which returns the whole tree.
	TestFalse(TEXT("it cannot be changed afterwards"),
			  UCataclysmPassiveTree::ChooseOption(NodeTable, Allocation,
												  Capstone, 3, Reason));
	TestTrue(TEXT("and the reason says it is permanent"),
			 Reason.Contains(TEXT("permanent")));

	TestTrue(TEXT("and now the point goes in"),
			 UCataclysmPassiveTree::Spend(NodeTable, EdgeTable, Allocation,
										  Capstone, Plenty, Reason));

	// A CAPSTONE WITH NO OPTIONS CANNOT BE TAKEN, which is what all four of the
	// Saboteur tree's capstones are today. Issue #935.
	const FName Empty(TEXT("Ravager_empty"));
	TestFalse(TEXT("a capstone offering nothing takes no option"),
			  UCataclysmPassiveTree::ChooseOption(NodeTable, Allocation, Empty,
												  1, Reason));
	TestTrue(TEXT("and says so, naming the issue"),
			 Reason.Contains(TEXT("#935")));
	TestFalse(TEXT("nor a point"),
			  UCataclysmPassiveTree::Spend(NodeTable, EdgeTable, Allocation,
										   Empty, Plenty, Reason));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveBudgetIsEnforcedTest,
	"Cataclysm.Passives.NothingCanBeSpentThatWasNotEarned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveBudgetIsEnforcedTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	UDataTable* EdgeTable = MakeEdgeTable(*this);
	if (!NodeTable || !EdgeTable)
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;

	// EXACTLY TWO POINTS TO SPEND.
	if (!SpendMany(*this, NodeTable, EdgeTable, Allocation,
				   TEXT("Ravager_root"), 1, 2)
		|| !SpendMany(*this, NodeTable, EdgeTable, Allocation,
					  TEXT("Ravager_mid"), 1, 2))
	{
		return false;
	}

	const FString Refusal = UCataclysmPassiveTree::RefusalForSpending(
		NodeTable, EdgeTable, Allocation, FName(TEXT("Ravager_mid")), 2);
	TestFalse(TEXT("a third point is refused"), Refusal.IsEmpty());
	TestTrue(TEXT("and the refusal says there are none left"),
			 Refusal.Contains(TEXT("No passive points left")));
	TestEqual(TEXT("and nothing was spent"), Allocation.Total(), 2);

	// A RESPEC RETURNS EVERYTHING, INCLUDING THE CAPSTONE CHOICES. A respec that
	// gave the points back but left the four decisions made would not be one.
	Allocation.Clear();
	TestEqual(TEXT("a reset returns every point"), Allocation.Total(), 0);
	TestEqual(TEXT("and forgets every node"), Allocation.Nodes.Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------
// Which trees a character can reach
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveReachabilityTest,
	"Cataclysm.Passives.OnlyTheTreesTheCarriedDamageTypeUnlocksCanBeSpentIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveReachabilityTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	// A DAMAGE TYPE UNLOCKS ITS THREE CLASSES AND A TREE IS NAMED AFTER ITS
	// CLASS. `UCataclysmCharacterCreation::ClassesFor` is the one list of which
	// classes belong to which damage type and this asks it, so the two cannot
	// disagree.
	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};
	const TArray<FName> War = {FName(TEXT("War"))};

	TestTrue(TEXT("a Demonic character reaches the Ravager tree"),
			 UCataclysmPassiveTree::TreeIsReachable(TEXT("Ravager"), Demonic));
	TestTrue(TEXT("and the Masochist tree"),
			 UCataclysmPassiveTree::TreeIsReachable(TEXT("Masochist"), Demonic));
	TestFalse(TEXT("but not the Bulwark tree, which is War"),
			  UCataclysmPassiveTree::TreeIsReachable(TEXT("Bulwark"), Demonic));

	TestTrue(TEXT("a War character reaches Bulwark"),
			 UCataclysmPassiveTree::TreeIsReachable(TEXT("Bulwark"), War));
	TestFalse(TEXT("and not Masochist"),
			  UCataclysmPassiveTree::TreeIsReachable(TEXT("Masochist"), War));

	// CARRYING BOTH REACHES BOTH, which is what multiclassing is: a weapon with
	// several damage types unlocks all their trees, out of one shared pool.
	const TArray<FName> Both = {FName(TEXT("Demonic")), FName(TEXT("War"))};
	TestTrue(TEXT("carrying both reaches Masochist"),
			 UCataclysmPassiveTree::TreeIsReachable(TEXT("Masochist"), Both));
	TestTrue(TEXT("and Bulwark"),
			 UCataclysmPassiveTree::TreeIsReachable(TEXT("Bulwark"), Both));

	// AND `TreeIsActive` IS THE SAME QUESTION, which is the project owner's
	// decision of 2026-08-25 written where the effects will be applied: points
	// stay spent and stop applying when the weapon that unlocked them comes off.
	TestFalse(TEXT("a tree the weapon no longer reaches is not active"),
			  UCataclysmPassiveTree::TreeIsActive(TEXT("Masochist"), War));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassivePointsStayWhenTheWeaponChangesTest,
	"Cataclysm.Passives.PointsStaySpentWhenTheWeaponThatUnlockedTheTreeComesOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassivePointsStayWhenTheWeaponChangesTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("a player state was spawned"), State))
	{
		return false;
	}

	// A DEMONIC CHARACTER, which is what one that has chosen nothing is.
	TestEqual(TEXT("the character is Demonic"), State->GetChosenDamageType(),
			  FName(TEXT("Demonic")));

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Masochist_basic_spine_000")), 1);
	State->SetPassiveAllocation(Allocation, {});
	TestEqual(TEXT("one point is in the Masochist tree"),
			  State->GetPassiveAllocation().Total(), 1);

	// THE WEAPON CHANGES TO A WAR ONE.
	State->SetCreationChoice(FName(TEXT("Sword")), FName(TEXT("War")));

	// THE POINT IS STILL SPENT. This is the project owner's decision of
	// 2026-08-25: taking off the weapon that unlocked a tree does not refund the
	// points. The alternative -- refunding, as Path of Exile 1 does when a
	// cluster jewel is unsocketed -- would make a weapon swap an unlimited free
	// respec, and this design already sells one at the Trainer for days.
	TestEqual(TEXT("the point is still spent"),
			  State->GetPassiveAllocation().Total(), 1);
	TestEqual(TEXT("and still in the same node"),
			  State->GetPassiveAllocation().PointsIn(
				  FName(TEXT("Masochist_basic_spine_000"))), 1);

	// AND THE TREE IS NO LONGER REACHABLE, so nothing further can go into it.
	TestFalse(TEXT("the Masochist tree is no longer reachable"),
			  State->ReachableTrees().Contains(TEXT("Masochist")));

	FString Reason;
	TestFalse(TEXT("so a further point into it is refused"),
			  State->SpendPassivePoint(FName(TEXT("Masochist_basic_spine_000")),
									   Reason));
	TestTrue(TEXT("and the reason names the weapon"),
			 Reason.Contains(TEXT("weapon")));

	return true;
}

// ---------------------------------------------------------------------------
// The real tables, and the one real entry point
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveRealTablesLoadTest,
	"Cataclysm.Passives.TheGeneratedTablesHoldTheFourTreesTheDesignHas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveRealTablesLoadTest::RunTest(const FString&)
{
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EdgeTable = UCataclysmPassiveTree::LoadEdgeTable();

	if (!TestNotNull(TEXT("the passive node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the passive edge table loads"), EdgeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const TArray<FString> Trees = UCataclysmPassiveTree::TreeNames(NodeTable);
	TestEqual(TEXT("four class trees"), Trees.Num(), 4);
	TestTrue(TEXT("Berserker"), Trees.Contains(TEXT("Berserker")));
	TestTrue(TEXT("Bulwark"), Trees.Contains(TEXT("Bulwark")));
	TestTrue(TEXT("Masochist"), Trees.Contains(TEXT("Masochist")));
	TestTrue(TEXT("Saboteur"), Trees.Contains(TEXT("Saboteur")));

	// EVERY TREE HOLDS FAR MORE THAN A CHARACTER CAN SPEND, which is the point
	// the design makes: "The per-character point budget is 230, meaning players
	// invest in roughly 53% of any tree -- specialization is required."
	for (const FString& Tree : Trees)
	{
		const TArray<FName> Nodes = UCataclysmPassiveTree::NodesIn(NodeTable, Tree);
		TestTrue(*FString::Printf(TEXT("%s has about seventy-four nodes"), *Tree),
				 Nodes.Num() >= 70 && Nodes.Num() <= 80);

		int32 Spendable = 0;
		for (const FName& Node : Nodes)
		{
			if (const FCataclysmPassiveNodeRow* Row =
					UCataclysmPassiveTree::FindNode(NodeTable, Node))
			{
				Spendable += Row->MaxPoints;
			}
		}
		TestTrue(*FString::Printf(
					 TEXT("%s holds %d points, more than the budget of %d"),
					 *Tree, Spendable, UCataclysmPassivePoints::Budget),
				 Spendable > UCataclysmPassivePoints::Budget);
	}

	// THE ONE NODE EACH TREE STARTS FROM. Every tree has exactly one node with
	// no incoming edge, and it is the one that unlocks the class resource. A
	// tree with none could never be started and nothing else would say so.
	FCataclysmPassiveAllocation Nothing;
	for (const FString& Tree : Trees)
	{
		int32 Open = 0;
		for (const FName& Node : UCataclysmPassiveTree::NodesIn(NodeTable, Tree))
		{
			const FCataclysmPassiveNodeRow* Row =
				UCataclysmPassiveTree::FindNode(NodeTable, Node);
			if (Row && Row->Kind != UCataclysmPassiveTree::CapstoneKind
				&& UCataclysmPassiveTree::EdgesAllow(NodeTable, EdgeTable,
													 Nothing, Node))
			{
				++Open;
			}
		}
		TestEqual(*FString::Printf(TEXT("%s can be started from exactly one node"),
								   *Tree), Open, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveReachesTheSaveRecordTest,
	"Cataclysm.Passives.TheTreeAndTheBossKillsAreWrittenToTheSaveRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveReachesTheSaveRecordTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	if (!TestNotNull(TEXT("a possessed player character"), Character))
	{
		return false;
	}

	ACataclysmPlayerState* State =
		Character->GetPlayerState<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("with a player state"), State))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Masochist_basic_spine_000")), 1);
	Allocation.SetChosenOption(FName(TEXT("Masochist_capstone_25")), 2);
	State->SetPassiveAllocation(Allocation, {});
	State->RecordCataclysmBossDefeat(FName(TEXT("Demonic")));

	UCataclysmCharacterSave* Record = NewObject<UCataclysmCharacterSave>();
	TestTrue(TEXT("the record was gathered"),
			 FCataclysmSaveGather::CharacterFrom(*Character, *Record));

	TestEqual(TEXT("the spent point is in the record"),
			  Record->PassiveAllocation.PointsIn(
				  FName(TEXT("Masochist_basic_spine_000"))), 1);
	TestEqual(TEXT("and the capstone choice with it"),
			  Record->PassiveAllocation.ChosenOptionIn(
				  FName(TEXT("Masochist_capstone_25"))), 2);
	TestEqual(TEXT("and the boss that was beaten"),
			  Record->DefeatedCataclysmBosses.Num(), 1);
	TestEqual(TEXT("by name"), Record->DefeatedCataclysmBosses[0],
			  FName(TEXT("Demonic")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveScreenSpendsThroughThePlayerStateTest,
	"Cataclysm.Passives.TheScreenSpendsThroughTheCharacterAndNotIntoItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveScreenSpendsThroughThePlayerStateTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	if (!TestNotNull(TEXT("a possessed player character"), Character))
	{
		return false;
	}

	APlayerController* Controller = Cast<APlayerController>(
		Character->GetController());
	ACataclysmPlayerState* State =
		Character->GetPlayerState<ACataclysmPlayerState>();
	if (!Controller || !State)
	{
		return false;
	}

	// THE WIDGET WITH NO BLUEPRINT AT ALL, which is every widget in a headless
	// test. Nothing it draws exists; everything it decides does.
	//
	// `NewObject` RATHER THAN `CreateWidget`, AND THE ENGINE FORCES IT.
	// `CreateWidget` refuses a controller that is not a LOCAL player controller,
	// and a world built by `UWorld::CreateWorld` has no game instance and so can
	// have no local player. The player state is handed over directly instead,
	// which is what an owning player would have supplied.
	UCataclysmPassiveTreeWidget* Screen =
		NewObject<UCataclysmPassiveTreeWidget>(Controller);
	if (!TestNotNull(TEXT("the screen was created"), Screen))
	{
		return false;
	}
	Screen->SetPlayerStateForTests(State);

	TestTrue(TEXT("the Masochist tree can be looked at"),
			 Screen->ShowTree(TEXT("Masochist")));
	TestFalse(TEXT("a tree that does not exist cannot"),
			  Screen->ShowTree(TEXT("Necromancer")));
	TestEqual(TEXT("so the Masochist tree is still the one shown"),
			  Screen->GetShownTree(), FString(TEXT("Masochist")));

	// A TREE THE CHARACTER CANNOT REACH CAN STILL BE LOOKED AT. A player
	// deciding which weapon to carry has to be able to read what the other trees
	// offer, and only spending is refused.
	TestTrue(TEXT("a War tree can be looked at by a Demonic character"),
			 Screen->ShowTree(TEXT("Bulwark")));
	TestFalse(TEXT("but not spent in"),
			  Screen->SpendInto(FName(TEXT("Bulwark_basic_trunk_000"))));
	TestTrue(TEXT("and the refusal says why"),
			 Screen->RefusalText().ToString().Contains(TEXT("weapon")));
	TestEqual(TEXT("and nothing was spent"),
			  State->GetPassiveAllocation().Total(), 0);

	// THE POINT OF THE TEST. Spending on the screen has to reach the player
	// state, which is what the save record is written from and what survives
	// death. A screen that kept its own copy would look right and save nothing.
	Screen->ShowTree(TEXT("Masochist"));
	const FName Root(TEXT("Masochist_basic_spine_000"));
	if (!TestTrue(TEXT("the Masochist root takes a point"),
				  Screen->SpendInto(Root)))
	{
		AddError(Screen->RefusalText().ToString());
		return false;
	}

	TestEqual(TEXT("and the point is on the player state"),
			  State->GetPassiveAllocation().PointsIn(Root), 1);
	TestEqual(TEXT("which is where the unspent count comes from"),
			  State->PassivePointsUnspent(),
			  State->PassivePointsAvailable() - 1);

	return true;
}


// ---------------------------------------------------------------------------
// What a spent point is worth
// ---------------------------------------------------------------------------

namespace CataclysmPassiveEffectTest
{
	/**
	 * An effect table this test controls, keyed to the built node table.
	 *
	 * THE ROW NAME IS NOT THE NODE. Since issue #953 the node is the `Node`
	 * column and the row name is that node with `#1`, `#2` and so on after it,
	 * because a node may have several rows and a DataTable key may not repeat.
	 * `Ravager_side` below has two, which is what proves both apply.
	 */
	UDataTable* MakeEffectTable(FAutomationTestBase& Test)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmPassiveEffectRow::StaticStruct();

		const TArray<FString> Problems = Table->CreateTableFromCSVString(TEXT(
			"Name,Node,Stat,ValueKind,ValuePerPoint,RequiredTags,Condition,ConditionValue,Scale,ScaleStep\r\n"
			// A plain increase on a node that holds five points, so the
			// multiplication by the points held is visible rather than assumed.
			"Ravager_mid#1,Ravager_mid,armor,increased,3.0,,,0,,0\r\n"
			// A more multiplier, which is the other bucket a passive may use.
			"Ravager_side#1,Ravager_side,damage_reduction,more,1.5,,,0,,0\r\n"
			// AND A SECOND STAT ON THAT SAME NODE. Issue #953. The Masochist's
			// starting node grants three Fervour rates at once and two other
			// nodes grant a health increase and an armour increase together, so
			// one row per node is a shape the design does not fit.
			"Ravager_side#2,Ravager_side,crit_multiplier,increased,7.0,,,0,,0\r\n"
			// A scoped one, to prove the tag column reaches the modifier.
			"Ravager_root#1,Ravager_root,area_of_effect,increased,10.0,Type.Trap,,0,,0\r\n"
			// AND ONE THAT DEPENDS ON THE CHARACTER'S HEALTH. Issue #959, and
			// it proves the two condition columns reach the modifier.
			"Ravager_low#1,Ravager_low,crit_chance,increased,3.0,,health_at_or_below,20,,0\r\n"
			// AND ONE THAT DEPENDS ON A WINDOW AFTER AN EVENT. Issue #962. It is
			// a second row on the SAME node deliberately: a new node would change
			// the rectangle the tree occupies and move an unrelated layout test's
			// answer.
			"Ravager_low#2,Ravager_low,attack_speed,increased,2.0,,seconds_after_health_cost,2,,0\r\n"
			// AND ONE WHOSE SIZE GROWS WITH A STATE rather than switching on and
			// off with it. Issue #968. A third row on the same node, for the same
			// reason the second one is.
			"Ravager_low#3,Ravager_low,max_health,increased,2.0,,,0,health_missing,5\r\n"
			// AND ONE UNDER THE SECOND KIND OF TIMED WINDOW. Issue #975. The
			// two windows are separate names and separate enumerators, so
			// covering one says nothing at all about the other.
			"Ravager_low#4,Ravager_low,movement_speed,increased,1.0,,seconds_after_foreign_damage,5,,0\r\n"
			// And one in the OTHER tree, which a Demonic character cannot reach.
			"Bulwark_root#1,Bulwark_root,armor,increased,50.0,,,0,,0\r\n"));

		for (const FString& Problem : Problems)
		{
			Test.AddError(Problem);
		}
		return Problems.Num() == 0 ? Table : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveEffectsBecomeModifiersTest,
	"Cataclysm.Passives.ASpentPointBecomesAStatModifierTimesThePointsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveEffectsBecomeModifiersTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmPassiveEffectTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	UDataTable* EffectTable = MakeEffectTable(*this);
	if (!NodeTable || !EffectTable)
	{
		return false;
	}

	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};

	// NOTHING SPENT GRANTS NOTHING, which is worth asserting rather than
	// assuming: an empty allocation and a missing table would look the same to
	// a caller that only checked the map was empty.
	FCataclysmPassiveAllocation Allocation;
	TestEqual(TEXT("an untouched character gets no modifiers"),
			  UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable,
												  EffectTable, Demonic).Num(),
			  0);

	// FOUR POINTS IN A NODE WORTH 3% EACH IS 12%, not 3%. Every authored value
	// is per point, which is what every description that has one says, so the
	// multiplication is the whole arithmetic and the easiest thing to leave out.
	Allocation.Add(FName(TEXT("Ravager_mid")), 4);

	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											Demonic);

	const TArray<FCataclysmStatModifier>* Armour =
		Modifiers.Find(FName(TEXT("armor")));
	if (!TestNotNull(TEXT("armour got a modifier"), Armour)
		|| !TestEqual(TEXT("exactly one"), Armour->Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("worth three per point times four points"),
			  (*Armour)[0].Value, 12.0f);
	TestEqual(TEXT("in the increased bucket"),
			  static_cast<int32>((*Armour)[0].Bucket),
			  static_cast<int32>(ECataclysmStatBucket::Increased));
	TestTrue(TEXT("and it applies to everything, having no required tag"),
			 (*Armour)[0].RequiredTags.IsEmpty());

	// THE MORE BUCKET IS CARRIED ACROSS. Putting a multiplicative value into the
	// increased bucket adds it to a sum instead of multiplying, which is a
	// different number on any invested character and the same number on a fresh
	// one -- so it looks right exactly where somebody would check it.
	Allocation.Add(FName(TEXT("Ravager_side")), 2);
	const TMap<FName, TArray<FCataclysmStatModifier>> WithMore =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											Demonic);
	const TArray<FCataclysmStatModifier>* Reduction =
		WithMore.Find(FName(TEXT("damage_reduction")));
	if (!TestNotNull(TEXT("damage reduction got a modifier"), Reduction))
	{
		return false;
	}
	TestEqual(TEXT("in the more bucket"),
			  static_cast<int32>((*Reduction)[0].Bucket),
			  static_cast<int32>(ECataclysmStatBucket::More));
	TestEqual(TEXT("worth one and a half per point times two"),
			  (*Reduction)[0].Value, 3.0f);

	// A REQUIRED TAG REACHES THE MODIFIER. Without this the Saboteur's trap
	// area of effect would widen every skill in the game rather than its traps.
	Allocation.Add(FName(TEXT("Ravager_root")), 1);
	const TMap<FName, TArray<FCataclysmStatModifier>> WithTag =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											Demonic);
	const TArray<FCataclysmStatModifier>* Area =
		WithTag.Find(FName(TEXT("area_of_effect")));
	if (!TestNotNull(TEXT("area of effect got a modifier"), Area))
	{
		return false;
	}
	TestFalse(TEXT("and it is scoped rather than global"),
			  (*Area)[0].RequiredTags.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveNodeWithTwoEffectsTest,
	"Cataclysm.Passives.ANodeGrantingTwoStatsGrantsBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveNodeWithTwoEffectsTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmPassiveEffectTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	UDataTable* EffectTable = MakeEffectTable(*this);
	if (!NodeTable || !EffectTable)
	{
		return false;
	}

	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};

	// ISSUE #953. `Ravager_side` has two rows in the fixture table: 1.5 more
	// damage reduction and 7% increased critical strike multiplier. Before this
	// a node could have exactly one, because the DataTable's row name WAS the
	// node name, and the second row could not be written down at all.
	//
	// THE FAILURE THIS CATCHES IS SILENT. A lookup that answered with the first
	// row and stopped would grant half of what the node says and report nothing
	// at all -- the character would simply be weaker than the tree promises.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Ravager_side")), 2);

	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											Demonic);

	const TArray<FCataclysmStatModifier>* Reduction =
		Modifiers.Find(FName(TEXT("damage_reduction")));
	const TArray<FCataclysmStatModifier>* Multiplier =
		Modifiers.Find(FName(TEXT("crit_multiplier")));

	if (!TestNotNull(TEXT("the node's first stat arrived"), Reduction)
		|| !TestNotNull(TEXT("and so did its second"), Multiplier))
	{
		return false;
	}

	// EACH IN ITS OWN BUCKET AND EACH TIMES THE POINTS HELD, which is what says
	// the second row went through the same path as the first rather than being
	// copied from it.
	TestEqual(TEXT("one and a half more damage reduction per point, twice"),
			  (*Reduction)[0].Value, 3.0f);
	TestEqual(TEXT("in the more bucket"),
			  static_cast<int32>((*Reduction)[0].Bucket),
			  static_cast<int32>(ECataclysmStatBucket::More));
	TestEqual(TEXT("seven percent increased critical multiplier per point, twice"),
			  (*Multiplier)[0].Value, 14.0f);
	TestEqual(TEXT("in the increased bucket"),
			  static_cast<int32>((*Multiplier)[0].Bucket),
			  static_cast<int32>(ECataclysmStatBucket::Increased));

	// AND THE COUNT SAYS TWO. A caller telling "nothing applied" from "nothing
	// was spent" reads this number, and a node with two rows added two things.
	TMap<FName, TArray<FCataclysmStatModifier>> Totals;
	TestEqual(TEXT("one node, two modifiers"),
			  UCataclysmPassiveTree::AccumulateInto(Totals, Allocation, NodeTable,
													EffectTable, Demonic),
			  2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveConditionReachesTheModifierTest,
	"Cataclysm.Passives.ANodesConditionReachesTheModifierItGrants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveConditionReachesTheModifierTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmPassiveEffectTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	UDataTable* EffectTable = MakeEffectTable(*this);
	if (!NodeTable || !EffectTable)
	{
		return false;
	}

	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};

	// ISSUE #959. `Ravager_low` carries `health_at_or_below` with 20, which is
	// the shape the Masochist's Last Stand node uses. The two columns have to
	// survive the trip from the table into the modifier; a condition dropped on
	// the way is a bonus that applies all the time instead of some of the time,
	// silently and in the player's favour.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Ravager_low")), 8);

	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											Demonic);

	const TArray<FCataclysmStatModifier>* Chance =
		Modifiers.Find(FName(TEXT("crit_chance")));
	if (!TestNotNull(TEXT("the node granted a critical strike chance"), Chance)
		|| !TestEqual(TEXT("exactly one"), Chance->Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("three per point times eight points"), (*Chance)[0].Value,
			  24.0f);
	TestEqual(TEXT("and it carries the health condition"),
			  static_cast<int32>((*Chance)[0].Condition),
			  static_cast<int32>(ECataclysmStatCondition::HealthAtOrBelowPercent));
	TestEqual(TEXT("at the threshold the table states"),
			  (*Chance)[0].ConditionValue, 20.0f);

	// AND THE SECOND KIND OF CONDITION MAKES THE SAME TRIP. Issue #962. The same
	// node carries `seconds_after_health_cost` with 2, which is the shape Blood
	// Rush uses. A name this build does not recognise is left unconditional
	// rather than refused, so without this the whole window would silently
	// become a bonus that holds all the time.
	const TArray<FCataclysmStatModifier>* Speed =
		Modifiers.Find(FName(TEXT("attack_speed")));
	if (TestNotNull(TEXT("the node also granted attack speed"), Speed)
		&& TestEqual(TEXT("exactly one of it"), Speed->Num(), 1))
	{
		TestEqual(TEXT("two per point times eight points"), (*Speed)[0].Value,
				  16.0f);
		TestEqual(TEXT("and it carries the window condition"),
				  static_cast<int32>((*Speed)[0].Condition),
				  static_cast<int32>(
					  ECataclysmStatCondition::WithinSecondsOfHealthCost));
		TestEqual(TEXT("for the number of seconds the table states"),
				  (*Speed)[0].ConditionValue, 2.0f);
	}

	// AND THE SECOND KIND OF WINDOW MAKES THE SAME TRIP. Issue #975. A
	// name left unrecognised is applied with NO condition at all -- a bonus
	// that holds all the time instead of for five seconds after one kind of
	// hit -- so each name needs its own check.
	const TArray<FCataclysmStatModifier>* Movement =
		Modifiers.Find(FName(TEXT("movement_speed")));
	if (TestNotNull(TEXT("the node also granted movement speed"), Movement)
		&& TestEqual(TEXT("exactly one of it"), Movement->Num(), 1))
	{
		TestEqual(TEXT("and it carries the foreign damage window"),
				  static_cast<int32>((*Movement)[0].Condition),
				  static_cast<int32>(
					  ECataclysmStatCondition::WithinSecondsOfForeignDamage));
		TestEqual(TEXT("for the number of seconds the table states"),
				  (*Movement)[0].ConditionValue, 5.0f);
	}

	// AND A ROW WITH NO CONDITION IS UNCONDITIONAL, which is every other row in
	// the fixture and every row in the game before this issue. Without this the
	// test above would pass just as well if every modifier came out conditional.
	Allocation.Add(FName(TEXT("Ravager_mid")), 1);
	const TMap<FName, TArray<FCataclysmStatModifier>> Both =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											Demonic);
	const TArray<FCataclysmStatModifier>* Armour =
		Both.Find(FName(TEXT("armor")));
	if (TestNotNull(TEXT("the unconditional node granted armour"), Armour))
	{
		TestEqual(TEXT("and it carries no condition"),
				  static_cast<int32>((*Armour)[0].Condition),
				  static_cast<int32>(ECataclysmStatCondition::Always));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveScaleReachesTheModifierTest,
	"Cataclysm.Passives.ANodesScaleReachesTheModifierItGrants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveScaleReachesTheModifierTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmPassiveEffectTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	UDataTable* EffectTable = MakeEffectTable(*this);
	if (!NodeTable || !EffectTable)
	{
		return false;
	}

	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};

	// ISSUE #968. `Ravager_low` carries a third row with `health_missing` and a
	// step of 5, which is the shape Vicious Onslaught uses. The two scale
	// columns have to survive the trip from the table into the modifier.
	//
	// A SCALE DROPPED ON THE WAY IS WORSE THAN A CONDITION DROPPED ON THE WAY.
	// A lost condition grants a bonus more often than the design said; a lost
	// scale would grant its FULL value at every state, which for a node like
	// this is the bonus a character at death's door earns, handed to one at
	// full health.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Ravager_low")), 8);

	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											Demonic);

	const TArray<FCataclysmStatModifier>* Health =
		Modifiers.Find(FName(TEXT("max_health")));
	if (!TestNotNull(TEXT("the node granted maximum health"), Health)
		|| !TestEqual(TEXT("exactly one of it"), Health->Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("two per point times eight points"), (*Health)[0].Value,
			  16.0f);
	TestEqual(TEXT("and it carries the missing-health scale"),
			  static_cast<int32>((*Health)[0].Scale),
			  static_cast<int32>(
				  ECataclysmStatScale::PerPercentOfMaximumHealthMissing));
	TestEqual(TEXT("in steps of the size the table states"),
			  (*Health)[0].ScaleStep, 5.0f);

	// AND A ROW WITH NO SCALE IS FIXED, which is every other row in the fixture
	// and every row in the game before this issue. Without this the checks above
	// would pass just as well if every modifier came out scaling.
	const TArray<FCataclysmStatModifier>* Chance =
		Modifiers.Find(FName(TEXT("crit_chance")));
	if (TestNotNull(TEXT("the unscaled row on the same node granted its stat"),
					Chance))
	{
		TestEqual(TEXT("and it carries no scale"),
				  static_cast<int32>((*Chance)[0].Scale),
				  static_cast<int32>(ECataclysmStatScale::Fixed));
		TestEqual(TEXT("and no step"), (*Chance)[0].ScaleStep, 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveStatsHaveAttributesTest,
	"Cataclysm.Passives.EveryStatAPassiveNodeGrantsHasAnAttributeBehindIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveStatsHaveAttributesTest::RunTest(const FString&)
{
	// A STAT WITH NO ATTRIBUTE BEHIND IT IS DROPPED IN SILENCE.
	// `UCataclysmPlayerClassStats::ApplyTo` loops over `StatToAttribute` rather
	// than over the modifiers, so a passive node granting a stat missing from
	// that map applies nothing, reports nothing, and leaves the character
	// exactly as if the node had never been bought. That is how `attack_speed`
	// was worth nothing for some time -- issue #120 -- and it is the failure a
	// misspelt stat name in the workbook produces.
	//
	// THE WHOLE FILE RATHER THAN THE THREE STATS THAT PROMPTED IT. The generator
	// checks a stat name against the class stat lines, the attribute table, and
	// since issue #954 against any flat row in the effects sheet itself, which a
	// name misspelt the same way twice would pass. This is what does not.
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const TMap<FString, FGameplayAttribute>& Attributes =
		UCataclysmPlayerClassStats::StatToAttribute();

	int32 Checked = 0;
	for (const TPair<FName, uint8*>& Row : EffectTable->GetRowMap())
	{
		const auto* Effect =
			reinterpret_cast<const FCataclysmPassiveEffectRow*>(Row.Value);
		if (!Effect || Effect->Stat.IsEmpty())
		{
			continue;
		}

		++Checked;
		TestTrue(*FString::Printf(
					 TEXT("%s grants '%s', which has an attribute behind it"),
					 *Row.Key.ToString(), *Effect->Stat),
				 Attributes.Contains(Effect->Stat));
	}

	// Without this the loop above passes on an empty table, which is what a
	// stale or unbuilt asset looks like.
	TestTrue(TEXT("the effect table has rows at all"), Checked >= 24);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveDormantTreeGrantsNothingTest,
	"Cataclysm.Passives.ATreeNoEquippedWeaponReachesGrantsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveDormantTreeGrantsNothingTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmPassiveEffectTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	UDataTable* EffectTable = MakeEffectTable(*this);
	if (!NodeTable || !EffectTable)
	{
		return false;
	}

	// POINTS IN BOTH TREES. Bulwark is War and Ravager is Demonic, so a
	// character carrying one damage type reaches exactly one of them.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Ravager_mid")), 1);
	Allocation.Add(FName(TEXT("Bulwark_root")), 1);

	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};
	const TMap<FName, TArray<FCataclysmStatModifier>> AsDemonic =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											Demonic);

	// THE PROJECT OWNER'S DECISION OF 2026-08-25, made arithmetic. The Bulwark
	// node is worth 50% armour and the Ravager one 3%; if the dormant tree were
	// counted the total would be 53.
	const TArray<FCataclysmStatModifier>* Armour =
		AsDemonic.Find(FName(TEXT("armor")));
	if (!TestNotNull(TEXT("armour got a modifier"), Armour)
		|| !TestEqual(TEXT("exactly one, from the reachable tree"),
					  Armour->Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("worth only what the reachable tree grants"),
			  (*Armour)[0].Value, 3.0f);

	// AND THE POINTS ARE STILL SPENT. Dormant is not refunded: the allocation is
	// untouched by any of this.
	TestEqual(TEXT("both points are still spent"), Allocation.Total(), 2);
	TestEqual(TEXT("including the one in the tree that grants nothing"),
			  Allocation.PointsIn(FName(TEXT("Bulwark_root"))), 1);

	// CARRYING BOTH DAMAGE TYPES TURNS BOTH TREES ON, which is what
	// multiclassing is.
	const TArray<FName> Both = {FName(TEXT("Demonic")), FName(TEXT("War"))};
	const TMap<FName, TArray<FCataclysmStatModifier>> AsBoth =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											Both);
	const TArray<FCataclysmStatModifier>* BothArmour =
		AsBoth.Find(FName(TEXT("armor")));
	if (!TestNotNull(TEXT("armour got modifiers"), BothArmour))
	{
		return false;
	}
	TestEqual(TEXT("now both trees contribute"), BothArmour->Num(), 2);

	// AND NO WEAPON AT ALL REACHES NOTHING.
	TestEqual(TEXT("a character carrying no damage type gets nothing"),
			  UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable,
												  EffectTable,
												  TArray<FName>()).Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveReachesTheCharactersArmourTest,
	"Cataclysm.Passives.ASpentPointChangesWhatARealCharactersArmourIsWorth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveReachesTheCharactersArmourTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	if (!TestNotNull(TEXT("a possessed player character"), Character))
	{
		return false;
	}

	ACataclysmPlayerState* State =
		Character->GetPlayerState<ACataclysmPlayerState>();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!State || !Equipment || !AbilitySystem)
	{
		AddError(TEXT("The spawned character is missing a component."));
		return false;
	}

	// THE REAL TABLES, because this is the end-to-end path: the point has to
	// reach an attribute on a real character through the real pipeline.
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	// A MASOCHIST NODE WORTH 3% ARMOUR PER POINT, on the Demonic tree the
	// character reaches by default.
	//
	// ASKED BY NODE AND NOT BY ROW NAME, which stopped being the same string on
	// issue #953 when a node gained the right to several effect rows.
	const FName Node(TEXT("Masochist_basic_fc_stem0"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("that node has one authored effect"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is armour"), Effects[0]->Stat, FString(TEXT("armor")));

	const float Before = AbilitySystem->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetArmorAttribute());

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 10);
	State->SetPassiveAllocation(Allocation, TArray<FName>());

	// THE ONE REAL ENTRY POINT. Nothing calls the passive tree directly here:
	// refreshing the character's attributes is what a worn item change does, and
	// it is where the passive tree was joined in.
	Equipment->RefreshAttributes(AbilitySystem);

	const float After = AbilitySystem->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetArmorAttribute());

	// THIRTY PER CENT MORE ARMOUR, from ten points worth three per cent each.
	// Asserted as a relationship rather than as a figure, because the base comes
	// from the class stat line at the character's level and pinning it here
	// would make this test fail whenever either is tuned.
	TestTrue(*FString::Printf(TEXT("armour rose from %.1f to %.1f"), Before, After),
			 After > Before);
	TestTrue(*FString::Printf(
				 TEXT("by about thirty per cent: %.1f against %.1f expected"),
				 After, Before * 1.30f),
			 FMath::IsNearlyEqual(After, Before * 1.30f, Before * 0.02f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveScopedNodeGrantsNothingTest,
	"Cataclysm.Passives.ATagScopedNodeReachesOnlyTheSkillsItNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A passive node scoped by a tag reaches the skills it names, and no others.
 *
 * WHAT THIS REPLACED. Until issue #943 this test was named
 * `ATagScopedNodeGrantsNothingYet` and asserted the opposite: that a node naming
 * any required tag reached nothing at all. That was true and it was a defect.
 * `UCataclysmPlayerClassStats::ApplyTo` works every stat out with an empty tag
 * container -- correctly, because a character sheet has no skill in hand -- and
 * then threw the base and the modifier list away, so a skill had nothing left to
 * ask with and every scoped modifier in the game was lost.
 *
 * WHAT MAKES IT WORK NOW. `ApplyTo` keeps what each stat was worked out from, as
 * `FCataclysmStatInputs` on the ability system component, and
 * `UCataclysmAbilitySystemComponent::StatForSkill` runs the same pipeline over
 * them with the skill's own tags. The project owner chose that route on
 * 2026-08-25; it is what Path of Exile and Last Epoch both do.
 *
 * THE THREE ASSERTIONS ARE A SET AND EACH ONE RULES SOMETHING OUT:
 *
 *   the trap skill gets it       -- or the modifier is still being discarded
 *   the untagged skill does not  -- or scoping has been turned off altogether,
 *                                   which would widen every skill in the game
 *   the character sheet does not -- because a sheet has no skill in hand, and
 *                                   that rule did not change
 *
 * THE UNSCOPED NODE IS THE CONTROL AND IT IS NOT DECORATION. Without it this
 * test would pass just as happily if the Saboteur tree were unreachable, if the
 * allocation never arrived, or if `RefreshAttributes` did nothing.
 */
bool FCataclysmPassiveScopedNodeGrantsNothingTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	if (!TestNotNull(TEXT("a possessed player character"), Character))
	{
		return false;
	}

	ACataclysmPlayerState* State =
		Character->GetPlayerState<ACataclysmPlayerState>();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!State || !Equipment || !AbilitySystem)
	{
		AddError(TEXT("The spawned character is missing a component."));
		return false;
	}

	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	// WAR RATHER THAN THE DEFAULT DEMONIC, because both nodes below are in War
	// trees: `docs/Cataclysm_GDD_v2.md` gives War the Bulwark, Berserker and
	// Saboteur classes. A character carrying Demonic reaches neither, and the
	// test would then measure a dormant tree instead of a dropped modifier.
	State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("War")));

	// THE ONLY SCOPED ROW IN game/Data/PassiveEffects.csv, and the only one this
	// gap can be shown with. It is worth 15% area of effect per point.
	const FName Scoped(TEXT("Saboteur_basic_trap_deep_001"));
	// AND AN UNSCOPED ONE IN THE OTHER WAR TREE, worth 3% armour per point.
	const FName Unscoped(TEXT("Bulwark_basic_trunk_001"));

	// READ OUT OF THE REAL TABLE RATHER THAN ASSUMED. Re-authoring the sheet is
	// how this test would quietly stop measuring anything: drop the tag from the
	// Saboteur row and the assertions below still pass while proving nothing.
	//
	// ASKED BY NODE AND NOT BY ROW NAME. They stopped being the same string on
	// issue #953, when a node gained the right to several effect rows.
	const TArray<const FCataclysmPassiveEffectRow*> ScopedRows =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Scoped);
	const TArray<const FCataclysmPassiveEffectRow*> UnscopedRows =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Unscoped);
	if (!TestEqual(TEXT("the scoped node has one authored effect"),
				   ScopedRows.Num(), 1)
		|| !TestEqual(TEXT("and so does the unscoped one"),
					  UnscopedRows.Num(), 1))
	{
		return false;
	}
	const FCataclysmPassiveEffectRow* ScopedRow = ScopedRows[0];
	const FCataclysmPassiveEffectRow* UnscopedRow = UnscopedRows[0];
	TestEqual(TEXT("the scoped one is area of effect"), ScopedRow->Stat,
			  FString(TEXT("area_of_effect")));
	TestFalse(TEXT("and it really is scoped"), ScopedRow->RequiredTags.IsEmpty());
	TestEqual(TEXT("the unscoped one is armour"), UnscopedRow->Stat,
			  FString(TEXT("armor")));
	TestTrue(TEXT("and it really is unscoped"),
			 UnscopedRow->RequiredTags.IsEmpty());

	const float AreaBefore = AbilitySystem->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetAreaOfEffectAttribute());
	const float ArmourBefore = AbilitySystem->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetArmorAttribute());

	// BOTH NODES FILLED, IN ONE ALLOCATION AND ONE REFRESH, so the two results
	// cannot differ because of anything but the tag.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Scoped, 6);
	Allocation.Add(Unscoped, 8);
	State->SetPassiveAllocation(Allocation, TArray<FName>());

	Equipment->RefreshAttributes(AbilitySystem);

	const float AreaAfter = AbilitySystem->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetAreaOfEffectAttribute());
	const float ArmourAfter = AbilitySystem->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetArmorAttribute());

	// THE CONTROL. Eight points at 3% each is 24% more armour, so the tree is
	// reachable, the points arrived, and the refresh ran.
	TestTrue(*FString::Printf(TEXT("armour rose from %.1f to %.1f"),
							  ArmourBefore, ArmourAfter),
			 ArmourAfter > ArmourBefore);
	TestTrue(*FString::Printf(
				 TEXT("by about a quarter: %.1f against %.1f expected"),
				 ArmourAfter, ArmourBefore * 1.24f),
			 FMath::IsNearlyEqual(ArmourAfter, ArmourBefore * 1.24f,
								  ArmourBefore * 0.02f));

	// THE CHARACTER SHEET IS UNCHANGED, AND THAT IS CORRECT RATHER THAN THE BUG.
	// A sheet has no skill in hand, so a bonus that applies only to traps must
	// not be shown as though it applied to everything. Issue #943 changed where
	// the scoped bonus is applied, not this rule.
	TestEqual(*FString::Printf(
				  TEXT("the character sheet's area of effect is unchanged: "
					   "%.1f against %.1f"),
				  AreaAfter, AreaBefore),
			  AreaAfter, AreaBefore);

	// AND NOW THE POINT OF ALL OF IT. Six points at 15% each is 90% more area of
	// effect, for a skill that carries the tag the node names and for no other.
	FGameplayTagContainer TrapTags;
	TrapTags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false));
	if (!TestEqual(TEXT("the trap tag exists in the vocabulary"),
				   TrapTags.Num(), 1))
	{
		return false;
	}

	const float ForATrap = AbilitySystem->StatForSkill(
		FName(TEXT("area_of_effect")), TrapTags, AreaAfter);
	const float ForAnythingElse = AbilitySystem->StatForSkill(
		FName(TEXT("area_of_effect")), FGameplayTagContainer(), AreaAfter);

	TestTrue(*FString::Printf(
				 TEXT("a trap is widened by about ninety per cent: %.1f against "
					  "%.1f expected, from a sheet value of %.1f"),
				 ForATrap, AreaAfter * 1.90f, AreaAfter),
			 FMath::IsNearlyEqual(ForATrap, AreaAfter * 1.90f,
								  AreaAfter * 0.02f));

	// WITHOUT THIS THE TEST WOULD PASS IF SCOPING WERE TURNED OFF ALTOGETHER,
	// which would widen every skill in the game instead of the traps.
	TestEqual(*FString::Printf(
				  TEXT("and a skill carrying no tags is not widened: %.1f "
					   "against %.1f"),
				  ForAnythingElse, AreaAfter),
			  ForAnythingElse, AreaAfter);

	return true;
}


// ---------------------------------------------------------------------------
// Where the tree is drawn
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveExtentTest,
	"Cataclysm.Passives.ATreesExtentIsTheRectangleItsNodesOccupy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveExtentTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	if (!NodeTable)
	{
		return false;
	}

	// The built tree's Ravager nodes sit at (0,0), (0,100), (0,200), (100,100),
	// (-100,100), (300,0) and (300,100).
	const FCataclysmTreeExtent Extent =
		UCataclysmPassiveTreeLayout::ExtentOf(NodeTable, TEXT("Ravager"));

	TestTrue(TEXT("nodes were found"), Extent.bAny);
	TestEqual(TEXT("the leftmost node"), Extent.Least.X, -100.0);
	TestEqual(TEXT("the topmost"), Extent.Least.Y, 0.0);
	TestEqual(TEXT("the rightmost"), Extent.Most.X, 300.0);
	TestEqual(TEXT("the lowest"), Extent.Most.Y, 200.0);
	TestEqual(TEXT("so the centre is between them"), Extent.Centre(),
			  FVector2D(100.0, 100.0));
	TestEqual(TEXT("and the span is the difference"), Extent.Span(),
			  FVector2D(400.0, 200.0));

	// A TREE THAT IS NOT THERE HAS NO EXTENT, and `bAny` is how a caller tells
	// that from a tree whose nodes all sit on one point. Fitting a view to a
	// rectangle of nothing would divide by zero.
	const FCataclysmTreeExtent Missing =
		UCataclysmPassiveTreeLayout::ExtentOf(NodeTable, TEXT("Necromancer"));
	TestFalse(TEXT("a tree that does not exist has no extent"), Missing.bAny);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveZoomToFitTest,
	"Cataclysm.Passives.TheViewFitsTheWholeTreeInsideThePanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveZoomToFitTest::RunTest(const FString&)
{
	FCataclysmTreeExtent Extent;
	Extent.bAny = true;
	Extent.Least = FVector2D(-500.0, -100.0);
	Extent.Most = FVector2D(500.0, 100.0);          // 1000 wide, 200 tall

	// A PANEL WITH ROOM FOR 800 BY 620 ONCE THE MARGIN IS TAKEN OFF BOTH SIDES.
	// The width allows 800/1000 = 0.8 and the height 620/200 = 3.1.
	const FVector2D Panel(800.0 + UCataclysmPassiveTreeLayout::FitMarginPx * 2.0,
						  620.0 + UCataclysmPassiveTreeLayout::FitMarginPx * 2.0);

	// THE SMALLER OF THE TWO, so the tree fits both ways rather than one. Taking
	// the larger would fit the height and run a fifth of the width off each
	// side, which is the mistake that looks right on a tall tree.
	TestEqual(TEXT("the width binds, not the height"),
			  UCataclysmPassiveTreeLayout::ZoomToFit(Extent, Panel), 0.8f);

	// AND THE ANSWER IS CLAMPED. A panel far larger than the tree would
	// otherwise fit it at a zoom of ten and draw one node across the screen.
	const FVector2D Huge(100000.0, 100000.0);
	TestEqual(TEXT("a very large panel is capped at the largest zoom"),
			  UCataclysmPassiveTreeLayout::ZoomToFit(Extent, Huge),
			  UCataclysmPassiveTreeLayout::LargestZoom);

	// A PANEL SMALLER THAN THE MARGIN LEAVES NEGATIVE ROOM, which would give a
	// negative zoom and mirror the whole tree.
	const FVector2D Tiny(10.0, 10.0);
	TestEqual(TEXT("a panel smaller than its own margin is capped at the "
				   "smallest zoom"),
			  UCataclysmPassiveTreeLayout::ZoomToFit(Extent, Tiny),
			  UCataclysmPassiveTreeLayout::SmallestZoom);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveScreenPositionTest,
	"Cataclysm.Passives.WhatIsFocusedSitsInTheMiddleOfThePanelAtAnyZoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveScreenPositionTest::RunTest(const FString&)
{
	const FVector2D Panel(1000.0, 600.0);
	const FVector2D Focus(200.0, -50.0);

	// THE POINT BEING LOOKED AT IS IN THE MIDDLE, WHATEVER THE ZOOM. That is
	// what makes zooming keep looking at the same thing rather than drifting
	// towards a corner, and it is the property the whole view rests on.
	for (const float Zoom : {0.25f, 1.0f, 2.5f})
	{
		TestEqual(*FString::Printf(TEXT("the focus is centred at zoom %.2f"), Zoom),
				  UCataclysmPassiveTreeLayout::ScreenPositionFor(Focus, Focus,
																 Zoom, Panel),
				  Panel * 0.5);
	}

	// A NODE 100 UNITS RIGHT OF THE FOCUS IS 100 PIXELS RIGHT AT A ZOOM OF ONE,
	// and 200 at a zoom of two.
	const FVector2D Right = Focus + FVector2D(100.0, 0.0);
	TestEqual(TEXT("at a zoom of one"),
			  UCataclysmPassiveTreeLayout::ScreenPositionFor(Right, Focus, 1.0f,
															 Panel),
			  Panel * 0.5 + FVector2D(100.0, 0.0));
	TestEqual(TEXT("and twice as far at a zoom of two"),
			  UCataclysmPassiveTreeLayout::ScreenPositionFor(Right, Focus, 2.0f,
															 Panel),
			  Panel * 0.5 + FVector2D(200.0, 0.0));

	// AND THE REVERSE ANSWERS THE SAME POINT. Turning a click into a place in
	// the tree is the same arithmetic backwards, so a mistake in either would
	// show up here rather than as a click that selected the wrong node.
	const FVector2D Somewhere(613.0, 77.0);
	const FVector2D ThereAndBack = UCataclysmPassiveTreeLayout::ScreenPositionFor(
		UCataclysmPassiveTreeLayout::TreePositionFor(Somewhere, Focus, 1.7f, Panel),
		Focus, 1.7f, Panel);
	TestTrue(*FString::Printf(TEXT("a point survives the round trip: %s against %s"),
							  *ThereAndBack.ToString(), *Somewhere.ToString()),
			 ThereAndBack.Equals(Somewhere, 0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveEdgeGeometryTest,
	"Cataclysm.Passives.AnEdgeIsARectangleTurnedToPointAtItsFarEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveEdgeGeometryTest::RunTest(const FString&)
{
	FVector2D At;
	FVector2D Size;
	float Angle = 0.0f;

	// STRAIGHT RIGHT IS NO ROTATION AT ALL, which is the case that tells a sign
	// mistake from a scale mistake.
	UCataclysmPassiveTreeLayout::EdgeGeometry(FVector2D(10.0, 20.0),
											  FVector2D(110.0, 20.0), 3.0f,
											  At, Size, Angle);
	TestEqual(TEXT("it starts at the near end"), At, FVector2D(10.0, 20.0));
	TestEqual(TEXT("it is as long as the gap"), Size.X, 100.0);
	TestEqual(TEXT("and as thick as asked"), Size.Y, 3.0);
	TestEqual(TEXT("and points right"), Angle, 0.0f);

	// STRAIGHT DOWN IS NINETY DEGREES, POSITIVE. Screen y grows downwards, so a
	// line to a node below turns clockwise. Getting this sign wrong mirrors
	// every edge about the horizontal and the tree looks plausible upside down,
	// which is why it is asserted rather than left to be noticed.
	UCataclysmPassiveTreeLayout::EdgeGeometry(FVector2D(0.0, 0.0),
											  FVector2D(0.0, 50.0), 3.0f,
											  At, Size, Angle);
	TestEqual(TEXT("a line to a node below is fifty long"), Size.X, 50.0);
	TestEqual(TEXT("and turns ninety degrees clockwise"), Angle, 90.0f);

	// AND A DIAGONAL IS THE HYPOTENUSE.
	UCataclysmPassiveTreeLayout::EdgeGeometry(FVector2D(0.0, 0.0),
											  FVector2D(30.0, 40.0), 3.0f,
											  At, Size, Angle);
	TestEqual(TEXT("three, four, five"), Size.X, 50.0);

	// TWO POINTS IN THE SAME PLACE HAVE NO DIRECTION AND MUST NOT BE AN ERROR.
	UCataclysmPassiveTreeLayout::EdgeGeometry(FVector2D(7.0, 7.0),
											  FVector2D(7.0, 7.0), 3.0f,
											  At, Size, Angle);
	TestEqual(TEXT("a zero-length edge is zero long"), Size.X, 0.0);
	TestEqual(TEXT("and points right rather than nowhere"), Angle, 0.0f);

	// A LINE IS NEVER THINNER THAN ONE PIXEL, however far the view is zoomed
	// out. At the smallest zoom the thickness asked for is 0.3, and a rectangle
	// less than a pixel tall is drawn as nothing at all.
	UCataclysmPassiveTreeLayout::EdgeGeometry(FVector2D(0.0, 0.0),
											  FVector2D(10.0, 0.0), 0.3f,
											  At, Size, Angle);
	TestEqual(TEXT("a very thin line is still one pixel"), Size.Y, 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveZoomStepsTest,
	"Cataclysm.Passives.EveryWheelNotchChangesTheViewByTheSameProportion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveZoomStepsTest::RunTest(const FString&)
{
	// A FACTOR PER NOTCH RATHER THAN AN AMOUNT. Adding a fixed step makes one
	// notch nothing when zoomed out and an enormous jump when zoomed in.
	const float In = UCataclysmPassiveTreeLayout::ZoomAfterNotches(1.0f, 1.0f);
	TestTrue(TEXT("one notch in makes it larger"), In > 1.0f);

	const float Back = UCataclysmPassiveTreeLayout::ZoomAfterNotches(In, -1.0f);
	TestTrue(*FString::Printf(TEXT("and one notch back returns to where it was: "
								   "%.4f against 1.0"), Back),
			 FMath::IsNearlyEqual(Back, 1.0f, 0.0001f));

	// THE RANGE HOLDS AT BOTH ENDS whatever is asked for.
	TestEqual(TEXT("scrolling out forever stops at the smallest"),
			  UCataclysmPassiveTreeLayout::ZoomAfterNotches(1.0f, -1000.0f),
			  UCataclysmPassiveTreeLayout::SmallestZoom);
	TestEqual(TEXT("and scrolling in forever stops at the largest"),
			  UCataclysmPassiveTreeLayout::ZoomAfterNotches(1.0f, 1000.0f),
			  UCataclysmPassiveTreeLayout::LargestZoom);

	// NOT-A-NUMBER IS CLAMPED RATHER THAN PASSED THROUGH. Every comparison
	// against it is false, so FMath::Clamp on its own would hand it back and
	// every node would be placed nowhere at all.
	TestEqual(TEXT("a zoom that is not a number becomes the smallest"),
			  UCataclysmPassiveTreeLayout::ClampZoom(
				  std::numeric_limits<float>::quiet_NaN()),
			  UCataclysmPassiveTreeLayout::SmallestZoom);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveRealTreesFitTest,
	"Cataclysm.Passives.EveryRealTreeFitsAnOrdinaryPanelAtAReadableZoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveRealTreesFitTest::RunTest(const FString&)
{
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the passive node table loads"), NodeTable))
	{
		return false;
	}

	// A PANEL ABOUT THE SIZE THE SCREEN ACTUALLY GETS, measured from a play
	// session at 2500 pixels wide.
	const FVector2D Panel(2400.0, 900.0);

	for (const FString& Tree : UCataclysmPassiveTree::TreeNames(NodeTable))
	{
		const FCataclysmTreeExtent Extent =
			UCataclysmPassiveTreeLayout::ExtentOf(NodeTable, Tree);
		TestTrue(*FString::Printf(TEXT("%s has an extent"), *Tree), Extent.bAny);

		const float Fit = UCataclysmPassiveTreeLayout::ZoomToFit(Extent, Panel);

		// NOT SQUEEZED TO THE FLOOR. If a real tree needed the smallest zoom to
		// fit, the whole thing would open as a field of dots and the fit would
		// be useless. Measured: the widest tree is 3,600 units and the panel
		// leaves 2,220, so the tightest fit is about 0.6.
		TestTrue(*FString::Printf(
					 TEXT("%s fits at a readable zoom of %.2f, not the floor"),
					 *Tree, Fit),
				 Fit > UCataclysmPassiveTreeLayout::SmallestZoom * 2.0f);

		// AND EVERY NODE REALLY IS INSIDE THE PANEL AT THAT ZOOM, which is the
		// thing "fit" means and the thing the arithmetic could get wrong.
		for (const FName& Node : UCataclysmPassiveTree::NodesIn(NodeTable, Tree))
		{
			const FCataclysmPassiveNodeRow* Row =
				UCataclysmPassiveTree::FindNode(NodeTable, Node);
			if (!Row)
			{
				continue;
			}

			const FVector2D At = UCataclysmPassiveTreeLayout::ScreenPositionFor(
				FVector2D(Row->PositionX, Row->PositionY), Extent.Centre(), Fit,
				Panel);

			TestTrue(*FString::Printf(TEXT("%s lands inside the panel at %s"),
									  *Node.ToString(), *At.ToString()),
					 At.X >= 0.0 && At.X <= Panel.X
						 && At.Y >= 0.0 && At.Y <= Panel.Y);
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
