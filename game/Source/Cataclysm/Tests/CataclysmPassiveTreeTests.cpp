// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmPassiveTreeWidget.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

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
 * WHAT IS DELIBERATELY NOT COVERED. What a spent point is worth, because it does
 * not exist: a node's effect is a sentence written for a player and there is no
 * number behind it anywhere in the design files. Issue #936. And anything a
 * person can see: the automation command passes `-nullrhi`, so
 * `WBP_PassiveTree` cannot be loaded and no widget draws. The screen's logic is
 * reached by constructing `UCataclysmPassiveTreeWidget` with no Blueprint at
 * all, which is why every bound pointer in it is checked before it is used.
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

#endif // WITH_AUTOMATION_TESTS
