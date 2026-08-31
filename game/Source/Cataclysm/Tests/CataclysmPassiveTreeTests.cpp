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
// For the bucket and the condition a modifier carries, which many of the tests
// below read off an authored row.
#include "AbilitySystem/CataclysmStatPipeline.h"
// For putting health back the way the game itself does, rather than reading the
// rate off a gameplay attribute a scaled bonus never reaches. Issue #1038.
#include "AbilitySystem/CataclysmRegeneration.h"
// For the conversion window The Breaking Point lengthens. Issue #1025.
#include "AbilitySystem/CataclysmDamageConversion.h"
// For the debuffs the five new nodes read. Issue #962.
#include "AbilitySystem/CataclysmDebuffs.h"
// For the three Fervour rate stat names and the function that reads one back
// through the stat pipeline. Issue #978.
#include "AbilitySystem/CataclysmFervour.h"
// For putting a real bleed on a real character. Issue #962.
#include "AbilitySystem/CataclysmSkillEffects.h"
// For asking the game's own reader how far a character's retaliation reaches
// and whether it leeches. Issues #1047 and #1048.
#include "AbilitySystem/CataclysmRetaliation.h"
// For the nova a character at very low health releases. Issue #1050.
#include "AbilitySystem/CataclysmNova.h"
// For the flag saying a character's skills cost no health. Issue #1051.
#include "AbilitySystem/CataclysmSkillTemplate.h"
// For how long a lasting harmful effect on the character runs. Issue #1033.
#include "AbilitySystem/CataclysmDebuffs.h"
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
			// AND A THRESHOLD THAT POINTS UPWARDS. Issue #1070. Ceaseless
			// Penance is the only node in the game asking whether health is
			// still HIGH, and the failure if the name goes unrecognised is the
			// worst of the three: the row is left UNCONDITIONAL, so the option
			// would hold a character's debuffs still at every health rather
			// than only above half.
			"Ravager_low#11,Ravager_low,block_chance,flat,1.0,,health_above,50,,0,0\r\n"
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

	// AND A THRESHOLD THAT POINTS UPWARDS MAKES THE TRIP. Issue #1070. The same
	// node carries `health_above` with 50, which is the shape Ceaseless Penance
	// uses and the only node in the game that asks whether health is still
	// high. An unrecognised name is left unconditional, which for that option
	// would hold a character's debuffs still at every health rather than above
	// half -- silently, and in the player's favour.
	const TArray<FCataclysmStatModifier>* Block =
		Modifiers.Find(FName(TEXT("block_chance")));
	if (TestNotNull(TEXT("the node also granted block chance"), Block)
		&& TestEqual(TEXT("exactly one of it"), Block->Num(), 1))
	{
		TestEqual(TEXT("and it carries the upward health condition"),
				  static_cast<int32>((*Block)[0].Condition),
				  static_cast<int32>(
					  ECataclysmStatCondition::HealthAbovePercent));
		TestEqual(TEXT("at the threshold the table states"),
				  (*Block)[0].ConditionValue, 50.0f);
		TestTrue(TEXT("and it is not left unconditional"),
				 (*Block)[0].Condition != ECataclysmStatCondition::Always);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveGivesTheMasochistLifeLeechTest,
	"Cataclysm.Passives.ASpentPointGivesAMasochistLifeLeechItHasNoBaseFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPassiveGivesTheMasochistLifeLeechTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Vital = UCataclysmVitalAttributeSet;

	// WHAT WENT WRONG. Issue #1105. `game/Data/ClassStats.csv` gives a base life
	// leech to one class, the Ravager. The Masochist has none, and neither does
	// the shared `Default` line it falls back to, so its base is zero. Both of
	// the tree's life leech nodes were `increased` rows, and the three-bucket
	// pipeline is `(base + flat) x (1 + increases) x more`, so 36% increased of
	// nothing is nothing. The project owner spent 14 points on them and reported
	// from play: "I should be getting life leech or something I thought, but it
	// seems like I have no healing."
	//
	// BRUTAL DETERMINATION IS NOW THE SOURCE, a flat 0.4% a point, and Undying
	// Hunger increases what it supplies. This test is the end-to-end statement:
	// a point spent on a real character, through the real pipeline, reaching an
	// attribute that was zero and could not have been anything else.
	//
	// THE OTHER DIRECTION IS CHECKED IN PYTHON, by
	// `test_no_node_is_worth_nothing_to_its_own_class` in
	// `tools/tests/test_passive_effects_match_the_node_text.py`, which fails if
	// any node's every row increases a stat its own class has no base for.
	//
	// AND IT IS THE SAME FAULT AS ISSUE #980, THE OTHER WAY ROUND. That one was
	// a node increasing `retaliation`, which only the Masochist's class line
	// names, so it was worth nothing on any other class. `FScopedPlayerClass`
	// below exists because of it. This is a node increasing `life_leech`, which
	// only the RAVAGER's line names, on a node only a Masochist can reach.

	// AS A MASOCHIST AND NOT AS WHATEVER RAN LAST. The default class is the
	// Ravager, which HAS a base life leech of 2.98% at level 100, so this test
	// would measure the Ravager's base rather than the node and the assertion
	// that the character starts at zero would fail. The class also decides which
	// base every other stat stands on; `RefreshAttributes` reads it.
	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class can be set"), AsMasochist.IsUsable()))
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

	const FName Node(TEXT("Masochist_basic_spine_004"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("Brutal Determination grants one row"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is life leech"), Effects[0]->Stat,
			  FString(TEXT("life_leech")));

	// THE BUCKET IS THE WHOLE POINT OF THE FIX AND IS ASSERTED, rather than left
	// for the arithmetic below to imply. An `increased` row here would be worth
	// nothing, and this test would then be measuring the wrong thing quietly.
	TestEqual(TEXT("supplying a base rather than increasing one"),
			  Effects[0]->ValueKind, FString(TEXT("flat")));

	const FGameplayAttribute Leech = Vital::GetLifeLeechAttribute();

	// NOTHING TO BEGIN WITH, WHICH IS THE CLASS LINE SPEAKING. Said out loud
	// because it is the fact the whole issue turns on: were this ever non-zero,
	// the assertion at the end would pass without the node doing anything.
	const float Before = AbilitySystem->GetNumericAttribute(Leech);
	TestEqual(TEXT("a Masochist starts with no life leech at all"), Before, 0.0f,
			  0.001f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 10);
	State->SetPassiveAllocation(Allocation, TArray<FName>());

	// THE ONE REAL ENTRY POINT, the same one the armour test above uses:
	// refreshing a character's attributes is what a worn item change does, and
	// it is where the passive tree was joined in.
	Equipment->RefreshAttributes(AbilitySystem);

	const float After = AbilitySystem->GetNumericAttribute(Leech);

	// FOUR PER CENT, FROM TEN POINTS WORTH FOUR TENTHS EACH. Pinned as a figure
	// rather than as a relationship, unlike the armour test above, and the
	// difference is the point: armour has a class base that tuning moves, and
	// this stat has none, so the whole of what is here came from the node.
	TestEqual(*FString::Printf(TEXT("ten points give four per cent: %.2f"), After),
			  After, 4.0f, 0.001f);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveMutilationMasteryOnARealCharacterTest,
	"Cataclysm.Passives.MutilationMasteryGivesARealMasochistFortyPercent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Mutilation Mastery on a real character. Issue #1032.
 *
 * "Your melee critical strikes have a 5% chance per point to apply Bleeding."
 * Eight points, so 40% at most.
 *
 * WHAT THIS TEST COVERS AND WHAT IT DOES NOT, stated plainly because the two
 * halves of this node cannot be joined in one test:
 *
 *   COVERED HERE. That the workbook row reaches a real spawned Masochist and
 *   lands on `BleedOnCritChance` as a flat 5 a point, so eight points read 40.
 *   That is the whole chain from `docs/All_Things_Cataclysm.xlsx` through
 *   `game/Data/PassiveEffects.csv`, the generated asset,
 *   `UCataclysmPlayerClassStats::StatToAttribute` and
 *   `UCataclysmPassiveTree::AccumulateInto` onto a character's attribute.
 *
 *   COVERED IN `CataclysmMeleeBleedTests.cpp`. That a melee critical strike
 *   reads THAT SAME attribute off the attacker and applies Bleeding, and that
 *   dropping any one of melee, critical or damage-that-reached-health applies
 *   nothing. Those tests drive the attribute to 100 and to 0.
 *
 *   COVERED NOWHERE, AND IT CANNOT BE. That a chance of 40 makes a bleed happen
 *   about 40% of the time. The roll is `FMath::FRandRange` inside
 *   `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` with no injection
 *   point, unlike the evasion, block and critical strike rolls, which
 *   `UCataclysmDamageCalculation::Resolve` takes as parameters. A test of it
 *   would have to be statistical. Issue #1034 is the same gap on the blunt
 *   weapon stun rule, which this copies.
 *
 * THE READING BEFORE ANY POINT IS SPENT IS HALF THE POINT. `ApplyTo` writes the
 * attribute for every mapped stat whether or not a modifier touches it, so a
 * Masochist that has spent nothing reading zero is what says the node -- rather
 * than a base somewhere -- is the source. Every class starts at zero and no
 * affix grants this.
 */
bool FCataclysmPassiveMutilationMasteryOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Combat = UCataclysmCombatAttributeSet;

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

	const FName Node(TEXT("Masochist_basic_fc_b2"));

	// WHAT THE ROW IS AUTHORED AS, checked before anything is spent. A row that
	// had become an increase rather than a flat amount would multiply a base of
	// zero and grant nothing, and the assertion further down would then be
	// reading the same zero it started from without saying why.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("Mutilation Mastery grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is the chance to bleed on a critical strike"),
			  Effects[0]->Stat, FString(TEXT("bleed_on_crit_chance")));
	TestEqual(TEXT("stated as a flat amount"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of five a point"), Effects[0]->ValuePerPoint, 5.0f);

	// AND IT CARRIES NO CONDITION, which is what makes reading the attribute
	// straight -- rather than asking `UCataclysmStatPipeline` for it -- correct
	// in `UCataclysmVitalAttributeSet`. A conditional row is never folded into
	// an attribute, so the day this row grows a condition the read there has to
	// change or the node silently stops working. Issue #1022 is that shape.
	TestEqual(TEXT("and no condition, which is what lets the rule read the "
				   "attribute directly"),
			  Effects[0]->Condition, FString());

	const FGameplayAttribute Chance = Combat::GetBleedOnCritChanceAttribute();

	// NOTHING SPENT, NOTHING GRANTED. This is the reading for every character in
	// the game except a Masochist who bought this node.
	Equipment->RefreshAttributes(AbilitySystem);
	TestEqual(TEXT("an unspent Masochist has no chance to bleed on a critical "
				   "strike"),
			  AbilitySystem->GetNumericAttribute(Chance), 0.0f, 0.001f);

	// ONE POINT FIRST, so the figure below is the row being applied per point
	// rather than a fixed amount granted for owning the node at all.
	{
		FCataclysmPassiveAllocation OnePoint;
		OnePoint.Add(Node, 1);
		State->SetPassiveAllocation(OnePoint, TArray<FName>());
		Equipment->RefreshAttributes(AbilitySystem);
		TestEqual(TEXT("one point is worth five percent"),
				  AbilitySystem->GetNumericAttribute(Chance), 5.0f, 0.001f);
	}

	// EIGHT POINTS, WHICH IS THE NODE'S OWN MAXIMUM, so 40 is what a player who
	// committed to it actually gets. The node text and the attribute's own
	// header both quote 40 for exactly this case.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 8);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	TestEqual(TEXT("eight points make it forty percent"),
			  AbilitySystem->GetNumericAttribute(Chance), 40.0f, 0.001f);

	// AND TAKING THE POINTS BACK TAKES THE CHANCE WITH THEM. Without this the
	// test would pass against a build that granted the chance once and never
	// recomputed it, which is what a refund or a respec would find.
	State->SetPassiveAllocation(FCataclysmPassiveAllocation(), TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);
	TestEqual(TEXT("and giving the points back takes it away again"),
			  AbilitySystem->GetNumericAttribute(Chance), 0.0f, 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// The Third Vow's two authored options. Issue #1040.
//
// TWO OPTIONS OF ONE CAPSTONE, so each test also has to say the OTHER one
// granted nothing. That is what the `Option` column exists for, and a build
// ignoring it would apply all seven rows to whoever reached 100 points.
// ---------------------------------------------------------------------------

namespace CataclysmPassiveTest
{
	/**
	 * Spend enough points elsewhere in the Masochist tree to open a capstone.
	 *
	 * READ OFF THE NODE TABLE RATHER THAN A LIST WRITTEN HERE, so this keeps
	 * working when the tree is re-authored, and every node is filled to its own
	 * maximum because a node's maximum is authored data.
	 *
	 * @param Capstone   the node being opened, which is skipped while filling
	 * @param OutFilled  how many points were actually placed
	 * @return the capstone's own threshold, or 0 if it could not be read
	 */
	static int32 FillTreeToOpen(const UDataTable* NodeTable, const FName& Capstone,
								FCataclysmPassiveAllocation& Allocation,
								int32& OutFilled)
	{
		OutFilled = 0;
		if (!NodeTable)
		{
			return 0;
		}

		int32 Threshold = 0;
		for (const TPair<FName, uint8*>& Pair : NodeTable->GetRowMap())
		{
			if (Pair.Key == Capstone)
			{
				Threshold = reinterpret_cast<const FCataclysmPassiveNodeRow*>(
					Pair.Value)->Threshold;
			}
		}
		if (Threshold <= 0)
		{
			return 0;
		}

		for (const TPair<FName, uint8*>& Pair : NodeTable->GetRowMap())
		{
			if (OutFilled >= Threshold)
			{
				break;
			}
			const auto* Row =
				reinterpret_cast<const FCataclysmPassiveNodeRow*>(Pair.Value);
			if (Row->Tree != TEXT("Masochist") || Pair.Key == Capstone
				|| Row->MaxPoints <= 0)
			{
				continue;
			}
			const int32 Take = FMath::Min(Row->MaxPoints, Threshold - OutFilled);
			Allocation.Add(Pair.Key, Take);
			OutFilled += Take;
		}
		return Threshold;
	}

	/**
	 * The additive sum on one stat, in percentage points, right now.
	 *
	 * WHY EVERY CAPSTONE TEST BELOW NEEDS THIS. The increases bucket is a SUM:
	 * a row worth 4 does not multiply the result by 1.04, it adds 4 to whatever
	 * sum is already there, so the result is multiplied by
	 * (100 + S + 4) / (100 + S). Filling a tree to a capstone's threshold spends
	 * points in other nodes, several of which touch the same stats, so S is
	 * rarely zero and an assertion written as "times 1.04" is wrong by however
	 * much the rest of the tree contributed. That mistake was made three times
	 * while these tests were being written.
	 *
	 * ASKED WITH THE CHARACTER'S CURRENT STATE, so a scaled or conditional row
	 * counts exactly as it would when the game itself asks.
	 */
	static float IncreasesOn(const UCataclysmAbilitySystemComponent* AbilitySystem,
							 const TCHAR* Stat)
	{
		if (!AbilitySystem)
		{
			return 0.0f;
		}
		const FCataclysmStatInputs* Inputs =
			AbilitySystem->GetStatInputs(FName(Stat));
		if (!Inputs)
		{
			return 0.0f;
		}
		return UCataclysmStatPipeline::Evaluate(
			Inputs->Base, Inputs->Modifiers, FGameplayTagContainer(),
			AbilitySystem->CurrentConditions()).SumOfIncreases;
	}

	/**
	 * Every More multiplier on one stat, multiplied together, right now.
	 *
	 * A RATIO WHERE `IncreasesOn` ABOVE GIVES A SUM, and the difference decides
	 * how an assertion is written. More multipliers multiply, so a row worth 5
	 * per debuff carried by two debuffs contributes ONE factor of 1.10 -- not
	 * two of 1.05, because `StackedValue` multiplies the row's value by the
	 * count and hands over a single modifier. Whatever the rest of the tree
	 * contributes is a separate constant factor, so the ratio between a reading
	 * before a choice and after it isolates the option cleanly.
	 */
	static float MoreMultiplierOn(
		const UCataclysmAbilitySystemComponent* AbilitySystem, const TCHAR* Stat)
	{
		if (!AbilitySystem)
		{
			return 1.0f;
		}
		const FCataclysmStatInputs* Inputs =
			AbilitySystem->GetStatInputs(FName(Stat));
		if (!Inputs)
		{
			return 1.0f;
		}
		return UCataclysmStatPipeline::Evaluate(
			Inputs->Base, Inputs->Modifiers, FGameplayTagContainer(),
			AbilitySystem->CurrentConditions()).MoreMultiplier;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveDeficitOnARealCharacterTest,
	"Cataclysm.Passives.DeficitPaysForHealthMissingAndHealthOwedOnARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Third Vow's first option on a real character. Issue #1040.
 *
 * "Your damage is increased by 1% for every 2% of your maximum health you are
 * missing or owe."
 *
 * BOTH HALVES OF "MISSING OR OWE", because they are two different states of one
 * character and two separate rows. A character that deferred a cost owes health
 * it is still standing on, so a build carrying only one of the two would pay a
 * player for being hurt and not for being in debt, or the reverse, and nothing
 * at run time would report it.
 *
 * MEASURED AS THE DIFFERENCE THE CHOICE MAKES, not as an absolute figure, and
 * that is forced by what opening a capstone costs. Reaching 100 points spends
 * points across the whole Masochist tree, and one of the nodes filled on the way
 * -- `Masochist_basic_fc_a0` -- ALSO scales with health missing. An absolute
 * reading at half health would include its bonus as well as this option's. So
 * every figure below is taken twice, once before the option is chosen and once
 * after, and only the difference is asserted.
 *
 * AND THE DIFFERENCE IS AN EXACT NUMBER RATHER THAN A DIRECTION, because the
 * increases bucket is a sum: adding 25 percentage points to it moves the final
 * damage by exactly the base times 0.25, whatever else is already in the sum.
 * The base is read from what the pipeline recorded for the stat.
 */
bool FCataclysmPassiveDeficitOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Combat = UCataclysmCombatAttributeSet;
	using Resource = UCataclysmClassResourceAttributeSet;
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
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the effect table loads"), EffectTable)
		|| !TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Masochist_capstone_100"));

	// WHAT THE ROWS ARE AUTHORED AS, checked before anything is spent. Four
	// belong to this option: two stats, because "damage" is attack damage and
	// spell damage in this project, each under two scales.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	TArray<const FCataclysmPassiveEffectRow*> Mine;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		if (Row->Option == 1)
		{
			Mine.Add(Row);
		}
	}
	if (!TestEqual(TEXT("Deficit grants four rows"), Mine.Num(), 4))
	{
		return false;
	}

	int32 Missing = 0;
	int32 Owed = 0;
	for (const FCataclysmPassiveEffectRow* Row : Mine)
	{
		TestEqual(*FString::Printf(TEXT("%s joins the additive sum"), *Row->Stat),
				  Row->ValueKind, FString(TEXT("increased")));
		TestEqual(*FString::Printf(TEXT("%s is one percent a step"), *Row->Stat),
				  Row->ValuePerPoint, 1.0f);
		TestEqual(*FString::Printf(TEXT("%s steps every two percent"), *Row->Stat),
				  Row->ScaleStep, 2.0f);
		if (Row->Scale == FString(TEXT("health_missing")))
		{
			++Missing;
		}
		else if (Row->Scale == FString(TEXT("health_owed")))
		{
			++Owed;
		}
	}
	TestEqual(TEXT("two of them scale with health missing"), Missing, 2);
	TestEqual(TEXT("and two with health owed"), Owed, 2);

	// A HUNDRED POINTS IN THE TREE FIRST, BECAUSE THAT IS WHEN THIS CAPSTONE
	// OPENS. `ChoosePassiveOption` checks the threshold as well as the points in
	// the node, so a player cannot decide all four capstones at 25 points.
	FCataclysmPassiveAllocation Allocation;
	int32 Filled = 0;
	const int32 Threshold = FillTreeToOpen(NodeTable, Node, Allocation, Filled);
	if (!TestTrue(TEXT("the capstone states a threshold"), Threshold > 0)
		|| !TestEqual(*FString::Printf(
			   TEXT("the tree can hold the %d points it opens at"), Threshold),
			   Filled, Threshold))
	{
		return false;
	}
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	const FGameplayTagContainer NoSkill;
	const auto AttackDamage = [&]
	{
		return AbilitySystem->StatForSkill(
			FName(TEXT("attack_damage")), NoSkill,
			AbilitySystem->GetNumericAttribute(
				Combat::GetAttackDamageAttribute()));
	};

	// THE SUM OF INCREASES, WHICH IS WHERE THIS OPTION'S ROWS LAND, and reading
	// it is what makes the assertions below exact figures rather than
	// directions.
	//
	// NOT THE BASE, AND THE FIRST VERSION OF THIS TEST TRIED THE BASE AND
	// FAILED. `attack_damage` has a base of ZERO on every character: a
	// character's attack damage comes from the weapon it holds, which
	// `UCataclysmEquipmentComponent::GatherModifiers` hands over as a FLAT
	// modifier, so the quantity the increases multiply is the base plus the
	// flats and not the base. Asserting "a quarter of the base" was therefore
	// asserting a quarter of nothing.
	//
	// THE SUM IS IN PERCENTAGE POINTS. 25 means the final figure is multiplied
	// by an extra 0.25 of whatever the increases multiply.
	const auto IncreasesOnAttackDamage = [&]() -> float
	{
		return IncreasesOn(AbilitySystem, TEXT("attack_damage"));
	};

	if (!TestNotNull(TEXT("the pipeline recorded what attack damage is built "
						  "from"),
					 AbilitySystem->GetStatInputs(FName(TEXT("attack_damage")))))
	{
		return false;
	}

	const float Maximum =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	if (!TestTrue(TEXT("the character has some maximum health"), Maximum > 0.0f))
	{
		return false;
	}

	// EVERY READING TAKEN TWICE, once with the capstone holding its point and no
	// choice made, and once after the choice. A capstone with no option chosen
	// grants nothing, which is what makes the first reading a clean baseline.
	//
	// AND A BASELINE IS NECESSARY RATHER THAN TIDY. Reaching the 100 points this
	// capstone opens at spends points across the whole tree, and one of the
	// nodes filled on the way -- `Masochist_basic_fc_a0` -- ALSO scales with
	// health missing. So the sum of increases at half health is already above
	// zero before this option grants anything, and an absolute figure here would
	// be measuring that node as well as this one.
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), Maximum);
	const float FullBefore = IncreasesOnAttackDamage();
	const float FullDamageBefore = AttackDamage();

	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   Maximum * 0.5f);
	const float HalfBefore = IncreasesOnAttackDamage();
	const float HalfDamageBefore = AttackDamage();

	// AND THE DAMAGE OVER TIME THE OTHER OPTION WOULD SOFTEN, read before any
	// choice is made for the same reason: Echoes of Agony is filled on the way
	// to 100 points and already reduces it, so 100 is NOT the baseline.
	const float SofteningBefore = AbilitySystem->GetNumericAttribute(
		Combat::GetDamageOverTimeTakenAttribute());

	FString Refusal;
	if (!TestTrue(TEXT("the first option can be chosen"),
				  State->ChoosePassiveOption(Node, 1, Refusal)))
	{
		AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
		return false;
	}
	Equipment->RefreshAttributes(AbilitySystem);

	// HALF THE HEALTH BAR MISSING IS TWENTY-FIVE STEPS OF TWO PERCENT, so the
	// sum of increases grows by exactly 25 percentage points.
	const float HalfAfter = IncreasesOnAttackDamage();
	TestEqual(TEXT("at half health the option adds 25 percentage points of "
				   "increased damage"),
			  HalfAfter - HalfBefore, 25.0f, 0.01f);

	// AND IT REACHES THE NUMBER A SKILL ACTUALLY USES, which is the half that
	// says the row is not merely present in the pipeline's own bookkeeping. The
	// increases bucket multiplies whatever the base and the flat modifiers come
	// to, so a sum rising from S to S+25 multiplies the final figure by
	// (100 + S + 25) / (100 + S). Both sums are measured rather than assumed.
	TestEqual(TEXT("and the damage a skill would use rises by exactly that"),
			  AttackDamage(),
			  HalfDamageBefore * (100.0f + HalfAfter) / (100.0f + HalfBefore),
			  HalfDamageBefore * 0.001f);

	// AND AT FULL HEALTH IT IS WORTH NOTHING, which is the half that catches a
	// scale dropped on the way. Without it the option would be a flat bonus and
	// nothing at run time would say so.
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), Maximum);
	TestEqual(TEXT("and at full health, with nothing owed, it adds nothing"),
			  IncreasesOnAttackDamage(), FullBefore, 0.01f);
	TestEqual(TEXT("so the damage there is where it started"),
			  AttackDamage(), FullDamageBefore, FullDamageBefore * 0.001f);

	// NOW THE SECOND HALF OF THE SENTENCE, on a character at FULL health that
	// owes some. Owing is not the same state as being hurt: a deferred cost is
	// health the character is still standing on.
	AbilitySystem->SetNumericAttributeBase(Resource::GetHealthOwedAttribute(),
										   Maximum * 0.2f);
	TestEqual(TEXT("owing a fifth of the bar adds 10 points, at full health"),
			  IncreasesOnAttackDamage() - FullBefore, 10.0f, 0.01f);

	// AND THE TWO ADD UP RATHER THAN ONE REPLACING THE OTHER.
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   Maximum * 0.5f);
	TestEqual(TEXT("half missing and a fifth owed together add 35"),
			  IncreasesOnAttackDamage() - HalfBefore, 35.0f, 0.01f);

	// AND THE OPTION THE PLAYER DID NOT PICK GRANTED NOTHING. Doctrine Made
	// Flesh is the third option of this same capstone and its own third row
	// softens damage over time; a build ignoring the `Option` column would have
	// applied it here too, taking this reading ten points lower.
	TestEqual(TEXT("the option that was not chosen granted nothing: damage over "
				   "time taken is where it was before the choice"),
			  AbilitySystem->GetNumericAttribute(
				  Combat::GetDamageOverTimeTakenAttribute()),
			  SofteningBefore, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveDoctrineMadeFleshOnARealCharacterTest,
	"Cataclysm.Passives.DoctrineMadeFleshPaysPerDebuffAndSoftensThemOnARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Third Vow's third option on a real character. Issue #1040.
 *
 * "Each debuff on you grants 1% more damage and reduces the damage that debuff
 * deals to you by 10%."
 *
 * BOTH CLAUSES, AND THEY ARE SHAPED DIFFERENTLY ON PURPOSE. The first grows with
 * how many debuffs are carried; the second does NOT. "That debuff" is each
 * debuff's own damage, and every debuff is reduced by the same tenth, so a
 * character carrying four is not taking 40% less from each. A build that scaled
 * the second clause as well would be several times stronger than the sentence.
 *
 * THE FIRST CLAUSE MULTIPLIES AND THE SECOND ADDS, which is the reading the
 * words give. "1% more damage" is its own multiplier; "reduced by 10%" joins the
 * additive sum, which is what Echoes of Agony's identical wording does.
 */
bool FCataclysmPassiveDoctrineMadeFleshOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Combat = UCataclysmCombatAttributeSet;

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
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the effect table loads"), EffectTable)
		|| !TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Masochist_capstone_100"));

	// WHAT THE ROWS ARE AUTHORED AS, checked before anything is spent. Three
	// belong to this option, and the third is deliberately unlike the other two:
	// it carries NO scale, which is what makes the second clause a flat
	// reduction rather than one that grows with the debuff count.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	TArray<const FCataclysmPassiveEffectRow*> Mine;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		if (Row->Option == 3)
		{
			Mine.Add(Row);
		}
	}
	if (!TestEqual(TEXT("Doctrine Made Flesh grants three rows"), Mine.Num(), 3))
	{
		return false;
	}

	int32 PerDebuff = 0;
	const FCataclysmPassiveEffectRow* Softening = nullptr;
	for (const FCataclysmPassiveEffectRow* Row : Mine)
	{
		if (Row->Stat == FString(TEXT("damage_over_time_taken")))
		{
			Softening = Row;
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s is its own multiplier"), *Row->Stat),
				  Row->ValueKind, FString(TEXT("more")));
		TestEqual(*FString::Printf(TEXT("%s is one percent a debuff"), *Row->Stat),
				  Row->ValuePerPoint, 1.0f);
		TestEqual(*FString::Printf(TEXT("%s counts debuffs"), *Row->Stat),
				  Row->Scale, FString(TEXT("debuffs_carried")));
		++PerDebuff;
	}
	TestEqual(TEXT("two rows grow with the debuff count"), PerDebuff, 2);
	if (!TestNotNull(TEXT("and one softens what a debuff deals"), Softening))
	{
		return false;
	}
	TestEqual(TEXT("the softening joins the additive sum"), Softening->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("it takes ten percent away"), Softening->ValuePerPoint,
			  -10.0f);
	TestEqual(TEXT("and it does NOT grow with the debuff count"),
			  Softening->Scale, FString());

	FCataclysmPassiveAllocation Allocation;
	int32 Filled = 0;
	const int32 Threshold = FillTreeToOpen(NodeTable, Node, Allocation, Filled);
	if (!TestTrue(TEXT("the capstone states a threshold"), Threshold > 0)
		|| !TestEqual(*FString::Printf(
			   TEXT("the tree can hold the %d points it opens at"), Threshold),
			   Filled, Threshold))
	{
		return false;
	}
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	const FGameplayTagContainer NoSkill;
	const auto AttackDamage = [&]
	{
		return AbilitySystem->StatForSkill(
			FName(TEXT("attack_damage")), NoSkill,
			AbilitySystem->GetNumericAttribute(
				Combat::GetAttackDamageAttribute()));
	};
	const FGameplayAttribute OverTimeTaken =
		Combat::GetDamageOverTimeTakenAttribute();

	// THE BASELINE IS NOT A HUNDRED, AND THE FIRST VERSION OF THIS TEST ASSUMED
	// IT WAS AND FAILED. Reaching the 100 points this capstone opens at spends
	// all ten points of Echoes of Agony, which is "Damage taken from damage over
	// time effects is reduced by 1% per point" -- so this reading is already 90
	// before the option grants anything. What this option adds is measured
	// against whatever the tree left here, not against a clean 100.
	//
	// IT IS STILL BELOW A HUNDRED, which is worth asserting: a reading at or
	// above normal would mean the tree was not filled and the comparison below
	// would be measuring nothing.
	const float SofteningBefore =
		AbilitySystem->GetNumericAttribute(OverTimeTaken);
	TestTrue(TEXT("filling the tree has already softened damage over time, so "
				  "this option is measured against that rather than against 100"),
			 SofteningBefore < UCataclysmDamageCalculation::NormalDamageTaken);

	const float NoDebuffs = AttackDamage();

	FString Refusal;
	if (!TestTrue(TEXT("the third option can be chosen"),
				  State->ChoosePassiveOption(Node, 3, Refusal)))
	{
		AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
		return false;
	}
	Equipment->RefreshAttributes(AbilitySystem);

	// THE SECOND CLAUSE IS READ OFF THE ATTRIBUTE, and that is correct here
	// where it would be wrong for the other two rows: this row carries no
	// condition and no scale, so it IS folded in. `UCataclysmDamageCalculation`
	// asks for the stat anyway, which is what lets a future conditional row work.
	//
	// TEN POINTS OFF WHATEVER THE TREE LEFT, not a tenth of it. The row is an
	// `increased` of -10, and the increases bucket is a SUM: the stat is 100 for
	// normal, so this takes the sum ten percentage points lower and the reading
	// falls by exactly ten. A tenth of the previous reading would be nine, which
	// is what a row written as a `more` would have given.
	TestEqual(TEXT("choosing it takes ten points off the damage over time that "
				   "reaches the character"),
			  SofteningBefore
				  - AbilitySystem->GetNumericAttribute(OverTimeTaken),
			  UCataclysmDamageCalculation::NormalDamageTaken * 0.10f, 0.01f);

	// AND WITH NO DEBUFF CARRIED, THE FIRST CLAUSE IS WORTH NOTHING. Without
	// this the option would be a flat multiplier on every Masochist that took
	// it, which is several times what the sentence says.
	TestEqual(TEXT("and with no debuff carried the damage is unchanged"),
			  AttackDamage(), NoDebuffs, NoDebuffs * 0.001f);

	// NOW MAKE IT CARRY DEBUFFS, THROUGH REAL GAMEPLAY EFFECTS. The count is a
	// read of the tags the ability system is already holding, so this is the
	// same route a real hit takes.
	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!TestTrue(TEXT("the vocabulary has Bleed and Burn"),
				  Bleed.IsValid() && Burn.IsValid()))
	{
		return false;
	}

	if (!TestTrue(TEXT("a bleed can be put on the character"),
				  UCataclysmSkillEffects::ApplyTagForDuration(
					  Character, Character, Bleed, 30.0f)))
	{
		return false;
	}
	TestEqual(TEXT("carrying one debuff is one percent more damage"),
			  AttackDamage(), NoDebuffs * 1.01f, NoDebuffs * 0.001f);

	if (!TestTrue(TEXT("a burn can be put on it as well"),
				  UCataclysmSkillEffects::ApplyTagForDuration(
					  Character, Character, Burn, 30.0f)))
	{
		return false;
	}
	TestEqual(TEXT("and carrying two debuffs is two percent, not one percent "
				   "twice over"),
			  AttackDamage(), NoDebuffs * 1.02f, NoDebuffs * 0.001f);

	// AND IT GOES AWAY AGAIN WHEN THE DEBUFFS DO. Without this the option would
	// pass against a build that turned the bonus on the first time the character
	// was ever hurt and left it there.
	UCataclysmSkillEffects::RemoveEffectsGranting(Character, Bleed);
	UCataclysmSkillEffects::RemoveEffectsGranting(Character, Burn);
	TestEqual(TEXT("carrying none again is back where it started"),
			  AttackDamage(), NoDebuffs, NoDebuffs * 0.001f);

	// AND THE SOFTENING IS STILL THERE, because it never depended on the count.
	TestEqual(TEXT("while the softening, which never counted debuffs, remains"),
			  SofteningBefore
				  - AbilitySystem->GetNumericAttribute(OverTimeTaken),
			  UCataclysmDamageCalculation::NormalDamageTaken * 0.10f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveStigmaticOnARealCharacterTest,
	"Cataclysm.Passives.StigmaticPaysPerDebuffInDamageAndInRealRegenerationOnARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The First Vow's third option on a real character. Issues #1042 and #1038.
 *
 * "Each debuff on you grants 4% increased damage and 4% increased health
 * regeneration."
 *
 * THE HEALTH REGENERATION HALF IS MEASURED AS REGENERATION, NOT AS THE
 * ATTRIBUTE, and that is the whole point of this test rather than a refinement
 * of it. `UCataclysmPlayerClassStats::ApplyTo` resolves every stat with the
 * default `FCataclysmStatConditions`, which reads every count as zero, so a
 * bonus that scales with the debuff count is NEVER folded into the gameplay
 * attribute. Reading `HealthRegen` here would therefore pass against a build in
 * which this row does nothing at all -- which is exactly what the build before
 * issue #1038 was. What the character actually gets back is what
 * `UCataclysmRegeneration::ApplyStep` puts on the health bar, and that is what
 * is asserted.
 *
 * MEASURED AS THE DIFFERENCE THE DEBUFFS MAKE, because filling the tree to 25
 * points spends points elsewhere and some of those touch the same stats. Every
 * figure is taken with no debuff carried and again with debuffs, and only the
 * change is asserted.
 *
 * NO CROSS-CHECK AGAINST THE OTHER TWO OPTIONS, and that is worth saying rather
 * than leaving a reader to notice its absence. The First Vow's other two are
 * Water to Blood and Reprisal Wave; neither is authored, so there are no rows
 * belonging to another option that could wrongly apply here. The Third Vow's two
 * tests do have that cross-check, because both of its options exist.
 */
bool FCataclysmPassiveStigmaticOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Combat = UCataclysmCombatAttributeSet;
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
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the effect table loads"), EffectTable)
		|| !TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Masochist_capstone_25"));

	// WHAT THE ROWS ARE AUTHORED AS, checked before anything is spent. Three,
	// and all three carry the same scale: two stats for "damage" and one for the
	// regeneration.
	//
	// THE OPTION IS FILTERED FOR BEFORE THEY ARE COUNTED, AND IT WAS NOT UNTIL
	// ISSUE #1047. This counted every row on the capstone and asserted three,
	// which was true only while Stigmatic was the ONLY authored option of The
	// First Vow. Authoring Reprisal Wave beside it made the count four and this
	// test failed, on a node it does not test and for a reason its own message
	// could not state. The neighbouring capstone tests all filter first; this
	// one did not, and the loop below asserting `Option == 3` on every row was
	// what hid it, because that assertion passes vacuously while nothing else is
	// authored and fails with an unhelpful message once something is.
	const TArray<const FCataclysmPassiveEffectRow*> Everything =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	TArray<const FCataclysmPassiveEffectRow*> Effects;
	for (const FCataclysmPassiveEffectRow* Row : Everything)
	{
		if (Row->Option == 3)
		{
			Effects.Add(Row);
		}
	}
	if (!TestEqual(TEXT("Stigmatic grants three rows"), Effects.Num(), 3))
	{
		return false;
	}
	bool bHasRegen = false;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		TestEqual(*FString::Printf(TEXT("%s joins the additive sum"), *Row->Stat),
				  Row->ValueKind, FString(TEXT("increased")));
		TestEqual(*FString::Printf(TEXT("%s is four percent a debuff"), *Row->Stat),
				  Row->ValuePerPoint, 4.0f);
		TestEqual(*FString::Printf(TEXT("%s counts debuffs"), *Row->Stat),
				  Row->Scale, FString(TEXT("debuffs_carried")));
		if (Row->Stat == FString(UCataclysmRegeneration::HealthRegenStat))
		{
			bHasRegen = true;
		}
	}
	TestTrue(TEXT("and one of them is the health regeneration"), bHasRegen);

	FCataclysmPassiveAllocation Allocation;
	int32 Filled = 0;
	const int32 Threshold = FillTreeToOpen(NodeTable, Node, Allocation, Filled);
	if (!TestTrue(TEXT("the capstone states a threshold"), Threshold > 0)
		|| !TestEqual(*FString::Printf(
			   TEXT("the tree can hold the %d points it opens at"), Threshold),
			   Filled, Threshold))
	{
		return false;
	}
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	FString Refusal;
	if (!TestTrue(TEXT("the third option can be chosen"),
				  State->ChoosePassiveOption(Node, 3, Refusal)))
	{
		AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
		return false;
	}
	Equipment->RefreshAttributes(AbilitySystem);

	const FGameplayTagContainer NoSkill;
	const auto AttackDamage = [&]
	{
		return AbilitySystem->StatForSkill(
			FName(TEXT("attack_damage")), NoSkill,
			AbilitySystem->GetNumericAttribute(
				Combat::GetAttackDamageAttribute()));
	};

	// WHAT ONE STEP OF REGENERATION REALLY PUTS BACK, which is the reading that
	// separates a working build from one where this row is dropped. The
	// character is hurt first, because a full health bar cannot go up and every
	// reading would be zero.
	const float Maximum =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	if (!TestTrue(TEXT("the character has some maximum health"), Maximum > 0.0f))
	{
		return false;
	}
	const auto OneStepOfRegeneration = [&]() -> float
	{
		AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
											   Maximum * 0.5f);
		const float Before =
			AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		UCataclysmRegeneration::ApplyStep(
			Character, UCataclysmRegeneration::StepSeconds,
			/*SecondsSinceLastDamage=*/100.0f);
		return AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute())
			- Before;
	};

	const float DamageWithNone = AttackDamage();
	const float RegenWithNone = OneStepOfRegeneration();

	// AND THE ADDITIVE SUM ON EACH STAT, because a row worth 4 does not multiply
	// the result by 1.04. It adds 4 to whatever the rest of the tree already
	// put in the sum, so the result moves by (100 + S + 4) / (100 + S). Filling
	// this tree to 25 points leaves 12 percentage points on the health
	// regeneration, and the first version of this test asserted a flat 1.04 and
	// failed by exactly that much.
	const float DamageIncreasesWithNone =
		IncreasesOn(AbilitySystem, TEXT("attack_damage"));
	const float RegenIncreasesWithNone =
		IncreasesOn(AbilitySystem, UCataclysmRegeneration::HealthRegenStat);

	// AND WHAT THE GAMEPLAY ATTRIBUTE HOLDS, kept so the reading taken after the
	// debuffs arrive can be compared against it. It must NOT move, and that is
	// the assertion saying why the regeneration above had to be measured rather
	// than read off this number.
	const float RegenAttributeWithNone = AbilitySystem->GetNumericAttribute(
		Vital::GetHealthRegenAttribute());
	if (!TestTrue(*FString::Printf(
			TEXT("the character regenerates something to begin with (%.4f a "
				 "step)"), RegenWithNone), RegenWithNone > 0.0f))
	{
		// A BASE RATE OF ZERO WOULD MAKE EVERY FIGURE BELOW ZERO, and the test
		// would pass while proving nothing.
		return false;
	}

	// ONE DEBUFF IS FOUR PERCENT, APPLIED THROUGH A REAL GAMEPLAY EFFECT. The
	// count is a read of the tags the ability system is already holding, so this
	// is the route a real hit takes.
	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!TestTrue(TEXT("the vocabulary has Bleed and Burn"),
				  Bleed.IsValid() && Burn.IsValid()))
	{
		return false;
	}

	if (!TestTrue(TEXT("a bleed can be put on the character"),
				  UCataclysmSkillEffects::ApplyTagForDuration(
					  Character, Character, Bleed, 30.0f)))
	{
		return false;
	}
	{
		const float Damage = IncreasesOn(AbilitySystem, TEXT("attack_damage"));
		const float Regen = IncreasesOn(
			AbilitySystem, UCataclysmRegeneration::HealthRegenStat);

		TestEqual(TEXT("one debuff adds four points of increased damage"),
				  Damage - DamageIncreasesWithNone, 4.0f, 0.01f);
		TestEqual(TEXT("and four points of increased health regeneration"),
				  Regen - RegenIncreasesWithNone, 4.0f, 0.01f);

		TestEqual(TEXT("the damage a skill would use rises by exactly that"),
				  AttackDamage(),
				  DamageWithNone * (100.0f + Damage)
					  / (100.0f + DamageIncreasesWithNone),
				  DamageWithNone * 0.001f);

		// AND THE HEALTH REALLY COMES BACK, which is the assertion the whole
		// test exists for. Issue #1038: before it, this row reached the
		// character's stat sheet and never reached its regeneration.
		TestEqual(TEXT("and that much more health really comes back"),
				  OneStepOfRegeneration(),
				  RegenWithNone * (100.0f + Regen)
					  / (100.0f + RegenIncreasesWithNone),
				  RegenWithNone * 0.001f);
	}

	// AND TWO DEBUFFS ARE EIGHT PERCENT, which is what says the bonus counts
	// them rather than merely noticing that one exists.
	if (!TestTrue(TEXT("a burn can be put on it as well"),
				  UCataclysmSkillEffects::ApplyTagForDuration(
					  Character, Character, Burn, 30.0f)))
	{
		return false;
	}
	{
		const float Damage = IncreasesOn(AbilitySystem, TEXT("attack_damage"));
		const float Regen = IncreasesOn(
			AbilitySystem, UCataclysmRegeneration::HealthRegenStat);

		TestEqual(TEXT("two debuffs add eight points of increased damage, so "
					   "the bonus counts them rather than noticing one exists"),
				  Damage - DamageIncreasesWithNone, 8.0f, 0.01f);
		TestEqual(TEXT("and eight points of increased health regeneration"),
				  Regen - RegenIncreasesWithNone, 8.0f, 0.01f);

		TestEqual(TEXT("the damage rises by exactly that"),
				  AttackDamage(),
				  DamageWithNone * (100.0f + Damage)
					  / (100.0f + DamageIncreasesWithNone),
				  DamageWithNone * 0.001f);
		TestEqual(TEXT("and so does the health that comes back"),
				  OneStepOfRegeneration(),
				  RegenWithNone * (100.0f + Regen)
					  / (100.0f + RegenIncreasesWithNone),
				  RegenWithNone * 0.001f);
	}

	// AND THE GAMEPLAY ATTRIBUTE NEVER MOVED THROUGHOUT, which is the assertion
	// that says why the regeneration had to be measured rather than read. A
	// scaled bonus is never folded into an attribute, so a build reading the
	// attribute where `ApplyStep` now asks would give the character its base rate
	// for ever and nothing would report it. Issue #1038.
	TestEqual(TEXT("while the health regeneration attribute is exactly where it "
				   "was with no debuff carried, which is why reading it would "
				   "have proved nothing"),
			  AbilitySystem->GetNumericAttribute(
				  Vital::GetHealthRegenAttribute()),
			  RegenAttributeWithNone, 0.001f);

	// AND IT ALL GOES AWAY WHEN THE DEBUFFS DO.
	UCataclysmSkillEffects::RemoveEffectsGranting(Character, Bleed);
	UCataclysmSkillEffects::RemoveEffectsGranting(Character, Burn);
	TestEqual(TEXT("carrying none again, the damage is back where it started"),
			  AttackDamage(), DamageWithNone, DamageWithNone * 0.001f);
	TestEqual(TEXT("and so is the health that comes back"),
			  OneStepOfRegeneration(), RegenWithNone, RegenWithNone * 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveVesselUnbrokenOnARealCharacterTest,
	"Cataclysm.Passives.VesselUnbrokenSilencesDebuffsAndPaysForThemOnARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Final Vow's third option on a real character. Issue #1039.
 *
 * "Debuffs on you deal no damage at all, and each one grants 5% more damage and
 * 5 Fervour per second."
 *
 * ALL THREE CLAUSES, AND THE FIRST IS WHY THE OPTION NEEDED NEW MACHINERY. "No
 * damage at all" cannot be a multiplier: `LessMultiplierFloor` clamps a Less
 * multiplier to -99 on purpose, and 99% less is not none. It is a flag, and
 * `UCataclysmDamageCalculation::Resolve` reads it at the damage over time step.
 *
 * THE DEBUFF MUST STILL BE CARRIED AFTERWARDS, and that is asserted rather than
 * assumed. The option's other two clauses count debuffs, so a build that removed
 * the effect instead of silencing its damage would make the option cancel its
 * own other two thirds -- and would still pass a test that only checked the
 * damage was gone.
 *
 * EVERY FIGURE IS MEASURED BEFORE AND AFTER THE CHOICE, at a fixed number of
 * debuffs. Filling the tree to 200 points spends points in Doctrine of Pain and
 * Flagellant, which multiply damage and grant Fervour for the same debuffs, so
 * an absolute reading would include theirs as well as this option's.
 */
bool FCataclysmPassiveVesselUnbrokenOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Combat = UCataclysmCombatAttributeSet;

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
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the effect table loads"), EffectTable)
		|| !TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Masochist_capstone_200"));

	// WHAT THE ROWS ARE AUTHORED AS, checked before anything is spent. Four, and
	// the flag is deliberately unlike the other three: it carries no scale,
	// because "no damage at all" does not grow with how many debuffs there are.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	TArray<const FCataclysmPassiveEffectRow*> Mine;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		if (Row->Option == 3)
		{
			Mine.Add(Row);
		}
	}
	if (!TestEqual(TEXT("Vessel Unbroken grants four rows"), Mine.Num(), 4))
	{
		return false;
	}

	const FCataclysmPassiveEffectRow* Silence = nullptr;
	int32 PerDebuff = 0;
	for (const FCataclysmPassiveEffectRow* Row : Mine)
	{
		if (Row->Stat
			== FString(UCataclysmDamageCalculation::DebuffDamageSuppressedStat))
		{
			Silence = Row;
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s counts debuffs"), *Row->Stat),
				  Row->Scale, FString(TEXT("debuffs_carried")));
		++PerDebuff;
	}
	TestEqual(TEXT("three rows grow with the debuff count"), PerDebuff, 3);
	if (!TestNotNull(TEXT("and one silences what a debuff deals"), Silence))
	{
		return false;
	}
	TestEqual(TEXT("the silence is a flat flag"), Silence->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of one, meaning on"), Silence->ValuePerPoint, 1.0f);
	TestEqual(TEXT("and it does NOT grow with the debuff count, because "
				   "\"no damage at all\" is not a quantity"),
			  Silence->Scale, FString());

	FCataclysmPassiveAllocation Allocation;
	int32 Filled = 0;
	const int32 Threshold = FillTreeToOpen(NodeTable, Node, Allocation, Filled);
	if (!TestTrue(TEXT("the capstone states a threshold"), Threshold > 0)
		|| !TestEqual(*FString::Printf(
			   TEXT("the tree can hold the %d points it opens at"), Threshold),
			   Filled, Threshold))
	{
		return false;
	}
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	// TWO DEBUFFS, PUT ON THROUGH REAL GAMEPLAY EFFECTS, and put on BEFORE the
	// choice so the two readings either side of it are taken in the same state.
	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!TestTrue(TEXT("the vocabulary has Bleed and Burn"),
				  Bleed.IsValid() && Burn.IsValid()))
	{
		return false;
	}
	if (!TestTrue(TEXT("a bleed can be put on the character"),
				  UCataclysmSkillEffects::ApplyTagForDuration(
					  Character, Character, Bleed, 30.0f))
		|| !TestTrue(TEXT("and a burn as well"),
					 UCataclysmSkillEffects::ApplyTagForDuration(
						 Character, Character, Burn, 30.0f)))
	{
		return false;
	}
	TestEqual(TEXT("the character carries two debuffs"),
			  UCataclysmDebuffs::CountOn(AbilitySystem), 2);

	// A SMALL BLOW, so the floor at remaining health can never be what is being
	// measured. `Resolve` ends with Min(Damage, Health), and a Masochist at
	// level 20 holds a few hundred health.
	constexpr float RawTick = 40.0f;

	const float OverTimeBefore =
		DamageAHitDeals(AbilitySystem, RawTick, /*bIsDamageOverTime=*/true);
	const float OrdinaryBefore =
		DamageAHitDeals(AbilitySystem, RawTick, /*bIsDamageOverTime=*/false);
	const float MoreBefore = MoreMultiplierOn(AbilitySystem,
											  TEXT("attack_damage"));
	const float FervourBefore = AbilitySystem->StatForSkill(
		FName(UCataclysmFervour::PerSecondStat), FGameplayTagContainer(), 0.0f);

	if (!TestTrue(*FString::Printf(
			TEXT("a damage over time tick hurts this character to begin with "
				 "(%.2f of %.0f)"), OverTimeBefore, RawTick),
			OverTimeBefore > 0.0f))
	{
		// WITHOUT THIS THE ZERO BELOW WOULD PROVE NOTHING, because a tick that
		// already dealt nothing would still deal nothing afterwards.
		return false;
	}

	FString Refusal;
	if (!TestTrue(TEXT("the third option can be chosen"),
				  State->ChoosePassiveOption(Node, 3, Refusal)))
	{
		AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
		return false;
	}
	Equipment->RefreshAttributes(AbilitySystem);

	// THE FIRST CLAUSE: NO DAMAGE AT ALL, not merely less of it.
	TestEqual(TEXT("a damage over time tick now deals nothing at all"),
			  DamageAHitDeals(AbilitySystem, RawTick,
							  /*bIsDamageOverTime=*/true),
			  0.0f, 0.001f);

	// AND AN ORDINARY BLOW IS UNTOUCHED, which is the half that catches a flag
	// read at the wrong step. A character immune to every hit is not what the
	// sentence says.
	TestEqual(TEXT("while an ordinary hit lands exactly as it did"),
			  DamageAHitDeals(AbilitySystem, RawTick,
							  /*bIsDamageOverTime=*/false),
			  OrdinaryBefore, FMath::Max(OrdinaryBefore * 0.001f, 0.001f));

	// AND THE DEBUFFS ARE STILL THERE. This is the assertion that says the
	// damage was silenced rather than the effect removed. Without it a build
	// that stripped the effects would pass everything above and quietly cancel
	// the option's other two clauses.
	TestEqual(TEXT("and the character still carries both debuffs, so the rest "
				   "of the option still has something to count"),
			  UCataclysmDebuffs::CountOn(AbilitySystem), 2);

	// THE SECOND CLAUSE: 5% MORE DAMAGE FOR EACH OF THE TWO, which is one More
	// multiplier of 1.10 rather than two of 1.05. `StackedValue` multiplies the
	// row's value by the count and hands over a single modifier.
	TestEqual(TEXT("two debuffs multiply damage by a further 1.10"),
			  MoreMultiplierOn(AbilitySystem, TEXT("attack_damage")),
			  MoreBefore * 1.10f, MoreBefore * 0.001f);

	// THE THIRD CLAUSE: 5 FERVOUR A SECOND FOR EACH OF THE TWO, on top of
	// whatever Flagellant already grants for the same two.
	TestEqual(TEXT("and grant ten Fervour a second between them"),
			  AbilitySystem->StatForSkill(
				  FName(UCataclysmFervour::PerSecondStat),
				  FGameplayTagContainer(), 0.0f),
			  FervourBefore + 10.0f, 0.01f);

	// AND ALL THREE FOLLOW THE DEBUFFS. Taking them away leaves the damage
	// multiplier and the Fervour where they started, and lets damage over time
	// hurt again -- which is what says the silence is the option's doing rather
	// than something that happened once and stuck.
	UCataclysmSkillEffects::RemoveEffectsGranting(Character, Bleed);
	UCataclysmSkillEffects::RemoveEffectsGranting(Character, Burn);
	TestEqual(TEXT("with no debuff carried the damage multiplier is back"),
			  MoreMultiplierOn(AbilitySystem, TEXT("attack_damage")),
			  MoreBefore, MoreBefore * 0.001f);
	TestEqual(TEXT("and so is the Fervour a second"),
			  AbilitySystem->StatForSkill(
				  FName(UCataclysmFervour::PerSecondStat),
				  FGameplayTagContainer(), 0.0f),
			  FervourBefore, 0.01f);

	// THE SILENCE, HOWEVER, STAYS. It never counted debuffs, so a character
	// holding this option takes nothing from damage over time whether or not it
	// is carrying any right now.
	TestEqual(TEXT("while damage over time still deals nothing, because the "
				   "silence never counted debuffs"),
			  DamageAHitDeals(AbilitySystem, RawTick,
							  /*bIsDamageOverTime=*/true),
			  0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveGluttonOnARealCharacterTest,
	"Cataclysm.Passives.GluttonGrowsRetaliationWithLifeLeechOnARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Third Vow's second option on a real character. Issue #1045.
 *
 * "Your retaliation damage is increased by 1% for every 1% of life leech you
 * have."
 *
 * ONE STAT'S VALUE DECIDING ANOTHER STAT'S SIZE, which no node did before this.
 * Every scale written until now reads a STATE -- where health is, what is owed,
 * how full the pool is, how many things are on the character -- and all of those
 * change from moment to moment. Life leech changes only when the character's
 * gear or passive points change, so this is a bonus that grows with an
 * investment rather than with a situation.
 *
 * THE SUM IS READ RATHER THAN A RATIO ASSUMED. The row lands in the increases
 * bucket, which is a sum: 20 points of leech add 20 percentage points to
 * whatever the rest of the tree already put there, so the final figure moves by
 * (100 + S + 20) / (100 + S) and not by 1.20. Filling this tree to 100 points
 * spends points in nodes that increase retaliation, so S is not zero.
 *
 * THE CROSS-CHECK AGAINST THE OTHER TWO OPTIONS USES DAMAGE OVER TIME AND NOT
 * DAMAGE, deliberately. Deficit, this capstone's first option, increases damage
 * with health missing -- and so does `Masochist_basic_fc_a0`, which is spent
 * while filling the tree, so a reading of damage at low health cannot tell the
 * two apart. Doctrine Made Flesh, the third option, softens damage over time by
 * a flat ten, and the only other node touching that stat contributes a constant.
 * So that reading isolates cleanly and this one does not.
 */
bool FCataclysmPassiveGluttonOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Combat = UCataclysmCombatAttributeSet;
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
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the effect table loads"), EffectTable)
		|| !TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Masochist_capstone_100"));

	// WHAT THE ROW IS AUTHORED AS, checked before anything is spent. ONE row,
	// which is what separates this option from the other two on the same
	// capstone: those each write two, because "damage" is attack damage and
	// spell damage, and retaliation is a single stat.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	TArray<const FCataclysmPassiveEffectRow*> Mine;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		if (Row->Option == 2)
		{
			Mine.Add(Row);
		}
	}
	if (!TestEqual(TEXT("Glutton grants one row"), Mine.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is retaliation"), Mine[0]->Stat,
			  FString(TEXT("retaliation")));
	TestEqual(TEXT("joining the additive sum"), Mine[0]->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("one percent a step"), Mine[0]->ValuePerPoint, 1.0f);
	TestEqual(TEXT("scaled by life leech"), Mine[0]->Scale,
			  FString(TEXT("life_leech")));
	TestEqual(TEXT("a step for every one percent of it"), Mine[0]->ScaleStep,
			  1.0f);

	FCataclysmPassiveAllocation Allocation;
	int32 Filled = 0;
	const int32 Threshold = FillTreeToOpen(NodeTable, Node, Allocation, Filled);
	if (!TestTrue(TEXT("the capstone states a threshold"), Threshold > 0)
		|| !TestEqual(*FString::Printf(
			   TEXT("the tree can hold the %d points it opens at"), Threshold),
			   Filled, Threshold))
	{
		return false;
	}
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	// READ BEFORE THE CHOICE IS MADE, for the cross-check further down. This is
	// whatever the filled tree left on the stat, which is NOT 100: Echoes of
	// Agony is spent on the way to 100 points.
	const float SofteningBefore = AbilitySystem->GetNumericAttribute(
		Combat::GetDamageOverTimeTakenAttribute());

	FString Refusal;
	if (!TestTrue(TEXT("the second option can be chosen"),
				  State->ChoosePassiveOption(Node, 2, Refusal)))
	{
		AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
		return false;
	}
	Equipment->RefreshAttributes(AbilitySystem);

	const FGameplayAttribute Leech = Vital::GetLifeLeechAttribute();
	const auto Retaliation = [&]
	{
		return AbilitySystem->StatForSkill(
			FName(TEXT("retaliation")), FGameplayTagContainer(),
			AbilitySystem->GetNumericAttribute(
				Combat::GetRetaliationAttribute()));
	};

	// NO LEECH AT ALL FIRST, so the option is worth nothing and this is a clean
	// baseline. Written rather than assumed: filling the tree spends points in
	// Undying Hunger, which increases life leech, so the character does not
	// start at zero.
	AbilitySystem->SetNumericAttributeBase(Leech, 0.0f);
	const float IncreasesWithNoLeech =
		IncreasesOn(AbilitySystem, TEXT("retaliation"));
	const float WithNoLeech = Retaliation();
	if (!TestTrue(*FString::Printf(
			TEXT("the character retaliates for something to begin with (%.2f)"),
			WithNoLeech), WithNoLeech > 0.0f))
	{
		// A BASELINE OF ZERO WOULD MAKE EVERY RATIO BELOW MEANINGLESS.
		return false;
	}

	// TWENTY PER CENT OF LIFE LEECH IS TWENTY STEPS OF ONE, so the sum of
	// increases on retaliation grows by exactly 20 percentage points.
	AbilitySystem->SetNumericAttributeBase(Leech, 20.0f);
	const float IncreasesAtTwenty =
		IncreasesOn(AbilitySystem, TEXT("retaliation"));
	TestEqual(TEXT("twenty percent of life leech adds twenty points of "
				   "increased retaliation"),
			  IncreasesAtTwenty - IncreasesWithNoLeech, 20.0f, 0.01f);

	// AND IT REACHES THE FIGURE THE GAME ITSELF ASKS FOR when a character
	// strikes back, which is the half that says the row is not merely present in
	// the pipeline's own bookkeeping.
	TestEqual(TEXT("and the retaliation a real strike back would use rises by "
				   "exactly that"),
			  Retaliation(),
			  WithNoLeech * (100.0f + IncreasesAtTwenty)
				  / (100.0f + IncreasesWithNoLeech),
			  WithNoLeech * 0.001f);

	// TWICE THE LEECH IS TWICE THE BONUS, which is what says the row counts the
	// leech rather than noticing that some exists.
	AbilitySystem->SetNumericAttributeBase(Leech, 40.0f);
	TestEqual(TEXT("forty percent of it adds forty points"),
			  IncreasesOn(AbilitySystem, TEXT("retaliation"))
				  - IncreasesWithNoLeech,
			  40.0f, 0.01f);

	// AND HALF A STEP COUNTS FOR NOTHING, because steps are whole and rounded
	// down. Every scale follows that rule and this is a place it can be seen:
	// 20.5% of leech is twenty whole steps, not twenty and a half.
	AbilitySystem->SetNumericAttributeBase(Leech, 20.5f);
	TestEqual(TEXT("and twenty and a half percent is twenty whole steps"),
			  IncreasesOn(AbilitySystem, TEXT("retaliation"))
				  - IncreasesWithNoLeech,
			  20.0f, 0.01f);

	// AND THE OPTION THE PLAYER DID NOT PICK GRANTED NOTHING. Doctrine Made
	// Flesh is the third option of this same capstone and softens damage over
	// time by a flat ten; a build ignoring the `Option` column would have
	// applied it here too, taking this reading ten points lower.
	TestEqual(TEXT("the third option was not chosen, so damage over time taken "
				   "is where the tree left it"),
			  AbilitySystem->GetNumericAttribute(
				  Combat::GetDamageOverTimeTakenAttribute()),
			  SofteningBefore, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveReprisalWaveOnARealCharacterTest,
	"Cataclysm.Passives.ReprisalWaveWidensRetaliationOnARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The First Vow's second option on a real character. Issue #1047.
 *
 * "Your retaliation damage strikes every enemy within 4 metres, not only the one
 * that hit you."
 *
 * WHAT THIS CHECKS AND WHAT IT DOES NOT. It checks that the authored row reaches
 * a character built the way the game builds one, that it carries the four the
 * node's own sentence states, and that the game's own reader answers with it.
 * Whether the wave then strikes the right enemies is checked where there are
 * enemies to strike, in `Cataclysm.Retaliation.AWaveStrikesEveryEnemyWithinItsRadius`.
 *
 * THE READING BEFORE THE CHOICE IS THE CROSS-CHECK, and it is a real one here.
 * A build that ignored the `Option` column would apply every row of the node the
 * moment the point was spent, so the radius would already be 4 before any option
 * was picked. It is zero, which is what says the column is honoured.
 *
 * NO SUM TO READ, unlike the four capstone tests above it. This row is a FLAT
 * modifier on a stat nothing else in the tree touches, so the attribute holds
 * exactly what the row grants and there is no bucket to subtract a baseline
 * from.
 */
bool FCataclysmPassiveReprisalWaveOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Combat = UCataclysmCombatAttributeSet;

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
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the effect table loads"), EffectTable)
		|| !TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Masochist_capstone_25"));

	// WHAT THE ROW IS AUTHORED AS, checked before anything is spent. ONE row:
	// everything the option changes is about retaliation, which the passive
	// effects sheet spells as one stat.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	TArray<const FCataclysmPassiveEffectRow*> Mine;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		if (Row->Option == 2)
		{
			Mine.Add(Row);
		}
	}
	if (!TestEqual(TEXT("Reprisal Wave grants one row"), Mine.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is the retaliation radius"), Mine[0]->Stat,
			  FString(UCataclysmRetaliation::RadiusMetresStat));
	TestEqual(TEXT("as a flat amount"), Mine[0]->ValueKind,
			  FString(TEXT("flat")));

	// FOUR, WHICH IS THE NUMBER IN THE NODE'S OWN SENTENCE. The sheet carries
	// metres rather than the centimetres Unreal measures in, so that
	// `test_every_value_appears_in_the_nodes_own_description` can tie the two
	// together. The one conversion is in `UCataclysmRetaliation`.
	TestEqual(TEXT("of four metres"), Mine[0]->ValuePerPoint, 4.0f);

	FCataclysmPassiveAllocation Allocation;
	int32 Filled = 0;
	const int32 Threshold = FillTreeToOpen(NodeTable, Node, Allocation, Filled);
	if (!TestTrue(TEXT("the capstone states a threshold"), Threshold > 0)
		|| !TestEqual(*FString::Printf(
			   TEXT("the tree can hold the %d points it opens at"), Threshold),
			   Filled, Threshold))
	{
		return false;
	}
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	// THE POINT IS SPENT AND NO OPTION IS CHOSEN, so nothing of the three has
	// been granted. See the note above: this is what says the `Option` column is
	// read at all.
	TestEqual(TEXT("with the point spent and no option chosen, retaliation "
				   "still reaches only whatever hit the character"),
			  UCataclysmRetaliation::RadiusMetresFor(AbilitySystem), 0.0f,
			  0.001f);

	FString Refusal;
	if (!TestTrue(TEXT("the second option can be chosen"),
				  State->ChoosePassiveOption(Node, 2, Refusal)))
	{
		AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
		return false;
	}
	Equipment->RefreshAttributes(AbilitySystem);

	// AND THE GAME'S OWN READER ANSWERS WITH THE FOUR, which is the half that
	// says the row is not merely sitting in the pipeline's bookkeeping. It asks
	// for the stat rather than reading the attribute, so this covers the route
	// the retaliation code really takes.
	TestEqual(TEXT("and now it reaches four metres"),
			  UCataclysmRetaliation::RadiusMetresFor(AbilitySystem), 4.0f,
			  0.001f);

	// AND THE ATTRIBUTE HOLDS IT TOO, because this row carries no condition and
	// no scale. A conditional row would be judged at the moment of the call and
	// would never be folded in, so the two answers agreeing is a statement about
	// this row rather than about every row.
	TestEqual(TEXT("and the attribute holds it, the row carrying no condition"),
			  AbilitySystem->GetNumericAttribute(
				  Combat::GetRetaliationRadiusMetresAttribute()),
			  4.0f, 0.001f);

	// AND THE OPTION THE PLAYER DID NOT PICK GRANTED NOTHING. Feeding Wound
	// belongs to a different capstone, so the flag it sets is the cleanest thing
	// to read here: it stays off.
	TestFalse(TEXT("and retaliation still does not leech"),
			  UCataclysmRetaliation::LeechesFor(AbilitySystem));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveFeedingWoundOnARealCharacterTest,
	"Cataclysm.Passives.FeedingWoundLetsRetaliationLeechOnARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Second Vow's second option on a real character. Issue #1048.
 *
 * "Your life leech applies to your retaliation damage as well as to your
 * attacks."
 *
 * THE FIRST OPTION OF THAT CAPSTONE TO BE AUTHORED AT ALL, so this is what takes
 * `Masochist_capstone_50` out of the list of Masochist nodes that do nothing.
 *
 * A FLAG WITH NO NUMBER IN ITS SENTENCE, which is why `VALUE_IN_WORDS` in
 * `tools/tests/test_passive_effects_match_the_node_text.py` carries the words
 * that stand in for the 1. How much is leeched is not stated here because it is
 * stated elsewhere: it is whatever life leech the character already has.
 *
 * WHAT THIS CHECKS AND WHAT IT DOES NOT. It checks that the authored row reaches
 * a real character and that the game's own reader answers with it. What the flag
 * then does to a real payment is checked where there is something to retaliate
 * against, in
 * `Cataclysm.Retaliation.LifeLeechReachesItOnlyForACharacterThatBoughtThat`.
 */
bool FCataclysmPassiveFeedingWoundOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Combat = UCataclysmCombatAttributeSet;

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
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the effect table loads"), EffectTable)
		|| !TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Masochist_capstone_50"));

	// THE OPTION IS FILTERED FOR BEFORE THE ROWS ARE COUNTED, though only one of
	// this capstone's three options is authored today and the filter therefore
	// removes nothing. It is here because the Stigmatic test above did not have
	// it and broke the moment a second option of ITS capstone was authored, on a
	// node it does not test.
	const TArray<const FCataclysmPassiveEffectRow*> Everything =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	TArray<const FCataclysmPassiveEffectRow*> Effects;
	for (const FCataclysmPassiveEffectRow* Row : Everything)
	{
		if (Row->Option == 2)
		{
			Effects.Add(Row);
		}
	}
	if (!TestEqual(TEXT("Feeding Wound grants one row"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is the retaliation leech flag"), Effects[0]->Stat,
			  FString(UCataclysmRetaliation::LeechesStat));
	TestEqual(TEXT("as a flat amount"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));

	// ONE, MEANING ON. There is no magnitude to write: the sentence says where
	// the character's life leech now applies, not how much of it there is.
	TestEqual(TEXT("of one, which means on"), Effects[0]->ValuePerPoint, 1.0f);

	FCataclysmPassiveAllocation Allocation;
	int32 Filled = 0;
	const int32 Threshold = FillTreeToOpen(NodeTable, Node, Allocation, Filled);
	if (!TestTrue(TEXT("the capstone states a threshold"), Threshold > 0)
		|| !TestEqual(*FString::Printf(
			   TEXT("the tree can hold the %d points it opens at"), Threshold),
			   Filled, Threshold))
	{
		return false;
	}
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	TestFalse(TEXT("with the point spent and no option chosen, retaliation does "
				   "not leech"),
			  UCataclysmRetaliation::LeechesFor(AbilitySystem));

	FString Refusal;
	if (!TestTrue(TEXT("the second option can be chosen"),
				  State->ChoosePassiveOption(Node, 2, Refusal)))
	{
		AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
		return false;
	}
	Equipment->RefreshAttributes(AbilitySystem);

	TestTrue(TEXT("and now it does"),
			 UCataclysmRetaliation::LeechesFor(AbilitySystem));
	TestEqual(TEXT("and the attribute holds the flag, the row carrying no "
				   "condition"),
			  AbilitySystem->GetNumericAttribute(
				  Combat::GetRetaliationLeechesAttribute()),
			  1.0f, 0.001f);

	// HOW MUCH LIFE LEECH THE CHARACTER HAS IS DELIBERATELY NOT ASSERTED HERE,
	// and an earlier draft of this test did assert it had some. That was wrong:
	// the Masochist's class line grants none, and filling the tree to fifty
	// points does not happen to spend a point on either of the two nodes that
	// increase it, so the figure is zero and the assertion failed. It was a
	// guess about what `FillTreeToOpen` spends rather than a statement about the
	// option, which is the same trap as reading a stat's baseline after filling
	// a tree and assuming it is the normal value.
	//
	// WHAT THE FLAG IS WORTH WITH REAL LEECH IS CHECKED WHERE THERE IS SOMETHING
	// TO RETALIATE AGAINST, in
	// `Cataclysm.Retaliation.LifeLeechReachesItOnlyForACharacterThatBoughtThat`,
	// which sets the leech itself and reads the payment.

	// AND THE OTHER CAPSTONE'S OPTION WAS NOT GRANTED WITH IT. Reaching fifty
	// points opens The First Vow as well, and Reprisal Wave is its second
	// option; choosing option two here must not choose option two there.
	TestEqual(TEXT("and retaliation still reaches only whatever hit the "
				   "character"),
			  UCataclysmRetaliation::RadiusMetresFor(AbilitySystem), 0.0f,
			  0.001f);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveUnstableAuraOnARealCharacterTest,
	"Cataclysm.Passives.UnstableAuraReleasesANovaOnARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Unstable Aura on a real character. Issue #1050.
 *
 * "While at or below 10% health, you release a nova every 5 seconds dealing
 * damage equal to 1% of your missing health per point to enemies within 6
 * metres."
 *
 * THE LAST MASOCHIST NODE THAT DID NOTHING AND WAS NEITHER BLOCKED NOR
 * UNDECIDED. The six that remain need a way to put a debuff on an ENEMY (issues
 * #742 and #674), a reading answered (#1033), a cap on unique debuffs that no
 * document decides, or a comparison between the character's debuffs and an
 * enemy's.
 *
 * IT IS A BASIC NODE AND NOT A CAPSTONE, so unlike the five capstone option
 * tests above it there is no threshold to fill to and no option to choose. Eight
 * points are spent on it directly.
 *
 * WHAT IS READ IS WHAT ONE NOVA IS WORTH, not an entry in the pipeline's
 * bookkeeping. `UCataclysmNova::Step` is the function the per-character timer
 * really calls, and it asks for the stat rather than reading the attribute,
 * so this covers the route the game itself takes.
 */
bool FCataclysmPassiveUnstableAuraOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Combat = UCataclysmCombatAttributeSet;
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

	const FName Node(TEXT("Masochist_basic_ll_b2"));

	// WHAT THE ROW IS AUTHORED AS, checked before anything is spent. ONE row:
	// the per-point share is the only magnitude the sheet carries, because the
	// 5 seconds and the 6 metres are constants of the mechanic on
	// `UCataclysmNova`, the way The Breaking Point's are.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("Unstable Aura grants one row"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is the nova stat"), Effects[0]->Stat,
			  FString(UCataclysmNova::DamageStat));
	TestEqual(TEXT("as a flat amount"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of one per cent a point"), Effects[0]->ValuePerPoint, 1.0f);
	TestEqual(TEXT("only at or below a tenth of health"), Effects[0]->Condition,
			  FString(TEXT("health_at_or_below")));
	TestEqual(TEXT("which is ten"), Effects[0]->ConditionValue, 10.0f);

	// EIGHT POINTS, WHICH IS THE WHOLE NODE.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 8);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	// THE ATTRIBUTE IS STILL ZERO, and that is the point of asking for the stat
	// rather than reading it. The row carries a condition, so
	// `UCataclysmPlayerClassStats::ApplyTo` refuses it and never folds it in.
	TestEqual(TEXT("the gameplay attribute stays at zero, the row being "
				   "conditional"),
			  AbilitySystem->GetNumericAttribute(
				  Combat::GetNovaDamageOfMissingHealthAttribute()),
			  0.0f, 0.001f);

	const float MaxHealth =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	if (!TestTrue(*FString::Printf(TEXT("the character has health (%.1f)"),
								   MaxHealth), MaxHealth > 0.0f))
	{
		return false;
	}

	// AT FULL HEALTH IT RELEASES NOTHING, asserted first so the reading below is
	// evidence of the node rather than of anything else on the step.
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   MaxHealth);
	TestEqual(TEXT("a character at full health releases no nova"),
			  UCataclysmNova::Step(Character), 0.0f, 0.001f);

	// AND AT A TWENTIETH OF ITS HEALTH IT DOES. Five per cent is at or below the
	// node's ten, and 95% of the bar is missing, so eight points deal 8% of that.
	const float Fifth = MaxHealth * 0.05f;
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), Fifth);

	TestEqual(TEXT("and one at five per cent releases eight per cent of what "
				   "it is missing"),
			  UCataclysmNova::Step(Character),
			  (MaxHealth - Fifth) * 8.0f / 100.0f, MaxHealth * 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveTheLastDropOnARealCharacterTest,
	"Cataclysm.Passives.TheLastDropSuppressesCostsAndPaysPerCastOnARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Final Vow's first option on a real character. Issue #1051.
 *
 * "While below 20% health your skills cost no health, and every skill you cast
 * grants 10 Fervour."
 *
 * BOTH CLAUSES ARE UNDER THE ONE CONDITION, which is a reading rather than
 * something the sentence settles outright. The Edge in the same tree has exactly
 * this shape -- "While at or below 20% health you take 25% less damage and your
 * Fervour does not decrease" -- and both of its clauses are under its condition.
 * `docs/DECISIONS.md` carries the reasoning.
 *
 * THE CONDITION IS THE FIRST STRICTLY-BELOW HEALTH THRESHOLD IN THE GAME. Every
 * other health threshold in all four trees is worded "at or below" and takes
 * `health_at_or_below`. The boundary itself is checked in
 * `Cataclysm.StatPipeline.BelowAndAtOrBelowDifferAtExactlyTheThreshold`; what
 * this test adds is that the row reaches a real character and that the game's
 * own readers answer with it.
 */
bool FCataclysmPassiveTheLastDropOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Resource = UCataclysmClassResourceAttributeSet;
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
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the effect table loads"), EffectTable)
		|| !TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Masochist_capstone_200"));

	// WHAT THE ROWS ARE AUTHORED AS. Two, one per clause, and both carry the
	// same condition.
	const TArray<const FCataclysmPassiveEffectRow*> Everything =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	TArray<const FCataclysmPassiveEffectRow*> Mine;
	for (const FCataclysmPassiveEffectRow* Row : Everything)
	{
		if (Row->Option == 1)
		{
			Mine.Add(Row);
		}
	}
	if (!TestEqual(TEXT("The Last Drop grants two rows"), Mine.Num(), 2))
	{
		return false;
	}

	bool bHasSuppression = false;
	bool bHasFervour = false;
	for (const FCataclysmPassiveEffectRow* Row : Mine)
	{
		TestEqual(*FString::Printf(TEXT("%s is a flat amount"), *Row->Stat),
				  Row->ValueKind, FString(TEXT("flat")));
		TestEqual(*FString::Printf(TEXT("%s is strictly below a threshold"),
								   *Row->Stat),
				  Row->Condition, FString(TEXT("health_below")));
		TestEqual(*FString::Printf(TEXT("%s uses a fifth of health"), *Row->Stat),
				  Row->ConditionValue, 20.0f);

		if (Row->Stat == FString(UCataclysmSkillTemplate::HealthCostSuppressedStat))
		{
			bHasSuppression = true;
			TestEqual(TEXT("the flag is a one, meaning on"), Row->ValuePerPoint,
					  1.0f);
		}
		else if (Row->Stat == FString(UCataclysmFervour::PerCastStat))
		{
			bHasFervour = true;
			TestEqual(TEXT("and the cast grants ten"), Row->ValuePerPoint, 10.0f);
		}
	}
	TestTrue(TEXT("one row suppresses the health cost"), bHasSuppression);
	TestTrue(TEXT("and one grants Fervour per cast"), bHasFervour);

	FCataclysmPassiveAllocation Allocation;
	int32 Filled = 0;
	const int32 Threshold = FillTreeToOpen(NodeTable, Node, Allocation, Filled);
	if (!TestTrue(TEXT("the capstone states a threshold"), Threshold > 0)
		|| !TestEqual(*FString::Printf(
			   TEXT("the tree can hold the %d points it opens at"), Threshold),
			   Filled, Threshold))
	{
		return false;
	}
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	const float MaxHealth =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	if (!TestTrue(*FString::Printf(TEXT("the character has health (%.1f)"),
								   MaxHealth), MaxHealth > 0.0f))
	{
		return false;
	}

	// HURT WELL BELOW THE THRESHOLD BEFORE THE CHOICE IS MADE, so that the
	// readings below differ because of the OPTION and not because of where
	// health is standing.
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   MaxHealth * 0.1f);

	TestFalse(TEXT("with the point spent and no option chosen, skills still "
				   "cost health"),
			  UCataclysmSkillTemplate::HealthCostIsSuppressed(AbilitySystem));
	TestEqual(TEXT("and a cast grants no Fervour"),
			  UCataclysmFervour::GainForCast(AbilitySystem), 0.0f, 0.001f);

	FString Refusal;
	if (!TestTrue(TEXT("the first option can be chosen"),
				  State->ChoosePassiveOption(Node, 1, Refusal)))
	{
		AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
		return false;
	}
	Equipment->RefreshAttributes(AbilitySystem);

	// AND NOW BOTH CLAUSES HOLD.
	TestTrue(TEXT("and now its skills cost no health"),
			 UCataclysmSkillTemplate::HealthCostIsSuppressed(AbilitySystem));

	// EMPTIED FIRST, because filling the tree to two hundred points spends
	// points in nodes that generate Fervour and the bar may already hold some.
	AbilitySystem->SetNumericAttributeBase(
		Resource::GetClassResourceAttribute(), 0.0f);
	TestEqual(TEXT("and a cast grants ten Fervour"),
			  UCataclysmFervour::GainForCast(AbilitySystem), 10.0f, 0.01f);

	// AND BOTH STOP AT THE THRESHOLD. Health back above a fifth and neither
	// clause applies, which is what says the condition is read at the moment of
	// the call rather than folded in once when the tree was applied.
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   MaxHealth * 0.5f);
	TestFalse(TEXT("at half health its skills cost health again"),
			  UCataclysmSkillTemplate::HealthCostIsSuppressed(AbilitySystem));
	TestEqual(TEXT("and a cast grants nothing there"),
			  UCataclysmFervour::GainForCast(AbilitySystem), 0.0f, 0.001f);

	// AND THE OPTIONS THE PLAYER DID NOT PICK GRANTED NOTHING. Vessel Unbroken
	// is the third option of this same capstone and suppresses damage over time;
	// a build ignoring the `Option` column would have set that flag here too.
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   MaxHealth * 0.1f);
	TestEqual(TEXT("and the third option was not chosen, so damage over time "
				   "still hurts"),
			  AbilitySystem->GetNumericAttribute(
				  UCataclysmCombatAttributeSet::GetDebuffDamageSuppressedAttribute()),
			  0.0f, 0.001f);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveSymphonyOfPainOnARealCharacterTest,
	"Cataclysm.Passives.SymphonyOfPainLengthensDebuffsAndSoftensThemOnARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Symphony of Pain on a real character. Issue #1033.
 *
 * "Debuffs on you last 2% longer per point, and their effect on you is reduced
 * by 1% per point."
 *
 * WHAT "THEIR EFFECT" MEANS WAS A READING AND THE PROJECT OWNER SETTLED IT on
 * 2026-08-28: it means the DAMAGE a lasting harmful effect deals. Reading it as
 * everything such an effect does would shorten its duration and contradict the
 * first half of the same sentence. The owner accepted the consequence, which is
 * that a stun is a pure downside for this node: a character with all eight
 * points is stunned 16% longer and gets nothing back for it.
 *
 * IT WAS ON THE BLOCKED LIST UNTIL THAT ANSWER, and it needed a stat that did
 * not exist: how long a lasting harmful effect applied TO this character runs.
 * Vessel of Plagues below needs the same one.
 *
 * A BASIC NODE, so unlike the capstone tests above there is no threshold to fill
 * to and no option to choose. Eight points are spent on it directly.
 */
bool FCataclysmPassiveSymphonyOfPainOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Combat = UCataclysmCombatAttributeSet;

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

	const FName Node(TEXT("Masochist_basic_fl_a2"));

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("Symphony of Pain grants two rows"), Effects.Num(), 2))
	{
		return false;
	}

	bool bHasDuration = false;
	bool bHasSoftening = false;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		TestEqual(*FString::Printf(TEXT("%s joins the additive sum"), *Row->Stat),
				  Row->ValueKind, FString(TEXT("increased")));
		TestTrue(*FString::Printf(TEXT("%s carries no condition"), *Row->Stat),
				 Row->Condition.IsEmpty());

		if (Row->Stat == FString(UCataclysmDebuffs::DurationStat))
		{
			bHasDuration = true;
			TestEqual(TEXT("two per cent longer a point"), Row->ValuePerPoint,
					  2.0f);
		}
		else if (Row->Stat
				 == FString(UCataclysmDamageCalculation::DamageOverTimeTakenStat))
		{
			bHasSoftening = true;
			TestEqual(TEXT("and one per cent less damage a point"),
					  Row->ValuePerPoint, -1.0f);
		}
	}
	TestTrue(TEXT("one row lengthens the effect"), bHasDuration);
	TestTrue(TEXT("and one softens its damage"), bHasSoftening);

	// READ BEFORE ANYTHING IS SPENT. Nothing else in the tree touches either
	// stat, so both start where the engine put them, but the readings are taken
	// rather than assumed: a bucket is a SUM and assuming a clean baseline has
	// cost three build cycles on this project before.
	const float LengthBefore = IncreasesOn(AbilitySystem,
										   UCataclysmDebuffs::DurationStat);
	const float SofteningBefore = IncreasesOn(
		AbilitySystem, UCataclysmDamageCalculation::DamageOverTimeTakenStat);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 8);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	// EIGHT POINTS AT TWO PER CENT IS SIXTEEN POINTS OF INCREASE, and one per
	// cent a point the other way is eight off the damage.
	TestEqual(TEXT("eight points add sixteen points of increased duration"),
			  IncreasesOn(AbilitySystem, UCataclysmDebuffs::DurationStat)
				  - LengthBefore, 16.0f, 0.01f);
	TestEqual(TEXT("and take eight points off the damage over time taken"),
			  IncreasesOn(AbilitySystem,
						  UCataclysmDamageCalculation::DamageOverTimeTakenStat)
				  - SofteningBefore, -8.0f, 0.01f);

	// AND THE GAME'S OWN READER ANSWERS WITH IT, which is the half that says the
	// rows are not merely sitting in the pipeline's bookkeeping. Neither row
	// carries a condition, so both are folded into their attributes and a ten
	// second effect really runs for 11.6.
	TestEqual(TEXT("and a ten second effect on this character runs for 11.6"),
			  UCataclysmDebuffs::DurationOn(AbilitySystem, 10.0f), 11.6f, 0.01f);
	TestEqual(TEXT("and the attribute holds it, the row carrying no condition"),
			  AbilitySystem->GetNumericAttribute(
				  Combat::GetDebuffDurationTakenAttribute()),
			  116.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveVesselOfPlaguesOnARealCharacterTest,
	"Cataclysm.Passives.VesselOfPlaguesLengthensDebuffsAndWorsensThemOnARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Vessel of Plagues on a real character. Issue #1033.
 *
 * "Debuffs on you last 50% longer, and debuffs on you deal 50% more damage to
 * you."
 *
 * THE NODE WAS REWORDED ON 2026-08-28 AND THIS IS WHY. Its first half read "You
 * can carry twice as many unique debuffs", and nothing in the game limits how
 * many DIFFERENT harmful effects a character may carry, so that half doubled
 * nothing. The project owner decided the number is deliberately unlimited --
 * which is what Path of Exile, Diablo IV and Last Epoch all do -- and chose this
 * replacement. `docs/DECISIONS.md` carries it.
 *
 * ITS TWO HALVES SIT IN DIFFERENT BUCKETS, and the node's own words decide
 * which. "50% more damage" uses the word this tree reserves for the
 * multiplicative bucket; "50% longer" says neither "more" nor "multiplicative",
 * so it joins the sum. That is why one is read with `IncreasesOn` and the other
 * with `MoreMultiplierOn`.
 */
bool FCataclysmPassiveVesselOfPlaguesOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Combat = UCataclysmCombatAttributeSet;

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

	const FName Node(TEXT("Masochist_keystone_fl_kA"));

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("Vessel of Plagues grants two rows"), Effects.Num(), 2))
	{
		return false;
	}

	bool bHasDuration = false;
	bool bHasWorsening = false;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		TestEqual(*FString::Printf(TEXT("%s is fifty"), *Row->Stat),
				  Row->ValuePerPoint, 50.0f);

		if (Row->Stat == FString(UCataclysmDebuffs::DurationStat))
		{
			bHasDuration = true;
			TestEqual(TEXT("the length joins the additive sum"), Row->ValueKind,
					  FString(TEXT("increased")));
		}
		else if (Row->Stat
				 == FString(UCataclysmDamageCalculation::DamageOverTimeTakenStat))
		{
			bHasWorsening = true;
			TestEqual(TEXT("and the damage multiplies"), Row->ValueKind,
					  FString(TEXT("more")));
		}
	}
	TestTrue(TEXT("one row lengthens the effect"), bHasDuration);
	TestTrue(TEXT("and one worsens its damage"), bHasWorsening);

	const float LengthBefore = IncreasesOn(AbilitySystem,
										   UCataclysmDebuffs::DurationStat);
	const float WorseningBefore = MoreMultiplierOn(
		AbilitySystem, UCataclysmDamageCalculation::DamageOverTimeTakenStat);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	TestEqual(TEXT("one point adds fifty points of increased duration"),
			  IncreasesOn(AbilitySystem, UCataclysmDebuffs::DurationStat)
				  - LengthBefore, 50.0f, 0.01f);

	// A RATIO WHERE THE READING ABOVE IS A SUM, which is why the two assertions
	// are written differently. A More multiplier multiplies whatever the rest of
	// the tree contributed, so the ratio between the reading before and after
	// isolates the row.
	TestEqual(TEXT("and multiplies the damage they deal by one and a half"),
			  MoreMultiplierOn(
				  AbilitySystem,
				  UCataclysmDamageCalculation::DamageOverTimeTakenStat),
			  WorseningBefore * 1.5f, 0.001f);

	// AND THE GAME'S OWN READER ANSWERS WITH THE LENGTH, which is the half that
	// says the row is not merely present in the pipeline's bookkeeping.
	TestEqual(TEXT("and a ten second effect on this character runs for fifteen"),
			  UCataclysmDebuffs::DurationOn(AbilitySystem, 10.0f), 15.0f, 0.01f);
	TestEqual(TEXT("and the attribute holds it, the row carrying no condition"),
			  AbilitySystem->GetNumericAttribute(
				  Combat::GetDebuffDurationTakenAttribute()),
			  150.0f, 0.01f);

	return true;
}


// ---------------------------------------------------------------------------
// A spent point reaching the character with nothing else touched
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveSpendingRefreshesTheStatLineTest,
	"Cataclysm.Passives.SpendingAPointRaisesMaximumHealthWithNothingElseTouched",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Spending a passive point has to reach the character by itself. Issue #1054.
 *
 * WHAT WENT WRONG. The project owner put points into nodes reading "+1%
 * increased Maximum Health per point" and their health did not move at all. The
 * nodes were authored correctly and the pipeline resolved them correctly.
 * Nothing ever ran the pipeline again. `UCataclysmEquipmentComponent::
 * RefreshAttributes` is the only place a spent passive point becomes a gameplay
 * attribute, and neither the screen nor the console command called it, so a
 * point took effect only by accident -- the next time the player changed a worn
 * item, spent an attribute point, or gained a level.
 *
 * WHY EVERY EARLIER TEST MISSED IT, AND WHY THIS ONE MUST NOT CALL
 * `RefreshAttributes`. Every other test of the tree writes the allocation with
 * `SetPassiveAllocation` and then calls `RefreshAttributes` itself. That is
 * exactly the step the game was missing, so those tests proved the pipeline and
 * never the wiring into it. A single call to `RefreshAttributes` anywhere below
 * would put the defect straight back out of this test's reach.
 *
 * BOTH WAYS A POINT CAN BE SPENT, because they are two call sites sharing one
 * function. `ACataclysmPlayerState::SpendPassivePoint` is what the console
 * command `Cataclysm.SpendPassivePoint` calls and also what
 * `UCataclysmPassiveTreeWidget::SpendInto` calls. The first half below spends
 * through the player state and the second half through the screen, so a fix
 * placed in only one of the two callers fails one half or the other.
 *
 * TWO STATS FROM ONE NODE, AND THAT IS NOT DECORATION. Pain Tolerance grants
 * maximum health AND armour, so a repair that somehow reached only the stat the
 * owner happened to name is caught here.
 *
 * READ AS A SUM AND NOT AS A MULTIPLIER. The increases bucket adds: ten points
 * worth 1% each do not multiply maximum health by 1.10, they add 10 to whatever
 * sum is already on the stat, and the result is multiplied by
 * (100 + S + 10) / (100 + S). `IncreasesOn` is what reads S.
 */
bool FCataclysmPassiveSpendingRefreshesTheStatLineTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	if (!TestNotNull(TEXT("a possessed player character"), Character))
	{
		return false;
	}

	APlayerController* Controller =
		Cast<APlayerController>(Character->GetController());
	ACataclysmPlayerState* State =
		Character->GetPlayerState<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!Controller || !State || !AbilitySystem)
	{
		AddError(TEXT("The spawned character is missing a component."));
		return false;
	}

	// THE REAL TABLES, because the whole point is the end-to-end path.
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	// THE ROOT AND THE NODE BEHIND IT. The root holds one point and opens Pain
	// Tolerance, which holds twelve.
	const FName Root(TEXT("Masochist_basic_spine_000"));
	const FName PainTolerance(TEXT("Masochist_basic_spine_001"));

	// WHAT THE SHEET SAYS THAT NODE IS WORTH, READ OUT OF THE REAL TABLE RATHER
	// THAN ASSUMED. Re-authoring Pain Tolerance is how this test would quietly
	// stop measuring anything: change its value per point and the arithmetic
	// below would be wrong while still passing.
	const TArray<const FCataclysmPassiveEffectRow*> Rows =
		UCataclysmPassiveTree::EffectsFor(EffectTable, PainTolerance);
	if (!TestEqual(TEXT("Pain Tolerance has two authored effects"), Rows.Num(), 2))
	{
		return false;
	}
	float HealthPerPoint = 0.0f;
	float ArmourPerPoint = 0.0f;
	for (const FCataclysmPassiveEffectRow* Row : Rows)
	{
		if (Row->Stat == TEXT("max_health"))
		{
			HealthPerPoint = Row->ValuePerPoint;
			TestEqual(TEXT("and the health one is an increase"), Row->ValueKind,
					  FString(TEXT("increased")));
		}
		else if (Row->Stat == TEXT("armor"))
		{
			ArmourPerPoint = Row->ValuePerPoint;
			TestEqual(TEXT("and so is the armour one"), Row->ValueKind,
					  FString(TEXT("increased")));
		}
	}
	if (!TestTrue(TEXT("one of the two is maximum health"), HealthPerPoint > 0.0f)
		|| !TestTrue(TEXT("and the other is armour"), ArmourPerPoint > 0.0f))
	{
		return false;
	}

	const float HealthBefore =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	const float ArmourBefore =
		AbilitySystem->GetNumericAttribute(Combat::GetArmorAttribute());
	const float HealthSumBefore = IncreasesOn(AbilitySystem, TEXT("max_health"));
	const float ArmourSumBefore = IncreasesOn(AbilitySystem, TEXT("armor"));

	// THE CHARACTER IS STANDING ON A REAL STAT LINE BEFORE ANYTHING IS SPENT.
	// `UCataclysmVitalAttributeSet`'s constructor writes a placeholder 100, and
	// a test that began from the placeholder would be measuring an unpossessed
	// character rather than the game.
	if (!TestTrue(*FString::Printf(
					  TEXT("maximum health is off the class line, not the "
						   "placeholder 100: %.1f"), HealthBefore),
				  HealthBefore > 100.0f))
	{
		return false;
	}

	// AND IT HAS ARMOUR TO MULTIPLY. A class line stating no armour would make
	// the armour half of this test compare zero against zero and pass on
	// nothing, which is the shape of a check that cannot fail.
	if (!TestTrue(*FString::Printf(
					  TEXT("and it starts with some armour to increase: %.1f"),
					  ArmourBefore),
				  ArmourBefore > 0.0f))
	{
		return false;
	}

	// ---------------------------------------------------------------------
	// Spending through the player state, which is what the console command does
	// ---------------------------------------------------------------------

	FString Reason;
	if (!TestTrue(TEXT("the Masochist root takes a point"),
				  State->SpendPassivePoint(Root, Reason)))
	{
		AddError(Reason);
		return false;
	}

	const int32 Points = 10;
	for (int32 Each = 0; Each < Points; ++Each)
	{
		if (!State->SpendPassivePoint(PainTolerance, Reason))
		{
			AddError(FString::Printf(
				TEXT("point %d of %d into Pain Tolerance was refused: %s"),
				Each + 1, Points, *Reason));
			return false;
		}
	}
	TestEqual(TEXT("ten points are on the node"),
			  State->GetPassiveAllocation().PointsIn(PainTolerance), Points);

	// NOTHING ELSE IS TOUCHED FROM HERE ON. No equipment change, no attribute
	// point, no level, and above all no call to `RefreshAttributes`.

	const float HealthAfter =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	const float ArmourAfter =
		AbilitySystem->GetNumericAttribute(Combat::GetArmorAttribute());
	const float HealthSumAfter = IncreasesOn(AbilitySystem, TEXT("max_health"));
	const float ArmourSumAfter = IncreasesOn(AbilitySystem, TEXT("armor"));

	// THE HEADLINE, IN THE OWNER'S OWN TERMS: the number on the screen went up.
	TestTrue(*FString::Printf(
				 TEXT("maximum health rose from %.1f to %.1f after ten points "
					  "into a node that says it raises it"),
				 HealthBefore, HealthAfter),
			 HealthAfter > HealthBefore);

	// AND BY THE AMOUNT THE SHEET STATES. Ten points at 1% each add 10 to the
	// sum of increases, whatever that sum already was.
	const float HealthExpectedSum = HealthSumBefore + HealthPerPoint * Points;
	TestEqual(TEXT("the sum of increases on maximum health grew by ten"),
			  HealthSumAfter, HealthExpectedSum, 0.01f);
	TestEqual(TEXT("and the attribute followed that sum"), HealthAfter,
			  HealthBefore * (100.0f + HealthExpectedSum)
				  / (100.0f + HealthSumBefore),
			  FMath::Max(HealthBefore * 0.001f, 0.01f));

	// THE SECOND STAT ON THE SAME NODE, so a repair that reached only the one
	// the owner named would fail here.
	const float ArmourExpectedSum = ArmourSumBefore + ArmourPerPoint * Points;
	TestTrue(*FString::Printf(TEXT("armour rose from %.1f to %.1f"), ArmourBefore,
							  ArmourAfter),
			 ArmourAfter > ArmourBefore);
	TestEqual(TEXT("the sum of increases on armour grew by five"), ArmourSumAfter,
			  ArmourExpectedSum, 0.01f);
	TestEqual(TEXT("and the armour attribute followed that sum"), ArmourAfter,
			  ArmourBefore * (100.0f + ArmourExpectedSum)
				  / (100.0f + ArmourSumBefore),
			  FMath::Max(ArmourBefore * 0.001f, 0.01f));

	// THE CHARACTER IS NOT HEALED BY SPENDING, WHICH IS THE RULE ALREADY IN
	// FORCE FOR ATTRIBUTE POINTS. `Cataclysm.SpendAttributePoint`'s own comment
	// says putting a point into Vitality "raises maximum health without healing
	// anybody", and a passive point has to behave the same way. The pools are
	// filled when a character arrives in the world and never again.
	TestEqual(TEXT("current health stayed where it was rather than refilling"),
			  AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute()),
			  HealthBefore, FMath::Max(HealthBefore * 0.001f, 0.01f));

	// ---------------------------------------------------------------------
	// And again through the screen, which is the other caller
	// ---------------------------------------------------------------------

	// `NewObject` RATHER THAN `CreateWidget`, for the reason
	// `TheScreenSpendsThroughTheCharacterAndNotIntoItself` gives: a world built
	// by `UWorld::CreateWorld` has no local player, and `CreateWidget` refuses a
	// controller that is not one.
	UCataclysmPassiveTreeWidget* Screen =
		NewObject<UCataclysmPassiveTreeWidget>(Controller);
	if (!TestNotNull(TEXT("the screen was created"), Screen))
	{
		return false;
	}
	Screen->SetPlayerStateForTests(State);
	Screen->ShowTree(TEXT("Masochist"));

	if (!TestTrue(TEXT("the screen puts an eleventh point into Pain Tolerance"),
				  Screen->SpendInto(PainTolerance)))
	{
		AddError(Screen->RefusalText().ToString());
		return false;
	}

	const float HealthAfterScreen =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	const float HealthSumAfterScreen =
		IncreasesOn(AbilitySystem, TEXT("max_health"));

	TestTrue(*FString::Printf(
				 TEXT("maximum health rose again, from %.1f to %.1f, when the "
					  "point was spent on the screen"),
				 HealthAfter, HealthAfterScreen),
			 HealthAfterScreen > HealthAfter);
	TestEqual(TEXT("by one more point's worth"), HealthSumAfterScreen,
			  HealthExpectedSum + HealthPerPoint, 0.01f);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassivePossessionAppliesTheTreeTest,
	"Cataclysm.Passives.ACharacterArrivingInTheWorldGetsThePointsItHasAlreadySpent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The other half of issue #1054: points spent before the character exists.
 *
 * WHAT WENT WRONG. `ACataclysmPlayerCharacter::ApplyChosenClassStats` runs from
 * `PossessedBy` and gathered its own modifier map -- the class line and the worn
 * items -- without ever asking the passive tree. It was a second, older copy of
 * the gathering `UCataclysmEquipmentComponent::RefreshAttributes` does, and the
 * tree had only ever been joined into the newer one. A character loading a save
 * with 230 points already spent therefore stood up with none of them applied,
 * and stayed that way until it happened to change a worn item.
 *
 * TWO CHARACTERS IN ONE WORLD, WHICH IS WHAT MAKES THIS MEASURABLE. The figure
 * a character should have depends on its class line and its level, and pinning
 * either here would make this test fail whenever they are tuned. An identical
 * character with an empty tree is the baseline instead, so the only difference
 * between the two readings is the ten points.
 *
 * THE ALLOCATION ARRIVES BEFORE THE PAWN DOES, deliberately. That is the order a
 * save restore takes, and it is the order in which
 * `ACataclysmPlayerState::RefreshCharacterStats` can do nothing at all -- there
 * is no character to write to yet. Possession has to be what applies it.
 *
 * AND NOTHING CALLS `RefreshAttributes`, for the reason
 * `SpendingAPointRaisesMaximumHealthWithNothingElseTouched` gives: it is the
 * step the game was missing, so a test that supplies it proves nothing.
 */
bool FCataclysmPassivePossessionAppliesTheTreeTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FName Root(TEXT("Masochist_basic_spine_000"));
	const FName PainTolerance(TEXT("Masochist_basic_spine_001"));
	const int32 Points = 10;

	// WHAT THE SHEET SAYS THE NODE IS WORTH, read rather than assumed, so
	// re-authoring Pain Tolerance cannot leave this test asserting an old figure.
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}
	float HealthPerPoint = 0.0f;
	for (const FCataclysmPassiveEffectRow* Row :
		 UCataclysmPassiveTree::EffectsFor(EffectTable, PainTolerance))
	{
		if (Row->Stat == TEXT("max_health"))
		{
			HealthPerPoint = Row->ValuePerPoint;
		}
	}
	if (!TestTrue(TEXT("Pain Tolerance grants maximum health"),
				  HealthPerPoint > 0.0f))
	{
		return false;
	}

	// ONE CHARACTER WITH AN EMPTY TREE AND ONE WITH TEN POINTS IN IT, built the
	// same way in the same world so nothing but the allocation differs.
	//
	// THE CURRENT HEALTH COMES BACK ALONGSIDE THE MAXIMUM, because the last
	// assertion needs the pair from the same character and finding it again
	// afterwards would mean identifying one character by its own reading.
	const auto Possess = [&](const FCataclysmPassiveAllocation* Allocation,
							 float& OutHealth) -> float
	{
		ACataclysmPlayerState* PlayerState =
			World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState || !Controller || !Character)
		{
			AddError(TEXT("A character could not be built."));
			return 0.0f;
		}

		Controller->SetPlayerState(PlayerState);

		// BEFORE POSSESSION. There is no pawn to write to at this moment, so
		// nothing about the allocation can reach an attribute until the line
		// below runs.
		if (Allocation)
		{
			PlayerState->SetPassiveAllocation(*Allocation, TArray<FName>());
		}

		Controller->Possess(Character);

		const UCataclysmAbilitySystemComponent* AbilitySystem =
			PlayerState->GetCataclysmAbilitySystemComponent();
		if (!AbilitySystem)
		{
			return 0.0f;
		}
		OutHealth =
			AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		return AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	};

	float BareHealth = 0.0f;
	const float Bare = Possess(nullptr, BareHealth);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Root, 1);
	Allocation.Add(PainTolerance, Points);
	float InvestedHealth = 0.0f;
	const float Invested = Possess(&Allocation, InvestedHealth);

	// THE BASELINE IS A REAL STAT LINE. A character still on the placeholder 100
	// its attribute set's constructor writes would make the comparison below
	// meaningless.
	if (!TestTrue(*FString::Printf(
					  TEXT("the bare character stands on its class line, not "
						   "the placeholder 100: %.1f"), Bare),
				  Bare > 100.0f))
	{
		return false;
	}

	TestTrue(*FString::Printf(
				 TEXT("the character that had already spent ten points stood up "
					  "with more health: %.1f against %.1f"),
				 Invested, Bare),
			 Invested > Bare);

	// AND BY EXACTLY WHAT THE SHEET STATES. Neither character has gear, spent
	// attribute points or anything else touching maximum health, so the sum of
	// increases on the bare one is zero and ten points at 1% each make the
	// difference a clean 10%.
	TestEqual(TEXT("and by the ten per cent the node promises"), Invested,
			  Bare * (100.0f + HealthPerPoint * Points) / 100.0f,
			  FMath::Max(Bare * 0.001f, 0.01f));

	// A CHARACTER ARRIVING IN THE WORLD STANDS UP FULL, which is the one case
	// where the pools are filled. This is the half of the change that could go
	// wrong in the other direction: filling the pools on every refresh instead
	// of only at possession would make swapping a helmet a free heal, and
	// `Cataclysm.Equipment` covers that side.
	//
	// AT THE RAISED MAXIMUM AND NOT THE UNRAISED ONE, which is what says the
	// tree was applied before the pools were filled rather than after. Health is
	// clamped to maximum health, so the order is load-bearing.
	TestEqual(TEXT("and it stands up at full health, on its raised maximum"),
			  InvestedHealth, Invested, FMath::Max(Invested * 0.001f, 0.01f));
	TestEqual(TEXT("as does the one with an empty tree, on its own"), BareHealth,
			  Bare, FMath::Max(Bare * 0.001f, 0.01f));

	return true;
}


// ---------------------------------------------------------------------------
// A capstone that has opened and is waiting to be decided
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveAwaitsAChoiceTest,
	"Cataclysm.Passives.ACapstoneThatHasOpenedAndIsUndecidedSaysSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmPassiveTree::AwaitsAnOptionChoice`. Issue #1064.
 *
 * WHY THE QUESTION EXISTS AT ALL. A capstone at its threshold with no option
 * taken cannot take a point -- `RefusalForSpending` says so -- and until this
 * issue every place that decided what a player may do asked only that. The
 * passive tree screen therefore drew a capstone that had just opened exactly
 * like one whose tier had not been reached, and the console command that lists
 * what is open left it out. The project owner spent thirty points, crossed the
 * first capstone's threshold of twenty-five, and nothing told them a decision
 * was waiting.
 *
 * THE REAL TABLE, because the answer depends on authored thresholds and on
 * whether a capstone names any options, and both are data.
 */
bool FCataclysmPassiveAwaitsAChoiceTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Capstone(TEXT("Masochist_capstone_25"));
	const FName Ordinary(TEXT("Masochist_basic_spine_001"));

	FCataclysmPassiveAllocation Allocation;

	// NOTHING SPENT, SO THE TIER IS NOT REACHED.
	TestFalse(TEXT("a capstone below its threshold is not waiting on anything"),
		UCataclysmPassiveTree::AwaitsAnOptionChoice(NodeTable, Allocation,
													Capstone));

	int32 Filled = 0;
	const int32 Threshold =
		FillTreeToOpen(NodeTable, Capstone, Allocation, Filled);
	if (!TestTrue(TEXT("the first Masochist capstone states a threshold"),
				  Threshold > 0)
		|| !TestEqual(TEXT("and the tree was filled to it"), Filled, Threshold))
	{
		return false;
	}

	// THE STATE THE OWNER WAS IN. Twenty-five points spent in the tree, the
	// capstone open, and no decision made.
	TestTrue(TEXT("at its threshold it is waiting on a choice"),
		UCataclysmPassiveTree::AwaitsAnOptionChoice(NodeTable, Allocation,
													Capstone));

	// AND IT STILL CANNOT TAKE A POINT, which is the fact every earlier caller
	// asked about and is why the state was invisible. Both are true at once.
	TestFalse(TEXT("while still refusing a point"),
		UCataclysmPassiveTree::RefusalForSpending(
			NodeTable, UCataclysmPassiveTree::LoadEdgeTable(), Allocation,
			Capstone, /*PointsAvailable=*/230).IsEmpty());

	// ONCE DECIDED THERE IS NOTHING LEFT TO ASK. The choice is permanent.
	FString Reason;
	if (!TestTrue(TEXT("the first option can be taken"),
				  UCataclysmPassiveTree::ChooseOption(NodeTable, Allocation,
													  Capstone, 1, Reason)))
	{
		AddError(Reason);
		return false;
	}
	TestFalse(TEXT("a decided capstone is no longer waiting"),
		UCataclysmPassiveTree::AwaitsAnOptionChoice(NodeTable, Allocation,
													Capstone));

	// AN ORDINARY NODE IS NEVER WAITING ON A CHOICE, whatever is spent.
	TestFalse(TEXT("an ordinary node has no options to choose between"),
		UCataclysmPassiveTree::AwaitsAnOptionChoice(NodeTable, Allocation,
													Ordinary));

	// AND NEITHER IS A CAPSTONE THAT NAMES NO OPTIONS. The Saboteur's four
	// offer none -- issue #935 -- and announcing a decision nobody wrote would
	// send a player looking for three names that do not exist.
	FCataclysmPassiveAllocation Saboteur;
	const FName SaboteurCapstone(TEXT("Saboteur_capstone_25"));
	for (const FName& Node :
		 UCataclysmPassiveTree::NodesIn(NodeTable, TEXT("Saboteur")))
	{
		if (Node != SaboteurCapstone)
		{
			Saboteur.Add(Node, 25);
			break;
		}
	}
	TestFalse(TEXT("a capstone offering no options announces nothing"),
		UCataclysmPassiveTree::AwaitsAnOptionChoice(NodeTable, Saboteur,
													SaboteurCapstone));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveClickOffersTheOptionsTest,
	"Cataclysm.Passives.ClickingAnOpenedCapstoneOffersItsOptionsRatherThanRefusing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * What a click on the passive tree screen means. Issue #1064.
 *
 * WHAT WAS WRONG. `UCataclysmPassiveTreeWidget::HandleNodeClicked` called
 * `SpendInto` and nothing else, so clicking a capstone always tried to spend a
 * point -- which is refused until one of its three options is taken. Nothing on
 * the screen ever called `ChooseOption`, so **no capstone in any tree could be
 * taken from the screen at all**: sixteen nodes, four per tree.
 *
 * WHY NO TEST CAUGHT IT. `HandleNodeClicked` is private and bound to a button by
 * reflection, and no headless test can press a button -- the automation command
 * passes `-nullrhi` and no widget draws. The two existing screen tests call
 * `SpendInto` and `ChooseOption` directly, so neither goes near the decision
 * between them. The decision now lives in `TouchNode`, which is public for
 * exactly that reason, and `HandleNodeClicked` is one line that calls it.
 *
 * THIS TEST MUST NOT CALL `SpendInto` OR `ChooseOption` TO DO THE WORK. Those
 * are the two things the screen was already able to do; what was missing was
 * anything deciding between them. It reads them only to assert what the old
 * behaviour still is.
 */
bool FCataclysmPassiveClickOffersTheOptionsTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	if (!TestNotNull(TEXT("a possessed player character"), Character))
	{
		return false;
	}

	APlayerController* Controller =
		Cast<APlayerController>(Character->GetController());
	ACataclysmPlayerState* State =
		Character->GetPlayerState<ACataclysmPlayerState>();
	if (!Controller || !State)
	{
		AddError(TEXT("The spawned character is missing a component."));
		return false;
	}

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		return false;
	}

	const FName Capstone(TEXT("Masochist_capstone_25"));

	UCataclysmPassiveTreeWidget* Screen =
		NewObject<UCataclysmPassiveTreeWidget>(Controller);
	if (!TestNotNull(TEXT("the screen was created"), Screen))
	{
		return false;
	}
	Screen->SetPlayerStateForTests(State);
	Screen->ShowTree(TEXT("Masochist"));

	// BELOW THE THRESHOLD A CLICK STILL MEANS SPEND, and is refused because the
	// tier has not been reached. Asserted first, so everything below is evidence
	// of the capstone having opened rather than of capstones being special.
	TestFalse(TEXT("clicking a shut capstone spends nothing"),
			  Screen->TouchNode(Capstone));
	TestTrue(TEXT("and the refusal is about its threshold"),
			 Screen->RefusalText().ToString().Contains(TEXT("opens at")));
	TestTrue(TEXT("and no options are being offered"),
			 Screen->GetCapstoneAwaitingAChoice().IsNone());

	// NOW OPEN IT. Twenty-five points elsewhere in the tree, which is the state
	// the project owner reported from.
	FCataclysmPassiveAllocation Allocation;
	int32 Filled = 0;
	const int32 Threshold =
		FillTreeToOpen(NodeTable, Capstone, Allocation, Filled);
	if (!TestTrue(TEXT("the tree was filled to the capstone's threshold"),
				  Threshold > 0 && Filled == Threshold))
	{
		return false;
	}
	State->SetPassiveAllocation(Allocation, TArray<FName>());

	// THE ASSERTION THE WHOLE ISSUE IS ABOUT. A click now offers the choice
	// instead of refusing a spend.
	TestTrue(TEXT("clicking an opened capstone does something"),
			 Screen->TouchNode(Capstone));
	TestEqual(TEXT("and what it did was offer that capstone's options"),
			  Screen->GetCapstoneAwaitingAChoice(), Capstone);
	TestEqual(TEXT("and it spent no point on the way"),
			  State->GetPassiveAllocation().PointsIn(Capstone), 0);

	// A CLICK NEVER COMMITS A CHOICE, because the choice is permanent. Offering
	// them is all it may do.
	TestEqual(TEXT("and committed nothing"),
			  State->GetPassiveAllocation().ChosenOptionIn(Capstone), 0);

	// SPENDING DIRECTLY IS STILL REFUSED, which is the behaviour that was right
	// all along and must not have changed.
	TestFalse(TEXT("spending into it directly is still refused"),
			  Screen->SpendInto(Capstone));
	TestTrue(TEXT("and says a choice comes first"),
			 Screen->RefusalText().ToString().Contains(TEXT("options first")));

	// TAKE ONE, AND TAKING IT IS THE WHOLE ACT. Issue #1075 changed this, and
	// what it changed is worth stating: until 2026-08-28 choosing recorded the
	// decision and nothing else, and the player had to click the capstone a
	// SECOND time to put the point in. Nothing on the screen said so, the
	// capstone granted nothing in the meantime, and the project owner took Water
	// to Blood in play and still had a mana pool.
	if (!TestTrue(TEXT("the first option can be taken"),
				  Screen->ChooseOption(Capstone, 1)))
	{
		AddError(Screen->RefusalText().ToString());
		return false;
	}
	TestFalse(TEXT("nothing is waiting on a choice any more"),
			  UCataclysmPassiveTree::AwaitsAnOptionChoice(
				  NodeTable, State->GetPassiveAllocation(), Capstone));

	TestEqual(TEXT("and the capstone already holds its point"),
			  State->GetPassiveAllocation().PointsIn(Capstone), 1);

	// AND A SECOND CLICK IS REFUSED BECAUSE THERE IS NOTHING LEFT TO DO, which
	// is what a full node has always answered. The refusal names the node as
	// full rather than saying nothing, so a player who clicks again is told why.
	TestFalse(TEXT("clicking it again spends nothing further"),
			  Screen->TouchNode(Capstone));
	TestTrue(TEXT("and says the node is full"),
			 Screen->RefusalText().ToString().Contains(TEXT("full at")));
	TestEqual(TEXT("and it still holds exactly one point"),
			  State->GetPassiveAllocation().PointsIn(Capstone), 1);

	// AND A CLICK ON AN ORDINARY NODE LEAVES NOTHING WAITING, which is the way
	// out of the option list without deciding.
	Screen->TouchNode(FName(TEXT("Masochist_basic_spine_001")));
	TestTrue(TEXT("an ordinary node leaves nothing waiting on a choice"),
			 Screen->GetCapstoneAwaitingAChoice().IsNone());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveOptionValueTest,
	"Cataclysm.Passives.AnOptionButtonsValueCannotBeMistakenForATree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The tree list carries two kinds of value while a capstone is being decided.
 * Issue #1064.
 *
 * ONE LIST AND ONE CLICK HANDLER CARRY BOTH, so the two have to be told apart
 * with certainty. A tree's value is its name out of the node table's `Tree`
 * column; an option's begins with a space, which no tree name can. Getting this
 * wrong in either direction is silent: a tree read as an option would commit a
 * permanent choice from a click meant to change which tree is shown.
 */
bool FCataclysmPassiveOptionValueTest::RunTest(const FString&)
{
	for (int32 Option = 1; Option <= UCataclysmPassiveTree::CapstoneOptions;
		 ++Option)
	{
		const FName Value = UCataclysmPassiveTreeWidget::OptionValue(Option);
		TestEqual(*FString::Printf(TEXT("option %d survives the round trip"),
								   Option),
				  UCataclysmPassiveTreeWidget::OptionFromValue(Value), Option);
	}

	// EVERY REAL TREE NAME READS AS NO OPTION AT ALL.
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		const TArray<FString> Trees = UCataclysmPassiveTree::TreeNames(NodeTable);
		TestTrue(TEXT("there are trees to check"), Trees.Num() > 0);
		for (const FString& Tree : Trees)
		{
			TestEqual(*FString::Printf(TEXT("%s is not an option"), *Tree),
					  UCataclysmPassiveTreeWidget::OptionFromValue(
						  FName(*Tree)), 0);
		}
	}

	TestEqual(TEXT("and neither is nothing at all"),
			  UCataclysmPassiveTreeWidget::OptionFromValue(NAME_None), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveWaterToBloodTest,
	"Cataclysm.Passives.WaterToBloodTradesTheManaPoolForHealthOnARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The first option of the Masochist's first capstone. Issue #1067.
 *
 * "You no longer have a mana pool. All maximum mana is converted into added
 * maximum health, and every ability costs health instead of mana."
 *
 * WHAT WAS WRONG. It had no row in `game/Data/PassiveEffects.csv` at all, so
 * taking it granted nothing: the project owner took it in play and still had a
 * mana pool and no added health. Issue #1066 measures how widespread that is --
 * 8 of 36 named capstone options granted something.
 *
 * THREE CLAUSES AND THREE ASSERTIONS. The mana maximum is gone, the health
 * maximum grew by exactly what the mana maximum was, and the character stands on
 * the converted figure rather than being clamped back to the old one.
 *
 * THE FOURTH CLAUSE -- that abilities cost health -- IS NOT HERE. It is a
 * property of an activation rather than of a stat line, and it needs a granted
 * ability to test;
 * `Cataclysm.Skills.WaterToBloodPaysASkillsCostOutOfHealth` covers it.
 *
 * MEASURED AGAINST THE SAME CHARACTER WITHOUT THE OPTION, because the figures
 * come from the class stat line at the character's level and pinning either here
 * would make this fail whenever the Masochist line is tuned.
 */
bool FCataclysmPassiveWaterToBloodTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Vital = UCataclysmVitalAttributeSet;

	// THE MASOCHIST, BEFORE THE CHARACTER IS SPAWNED. Its class line is the only
	// one this option is reachable from, and a character built on another line
	// would have a different mana pool to convert.
	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsMasochist.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Capstone(TEXT("Masochist_capstone_25"));

	// THE ALLOCATION BOTH CHARACTERS SHARE: the tree filled to the capstone's
	// threshold, and one point in the capstone itself.
	FCataclysmPassiveAllocation Taken;
	int32 Filled = 0;
	if (!TestTrue(TEXT("the tree can be filled to the capstone's threshold"),
				  FillTreeToOpen(NodeTable, Capstone, Taken, Filled) > 0))
	{
		return false;
	}
	Taken.Add(Capstone, 1);

	FCataclysmPassiveAllocation Undecided = Taken;

	// AND THE ONE DIFFERENCE. The option is taken on one and not the other, so
	// nothing but the option can explain what changes.
	Taken.SetChosenOption(Capstone, 1);

	const auto Stand = [&](const FCataclysmPassiveAllocation& Allocation,
						   float& OutMaxHealth, float& OutMaxMana,
						   float& OutHealth, float& OutMana) -> bool
	{
		ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
		ACataclysmPlayerState* State =
			Character ? Character->GetPlayerState<ACataclysmPlayerState>()
					  : nullptr;
		UCataclysmEquipmentComponent* Equipment =
			Character ? Character->GetEquipment() : nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem =
			State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
		if (!State || !Equipment || !AbilitySystem)
		{
			AddError(TEXT("The spawned character is missing a component."));
			return false;
		}

		// THE ALLOCATION ARRIVES AND THE STAT LINE IS WRITTEN AGAIN, which is
		// what a character standing in the world does when a point is spent.
		// `SetPassiveAllocation` re-runs the pipeline itself since issue #1054.
		State->SetPassiveAllocation(Allocation, TArray<FName>());

		// AND THE POOLS ARE FILLED, because the conversion has to happen before
		// they are and this is the only path that fills them. A character
		// arriving in the world takes it; `LeaveAsTheyAre` is every other
		// caller.
		Equipment->RefreshAttributes(AbilitySystem,
									 ECataclysmPoolFill::FillToMaximum);

		OutMaxHealth =
			AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
		OutMaxMana =
			AbilitySystem->GetNumericAttribute(Vital::GetMaxManaAttribute());
		OutHealth =
			AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		OutMana = AbilitySystem->GetNumericAttribute(Vital::GetManaAttribute());
		return true;
	};

	float BareMaxHealth = 0.0f, BareMaxMana = 0.0f;
	float BareHealth = 0.0f, BareMana = 0.0f;
	if (!Stand(Undecided, BareMaxHealth, BareMaxMana, BareHealth, BareMana))
	{
		return false;
	}

	// THE CHARACTER HAS A MANA POOL TO TRADE. Without this the test would pass
	// on a class line with no mana at all and prove nothing.
	if (!TestTrue(*FString::Printf(
					  TEXT("a Masochist with the option undecided has mana: %.1f"),
					  BareMaxMana),
				  BareMaxMana > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("and stands on a full mana pool"), BareMana, BareMaxMana,
			  FMath::Max(BareMaxMana * 0.001f, 0.01f));

	float MaxHealth = 0.0f, MaxMana = 0.0f, Health = 0.0f, Mana = 0.0f;
	if (!Stand(Taken, MaxHealth, MaxMana, Health, Mana))
	{
		return false;
	}

	// "YOU NO LONGER HAVE A MANA POOL."
	TestEqual(TEXT("the mana maximum is gone"), MaxMana, 0.0f, 0.01f);
	TestEqual(TEXT("and so is the mana that was in it"), Mana, 0.0f, 0.01f);

	// "ALL MAXIMUM MANA IS CONVERTED INTO ADDED MAXIMUM HEALTH."
	TestEqual(*FString::Printf(
				  TEXT("maximum health grew from %.1f to %.1f, which is the "
					   "%.1f mana that went away"),
				  BareMaxHealth, MaxHealth, BareMaxMana),
			  MaxHealth, BareMaxHealth + BareMaxMana,
			  FMath::Max(MaxHealth * 0.001f, 0.01f));

	// AND THE CHARACTER STANDS ON THE CONVERTED FIGURE. This is the assertion
	// that catches the ordering: `UCataclysmVitalAttributeSet` clamps current
	// health to maximum health, so a conversion done after the pools were filled
	// would leave the character on the old maximum with a larger one above it.
	TestEqual(TEXT("and it stands up full on the raised maximum"), Health,
			  MaxHealth, FMath::Max(MaxHealth * 0.001f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveChoosingTakesTheCapstoneTest,
	"Cataclysm.Passives.ChoosingACapstoneOptionSpendsThePointThatTurnsItOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Choosing a capstone option takes the capstone. Issue #1075.
 *
 * WHAT WENT WRONG IN PLAY. Taking a capstone was two separate acts: choose one
 * of its three options, then spend a point on the node. The screen led the
 * player through the first, went silent at the exact moment it would have said a
 * second was left, and the capstone granted nothing. The project owner took
 * Water to Blood on 2026-08-28, was told the option was picked, and still had a
 * mana pool and abilities that still cost mana. Their save record read
 * `{"Node": "Masochist_capstone_25", "Points": 0, "ChosenOption": 1}`.
 *
 * WHY THE OPTION ITSELF WAS FINE.
 * `UCataclysmPassiveTree::AccumulateInto` skips a node holding no points before
 * it ever reads the chosen option, so every row of that option was passed over.
 * `Cataclysm.Passives.WaterToBloodTradesTheManaPoolForHealthOnARealCharacter`
 * passes and always did -- it spends a point on the capstone as well as choosing
 * the option, which is the state a player could not reach from the screen.
 *
 * THE SAME CHARACTER AND THE SAME OPTION AS THAT TEST, ONE ACT SHORT. It reaches
 * the state the owner was really in and asks whether one click gets out of it.
 */
bool FCataclysmPassiveChoosingTakesTheCapstoneTest::RunTest(const FString&)
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

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Capstone(TEXT("Masochist_capstone_25"));

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	ACataclysmPlayerState* State =
		Character ? Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
	UCataclysmEquipmentComponent* Equipment =
		Character ? Character->GetEquipment() : nullptr;
	UCataclysmAbilitySystemComponent* AbilitySystem =
		State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!State || !Equipment || !AbilitySystem)
	{
		AddError(TEXT("The spawned character is missing a component."));
		return false;
	}

	// THE TREE FILLED TO THE CAPSTONE'S THRESHOLD AND NOT ONE POINT MORE, which
	// is exactly where the owner's character stood: the capstone had opened and
	// held nothing.
	FCataclysmPassiveAllocation Opened;
	int32 Filled = 0;
	const int32 Threshold = FillTreeToOpen(NodeTable, Capstone, Opened, Filled);
	if (!TestTrue(TEXT("the capstone states a threshold"), Threshold > 0)
		|| !TestEqual(TEXT("and the tree can be filled to it"), Filled,
					  Threshold))
	{
		return false;
	}
	State->SetPassiveAllocation(Opened, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem, ECataclysmPoolFill::FillToMaximum);

	TestEqual(TEXT("the capstone holds no point yet"),
			  State->GetPassiveAllocation().PointsIn(Capstone), 0);
	TestEqual(TEXT("and no option has been chosen"),
			  State->GetPassiveAllocation().ChosenOptionIn(Capstone), 0);

	// AND A MANA POOL TO TRADE. Without this the assertion below would hold on a
	// character that never had one and would prove nothing.
	const float BeforeMaxMana =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxManaAttribute());
	if (!TestTrue(*FString::Printf(TEXT("the character has mana: %.1f"),
								   BeforeMaxMana),
				  BeforeMaxMana > 0.0f))
	{
		return false;
	}

	// ONE ACT, AND IT IS THE ONLY ONE THE PLAYER PERFORMS.
	FString Refusal;
	if (!TestTrue(TEXT("the first option can be chosen"),
				  State->ChoosePassiveOption(Capstone, 1, Refusal)))
	{
		AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
		return false;
	}

	// THE CHOICE IS RECORDED AND THE POINT IS IN. Both halves, because a build
	// that recorded the choice and skipped the point is exactly what was wrong.
	TestEqual(TEXT("the option is recorded"),
			  State->GetPassiveAllocation().ChosenOptionIn(Capstone), 1);
	TestEqual(TEXT("and the capstone now holds its point"),
			  State->GetPassiveAllocation().PointsIn(Capstone), 1);
	TestEqual(TEXT("and exactly one point was charged for it"),
			  State->GetPassiveAllocation().Total(), Threshold + 1);

	// AND THE OPTION REALLY REACHES THE CHARACTER, which is the assertion the
	// player would have made: Water to Blood takes the mana pool away.
	TestEqual(TEXT("the mana maximum is gone"),
			  AbilitySystem->GetNumericAttribute(Vital::GetMaxManaAttribute()),
			  0.0f, 0.01f);

	// AND CHOOSING THE SAME OPTION AGAIN IS NOT CHARGED A SECOND POINT, which is
	// what a player clicking the marked option does.
	if (!TestTrue(TEXT("the same option can be chosen again"),
				  State->ChoosePassiveOption(Capstone, 1, Refusal)))
	{
		AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
		return false;
	}
	TestEqual(TEXT("and the capstone still holds exactly one point"),
			  State->GetPassiveAllocation().PointsIn(Capstone), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveChoosingWithNoPointsTest,
	"Cataclysm.Passives.ChoosingACapstoneOptionWithNoPointsLeftCommitsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The other half of issue #1075, and the reason both acts are applied to a copy.
 *
 * THE CHOICE IS PERMANENT. Every capstone's own description ends "The choice is
 * permanent", so a character that recorded a choice and then found it had no
 * point left would be committed for ever to an option it could not turn on, and
 * only a respec of the whole tree would get it back. The act has to be refused
 * whole.
 */
bool FCataclysmPassiveChoosingWithNoPointsTest::RunTest(const FString&)
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

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		return false;
	}

	const FName Capstone(TEXT("Masochist_capstone_25"));

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	ACataclysmPlayerState* State =
		Character ? Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
	if (!State)
	{
		AddError(TEXT("The spawned character has no player state."));
		return false;
	}

	// THE TREE FILLED TO THE THRESHOLD, AND THEN TO THE LAST POINT THE CHARACTER
	// HAS. The capstone has opened and there is nothing left to put in it.
	FCataclysmPassiveAllocation Spent;
	int32 Filled = 0;
	if (!TestTrue(TEXT("the capstone states a threshold"),
				  FillTreeToOpen(NodeTable, Capstone, Spent, Filled) > 0))
	{
		return false;
	}

	const int32 Available = State->PassivePointsAvailable();
	for (const TPair<FName, uint8*>& Pair : NodeTable->GetRowMap())
	{
		if (Spent.Total() >= Available)
		{
			break;
		}
		const auto* Row =
			reinterpret_cast<const FCataclysmPassiveNodeRow*>(Pair.Value);
		if (Row->Tree != TEXT("Masochist") || Pair.Key == Capstone)
		{
			continue;
		}
		const int32 Room = Row->MaxPoints - Spent.PointsIn(Pair.Key);
		const int32 Take = FMath::Min(Room, Available - Spent.Total());
		if (Take > 0)
		{
			Spent.Add(Pair.Key, Take);
		}
	}

	if (!TestEqual(TEXT("every point the character has is spent elsewhere"),
				   Spent.Total(), Available))
	{
		return false;
	}
	State->SetPassiveAllocation(Spent, TArray<FName>());

	// AND THE WHOLE ACT IS REFUSED.
	FString Refusal;
	TestFalse(TEXT("choosing an option with no points left is refused"),
			  State->ChoosePassiveOption(Capstone, 1, Refusal));
	TestTrue(TEXT("and the refusal says the points are gone"),
			 Refusal.Contains(TEXT("No passive points left")));

	// AND NOTHING WAS COMMITTED, WHICH IS THE POINT. A build that recorded the
	// choice before finding out it could not spend would leave this character
	// permanently committed to an option that grants nothing.
	TestEqual(TEXT("no option was recorded"),
			  State->GetPassiveAllocation().ChosenOptionIn(Capstone), 0);
	TestEqual(TEXT("and the capstone holds no point"),
			  State->GetPassiveAllocation().PointsIn(Capstone), 0);
	TestEqual(TEXT("and nothing else moved either"),
			  State->GetPassiveAllocation().Total(), Available);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveCapstoneOptionTextTest,
	"Cataclysm.Passives.ReadingACapstoneSaysWhatItsThreeOptionsDo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A capstone's three options describe themselves. Issue #1076.
 *
 * WHAT WAS WRONG. The passive tree screen showed a node's own `Description` and
 * nothing else. Every capstone's own description is "Unlocks at N points spent.
 * Choose one. The choice is permanent" -- it names no option and describes none
 * -- so the screen asked a player to make a permanent decision between three
 * things it never named. The project owner reported on 2026-08-28 that you have
 * to guess what they do.
 *
 * THE TEXT WAS ALREADY IN THE TABLE AND UNREAD. `Option1Description` and its two
 * siblings are on every capstone row and nothing in
 * `game/Source/Cataclysm/Interface/` read any of them.
 *
 * THE TABLE IS READ RATHER THAN A FIXTURE BUILT, because what is being checked
 * is that the real rows reach a reader. A fixture would pass against a table
 * whose option descriptions were all empty.
 */
bool FCataclysmPassiveCapstoneOptionTextTest::RunTest(const FString&)
{
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	// AN ORDINARY NODE IS UNCHANGED, and this comes first: a version that
	// appended options to everything would pass every check below it.
	const FName Ordinary(TEXT("Masochist_basic_spine_001"));
	const FCataclysmPassiveNodeRow* OrdinaryRow =
		UCataclysmPassiveTree::FindNode(NodeTable, Ordinary);
	if (!TestNotNull(TEXT("an ordinary Masochist node exists"), OrdinaryRow))
	{
		return false;
	}
	TestEqual(TEXT("an ordinary node reads exactly its own description"),
			  UCataclysmPassiveTree::FullDescriptionOf(NodeTable, Ordinary),
			  OrdinaryRow->Description);

	// AND A CAPSTONE CARRIES ALL THREE OPTIONS, name and text alike.
	const FName Capstone(TEXT("Masochist_capstone_25"));
	const FCataclysmPassiveNodeRow* CapstoneRow =
		UCataclysmPassiveTree::FindNode(NodeTable, Capstone);
	if (!TestNotNull(TEXT("the first Masochist capstone exists"), CapstoneRow))
	{
		return false;
	}

	const FString Full =
		UCataclysmPassiveTree::FullDescriptionOf(NodeTable, Capstone);

	TestTrue(TEXT("it still starts with the capstone's own description"),
			 Full.StartsWith(CapstoneRow->Description));

	const TArray<FString> Names =
		UCataclysmPassiveTree::OptionNamesOf(*CapstoneRow);
	const TArray<FString> Descriptions =
		UCataclysmPassiveTree::OptionDescriptionsOf(*CapstoneRow);

	for (int32 Index = 0; Index < Names.Num(); ++Index)
	{
		// THE ROWS THEMSELVES HAVE TO CARRY THE TEXT, which is the half that
		// catches a table regenerated without those columns. Without it every
		// assertion below would hold vacuously on empty strings.
		if (!TestTrue(*FString::Printf(
						  TEXT("option %d of the first Masochist capstone is "
							   "named"), Index + 1),
					  !Names[Index].IsEmpty())
			|| !TestTrue(*FString::Printf(
							 TEXT("and option %d says what it does"), Index + 1),
						 !Descriptions[Index].IsEmpty()))
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("the text names %s"), *Names[Index]),
				 Full.Contains(Names[Index]));
		TestTrue(*FString::Printf(TEXT("and says what %s does"), *Names[Index]),
				 Full.Contains(Descriptions[Index]));
	}

	// AND A CAPSTONE WITH NO OPTIONS WRITTEN READS AS ITS OWN DESCRIPTION AND
	// NOTHING MORE. The Saboteur's four name none at all, issue #935, and three
	// empty headings would suggest there is something there to read.
	const FName Nameless(TEXT("Saboteur_capstone_25"));
	const FCataclysmPassiveNodeRow* NamelessRow =
		UCataclysmPassiveTree::FindNode(NodeTable, Nameless);
	if (TestNotNull(TEXT("the first Saboteur capstone exists"), NamelessRow))
	{
		TestEqual(TEXT("a capstone naming no option reads as its own "
					   "description alone"),
				  UCataclysmPassiveTree::FullDescriptionOf(NodeTable, Nameless),
				  NamelessRow->Description);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveShortCapstoneTextTest,
	"Cataclysm.Passives.ACapstoneReadsAsOneLineUntilItsOptionsAreOnOffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The short form of a capstone's description. Issue #1078.
 *
 * WHY LENGTH IS A CORRECTNESS QUESTION ON THIS SCREEN. The tree is drawn on the
 * one child of the screen's vertical box with a Fill size rule, so the panel
 * gives up whatever height the labels below it take. Issue #1076 put seven lines
 * of option text in one of those labels and the tree lost half the screen; the
 * project owner could not click a capstone.
 *
 * SO THERE ARE TWO FORMS AND THE SCREEN PICKS BETWEEN THEM. The short one is
 * what a player reads while moving around the tree. The long one is shown only
 * while a capstone's three options are actually on offer, which is the moment
 * they are being read and the moment nobody is clicking nodes.
 */
bool FCataclysmPassiveShortCapstoneTextTest::RunTest(const FString&)
{
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		return false;
	}

	// AN ORDINARY NODE READS THE SAME IN BOTH FORMS, which comes first: a short
	// form that shortened everything would pass the checks below it.
	const FName Ordinary(TEXT("Masochist_basic_spine_001"));
	const FCataclysmPassiveNodeRow* OrdinaryRow =
		UCataclysmPassiveTree::FindNode(NodeTable, Ordinary);
	if (!TestNotNull(TEXT("an ordinary Masochist node exists"), OrdinaryRow))
	{
		return false;
	}
	TestEqual(TEXT("an ordinary node's short form is its own description"),
			  UCataclysmPassiveTree::ShortDescriptionOf(NodeTable, Ordinary),
			  OrdinaryRow->Description);

	const FName Capstone(TEXT("Masochist_capstone_25"));
	const FCataclysmPassiveNodeRow* CapstoneRow =
		UCataclysmPassiveTree::FindNode(NodeTable, Capstone);
	if (!TestNotNull(TEXT("the first Masochist capstone exists"), CapstoneRow))
	{
		return false;
	}

	const FString Short =
		UCataclysmPassiveTree::ShortDescriptionOf(NodeTable, Capstone);
	const FString Full =
		UCataclysmPassiveTree::FullDescriptionOf(NodeTable, Capstone);

	// IT IS ONE LINE. That is the whole property: no line break anywhere in it,
	// so it cannot take height off the tree however many options a capstone has.
	TestFalse(TEXT("the short form holds no line break"),
			  Short.Contains(TEXT("\n")));
	TestTrue(TEXT("and the long form does"), Full.Contains(TEXT("\n")));

	// AND IT STILL NAMES ALL THREE OPTIONS, which is what a player needs to know
	// exists before deciding to look.
	const TArray<FString> Names =
		UCataclysmPassiveTree::OptionNamesOf(*CapstoneRow);
	const TArray<FString> Descriptions =
		UCataclysmPassiveTree::OptionDescriptionsOf(*CapstoneRow);

	for (int32 Index = 0; Index < Names.Num(); ++Index)
	{
		if (!TestTrue(*FString::Printf(TEXT("option %d is named"), Index + 1),
					  !Names[Index].IsEmpty())
			|| !TestTrue(*FString::Printf(TEXT("and option %d is described"),
										  Index + 1),
						 !Descriptions[Index].IsEmpty()))
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("the short form names %s"),
								  *Names[Index]),
				 Short.Contains(Names[Index]));

		// AND NOT WHAT IT DOES, which is the half that keeps it to one line.
		TestFalse(*FString::Printf(TEXT("and does not say what %s does"),
								   *Names[Index]),
				  Short.Contains(Descriptions[Index]));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveRefitOnResizeTest,
	"Cataclysm.Passives.TheTreeIsFittedAgainWhenItsPanelChangesSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The tree is fitted again when the panel it is drawn on changes size.
 * Issue #1078.
 *
 * WHAT WENT WRONG IN PLAY. `FitToTree` ran when a different tree was shown and
 * at no other time. The panel is the one child of the screen's vertical box with
 * a Fill size rule, so it gives up whatever height the labels below it take, and
 * it clips to its own bounds. A longer description made it shorter, the graph
 * kept the zoom it had been fitted at, and the part that no longer fitted was
 * not drawn -- so the project owner could see a capstone and could not click it.
 *
 * THE PANEL SIZE IS SUPPLIED RATHER THAN MEASURED. A headless test has no
 * geometry, so `CanvasSize` would answer the same guess every time and the panel
 * could never appear to change. `SetPanelSizeForTests` is what makes the one
 * thing worth checking checkable.
 */
bool FCataclysmPassiveRefitOnResizeTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	APlayerController* Controller =
		Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	ACataclysmPlayerState* State =
		Character ? Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
	if (!Controller || !State)
	{
		AddError(TEXT("The spawned character is missing a component."));
		return false;
	}

	UCataclysmPassiveTreeWidget* Screen =
		NewObject<UCataclysmPassiveTreeWidget>(Controller);
	if (!TestNotNull(TEXT("the screen was created"), Screen))
	{
		return false;
	}
	Screen->SetPlayerStateForTests(State);

	// A TALL PANEL, AND A TREE FITTED TO IT.
	const FVector2D Tall(1600.0, 800.0);
	Screen->SetPanelSizeForTests(Tall);
	Screen->ShowTree(TEXT("Masochist"));

	const FGeometry Nothing;
	Screen->NativeTick(Nothing, 0.0f);

	const float FittedTall = Screen->CurrentZoom();
	if (!TestTrue(TEXT("the tree was fitted to something"), FittedTall > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("and it recorded the panel it was fitted against"),
			  Screen->FittedAgainstSize(), Tall);

	// A TICK WITH NOTHING CHANGED CHANGES NOTHING, which is the half that says
	// this is a comparison rather than a fit on every frame.
	Screen->NativeTick(Nothing, 0.0f);
	TestEqual(TEXT("a tick with the same panel leaves the zoom alone"),
			  Screen->CurrentZoom(), FittedTall, 0.0001f);

	// AND A PLAYER'S OWN ZOOM SURVIVES A TICK, which is what would be thrown
	// away by a build that simply refitted every frame. This is the sharpest
	// half of the test: everything else here would pass against that build.
	Screen->ZoomBy(2.0f);
	const float PlayersZoom = Screen->CurrentZoom();
	if (!TestTrue(TEXT("zooming in really changed the zoom"),
				  !FMath::IsNearlyEqual(PlayersZoom, FittedTall, 0.0001f)))
	{
		return false;
	}
	Screen->NativeTick(Nothing, 0.0f);
	TestEqual(TEXT("and a tick leaves the player's zoom alone"),
			  Screen->CurrentZoom(), PlayersZoom, 0.0001f);

	// AND A SHORTER PANEL IS FITTED AGAIN. Half the height, which is roughly
	// what seven lines of capstone option text took.
	const FVector2D Short(1600.0, 400.0);
	Screen->SetPanelSizeForTests(Short);
	Screen->NativeTick(Nothing, 0.0f);

	TestEqual(TEXT("the shorter panel was recorded"),
			  Screen->FittedAgainstSize(), Short);
	TestTrue(*FString::Printf(
				 TEXT("and the tree was scaled down to fit it: %.4f then %.4f"),
				 FittedTall, Screen->CurrentZoom()),
			 Screen->CurrentZoom() < FittedTall);

	// AND GROWING BACK RESTORES THE FIT, so this is not a one-way shrink.
	Screen->SetPanelSizeForTests(Tall);
	Screen->NativeTick(Nothing, 0.0f);
	TestEqual(TEXT("and the taller panel fits as it did at first"),
			  Screen->CurrentZoom(), FittedTall, 0.0001f);

	return true;
}
#endif // WITH_AUTOMATION_TESTS
