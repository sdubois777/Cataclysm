// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
// For the map from a stat name to the attribute it drives. Issue #954.
#include "Character/CataclysmPlayerClassStats.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For the swing time Thirst for Pain shortens. Issue #962.
#include "AbilitySystem/CataclysmBasicAttack.h"
// For resolving a real hit against a real character, and the two damage-taken
// stat names. Issue #1026.
#include "AbilitySystem/CataclysmDamageCalculation.h"
// For judging a condition directly, to pin the Apotheosis interaction. #1029.
#include "AbilitySystem/CataclysmStatPipeline.h"
// For the conversion window The Breaking Point lengthens. Issue #1025.
#include "AbilitySystem/CataclysmDamageConversion.h"
// For the debuffs the five new nodes read. Issue #962.
#include "AbilitySystem/CataclysmDebuffs.h"
// For the three Fervour rate stat names and the function that reads one back
// through the stat pipeline. Issue #978.
#include "AbilitySystem/CataclysmFervour.h"
// For putting a real bleed on a real character. Issue #962.
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
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
// For the console variable that says which class a character is. Issue #980.
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
// For capturing what a console command printed. Issue #962.
#include "Misc/StringOutputDevice.h"

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

	/**
	 * Sets `Cataclysm.PlayerClass` and puts it back when it goes out of scope.
	 *
	 * WHY A TEST NEEDS THIS. `UCataclysmEquipmentComponent::RefreshAttributes`
	 * reads the class from that console variable, and which class a character is
	 * decides which base every stat stands on. A node granting an increase on a
	 * stat that only one class line names -- `retaliation` is the Masochist's
	 * alone -- multiplies zero on any other class. Issue #980.
	 *
	 * PUT BACK IN THE DESTRUCTOR, because the variable is global to the process
	 * and the automation tests share one. A test that left it set would change
	 * whichever test ran next, which is the shape of defect issue #888 records.
	 *
	 * The same helper `CataclysmCharacterLevelTests.cpp` has for the level, one
	 * variable across.
	 */
	struct FScopedPlayerClass
	{
		explicit FScopedPlayerClass(const TCHAR* ClassName)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(
				TEXT("Cataclysm.PlayerClass"));
			if (Variable)
			{
				Previous = Variable->GetString();
				Variable->Set(ClassName, ECVF_SetByCode);
			}
		}

		~FScopedPlayerClass()
		{
			if (Variable)
			{
				Variable->Set(*Previous, ECVF_SetByCode);
			}
		}

		bool IsUsable() const { return Variable != nullptr; }

		IConsoleVariable* Variable = nullptr;
		FString Previous;
	};
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
			"Name,Node,Stat,ValueKind,ValuePerPoint,RequiredTags,Condition,ConditionValue,Scale,ScaleStep,Option\r\n"
			// A plain increase on a node that holds five points, so the
			// multiplication by the points held is visible rather than assumed.
			"Ravager_mid#1,Ravager_mid,armor,increased,3.0,,,0,,0,0\r\n"
			// A more multiplier, which is the other bucket a passive may use.
			"Ravager_side#1,Ravager_side,damage_reduction,more,1.5,,,0,,0,0\r\n"
			// AND A SECOND STAT ON THAT SAME NODE. Issue #953. The Masochist's
			// starting node grants three Fervour rates at once and two other
			// nodes grant a health increase and an armour increase together, so
			// one row per node is a shape the design does not fit.
			"Ravager_side#2,Ravager_side,crit_multiplier,increased,7.0,,,0,,0,0\r\n"
			// A scoped one, to prove the tag column reaches the modifier.
			"Ravager_root#1,Ravager_root,area_of_effect,increased,10.0,Type.Trap,,0,,0,0\r\n"
			// AND ONE THAT DEPENDS ON THE CHARACTER'S HEALTH. Issue #959, and
			// it proves the two condition columns reach the modifier.
			"Ravager_low#1,Ravager_low,crit_chance,increased,3.0,,health_at_or_below,20,,0,0\r\n"
			// AND ONE THAT DEPENDS ON A WINDOW AFTER AN EVENT. Issue #962. It is
			// a second row on the SAME node deliberately: a new node would change
			// the rectangle the tree occupies and move an unrelated layout test's
			// answer.
			"Ravager_low#2,Ravager_low,attack_speed,increased,2.0,,seconds_after_health_cost,2,,0,0\r\n"
			// AND ONE WHOSE SIZE GROWS WITH A STATE rather than switching on and
			// off with it. Issue #968. A third row on the same node, for the same
			// reason the second one is.
			"Ravager_low#3,Ravager_low,max_health,increased,2.0,,,0,health_missing,5,0\r\n"
			// AND ONE UNDER THE SECOND KIND OF TIMED WINDOW. Issue #975. The
			// two windows are separate names and separate enumerators, so
			// covering one says nothing at all about the other.
			"Ravager_low#4,Ravager_low,movement_speed,increased,1.0,,seconds_after_foreign_damage,5,,0,0\r\n"
			// AND ONE THAT GROWS WITH WHAT THE CHARACTER OWES. Issue #994. A
			// SECOND scale, and telling it apart from the one above is the
			// point: health missing and health owed are different states of one
			// character, so a build that mapped either name onto either
			// enumerator would pass every check written before this row.
			"Ravager_low#5,Ravager_low,life_leech,increased,1.0,,,0,health_owed,5,0\r\n"
			// AND THREE COUNTS OF STACKS, ONE PER KIND. Issues #1002, #1003 and
			// #1004. All three are here rather than one of them, because the
			// three names must not be interchangeable: each kind is granted by a
			// different event and lasts a different length of time, and a build
			// that mapped two of the names onto one enumerator would hand a node
			// somebody else's stacks with nothing reporting it. One row cannot
			// catch that; three can.
			"Ravager_low#6,Ravager_low,armor,increased,1.0,,,0,momentum_stacks,1,0\r\n"
			"Ravager_low#7,Ravager_low,magic_find,increased,1.0,,,0,bloodlust_stacks,1,0\r\n"
			"Ravager_low#8,Ravager_low,dot_damage,increased,1.0,,,0,carnage_stacks,1,0\r\n"
			// AND A COUNT OF THE DEBUFFS THE CHARACTER IS UNDER. Issue #962. A
			// fourth count beside the three stacks, and its own row for the same
			// argument: a build that mapped this name onto a stack enumerator
			// would count something the character EARNED instead of something
			// being DONE to it, and every check above would still pass.
			"Ravager_low#9,Ravager_low,spell_damage,increased,1.0,,,0,debuffs_carried,1,0\r\n"
			// AND A CONDITION THAT NAMES AN EFFECT RATHER THAN A THRESHOLD.
			// Issue #962. It is the only condition that reads no value, so it is
			// the only one where the value column could be carried across and
			// compared against with nothing reporting it.
			"Ravager_low#10,Ravager_low,evasion,increased,3.0,,while_bleeding,0,,0,0\r\n"
			// And one in the OTHER tree, which a Demonic character cannot reach.
			"Bulwark_root#1,Bulwark_root,armor,increased,50.0,,,0,,0,0\r\n"
			// A CAPSTONE'S THREE OPTIONS, ONE ROW EACH. Issue #1029. Only the
			// option the player chose may apply, and a capstone with no choice
			// made yet grants none of the three.
			//
			// THREE RATHER THAN TWO, AND THE THIRD EARNS ITS PLACE. Two would
			// prove a chosen option applies and an unchosen one does not; only a
			// third says the skip is by option NUMBER rather than by "not the
			// first one". Each grants a different stat so the test can tell which
			// of the three arrived.
			"Ravager_cap#1,Ravager_cap,armor,increased,10.0,,,0,,0,1\r\n"
			"Ravager_cap#2,Ravager_cap,evasion,increased,20.0,,,0,,0,2\r\n"
			"Ravager_cap#3,Ravager_cap,magic_find,increased,30.0,,,0,,0,3\r\n"));

		for (const FString& Problem : Problems)
		{
			Test.AddError(Problem);
		}
		return Problems.Num() == 0 ? Table : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveCapstoneOptionTest,
	"Cataclysm.Passives.OnlyTheCapstoneOptionTheCharacterChoseGrantsAnything",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A capstone offers three options and only the chosen one applies. Issue #1029.
 *
 * WHAT WAS MISSING AND WHY IT WAS INVISIBLE. The choice has been stored, offered
 * and printed since the tree screen was built -- `ChoosePassiveOption`,
 * `ChosenOptionIn`, the console command and the widget all existed. The effects
 * sheet had no way to say which option a row belonged to, so no capstone row
 * could be authored at all, and the four Masochist capstones granted nothing.
 *
 * THREE OPTIONS RATHER THAN TWO, AND THE THIRD EARNS ITS PLACE. Two would prove
 * that a chosen option applies and an unchosen one does not; only a third says
 * the skip is by option NUMBER rather than by "not the first one". Each of the
 * three fixture rows grants a different stat, so the answer says which arrived.
 *
 * AND THE NO-CHOICE CASE IS ASSERTED FIRST, because it is the one a build gets
 * wrong in the player's favour: a capstone with points in it and no choice made
 * must grant none of the three, not all of them and not the first.
 */
bool FCataclysmPassiveCapstoneOptionTest::RunTest(const FString&)
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
	const FName Capstone(TEXT("Ravager_cap"));

	// The three stats the three options grant, in order.
	const TArray<FName> PerOption = {
		FName(TEXT("armor")), FName(TEXT("evasion")), FName(TEXT("magic_find"))};

	const auto GrantedStats = [&](const FCataclysmPassiveAllocation& Allocation)
	{
		const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
			UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
												Demonic);
		TArray<FName> Found;
		for (const FName& Stat : PerOption)
		{
			if (Modifiers.Contains(Stat))
			{
				Found.Add(Stat);
			}
		}
		return Found;
	};

	// A POINT IN THE CAPSTONE AND NO CHOICE MADE. The point is spent and the node
	// is held; nothing has been picked, so nothing is granted.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Capstone, 1);
	TestEqual(TEXT("the point really is in the node"),
			  Allocation.PointsIn(Capstone), 1);
	TestEqual(TEXT("and no option has been chosen"),
			  Allocation.ChosenOptionIn(Capstone), 0);
	TestEqual(TEXT("so none of the three options grants anything"),
			  GrantedStats(Allocation).Num(), 0);

	// NOW EACH OPTION IN TURN, and each must grant its own stat and no other.
	for (int32 Option = 1; Option <= UCataclysmPassiveTree::CapstoneOptions;
		 ++Option)
	{
		Allocation.SetChosenOption(Capstone, Option);
		TestEqual(*FString::Printf(TEXT("option %d is recorded"), Option),
				  Allocation.ChosenOptionIn(Capstone), Option);

		const TArray<FName> Granted = GrantedStats(Allocation);
		if (!TestEqual(*FString::Printf(
				TEXT("option %d grants exactly one of the three stats"), Option),
				Granted.Num(), 1))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("and it is the one option %d names"),
								   Option),
				  Granted[0], PerOption[Option - 1]);
	}

	// AND A ROW WITH NO OPTION IS UNTOUCHED BY ANY OF THIS, which is every row in
	// the four trees except the capstones'. Without this the check could have been
	// written as "skip a row unless its option matches" and would have silenced
	// the whole sheet.
	Allocation.Add(FName(TEXT("Ravager_mid")), 4);
	const TMap<FName, TArray<FCataclysmStatModifier>> WithOrdinary =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											Demonic);
	const TArray<FCataclysmStatModifier>* Armour =
		WithOrdinary.Find(FName(TEXT("armor")));
	if (TestNotNull(TEXT("an ordinary node still grants its stat"), Armour))
	{
		// TWELVE FROM THE ORDINARY NODE AND THIRTY FROM THE CAPSTONE'S THIRD
		// OPTION, which is not armour, so armour holds one modifier and not two.
		TestEqual(TEXT("and only its own modifier, not the capstone's"),
				  Armour->Num(), 1);
		TestEqual(TEXT("worth three per point times four points"),
				  (*Armour)[0].Value, 12.0f);
	}

	return true;
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

	// AND A CONDITION THAT NAMES AN EFFECT MAKES THE TRIP TOO. Issue #962. The
	// same node carries `while_bleeding`, which is the shape Thirst for Pain
	// uses, and it is the only condition in the vocabulary that compares
	// nothing. Two things could go wrong quietly: the name could be left
	// unrecognised, which applies the bonus at ALL times rather than while
	// bleeding, and the value column could be carried across, which would make
	// the predicate compare against a number it is not supposed to have.
	const TArray<FCataclysmStatModifier>* Evasion =
		Modifiers.Find(FName(TEXT("evasion")));
	if (TestNotNull(TEXT("the node also granted evasion"), Evasion)
		&& TestEqual(TEXT("exactly one of it"), Evasion->Num(), 1))
	{
		TestEqual(TEXT("and it carries the bleeding condition"),
				  static_cast<int32>((*Evasion)[0].Condition),
				  static_cast<int32>(ECataclysmStatCondition::WhileBleeding));
		TestEqual(TEXT("carrying no value, because it compares nothing"),
				  (*Evasion)[0].ConditionValue, 0.0f);
		TestTrue(TEXT("and it is not left unconditional"),
				 (*Evasion)[0].Condition != ECataclysmStatCondition::Always);
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

	// AND THE SECOND SCALE IS A DIFFERENT ENUMERATOR. Issue #994. `Ravager_low`
	// carries a fifth row with `health_owed`, and this is what would notice a
	// build that mapped both names onto one enumerator: every assertion above
	// would still pass, and Compound Interest would silently pay for being hurt
	// rather than for being in debt.
	const TArray<FCataclysmStatModifier>* Leech =
		Modifiers.Find(FName(TEXT("life_leech")));
	if (TestNotNull(TEXT("the owed-health row granted its stat"), Leech))
	{
		TestEqual(TEXT("one per point times eight points"), (*Leech)[0].Value,
				  8.0f);
		TestEqual(TEXT("and it carries the owed-health scale"),
				  static_cast<int32>((*Leech)[0].Scale),
				  static_cast<int32>(
					  ECataclysmStatScale::PerPercentOfMaximumHealthOwed));
		TestEqual(TEXT("in steps of the size the table states"),
				  (*Leech)[0].ScaleStep, 5.0f);
		TestTrue(TEXT("and it is not the missing-health scale"),
				 (*Leech)[0].Scale
					 != ECataclysmStatScale::PerPercentOfMaximumHealthMissing);
	}

	// AND THE THREE STACK NAMES REACH THREE DIFFERENT ENUMERATORS. Issues
	// #1002, #1003 and #1004. Checked together rather than one at a time,
	// because what would go wrong is two names landing on one enumerator, and no
	// single row can see that: a node would count somebody else's stacks, which
	// are granted by a different event and expire on a different clock, and the
	// arithmetic would run perfectly.
	const TPair<const TCHAR*, ECataclysmStatScale> Stacks[] = {
		{TEXT("armor"), ECataclysmStatScale::PerStackOfSanguineMomentum},
		{TEXT("magic_find"), ECataclysmStatScale::PerStackOfBloodlust},
		{TEXT("dot_damage"), ECataclysmStatScale::PerStackOfCarnage},
	};

	for (const TPair<const TCHAR*, ECataclysmStatScale>& Each : Stacks)
	{
		const TArray<FCataclysmStatModifier>* Found =
			Modifiers.Find(FName(Each.Key));
		if (!TestNotNull(FString::Printf(TEXT("the stack row granted '%s'"),
										 Each.Key), Found))
		{
			continue;
		}

		TestEqual(FString::Printf(TEXT("'%s' carries the scale its name asked "
									   "for"), Each.Key),
				  static_cast<int32>((*Found)[0].Scale),
				  static_cast<int32>(Each.Value));
		TestEqual(FString::Printf(TEXT("'%s' scales in single stacks"),
								  Each.Key),
				  (*Found)[0].ScaleStep, 1.0f);
	}

	// AND THE DEBUFF COUNT IS A FOURTH ENUMERATOR, NOT ONE OF THOSE THREE.
	// Issue #962. It is counted by the same arithmetic a stack is, which is
	// exactly why it needs its own check: a build that mapped `debuffs_carried`
	// onto a stack enumerator would produce a number, the node would grow with
	// something, and nothing would say it was growing with the wrong thing.
	const TArray<FCataclysmStatModifier>* SpellDamage =
		Modifiers.Find(FName(TEXT("spell_damage")));
	if (TestNotNull(TEXT("the debuff row granted spell damage"), SpellDamage)
		&& TestEqual(TEXT("exactly one of it"), SpellDamage->Num(), 1))
	{
		TestEqual(TEXT("and it carries the debuff scale"),
				  static_cast<int32>((*SpellDamage)[0].Scale),
				  static_cast<int32>(ECataclysmStatScale::PerDebuffCarried));
		TestEqual(TEXT("counting single debuffs"),
				  (*SpellDamage)[0].ScaleStep, 1.0f);
		TestTrue(TEXT("and it is none of the three stack scales"),
				 (*SpellDamage)[0].Scale
					 != ECataclysmStatScale::PerStackOfSanguineMomentum
				 && (*SpellDamage)[0].Scale
					 != ECataclysmStatScale::PerStackOfBloodlust
				 && (*SpellDamage)[0].Scale
					 != ECataclysmStatScale::PerStackOfCarnage);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveFervourTradeKeystoneTest,
	"Cataclysm.Passives.AKeystoneMultipliesOneFervourSourceAndDividesTheOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Flesh Craver and Blood Tithe, the two keystones that trade one of the two ways
 * Fervour is gained against the other. Issue #978.
 *
 * THE FIRST KEYSTONES IN THE PROJECT THAT GRANT ANYTHING. Every authored passive
 * effect before these two was on a basic node.
 * `UCataclysmPassiveTree::AccumulateInto` never looked at a node's kind, so
 * nothing had to change for them to work -- but nothing had proved it either.
 *
 * AND THE FIRST `more` MULTIPLIERS OUTSIDE THE BULWARK TREE. The two words are
 * what makes a keystone a keystone: `docs/DECISIONS.md` on 2026-08-14 records
 * "more" and "less" as "the multipliers that apply separately instead of joining
 * the additive bucket". An `increased` row would pass every other check in
 * `tools/tests/test_passive_effects_match_the_node_text.py` and be the wrong
 * arithmetic -- the difference only shows on a character that already has other
 * modifiers on the same stat, which is exactly the character nobody tests on.
 *
 * THE END-TO-END PATH AND NOT THE FIXTURE, for the reason
 * `ASpentPointChangesWhatARealCharactersArmourIsWorth` gives: a fixture proves
 * the table is read, and this has to prove the real rows reach a real
 * character's real Fervour rate.
 *
 * READ THROUGH `RateFor` AND NOT OFF THE ATTRIBUTE, because that is what the
 * game does. `UCataclysmFervour::Move` asks `RateFor` on every hit, and
 * `RateFor` runs the stat pipeline again rather than trusting the attribute.
 */
bool FCataclysmPassiveFervourTradeKeystoneTest::RunTest(const FString&)
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

	// THE TREE'S STARTING NODE IS WHAT THERE IS TO MULTIPLY. It grants a flat 1
	// to each of the three rates, and no class line names any of them, so a
	// character without it has a rate of zero and a multiplier on zero is zero.
	// Both keystones are worth nothing on their own and that is correct.
	const FName Start(TEXT("Masochist_basic_spine_000"));
	const FName FleshCraver(TEXT("Masochist_keystone_fc_kC"));
	const FName BloodTithe(TEXT("Masochist_keystone_bt_kC"));

	const FName FromDamage(UCataclysmFervour::FromDamageStat);
	const FName FromCost(UCataclysmFervour::FromCostStat);

	// WHAT THE TWO KEYSTONES ARE AUTHORED AS, checked before anything is spent.
	// A row silently rewritten from `more` to `increased` would still make both
	// rates move, so the assertions below would pass while the arithmetic was
	// wrong on any character carrying another modifier on the same stat.
	for (const FName& Node : {FleshCraver, BloodTithe})
	{
		const TArray<const FCataclysmPassiveEffectRow*> Effects =
			UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
		if (!TestEqual(*FString::Printf(TEXT("%s grants two stats"),
										*Node.ToString()),
					   Effects.Num(), 2))
		{
			return false;
		}
		for (const FCataclysmPassiveEffectRow* Effect : Effects)
		{
			TestEqual(*FString::Printf(TEXT("%s grants %s multiplicatively"),
									   *Node.ToString(), *Effect->Stat),
					  Effect->ValueKind, FString(TEXT("more")));
		}
	}

	// A helper that spends a fresh allocation and reads both rates back.
	const auto RatesAfterSpending =
		[&](const TArray<TPair<FName, int32>>& Nodes) -> TPair<float, float>
	{
		FCataclysmPassiveAllocation Allocation;
		for (const TPair<FName, int32>& Node : Nodes)
		{
			Allocation.Add(Node.Key, Node.Value);
		}
		State->SetPassiveAllocation(Allocation, TArray<FName>());

		// THE ONE REAL ENTRY POINT, the same one the armour test uses. Nothing
		// calls the passive tree directly.
		Equipment->RefreshAttributes(AbilitySystem);

		return TPair<float, float>(
			UCataclysmFervour::RateFor(AbilitySystem, FromDamage,
									   FGameplayTagContainer()),
			UCataclysmFervour::RateFor(AbilitySystem, FromCost,
									   FGameplayTagContainer()));
	};

	// THE STARTING NODE ALONE, WHICH IS THE FIGURE THE KEYSTONES CHANGE. Without
	// this the two assertions below could both hold on a character whose rates
	// were already 1.3 and 0.5 for some other reason.
	const TPair<float, float> Plain = RatesAfterSpending({{Start, 1}});
	TestEqual(TEXT("the starting node alone gives a rate of one from damage"),
			  Plain.Key, 1.0f, 0.001f);
	TestEqual(TEXT("and a rate of one from a health cost"), Plain.Value, 1.0f,
			  0.001f);

	// FLESH CRAVER: "You gain 30% more Fervour from health lost to damage, and
	// 50% less Fervour from health spent as an ability cost."
	const TPair<float, float> Craver =
		RatesAfterSpending({{Start, 1}, {FleshCraver, 1}});
	TestEqual(TEXT("Flesh Craver multiplies the damage rate by 1.30"),
			  Craver.Key, 1.30f, 0.001f);
	TestEqual(TEXT("and halves the cost rate"), Craver.Value, 0.50f, 0.001f);

	// AND IT MULTIPLIES RATHER THAN JOINING THE SUM, WHICH NEEDS A SECOND
	// MODIFIER ON THE SAME STAT BEFORE THE TWO CAN DIFFER AT ALL.
	//
	// THIS IS THE ONLY ASSERTION HERE THAT CAN SEE THE BUCKET. Everything above
	// gives the same figure whether the keystone lands in the `more` bucket or
	// the `increased` one, because one modifier times 1.30 and one modifier plus
	// 30% are the same number. `docs/DECISIONS.md` says exactly this about the
	// failure being invisible on the character nobody tests on, so the test has
	// to build the character it is visible on.
	//
	// Open Wounds is "+2% increased Fervour gained from health lost to damage
	// per point" and holds eight points, so it is +16% in the increases bracket:
	//
	//     as a multiplier   1 x 1.16 x 1.30       = 1.508
	//     as an increase    1 x (1 + 0.16 + 0.30) = 1.46
	const FName OpenWounds(TEXT("Masochist_basic_spine_006"));
	const TPair<float, float> Stacked =
		RatesAfterSpending({{Start, 1}, {OpenWounds, 8}, {FleshCraver, 1}});
	TestEqual(TEXT("the keystone multiplies the increases bracket rather than "
				   "joining it"),
			  Stacked.Key, 1.508f, 0.001f);

	// BLOOD TITHE IS THE MIRROR OF IT, and asserting both is what shows the two
	// rows on a node are not being applied to whichever stat comes first.
	const TPair<float, float> Tithe =
		RatesAfterSpending({{Start, 1}, {BloodTithe, 1}});
	TestEqual(TEXT("Blood Tithe halves the damage rate"), Tithe.Key, 0.50f,
			  0.001f);
	TestEqual(TEXT("and multiplies the cost rate by 1.30"), Tithe.Value, 1.30f,
			  0.001f);

	// AND THE MULTIPLIER REACHES THE FERVOUR ACTUALLY GAINED, not only the rate
	// the pipeline reports. `UCataclysmFervour::GainFromDamage` is what a hit
	// calls; a rate that moved while the gain did not would be a node that reads
	// as working and grants nothing.
	//
	// 100 OF 500 MAXIMUM HEALTH IS 20% OF THE CHARACTER, so at a rate of 1 that
	// is 20 Fervour and at Blood Tithe's halved rate it is 10.
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 500.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
		100.0f);

	TestEqual(TEXT("under Blood Tithe a fifth of the character's health lost "
				   "grants half of the twenty it would otherwise"),
			  UCataclysmFervour::GainFromDamage(AbilitySystem, 100.0f,
												FGameplayTagContainer()),
			  10.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveReciprocityScalesWithFervourTest,
	"Cataclysm.Passives.ReciprocityGrowsWithTheFervourARealCharacterHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Reciprocity, the keystone whose bonus grows with the resource. Issue #980.
 *
 * "Your Retaliation damage is increased by 1% for each point of Fervour you
 * currently hold."
 *
 * THE REAL TABLE AND A REAL CHARACTER, for the reason
 * `ASpentPointChangesWhatARealCharactersArmourIsWorth` gives. A fixture proves
 * the columns are read; this has to prove the authored row reaches a character.
 *
 * READ THROUGH `StatForSkill` AND NOT OFF THE ATTRIBUTE, and that is not a
 * choice here. A modifier that scales with a state is never folded into a
 * gameplay attribute, so the attribute is the figure WITHOUT this node and stays
 * that way however much Fervour is held. Reading it would show nothing moving
 * and prove the opposite of what is wanted.
 */
bool FCataclysmPassiveReciprocityScalesWithFervourTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	// THE CHARACTER HAS TO BE A MASOCHIST, AND THAT IS NOT A TEST DETAIL. The
	// Masochist is the only line of `game/Data/ClassStats.csv` naming
	// `retaliation` at all, so every other class stands on a base of zero and an
	// increase multiplies nothing however well the rest of the chain works.
	//
	// FOUND BY THIS TEST FAILING. Written without it the character sat on the
	// starting class, its retaliation was 0.0, and the assertion that there was
	// something to grow is what said so.
	//
	// BEFORE THE CHARACTER IS SPAWNED, so nothing is ever built on the wrong
	// class line. `RefreshAttributes` would overwrite it later, but a test that
	// depends on an overwrite is a test that breaks when the order changes.
	//
	// THE CONSOLE VARIABLE RATHER THAN CALLING `ApplyTo` WITH THE NAME, so the
	// path stays the one a running game takes.
	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsMasochist.IsUsable()))
	{
		return false;
	}

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

	const FName Node(TEXT("Masochist_keystone_spine_003"));
	const FName Stat(TEXT("retaliation"));

	// WHAT THE ROW IS AUTHORED AS, checked before anything is spent. A scale
	// column silently emptied would leave a flat +1% that never grew, and every
	// figure below would still be a number.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("Reciprocity grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is retaliation"), Effects[0]->Stat,
			  FString(TEXT("retaliation")));
	TestEqual(TEXT("scaled by how much of the pool is held"), Effects[0]->Scale,
			  FString(TEXT("class_resource_held")));
	TestEqual(TEXT("one point at a time"), Effects[0]->ScaleStep, 1.0f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	const auto RetaliationHolding = [&](float Held) -> float
	{
		AbilitySystem->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
			Held);
		return AbilitySystem->StatForSkill(
			Stat, FGameplayTagContainer(),
			AbilitySystem->GetNumericAttribute(
				UCataclysmCombatAttributeSet::GetRetaliationAttribute()));
	};

	// THE FIGURE WITH AN EMPTY BAR IS WHAT THE OTHER TWO ARE MEASURED AGAINST.
	// It is not pinned to a number: the base comes from the Masochist class stat
	// line at whatever level the character was created on, and pinning it would
	// make this fail whenever either is tuned.
	const float Empty = RetaliationHolding(0.0f);
	if (!TestTrue(*FString::Printf(
					  TEXT("the character has retaliation to grow (%.1f)"), Empty),
				  Empty > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("fifty Fervour held is half as much again"),
			  RetaliationHolding(50.0f), Empty * 1.50f, Empty * 0.001f);
	TestEqual(TEXT("and a full hundred doubles it"),
			  RetaliationHolding(100.0f), Empty * 2.0f, Empty * 0.001f);

	// AND THE GAMEPLAY ATTRIBUTE DOES NOT MOVE WITH IT, which is the whole
	// reason the blow sent back has to ask rather than read. This is not a
	// defect being tolerated; it is the rule that keeps a scaled bonus from
	// going stale, asserted so that folding it into the attribute one day would
	// be a deliberate change rather than an accident.
	const float AttributeAtFull = AbilitySystem->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetRetaliationAttribute());
	TestEqual(TEXT("the attribute is unchanged by a full bar"), AttributeAtFull,
			  Empty, Empty * 0.001f);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveThirstForPainOnARealCharacterTest,
	"Cataclysm.Passives.ThirstForPainSpeedsUpARealCharactersSwingWhileItBleeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Thirst for Pain on a real character, bleeding for real. Issue #962.
 *
 * "While you are Bleeding, +2% increased Attack Speed per point."
 *
 * WHAT THIS PROVES THAT THE OTHER TESTS DO NOT, AND WHY IT WAS MISSING. The
 * tests written with this node measured the pieces: that the counter counts, that
 * the pipeline's arithmetic is right, that the two new names reach the right
 * enumerators in a fixture table. None of them ran the whole chain on a real
 * character with the real authored numbers, so every one of them could have
 * passed against a build where spending the point changed nothing a player would
 * feel. This is the shape
 * `ReciprocityGrowsWithTheFervourARealCharacterHolds` already uses and it should
 * have been written at the same time.
 *
 * THE BLEED IS APPLIED AS A GAMEPLAY EFFECT, not by setting a flag. The chain
 * being checked is: a real effect grants a real tag, `UCataclysmDebuffs` reads
 * that tag off the ability system, `CurrentConditions` puts it in the state,
 * `ConditionHolds` judges it, and `UCataclysmBasicAttack` asks for attack speed
 * through `StatForSkill`. Anything short of a real effect skips part of it.
 *
 * THE SWING TIME AND NOT THE STAT, because the swing time is what a player
 * experiences. A faster attack speed is a SHORTER gap between swings, so the
 * assertion runs the opposite way from the percentage and is worth stating in
 * those terms.
 */
bool FCataclysmPassiveThirstForPainOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsMasochist.IsUsable()))
	{
		return false;
	}

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

	const FName Node(TEXT("Masochist_basic_fc_a2"));

	// WHAT THE ROW IS AUTHORED AS, checked before anything is spent. A condition
	// column silently emptied would leave an unconditional +16% attack speed,
	// and the "faster while bleeding" assertion below would still find a bigger
	// number -- it would simply also be bigger when not bleeding, which the
	// second half catches. Checking the row as well says which of the two went
	// wrong.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("Thirst for Pain grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is attack speed"), Effects[0]->Stat,
			  FString(TEXT("attack_speed")));
	TestEqual(TEXT("only while the character is bleeding"),
			  Effects[0]->Condition, FString(TEXT("while_bleeding")));
	TestEqual(TEXT("two percent a point"), Effects[0]->ValuePerPoint, 2.0f);

	// EIGHT POINTS, WHICH IS THE NODE'S OWN MAXIMUM, so the figure below is what
	// a player who committed to it actually gets.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 8);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	const float Unhurt = UCataclysmBasicAttack::SecondsBetweenSwingsFor(
		AbilitySystem);
	if (!TestTrue(*FString::Printf(TEXT("the character swings at all (%.4fs)"),
								   Unhurt), Unhurt > 0.0f))
	{
		return false;
	}

	// NOT BLEEDING YET, SO NOTHING HAS CHANGED. This is the half that catches a
	// condition dropped on the way, which would make the node an unconditional
	// bonus -- silently, and in the player's favour.
	TestEqual(TEXT("an unhurt character carries no debuff"),
			  UCataclysmDebuffs::CountOn(AbilitySystem), 0);
	TestFalse(TEXT("and is not Bleeding"),
			  UCataclysmDebuffs::IsBleeding(AbilitySystem));

	// NOW MAKE IT BLEED, THROUGH A REAL GAMEPLAY EFFECT.
	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	if (!TestTrue(TEXT("the vocabulary has Keyword.DoT.Bleed"), Bleed.IsValid()))
	{
		return false;
	}
	if (!TestTrue(TEXT("a bleed can be put on the character"),
				  UCataclysmSkillEffects::ApplyTagForDuration(
					  Character, Character, Bleed, 30.0f)))
	{
		return false;
	}
	TestTrue(TEXT("the character is Bleeding"),
			 UCataclysmDebuffs::IsBleeding(AbilitySystem));

	const float Bleeding = UCataclysmBasicAttack::SecondsBetweenSwingsFor(
		AbilitySystem);

	// SIXTEEN PERCENT INCREASED ATTACK SPEED IS A SHORTER GAP, NOT A LONGER ONE.
	// The pipeline sums increases, so the rate is multiplied by 1.16 and the gap
	// is divided by it.
	TestEqual(TEXT("eight points make a bleeding character swing 16% faster"),
			  Bleeding, Unhurt / 1.16f, Unhurt * 0.001f);
	TestTrue(TEXT("which is a strictly shorter gap between swings"),
			 Bleeding < Unhurt);

	// AND IT GOES AWAY AGAIN WHEN THE BLEED DOES. Without this the test would
	// pass against a build that turned the bonus on permanently the first time
	// the character was ever hurt.
	const int32 Removed =
		UCataclysmSkillEffects::RemoveEffectsGranting(Character, Bleed);
	TestEqual(TEXT("the bleed was removed"), Removed, 1);
	TestFalse(TEXT("the character is no longer Bleeding"),
			  UCataclysmDebuffs::IsBleeding(AbilitySystem));
	TestEqual(TEXT("and the swing is back where it started"),
			  UCataclysmBasicAttack::SecondsBetweenSwingsFor(AbilitySystem),
			  Unhurt, Unhurt * 0.001f);

	// AND THE GAMEPLAY ATTRIBUTE NEVER MOVED, which is why the swing has to ask
	// rather than read. A conditional bonus is never folded into an attribute --
	// it would be stale the moment the bleed ended -- so a build that folded it
	// in one day should be a deliberate change rather than an accident.
	TestEqual(TEXT("the attack speed attribute is untouched throughout"),
			  UCataclysmBasicAttack::SecondsBetweenSwings(
				  AbilitySystem->GetNumericAttribute(
					  UCataclysmCombatAttributeSet::GetAttackSpeedAttribute())),
			  Unhurt, Unhurt * 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveFlagellantOnARealCharacterTest,
	"Cataclysm.Passives.FlagellantGrantsFervourForEachDebuffARealCharacterCarries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Flagellant on a real character, under real debuffs. Issue #962.
 *
 * "Every debuff on you grants 5 Fervour per second for as long as it lasts."
 *
 * THE COUNTING HALF OF WHAT THE TEST ABOVE PROVES, and a separate test because
 * they are separate machinery: that one is a condition that switches a bonus on
 * and off, this is a scale that decides how large it is. A build could get
 * either right and the other wrong.
 *
 * FERVOUR PER SECOND RATHER THAN A DAMAGE STAT, chosen deliberately from the
 * four nodes that scale with the count. It is the only one of the four whose
 * effect a player can watch happen: the bar fills, at a rate that changes with
 * how many debuffs are on them.
 *
 * MEASURED THROUGH `GainPerSecondStep`, which is the function the game's own
 * clock calls. Reading the stat instead would prove the pipeline and skip the
 * part that turns it into Fervour actually arriving in the pool.
 */
bool FCataclysmPassiveFlagellantOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Resource = UCataclysmClassResourceAttributeSet;

	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsMasochist.IsUsable()))
	{
		return false;
	}

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

	const FName Node(TEXT("Masochist_keystone_fl_kC"));

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("Flagellant grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is Fervour a second"), Effects[0]->Stat,
			  FString(TEXT("fervour_per_second")));
	TestEqual(TEXT("counting the debuffs carried"), Effects[0]->Scale,
			  FString(TEXT("debuffs_carried")));
	TestEqual(TEXT("one debuff at a time"), Effects[0]->ScaleStep, 1.0f);
	TestEqual(TEXT("five a second each"), Effects[0]->ValuePerPoint, 5.0f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	// A BAR WITH ROOM IN IT, emptied before each measurement. A full bar gains
	// nothing however fast the rate is, which would read as a node doing
	// nothing.
	AbilitySystem->SetNumericAttributeBase(
		Resource::GetMaxClassResourceAttribute(), 100.0f);

	const auto GainedInOneSecond = [&]() -> float
	{
		AbilitySystem->SetNumericAttributeBase(
			Resource::GetClassResourceAttribute(), 0.0f);
		return UCataclysmFervour::GainPerSecondStep(AbilitySystem, 1.0f);
	};

	// CARRYING NOTHING GRANTS NOTHING, which is the half that catches a bonus
	// granted unconditionally. A Masochist standing untouched must not be
	// filling its bar off this node.
	TestEqual(TEXT("an untouched character carries no debuff"),
			  UCataclysmDebuffs::CountOn(AbilitySystem), 0);
	TestEqual(TEXT("and gains no Fervour from this node"), GainedInOneSecond(),
			  0.0f, 0.001f);

	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	const FGameplayTag Stunned = UCataclysmSkillEffects::StunnedTag();
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!TestTrue(TEXT("the vocabulary has the three tags"),
				  Bleed.IsValid() && Stunned.IsValid() && Burn.IsValid()))
	{
		return false;
	}

	UCataclysmSkillEffects::ApplyTagForDuration(Character, Character, Bleed, 30.0f);
	TestEqual(TEXT("one debuff"),
			  UCataclysmDebuffs::CountOn(AbilitySystem), 1);
	TestEqual(TEXT("grants five Fervour a second"), GainedInOneSecond(), 5.0f,
			  0.001f);

	// A SECOND KIND, AND THE RATE GOES UP WITH IT. This is what says the bonus
	// really counts rather than switching on once.
	UCataclysmSkillEffects::ApplyTagForDuration(Character, Character, Stunned, 30.0f);
	TestEqual(TEXT("two debuffs"),
			  UCataclysmDebuffs::CountOn(AbilitySystem), 2);
	TestEqual(TEXT("grant ten a second"), GainedInOneSecond(), 10.0f, 0.001f);

	UCataclysmSkillEffects::ApplyTagForDuration(Character, Character, Burn, 30.0f);
	TestEqual(TEXT("three debuffs"),
			  UCataclysmDebuffs::CountOn(AbilitySystem), 3);
	TestEqual(TEXT("grant fifteen a second"), GainedInOneSecond(), 15.0f, 0.001f);

	// AND THE SAME KIND TWICE IS STILL ONE, WHICH IS WHAT "EVERY DEBUFF" MEANS
	// HERE. A second bleed refreshes the first rather than stacking, so the rate
	// must not move. Without this the node would pay a character for standing in
	// two burning patches as though it were under two separate ailments.
	UCataclysmSkillEffects::ApplyTagForDuration(Character, Character, Bleed, 30.0f);
	TestEqual(TEXT("a second bleed is still three debuffs"),
			  UCataclysmDebuffs::CountOn(AbilitySystem), 3);
	TestEqual(TEXT("and the rate has not moved"), GainedInOneSecond(), 15.0f,
			  0.001f);

	// AND IT FALLS AWAY AS THE DEBUFFS DO.
	UCataclysmSkillEffects::RemoveEffectsGranting(Character, Stunned);
	TestEqual(TEXT("two debuffs left"),
			  UCataclysmDebuffs::CountOn(AbilitySystem), 2);
	TestEqual(TEXT("granting ten a second again"), GainedInOneSecond(), 10.0f,
			  0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveSpendCommandTakesACountTest,
	"Cataclysm.Passives.TheSpendCommandPutsInAsManyPointsAsItIsAskedFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Cataclysm.SpendPassivePoint <node> [count]`.
 *
 * WHY THE COUNT EXISTS. A node deep in a tree opens only once its whole chain is
 * filled, and the chains are long: Thirst for Pain sits behind ten nodes and
 * needs 31 points altogether. One point per command made checking that node by
 * hand 31 separate commands, which is enough friction that it went unchecked by
 * play. The project owner said so on 2026-08-26.
 *
 * THE FIRST TEST IN THIS PROJECT THAT DRIVES A CONSOLE COMMAND, and it is worth
 * saying why one is warranted here. The command's own loop is the new part: it
 * stops at the first refusal and keeps what it already spent. That logic exists
 * nowhere else, so nothing else can cover it, and the alternative was to ship a
 * change nothing checked.
 *
 * THE COMMAND IS FOUND BY NAME AND RUN, rather than the lambda being called. A
 * command registered under a name nobody types is a command that does nothing,
 * and the name is half of what this is for.
 */
bool FCataclysmPassiveSpendCommandTakesACountTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsMasochist.IsUsable()))
	{
		return false;
	}

	IConsoleObject* Object = IConsoleManager::Get().FindConsoleObject(
		TEXT("Cataclysm.SpendPassivePoint"));
	IConsoleCommand* Command = Object ? Object->AsCommand() : nullptr;
	if (!TestNotNull(TEXT("the spend command is registered under its name"),
					 Command))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	if (!TestNotNull(TEXT("a possessed player character"), Character))
	{
		return false;
	}

	ACataclysmPlayerState* State =
		Character->GetPlayerState<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("a player state"), State))
	{
		return false;
	}

	// ENOUGH POINTS THAT THE COUNT IS WHAT LIMITS THIS, not the purse. A test
	// that ran out would report the loop stopping for the wrong reason.
	State->SetLevelAndExperience(100, 0);
	const int32 Purse = State->PassivePointsUnspent();
	if (!TestTrue(*FString::Printf(TEXT("the character has points (%d)"), Purse),
				  Purse >= 20))
	{
		return false;
	}

	// THE ROOT OF THE MASOCHIST TREE, WHICH HOLDS EXACTLY ONE POINT. It is the
	// Fervour generator and the only way into the tree, so it has to be bought
	// before anything else can be. The node the count is measured on is the one
	// after it, which holds 12.
	//
	// FOUND BY THIS TEST FAILING. Written against the root as though it held 12,
	// the run said "Expected 12, but it was 1" and the command's own refusal
	// read "Fervour is full at 1 point". The command was right and the test was
	// wrong, which is worth recording: a node's maximum is authored data and not
	// something to assume.
	const FName Root(TEXT("Masochist_basic_spine_000"));
	const FName Deep(TEXT("Masochist_basic_spine_001"));

	const auto Run = [&](const TArray<FString>& Args) -> FString
	{
		FStringOutputDevice Output;
		Command->Execute(Args, World, Output);
		return FString(Output);
	};

	const auto PointsIn = [&](const FName Node) -> int32
	{
		return State->GetPassiveAllocation().PointsIn(Node);
	};

	// NO COUNT IS ONE POINT, which is every use of this command before today and
	// must keep working exactly as it did.
	Run({Root.ToString()});
	TestEqual(TEXT("no count puts in one point"), PointsIn(Root), 1);

	// AND A COUNT PUTS IN THAT MANY. The node after the root holds 12 and one
	// point spent in the tree is what opens it.
	Run({Deep.ToString(), TEXT("5")});
	TestEqual(TEXT("a count of five puts in five"), PointsIn(Deep), 5);

	// ASKING FOR MORE THAN THE NODE HOLDS FILLS IT AND SAYS SO. Seven are left,
	// so asking for fifty must put in seven rather than refusing outright, and
	// must say how many really went in rather than letting the number be
	// inferred.
	const FString TooMany = Run({Deep.ToString(), TEXT("50")});
	TestEqual(TEXT("asking for fifty fills the node to its maximum"),
			  PointsIn(Deep), 12);
	TestTrue(*FString::Printf(TEXT("and says how many went in: %s"), *TooMany),
			 TooMany.Contains(TEXT("Put in 7 of the 50")));

	// A FULL NODE REFUSES, and a count does not change that.
	const FString Full = Run({Deep.ToString(), TEXT("3")});
	TestEqual(TEXT("a full node takes no more"), PointsIn(Deep), 12);
	TestTrue(*FString::Printf(TEXT("and is refused: %s"), *Full),
			 Full.Contains(TEXT("Refused")));

	// A COUNT THAT IS NOT A NUMBER IS REFUSED RATHER THAN READ AS ZERO OR ONE.
	// `FCString::Atoi` answers zero for a word, and zero points is not what
	// anybody typing a word meant. Spending one instead would be worse: it would
	// look as though the command had understood.
	const FName Third(TEXT("Masochist_basic_spine_002"));
	const int32 BeforeWord = PointsIn(Third);
	const FString Word = Run({Third.ToString(), TEXT("banana")});
	TestEqual(TEXT("a word for a count spends nothing"), PointsIn(Third),
			  BeforeWord);
	TestTrue(*FString::Printf(TEXT("and is refused: %s"), *Word),
			 Word.Contains(TEXT("Refused")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveBreakingPointOnARealCharacterTest,
	"Cataclysm.Passives.TheBreakingPointConvertsForARealCharactersFullWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Breaking Point on a real character, hurt for real. Issue #1025.
 *
 * "Dropping below 50% health converts all damage you take into Bleeding over 5
 * seconds. The conversion lasts 3 seconds, increased by 5% per point, and cannot
 * happen more than once every 10 seconds."
 *
 * WHAT IT CAUGHT, WHICH IS WHY IT IS WORTH THE SPAWN. The window resolved to
 * ZERO on every real character, so the node converted nothing at all, and
 * `NoteDamageConversionStarted` refuses a window of zero, so not one turn of the
 * conversion ever began. The base of 3 seconds was named by
 * `ENGINE_SUPPLIED_BASES` in `tools/generate_datatables.py`, which exempted the
 * stat from the check refusing an increase with no base under it, and nothing
 * anywhere put that base on a character.
 *
 * WHY NOTHING ELSE NOTICED. `CataclysmDamageConversionTests.cpp` writes the
 * window onto the attribute by hand in its `TakeTheNode` helper, so every test
 * there proves the conversion works GIVEN a window and none of them asks where a
 * window comes from. The same shape of gap #1024 was written for: every part
 * works and they are not joined up.
 *
 * THE BASE IS CHECKED BEFORE ANY POINT IS SPENT, and that half is the one that
 * fails against the old build. `ApplyTo` writes the attribute for every mapped
 * stat whether or not a modifier touches it, so an unspent character reading 3
 * seconds is what says the base reached a character at all.
 *
 * AND THE WHOLE CHAIN AFTER IT: eight points make it 4.2 seconds, dropping below
 * half health opens a window of exactly that length, and the character really is
 * converting. Reading the stat alone would prove the arithmetic and skip the part
 * that turns it into a window a player experiences.
 */
bool FCataclysmPassiveBreakingPointOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;
	using Conversion = UCataclysmDamageConversion;

	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsMasochist.IsUsable()))
	{
		return false;
	}

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

	const FName Node(TEXT("Masochist_basic_ll_b1"));

	// WHAT THE ROWS ARE AUTHORED AS, checked before anything is spent. The node
	// grants two stats and they answer different questions: a flag saying the
	// rule applies at all, and a duration saying how long one turn of it lasts.
	// A build that lost the second row would still switch the rule on, and the
	// window assertions below would then be measuring a base nobody increased.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("The Breaking Point grants two stats"),
				   Effects.Num(), 2))
	{
		return false;
	}

	const FCataclysmPassiveEffectRow* WindowRow = nullptr;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		if (Row->Stat == FString(Conversion::WindowStat))
		{
			WindowRow = Row;
		}
	}
	if (!TestNotNull(TEXT("one of them is the conversion window"), WindowRow))
	{
		return false;
	}
	TestEqual(TEXT("and it is an increase"), WindowRow->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("of five percent a point"), WindowRow->ValuePerPoint, 5.0f);

	const FGameplayAttribute WindowAttribute =
		Resource::GetDamageToBleedingWindowAttribute();

	// THE BASE, ON A CHARACTER THAT HAS SPENT NOTHING. This is the assertion the
	// old build failed: the stat had no base anywhere, so it resolved to zero and
	// the increase below multiplied nothing.
	Equipment->RefreshAttributes(AbilitySystem);
	TestEqual(TEXT("an unspent Masochist already has a three second window"),
			  AbilitySystem->GetNumericAttribute(WindowAttribute), 3.0f, 0.001f);

	// AND THE RULE IS STILL OFF, which is what makes that base harmless. The
	// window says how long a turn lasts; the flag beside it says whether a turn
	// may begin at all.
	TestEqual(TEXT("and the rule itself is off until a point is spent"),
			  AbilitySystem->GetNumericAttribute(
				  Resource::GetDamageToBleedingOnLowHealthAttribute()),
			  0.0f, 0.001f);

	// EIGHT POINTS, WHICH IS THE NODE'S OWN MAXIMUM, so the figure below is what
	// a player who committed to it actually gets. `docs/Cataclysm_GDD_v2.md` and
	// the header both quote 4.2 seconds for exactly this case.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 8);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	TestEqual(TEXT("eight points make the window 4.2 seconds"),
			  AbilitySystem->GetNumericAttribute(WindowAttribute), 4.2f, 0.001f);
	TestTrue(TEXT("and the rule is now on"),
			 AbilitySystem->GetNumericAttribute(
				 Resource::GetDamageToBleedingOnLowHealthAttribute()) > 0.0f);

	// NOW HURT IT, THROUGH THE FUNCTION THE GAME'S OWN HEALTH CHANGE CALLS.
	// Nothing is converting yet, because the character is at full health and has
	// not dropped anywhere.
	TestFalse(TEXT("a character at full health is not converting"),
			  AbilitySystem->IsConvertingDamageToBleeding());

	const float Maximum = AbilitySystem->GetNumericAttribute(
		Vital::GetMaxHealthAttribute());
	if (!TestTrue(TEXT("the character has some maximum health"), Maximum > 0.0f))
	{
		return false;
	}

	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   Maximum * 0.4f);
	Conversion::NoteHealthChanged(Character);

	TestTrue(TEXT("dropping below half health starts the conversion"),
			 AbilitySystem->IsConvertingDamageToBleeding());

	// AND FOR THE LENGTH THE NODE PAID FOR, not for the base and not for nothing.
	// Without this the test would pass against a build that opened a window of
	// any length at all, including the three seconds an unspent character has.
	//
	// NARROWED TO A FLOAT ON PURPOSE. `GetTimeSeconds` answers a double and the
	// window is a float, so the subtraction is a double and `TestEqual` cannot
	// choose between its float and double overloads.
	const float Remaining = static_cast<float>(
		AbilitySystem->DamageConversionEndsAt() - World->GetTimeSeconds());
	TestEqual(TEXT("and it runs for the full 4.2 seconds"),
			  Remaining, 4.2f, 0.01f);

	return true;
}

// ---------------------------------------------------------------------------
// The three nodes that change how much damage the character takes. Issue #1026.
//
// ONE TEST EACH, AND EACH MEASURES A HIT RATHER THAN A STAT. The stat is what
// the pipeline produces; the damage a real hit deals is what a player feels, and
// only the second says the two are joined up. `UCataclysmDamageCalculation::Resolve`
// is the function the game itself calls when a blow lands.
//
// EACH SPENDS REAL POINTS THROUGH THE REAL ALLOCATION. Writing the attribute by
// hand would prove the arithmetic and skip everything between the workbook row
// and the character, which is the gap issues #1024 and #1025 were both about.
// ---------------------------------------------------------------------------

namespace CataclysmPassiveTest
{
	/** What a hit of this size actually takes off a real character's health. */
	static float DamageAHitDeals(
		const UCataclysmAbilitySystemComponent* AbilitySystem,
		float Raw, bool bIsDamageOverTime)
	{
		FCataclysmIncomingHit Hit;
		Hit.Damage = Raw;
		Hit.bIsDamageOverTime = bIsDamageOverTime;

		// BOTH ROLLS PINNED TO "DID NOT HAPPEN", so evasion and block cannot make
		// one call differ from the next. A player carries neither by default, but
		// pinning them says so rather than relying on it.
		return UCataclysmDamageCalculation::Resolve(
				   Hit, AbilitySystem, /*Tier=*/1,
				   /*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f).DealtToHealth;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveEchoesOfAgonyOnARealCharacterTest,
	"Cataclysm.Passives.EchoesOfAgonySoftensADamageOverTimeTickForARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Echoes of Agony on a real character, bleeding for real. Issue #1026.
 *
 * "Damage taken from damage over time effects is reduced by 1% per point."
 *
 * THE SECOND HALF IS THE ONE THAT CAN FAIL QUIETLY. A build reading the stat for
 * every hit rather than only for a damage over time one would soften the
 * character's whole life and nothing would report it -- the node would simply be
 * worth several times what a player reads. So a direct hit is measured as well,
 * and it has to be untouched.
 */
bool FCataclysmPassiveEchoesOfAgonyOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsMasochist.IsUsable()))
	{
		return false;
	}

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

	const FName Node(TEXT("Masochist_basic_spine_005"));

	// WHAT THE ROW IS AUTHORED AS, checked before anything is spent. A value that
	// lost its minus sign would make the node worth the opposite of what a player
	// reads, and the assertion below would find a difference either way.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("Echoes of Agony grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is the damage over time one"), Effects[0]->Stat,
			  FString(UCataclysmDamageCalculation::DamageOverTimeTakenStat));
	TestEqual(TEXT("as an increase"), Effects[0]->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("of minus one a point"), Effects[0]->ValuePerPoint, -1.0f);

	// WHAT A HIT COSTS BEFORE ANY POINT IS SPENT. Measured rather than assumed,
	// because a Masochist carries class armour and the figure below is what is
	// left after every other layer.
	Equipment->RefreshAttributes(AbilitySystem);
	const float TickBefore = DamageAHitDeals(AbilitySystem, 400.0f, true);
	const float HitBefore = DamageAHitDeals(AbilitySystem, 400.0f, false);
	if (!TestTrue(TEXT("an unspent character takes damage at all"),
				  TickBefore > 0.0f && HitBefore > 0.0f))
	{
		return false;
	}

	// AND THE BLOW IS SMALLER THAN THE HEALTH IT LANDS ON, so what is measured is
	// the damage rather than the health left. `Resolve` finishes with
	// `DealtToHealth = Min(Damage, Health)`, and a blow larger than the character
	// reports the character. Asserted rather than assumed: the class line's
	// health is data and could move.
	const float Health = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	if (!TestTrue(*FString::Printf(
			TEXT("the hit (%.1f) is smaller than the health it lands on (%.1f)"),
			HitBefore, Health),
			HitBefore < Health))
	{
		return false;
	}

	// TEN POINTS, WHICH IS THE NODE'S OWN MAXIMUM, so the figure is what a player
	// who committed to it gets: a tenth less from anything spread over time.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 10);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	TestEqual(TEXT("ten points take a tenth off a damage over time tick"),
			  DamageAHitDeals(AbilitySystem, 400.0f, true),
			  TickBefore * 0.90f, TickBefore * 0.001f);

	// AND A DIRECT HIT IS UNTOUCHED. Without this the test would pass against a
	// build that softened every blow the character ever took.
	TestEqual(TEXT("and a direct hit is exactly what it was"),
			  DamageAHitDeals(AbilitySystem, 400.0f, false),
			  HitBefore, HitBefore * 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveCommunionOfPainOnARealCharacterTest,
	"Cataclysm.Passives.CommunionOfPainCutsBothWaysForARealCharacterAtFullFervour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Communion of Pain on a real character, at a real full bar. Issue #1026.
 *
 * "While your Fervour is at maximum you deal 20% more damage and take 20% more
 * damage."
 *
 * BOTH CLAUSES, BECAUSE THE NODE IS A TRADE. A build that granted the damage and
 * dropped the cost would be strictly better than the sentence, and one that did
 * the reverse strictly worse. Each is measured.
 *
 * AND WITH THE BAR SHORT OF FULL, which is the half that catches a condition
 * dropped on the way. Without it the node would be an unconditional bonus and an
 * unconditional penalty, and nothing at run time would say so.
 */
bool FCataclysmPassiveCommunionOfPainOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Resource = UCataclysmClassResourceAttributeSet;

	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsMasochist.IsUsable()))
	{
		return false;
	}

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

	const FName Node(TEXT("Masochist_keystone_spine_001"));

	// THREE ROWS, AND ALL THREE CARRY THE CONDITION. "Increased damage" is two
	// stats in this project because a character deals attack damage and spell
	// damage, and the third is what the second clause costs.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("Communion of Pain grants three stats"),
				   Effects.Num(), 3))
	{
		return false;
	}
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		TestEqual(*FString::Printf(TEXT("%s multiplies"), *Row->Stat),
				  Row->ValueKind, FString(TEXT("more")));
		TestEqual(*FString::Printf(TEXT("%s is worth twenty"), *Row->Stat),
				  Row->ValuePerPoint, 20.0f);
		TestEqual(*FString::Printf(TEXT("%s applies only at full Fervour"),
								   *Row->Stat),
				  Row->Condition, FString(TEXT("class_resource_at_maximum")));
	}

	// A KEYSTONE HOLDS ONE POINT. Reading it off the node table rather than
	// writing 1 here, because a node's maximum is authored data.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	const FGameplayTagContainer NoSkill;
	const auto AttackDamage = [&]
	{
		return AbilitySystem->StatForSkill(
			FName(TEXT("attack_damage")),
			NoSkill,
			AbilitySystem->GetNumericAttribute(
				UCataclysmCombatAttributeSet::GetAttackDamageAttribute()));
	};

	// THE BAR IS EMPTY TO BEGIN WITH. A Masochist generates Fervour from damage
	// taken and from health spent, and this character has done neither.
	const float Maximum =
		AbilitySystem->GetNumericAttribute(Resource::GetMaxClassResourceAttribute());
	if (!TestTrue(TEXT("the class resource has a maximum above nothing"),
				  Maximum > 0.0f))
	{
		return false;
	}
	AbilitySystem->SetNumericAttributeBase(Resource::GetClassResourceAttribute(),
										   0.0f);

	const float DamageEmpty = AttackDamage();
	const float TakenEmpty = DamageAHitDeals(AbilitySystem, 400.0f, false);
	if (!TestTrue(TEXT("the character swings for something and takes something"),
				  DamageEmpty > 0.0f && TakenEmpty > 0.0f))
	{
		return false;
	}

	// AND THE AMPLIFIED BLOW IS STILL SMALLER THAN THE HEALTH IT LANDS ON.
	// `Resolve` finishes with `DealtToHealth = Min(Damage, Health)`, so a blow
	// larger than the character reports the character and the fifth this node
	// adds would be invisible. The figure checked is the one AFTER the increase,
	// because that is the larger of the two.
	const float Health = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	if (!TestTrue(*FString::Printf(
			TEXT("a fifth more than the hit (%.1f) is still under the health it "
				 "lands on (%.1f)"), TakenEmpty * 1.20f, Health),
			TakenEmpty * 1.20f < Health))
	{
		return false;
	}

	// NOW FILL THE BAR.
	AbilitySystem->SetNumericAttributeBase(Resource::GetClassResourceAttribute(),
										   Maximum);

	TestEqual(TEXT("at full Fervour the character deals a fifth more"),
			  AttackDamage(), DamageEmpty * 1.20f, DamageEmpty * 0.001f);
	TestEqual(TEXT("and takes a fifth more"),
			  DamageAHitDeals(AbilitySystem, 400.0f, false),
			  TakenEmpty * 1.20f, TakenEmpty * 0.001f);

	// AND ONE POINT SHORT OF FULL IS NOTHING AT ALL. This is the half that
	// catches a condition dropped on the way, and it also says the comparison is
	// "at maximum" rather than "near it".
	AbilitySystem->SetNumericAttributeBase(Resource::GetClassResourceAttribute(),
										   Maximum - 1.0f);
	TestEqual(TEXT("one point short of full, the damage is back where it was"),
			  AttackDamage(), DamageEmpty, DamageEmpty * 0.001f);
	TestEqual(TEXT("and so is the damage taken"),
			  DamageAHitDeals(AbilitySystem, 400.0f, false),
			  TakenEmpty, TakenEmpty * 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveTheEdgeOnARealCharacterTest,
	"Cataclysm.Passives.TheEdgeSoftensHitsAndHoldsFervourForARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Edge on a real character, at real low health. Issue #1026.
 *
 * "While at or below 20% health you take 25% less damage and your Fervour does
 * not decrease."
 *
 * TWO CLAUSES AND TWO ROWS, and they are separate machinery: one is a multiplier
 * in the damage calculation, the other a flag `UCataclysmFervour::LossIsSuppressed`
 * reads. A build could get either right and the other wrong.
 *
 * THE FERVOUR CLAUSE IS ASKED WITH NO TAGS, and that is what makes it different
 * from the two keystones already using that flag. Sanguine Ledger requires
 * `Keyword.Regeneration` and Wounds That Feed requires `Keyword.Leech`, so each
 * covers one kind of healing; this row requires none, so it covers every kind.
 * The design says "does not decrease" without qualification, and out-of-combat
 * decay -- the other way Fervour could fall -- does not exist in this game yet.
 */
bool FCataclysmPassiveTheEdgeOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Vital = UCataclysmVitalAttributeSet;

	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsMasochist.IsUsable()))
	{
		return false;
	}

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

	const FName Node(TEXT("Masochist_keystone_ll_kA"));

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("The Edge grants two stats"), Effects.Num(), 2))
	{
		return false;
	}
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		TestEqual(*FString::Printf(TEXT("%s applies below a fifth of health"),
								   *Row->Stat),
				  Row->Condition, FString(TEXT("health_at_or_below")));
		TestEqual(*FString::Printf(TEXT("%s at twenty percent"), *Row->Stat),
				  Row->ConditionValue, 20.0f);
	}

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	const float Maximum =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	if (!TestTrue(TEXT("the character has some maximum health"), Maximum > 0.0f))
	{
		return false;
	}

	// A SMALL HIT, AND THE SIZE IS THE WHOLE POINT. `Resolve` finishes with
	// `DealtToHealth = Min(Damage, Health)`, so a blow larger than what the
	// character has left reports the health rather than the damage. This test
	// measures a character at a tenth of its health, where a Masochist has about
	// 61 points, and a 400 hit resolved to 303 -- so the first version of it
	// compared 60.6 against 227 and failed for a reason that had nothing to do
	// with the node. The guards below are what stop that returning silently.
	constexpr float RawHit = 40.0f;

	// HEALTHY FIRST, so the figure below is compared against this same character
	// rather than against a number written here. Half health is well clear of
	// the threshold.
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   Maximum * 0.5f);
	const float Healthy = DamageAHitDeals(AbilitySystem, RawHit, false);
	if (!TestTrue(TEXT("a healthy character takes damage"), Healthy > 0.0f))
	{
		return false;
	}

	// AND THE HIT IS SMALL ENOUGH THAT THE CAP CANNOT BITE AT A TENTH OF HEALTH,
	// which is the lowest this test goes. Asserted rather than assumed, because
	// the class line's health is data and could move.
	if (!TestTrue(*FString::Printf(
			TEXT("the hit (%.1f) stays under a tenth of maximum health (%.1f), "
				 "so DealtToHealth is the damage and not the health left"),
			Healthy, Maximum * 0.1f),
			Healthy < Maximum * 0.1f))
	{
		return false;
	}

	// AND ITS FERVOUR STILL FALLS TO HEALING. Without this the test would pass
	// against a build that suppressed the loss for ever from the moment the node
	// was taken.
	const FGameplayTagContainer AnyHealing;
	TestFalse(TEXT("a healthy character's Fervour still falls to healing"),
			  UCataclysmFervour::LossIsSuppressed(AbilitySystem, AnyHealing));

	// NOW DOWN TO A TENTH, WHICH IS BELOW THE THRESHOLD.
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   Maximum * 0.1f);

	TestEqual(TEXT("below a fifth of health the hit is a quarter smaller"),
			  DamageAHitDeals(AbilitySystem, RawHit, false),
			  Healthy * 0.75f, Healthy * 0.001f);

	TestTrue(TEXT("and Fervour no longer falls to healing"),
			 UCataclysmFervour::LossIsSuppressed(AbilitySystem, AnyHealing));

	// EXACTLY ON THE THRESHOLD IS INSIDE IT. Every node in this tree is written
	// "at or below", and a character sitting precisely on a fifth gets the bonus.
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   Maximum * 0.2f);
	TestEqual(TEXT("and exactly a fifth is inside the threshold"),
			  DamageAHitDeals(AbilitySystem, RawHit, false),
			  Healthy * 0.75f, Healthy * 0.001f);

	// AND ONE POINT ABOVE THE THRESHOLD IS OUTSIDE IT. The pair says the
	// comparison is at-or-below rather than strictly-below, which is the
	// distinction every health threshold in this tree turns on.
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   Maximum * 0.2f + 1.0f);
	TestEqual(TEXT("and a point above it the hit is back to full size"),
			  DamageAHitDeals(AbilitySystem, RawHit, false),
			  Healthy, Healthy * 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveApotheosisOnARealCharacterTest,
	"Cataclysm.Passives.ApotheosisUncapsARealCharactersFervourAndChargesForIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Final Vow's second option on a real character. Issue #1029.
 *
 * "Your Fervour has no maximum, but you take 1% more damage for every 100
 * Fervour you hold."
 *
 * THE FIRST CAPSTONE OPTION EVER AUTHORED, so this is also what proves the
 * `Option` column reaches a real character rather than only a fixture table.
 *
 * BOTH CLAUSES, BECAUSE THE NODE IS A TRADE. A build that lifted the cap and
 * dropped the cost would be strictly better than the sentence. Each is measured,
 * and the cost is measured at two different amounts of Fervour so that a build
 * granting a fixed penalty rather than a scaling one fails.
 *
 * AND THE CHOICE IS MADE THROUGH `ChoosePassiveOption`, the same call the console
 * command and the tree screen use, rather than by writing the option onto the
 * allocation. Anything short of that skips the part that refuses a second choice.
 */
bool FCataclysmPassiveApotheosisOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Resource = UCataclysmClassResourceAttributeSet;

	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsMasochist.IsUsable()))
	{
		return false;
	}

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

	const FName Node(TEXT("Masochist_capstone_200"));

	// WHAT THE ROWS ARE AUTHORED AS, checked before anything is spent. Both must
	// name option 2: a row that lost its option would apply whichever of the three
	// the player picked, which is the failure the column exists to prevent.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("The Final Vow's authored option grants two stats"),
				   Effects.Num(), 2))
	{
		return false;
	}
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		TestEqual(*FString::Printf(TEXT("%s belongs to option 2"), *Row->Stat),
				  Row->Option, 2);
	}

	// THE POOL HAS A MAXIMUM TO BEGIN WITH, and the character cannot pass it.
	const float Maximum =
		AbilitySystem->GetNumericAttribute(Resource::GetMaxClassResourceAttribute());
	if (!TestTrue(TEXT("the class resource has a maximum above nothing"),
				  Maximum > 0.0f))
	{
		return false;
	}

	const FGameplayAttribute Pool = Resource::GetClassResourceAttribute();
	AbilitySystem->SetNumericAttributeBase(Pool, Maximum * 3.0f);
	TestEqual(TEXT("without the node the pool is held at its maximum"),
			  AbilitySystem->GetNumericAttribute(Pool), Maximum, 0.01f);

	// TWO HUNDRED POINTS IN THE TREE FIRST, BECAUSE THAT IS WHEN THIS CAPSTONE
	// OPENS. `ChoosePassiveOption` checks the threshold as well as the points in
	// the node, so that a player cannot decide all four capstones at 25 points
	// and spend into them later with no decision left to make. The first version
	// of this test spent one point and was refused with "The Final Vow opens at
	// 200 points spent in the Masochist tree. 1 so far."
	//
	// FILLED FROM THE NODE TABLE RATHER THAN FROM A LIST WRITTEN HERE, so this
	// keeps working when the tree is re-authored. Every Masochist node except
	// this one is filled to its own maximum until the threshold is met, which
	// also honours the rule that a node's maximum is authored data.
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		return false;
	}

	const FCataclysmPassiveNodeRow* Capstone = nullptr;
	int32 Threshold = 0;
	for (const TPair<FName, uint8*>& Pair : NodeTable->GetRowMap())
	{
		if (Pair.Key == Node)
		{
			Capstone =
				reinterpret_cast<const FCataclysmPassiveNodeRow*>(Pair.Value);
			Threshold = Capstone->Threshold;
		}
	}
	if (!TestNotNull(TEXT("the capstone is in the node table"), Capstone)
		|| !TestTrue(TEXT("and it opens above nothing"), Threshold > 0))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	int32 Filled = 0;
	for (const TPair<FName, uint8*>& Pair : NodeTable->GetRowMap())
	{
		if (Filled >= Threshold)
		{
			break;
		}
		const auto* Row =
			reinterpret_cast<const FCataclysmPassiveNodeRow*>(Pair.Value);
		if (Row->Tree != TEXT("Masochist") || Pair.Key == Node
			|| Row->MaxPoints <= 0)
		{
			continue;
		}
		const int32 Take = FMath::Min(Row->MaxPoints, Threshold - Filled);
		Allocation.Add(Pair.Key, Take);
		Filled += Take;
	}
	if (!TestEqual(*FString::Printf(
			TEXT("the tree can hold the %d points the capstone opens at"),
			Threshold), Filled, Threshold))
	{
		return false;
	}

	// AND THE POINT IN THE CAPSTONE ITSELF, on top of the threshold.
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());

	FString Refusal;
	if (!TestTrue(TEXT("the second option can be chosen"),
				  State->ChoosePassiveOption(Node, 2, Refusal)))
	{
		AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
		return false;
	}
	Equipment->RefreshAttributes(AbilitySystem);

	// THE MAXIMUM IS RE-READ HERE RATHER THAN REUSED FROM ABOVE, and the first
	// version of this test did reuse it and failed: 116 against an expected 100.
	// Four Masochist nodes grant "+2% increased maximum Fervour per point", and
	// filling the tree to its threshold spends points in them, so the maximum
	// really did move -- by the ordinary working of other nodes, not by anything
	// this one does. Comparing against a figure read before those points were
	// spent measures the wrong thing.
	const float Capped = AbilitySystem->GetNumericAttribute(
		Resource::GetMaxClassResourceAttribute());
	TestTrue(TEXT("the maximum is still a real number after the tree is filled"),
			 Capped > 0.0f);

	// THE CAP IS GONE. Written well past the maximum and it stays there.
	AbilitySystem->SetNumericAttributeBase(Pool, Capped * 3.0f);
	TestEqual(TEXT("with the node the pool passes its maximum"),
			  AbilitySystem->GetNumericAttribute(Pool), Capped * 3.0f, 0.01f);

	// AND THE MAXIMUM ATTRIBUTE ITSELF IS NOT WHAT MOVED, which is what makes
	// this a lifted cap rather than a raised one. Everything that draws the bar
	// still reads the same number after the pool has passed it.
	TestEqual(TEXT("and the maximum attribute is untouched by the pool passing it"),
			  AbilitySystem->GetNumericAttribute(
				  Resource::GetMaxClassResourceAttribute()),
			  Capped, 0.01f);

	// AND THE FLOOR STILL HOLDS, because no sentence says a pool may go negative.
	AbilitySystem->SetNumericAttributeBase(Pool, -50.0f);
	TestEqual(TEXT("but it still cannot go below nothing"),
			  AbilitySystem->GetNumericAttribute(Pool), 0.0f, 0.01f);

	// NOW THE COST. A small blow, so what is measured is the damage rather than
	// the health left -- `Resolve` ends with `Min(Damage, Health)`.
	constexpr float RawHit = 40.0f;

	AbilitySystem->SetNumericAttributeBase(Pool, 0.0f);
	const float Empty = DamageAHitDeals(AbilitySystem, RawHit, false);
	if (!TestTrue(TEXT("an empty-barred character takes damage"), Empty > 0.0f))
	{
		return false;
	}

	// A HUNDRED FERVOUR IS ONE STEP, so a hundredth more damage.
	AbilitySystem->SetNumericAttributeBase(Pool, 100.0f);
	TestEqual(TEXT("at 100 Fervour the character takes one percent more"),
			  DamageAHitDeals(AbilitySystem, RawHit, false),
			  Empty * 1.01f, Empty * 0.001f);

	// AND FIVE HUNDRED IS FIVE STEPS, which is the half that catches a fixed
	// penalty granted whatever the pool holds.
	AbilitySystem->SetNumericAttributeBase(Pool, 500.0f);
	TestEqual(TEXT("and at 500 Fervour it takes five percent more"),
			  DamageAHitDeals(AbilitySystem, RawHit, false),
			  Empty * 1.05f, Empty * 0.001f);

	// AND A PART STEP COUNTS FOR NOTHING, because steps are whole and rounded
	// down. 199 Fervour is one step, not one and ninety-nine hundredths.
	AbilitySystem->SetNumericAttributeBase(Pool, 199.0f);
	TestEqual(TEXT("and 199 Fervour is one whole step, not two"),
			  DamageAHitDeals(AbilitySystem, RawHit, false),
			  Empty * 1.01f, Empty * 0.001f);

	// AND "AT MAXIMUM FERVOUR" IS NEVER TRUE FOR THIS CHARACTER, which is the
	// interaction that would otherwise be found by a player. Issue #1029.
	// Communion of Pain applies "While your Fervour is at maximum";
	// `MaxClassResource` still holds its old number because the bar is drawn
	// against it, so a build comparing the pool against that number would have
	// this condition hold PERMANENTLY once the pool passed it -- the opposite of
	// what the sentence says.
	//
	// ASSERTED THROUGH THE PIPELINE'S OWN JUDGEMENT rather than by measuring a
	// node, because the two are separate capstones and a character cannot hold
	// this one and Communion of Pain's clause in a way this test could isolate.
	//
	// AND THE POOL IS CHECKED TO BE ABOVE THE MAXIMUM FIRST, so the assertion
	// below cannot pass merely because the bar happens to be low.
	TestTrue(TEXT("the pool really is above its stated maximum"),
			 AbilitySystem->GetNumericAttribute(Pool) > Capped);
	TestFalse(TEXT("and 'at maximum Fervour' is still false, because there is "
				   "no maximum to be at"),
			  UCataclysmStatPipeline::ConditionHolds(
				  ECataclysmStatCondition::ClassResourceAtMaximum, 0.0f,
				  AbilitySystem->CurrentConditions()));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
