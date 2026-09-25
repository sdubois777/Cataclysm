// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
// For the map from a stat name to the attribute it drives. Issue #954.
#include "Character/CataclysmPlayerClassStats.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For the stat Cast from Ward's row carries. Issue #1515.
#include "AbilitySystem/CataclysmGameplayAbility.h"
// For the swing time Thirst for Pain shortens. Issue #962.
#include "AbilitySystem/CataclysmBasicAttack.h"
// For resolving a real hit against a real character, and the two damage-taken
// stat names. Issue #1026.
#include "AbilitySystem/CataclysmDamageCalculation.h"
// For the bucket and the condition a modifier carries, which many of the tests
// below read off an authored row.
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmContagion.h"
// For putting health back the way the game itself does, rather than reading the
// rate off a gameplay attribute a scaled bonus never reaches. Issue #1038.
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmChorus.h"
#include "AbilitySystem/CataclysmFollowThrough.h"
#include "AbilitySystem/CataclysmSecondSelf.h"
#include "AbilitySystem/CataclysmShoulderThrough.h"
// For the conversion window The Breaking Point lengthens. Issue #1025.
#include "AbilitySystem/CataclysmDamageConversion.h"
// For the debuffs the five new nodes read. Issue #962.
#include "AbilitySystem/CataclysmDebuffs.h"
// For the three Fervour rate stat names and the function that reads one back
// through the stat pipeline. Issue #978.
#include "AbilitySystem/CataclysmFervour.h"
// For putting a real bleed on a real character. Issue #962.
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
// For asking the game's own reader how far a character's retaliation reaches
// and whether it leeches. Issues #1047 and #1048.
#include "AbilitySystem/CataclysmRetaliation.h"
// For the nova a character at very low health releases. Issue #1050.
#include "AbilitySystem/CataclysmNova.h"
// For the flag saying a character's skills cost no health. Issue #1051.
#include "AbilitySystem/CataclysmSkillTemplate.h"
// For Summon Imp and the two stats Press-Ganged and Rekindled are read by.
// Issue #1515.
#include "AbilitySystem/CataclysmSkillTemplates.h"
// For the weapon skill table, which says which damage types a weapon type can
// carry and so which creation choices are legal. Issue #1055.
#include "AbilitySystem/CataclysmWeaponSkills.h"
// For how long a lasting harmful effect on the character runs. Issue #1033.
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
// For an enemy with no defences, to land a real character's attack on.
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
// For a minion dying beside its summoner, which Fed by the Fallen must not
// count, and the ability system that kills it. Issue #1515.
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmPassiveTreeLayout.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Interface/CataclysmPassiveTreeWidget.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
// For the item base table, which says which weapon types a character may begin
// holding. Issue #1055.
#include "Items/CataclysmItem.h"
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
 * WHAT A SPENT POINT IS WORTH IS COVERED FOR THE NODES THAT HAVE A NUMBER, and
 * for no others. `game/Data/PassiveEffects.csv` gives a stat, a bucket and a
 * value per point for 78 of the 441 nodes, in 130 rows; the remaining 363 say
 * what they do in a sentence written for a player and carry no number anywhere a
 * machine can read. Issues #936 and #939.
 *
 * THIS PARAGRAPH SAID "26 OF THE 293" AND BOTH HALVES WERE WRONG BY 2026-09-07.
 * The 26 went stale on its own as authoring continued after it was written. The
 * 293 went stale when issue #950 added the Ravager and Ritualist trees, 74 nodes
 * each. The numerator did not move with them, because neither tree has an
 * authored effect row yet; that is issue #1463. So the share that does something
 * fell without anything regressing, which is the reading this paragraph exists
 * to give.
 *
 * SEVEN OF THE 130 ROWS NAME A REQUIRED TAG, and whether each tag reaches a real
 * character is a separate question. `ATagScopedNodeGrantsNothingYet` and issue
 * #943 record one that does not. THAT WAS THE ONLY TAGGED ROW WHEN #943 WAS
 * WRITTEN AND THERE ARE SEVEN NOW, so the other six have not been checked here
 * and this comment does not claim they have. This sentence previously read "Of
 * the 26, the one that names a required tag reaches nobody", which was stale in
 * the count and in the "the one".
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
	"Cataclysm.Passives.TheGeneratedTablesHoldEveryTreeTheDesignHas",
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

	// SIX SINCE 2026-09-07, issue #950, which added the Ravager and the Ritualist.
	// It was four, and the test was named for the number until then. THE NAME NO
	// LONGER CARRIES THE COUNT, because it will rise again: issue #24 covers the
	// other eighteen trees, and a name that has to be rewritten every time a tree
	// lands is a name that will eventually disagree with the assertion under it.
	//
	// PINNED EXACTLY RATHER THAN AS A FLOOR. A tree that the generator silently
	// dropped would leave a class with nothing to spend a point in, and the
	// characters that start on it -- the Ravager is the class every new character
	// starts on -- would have no tree of their own. That is worth a deliberate
	// failure whenever the count moves.
	const TArray<FString> Trees = UCataclysmPassiveTree::TreeNames(NodeTable);
	TestEqual(TEXT("every class tree the design has"), Trees.Num(), 6);
	TestTrue(TEXT("Berserker"), Trees.Contains(TEXT("Berserker")));
	TestTrue(TEXT("Bulwark"), Trees.Contains(TEXT("Bulwark")));
	TestTrue(TEXT("Masochist"), Trees.Contains(TEXT("Masochist")));
	TestTrue(TEXT("Saboteur"), Trees.Contains(TEXT("Saboteur")));
	TestTrue(TEXT("Ravager"), Trees.Contains(TEXT("Ravager")));
	TestTrue(TEXT("Ritualist"), Trees.Contains(TEXT("Ritualist")));

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
			"Name,Node,Stat,ValueKind,ValuePerPoint,RequiredTags,Condition,ConditionValue,Scale,ScaleStep,Option,ReachMetres\r\n"
			// A plain increase on a node that holds five points, so the
			// multiplication by the points held is visible rather than assumed.
			"Ravager_mid#1,Ravager_mid,armor,increased,3.0,,,0,,0,0,-1\r\n"
			// A more multiplier, which is the other bucket a passive may use.
			"Ravager_side#1,Ravager_side,damage_reduction,more,1.5,,,0,,0,0,-1\r\n"
			// AND A SECOND STAT ON THAT SAME NODE. Issue #953. The Masochist's
			// starting node grants three Fervour rates at once and two other
			// nodes grant a health increase and an armour increase together, so
			// one row per node is a shape the design does not fit.
			"Ravager_side#2,Ravager_side,crit_multiplier,increased,7.0,,,0,,0,0,-1\r\n"
			// A scoped one, to prove the tag column reaches the modifier.
			"Ravager_root#1,Ravager_root,area_of_effect,increased,10.0,Type.Trap,,0,,0,0,-1\r\n"
			// AND ONE THAT DEPENDS ON THE CHARACTER'S HEALTH. Issue #959, and
			// it proves the two condition columns reach the modifier.
			"Ravager_low#1,Ravager_low,crit_chance,increased,3.0,,health_at_or_below,20,,0,0,-1\r\n"
			// AND ONE THAT DEPENDS ON A WINDOW AFTER AN EVENT. Issue #962. It is
			// a second row on the SAME node deliberately: a new node would change
			// the rectangle the tree occupies and move an unrelated layout test's
			// answer.
			"Ravager_low#2,Ravager_low,attack_speed,increased,2.0,,seconds_after_health_cost,2,,0,0,-1\r\n"
			// AND ONE WHOSE SIZE GROWS WITH A STATE rather than switching on and
			// off with it. Issue #968. A third row on the same node, for the same
			// reason the second one is.
			"Ravager_low#3,Ravager_low,max_health,increased,2.0,,,0,health_missing,5,0,-1\r\n"
			// AND ONE UNDER THE SECOND KIND OF TIMED WINDOW. Issue #975. The
			// two windows are separate names and separate enumerators, so
			// covering one says nothing at all about the other.
			"Ravager_low#4,Ravager_low,movement_speed,increased,1.0,,seconds_after_foreign_damage,5,,0,0,-1\r\n"
			// AND ONE THAT GROWS WITH WHAT THE CHARACTER OWES. Issue #994. A
			// SECOND scale, and telling it apart from the one above is the
			// point: health missing and health owed are different states of one
			// character, so a build that mapped either name onto either
			// enumerator would pass every check written before this row.
			"Ravager_low#5,Ravager_low,life_leech,increased,1.0,,,0,health_owed,5,0,-1\r\n"
			// AND THREE COUNTS OF STACKS, ONE PER KIND. Issues #1002, #1003 and
			// #1004. All three are here rather than one of them, because the
			// three names must not be interchangeable: each kind is granted by a
			// different event and lasts a different length of time, and a build
			// that mapped two of the names onto one enumerator would hand a node
			// somebody else's stacks with nothing reporting it. One row cannot
			// catch that; three can.
			"Ravager_low#6,Ravager_low,armor,increased,1.0,,,0,momentum_stacks,1,0,-1\r\n"
			"Ravager_low#7,Ravager_low,magic_find,increased,1.0,,,0,bloodlust_stacks,1,0,-1\r\n"
			"Ravager_low#8,Ravager_low,dot_damage,increased,1.0,,,0,carnage_stacks,1,0,-1\r\n"
			// AND A COUNT OF THE DEBUFFS THE CHARACTER IS UNDER. Issue #962. A
			// fourth count beside the three stacks, and its own row for the same
			// argument: a build that mapped this name onto a stack enumerator
			// would count something the character EARNED instead of something
			// being DONE to it, and every check above would still pass.
			"Ravager_low#9,Ravager_low,spell_damage,increased,1.0,,,0,debuffs_carried,1,0,-1\r\n"
			// AND A CONDITION THAT NAMES AN EFFECT RATHER THAN A THRESHOLD.
			// Issue #962. It is the only condition that reads no value, so it is
			// the only one where the value column could be carried across and
			// compared against with nothing reporting it.
			"Ravager_low#10,Ravager_low,evasion,increased,3.0,,while_bleeding,0,,0,0,-1\r\n"
			// AND A THRESHOLD THAT POINTS UPWARDS. Issue #1070. Ceaseless
			// Penance is the only node in the game asking whether health is
			// still HIGH, and the failure if the name goes unrecognised is the
			// worst of the three: the row is left UNCONDITIONAL, so the option
			// would hold a character's debuffs still at every health rather
			// than only above half.
			"Ravager_low#11,Ravager_low,block_chance,flat,1.0,,health_above,50,,0,0,-1\r\n"
			// AND ONE THAT COUNTS THE ENEMIES STANDING NEARBY. Issue #1597. The
			// only row here carrying a `ReachMetres`, and the only one that can
			// show the column travelling from a table row to a modifier. Every
			// other row carries -1, which is what a row that counts no enemies
			// carries, so a build that dropped the column on the way would leave
			// this one indistinguishable from all of them.
			"Ravager_low#12,Ravager_low,retaliation,increased,4.0,,enemies_in_reach_at_least,3,,0,0,4\r\n"
			// And one in the OTHER tree, which a Demonic character cannot reach.
			"Bulwark_root#1,Bulwark_root,armor,increased,50.0,,,0,,0,0,-1\r\n"
			// A CAPSTONE'S THREE OPTIONS, ONE ROW EACH. Issue #1029. Only the
			// option the player chose may apply, and a capstone with no choice
			// made yet grants none of the three.
			//
			// THREE RATHER THAN TWO, AND THE THIRD EARNS ITS PLACE. Two would
			// prove a chosen option applies and an unchosen one does not; only a
			// third says the skip is by option NUMBER rather than by "not the
			// first one". Each grants a different stat so the test can tell which
			// of the three arrived.
			"Ravager_cap#1,Ravager_cap,armor,increased,10.0,,,0,,0,1,-1\r\n"
			"Ravager_cap#2,Ravager_cap,evasion,increased,20.0,,,0,,0,2,-1\r\n"
			"Ravager_cap#3,Ravager_cap,magic_find,increased,30.0,,,0,,0,3,-1\r\n"));

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
	// the six trees except the capstones'. Without this the check could have been
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

	// AND HOW FAR "NEAR" IS MAKES THE TRIP AS A COLUMN OF ITS OWN. Issue
	// #1597. The same node carries `enemies_in_reach_at_least` with a count of
	// 3 and a reach of 4, which is the shape `Unbreaking` uses: "You take 15%
	// less damage while three or more enemies are within 4 metres of you".
	//
	// THREE NUMBERS HAVE TO ARRIVE SEPARATELY -- the value, the count and the
	// radius -- and the radius is the one with nothing to fall back on. A
	// modifier whose reach was dropped on the way carries -1 and counts
	// NOBODY, so the node would grant nothing at all and no log line would
	// mention it.
	const TArray<FCataclysmStatModifier>* Retaliation =
		Modifiers.Find(FName(TEXT("retaliation")));
	if (TestNotNull(TEXT("the node also granted retaliation"), Retaliation)
		&& TestEqual(TEXT("exactly one of it"), Retaliation->Num(), 1))
	{
		TestEqual(TEXT("four per point times eight points"),
				  (*Retaliation)[0].Value, 32.0f);
		TestEqual(TEXT("and it carries the enemies-in-reach condition"),
				  static_cast<int32>((*Retaliation)[0].Condition),
				  static_cast<int32>(
					  ECataclysmStatCondition::EnemiesInReachAtLeast));
		TestEqual(TEXT("asking for the count the table states"),
				  (*Retaliation)[0].ConditionValue, 3.0f);
		TestEqual(TEXT("within the reach the table states"),
				  (*Retaliation)[0].ReachMetres, 4.0f);
	}

	// AND A ROW THAT COUNTS NO ENEMIES STILL CARRIES NO REACH, which is the
	// control on the four lines above. A build that stamped one row's radius
	// onto every modifier it made would pass every one of them and fail this.
	TestEqual(TEXT("a row that counts no enemies carries no reach"),
			  (*Chance)[0].ReachMetres, -1.0f);

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

// ---------------------------------------------------------------------------
// The one table of condition names, read by the passive tree. Issue #1581.
//
// A TABLE OF ONE ROW PER CASE, RATHER THAN MORE ROWS IN THE FIXTURE ABOVE. That
// fixture is read by a dozen tests which count modifiers and read values off
// named stats, so a row added to it for one case moves what another case
// measures. A table holding only the row under test cannot.
// ---------------------------------------------------------------------------

namespace CataclysmPassiveConditionTest
{
	/** One effect row on `Ravager_low`, the fixture node that holds 8 points. */
	UDataTable* MakeOneRow(FAutomationTestBase& Test, const TCHAR* Stat,
						   const TCHAR* Condition, const TCHAR* Value)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmPassiveEffectRow::StaticStruct();

		const TArray<FString> Problems = Table->CreateTableFromCSVString(
			FString::Printf(
				TEXT("Name,Node,Stat,ValueKind,ValuePerPoint,RequiredTags,")
				TEXT("Condition,ConditionValue,Scale,ScaleStep,Option,ReachMetres\r\n")
				TEXT("Ravager_low#1,Ravager_low,%s,increased,3.0,,%s,%s,,0,0,-1\r\n"),
				Stat, Condition, Value));

		for (const FString& Problem : Problems)
		{
			Test.AddError(Problem);
		}
		return Problems.Num() == 0 ? Table : nullptr;
	}

	/**
	 * The ten predicates that compare nothing, written out here rather than
	 * asked of the function under test.
	 *
	 * DELIBERATELY A SECOND COPY OF THAT LIST. Asking
	 * `UCataclysmStatPipeline::ConditionTakesAValue` what to expect would make
	 * the test agree with the code by construction and pass whatever the code
	 * said. Writing the ten out means a change to either one has to be made
	 * in both places on purpose.
	 */
	bool ComparesNothing(const FString& Name)
	{
		return Name == TEXT("while_bleeding")
			|| Name == TEXT("class_resource_at_maximum")
			|| Name == TEXT("hit_is_melee_attack")
			|| Name == TEXT("hit_is_ranged_attack")
			|| Name == TEXT("hit_is_spell")
			|| Name == TEXT("opponent_is_boss")
			|| Name == TEXT("opponent_is_staggered")
			// ISSUE #45 AGAIN, AND THE MIRROR OF THE NAME ABOVE. One asks whether
			// whoever threw the blow is staggered and the other whether the
			// character being hit is, and they read separate fields on purpose.
			|| Name == TEXT("target_is_staggered")
			// ISSUE #1686. Whether the attacker is under crowd control, beside
			// the staggered pair; a state, so no value.
			|| Name == TEXT("opponent_is_crowd_controlled")
			// ISSUE #1686 AGAIN, ruled on #1697: a melee hit while moving. A
			// pair of states, so no value.
			|| Name == TEXT("melee_hit_while_moving")
			// ISSUE #1815, THE MISSING HALF OF THE BOSS PAIR AND ITS NEGATION.
			// `opponent_is_boss` above answers "a boss hit me"; these answer "I am
			// hitting a boss" and "I am not". Each names a state rather than a
			// threshold, so none of the three compares a value.
			|| Name == TEXT("target_is_boss")
			|| Name == TEXT("target_is_not_boss")
			// ISSUE #41'S SLICE 2. The other three movement conditions compare
			// a number, so they are deliberately absent.
			|| Name == TEXT("while_moving")
			|| Name == TEXT("while_stationary")
			// ISSUE #1515, THE THREE THAT READ AN AILMENT ON THE OTHER
			// CHARACTER. Each names its ailment, so there is no threshold for a
			// number to be. The third reads the other end of the blow, which
			// changes nothing about whether it compares a value.
			//
			// AND A FOURTH UNDER ISSUE #1642, for an enchantment rather than a
			// node: "enemies carrying a void splinter take increased damage from
			// you". It names its ailment like the three above and so compares
			// nothing either. `docs/DECISIONS.md` records why a fourth name was
			// taken rather than the parameterised column the rule points at.
			|| Name == TEXT("target_carries_void_splinter")
			// AND ONE NAMING THE SAME TWO AILMENTS FROM THE ATTACKER'S END.
			// Issue #1718, Spreading Hurt. It asks whether this character can
			// apply either at all rather than what the target carries, and it
			// compares no number for the same reason: it names its ailments.
			|| Name == TEXT("can_cripple_or_weaken")
			|| Name == TEXT("target_carries_cripple")
			|| Name == TEXT("target_carries_cripple_and_weaken")
			|| Name == TEXT("opponent_carries_weaken")
			// ISSUE #1515, THE SECOND POOL ASKED WHETHER IT IS FULL. "Full"
			// names the top of the bar rather than a number, the same as
			// `class_resource_at_maximum` above.
			|| Name == TEXT("energy_shield_at_maximum")
			// ISSUE #1981, THE SAME POOL ASKED A DIFFERENT QUESTION. "Active"
			// means held above zero rather than full, so it is not the name
			// above under another spelling: one asks whether the bar is full and
			// this asks whether any of it is left. Neither states a number.
			|| Name == TEXT("energy_shield_above_zero")
			// ISSUE #1815, THE FIRST HIT AGAINST EACH ENEMY. "First" names the
			// state of the target's record of who has struck it, not a number.
			|| Name == TEXT("target_not_yet_struck_by_you")
			|| Name == TEXT("target_not_yet_crit_by_you")
			// ISSUE #1815, THE COMBAT STATE. One meaning of "in combat" for the
			// whole game, so no row states a window of its own.
			|| Name == TEXT("in_combat")
			|| Name == TEXT("out_of_combat")
			// ISSUE #1815, THE TARGET'S DEBUFFS. Whether it carries any, and
			// whether one is a damage over time; neither is a number.
			|| Name == TEXT("target_carries_any_debuff")
			|| Name == TEXT("target_carries_a_dot")
			// ISSUE #1515, TWO HANDS. It names the weapon in hand rather than a
			// number, so it compares nothing.
			|| Name == TEXT("wielding_two_handed_weapon");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveUnknownConditionTest,
	"Cataclysm.Passives.APassiveRowNamingAConditionThisBuildCannotJudgeGrantsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A condition name the build cannot judge grants nothing at all. Issue #1581.
 *
 * WHAT IT USED TO DO, AND WHY IT WAS THE WORST AVAILABLE ANSWER. The name was
 * logged and the modifier was then applied with NO condition, so a row nobody
 * could judge became a bonus that held all the time. That is silent, and it is
 * in the player's favour, which is the combination that survives playtesting.
 *
 * THE CONTROL IS ASSERTED FIRST AND IT IS NOT DECORATION. Without it this test
 * would pass just as well against a build that granted nothing for EVERY row,
 * which is the obvious way to break the fix.
 */
bool FCataclysmPassiveUnknownConditionTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmPassiveConditionTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	if (!NodeTable)
	{
		return false;
	}

	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Ravager_low")), 8);

	// THE CONTROL. A name this build does know grants its modifier.
	UDataTable* Known = MakeOneRow(*this, TEXT("crit_chance"),
								   TEXT("health_at_or_below"), TEXT("20"));
	if (!Known)
	{
		return false;
	}

	TMap<FName, TArray<FCataclysmStatModifier>> FromKnown;
	TestEqual(TEXT("a row this build can judge adds one modifier"),
			  UCataclysmPassiveTree::AccumulateInto(FromKnown, Allocation,
													NodeTable, Known, Demonic),
			  1);
	TestTrue(TEXT("and it is the stat the row names"),
			 FromKnown.Contains(FName(TEXT("crit_chance"))));

	// AND THE ROW NOBODY CAN JUDGE. The name is written to be one nothing will
	// ever add: the point is that it is absent from the shared table.
	UDataTable* Unknown = MakeOneRow(*this, TEXT("crit_chance"),
									 TEXT("no_such_condition_exists"),
									 TEXT("20"));
	if (!Unknown)
	{
		return false;
	}

	TMap<FName, TArray<FCataclysmStatModifier>> FromUnknown;
	TestEqual(TEXT("a row naming a condition this build cannot judge adds "
				   "nothing"),
			  UCataclysmPassiveTree::AccumulateInto(FromUnknown, Allocation,
													NodeTable, Unknown,
													Demonic),
			  0);
	TestFalse(TEXT("and the stat it named was not granted"),
			  FromUnknown.Contains(FName(TEXT("crit_chance"))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveSharedConditionTableTest,
	"Cataclysm.Passives.AConditionOnlyTheSharedTableKnewNowReachesAPassiveModifier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `hit_is_spell` reaches a passive modifier. Issue #1581.
 *
 * THE NAME IS CHOSEN BECAUSE IT IS THE EXACT GAP. Issue #1578 added four names
 * to `CONDITIONS` in the generator and to the stat pipeline's table, and not to
 * the chain this function used to carry. Both tests that hold the names
 * together read the generator and the pipeline, so all of them passed while a
 * passive row naming one of the four would have been granted with no condition.
 *
 * AND IT CARRIES NO VALUE, which is the second half of the same row. This is
 * one of the ten predicates that compare nothing, so a build copying the value
 * across would hand it a threshold it has no meaning for.
 */
bool FCataclysmPassiveSharedConditionTableTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmPassiveConditionTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	// A VALUE IS WRITTEN INTO THE ROW ON PURPOSE. The generator refuses to write
	// one beside a predicate that compares nothing, so the only way to find out
	// whether this reader would copy it is to hand it one.
	UDataTable* EffectTable = MakeOneRow(*this, TEXT("spell_damage"),
										 TEXT("hit_is_spell"), TEXT("7"));
	if (!NodeTable || !EffectTable)
	{
		return false;
	}

	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Ravager_low")), 8);

	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											Demonic);

	const TArray<FCataclysmStatModifier>* Spell =
		Modifiers.Find(FName(TEXT("spell_damage")));
	if (!TestNotNull(TEXT("the node granted spell damage"), Spell)
		|| !TestEqual(TEXT("exactly one"), Spell->Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("three per point times eight points"), (*Spell)[0].Value,
			  24.0f);
	TestEqual(TEXT("and it carries the spell condition"),
			  static_cast<int32>((*Spell)[0].Condition),
			  static_cast<int32>(ECataclysmStatCondition::HitIsSpell));
	TestTrue(TEXT("and it is not left unconditional"),
			 (*Spell)[0].Condition != ECataclysmStatCondition::Always);
	TestEqual(TEXT("carrying no value, because it compares nothing"),
			  (*Spell)[0].ConditionValue, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveDistanceConditionTest,
	"Cataclysm.Passives.ADistanceConditionReachesAPassiveModifierCarryingItsThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `attacker_beyond_metres` reaches a modifier AND keeps its number.
 *
 * THE OPPOSITE ASSERTION FROM THE TEST ABOVE, and that is why it is its own
 * test. `hit_is_spell` must come out with NO value, because it compares nothing;
 * this one must come out carrying 6, because the whole predicate is a
 * comparison. A build that copied the value for every condition would pass the
 * first and a build that copied it for none would pass neither, so the pair
 * catches both mistakes.
 *
 * SIX METRES IS THE NODE'S OWN THRESHOLD. Standing Apart, the Ritualist's
 * 100-point capstone third option, reads "You take 25% less damage from enemies
 * more than 6 metres away from you", and `game/Data/PassiveEffects.csv` carries
 * exactly that row.
 */
bool FCataclysmPassiveDistanceConditionTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmPassiveConditionTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	UDataTable* EffectTable = MakeOneRow(*this, TEXT("damage_taken"),
										 TEXT("attacker_beyond_metres"), TEXT("6"));
	if (!NodeTable || !EffectTable)
	{
		return false;
	}

	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Ravager_low")), 8);

	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											Demonic);

	const TArray<FCataclysmStatModifier>* Taken =
		Modifiers.Find(FName(TEXT("damage_taken")));
	if (!TestNotNull(TEXT("the node granted damage taken"), Taken)
		|| !TestEqual(TEXT("exactly one"), Taken->Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("and it carries the distance condition"),
			  static_cast<int32>((*Taken)[0].Condition),
			  static_cast<int32>(ECataclysmStatCondition::OpponentBeyondMetres));
	TestTrue(TEXT("and it is not left unconditional"),
			 (*Taken)[0].Condition != ECataclysmStatCondition::Always);
	TestEqual(TEXT("carrying the threshold the row states, because it compares one"),
			  (*Taken)[0].ConditionValue, 6.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveEveryConditionNameTest,
	"Cataclysm.Passives.EveryConditionNameASheetMayWriteReachesAPassiveModifier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every name a data sheet may write reaches a passive modifier. Issue #1581.
 *
 * THE TEST THAT KEEPS THE LISTS FROM PARTING AGAIN, and it is the reason
 * `AllConditionNames` exists. Naming the conditions by hand here would pass for
 * ever after somebody adds a thirteenth, which is exactly how the passive tree
 * came to be four names behind without any test noticing.
 *
 * THE NAMES COME FROM THE ENGINE'S OWN TABLE, and `CONDITIONS` in
 * `tools/generate_datatables.py` is held equal to that table by
 * `tools/tests/test_stat_condition_names_match_the_engine.py`. So the two
 * together say every name the generator may write is one a passive row can
 * carry.
 *
 * AND THE VALUE RULE FOR EACH, which is the other half of reading a condition.
 * Every row here is handed a value of 7; the ten predicates that compare
 * nothing must come out with none, and the rest must carry it.
 */
bool FCataclysmPassiveEveryConditionNameTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmPassiveConditionTest;

	UDataTable* NodeTable = MakeNodeTable(*this);
	if (!NodeTable)
	{
		return false;
	}

	TArray<FString> Names;
	UCataclysmStatPipeline::AllConditionNames(Names);

	// WITHOUT THIS, AN EMPTY LIST WOULD MAKE THE LOOP BELOW ASSERT NOTHING and
	// the test would pass having checked no name at all.
	if (!TestTrue(TEXT("the shared table names at least twelve conditions"),
				  Names.Num() >= 12))
	{
		return false;
	}

	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Ravager_low")), 8);

	for (const FString& Name : Names)
	{
		// THE NAME RESOLVES THROUGH THE SHARED READER AT ALL.
		ECataclysmStatCondition Resolved = ECataclysmStatCondition::Always;
		if (!TestTrue(*FString::Printf(TEXT("%s resolves through ConditionNamed"),
									   *Name),
					  UCataclysmStatPipeline::ConditionNamed(Name, Resolved)))
		{
			continue;
		}

		UDataTable* EffectTable = MakeOneRow(*this, TEXT("armor"), *Name,
											 TEXT("7"));
		if (!EffectTable)
		{
			return false;
		}

		const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
			UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable,
												EffectTable, Demonic);

		const TArray<FCataclysmStatModifier>* Armour =
			Modifiers.Find(FName(TEXT("armor")));
		if (!TestNotNull(*FString::Printf(TEXT("%s granted its modifier"), *Name),
						 Armour)
			|| !TestEqual(*FString::Printf(TEXT("%s granted exactly one"), *Name),
						  Armour->Num(), 1))
		{
			continue;
		}

		TestEqual(*FString::Printf(TEXT("%s reached the modifier as itself"),
								   *Name),
				  static_cast<int32>((*Armour)[0].Condition),
				  static_cast<int32>(Resolved));
		TestTrue(*FString::Printf(TEXT("%s did not come out unconditional"),
								  *Name),
				 (*Armour)[0].Condition != ECataclysmStatCondition::Always);

		const float Expected = ComparesNothing(Name) ? 0.0f : 7.0f;
		TestEqual(*FString::Printf(TEXT("%s carries the value it should"), *Name),
				  (*Armour)[0].ConditionValue, Expected);
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

	// AND THE STATS THAT DELIBERATELY HAVE NO ATTRIBUTE. Issue #1733.
	//
	// THE REASON ABOVE STOPPED BEING TRUE WHEN #1724 MERGED, and this is where
	// it shows. `ApplyTo` no longer loops only over `StatToAttribute`: a third
	// pass loops over `StatsWithNoAttribute()` and RECORDS those stats without
	// writing any attribute, so a passive row naming one of them is not dropped.
	// Bespoke code reads them directly: the minion stats' increases in
	// `UCataclysmCommand::AttackIntervalScaleFor`,
	// `ACataclysmMinion::AttackTarget` and `ACataclysmMinion::Spawn`, and, since
	// issue #1791, whether `mana_on_hit` is removed in
	// `UCataclysmSkillTemplate::ApplyManaOnHit`.
	//
	// READ FROM THE ENGINE'S OWN LIST RATHER THAN RESTATED HERE, so this test
	// and the code it checks cannot disagree about which stats are exempt. Three
	// places needed these names and each had its own copy before #1733.
	//
	// AN EXEMPTION IS A PROMISE AND THIS TEST DOES NOT KEEP IT. All it does is
	// stop refusing them. That every name on that list is really read by code is
	// held by
	// `Cataclysm.StatExemption.EveryStatWithNoAttributeIsActuallyRead`,
	// which is the half issue #1025 was missing.
	const TArray<FString>& Exempt =
		UCataclysmPlayerClassStats::StatsWithNoAttribute();

	int32 Checked = 0;
	int32 Exempted = 0;
	for (const TPair<FName, uint8*>& Row : EffectTable->GetRowMap())
	{
		const auto* Effect =
			reinterpret_cast<const FCataclysmPassiveEffectRow*>(Row.Value);
		if (!Effect || Effect->Stat.IsEmpty())
		{
			continue;
		}

		++Checked;
		if (Exempt.Contains(Effect->Stat))
		{
			++Exempted;
			continue;
		}

		TestTrue(*FString::Printf(
					 TEXT("%s grants '%s', which has an attribute behind it"),
					 *Row.Key.ToString(), *Effect->Stat),
				 Attributes.Contains(Effect->Stat));
	}

	// AND SAY HOW MANY TOOK THE EXEMPTION, so a build where the exemption
	// swallowed everything is visible rather than silently green. A reader who
	// sees this climb without the list growing has found a misspelling that
	// happens to match an exempt name.
	AddInfo(FString::Printf(
		TEXT("%d passive rows checked, %d of them exempt from needing an "
			 "attribute"),
		Checked, Exempted));

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

namespace CataclysmPassiveTest
{
	/**
	 * An enemy to land a real character's attack on, with nothing that mitigates.
	 *
	 * ITS DEFENCES ARE REMOVED AFTER IT HAS BEGUN PLAY, because an enemy applies
	 * its archetype's armour, resistance and energy shield then, and the test
	 * using it measures what the character sent rather than what the enemy
	 * stopped. `CataclysmConditionalDamageTests.cpp` strips the same things.
	 */
	ACataclysmEnemyCharacter* SpawnUndefendedEnemy(UWorld* World)
	{
		ACataclysmEnemyCharacter* Enemy =
			World->SpawnActor<ACataclysmEnemyCharacter>(
				FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		UAbilitySystemComponent* AbilitySystem =
			Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
		if (!AbilitySystem)
		{
			return nullptr;
		}

		using Combat = UCataclysmCombatAttributeSet;
		using Vital = UCataclysmVitalAttributeSet;
		AbilitySystem->SetNumericAttributeBase(Combat::GetArmorAttribute(), 0.0f);
		AbilitySystem->SetNumericAttributeBase(
			Combat::GetDamageReductionAttribute(), 0.0f);
		AbilitySystem->SetNumericAttributeBase(Combat::GetEvasionAttribute(), 0.0f);
		AbilitySystem->SetNumericAttributeBase(
			Combat::GetBlockChanceAttribute(), 0.0f);
		AbilitySystem->SetNumericAttributeBase(
			UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		AbilitySystem->SetNumericAttributeBase(
			Vital::GetMaxEnergyShieldAttribute(), 0.0f);
		AbilitySystem->SetNumericAttributeBase(
			Vital::GetEnergyShieldAttribute(), 0.0f);

		// DEEP ENOUGH THAT NO HIT HERE KILLS IT, so every hit is measured in full
		// rather than cut short at the health it has left.
		//
		// A FLOAT NEAR 1,000,000 STEPS IN UNITS OF 0.0625, so a blow measured as the
		// difference of two readings of this pool is quantised to that: an expected
		// figure that is not a multiple of 0.0625 (a -40% blow of 220.5 is 132.3, read
		// as 132.3125) misses a 0.01 tolerance however correct the engine is. Pick
		// figures that land on the grid, or use a smaller pool the way
		// CataclysmMinionGearTests.cpp does. Issue #1728.
		AbilitySystem->SetNumericAttributeBase(
			Vital::GetMaxHealthAttribute(), 1'000'000.0f);
		AbilitySystem->SetNumericAttributeBase(
			Vital::GetHealthAttribute(), 1'000'000.0f);
		return Enemy;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveCommunionOfPainAttackOnARealCharacterTest,
	"Cataclysm.Passives.CommunionOfPainsMoreDamageReachesARealCharactersAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Communion of Pain's "you deal 20% more damage", delivered by a real attack.
 *
 * `CommunionOfPainCutsBothWaysForARealCharacterAtFullFervour` above reads the
 * damage through `StatForSkill`, which runs the stat pipeline over every
 * modifier and so reports the fifth whether or not a hit ever carries it. A hit
 * worked out only the increases again, not the "more" multipliers, so that test
 * passed while an attack at full Fervour dealt exactly what one a point short of
 * full did. This one lands the attack.
 *
 * ONE POINT SHORT OF FULL IS THE COMPARISON, as in the test above, because it is
 * the closest state in which the condition does not hold.
 */
bool FCataclysmPassiveCommunionOfPainAttackOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Resource = UCataclysmClassResourceAttributeSet;

	// A CRITICAL STRIKE ON ONE OF THE TWO ATTACKS AND NOT THE OTHER would read as
	// the node being worth half again what it says.
	CataclysmTestWorld::SilenceCriticalStrikes();

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

	// COMMUNION OF PAIN'S ONE POINT, through the real allocation and the real
	// effect table, so the rows the workbook authored are what is measured.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Masochist_keystone_spine_001")), 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	ACataclysmEnemyCharacter* Enemy = SpawnUndefendedEnemy(World);
	if (!TestNotNull(TEXT("an enemy to attack"), Enemy))
	{
		return false;
	}
	UAbilitySystemComponent* EnemySystem = Enemy->GetAbilitySystemComponent();
	const FGameplayAttribute Health =
		UCataclysmVitalAttributeSet::GetHealthAttribute();

	// ONE ATTACK AT THE CHARACTER'S FULL WEAPON DAMAGE, and what it took off.
	const auto AttackLands = [&]
	{
		const float Before = EnemySystem->GetNumericAttribute(Health);
		UCataclysmSkillEffects::ApplyHit(Character, Enemy,
										 /*DamagePercent=*/100.0f,
										 FGameplayTagContainer());
		return Before - EnemySystem->GetNumericAttribute(Health);
	};

	const float Maximum =
		AbilitySystem->GetNumericAttribute(Resource::GetMaxClassResourceAttribute());
	if (!TestTrue(TEXT("the class resource has a maximum above one"),
				  Maximum > 1.0f))
	{
		return false;
	}

	AbilitySystem->SetNumericAttributeBase(Resource::GetClassResourceAttribute(),
										   Maximum - 1.0f);
	const float ShortOfFull = AttackLands();

	AbilitySystem->SetNumericAttributeBase(Resource::GetClassResourceAttribute(),
										   Maximum);
	const float AtFull = AttackLands();

	if (!TestTrue(FString::Printf(TEXT("both attacks landed (%.1f, %.1f)"),
								  ShortOfFull, AtFull),
				  ShortOfFull > 0.0f && AtFull > 0.0f))
	{
		return false;
	}

	TestEqual(FString::Printf(
		TEXT("at full Fervour the attack deals a fifth more, and dealt %.3f times"),
		AtFull / ShortOfFull),
		AtFull / ShortOfFull, 1.20f, 0.001f);

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
 * other health threshold in all six trees is worded "at or below" and takes
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
// A changed damage type reaching the character with nothing else touched
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveDamageTypeChangeRefreshesTest,
	"Cataclysm.Passives.ChangingOnlyTheDamageTypeReRunsTheStatLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A changed damage type has to reach the character by itself. Issue #1055.
 *
 * WHAT WENT WRONG. A character's damage type decides which passive trees apply
 * to it at all: points in a tree no equipped weapon reaches stay spent and grant
 * nothing, which is the project owner's decision of 2026-08-25.
 * `UCataclysmEquipmentComponent::RefreshAttributes` enforces that by passing
 * `GetChosenDamageType()` to `UCataclysmPassiveTree::AccumulateInto`. Nothing
 * re-ran the stat line when the damage type changed, so a character that
 * reopened the creation screen and changed only its damage type kept the old
 * type's trees applied to its stats while the passive tree screen showed the new
 * type's -- the screen and the character disagreed until something else happened
 * to refresh.
 *
 * WHY CHANGING ONLY THE DAMAGE TYPE IS THE CASE THAT MATTERS. Changing the
 * weapon type hides the defect. `ACataclysmPlayerCharacter::ApplyCreationChoice`
 * puts the newly chosen weapon on, that broadcasts `EquipmentChanged`, and the
 * refresh then happens by accident. `ApplyCreationChoice` returns early when the
 * character already holds a weapon of the chosen type, and its own comment names
 * the case: "a choice that only changed the damage type". So the weapon type is
 * held at Greataxe throughout below and only the damage type moves.
 *
 * THIS TEST MUST NOT CALL `RefreshAttributes`, for the reason
 * `SpendingAPointRaisesMaximumHealthWithNothingElseTouched` gives for issue
 * #1054: that call is exactly the step the game was missing, so making it here
 * would put the defect back out of reach. Nothing below touches equipment, an
 * attribute point or a level either.
 *
 * BOTH WRITERS, BECAUSE THEY ARE TWO FUNCTIONS AND NOT ONE.
 * `ACataclysmPlayerState::ChooseAtCreation` is what the creation screen and the
 * `Cataclysm.ChooseAtCreation` console command call; `SetCreationChoice` is the
 * save-restore path, which takes a record without judging it. A repair placed in
 * only one of them fails one half or the other, so the first change below goes
 * through one and the change back through the other.
 *
 * GREATAXE CARRIES BOTH DEMONIC AND WAR, which is what makes the pair legal in
 * both directions -- the Demonic Greataxe rows and War's Annihilator are both in
 * `game/Data/WeaponSkills.csv`. Demonic reaches the Masochist tree and War does
 * not; War reaches Bulwark, Berserker and Saboteur.
 *
 * READ AS A SUM AND NOT AS A MULTIPLIER, for the reason `IncreasesOn` explains.
 * Ten points worth 1% each add 10 to whatever sum the stat already carries.
 */
bool FCataclysmPassiveDamageTypeChangeRefreshesTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	// THE MASOCHIST CLASS LINE, BECAUSE THE TREE BEING SWITCHED OFF IS THE
	// MASOCHIST'S. Which class a character is decides which base every stat
	// stands on, and an increase on a stat whose base is zero multiplies
	// nothing. Issue #980.
	FScopedPlayerClass AsMasochist(TEXT("Masochist"));

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	if (!TestNotNull(TEXT("a possessed player character"), Character))
	{
		return false;
	}

	ACataclysmPlayerState* State =
		Character->GetPlayerState<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!State || !AbilitySystem)
	{
		AddError(TEXT("The spawned character is missing a component."));
		return false;
	}

	// THE REAL TABLES, because the whole point is the end-to-end path.
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	const UDataTable* Skills = UCataclysmWeaponSkills::LoadGeneratedTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("the passive effect table loads"), EffectTable)
		|| !TestNotNull(TEXT("the weapon skill table loads"), Skills)
		|| !TestNotNull(TEXT("the item base table loads"), Bases))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	// WHAT THE SHEET SAYS PAIN TOLERANCE IS WORTH, READ OUT OF THE REAL TABLE.
	// The root holds one point and opens Pain Tolerance, which grants increased
	// maximum health and increased armour.
	const FName Root(TEXT("Masochist_basic_spine_000"));
	const FName PainTolerance(TEXT("Masochist_basic_spine_001"));

	float HealthPerPoint = 0.0f;
	float ArmourPerPoint = 0.0f;
	for (const FCataclysmPassiveEffectRow* Row :
		 UCataclysmPassiveTree::EffectsFor(EffectTable, PainTolerance))
	{
		if (Row->Stat == TEXT("max_health"))
		{
			HealthPerPoint = Row->ValuePerPoint;
		}
		else if (Row->Stat == TEXT("armor"))
		{
			ArmourPerPoint = Row->ValuePerPoint;
		}
	}
	if (!TestTrue(TEXT("Pain Tolerance grants increased maximum health"),
				  HealthPerPoint > 0.0f)
		|| !TestTrue(TEXT("and increased armour"), ArmourPerPoint > 0.0f))
	{
		return false;
	}

	// -------------------------------------------------------------------
	// Demonic, with points spent in a tree Demonic reaches
	// -------------------------------------------------------------------

	FString Reason;
	if (!TestTrue(TEXT("a Greataxe carrying Demonic is taken"),
				  State->ChooseAtCreation(Skills, Bases, FName(TEXT("Greataxe")),
										  FName(TEXT("Demonic")), Reason)))
	{
		AddError(Reason);
		return false;
	}

	const float HealthSumBare = IncreasesOn(AbilitySystem, TEXT("max_health"));
	const float ArmourSumBare = IncreasesOn(AbilitySystem, TEXT("armor"));
	const float HealthBare =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	const float ArmourBare =
		AbilitySystem->GetNumericAttribute(Combat::GetArmorAttribute());

	// A REAL STAT LINE UNDERNEATH, NOT THE PLACEHOLDER.
	// `UCataclysmVitalAttributeSet`'s constructor writes 100, and a test that
	// began from the placeholder would be measuring an unpossessed character.
	if (!TestTrue(*FString::Printf(
					  TEXT("maximum health is off the class line, not the "
						   "placeholder 100: %.1f"), HealthBare),
				  HealthBare > 100.0f)
		|| !TestTrue(*FString::Printf(
						 TEXT("and there is armour to increase: %.1f"),
						 ArmourBare),
					 ArmourBare > 0.0f))
	{
		return false;
	}

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

	const float HealthSumSpent = IncreasesOn(AbilitySystem, TEXT("max_health"));
	const float ArmourSumSpent = IncreasesOn(AbilitySystem, TEXT("armor"));
	const float HealthSpent =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());

	// THE POINTS ARE WORTH SOMETHING BEFORE THE DAMAGE TYPE MOVES. Without this
	// the assertions below would compare zero against zero and pass on nothing,
	// which is the shape of a check that cannot fail.
	TestEqual(TEXT("ten points raised the sum of increases on maximum health"),
			  HealthSumSpent, HealthSumBare + HealthPerPoint * Points, 0.01f);
	TestTrue(*FString::Printf(
				 TEXT("and maximum health rose with it, from %.1f to %.1f"),
				 HealthBare, HealthSpent),
			 HealthSpent > HealthBare);

	// THE SECOND STAT ON THE SAME NODE, so a repair that somehow reached only
	// maximum health is caught by the armour assertions further down.
	TestEqual(TEXT("and the sum of increases on armour rose by its own amount"),
			  ArmourSumSpent, ArmourSumBare + ArmourPerPoint * Points, 0.01f);

	// -------------------------------------------------------------------
	// War, chosen through `ChooseAtCreation`, with the weapon type unchanged
	// -------------------------------------------------------------------

	if (!TestTrue(TEXT("the same Greataxe carrying War is taken"),
				  State->ChooseAtCreation(Skills, Bases, FName(TEXT("Greataxe")),
										  FName(TEXT("War")), Reason)))
	{
		AddError(Reason);
		return false;
	}
	TestEqual(TEXT("and the weapon type did not move"),
			  State->GetChosenWeaponType(), FName(TEXT("Greataxe")));

	// NOTHING ELSE IS TOUCHED FROM HERE. No equipment change, no attribute
	// point, no level, and above all no call to `RefreshAttributes`.

	const float HealthSumAsWar = IncreasesOn(AbilitySystem, TEXT("max_health"));
	const float ArmourSumAsWar = IncreasesOn(AbilitySystem, TEXT("armor"));
	const float HealthAsWar =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	const float ArmourAsWar =
		AbilitySystem->GetNumericAttribute(Combat::GetArmorAttribute());

	// THE HEADLINE: a tree War cannot reach stops being worth anything, and the
	// character's own stats say so without anything else being touched.
	TestEqual(TEXT("the Masochist tree's increases came off maximum health"),
			  HealthSumAsWar, HealthSumBare, 0.01f);
	TestEqual(TEXT("and off armour"), ArmourSumAsWar, ArmourSumBare, 0.01f);
	TestEqual(TEXT("so maximum health is back where it started"), HealthAsWar,
			  HealthBare, FMath::Max(HealthBare * 0.001f, 0.01f));
	TestEqual(TEXT("and so is armour"), ArmourAsWar, ArmourBare,
			  FMath::Max(ArmourBare * 0.001f, 0.01f));

	// AND THE POINTS ARE STILL SPENT. Dormant is not refunded, which is the
	// other half of the 2026-08-25 decision.
	TestEqual(TEXT("the eleven points are still spent"),
			  State->GetPassiveAllocation().Total(), Points + 1);
	TestEqual(TEXT("including the ten in the tree that now grants nothing"),
			  State->GetPassiveAllocation().PointsIn(PainTolerance), Points);

	// -------------------------------------------------------------------
	// And back to Demonic through `SetCreationChoice`, the other writer
	// -------------------------------------------------------------------

	// A LOADED RECORD RATHER THAN A CHOICE, which is the save-restore path and a
	// separate function. A repair placed only in `ChooseAtCreation` fails here.
	State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("Demonic")));

	const float HealthSumBack = IncreasesOn(AbilitySystem, TEXT("max_health"));
	const float HealthBack =
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute());

	TestEqual(TEXT("loading Demonic back puts the tree's increases back on"),
			  HealthSumBack, HealthSumSpent, 0.01f);
	TestEqual(TEXT("and maximum health with them"), HealthBack, HealthSpent,
			  FMath::Max(HealthSpent * 0.001f, 0.01f));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFinalPactPerMinionTest,
	"Cataclysm.Passives.TheFinalPactsSecondOptionGrantsItsThreeRowsAndTheOthersOnlyTheirOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_capstone_200` option 2, and the fault no count can see.
 *
 * THE OPTION: "Each minion you have grants you 4% more damage and 4% increased
 * Maximum Energy Shield." Three rows -- "damage" unqualified is attack damage
 * and spell damage in this project, so the damage half is two rows.
 *
 * WHY THIS TEST EXISTS RATHER THAN THE ROW COUNTS. `AUTHORED_OPTIONS` in
 * `tools/tests/test_passive_effects_match_the_node_text.py` counts capstone
 * options that have at least one row. **Writing `Option=1` instead of `Option=2`
 * moves that count identically**: one option filled either way. The row count,
 * the node count and the sentence check are all blind to WHICH option, and these
 * rows carry no condition for the sentence check to compare. **A wrong option
 * number means the player who picks the second choice receives nothing while a
 * different choice pays out twice, and nothing else in the suite would say so.**
 *
 * IT DOES NOT TEST WHETHER THE MINION COUNT WORKS. That scale is exercised by
 * `Cataclysm.Fervour.MinionsHeldGenerateFervourEverySecond` and two setups in
 * `CataclysmCommandTests.cpp`, and re-proving a proven mechanism is the
 * expensive kind of redundancy.
 *
 * THE BUCKETS ARE THE SECOND JOB. The node's sentence says "more damage" and
 * "increased Maximum Energy Shield", which are different words for different
 * arithmetic: one multiplies and one joins the additive sum. **Swapping them
 * produces a plausible number rather than an error**, so the buckets are
 * asserted by name.
 */
bool FCataclysmFinalPactPerMinionTest::RunTest(const FString&)
{
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};
	const FName Capstone(TEXT("Ritualist_capstone_200"));

	// A POINT IN THE CAPSTONE AND NO CHOICE MADE. The point is spent and
	// nothing has been picked, so nothing is granted. This is the baseline the
	// two assertions below are measured against.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Capstone, 1);
	if (!TestEqual(TEXT("no option has been chosen"),
				   Allocation.ChosenOptionIn(Capstone), 0))
	{
		return false;
	}

	const auto ModifiersFor = [&](int32 Option)
	{
		FCataclysmPassiveAllocation Picked;
		Picked.Add(Capstone, 1);
		if (Option > 0)
		{
			Picked.SetChosenOption(Capstone, Option);
		}
		TMap<FName, TArray<FCataclysmStatModifier>> Totals;
		const int32 Added = UCataclysmPassiveTree::AccumulateInto(
			Totals, Picked, NodeTable, EffectTable, Demonic);
		return TPair<int32, TMap<FName, TArray<FCataclysmStatModifier>>>(
			Added, MoveTemp(Totals));
	};

	TestEqual(TEXT("with no option chosen the capstone grants nothing"),
			  ModifiersFor(0).Key, 0);

	// OPTION 2 GRANTS EXACTLY THREE MODIFIERS.
	const auto Second = ModifiersFor(2);
	if (!TestEqual(TEXT("option 2 grants three modifiers"), Second.Key, 3))
	{
		return false;
	}

	// AND THE OTHER TWO OPTIONS GRANT ONLY THEIR OWN ROW. This is the half that
	// catches a wrong option number: if these rows carried Option=1, option 1
	// would grant four and this assertion would fail where the count-based
	// checks would not. Options 1 and 3 granted nothing until 2026-09-25, when
	// A Second Self and Chorus gained one flag row each. Issue #1515.
	const auto GrantsOnly = [&](int32 Option, const TCHAR* OwnStat)
	{
		const auto Other = ModifiersFor(Option);
		TestEqual(*FString::Printf(TEXT("option %d grants one modifier, its own"),
								   Option),
				  Other.Key, 1);
		TestNotNull(*FString::Printf(TEXT("option %d's one modifier is %s"),
									 Option, OwnStat),
					Other.Value.Find(FName(OwnStat)));
		for (const TCHAR* Stat : {TEXT("attack_damage"), TEXT("spell_damage"),
								  TEXT("max_energy_shield")})
		{
			TestNull(*FString::Printf(TEXT("option %d grants none of option 2's %s"),
									  Option, Stat),
					 Other.Value.Find(FName(Stat)));
		}
	};
	GrantsOnly(1, TEXT("minion_held_longest_becomes_your_equal"));
	GrantsOnly(3, TEXT("minions_repeat_your_skills"));

	// THE THREE STATS, AND THE BUCKET EACH LANDS IN.
	const TMap<FName, TArray<FCataclysmStatModifier>>& Granted = Second.Value;
	const auto OneModifier = [&](const TCHAR* Stat)
		-> const FCataclysmStatModifier*
	{
		const TArray<FCataclysmStatModifier>* Rows = Granted.Find(FName(Stat));
		if (!TestNotNull(*FString::Printf(TEXT("%s is granted"), Stat), Rows)
			|| !TestEqual(*FString::Printf(TEXT("%s is granted once"), Stat),
						  Rows->Num(), 1))
		{
			return nullptr;
		}
		return &(*Rows)[0];
	};

	for (const TCHAR* Stat : {TEXT("attack_damage"), TEXT("spell_damage")})
	{
		if (const FCataclysmStatModifier* Row = OneModifier(Stat))
		{
			TestEqual(*FString::Printf(TEXT("%s is 4 per minion"), Stat),
					  Row->Value, 4.0f);
			TestEqual(*FString::Printf(TEXT("%s MULTIPLIES, because the node "
										   "says \"more damage\""), Stat),
					  static_cast<int32>(Row->Bucket),
					  static_cast<int32>(ECataclysmStatBucket::More));
			TestEqual(*FString::Printf(TEXT("%s scales per minion held"), Stat),
					  static_cast<int32>(Row->Scale),
					  static_cast<int32>(ECataclysmStatScale::PerMinionHeld));
		}
	}

	if (const FCataclysmStatModifier* Shield = OneModifier(TEXT("max_energy_shield")))
	{
		TestEqual(TEXT("the shield row is 4 per minion"), Shield->Value, 4.0f);
		TestEqual(TEXT("and JOINS THE ADDITIVE SUM, because the node says "
					   "\"increased Maximum Energy Shield\" and not \"more\""),
				  static_cast<int32>(Shield->Bucket),
				  static_cast<int32>(ECataclysmStatBucket::Increased));
		TestEqual(TEXT("and scales per minion held"),
				  static_cast<int32>(Shield->Scale),
				  static_cast<int32>(ECataclysmStatScale::PerMinionHeld));
	}

	return true;
}

// ---------------------------------------------------------------------------
// The four rows that make four nodes do something. Issue #1718.
//
// EVERY STAT AND CONDITION THESE FOUR NAME WAS BUILT BEFORE THE ROWS EXISTED,
// and until the rows landed all four nodes granted nothing at all. These tests
// are what say a row reaches a real character: the workbook, through
// `game/Data/PassiveEffects.csv`, the generated asset,
// `UCataclysmPlayerClassStats::StatToAttribute` and
// `UCataclysmPassiveTree::AccumulateInto`.
//
// THREE READ AN ATTRIBUTE AND THE FOURTH MUST NOT, which is the one thing to
// get right here. Dominion, Crowned and The Swarm are unconditioned flat rows,
// so they are folded into an attribute. Spreading Hurt carries
// `can_cripple_or_weaken`, and a CONDITIONAL ROW IS NEVER FOLDED INTO AN
// ATTRIBUTE -- it would be stale the moment the character's state moved -- so
// that one asks `UCataclysmAbilitySystemComponent::StatForSkill`. Reading the
// attribute there would read the base for ever and report a dead node working.
//
// THE FIRST TESTS IN THIS FILE TO SPAWN A REAL RITUALIST AND A REAL RAVAGER.
// Every earlier one is a Masochist because that tree was built first; nothing
// prevented the others and these are the changes that needed them.
//
// EACH ONE READS ZERO BEFORE SPENDING AND ZERO AGAIN AFTER GIVING THE POINTS
// BACK. The first says the node rather than a base somewhere is the source; the
// second would catch a build that granted the figure once and never recomputed
// it, which is what a respec finds.
// ---------------------------------------------------------------------------

namespace CataclysmFourRowTest
{
	/** Everything the four tests below need out of a freshly spawned player. */
	struct FRealCharacter
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
		const UDataTable* EffectTable = nullptr;

		bool IsComplete() const
		{
			return Character && State && Equipment && AbilitySystem
				&& EffectTable;
		}
	};

	FRealCharacter Spawn(UWorld* World)
	{
		using namespace CataclysmPassiveTest;

		FRealCharacter Made;
		Made.Character = SpawnPossessedPlayer(World);
		if (!Made.Character)
		{
			return Made;
		}
		Made.State = Made.Character->GetPlayerState<ACataclysmPlayerState>();
		Made.Equipment = Made.Character->GetEquipment();
		Made.AbilitySystem = Made.State
			? Made.State->GetCataclysmAbilitySystemComponent() : nullptr;
		Made.EffectTable = UCataclysmPassiveTree::LoadEffectTable();
		return Made;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveDominionOnARealCharacterTest,
	"Cataclysm.Passives.DominionRaisesARealRitualistsPossessionThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_keystone_a_kA` Dominion on a real character. Issue #1718.
 *
 * "A blow that leaves a target below 65% health can take it, rather than below
 * half."
 *
 * THE ROW HOLDS 15 AND NOT 65, AND THAT IS THE WHOLE SHAPE. Subjugate's own row
 * states `HealthThresholdPercent=50` and is the only place that figure is
 * written; this stat is ADDED to it. A stat holding 65 outright would state the
 * skill's number a second time and win, so re-tuning the skill row would
 * silently do nothing.
 */
bool FCataclysmPassiveDominionOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_keystone_a_kA"));

	// WHAT THE ROW IS AUTHORED AS, CHECKED BEFORE ANYTHING IS SPENT. The stat
	// starts at zero, so an `increased` row would multiply nothing and grant
	// nothing, and the assertion below would read the same zero it started from
	// without saying why.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Dominion grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is the bonus to the possession threshold"),
			  Effects[0]->Stat, FString(TEXT("possession_threshold_bonus")));
	TestEqual(TEXT("stated as a flat amount"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of fifteen"), Effects[0]->ValuePerPoint, 15.0f);
	TestEqual(TEXT("and carrying no condition, which is what lets the "
				   "possession read the attribute directly"),
			  Effects[0]->Condition, FString());

	const FGameplayAttribute Bonus =
		Combat::GetPossessionThresholdBonusAttribute();

	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("an unspent Ritualist gets no bonus to the threshold"),
			  Player.AbilitySystem->GetNumericAttribute(Bonus), 0.0f, 0.001f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);

	TestEqual(TEXT("taking Dominion is worth fifteen points of threshold"),
			  Player.AbilitySystem->GetNumericAttribute(Bonus), 15.0f, 0.001f);

	// AND FIFTEEN ON TOP OF SUBJUGATE'S OWN FIFTY IS THE SIXTY-FIVE THE NODE
	// PROMISES. The fifty is written here rather than read from the skill row
	// because this test has no skill; `test_every_value_appears_in_the_nodes_own_description`
	// in tools/tests/ is what ties the workbook value to the skill's own figure,
	// and it fails if either moves.
	TestEqual(TEXT("which with Subjugate's own 50 is the 65% the node states"),
			  50.0f + Player.AbilitySystem->GetNumericAttribute(Bonus),
			  65.0f, 0.001f);

	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("and giving the point back takes it away again"),
			  Player.AbilitySystem->GetNumericAttribute(Bonus), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveHollowCrownShieldTest,
	"Cataclysm.Passives.HollowCrownRaisesARealRitualistsMaximumEnergyShieldPerMinion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_capstone_200` Hollow Crown on a real character. Issue #1973.
 *
 * "Each minion you have grants you 4% more damage and 4% increased Maximum
 * Energy Shield."
 *
 * THIS ROW GRANTED NOTHING AT ALL UNTIL THIS CHANGE, and its two siblings
 * always worked. All three scale by `minions_held`, and a scaled row is never
 * folded into its gameplay attribute -- it is worked out when something asks.
 * Attack damage and spell damage are asked for by their own per-skill lookups;
 * the maximum energy shield was read straight off the attribute by every reader
 * in the game, so the shield half of this capstone was absent in play and
 * nothing errored or warned.
 *
 * IT MEASURES BOTH HALVES, BECAUSE EITHER ALONE WOULD BE A HALF-FIX. The
 * maximum has to rise, and the shield has to be able to reach it: a bar drawn
 * longer than the clamp allows would look fixed and never fill. The clamp and
 * the bar both ask
 * `UCataclysmAbilitySystemComponent::MaximumEnergyShield`, which is the one
 * function that answers.
 *
 * THE MINIONS ARE REAL. The reading is how many things this character commands,
 * counted from the world, so the case summons four rather than writing a number
 * into a conditions struct that no character in the game would hold.
 */
bool FCataclysmPassiveHollowCrownShieldTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using Vital = UCataclysmVitalAttributeSet;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_capstone_200"));

	// WHAT THE ROW IS AUTHORED AS, READ BEFORE ANYTHING IS SPENT, so a change to
	// the sheet fails here by name rather than as a figure that no longer moves.
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	const FCataclysmPassiveEffectRow* Shield = nullptr;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		if (Row && Row->Stat == TEXT("max_energy_shield"))
		{
			Shield = Row;
		}
	}
	if (!TestNotNull(TEXT("Hollow Crown has a maximum energy shield row"), Shield))
	{
		return false;
	}
	TestEqual(TEXT("stated as an increase"), Shield->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("of four a point"), Shield->ValuePerPoint, 4.0f);
	TestEqual(TEXT("scaled by how many minions are held"), Shield->Scale,
			  FString(TEXT("minions_held")));

	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	const float Bare = Player.AbilitySystem->MaximumEnergyShield();

	// THE RITUALIST IS THE ONE CLASS WITH A SHIELD AT ALL, and every figure below
	// is a share of this one, so a zero here would make them all trivially equal.
	if (!TestTrue(FString::Printf(TEXT("an unspent Ritualist has a shield to "
									   "raise: %.2f"), Bare), Bare > 0.0f))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Allocation.SetChosenOption(Node, 2);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);

	TestEqual(TEXT("taking the capstone with no minions out changes nothing"),
			  Player.AbilitySystem->MaximumEnergyShield(), Bare, 0.01f);

	for (int32 Summoned = 0; Summoned < 4; ++Summoned)
	{
		ACataclysmMinion::Spawn(
			Player.Character,
			FVector(200.0f * static_cast<float>(Summoned + 1), 0.0f, 0.0f),
			/*Lifetime=*/60.0f, /*bBurns=*/false, /*TypeName=*/TEXT("Imp"));
	}
	if (!TestEqual(TEXT("four minions are held"),
				   UCataclysmCommand::ThingsCommandedBy(Player.Character).Num(), 4))
	{
		return false;
	}

	// FOUR PER CENT EACH, SO SIXTEEN, and the figure is stated here rather than
	// read back from the row: a test that computes its expectation from the
	// thing it is testing passes whatever that thing does.
	const float Raised = Player.AbilitySystem->MaximumEnergyShield();
	TestEqual(FString::Printf(TEXT("and four minions raise the maximum by a "
								   "sixth: %.2f against %.2f"), Raised, Bare),
			  Raised, Bare * 1.16f, 0.01f);

	// AND THE SHIELD CAN REACH IT, which the maximum alone does not show. The
	// clamp in `UCataclysmVitalAttributeSet` asks the same function, so a write
	// far above the top lands exactly on it.
	Player.AbilitySystem->SetNumericAttributeBase(
		Vital::GetEnergyShieldAttribute(), Raised + 1000.0f);
	TestEqual(TEXT("and the shield fills to the raised maximum rather than the "
				   "attribute's"),
			  Player.AbilitySystem->GetNumericAttribute(
				  Vital::GetEnergyShieldAttribute()),
			  Raised, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveCrownedOnARealCharacterTest,
	"Cataclysm.Passives.CrownedLowersARealRitualistsMinionReserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_keystone_a_kC` Crowned on a real character. Issue #1718.
 *
 * "Each minion reserves 5 less Fervour, never less than 1."
 *
 * THE ROW HOLDS A POSITIVE 5 AND THE READ SITE SUBTRACTS IT. It is not a bonus
 * of -5: `UCataclysmCombatAttributeSet::PreAttributeChange` floors every
 * attribute in that set at zero, so a negative would be stored as zero and the
 * keystone would do nothing at all. The stat names the SIZE of the reduction,
 * which is how the project's other five `_reduction` stats are already spelled.
 */
bool FCataclysmPassiveCrownedOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_keystone_a_kC"));

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Crowned grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is the reduction to what a minion reserves"),
			  Effects[0]->Stat, FString(TEXT("minion_reserve_reduction")));
	TestEqual(TEXT("stated as a flat amount"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));

	// POSITIVE, AND THE SIGN IS THE ASSERTION. A row authored -5 would be
	// stored as zero by the attribute set's floor and this test would read zero
	// with the point spent, so checking the sign here says WHY rather than
	// leaving the reading below unexplained.
	TestEqual(TEXT("of five, positive, because the attribute cannot hold a "
				   "negative"),
			  Effects[0]->ValuePerPoint, 5.0f);
	TestTrue(TEXT("and it is above zero rather than a negative bonus"),
			 Effects[0]->ValuePerPoint > 0.0f);
	TestEqual(TEXT("and carrying no condition"),
			  Effects[0]->Condition, FString());

	const FGameplayAttribute Reduction =
		Combat::GetMinionReserveReductionAttribute();

	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("an unspent Ritualist takes nothing off the reserve"),
			  Player.AbilitySystem->GetNumericAttribute(Reduction), 0.0f,
			  0.001f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);

	TestEqual(TEXT("taking Crowned takes five Fervour off what a minion "
				   "reserves"),
			  Player.AbilitySystem->GetNumericAttribute(Reduction), 5.0f,
			  0.001f);

	// AND FIVE IS THE "5 LESS" THE NODE STATES, which takes Subjugate's own
	// thirty to twenty-five. The row holds the difference and never a reserve,
	// so this reads the attribute against the sentence's own figure.
	TestEqual(TEXT("which against Subjugate's own 30 leaves a thrall reserving "
				   "25"),
			  30.0f - Player.AbilitySystem->GetNumericAttribute(Reduction),
			  25.0f, 0.001f);

	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("and giving the point back puts the reserve back"),
			  Player.AbilitySystem->GetNumericAttribute(Reduction), 0.0f,
			  0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveSwarmOnARealCharacterTest,
	"Cataclysm.Passives.TheSwarmRaisesARealRitualistsMinionCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_keystone_b_kA` The Swarm on a real character. Issue #1718.
 *
 * "Each skill that limits how many of its minions may be active allows 2 more."
 *
 * THE ROW HOLDS 2, WHICH IS ADDED TO THE CAP A SKILL'S OWN ROW STATES. Summon
 * Imp's is 3, so 5. Every read site treats a cap of zero as no limit at all,
 * so a stat holding a cap itself would hand a skill designed to have none a
 * cap of two.
 */
bool FCataclysmPassiveSwarmOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_keystone_b_kA"));

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("The Swarm grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is the bonus to how many minions may be active"),
			  Effects[0]->Stat, FString(TEXT("minion_cap_bonus")));
	TestEqual(TEXT("stated as a flat amount"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of two"), Effects[0]->ValuePerPoint, 2.0f);
	TestEqual(TEXT("and carrying no condition"),
			  Effects[0]->Condition, FString());

	const FGameplayAttribute Bonus = Combat::GetMinionCapBonusAttribute();

	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("an unspent Ritualist gets no extra minions"),
			  Player.AbilitySystem->GetNumericAttribute(Bonus), 0.0f, 0.001f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);

	TestEqual(TEXT("taking The Swarm is worth two more minions"),
			  Player.AbilitySystem->GetNumericAttribute(Bonus), 2.0f, 0.001f);

	// AND TWO IS THE "2 MORE" THE NODE STATES, which takes Summon Imp's own three
	// to five.
	TestEqual(TEXT("which with Summon Imp's own 3 allows 5"),
			  3.0f + Player.AbilitySystem->GetNumericAttribute(Bonus), 5.0f,
			  0.001f);

	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("and giving the point back puts the cap back"),
			  Player.AbilitySystem->GetNumericAttribute(Bonus), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveSpreadingHurtOnARealCharacterTest,
	"Cataclysm.Passives.SpreadingHurtWidensARealRavagersAreaOnlyWhenItCanCrippleOrWeaken",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_basic_c_c0` Spreading Hurt on a real character. Issue #1718.
 *
 * "+4% increased Area of Effect per point for attacks that Cripple or Weaken."
 * Eight points, so 32% at most.
 *
 * THIS ONE CANNOT BE READ OFF AN ATTRIBUTE AND THE OTHER THREE CAN. Its row
 * carries `can_cripple_or_weaken`, and a conditional row is never folded into a
 * gameplay attribute -- it would be stale the moment the character's state
 * moved. So the attribute still reads the plain base with eight points spent,
 * and `StatForSkill` is what answers. A test written the other way would read
 * the base for ever and call a dead node working; issue #1038 is that failure.
 *
 * THE CONDITION IS ABOUT THE ATTACKER AND NOT THE TARGET, which is why it can
 * work on this row at all. An area of effect is decided when the skill goes off,
 * before anything has been hit, so a condition asking what the target carries
 * would be false every time here.
 *
 * A HUNDRED IS THE RAVAGER'S BASE AND IT COMES FROM THE SHARED CLASS LINE.
 * `area_of_effect` is stated only for the Ritualist, at 110, and every other
 * class inherits the `Default` line's 100. Without a base an `increased` row
 * would multiply zero and grant nothing, which is checked below before anything
 * is spent.
 */
bool FCataclysmPassiveSpreadingHurtOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_basic_c_c0"));
	const FName Stat(TEXT("area_of_effect"));

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Spreading Hurt grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is area of effect"), Effects[0]->Stat,
			  FString(TEXT("area_of_effect")));

	// `increased` AND NOT `flat`, WHICH IS THE OPPOSITE OF THE THREE ABOVE.
	// Area of effect has a base of 100, so a flat row would add four percentage
	// points to a hundred instead of four per cent of it.
	TestEqual(TEXT("stated as an increase, because the stat has a base of 100"),
			  Effects[0]->ValueKind, FString(TEXT("increased")));
	TestEqual(TEXT("of four a point"), Effects[0]->ValuePerPoint, 4.0f);
	TestEqual(TEXT("and it is conditioned on being able to Cripple or Weaken"),
			  Effects[0]->Condition, FString(TEXT("can_cripple_or_weaken")));

	const FGameplayAttribute Area = Combat::GetAreaOfEffectAttribute();

	// THE BASE, BEFORE ANYTHING IS SPENT. If this were zero the increase below
	// would multiply nothing and the node would be dead however the row is
	// written.
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("an unspent Ravager stands at the shared base of 100"),
			  Player.AbilitySystem->GetNumericAttribute(Area), 100.0f, 0.001f);

	// THE NODE'S OWN MAXIMUM, so 32% is what a player who committed to it gets.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 8);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);

	// THE ATTRIBUTE DOES NOT MOVE, AND THAT IS THE POINT RATHER THAN A MISS.
	// A conditional row is never folded into an attribute, so anything reading
	// this one gets the plain base whatever the character has spent.
	TestEqual(TEXT("eight points do not move the attribute, because a "
				   "conditional row is never folded into one"),
			  Player.AbilitySystem->GetNumericAttribute(Area), 100.0f, 0.001f);

	// WITH NO CHANCE TO CRIPPLE OR WEAKEN, THE CONDITION IS FALSE AND THE ROW
	// GRANTS NOTHING. A Ravager that has bought neither ailment is the ordinary
	// case, and it is what says the condition is doing something.
	Player.AbilitySystem->SetNumericAttributeBase(
		Combat::GetCrippleChanceAttribute(), 0.0f);
	Player.AbilitySystem->SetNumericAttributeBase(
		Combat::GetWeakenChanceAttribute(), 0.0f);
	TestEqual(TEXT("with no chance to Cripple or Weaken the row grants "
				   "nothing"),
			  Player.AbilitySystem->StatForSkill(Stat, FGameplayTagContainer(),
												 100.0f),
			  100.0f, 0.001f);

	// AND WITH ONE, THE EIGHT POINTS ARE WORTH 32%.
	Player.AbilitySystem->SetNumericAttributeBase(
		Combat::GetCrippleChanceAttribute(), 25.0f);
	TestEqual(TEXT("a chance to Cripple makes eight points worth 32%"),
			  Player.AbilitySystem->StatForSkill(Stat, FGameplayTagContainer(),
												 100.0f),
			  132.0f, 0.001f);

	// WEAKEN ALONE DOES IT TOO, because the condition asks whether the
	// character can apply EITHER.
	Player.AbilitySystem->SetNumericAttributeBase(
		Combat::GetCrippleChanceAttribute(), 0.0f);
	Player.AbilitySystem->SetNumericAttributeBase(
		Combat::GetWeakenChanceAttribute(), 25.0f);
	TestEqual(TEXT("and a chance to Weaken alone does the same"),
			  Player.AbilitySystem->StatForSkill(Stat, FGameplayTagContainer(),
												 100.0f),
			  132.0f, 0.001f);

	// AND GIVING THE POINTS BACK TAKES THE INCREASE WITH THEM, with the chance
	// left in place so the reading changes for the points and nothing else.
	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	Player.AbilitySystem->SetNumericAttributeBase(
		Combat::GetWeakenChanceAttribute(), 25.0f);
	TestEqual(TEXT("and giving the points back takes the increase away"),
			  Player.AbilitySystem->StatForSkill(Stat, FGameplayTagContainer(),
												 100.0f),
			  100.0f, 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// THE EIGHT KEYSTONES OF 2026-09-14, ONE TEST EACH. Issue #1515.
//
// WHY THESE EXIST WHEN THE ROW COUNTS ARE ALREADY PINNED. A pinned count says
// the file has nine more rows than it did. It does not say a node grants the
// stat, and it cannot: the pins read the CSV, while the game reads the
// DataTable ASSET built from it. Every test below reads the ASSET, through
// `UCataclysmPassiveTree::EffectsFor`, and then spends a point and reads what
// the character actually has.
//
// SO ALL EIGHT FAIL BEFORE THE ASSET IS REBUILT, and that is the evidence this
// change rests on rather than a guard proof. A data row cannot be broken through
// `prove_cpp_guard`, which refuses a file it does not compile and is right to.
// What can be shown is the pair: eight failing against the old asset, eight
// passing against the new one, with nothing else changed.
//
// SEVEN NODES MOVE AN ATTRIBUTE AND ONE DOES NOT. Unstoppable's two rows carry
// a condition, and a conditioned row is never folded into a gameplay attribute,
// so its test reads through `StatForSkill` and puts a body next to the player
// rather than stating a count.
// ---------------------------------------------------------------------------

namespace CataclysmKeystoneRowTest
{
	using namespace CataclysmFourRowTest;

	/** Centimetres in a metre, so a case can place a body in metres. */
	constexpr float M = 100.0f;

	/** A hostile body, for the one node that counts who is standing near. */
	static ACataclysmEnemyCharacter* SpawnHostile(UWorld* World,
												  const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where,
														FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(1'000'000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}
}

// ---------------------------------------------------------------------------
// Every plain row in the three Demonic trees, on a real player. Issue #1755's
// closing count, ruled 2026-09-25.
// ---------------------------------------------------------------------------

namespace CataclysmPlainRowTest
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;

	/**
	 * The fewest plain rows each tree held when this was written, measured on
	 * development b1e1e4b6 with the rule `IsPlain` states: Masochist 59, Ravager
	 * 56, Ritualist 63. A row may be added; a filter that silently skipped rows
	 * would read fewer, and that is what the floor is for.
	 */
	constexpr int32 FewestMasochist = 59;
	constexpr int32 FewestRavager = 56;
	constexpr int32 FewestRitualist = 63;

	/** A row with no condition, no scale, no required tag and no capstone option. */
	bool IsPlain(const FCataclysmPassiveEffectRow& Row)
	{
		return Row.Condition.IsEmpty() && Row.Scale.IsEmpty()
			&& Row.RequiredTags.IsEmpty() && Row.Option == 0;
	}

	/** A stat's recorded line, through the pipeline with no state in hand. */
	FCataclysmStatBreakdown LineOf(const FRealCharacter& Player, const FString& Stat)
	{
		const FCataclysmStatInputs* Inputs =
			Player.AbilitySystem->GetStatInputs(FName(*Stat));
		return Inputs
			? UCataclysmStatPipeline::Evaluate(Inputs->Base, Inputs->Modifiers,
											   FGameplayTagContainer(),
											   FCataclysmStatConditions())
			: FCataclysmStatBreakdown();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryPlainDemonicRowTest,
	"Cataclysm.PlainRows.EveryPlainDemonicRowReachesARealPlayerAtItsFigureTimesThePoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every row in the Masochist, Ravager and Ritualist trees that carries no
 * condition, scale, required tag or capstone option -- a plain "+2% increased
 * Armor per point" -- spent in full on a real player of its tree.
 *
 * WHAT IT ASSERTS, AND IN WHICH BUCKET. The node's points are spent alone on a
 * real player, the equipment refreshes its attributes the way the game does,
 * and the stat's own recorded line is read through the pipeline. Against the
 * same player with nothing spent, it must have moved by the row's figure times
 * the points in the row's own bucket: the flat sum for `flat`, the sum of
 * increases for `increased`, the multiplier by (1 + figure / 100) for `more`.
 * So a row that is missing, misspelt, of the wrong figure or the wrong kind, or
 * that does not reach a real player at all, fails here, named.
 *
 * ITS SCOPE IS PRINTED AND FLOORED. The number of rows checked per tree is
 * logged, and must be at least the count measured when this was written.
 */
bool FCataclysmEveryPlainDemonicRowTest::RunTest(const FString&)
{
	using namespace CataclysmPlainRowTest;

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	// EACH TREE'S PLAIN ROWS, GROUPED BY NODE, so each node is spent once.
	TMap<FString, TMap<FName, TArray<const FCataclysmPassiveEffectRow*>>> ByTree;
	TMap<FName, int32> MaxPointsOf;
	for (const TPair<FName, uint8*>& Pair : EffectTable->GetRowMap())
	{
		const auto* Row = reinterpret_cast<const FCataclysmPassiveEffectRow*>(Pair.Value);
		if (!Row || !IsPlain(*Row))
		{
			continue;
		}
		const FName Node(*Row->Node);
		const FCataclysmPassiveNodeRow* NodeRow =
			NodeTable->FindRow<FCataclysmPassiveNodeRow>(Node, TEXT("plain rows"), false);
		if (!NodeRow)
		{
			AddError(FString::Printf(TEXT("%s names the node %s, which the node table "
										  "does not hold"),
									 *Pair.Key.ToString(), *Row->Node));
			continue;
		}
		if (NodeRow->Tree != TEXT("Masochist") && NodeRow->Tree != TEXT("Ravager")
			&& NodeRow->Tree != TEXT("Ritualist"))
		{
			continue;
		}
		ByTree.FindOrAdd(NodeRow->Tree).FindOrAdd(Node).Add(Row);
		MaxPointsOf.Add(Node, NodeRow->MaxPoints);
	}

	const TMap<FString, int32> Fewest = {{TEXT("Masochist"), FewestMasochist},
										 {TEXT("Ravager"), FewestRavager},
										 {TEXT("Ritualist"), FewestRitualist}};

	for (const TPair<FString, int32>& Tree : Fewest)
	{
		FScopedPlayerClass AsClass(*Tree.Key);
		if (!TestTrue(*FString::Printf(TEXT("the %s class console variable exists"),
									   *Tree.Key),
					  AsClass.IsUsable()))
		{
			continue;
		}
		UWorld* World = MakeWorldThatHasBegunPlay();
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FRealCharacter Player = Spawn(World);
		if (!TestTrue(*FString::Printf(TEXT("a possessed %s"), *Tree.Key),
					  Player.IsComplete()))
		{
			continue;
		}

		const auto Take = [&Player](const FCataclysmPassiveAllocation& Allocation)
		{
			Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
			Player.Equipment->RefreshAttributes(Player.AbilitySystem);
		};

		int32 Checked = 0;
		const TMap<FName, TArray<const FCataclysmPassiveEffectRow*>>* Nodes =
			ByTree.Find(Tree.Key);
		if (Nodes)
		{
			for (const TPair<FName, TArray<const FCataclysmPassiveEffectRow*>>& Node : *Nodes)
			{
				const int32 Points = MaxPointsOf.FindRef(Node.Key);
				Take(FCataclysmPassiveAllocation());
				TMap<FString, FCataclysmStatBreakdown> Before;
				for (const FCataclysmPassiveEffectRow* Row : Node.Value)
				{
					Before.Add(Row->Stat, LineOf(Player, Row->Stat));
				}

				FCataclysmPassiveAllocation Spent;
				Spent.Add(Node.Key, Points);
				Take(Spent);

				for (const FCataclysmPassiveEffectRow* Row : Node.Value)
				{
					++Checked;
					const FCataclysmStatBreakdown& Was = Before.FindChecked(Row->Stat);
					const FCataclysmStatBreakdown Now = LineOf(Player, Row->Stat);
					const float Figure = Row->ValuePerPoint * Points;
					const FString Named = FString::Printf(
						TEXT("%s (%s %s %g a point, %d points)"), *Node.Key.ToString(),
						*Row->Stat, *Row->ValueKind, Row->ValuePerPoint, Points);

					if (Row->ValueKind.Equals(TEXT("flat"), ESearchCase::IgnoreCase))
					{
						TestEqual(*FString::Printf(TEXT("%s adds %g flat"), *Named, Figure),
								  Now.Flat - Was.Flat, Figure, 0.001f);
					}
					else if (Row->ValueKind.Equals(TEXT("more"), ESearchCase::IgnoreCase))
					{
						TestEqual(*FString::Printf(TEXT("%s multiplies by %g"), *Named,
												   1.0f + Figure / 100.0f),
								  Was.MoreMultiplier > 0.0f
									  ? Now.MoreMultiplier / Was.MoreMultiplier
									  : -1.0f,
								  1.0f + Figure / 100.0f, 0.0001f);
					}
					else
					{
						TestEqual(*FString::Printf(TEXT("%s adds %g to the increases"),
												   *Named, Figure),
								  Now.SumOfIncreases - Was.SumOfIncreases, Figure, 0.001f);
					}
				}
			}
		}

		AddInfo(FString::Printf(TEXT("%s: %d plain rows checked, at least %d expected"),
								*Tree.Key, Checked, Tree.Value));
		TestTrue(*FString::Printf(TEXT("the %s tree checked at least %d plain rows: %d"),
								  *Tree.Key, Tree.Value, Checked),
				 Checked >= Tree.Value);
	}
	return true;
}

// --- the three energy-shield keystones -------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveWardedOnARealCharacterTest,
	"Cataclysm.Passives.WardedLetsARealRitualistsShieldTakeDamageOverTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ritualist_keystone_c_kA` Warded: "Your Energy Shield absorbs damage over
 *  time as well as hits." A flag, so the row holds 1. */
bool FCataclysmPassiveWardedOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmKeystoneRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_keystone_c_kA"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Warded grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is whether the shield absorbs damage over time"),
			  Effects[0]->Stat,
			  FString(UCataclysmDamageCalculation::ShieldAbsorbsDamageOverTimeStat));
	TestEqual(TEXT("stated as a flat flag"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of one, because a flag is on or off"),
			  Effects[0]->ValuePerPoint, 1.0f);
	TestEqual(TEXT("and carrying no condition"), Effects[0]->Condition,
			  FString());

	const FGameplayAttribute Flag =
		Combat::GetShieldAbsorbsDamageOverTimeAttribute();
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("an unspent Ritualist's shield stops hits only"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("taking Warded turns the flag on"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 1.0f, 0.001f);

	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("and giving the point back turns it off"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveAblativeOnARealCharacterTest,
	"Cataclysm.Passives.AblativeRechargesARealRitualistsShieldWhileDamaged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ritualist_keystone_c_kB` Ablative: "Your Energy Shield recharges while you
 *  are taking damage, at half its usual rate." The half is in the code; the row
 *  is the flag that turns the rule on. */
bool FCataclysmPassiveAblativeOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmKeystoneRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_keystone_c_kB"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Ablative grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is whether the shield recharges while damaged"),
			  Effects[0]->Stat,
			  FString(UCataclysmRegeneration::ShieldRechargesWhileDamagedStat));
	TestEqual(TEXT("stated as a flat flag"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of one"), Effects[0]->ValuePerPoint, 1.0f);
	TestEqual(TEXT("and carrying no condition"), Effects[0]->Condition,
			  FString());

	const FGameplayAttribute Flag =
		Combat::GetShieldRechargesWhileDamagedAttribute();
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("an unspent Ritualist's shield waits out the delay"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("taking Ablative turns the flag on"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 1.0f, 0.001f);

	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("and giving the point back turns it off"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveLongGameOnARealCharacterTest,
	"Cataclysm.Passives.TheLongGameFeedsARealRitualistsShieldFromMana",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ritualist_keystone_d_kA` The Long Game: "Your Mana Regeneration also
 *  restores your Energy Shield, at half its rate." */
bool FCataclysmPassiveLongGameOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmKeystoneRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_keystone_d_kA"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("The Long Game grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is whether mana regeneration feeds the shield"),
			  Effects[0]->Stat,
			  FString(UCataclysmRegeneration::ManaRegenRestoresShieldStat));
	TestEqual(TEXT("stated as a flat flag"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of one"), Effects[0]->ValuePerPoint, 1.0f);
	TestEqual(TEXT("and carrying no condition"), Effects[0]->Condition,
			  FString());

	const FGameplayAttribute Flag = Combat::GetManaRegenRestoresShieldAttribute();
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("an unspent Ritualist's mana regeneration feeds mana"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("taking The Long Game turns the flag on"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 1.0f, 0.001f);

	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("and giving the point back turns it off"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);
	return true;
}

// --- the two Ravager keystones that forbid a defence -----------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveIronhideOnARealCharacterTest,
	"Cataclysm.Passives.IronhideKeepsARealRavagersArmourFromBeingIgnored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_keystone_spine_001` Ironhide: "Your Armor cannot be ignored." */
bool FCataclysmPassiveIronhideOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmKeystoneRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_keystone_spine_001"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Ironhide grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is whether armour penetration is refused"),
			  Effects[0]->Stat,
			  FString(UCataclysmDamageCalculation::ArmorPenetrationSuppressedStat));
	TestEqual(TEXT("stated as a flat flag"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of one"), Effects[0]->ValuePerPoint, 1.0f);
	TestEqual(TEXT("and carrying no condition"), Effects[0]->Condition,
			  FString());

	const FGameplayAttribute Flag =
		Combat::GetArmorPenetrationSuppressedAttribute();
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("an unspent Ravager's armour can be ignored like anyone's"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("taking Ironhide turns the flag on"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 1.0f, 0.001f);

	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("and giving the point back turns it off"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveEverySwingOnARealCharacterTest,
	"Cataclysm.Passives.EverySwingLandsStopsARealRavagersMeleeBeingEvaded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_keystone_spine_002` Every Swing Lands, first clause: "Your melee
 *  attacks cannot be evaded." The arc is not built and this says nothing of it. */
bool FCataclysmPassiveEverySwingOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmKeystoneRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_keystone_spine_002"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Every Swing Lands grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is whether melee evasion is refused"),
			  Effects[0]->Stat,
			  FString(UCataclysmDamageCalculation::MeleeEvasionSuppressedStat));
	TestEqual(TEXT("stated as a flat flag"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of one"), Effects[0]->ValuePerPoint, 1.0f);
	TestEqual(TEXT("and carrying no condition"), Effects[0]->Condition,
			  FString());

	const FGameplayAttribute Flag = Combat::GetMeleeEvasionSuppressedAttribute();
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("an unspent Ravager's melee can be evaded like anyone's"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("taking Every Swing Lands turns the flag on"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 1.0f, 0.001f);

	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("and giving the point back turns it off"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);
	return true;
}

// --- the three that move a crowd control or movement stat ------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveNothingMovesYouOnARealCharacterTest,
	"Cataclysm.Passives.NothingMovesYouHalvesCrowdControlOnARealRavager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_keystone_a_kB` Nothing Moves You, first clause: "Crowd control
 *  effects on you last half as long." Fifty is that half, by the arithmetic
 *  `AfterCrowdControlResistance` already uses.
 *
 *  THE NODE HAS TWO ROWS NOW, one per clause, so this looks its row up BY
 *  STAT NAME rather than taking the first. The second clause -- a stun ending
 *  when the character kills the enemy that applied it -- is
 *  `Ravager_keystone_a_kB#2` and is checked by
 *  `NothingMovesYouGrantsTheEndOnApplierDeathFlagOnARealRavager`.
 *
 *  THIS SAID THE SECOND CLAUSE "IS NOT BUILT" AND ASSERTED ONE ROW. Both were
 *  true when written and both stopped being true in the change that built it;
 *  the whole-suite run is what said so, with 'Expected ... to be 1, but it was
 *  2'. A positional Effects[0] would have gone on passing silently against
 *  whichever row the table happened to return first. */
bool FCataclysmPassiveNothingMovesYouOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmKeystoneRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_keystone_a_kB"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Nothing Moves You grants two stats, one per clause"),
				   Effects.Num(), 2))
	{
		return false;
	}

	// BY STAT NAME AND NOT BY POSITION. The table's order is not something this
	// test may rely on, and taking Effects[0] would silently check whichever
	// clause came back first.
	const FCataclysmPassiveEffectRow* Halving = nullptr;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		if (Row && Row->Stat ==
				FString(UCataclysmSkillEffects::CrowdControlResistanceStat))
		{
			Halving = Row;
		}
	}
	if (!TestNotNull(TEXT("one of them is crowd control resistance"), Halving))
	{
		return false;
	}
	TestEqual(TEXT("stated as a flat amount, because the stat has no base"),
			  Halving->ValueKind, FString(TEXT("flat")));
	TestEqual(TEXT("of fifty, which is what 'half as long' means"),
			  Halving->ValuePerPoint, 50.0f);
	TestEqual(TEXT("and carrying no condition"), Halving->Condition,
			  FString());

	const FGameplayAttribute Resistance =
		Combat::GetCrowdControlResistanceAttribute();
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	const float Before =
		Player.AbilitySystem->GetNumericAttribute(Resistance);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("taking Nothing Moves You adds fifty resistance"),
			  Player.AbilitySystem->GetNumericAttribute(Resistance),
			  Before + 50.0f, 0.001f);

	// AND FIFTY IS HALF **OF WHAT IS LEFT**, THROUGH THE FUNCTION THAT SCALES
	// THE EFFECT rather than by arithmetic repeated here.
	//
	// THE CLASS LINE ALREADY GRANTS SOME AND THE FIRST VERSION OF THIS TEST
	// ASSUMED IT DID NOT. game/Data/ClassStats.csv gives a Ravager
	// crowd_control_resistance 5 plus 0.15 a level, so a real one carries
	// 7.85 before a point is spent and a three second stun is already 2.76.
	// The node's promise is fifty MORE, so the reading has to be worked from
	// what the character had rather than from zero.
	if (!TestTrue(TEXT("a Ravager carries some resistance from its class line, "
					   "which is why nothing below is written as an absolute"),
				  Before > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("which shortens a three second stun by fifty points more "
				   "than the class line alone"),
			  UCataclysmSkillEffects::AfterCrowdControlResistance(
				  Player.Character, 3.0f),
			  3.0f * (1.0f - (Before + 50.0f) / 100.0f), 0.01f);

	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("and giving the point back leaves only what the class line "
				   "grants"),
			  UCataclysmSkillEffects::AfterCrowdControlResistance(
				  Player.Character, 3.0f),
			  3.0f * (1.0f - Before / 100.0f), 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveRelentlessOnARealCharacterTest,
	"Cataclysm.Passives.RelentlessStopsARealRavagersSpeedBeingReduced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_keystone_d_kA` Relentless: "Your Movement Speed cannot be reduced
 *  by any effect." */
bool FCataclysmPassiveRelentlessOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmKeystoneRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_keystone_d_kA"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Relentless grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is whether a speed reduction is refused"),
			  Effects[0]->Stat,
			  FString(ACataclysmPlayerCharacter::MovementSpeedReductionSuppressedStat));
	TestEqual(TEXT("stated as a flat flag"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of one"), Effects[0]->ValuePerPoint, 1.0f);
	TestEqual(TEXT("and carrying no condition, unlike Unstoppable's"),
			  Effects[0]->Condition, FString());

	const FGameplayAttribute Flag =
		Combat::GetMovementSpeedReductionSuppressedAttribute();
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("an unspent Ravager can be slowed like anyone"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("taking Relentless turns the flag on"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 1.0f, 0.001f);

	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("and giving the point back turns it off"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveUnstoppableOnARealCharacterTest,
	"Cataclysm.Passives.UnstoppableGrantsARealRavagerBothStatsOnlyWhileCrowded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_keystone_spine_003` Unstoppable, and it is the only one of the eight
 * with TWO rows: "You cannot be stunned, slowed, knocked back or knocked down
 * while an enemy is within 4 metres of you."
 *
 * THE ATTRIBUTES DO NOT MOVE AND THAT IS THE POINT RATHER THAN A MISS. Both rows
 * carry a condition, and a conditioned row is never folded into a gameplay
 * attribute, so this reads through `StatForSkill` instead.
 *
 * A BODY IS SPAWNED RATHER THAN A COUNT STATED. Nothing here tells the pipeline
 * how many enemies are near; a hostile character is placed and the game
 * measures. A test that supplies the missing step proves nothing.
 */
bool FCataclysmPassiveUnstoppableOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmKeystoneRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_keystone_spine_003"));
	const FName Resistance(UCataclysmSkillEffects::CrowdControlResistanceStat);
	const FName SpeedFlag(
		ACataclysmPlayerCharacter::MovementSpeedReductionSuppressedStat);

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Unstoppable grants TWO stats, one sentence apiece"),
				   Effects.Num(), 2))
	{
		return false;
	}
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		TestEqual(TEXT("each row is conditioned on an enemy being in reach"),
				  Row->Condition, FString(TEXT("enemies_in_reach_at_least")));
		TestEqual(TEXT("asking for one enemy"), Row->ConditionValue, 1.0f);
		TestEqual(TEXT("within four metres"), Row->ReachMetres, 4.0f);
		TestEqual(TEXT("and stated flat"), Row->ValueKind,
				  FString(TEXT("flat")));
	}

	// WHAT THE CHARACTER CARRIES BEFORE A POINT IS SPENT, READ RATHER THAN
	// ASSUMED TO BE ZERO. The first version of this test assumed zero and
	// failed at 7.85: game/Data/ClassStats.csv grants a Ravager
	// crowd_control_resistance 5 plus 0.15 a level. Every reading below is a
	// difference from this, so the test survives that line being retuned.
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	const float Carried = Player.AbilitySystem->GetNumericAttribute(
		Combat::GetCrowdControlResistanceAttribute());

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);

	// NEITHER ATTRIBUTE MOVES, because a conditioned row is never folded in.
	TestEqual(TEXT("the crowd control resistance attribute does not move"),
			  Player.AbilitySystem->GetNumericAttribute(
				  Combat::GetCrowdControlResistanceAttribute()),
			  Carried, 0.001f);
	TestEqual(TEXT("and neither does the movement flag"),
			  Player.AbilitySystem->GetNumericAttribute(
				  Combat::GetMovementSpeedReductionSuppressedAttribute()),
			  0.0f, 0.001f);

	// ALONE, THE CONDITION IS FALSE AND BOTH ROWS GRANT NOTHING, so each stat
	// answers the fallback it was given -- which for the resistance is what the
	// class line already put on the attribute, exactly as the real read site
	// passes it.
	TestEqual(TEXT("alone, the resistance is what the class line grants"),
			  Player.AbilitySystem->StatForSkill(Resistance,
												 FGameplayTagContainer(), Carried),
			  Carried, 0.001f);
	TestEqual(TEXT("alone, the movement flag is the fallback"),
			  Player.AbilitySystem->StatForSkill(SpeedFlag,
												 FGameplayTagContainer(), 0.0f),
			  0.0f, 0.001f);

	// TEN METRES AWAY IS NOT WITHIN FOUR, and this reading is the control.
	SpawnHostile(World, Player.Character->GetActorLocation()
						+ FVector(10.0f * M, 0.0f, 0.0f));
	TestEqual(TEXT("a body ten metres away does not satisfy the condition"),
			  Player.AbilitySystem->StatForSkill(Resistance,
												 FGameplayTagContainer(), Carried),
			  Carried, 0.001f);

	// AND TWO METRES AWAY, ON A DIFFERENT AXIS so a spawn refused for
	// overlapping cannot quietly move a body somewhere else.
	SpawnHostile(World, Player.Character->GetActorLocation()
						+ FVector(0.0f, 2.0f * M, 0.0f));
	TestEqual(TEXT("one within four metres adds the whole hundred to it"),
			  Player.AbilitySystem->StatForSkill(Resistance,
												 FGameplayTagContainer(), Carried),
			  Carried + 100.0f, 0.001f);
	TestEqual(TEXT("and turns the movement flag on"),
			  Player.AbilitySystem->StatForSkill(SpeedFlag,
												 FGameplayTagContainer(), 0.0f),
			  1.0f, 0.001f);

	// AND A HUNDRED IS IMMUNITY, read through the function that scales it.
	TestEqual(TEXT("which refuses a three second stun outright"),
			  UCataclysmSkillEffects::AfterCrowdControlResistance(
				  Player.Character, 3.0f), 0.0f, 0.01f);
	return true;
}


// ---------------------------------------------------------------------------
// NOTHING MOVES YOU'S SECOND CLAUSE. Issue #1515.
//
// "Crowd control effects on you last half as long, AND ONE ENDS ENTIRELY WHEN
// YOU KILL THE ENEMY THAT APPLIED IT." The first clause is a separate row
// granting fifty crowd control resistance and is tested elsewhere; these four
// are the second.
//
// A STUN IS WHAT "CROWD CONTROL" REACHES TODAY, and that was ruled rather than
// assumed: crowd control in this game is a stun and displacement, displacement
// is instantaneous and has nothing to end, and the first clause's own mechanism
// draws the same line.
//
// THE DEATH IS A REAL ONE. The player lands a real blow with `ApplyHit`, which
// is what records the victim's last blow, and `NoteDeath` then names the player
// as the killer from that record. Nothing here writes a death notice by hand,
// so a build where killer attribution broke would fail these rather than pass
// them.
// ---------------------------------------------------------------------------

namespace CataclysmApplierDeathTest
{
	using namespace CataclysmFourRowTest;

	/** The node, and the stun it is asked to end. */
	const TCHAR* const Node = TEXT("Ravager_keystone_a_kB");

	/** A stun long enough that it would plainly still be running. */
	constexpr float TenSeconds = 10.0f;

	/** One whole swing, as a share of the attacker's weapon damage. */
	constexpr float FullSwing = 100.0f;

	/** A hostile body that can stun and can be killed. */
	static ACataclysmEnemyCharacter* SpawnEnemy(UWorld* World,
											    const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where,
														FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetRarityStep(0);
			Made->SetHealth(1'000'000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}

	/** Spend the node's point, or take it back again. */
	static void Hold(FRealCharacter& Player, bool bHeld)
	{
		FCataclysmPassiveAllocation Allocation;
		if (bHeld)
		{
			Allocation.Add(FName(Node), 1);
		}
		Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
		Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	}

	/**
	 * The player kills this enemy, the way play kills one.
	 *
	 * A REAL BLOW FIRST, because that is what records the victim's last blow,
	 * and the death notice reads the killer out of that record rather than
	 * being told. Then the death is announced.
	 */
	static void KilledByThePlayer(FRealCharacter& Player,
								  ACataclysmEnemyCharacter* Enemy)
	{
		UCataclysmSkillEffects::ApplyHit(Player.Character, Enemy, FullSwing,
										 FGameplayTagContainer());
		UCataclysmCombatEvents::NoteDeath(Enemy);
	}
}

// --- the Masochist keystone that caps healing -------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassivePointOfNoReturnOnARealCharacterTest,
	"Cataclysm.Passives.PointOfNoReturnStopsARealMasochistHealingPastHalf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Masochist_keystone_ll_kB` Point of No Return, first clause: "You cannot be
 * healed above 50% of your maximum health". Issue #1904.
 *
 * WHY IT EXISTS. The owner reported the keystone no longer holding them at half
 * health in the playtest of 2026-09-15. The only test of the ceiling,
 * `Cataclysm.Regeneration.HealingStopsAtAReducedCeilingAndAnUncappedOneDoesNot`,
 * WRITES the attribute, so nothing checked that spending the point reaches it.
 * This reads the row out of the built asset, spends the point on a real
 * Masochist, and heals through `UCataclysmRegeneration::TopUp`.
 *
 * FIVE TIMES THE MAXIMUM IS OFFERED, as the regeneration test offers, rather than
 * a little more than the twenty per cent that would fit. Whatever else a
 * Masochist does to healing it receives, that is more than fits, so the only
 * thing that can stop it at exactly half is the ceiling.
 *
 * WHAT IT DOES NOT SHOW. The ceiling stops healing and does not pull health down
 * to it -- the regeneration test asserts that is the design -- so a character
 * that arrives in the world with full health stays above half until damage
 * takes it below.
 */
bool FCataclysmPassivePointOfNoReturnOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmKeystoneRowTest;
	using Vital = UCataclysmVitalAttributeSet;

	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsMasochist.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Masochist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Masochist_keystone_ll_kB"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);

	// THE CEILING'S ROW, FOUND BY ITS STAT. The keystone has three rows -- the
	// ceiling, and 25% more attack and spell damage -- and their order in the
	// table is not a promise.
	const FCataclysmPassiveEffectRow* Ceiling = nullptr;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		if (Row && Row->Stat == TEXT("healing_ceiling_reduction"))
		{
			Ceiling = Row;
		}
	}
	if (!TestNotNull(TEXT("Point of No Return has a row for the healing ceiling"),
					 Ceiling))
	{
		return false;
	}
	TestEqual(TEXT("stated as a flat reduction"), Ceiling->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of fifty"), Ceiling->ValuePerPoint, 50.0f);
	TestEqual(TEXT("and carrying no condition"), Ceiling->Condition, FString());

	const FGameplayAttribute Reduction = Vital::GetHealingCeilingReductionAttribute();
	const FGameplayAttribute Health = Vital::GetHealthAttribute();
	const FGameplayAttribute MaxHealth = Vital::GetMaxHealthAttribute();

	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("an unspent Masochist has no ceiling"),
			  Player.AbilitySystem->GetNumericAttribute(Reduction), 0.0f, 0.001f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	if (!TestEqual(TEXT("spending the point sets the reduction to fifty"),
				   Player.AbilitySystem->GetNumericAttribute(Reduction), 50.0f,
				   0.001f))
	{
		return false;
	}

	// HEALING STOPS AT HALF. Forty per cent of the character's own maximum, set
	// after the refresh so nothing the refresh does to the pools can move it.
	const float Maximum = Player.AbilitySystem->GetNumericAttribute(MaxHealth);
	if (!TestTrue(FString::Printf(TEXT("the Masochist has a maximum (%.1f)"),
								  Maximum),
				  Maximum > 0.0f))
	{
		return false;
	}
	Player.AbilitySystem->SetNumericAttributeBase(Health, Maximum * 0.4f);
	UCataclysmRegeneration::TopUp(*Player.AbilitySystem, Health, MaxHealth,
								  /*Gain=*/Maximum * 5.0f, FGameplayTagContainer());
	TestEqual(TEXT("healing stops at half of maximum health"),
			  Player.AbilitySystem->GetNumericAttribute(Health), Maximum * 0.5f,
			  0.05f);

	// AND GIVING THE POINT BACK LIFTS IT, so the half above is the keystone's
	// doing and not something every Masochist carries.
	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("giving the point back removes the reduction"),
			  Player.AbilitySystem->GetNumericAttribute(Reduction), 0.0f, 0.001f);
	Player.AbilitySystem->SetNumericAttributeBase(Health, Maximum * 0.4f);
	UCataclysmRegeneration::TopUp(*Player.AbilitySystem, Health, MaxHealth,
								  /*Gain=*/Maximum * 5.0f, FGameplayTagContainer());
	TestEqual(TEXT("and the same healing then fills the pool"),
			  Player.AbilitySystem->GetNumericAttribute(Health), Maximum, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveEndOnApplierDeathFlagTest,
	"Cataclysm.Passives.NothingMovesYouGrantsTheEndOnApplierDeathFlagOnARealRavager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The row reaches a real character, read out of the built ASSET.
 *
 * NOTHING MOVES YOU NOW HAS TWO ROWS, one per clause, and this asserts both are
 * there rather than only the one it is about. A build that dropped the first
 * would leave the node half working with this test still green.
 */
bool FCataclysmPassiveEndOnApplierDeathFlagTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmApplierDeathTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, FName(Node));
	if (!TestEqual(TEXT("Nothing Moves You grants TWO stats, one per clause"),
				   Effects.Num(), 2))
	{
		return false;
	}
	TSet<FString> Stats;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		Stats.Add(Row->Stat);
		TestEqual(TEXT("each stated flat"), Row->ValueKind,
				  FString(TEXT("flat")));
		TestEqual(TEXT("and carrying no condition"), Row->Condition, FString());
	}
	TestTrue(TEXT("one is the resistance that halves an effect's length"),
			 Stats.Contains(
				 FString(UCataclysmSkillEffects::CrowdControlResistanceStat)));
	TestTrue(TEXT("and one ends an effect when its applier dies"),
			 Stats.Contains(FString(
				 ACataclysmPlayerCharacter::
					 CrowdControlEndsWhenItsApplierDiesStat)));

	const FGameplayAttribute Flag =
		Combat::GetCrowdControlEndsWhenItsApplierDiesAttribute();
	Hold(Player, false);
	TestEqual(TEXT("an unspent Ravager's stuns outlive whoever applied them"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);

	Hold(Player, true);
	TestEqual(TEXT("taking Nothing Moves You turns the flag on"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 1.0f, 0.001f);

	Hold(Player, false);
	TestEqual(TEXT("and giving the point back turns it off"),
			  Player.AbilitySystem->GetNumericAttribute(Flag), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveEndsAStunOnItsAppliersDeathTest,
	"Cataclysm.Passives.NothingMovesYouEndsAStunWhenYouKillTheEnemyThatAppliedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The clause itself, end to end through a real death.
 *
 * THE STUN IS ASSERTED RUNNING FIRST. Without that opening reading this test
 * would pass against a build where the stun never landed at all, which is the
 * failure it is least able to notice otherwise.
 */
bool FCataclysmPassiveEndsAStunOnItsAppliersDeathTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmApplierDeathTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	Hold(Player, true);

	ACataclysmEnemyCharacter* Stunner =
		SpawnEnemy(World, FVector(300.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("an enemy to do the stunning"), Stunner))
	{
		return false;
	}

	UCataclysmSkillEffects::ApplyStun(Stunner, Player.Character, TenSeconds,
									  /*DamageDealt=*/0.0f,
									  /*bStunIsDesigned=*/true);
	if (!TestTrue(TEXT("the enemy's stun landed, which every reading below is "
					   "about"),
				  UCataclysmSkillEffects::IsStunned(Player.Character)))
	{
		return false;
	}

	KilledByThePlayer(Player, Stunner);

	TestFalse(TEXT("killing the enemy that applied it ends the stun outright"),
			  UCataclysmSkillEffects::IsStunned(Player.Character));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveAStunSurvivesAnotherKillTest,
	"Cataclysm.Passives.AStunSurvivesWhenYouKillSomeOtherEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The applier and not just anybody, which is the whole rule.
 *
 * WITHOUT THIS THE CHANGE LOOKS CORRECT WHILE ENDING STUNS FROM ENEMIES THE
 * PLAYER NEVER TOUCHED. The test above passes just as well against a build that
 * removed every stun on any kill; only this one separates them.
 */
bool FCataclysmPassiveAStunSurvivesAnotherKillTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmApplierDeathTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	Hold(Player, true);

	ACataclysmEnemyCharacter* Stunner =
		SpawnEnemy(World, FVector(300.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Bystander =
		SpawnEnemy(World, FVector(0.0f, 300.0f, 0.0f));
	if (!TestNotNull(TEXT("an enemy to do the stunning"), Stunner)
		|| !TestNotNull(TEXT("and another to kill instead"), Bystander))
	{
		return false;
	}

	UCataclysmSkillEffects::ApplyStun(Stunner, Player.Character, TenSeconds,
									  /*DamageDealt=*/0.0f,
									  /*bStunIsDesigned=*/true);
	if (!TestTrue(TEXT("the stun landed"),
				  UCataclysmSkillEffects::IsStunned(Player.Character)))
	{
		return false;
	}

	// THE ONE KILLED IS NOT THE ONE THAT STUNNED.
	KilledByThePlayer(Player, Bystander);

	TestTrue(TEXT("killing some other enemy leaves the stun running"),
			 UCataclysmSkillEffects::IsStunned(Player.Character));

	// AND KILLING THE RIGHT ONE STILL ENDS IT, so this test cannot pass because
	// the rule never fires at all.
	KilledByThePlayer(Player, Stunner);
	TestFalse(TEXT("and killing the one that did stun ends it"),
			  UCataclysmSkillEffects::IsStunned(Player.Character));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveNoNodeNoEndingTest,
	"Cataclysm.Passives.WithoutNothingMovesYouAKillEndsNoStun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The node and not a rule everybody has.
 *
 * Being stunned until the stun runs out is what happens to every character in
 * the game. A test that only checked the half WITH the node would pass against
 * a build where killing anything ended every stun for everyone.
 */
bool FCataclysmPassiveNoNodeNoEndingTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmApplierDeathTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}

	// NO POINT SPENT.
	Hold(Player, false);

	ACataclysmEnemyCharacter* Stunner =
		SpawnEnemy(World, FVector(300.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("an enemy to do the stunning"), Stunner))
	{
		return false;
	}

	UCataclysmSkillEffects::ApplyStun(Stunner, Player.Character, TenSeconds,
									  /*DamageDealt=*/0.0f,
									  /*bStunIsDesigned=*/true);
	if (!TestTrue(TEXT("the stun landed"),
				  UCataclysmSkillEffects::IsStunned(Player.Character)))
	{
		return false;
	}

	KilledByThePlayer(Player, Stunner);

	TestTrue(TEXT("without the node the stun outlives the enemy that applied "
				  "it, as it does for every character"),
			 UCataclysmSkillEffects::IsStunned(Player.Character));
	return true;
}

// --------------------------------------------------------------------------
// The Ravager's Fervour: it fills from nearby enemies and drains out of
// contact. Issue #1515.
// --------------------------------------------------------------------------

namespace CataclysmRavagerFervourTest
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;

	/** The tree's starting node: the rate, the decay and the radius. */
	const TCHAR* const StartingNode = TEXT("Ravager_basic_spine_000");

	/** "+2% increased Fervour gained from enemies near you per point." */
	const TCHAR* const HeldGround = TEXT("Ravager_basic_d_b0");

	/** "...within 8 metres of you, rather than 4." */
	const TCHAR* const NoGroundGiven = TEXT("Ravager_keystone_d_kC");

	/**
	 * A STEP LONG ENOUGH TO READ AND SHORT ENOUGH TO BE A STEP. The real one is
	 * `UCataclysmRegeneration::StepSeconds` and runs several times a second;
	 * these tests call the same functions directly with a whole second so the
	 * arithmetic in each assertion is the node's own number rather than that
	 * number divided by a step length.
	 */
	constexpr float OneSecond = 1.0f;

	/** 100 Unreal units to the metre; `UCataclysmTargeting::MetresBetween`. */
	constexpr float Metre = 100.0f;

	const FGameplayAttribute Pool()
	{
		return UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	}

	float FervourOf(const FRealCharacter& Player)
	{
		return Player.AbilitySystem->GetNumericAttribute(Pool());
	}

	/** Put Fervour in the bar without waiting for a fight to fill it. */
	void GiveFervour(FRealCharacter& Player, float Amount)
	{
		Player.AbilitySystem->ApplyModToAttribute(
			Pool(), EGameplayModOp::Additive, Amount);
	}

	/** Spend points across any number of nodes, or take them all back. */
	void Hold(FRealCharacter& Player,
			  const TMap<FName, int32>& PointsByNode)
	{
		FCataclysmPassiveAllocation Allocation;
		for (const TPair<FName, int32>& Each : PointsByNode)
		{
			Allocation.Add(Each.Key, Each.Value);
		}
		Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
		Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	}

	/** One enemy, that many metres along the x axis, harmless and unkillable. */
	ACataclysmEnemyCharacter* EnemyAtMetres(UWorld* World, float Metres)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(
				FVector(Metres * Metre, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetRarityStep(0);
			Made->SetHealth(1'000'000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRavagerFervourArrivesTest,
	"Cataclysm.Passives.FervourArrivesForEnemiesStandingNearARealRavager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_basic_spine_000` on a real character, read out of the built ASSET.
 * Issue #1515.
 *
 * "1 per second for every enemy within 4 metres of you."
 *
 * THE BAR EXISTS ALREADY AND NOTHING COULD MOVE IT. `Default_class_resource` in
 * `game/Data/ClassStats.csv` gives every class a pool of 100, and every stat
 * that filled one belonged to a Masochist node, so this asserts the bar rises
 * from zero rather than that a rate stat holds a number.
 */
bool FCataclysmRavagerFervourArrivesTest::RunTest(const FString&)
{
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	Hold(Player, {{FName(StartingNode), 1}});
	if (!TestEqual(TEXT("the bar starts empty"), FervourOf(Player), 0.0f,
				   0.001f))
	{
		return false;
	}

	if (!TestNotNull(TEXT("an enemy three metres away, inside the four"),
					 EnemyAtMetres(World, 3.0f)))
	{
		return false;
	}

	UCataclysmFervour::GainPerSecondStep(Player.AbilitySystem, OneSecond);
	TestEqual(TEXT("one second beside one enemy is one Fervour"),
			  FervourOf(Player), 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRavagerFervourCountsEachEnemyTest,
	"Cataclysm.Passives.TheFervourRateCountsEachEnemyRatherThanAnyEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The rate is PER enemy, not a flat rate gated on one being near. Issue #1515.
 *
 * THE SCALE IS WHAT THIS PINS AND THE CONDITION WOULD PASS WITHOUT IT. A row
 * written with `enemies_in_reach_at_least` instead of `Scale=enemies_in_reach`
 * would grant one Fervour a second to a Ravager standing in any crowd, and the
 * test above could not tell the two apart. Two enemies giving twice what one
 * gives can only be the scale.
 */
bool FCataclysmRavagerFervourCountsEachEnemyTest::RunTest(const FString&)
{
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	Hold(Player, {{FName(StartingNode), 1}});

	if (!TestNotNull(TEXT("the first enemy"), EnemyAtMetres(World, 3.0f)))
	{
		return false;
	}
	UCataclysmFervour::GainPerSecondStep(Player.AbilitySystem, OneSecond);
	const float AfterOne = FervourOf(Player);
	if (!TestEqual(TEXT("one enemy gives one a second"), AfterOne, 1.0f, 0.001f))
	{
		return false;
	}

	if (!TestNotNull(TEXT("a second enemy, also inside the four"),
					 EnemyAtMetres(World, 2.0f)))
	{
		return false;
	}
	UCataclysmFervour::GainPerSecondStep(Player.AbilitySystem, OneSecond);
	TestEqual(TEXT("two enemies give two a second, so the rate counts bodies"),
			  FervourOf(Player) - AfterOne, 2.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRavagerFervourRespectsTheRadiusTest,
	"Cataclysm.Passives.NoFervourArrivesFromEnemiesOutsideTheFourMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The row's `ReachMetres` is four and an enemy further off is not counted.
 * Issue #1515.
 */
bool FCataclysmRavagerFervourRespectsTheRadiusTest::RunTest(const FString&)
{
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	Hold(Player, {{FName(StartingNode), 1}});

	if (!TestNotNull(TEXT("an enemy five metres away, outside the four"),
					 EnemyAtMetres(World, 5.0f)))
	{
		return false;
	}
	UCataclysmFervour::GainPerSecondStep(Player.AbilitySystem, OneSecond);
	TestEqual(TEXT("an enemy outside the radius generates nothing"),
			  FervourOf(Player), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRavagerFervourGraceHoldsTest,
	"Cataclysm.Passives.FervourHoldsForThreeSecondsAfterContactIsLost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The three second grace. Issue #1515.
 *
 * "Fervour decays at 5 per second AFTER 3 seconds with no enemy within 4
 * metres."
 *
 * THE GRACE IS THE HALF A DECAY TEST ALONE CANNOT SEE. A decay written with no
 * delay drains the bar the instant a fight ends and still passes a test that
 * only checks the bar falls.
 */
bool FCataclysmRavagerFervourGraceHoldsTest::RunTest(const FString&)
{
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	Hold(Player, {{FName(StartingNode), 1}});
	GiveFervour(Player, 50.0f);
	if (!TestEqual(TEXT("fifty in the bar to lose"), FervourOf(Player), 50.0f,
				   0.001f))
	{
		return false;
	}

	// CONTACT FIRST, so the clock has a moment to count from. Without this the
	// character has never been in contact, which counts as out of contact and
	// would decay at once -- correct behaviour, and not what this test is about.
	ACataclysmEnemyCharacter* Near = EnemyAtMetres(World, 3.0f);
	if (!TestNotNull(TEXT("an enemy inside the radius"), Near))
	{
		return false;
	}
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	if (!TestEqual(TEXT("nothing decays while contact holds"),
				   FervourOf(Player), 50.0f, 0.001f))
	{
		return false;
	}

	Near->Destroy();
	World->TimeSeconds += 2.0f;
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	TestEqual(TEXT("two seconds after contact is lost, the bar is untouched"),
			  FervourOf(Player), 50.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRavagerFervourDecaysTest,
	"Cataclysm.Passives.FervourDecaysAtFivePerSecondOnceTheGraceHasLapsed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The decay itself, once the grace has run out. Issue #1515.
 */
bool FCataclysmRavagerFervourDecaysTest::RunTest(const FString&)
{
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	Hold(Player, {{FName(StartingNode), 1}});
	GiveFervour(Player, 50.0f);

	ACataclysmEnemyCharacter* Near = EnemyAtMetres(World, 3.0f);
	if (!TestNotNull(TEXT("an enemy inside the radius"), Near))
	{
		return false;
	}
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	Near->Destroy();

	World->TimeSeconds += 4.0f;
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	TestEqual(TEXT("one second of decay takes five"), FervourOf(Player), 45.0f,
			  0.001f);

	UCataclysmFervour::DecayStep(Player.Character, 2.0f);
	TestEqual(TEXT("and two more seconds take ten more"), FervourOf(Player),
			  35.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRavagerFervourGraceResetsTest,
	"Cataclysm.Passives.ReenteringReachInsideTheGraceMeansNoDecayAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The grace counts from the LAST moment contact held, not from the first time
 * it was lost. Issue #1515.
 *
 * "...so losing contact is what empties it rather than a timer." A character
 * stepping in and out of reach never decays, and a grace counted from the first
 * loss would drain one that had been back in the fight for two seconds.
 */
bool FCataclysmRavagerFervourGraceResetsTest::RunTest(const FString&)
{
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	Hold(Player, {{FName(StartingNode), 1}});
	GiveFervour(Player, 50.0f);

	ACataclysmEnemyCharacter* Near = EnemyAtMetres(World, 3.0f);
	if (!TestNotNull(TEXT("an enemy inside the radius"), Near))
	{
		return false;
	}
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);

	// OUT FOR TWO SECONDS, WHICH IS INSIDE THE THREE.
	Near->Destroy();
	World->TimeSeconds += 2.0f;
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);

	// AND BACK IN, WHICH RESTARTS THE COUNT.
	if (!TestNotNull(TEXT("another enemy steps into reach"),
					 EnemyAtMetres(World, 3.0f)))
	{
		return false;
	}
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);

	// TWO MORE SECONDS: four since the FIRST loss, two since the last.
	World->TimeSeconds += 2.0f;
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	TestEqual(TEXT("four seconds after the first loss but two after the last, "
				   "nothing has drained"),
			  FervourOf(Player), 50.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNowhereToRunFervourTest,
	"Cataclysm.Passives.NowhereToRunKeepsFervourWithAnEnemyWithinEightMetresNotTwelve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Nowhere to Run's third sentence, `Ravager_capstone_200` option 1: "Your
 * Fervour does not decay while any enemy is held this way." Issue #1515.
 *
 * A RAVAGER WITH ITS STARTING NODE, whose grace is 4 metres, and an enemy at 6.
 * Without the option, Fervour decays once the grace seconds pass. With it, the
 * enemy at 6 metres is within 8 and nothing decays. Moved to 10 metres, it
 * decays again: the radius is the larger of 4 and 8, ruled 2026-09-25, not
 * their sum, which would reach 12.
 *
 * THE OPTION IS GIVEN THROUGH THE DUNGEON STAT MODIFIERS, which the refresh
 * keeps beside the character's real rows, so the starting node's grace stays
 * what its row makes it. The option has no row yet.
 */
bool FCataclysmNowhereToRunFervourTest::RunTest(const FString&)
{
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"), AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"), Player.IsComplete()))
	{
		return false;
	}
	Hold(Player, {{FName(StartingNode), 1}});
	GiveFervour(Player, 50.0f);

	ACataclysmEnemyCharacter* Enemy = EnemyAtMetres(World, 6.0f);
	if (!TestNotNull(TEXT("an enemy at 6 metres"), Enemy))
	{
		return false;
	}

	// WITHOUT THE OPTION: THE CONTROL.
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	World->TimeSeconds += 4.0f;
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	const float AfterDecay = FervourOf(Player);
	if (!TestTrue(TEXT("without the option, an enemy at 6 metres is outside the "
					   "4 metre grace and Fervour decays"),
				  AfterDecay < 50.0f))
	{
		return false;
	}

	// WITH IT.
	FCataclysmStatModifier Flat;
	Flat.Bucket = ECataclysmStatBucket::Flat;
	Flat.Source = ECataclysmModifierSource::PassiveKeystone;
	Flat.Value = 8.0f;
	TMap<FName, TArray<FCataclysmStatModifier>> Option;
	Option.Add(FName(UCataclysmDebuffs::NowhereToRunMetresStat), {Flat});
	Player.AbilitySystem->SetDungeonStatModifiers(MoveTemp(Option));
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);

	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	World->TimeSeconds += 4.0f;
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	TestEqual(TEXT("with Nowhere to Run, the enemy at 6 metres is held and nothing "
				   "decays"),
			  FervourOf(Player), AfterDecay, 0.001f);

	Enemy->SetActorLocation(FVector(10.0f * Metre, 0.0f, 0.0f));
	World->TimeSeconds += 4.0f;
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	TestTrue(TEXT("and at 10 metres it decays again: the radius is 8, not 4 and 8 "
				  "added"),
			 FervourOf(Player) < AfterDecay);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourWithoutTheNodeNeverDecaysTest,
	"Cataclysm.Passives.AMasochistWithoutTheRavagerNodeNeverDecays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The decay is a stat the node grants, not a rule of the game. Issue #1515.
 *
 * THE CONTROL FOR THE WHOLE CHANGE. A decay written as a constant would drain
 * every pool in the game for a sentence one node states, and every test above
 * would still pass. The Masochist fills its bar by being hurt and has no
 * reason to lose it for standing alone.
 */
bool FCataclysmFervourWithoutTheNodeNeverDecaysTest::RunTest(const FString&)
{
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsMasochist(TEXT("Masochist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsMasochist.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Masochist with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	GiveFervour(Player, 50.0f);
	if (!TestEqual(TEXT("fifty in the bar"), FervourOf(Player), 50.0f, 0.001f))
	{
		return false;
	}

	// NO ENEMY AND A LONG TIME, which is the worst case for a character the
	// rule does not apply to.
	World->TimeSeconds += 30.0f;
	UCataclysmFervour::DecayStep(Player.Character, 30.0f);
	TestEqual(TEXT("a character without the Ravager's node keeps every point"),
			  FervourOf(Player), 50.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeldGroundRaisesTheRateTest,
	"Cataclysm.Passives.HeldGroundRaisesTheRateFromNearbyEnemies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_basic_d_b0` Held Ground. Issue #1515.
 *
 * "+2% increased Fervour gained from enemies near you per point."
 *
 * AN `increased` ROW AGAINST A STAT WITH NO BASE, which works only because the
 * starting node grants the flat value it multiplies. Eight points is 16%, so a
 * rate of one a second becomes 1.16.
 */
bool FCataclysmHeldGroundRaisesTheRateTest::RunTest(const FString&)
{
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("one enemy inside the radius"),
					 EnemyAtMetres(World, 3.0f)))
	{
		return false;
	}

	Hold(Player, {{FName(StartingNode), 1}});
	UCataclysmFervour::GainPerSecondStep(Player.AbilitySystem, OneSecond);
	const float Plain = FervourOf(Player);
	if (!TestEqual(TEXT("one a second without Held Ground"), Plain, 1.0f,
				   0.001f))
	{
		return false;
	}

	Hold(Player, {{FName(StartingNode), 1}, {FName(HeldGround), 8}});
	UCataclysmFervour::GainPerSecondStep(Player.AbilitySystem, OneSecond);
	TestEqual(TEXT("eight points of Held Ground is sixteen per cent more"),
			  FervourOf(Player) - Plain, 1.16f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoGroundGivenWidensTheRadiusTest,
	"Cataclysm.Passives.NoGroundGivenWidensTheRadiusToEight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_keystone_d_kC` No Ground Given. Issue #1515.
 *
 * "Your Fervour does not decay while an enemy is within 8 metres of you,
 * rather than 4."
 *
 * THIS TEST IS WHAT CHECKS THE ARITHMETIC THE SENTENCE DOES NOT STATE. The
 * keystone's row grants 4, not 8, because two flat rows on one stat sum with
 * the starting node's 4. Neither digit in the sentence is the row's value, so
 * `VALUE_IN_WORDS` in
 * `tools/tests/test_passive_effects_match_the_node_text.py` exempts that row
 * from the digit check and names this test as what replaces it. A change to
 * either row that breaks the sum fails here.
 *
 * AN ENEMY AT FIVE METRES IS THE WHOLE EXPERIMENT: outside the starting node's
 * four and inside the keystone's eight, so it decays without the keystone and
 * holds with it.
 */
bool FCataclysmNoGroundGivenWidensTheRadiusTest::RunTest(const FString&)
{
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("an enemy five metres away"),
					 EnemyAtMetres(World, 5.0f)))
	{
		return false;
	}

	// WITHOUT THE KEYSTONE: five metres is outside the four, so contact never
	// holds and the bar drains once the grace lapses.
	Hold(Player, {{FName(StartingNode), 1}});
	GiveFervour(Player, 50.0f);
	World->TimeSeconds += 4.0f;
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	if (!TestEqual(TEXT("at four metres of radius, an enemy at five is out of "
					    "contact and the bar drains"),
				   FervourOf(Player), 45.0f, 0.001f))
	{
		return false;
	}

	// WITH IT: the two rows sum to eight, so the same enemy is in contact.
	Hold(Player, {{FName(StartingNode), 1}, {FName(NoGroundGiven), 1}});
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	World->TimeSeconds += 4.0f;
	UCataclysmFervour::DecayStep(Player.Character, OneSecond);
	TestEqual(TEXT("with No Ground Given the radius is eight, so the same "
				   "enemy holds contact and nothing drains"),
			  FervourOf(Player), 45.0f, 0.001f);
	return true;
}

// --------------------------------------------------------------------------
// Wrung Out spends Fervour on a kill to restore health, and Grinding Halt
// generates it from crippled enemies near the character. Issue #1515.
// --------------------------------------------------------------------------

namespace CataclysmSpenderTest
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;

	/** "Killing an enemy spends 5 Fervour to restore 1% ... per point." */
	const TCHAR* const WrungOut = TEXT("Ravager_basic_d_c1");

	/** "Each Crippled enemy within 4 metres of you grants you 1 Fervour..." */
	const TCHAR* const GrindingHalt = TEXT("Ravager_keystone_c_kC");

	/** Wrung Out's full six points, which is six per cent. */
	constexpr int32 WrungOutPoints = 6;

	float HealthOf(const FRealCharacter& Player)
	{
		return Player.AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute());
	}

	float MaxHealthOf(const FRealCharacter& Player)
	{
		return Player.AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	}

	/** Put the character at half health, so a restoration has room to land. */
	void HalfHealth(FRealCharacter& Player)
	{
		Player.AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(),
			MaxHealthOf(Player) * 0.5f);
	}

	/** Cripple an enemy the way the game curses anything: a tag for a time. */
	bool Cripple(FRealCharacter& Player, ACataclysmEnemyCharacter* Enemy)
	{
		const FGameplayTag Tag = UCataclysmDebuffs::CrippleTag();
		return Tag.IsValid()
			&& UCataclysmSkillEffects::ApplyTagForDuration(
				   Player.Character, Enemy, Tag, 10.0f);
	}

	bool IsCrippled(const ACataclysmEnemyCharacter* Enemy)
	{
		return UCataclysmDebuffs::TagsOnActor(Enemy).HasTag(
			UCataclysmDebuffs::CrippleTag());
	}
}

// ---- Wrung Out --------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWrungOutRestoresTest,
	"Cataclysm.Passives.WrungOutRestoresHealthWhenARealRavagerKills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_basic_d_c1` Wrung Out on a real character, read out of the built
 * ASSET. Issue #1515.
 *
 * "Killing an enemy spends 5 Fervour to restore 1% of your maximum health per
 * point."
 *
 * HEALTH IS READ IMMEDIATELY EITHER SIDE OF THE KILL. The blow the kill helper
 * lands records a life leech payment for a Ravager, whose class line grants
 * leech, but leech is paid out by the periodic step and this test runs none.
 * If that ever changed, the difference would stop being exactly six per cent
 * and this would fail loudly rather than pass for the wrong reason.
 */
bool FCataclysmWrungOutRestoresTest::RunTest(const FString&)
{
	using namespace CataclysmSpenderTest;
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	ACataclysmEnemyCharacter* Victim = EnemyAtMetres(World, 3.0f);
	if (!TestNotNull(TEXT("an enemy to kill"), Victim))
	{
		return false;
	}

	CataclysmRavagerFervourTest::Hold(Player, {{FName(WrungOut), WrungOutPoints}});
	HalfHealth(Player);
	GiveFervour(Player, 50.0f);

	const float Maximum = MaxHealthOf(Player);
	const float HealthBefore = HealthOf(Player);
	CataclysmApplierDeathTest::KilledByThePlayer(Player, Victim);

	TestEqual(TEXT("six points of Wrung Out restore six per cent of maximum "
				   "health"),
			  HealthOf(Player) - HealthBefore, Maximum * 0.06f, 0.01f);
	TestEqual(TEXT("and cost five Fervour"), FervourOf(Player), 45.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWrungOutUnpayableTest,
	"Cataclysm.Passives.WrungOutRestoresNothingWithoutFiveFervour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * An unpayable cost buys nothing, and nothing is spent. Issue #1515.
 *
 * ALL OR NOTHING IS A READING, recorded in `UCataclysmFervour::RestoreHealthOnKill`:
 * the same tree's Bought With Ruin says "If you cannot pay, the attack still
 * hits but gains nothing". Three Fervour is less than the five a kill costs.
 */
bool FCataclysmWrungOutUnpayableTest::RunTest(const FString&)
{
	using namespace CataclysmSpenderTest;
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Victim = EnemyAtMetres(World, 3.0f);
	if (!TestNotNull(TEXT("an enemy to kill"), Victim))
	{
		return false;
	}

	CataclysmRavagerFervourTest::Hold(Player, {{FName(WrungOut), WrungOutPoints}});
	HalfHealth(Player);
	GiveFervour(Player, 3.0f);

	const float HealthBefore = HealthOf(Player);
	CataclysmApplierDeathTest::KilledByThePlayer(Player, Victim);

	TestEqual(TEXT("three Fervour cannot pay five, so no health arrives"),
			  HealthOf(Player), HealthBefore, 0.01f);
	TestEqual(TEXT("and none of the three is spent"), FervourOf(Player), 3.0f,
			  0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWrungOutFullHealthTest,
	"Cataclysm.Passives.WrungOutSpendsNothingAtFullHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A restoration that restores nothing costs nothing. Issue #1515.
 *
 * A JUDGEMENT RATHER THAN A WORD OF THE SENTENCE, recorded in
 * `UCataclysmFervour::RestoreHealthOnKill`: paying five Fervour to restore health
 * the character already has is a cost with no effect, and no sentence
 * describes one.
 */
bool FCataclysmWrungOutFullHealthTest::RunTest(const FString&)
{
	using namespace CataclysmSpenderTest;
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Victim = EnemyAtMetres(World, 3.0f);
	if (!TestNotNull(TEXT("an enemy to kill"), Victim))
	{
		return false;
	}

	CataclysmRavagerFervourTest::Hold(Player, {{FName(WrungOut), WrungOutPoints}});
	Player.AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), MaxHealthOf(Player));
	GiveFervour(Player, 50.0f);

	CataclysmApplierDeathTest::KilledByThePlayer(Player, Victim);
	TestEqual(TEXT("at full health a kill spends no Fervour"), FervourOf(Player),
			  50.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWrungOutControlTest,
	"Cataclysm.Passives.AKillWithoutWrungOutRestoresNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The control: a kill is not a heal for anyone without the node. Issue #1515.
 *
 * THE DEATH HANDLER RETURNED EARLY BEFORE THIS CHANGE and now runs two rules in
 * sequence. This is what fails if the new rule ever runs without its row.
 */
bool FCataclysmWrungOutControlTest::RunTest(const FString&)
{
	using namespace CataclysmSpenderTest;
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Victim = EnemyAtMetres(World, 3.0f);
	if (!TestNotNull(TEXT("an enemy to kill"), Victim))
	{
		return false;
	}

	CataclysmRavagerFervourTest::Hold(Player, {});
	HalfHealth(Player);
	GiveFervour(Player, 50.0f);

	const float HealthBefore = HealthOf(Player);
	CataclysmApplierDeathTest::KilledByThePlayer(Player, Victim);
	TestEqual(TEXT("without Wrung Out a kill restores no health"),
			  HealthOf(Player), HealthBefore, 0.01f);
	TestEqual(TEXT("and spends no Fervour"), FervourOf(Player), 50.0f, 0.001f);
	return true;
}

// ---- Grinding Halt ----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGrindingHaltGrantsTest,
	"Cataclysm.Passives.GrindingHaltGrantsFervourForACrippledEnemyNear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_keystone_c_kC` Grinding Halt on a real character, read out of the
 * built ASSET. Issue #1515.
 *
 * "Each Crippled enemy within 4 metres of you grants you 1 Fervour per second."
 *
 * THE KEYSTONE ALONE, WITHOUT THE STARTING NODE, so the one Fervour this asserts
 * can only have come from the crippled-enemy row. The starting node grants the
 * same rate over every enemy and would add its own one; that is its own test.
 */
bool FCataclysmGrindingHaltGrantsTest::RunTest(const FString&)
{
	using namespace CataclysmSpenderTest;
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	ACataclysmEnemyCharacter* Near = EnemyAtMetres(World, 3.0f);
	if (!TestNotNull(TEXT("an enemy three metres away"), Near)
		|| !TestTrue(TEXT("and it is crippled"), Cripple(Player, Near)
					 && IsCrippled(Near)))
	{
		return false;
	}

	CataclysmRavagerFervourTest::Hold(Player, {{FName(GrindingHalt), 1}});
	UCataclysmFervour::GainPerSecondStep(Player.AbilitySystem, OneSecond);
	TestEqual(TEXT("one crippled enemy within four metres gives one Fervour a "
				   "second"),
			  FervourOf(Player), 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGrindingHaltUncrippledTest,
	"Cataclysm.Passives.GrindingHaltIgnoresAnEnemyThatIsNotCrippled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The filter: an enemy near but not crippled grants nothing. Issue #1515.
 *
 * THE TEST THE DATA HALF CANNOT VOUCH FOR. It asserts nothing arrives, which is
 * true without the row too, so only a break of the filter itself can show it
 * discriminates.
 */
bool FCataclysmGrindingHaltUncrippledTest::RunTest(const FString&)
{
	using namespace CataclysmSpenderTest;
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Near = EnemyAtMetres(World, 3.0f);
	if (!TestNotNull(TEXT("an enemy three metres away"), Near)
		|| !TestFalse(TEXT("and it is not crippled"), IsCrippled(Near)))
	{
		return false;
	}

	CataclysmRavagerFervourTest::Hold(Player, {{FName(GrindingHalt), 1}});
	UCataclysmFervour::GainPerSecondStep(Player.AbilitySystem, OneSecond);
	TestEqual(TEXT("an enemy near but not crippled gives nothing"),
			  FervourOf(Player), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGrindingHaltCountsTest,
	"Cataclysm.Passives.GrindingHaltCountsEachCrippledEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The rate is PER crippled enemy, not a flat rate gated on one. Issue #1515.
 */
bool FCataclysmGrindingHaltCountsTest::RunTest(const FString&)
{
	using namespace CataclysmSpenderTest;
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* First = EnemyAtMetres(World, 3.0f);
	ACataclysmEnemyCharacter* Second = EnemyAtMetres(World, 2.0f);
	if (!TestNotNull(TEXT("a first enemy"), First)
		|| !TestNotNull(TEXT("and a second"), Second)
		|| !TestTrue(TEXT("both crippled"), Cripple(Player, First)
					 && Cripple(Player, Second) && IsCrippled(First)
					 && IsCrippled(Second)))
	{
		return false;
	}

	CataclysmRavagerFervourTest::Hold(Player, {{FName(GrindingHalt), 1}});
	UCataclysmFervour::GainPerSecondStep(Player.AbilitySystem, OneSecond);
	TestEqual(TEXT("two crippled enemies give two a second"), FervourOf(Player),
			  2.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGrindingHaltRadiusTest,
	"Cataclysm.Passives.GrindingHaltIgnoresACrippledEnemyBeyondFourMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The row's `ReachMetres` is four. Issue #1515. */
bool FCataclysmGrindingHaltRadiusTest::RunTest(const FString&)
{
	using namespace CataclysmSpenderTest;
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Far = EnemyAtMetres(World, 5.0f);
	if (!TestNotNull(TEXT("an enemy five metres away"), Far)
		|| !TestTrue(TEXT("and it is crippled"), Cripple(Player, Far)
					 && IsCrippled(Far)))
	{
		return false;
	}

	CataclysmRavagerFervourTest::Hold(Player, {{FName(GrindingHalt), 1}});
	UCataclysmFervour::GainPerSecondStep(Player.AbilitySystem, OneSecond);
	TestEqual(TEXT("a crippled enemy beyond four metres gives nothing"),
			  FervourOf(Player), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCrippledCountsTwiceTest,
	"Cataclysm.Passives.ACrippledEnemyCountsForTheStartingNodeAndGrindingHalt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The two rows ADD. Issue #1515.
 *
 * THE STARTING NODE grants one Fervour a second for every enemy within four
 * metres, and GRINDING HALT grants one more for every CRIPPLED enemy there. A
 * crippled enemy is both, so it earns two. They share one rate statistic, which
 * is why Held Ground increases both; this pins that the shared statistic sums
 * the two rows rather than one replacing the other.
 */
bool FCataclysmCrippledCountsTwiceTest::RunTest(const FString&)
{
	using namespace CataclysmSpenderTest;
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Near = EnemyAtMetres(World, 3.0f);
	if (!TestNotNull(TEXT("an enemy three metres away"), Near)
		|| !TestTrue(TEXT("and it is crippled"), Cripple(Player, Near)
					 && IsCrippled(Near)))
	{
		return false;
	}

	CataclysmRavagerFervourTest::Hold(
		Player, {{FName(StartingNode), 1}, {FName(GrindingHalt), 1}});
	UCataclysmFervour::GainPerSecondStep(Player.AbilitySystem, OneSecond);
	TestEqual(TEXT("a crippled enemy earns one from each row, so two"),
			  FervourOf(Player), 2.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// THE THREE RAVAGER NODES THAT READ HOW MANY ENEMIES ONE ATTACK STRUCK. Issue
// #1515. The count and what reads it were built and tested in
// `CataclysmSkillTemplateTests.cpp` and `CataclysmStatPipelineTests.cpp`; these
// three read each node's REAL ROW on a real Ravager, so a row authored with the
// wrong stat, bucket, tag, condition or scale fails here rather than granting
// nothing in play.
// ---------------------------------------------------------------------------

namespace CataclysmEnemiesStruckRowTest
{
	/** `Type.Melee`, or an empty container if the tag does not exist. */
	FGameplayTagContainer MeleeTags()
	{
		FGameplayTagContainer Tags;
		const FGameplayTag Melee = UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("Type.Melee")), /*ErrorIfNotFound=*/false);
		if (Melee.IsValid())
		{
			Tags.AddTag(Melee);
		}
		return Tags;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveCleavingArcOnARealCharacterTest,
	"Cataclysm.Passives.CleavingArcRaisesARealRavagersAttackDamageForEachEnemyBeyondTheFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_basic_b_a2` Cleaving Arc on a real character. Issue #1515.
 *
 * "+1% increased Attack Damage per point for each enemy your attack hits beyond
 * the first." Eight points.
 *
 * A SCALED ROW IS NEVER FOLDED INTO THE ATTRIBUTE, so
 * `AttackDamageIncreasesForSkill` answers, given the count the way a blow gives
 * it. EVERY READING IS A DIFFERENCE FROM ONE ENEMY STRUCK, so whatever else the
 * character's attack damage increases hold cancels out and only this row is
 * left.
 */
bool FCataclysmPassiveCleavingArcOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_basic_b_a2"));

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Cleaving Arc grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is attack damage"), Effects[0]->Stat,
			  FString(TEXT("attack_damage")));
	TestEqual(TEXT("stated as an increase"), Effects[0]->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("of one a point"), Effects[0]->ValuePerPoint, 1.0f);
	TestEqual(TEXT("scaled by each enemy the attack struck beyond the first"),
			  Effects[0]->Scale, FString(TEXT("enemies_hit_beyond_the_first")));
	TestEqual(TEXT("one step at a time"), Effects[0]->ScaleStep, 1.0f);
	TestEqual(TEXT("and requiring no tag, because the node says \"your attack\""),
			  Effects[0]->RequiredTags, FString());

	// THE INCREASES FOR AN ATTACK THAT STRUCK THIS MANY ENEMIES TOGETHER, as a
	// fraction. -1 is a blow that carries no count.
	const auto IncreasesFor = [&Player](int32 EnemiesStruckTogether)
	{
		return Player.AbilitySystem->AttackDamageIncreasesForSkill(
			FGameplayTagContainer(), -1.0f, -1.0f, -1.0f, false, nullptr,
			EnemiesStruckTogether);
	};

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 8);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);

	TestEqual(TEXT("eight points are worth 16% more against three enemies "
				   "struck than against one"),
			  IncreasesFor(3) - IncreasesFor(1), 0.16f, 0.0001f);
	TestEqual(TEXT("and 8% more against two"),
			  IncreasesFor(2) - IncreasesFor(1), 0.08f, 0.0001f);
	TestEqual(TEXT("and a blow carrying no count gets what one enemy gets"),
			  IncreasesFor(-1) - IncreasesFor(1), 0.0f, 0.0001f);

	// AND GIVING THE POINTS BACK TAKES IT AWAY, so the difference above belongs
	// to the points and to nothing else on the character.
	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("with the points given back, three enemies are worth no "
				   "more than one"),
			  IncreasesFor(3) - IncreasesFor(1), 0.0f, 0.0001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveBoughtWithRuinOnARealCharacterTest,
	"Cataclysm.Passives.BoughtWithRuinLetsARealRavagersMeleeAttacksBuyDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_basic_b_b2` Bought With Ruin on a real character. Issue #1515.
 *
 * "Each enemy your melee attack hits beyond the first costs 2 Fervour and deals
 * +3% increased damage per point. If you cannot pay, the attack still hits but
 * gains nothing." Eight points.
 *
 * THE ROW HOLDS THE PERCENTAGE ONLY. The 2 Fervour is
 * `UCataclysmFervour::ExtraEnemyHitCost`, because no node changes it, and what
 * the purchase does is pinned by the Skills tests that give the stat directly.
 * This reads what the real row gives, where the purchase asks for it: through
 * `StatForSkill` with the attacking skill's own tags.
 */
bool FCataclysmPassiveBoughtWithRuinOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using namespace CataclysmEnemiesStruckRowTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_basic_b_b2"));
	const FName Stat(UCataclysmFervour::IncreasedDamageBoughtPerExtraEnemyHitStat);

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Bought With Ruin grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is the damage bought for each extra enemy"),
			  Effects[0]->Stat,
			  FString(TEXT("increased_damage_bought_per_extra_enemy_hit")));
	TestEqual(TEXT("stated as a flat amount, because the stat starts at zero"),
			  Effects[0]->ValueKind, FString(TEXT("flat")));
	TestEqual(TEXT("of three a point"), Effects[0]->ValuePerPoint, 3.0f);
	TestEqual(TEXT("for melee attacks only"), Effects[0]->RequiredTags,
			  FString(TEXT("Type.Melee")));
	TestEqual(TEXT("and carrying no condition"), Effects[0]->Condition,
			  FString());

	const FGameplayTagContainer Melee = MeleeTags();
	if (!TestEqual(TEXT("the melee tag exists in the vocabulary"), Melee.Num(), 1))
	{
		return false;
	}

	const FGameplayAttribute Bought = UCataclysmClassResourceAttributeSet::
		GetIncreasedDamageBoughtPerExtraEnemyHitAttribute();

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 8);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);

	const float Attribute = Player.AbilitySystem->GetNumericAttribute(Bought);

	TestEqual(TEXT("eight points let a melee attack buy 24% for each enemy "
				   "beyond the first"),
			  Player.AbilitySystem->StatForSkill(Stat, Melee, Attribute), 24.0f,
			  0.001f);
	TestEqual(TEXT("and an attack that is not melee buys nothing"),
			  Player.AbilitySystem->StatForSkill(Stat, FGameplayTagContainer(),
												 Attribute),
			  0.0f, 0.001f);

	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("and with the points given back a melee attack buys nothing"),
			  Player.AbilitySystem->StatForSkill(
				  Stat, Melee, Player.AbilitySystem->GetNumericAttribute(Bought)),
			  0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveSunderingOnARealCharacterTest,
	"Cataclysm.Passives.SunderingIgnoresArmourOnlyWhenARealRavagersMeleeAttackStrikesThree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_keystone_b_kA` Sundering on a real character. Issue #1515.
 *
 * "Your melee attacks ignore enemy Armor entirely when they hit three or more
 * enemies at once."
 *
 * "ENTIRELY" IS 100 POINTS OF ARMOUR PENETRATION, the share of armour a blow
 * ignores, which the damage calculation clamps to 100. A defender that forbids
 * penetration still keeps its armour; the decisions entry of 2026-09-16 on the
 * count records why.
 *
 * READ THE WAY THE DEFENDER'S ARMOUR LOOKUP READS IT: `StatForSkill` with the
 * skill's tags and the count the blow carried. Every reading is a difference
 * from the attribute, so any other source of penetration cancels out.
 */
bool FCataclysmPassiveSunderingOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using namespace CataclysmEnemiesStruckRowTest;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_keystone_b_kA"));
	const FName Stat(TEXT("armor_penetration"));

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Sundering grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is armour penetration"), Effects[0]->Stat,
			  FString(TEXT("armor_penetration")));
	TestEqual(TEXT("stated as a flat amount"), Effects[0]->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of a hundred, which is all of it"),
			  Effects[0]->ValuePerPoint, 100.0f);
	TestEqual(TEXT("for melee attacks only"), Effects[0]->RequiredTags,
			  FString(TEXT("Type.Melee")));
	TestEqual(TEXT("when the attack struck at least"), Effects[0]->Condition,
			  FString(TEXT("enemies_hit_at_least")));
	TestEqual(TEXT("three enemies"), Effects[0]->ConditionValue, 3.0f);

	const FGameplayTagContainer Melee = MeleeTags();
	if (!TestEqual(TEXT("the melee tag exists in the vocabulary"), Melee.Num(), 1))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);

	const float Attribute = Player.AbilitySystem->GetNumericAttribute(
		Combat::GetArmorPenetrationAttribute());

	// WHAT A BLOW WITH THESE TAGS THAT STRUCK THIS MANY ENEMIES TOGETHER GETS ON
	// TOP OF THE ATTRIBUTE. -1 is a blow that carries no count.
	const auto AddedFor = [&](const FGameplayTagContainer& Tags,
							  int32 EnemiesStruckTogether)
	{
		return Player.AbilitySystem->StatForSkill(
				   Stat, Tags, Attribute, -1.0f, FCataclysmBlowContext(), -1.0f,
				   -1.0f, false, nullptr, EnemiesStruckTogether)
			- Attribute;
	};

	TestEqual(TEXT("a melee attack striking three enemies ignores all armour"),
			  AddedFor(Melee, 3), 100.0f, 0.001f);
	TestEqual(TEXT("and one striking four does too"), AddedFor(Melee, 4),
			  100.0f, 0.001f);
	TestEqual(TEXT("a melee attack striking two ignores none of it"),
			  AddedFor(Melee, 2), 0.0f, 0.001f);
	TestEqual(TEXT("an attack that is not melee, striking three, ignores none"),
			  AddedFor(FGameplayTagContainer(), 3), 0.0f, 0.001f);
	TestEqual(TEXT("and a blow carrying no count ignores none"),
			  AddedFor(Melee, -1), 0.0f, 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// THREE ROWS THAT NEEDED NO NEW MECHANISM OF THEIR OWN. Issue #1515. Each test
// reads its row out of the table the game loads, so a row authored with the
// wrong stat, bucket, value, tag, condition or scale fails here rather than
// granting the wrong thing in play.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveStartingNodePerHitRowTest,
	"Cataclysm.Passives.TheRavagersStartingNodeGrantsFervourForEachEnemyItsAttacksHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_basic_spine_000`, the Ravager's starting node, on a real character:
 * its fourth row. Issue #1515.
 *
 * "Enemies in reach generate Fervour: 1 for each enemy your attacks hit, and 1
 * per second for every enemy within 4 metres of you."
 *
 * THE ROW HOLDS ONE AND NOTHING IS COUNTED HERE. The enemies are counted where
 * the blows land, by `UCataclysmFervour::GainForEnemiesHit`, whose own tests in
 * `CataclysmSkillTemplateTests.cpp` record this modifier directly. This reads
 * the real row where that function asks for it: through `StatForSkill`.
 */
bool FCataclysmPassiveStartingNodePerHitRowTest::RunTest(const FString&)
{
	using namespace CataclysmRavagerFervourTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, FName(StartingNode));
	TestEqual(TEXT("the starting node grants four stats"), Effects.Num(), 4);

	const FCataclysmPassiveEffectRow* PerHit = nullptr;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		if (Row && Row->Stat == TEXT("fervour_per_enemy_hit"))
		{
			PerHit = Row;
		}
	}
	if (!TestNotNull(TEXT("and one of them is Fervour for each enemy hit"), PerHit))
	{
		return false;
	}
	TestEqual(TEXT("stated as a flat count"), PerHit->ValueKind,
			  FString(TEXT("flat")));
	TestEqual(TEXT("of one"), PerHit->ValuePerPoint, 1.0f);
	TestEqual(TEXT("for any attack"), PerHit->RequiredTags, FString());
	TestEqual(TEXT("with no condition"), PerHit->Condition, FString());
	TestEqual(TEXT("and no scale, because the enemies are counted where the "
				   "blows land"),
			  PerHit->Scale, FString());

	const FName Stat(UCataclysmFervour::PerEnemyHitStat);
	const auto PerEnemy = [&Player, &Stat]()
	{
		return Player.AbilitySystem->StatForSkill(Stat, FGameplayTagContainer(),
												  0.0f);
	};

	Hold(Player, TMap<FName, int32>());
	TestEqual(TEXT("a Ravager without the node earns nothing for an enemy hit"),
			  PerEnemy(), 0.0f, 0.001f);

	Hold(Player, {{FName(StartingNode), 1}});
	TestEqual(TEXT("holding the starting node earns one for each enemy hit"),
			  PerEnemy(), 1.0f, 0.001f);

	Hold(Player, TMap<FName, int32>());
	TestEqual(TEXT("and giving the point back takes it away"), PerEnemy(), 0.0f,
			  0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassivePressTheAdvantageRowTest,
	"Cataclysm.Passives.PressTheAdvantageGrantsSpellDamageForEachMinionHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_basic_a_b2` Press the Advantage, read out of the tables the game
 * loads. Issue #1515.
 *
 * "+2% increased Spell Damage per point for each minion you have."
 *
 * THROUGH THE TREE'S OWN ACCUMULATION AND THE STAT PIPELINE, the way the test of
 * The Final Pact's second option reads its rows, with the number of minions
 * stated in the conditions. Eight points and three minions are 48 percentage
 * points, and no minions are none.
 */
bool FCataclysmPassivePressTheAdvantageRowTest::RunTest(const FString&)
{
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_basic_a_b2"));

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("Press the Advantage grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is spell damage"), Effects[0]->Stat,
			  FString(TEXT("spell_damage")));
	TestEqual(TEXT("stated as an increase"), Effects[0]->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("of two a point"), Effects[0]->ValuePerPoint, 2.0f);
	TestEqual(TEXT("for each minion held"), Effects[0]->Scale,
			  FString(TEXT("minions_held")));
	TestEqual(TEXT("one minion at a time"), Effects[0]->ScaleStep, 1.0f);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 8);
	const TMap<FName, TArray<FCataclysmStatModifier>> Granted =
		UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
											{FName(TEXT("Demonic"))});
	const TArray<FCataclysmStatModifier>* SpellDamage =
		Granted.Find(FName(TEXT("spell_damage")));
	if (!TestNotNull(TEXT("eight points grant spell damage"), SpellDamage))
	{
		return false;
	}

	const auto IncreasesWith = [SpellDamage](int32 Minions)
	{
		FCataclysmStatConditions State;
		State.MinionsHeld = Minions;
		return UCataclysmStatPipeline::Evaluate(
			100.0f, *SpellDamage, FGameplayTagContainer(), State).SumOfIncreases;
	};

	TestEqual(TEXT("with no minions it grants nothing"), IncreasesWith(0), 0.0f,
			  0.001f);
	TestEqual(TEXT("with three it grants 48 percentage points: two a point, "
				   "eight points, three minions"),
			  IncreasesWith(3), 48.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveHeadlongSecondClauseRowTest,
	"Cataclysm.Passives.HeadlongsFirstMeleeAttackAfterMovingFiveMetresGainsItsIncrease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The First Onslaught's third option, Headlong, read out of the tables the game
 * loads: its second clause. Issue #1515.
 *
 * "+25% increased Movement Speed, and your first melee attack after moving 5
 * metres deals 50% increased damage."
 *
 * THE CONDITION IS HOW FAR THE ATTACKER MOVED BEFORE THE ATTACK, measured when
 * the skill is paid for and reset at each attack, which is what makes it the
 * first attack after moving. It holds at five metres or more and refuses a blow
 * that carries no distance. Read through the tree's own accumulation and the
 * stat pipeline, as The Final Pact's test reads its option.
 */
bool FCataclysmPassiveHeadlongSecondClauseRowTest::RunTest(const FString&)
{
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Capstone(TEXT("Ravager_capstone_25"));

	const FCataclysmPassiveEffectRow* Clause = nullptr;
	for (const FCataclysmPassiveEffectRow* Row :
		 UCataclysmPassiveTree::EffectsFor(EffectTable, Capstone))
	{
		if (Row && Row->Option == 3 && Row->Stat == TEXT("attack_damage"))
		{
			Clause = Row;
		}
	}
	if (!TestNotNull(TEXT("Headlong has an attack damage row"), Clause))
	{
		return false;
	}
	TestEqual(TEXT("stated as an increase"), Clause->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("of fifty"), Clause->ValuePerPoint, 50.0f);
	TestEqual(TEXT("for melee attacks only"), Clause->RequiredTags,
			  FString(TEXT("Type.Melee")));
	TestEqual(TEXT("after moving at least"), Clause->Condition,
			  FString(TEXT("metres_moved_before_attack")));
	TestEqual(TEXT("five metres"), Clause->ConditionValue, 5.0f);

	FCataclysmPassiveAllocation Picked;
	Picked.Add(Capstone, 1);
	Picked.SetChosenOption(Capstone, 3);
	const TMap<FName, TArray<FCataclysmStatModifier>> Granted =
		UCataclysmPassiveTree::ModifiersFor(Picked, NodeTable, EffectTable,
											{FName(TEXT("Demonic"))});
	const TArray<FCataclysmStatModifier>* AttackDamage =
		Granted.Find(FName(TEXT("attack_damage")));
	if (!TestNotNull(TEXT("choosing Headlong grants attack damage"), AttackDamage))
	{
		return false;
	}

	FGameplayTagContainer Melee;
	const FGameplayTag MeleeTag = UCataclysmDamageCalculation::MeleeTag();
	if (MeleeTag.IsValid())
	{
		Melee.AddTag(MeleeTag);
	}
	if (!TestEqual(TEXT("the melee tag exists"), Melee.Num(), 1))
	{
		return false;
	}

	const auto IncreasesAfter = [AttackDamage](float Metres,
											   const FGameplayTagContainer& Tags)
	{
		FCataclysmStatConditions State;
		State.MetresMovedBeforeBlow = Metres;
		return UCataclysmStatPipeline::Evaluate(100.0f, *AttackDamage, Tags, State)
			.SumOfIncreases;
	};

	TestEqual(TEXT("a melee attack after moving six metres gains 50"),
			  IncreasesAfter(6.0f, Melee), 50.0f, 0.001f);
	TestEqual(TEXT("and after exactly five, because the condition is at least"),
			  IncreasesAfter(5.0f, Melee), 50.0f, 0.001f);
	TestEqual(TEXT("after four it gains nothing"), IncreasesAfter(4.0f, Melee),
			  0.0f, 0.001f);
	TestEqual(TEXT("an attack that is not melee gains nothing after six"),
			  IncreasesAfter(6.0f, FGameplayTagContainer()), 0.0f, 0.001f);
	TestEqual(TEXT("and a blow that carries no distance gains nothing"),
			  IncreasesAfter(-1.0f, Melee), 0.0f, 0.001f);

	return true;
}

// ---- Two death rules that cost nothing: Long Hold and Fed by the Fallen -----

namespace CataclysmDeathRewardsTest
{
	using namespace CataclysmFourRowTest;

	/** A flat row of one stat, the shape each capstone option's row will take. */
	FCataclysmStatModifier Flat(float Value)
	{
		FCataclysmStatModifier Made;
		Made.Bucket = ECataclysmStatBucket::Flat;
		Made.Source = ECataclysmModifierSource::PassiveKeystone;
		Made.Value = Value;
		return Made;
	}

	/**
	 * Record these stats as the character's stat lines, one flat row each.
	 *
	 * STATED HERE BECAUSE THE ASSET DOES NOT CARRY THE ROWS YET: they follow in a
	 * later turn of the design workbook. WHOLESALE, as `SetStatInputs` always
	 * is. The kill and death routes read nothing else off a stat line: health,
	 * its maximum and the pool are attributes, and a stat with nothing recorded
	 * answers its attribute.
	 */
	void Record(FRealCharacter& Player, const TMap<FName, float>& FlatByStat)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		for (const TPair<FName, float>& Each : FlatByStat)
		{
			FCataclysmStatInputs& Line = Lines.FindOrAdd(Each.Key);
			Line.Base = 0.0f;
			Line.Modifiers.Add(Flat(Each.Value));
		}
		Player.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	}

	/** Health as a share of the maximum, so a reading names no class's pool size. */
	float ShareOfMaximum(const FRealCharacter& Player, float Health)
	{
		const float Maximum = Player.AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		return Maximum > 0.0f ? Health / Maximum : -1.0f;
	}

	float MaximumFervourOf(const FRealCharacter& Player)
	{
		return Player.AbilitySystem->GetNumericAttribute(
			UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute());
	}

	/** How far this body stands from the player, in metres. */
	float MetresFrom(const FRealCharacter& Player, const AActor* Other)
	{
		return FVector::Dist(Player.Character->GetActorLocation(),
							 Other->GetActorLocation()) / 100.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLongHoldRestoresOnAKillTest,
	"Cataclysm.DeathRewards.LongHoldRestoresHealthOnAKillAndNothingOnADeathItDidNotCause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Ravager's Long Hold capstone option on a real character. Issue #1515.
 *
 * "Killing an enemy restores 5% of your maximum health."
 *
 * THE BREAK THIS IS FOR: paying the restoration on a death this character did
 * not cause. The kill comes first, so a version that restores on no death at
 * all fails as well, and the kill at full health holds the ceiling.
 *
 * EVERY READING IS A SHARE OF THE MAXIMUM, so no assertion depends on how much
 * health a Ravager has at the level the test world gives it.
 */
bool FCataclysmLongHoldRestoresOnAKillTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmSpenderTest;
	using namespace CataclysmRavagerFervourTest;
	using namespace CataclysmDeathRewardsTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager"), Player.IsComplete()))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Killed = CataclysmApplierDeathTest::SpawnEnemy(
		World, FVector(3.0f * Metre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* DiedAnyway = CataclysmApplierDeathTest::SpawnEnemy(
		World, FVector(0.0f, 3.0f * Metre, 0.0f));
	ACataclysmEnemyCharacter* KilledAtFull = CataclysmApplierDeathTest::SpawnEnemy(
		World, FVector(-3.0f * Metre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("an enemy to kill"), Killed)
		|| !TestNotNull(TEXT("an enemy that dies to nobody"), DiedAnyway)
		|| !TestNotNull(TEXT("an enemy to kill at full health"), KilledAtFull))
	{
		return false;
	}

	Record(Player, {{FName(UCataclysmFervour::HealthRestoredOnKillAtNoCostStat), 5.0f}});
	HalfHealth(Player);
	GiveFervour(Player, 50.0f);

	const float BeforeKill = HealthOf(Player);
	CataclysmApplierDeathTest::KilledByThePlayer(Player, Killed);
	TestEqual(TEXT("a kill restores five per cent of maximum health"),
			  ShareOfMaximum(Player, HealthOf(Player) - BeforeKill), 0.05f, 0.001f);
	TestEqual(TEXT("and spends no Fervour"), FervourOf(Player), 50.0f, 0.001f);

	const float BeforeOtherDeath = HealthOf(Player);
	UCataclysmCombatEvents::NoteDeath(DiedAnyway);
	TestEqual(TEXT("a death this Ravager did not cause restores nothing"),
			  ShareOfMaximum(Player, HealthOf(Player) - BeforeOtherDeath), 0.0f,
			  0.001f);

	Player.AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), MaxHealthOf(Player));
	CataclysmApplierDeathTest::KilledByThePlayer(Player, KilledAtFull);
	TestEqual(TEXT("and a kill at full health leaves it at full"),
			  ShareOfMaximum(Player, HealthOf(Player)), 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLongHoldBeforeWrungOutTest,
	"Cataclysm.DeathRewards.LongHoldHealsBeforeWrungOutBuysAnything",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Long Hold and Wrung Out held together. Issue #1515.
 *
 * RULED 2026-09-17 under the project owner's delegation: the restoration that
 * costs nothing runs first. A Ravager three per cent below full is healed to
 * full by Long Hold's five, and Wrung Out, finding full health, spends nothing.
 * In the other order Wrung Out would spend five Fervour to restore the same
 * three per cent.
 *
 * THE FIRST KILL IS THE CONTROL. From half health both run: five per cent at no
 * cost and six bought with five Fervour. Without it, "no Fervour was spent"
 * could pass because Wrung Out never ran at all.
 */
bool FCataclysmLongHoldBeforeWrungOutTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmSpenderTest;
	using namespace CataclysmRavagerFervourTest;
	using namespace CataclysmDeathRewardsTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager"), Player.IsComplete()))
	{
		return false;
	}

	ACataclysmEnemyCharacter* First = CataclysmApplierDeathTest::SpawnEnemy(
		World, FVector(3.0f * Metre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Second = CataclysmApplierDeathTest::SpawnEnemy(
		World, FVector(0.0f, 3.0f * Metre, 0.0f));
	if (!TestNotNull(TEXT("a first enemy to kill"), First)
		|| !TestNotNull(TEXT("a second enemy to kill"), Second))
	{
		return false;
	}

	Record(Player, {
		{FName(UCataclysmFervour::HealthRestoredOnKillAtNoCostStat), 5.0f},
		{FName(UCataclysmFervour::HealthRestoredOnKillStat),
		 static_cast<float>(WrungOutPoints)}});
	GiveFervour(Player, 50.0f);

	HalfHealth(Player);
	const float BeforeFirst = HealthOf(Player);
	CataclysmApplierDeathTest::KilledByThePlayer(Player, First);
	TestEqual(TEXT("from half health both run: five per cent free and six bought"),
			  ShareOfMaximum(Player, HealthOf(Player) - BeforeFirst), 0.11f, 0.001f);
	TestEqual(TEXT("and the six cost five Fervour"), FervourOf(Player), 45.0f, 0.001f);

	Player.AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(),
		MaxHealthOf(Player) * 0.97f);
	CataclysmApplierDeathTest::KilledByThePlayer(Player, Second);
	TestEqual(TEXT("three per cent below full, the kill heals to full"),
			  ShareOfMaximum(Player, HealthOf(Player)), 1.0f, 0.001f);
	TestEqual(TEXT("and Wrung Out, finding full health, spends no Fervour"),
			  FervourOf(Player), 45.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFedByTheFallenRadiusTest,
	"Cataclysm.DeathRewards.FedByTheFallenGrantsFervourForAnEnemyDyingWithinTenMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Ritualist's Fed by the Fallen capstone option on a real character.
 * Issue #1515.
 *
 * "You gain 10 Fervour whenever an enemy dies within 10 metres of you."
 *
 * THE BREAK THIS IS FOR: the worn rows' three metres read in place of ten. Six
 * metres is inside ten and outside three, so both deaths there fail under it,
 * and twelve is outside both.
 *
 * WHOEVER KILLED IT: the first enemy dies to nobody here and the second to this
 * Ritualist. Ruled 2026-09-17: any enemy's death counts, the character's own
 * kills included.
 */
bool FCataclysmFedByTheFallenRadiusTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmRavagerFervourTest;
	using namespace CataclysmDeathRewardsTest;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist"), Player.IsComplete()))
	{
		return false;
	}

	ACataclysmEnemyCharacter* SixAway = CataclysmApplierDeathTest::SpawnEnemy(
		World, FVector(6.0f * Metre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* KilledSixAway = CataclysmApplierDeathTest::SpawnEnemy(
		World, FVector(0.0f, 6.0f * Metre, 0.0f));
	ACataclysmEnemyCharacter* TwelveAway = CataclysmApplierDeathTest::SpawnEnemy(
		World, FVector(-12.0f * Metre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* AtMaximum = CataclysmApplierDeathTest::SpawnEnemy(
		World, FVector(0.0f, -6.0f * Metre, 0.0f));
	if (!TestNotNull(TEXT("an enemy six metres away"), SixAway)
		|| !TestNotNull(TEXT("an enemy to kill six metres away"), KilledSixAway)
		|| !TestNotNull(TEXT("an enemy twelve metres away"), TwelveAway)
		|| !TestNotNull(TEXT("an enemy for the full bar"), AtMaximum))
	{
		return false;
	}
	if (!TestTrue(TEXT("six metres is inside ten and outside three"),
				  MetresFrom(Player, SixAway) > 5.5f && MetresFrom(Player, SixAway) < 6.5f
				  && MetresFrom(Player, KilledSixAway) > 5.5f
				  && MetresFrom(Player, KilledSixAway) < 6.5f
				  && MetresFrom(Player, AtMaximum) > 5.5f
				  && MetresFrom(Player, AtMaximum) < 6.5f)
		|| !TestTrue(TEXT("and twelve is outside ten"),
					 MetresFrom(Player, TwelveAway) > 11.5f))
	{
		return false;
	}

	Record(Player, {{FName(UCataclysmFervour::OnEnemyDeathNearbyStat), 10.0f}});

	// AN EMPTY BAR TO START, so a pool that began full cannot read as a death
	// that granted nothing.
	GiveFervour(Player, -FervourOf(Player));
	if (!TestEqual(TEXT("the bar starts empty"), FervourOf(Player), 0.0f, 0.001f)
		|| !TestTrue(TEXT("and can hold more than twenty"),
					 MaximumFervourOf(Player) > 20.0f))
	{
		return false;
	}

	const float BeforeSix = FervourOf(Player);
	UCataclysmCombatEvents::NoteDeath(SixAway);
	TestEqual(TEXT("an enemy dying six metres away, killed by nobody here, grants ten"),
			  FervourOf(Player) - BeforeSix, 10.0f, 0.001f);

	// THE KILL IS SPLIT FROM ITS ANNOUNCEMENT HERE, so the test can show the blow
	// is on record as the Ritualist's before the death is announced. Without that
	// check, a blow that recorded nobody would make this a second death killed by
	// nobody, and the case would pass without being the case it names.
	const float BeforeKilled = FervourOf(Player);
	UCataclysmSkillEffects::ApplyHit(Player.Character, KilledSixAway,
									 CataclysmApplierDeathTest::FullSwing,
									 FGameplayTagContainer());
	const UCataclysmAbilitySystemComponent* VictimSystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(KilledSixAway));
	if (!TestTrue(TEXT("the killing blow is on record as the Ritualist's"),
				  VictimSystem && VictimSystem->GetLastBlow().IsOnRecord()
				  && VictimSystem->GetLastBlow().Attacker.Get() == Player.Character))
	{
		return false;
	}
	UCataclysmCombatEvents::NoteDeath(KilledSixAway);
	TestEqual(TEXT("and one this Ritualist kills there grants ten as well"),
			  FervourOf(Player) - BeforeKilled, 10.0f, 0.001f);

	const float BeforeTwelve = FervourOf(Player);
	UCataclysmCombatEvents::NoteDeath(TwelveAway);
	TestEqual(TEXT("one dying twelve metres away grants nothing"),
			  FervourOf(Player) - BeforeTwelve, 0.0f, 0.001f);

	GiveFervour(Player, MaximumFervourOf(Player) - FervourOf(Player));
	if (!TestEqual(TEXT("the bar is full before the last death"),
				   FervourOf(Player), MaximumFervourOf(Player), 0.001f))
	{
		return false;
	}
	UCataclysmCombatEvents::NoteDeath(AtMaximum);
	TestEqual(TEXT("and a full bar stays at its maximum"),
			  FervourOf(Player), MaximumFervourOf(Player), 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFedByTheFallenOwnMinionTest,
	"Cataclysm.DeathRewards.FedByTheFallenGrantsNothingForTheRitualistsOwnMinion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "WHENEVER AN ENEMY DIES", AND THE RITUALIST'S OWN MINION IS NOT AN ENEMY.
 * Issue #1515. Ruled 2026-09-17 under the project owner's delegation, the
 * rule the worn rows' nearby death already follows: a minion dying beside its
 * summoner is announced exactly as an enemy's death is.
 *
 * THE BREAK THIS IS FOR: letting any death through Fed by the Fallen's branch.
 * The enemy comes after, as near, so a version refusing every death fails too.
 *
 * THE MINION DIES BY THE ROUTE PLAY USES: health written to zero reaches the
 * body's own death handling, which announces the death.
 */
bool FCataclysmFedByTheFallenOwnMinionTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmRavagerFervourTest;
	using namespace CataclysmDeathRewardsTest;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = CataclysmFourRowTest::Spawn(World);
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestTrue(TEXT("a possessed Ritualist"), Player.IsComplete())
		|| !TestNotNull(TEXT("the announcements"), Events))
	{
		return false;
	}

	Record(Player, {{FName(UCataclysmFervour::OnEnemyDeathNearbyStat), 10.0f}});
	GiveFervour(Player, -FervourOf(Player));
	if (!TestEqual(TEXT("the bar starts empty"), FervourOf(Player), 0.0f, 0.001f))
	{
		return false;
	}

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Player.Character, FVector(2.0f * Metre, 0.0f, 0.0f), /*Lifetime=*/60.0f,
		/*bBurns=*/false);
	UAbilitySystemComponent* ImpSystem = UCataclysmTargeting::AbilitySystemOf(Imp);
	if (!TestNotNull(TEXT("a minion"), Imp)
		|| !TestNotNull(TEXT("with an ability system"), ImpSystem)
		|| !TestEqual(TEXT("on the Ritualist's side"),
					  static_cast<int32>(UCataclysmTeams::AttitudeBetween(
						  Player.Character, Imp)),
					  static_cast<int32>(ETeamAttitude::Friendly))
		|| !TestTrue(TEXT("and inside ten metres"), MetresFrom(Player, Imp) < 10.0f))
	{
		return false;
	}

	const float BeforeMinion = FervourOf(Player);
	const int32 DeathsBefore = static_cast<int32>(Events->DeathsSent());
	ImpSystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
	if (!TestTrue(TEXT("the minion died"), UCataclysmSkillEffects::IsDead(Imp))
		|| !TestEqual(TEXT("and its death was announced"),
					  static_cast<int32>(Events->DeathsSent()), DeathsBefore + 1))
	{
		return false;
	}
	TestEqual(TEXT("the Ritualist's own minion dying two metres away grants nothing"),
			  FervourOf(Player) - BeforeMinion, 0.0f, 0.001f);

	ACataclysmEnemyCharacter* Enemy = CataclysmApplierDeathTest::SpawnEnemy(
		World, FVector(0.0f, 2.0f * Metre, 0.0f));
	if (!TestNotNull(TEXT("an enemy"), Enemy)
		|| !TestEqual(TEXT("on the other side"),
					  static_cast<int32>(UCataclysmTeams::AttitudeBetween(
						  Player.Character, Enemy)),
					  static_cast<int32>(ETeamAttitude::Hostile)))
	{
		return false;
	}
	const float BeforeEnemy = FervourOf(Player);
	UCataclysmCombatEvents::NoteDeath(Enemy);
	TestEqual(TEXT("and an enemy dying as near grants ten"),
			  FervourOf(Player) - BeforeEnemy, 10.0f, 0.001f);
	return true;
}

// ---- The four rows of 2026-09-17: two stat scales and two death rules -------

namespace CataclysmFourRowsReadTest
{
	/** The one row a node or capstone option carries for this stat, or null. */
	const FCataclysmPassiveEffectRow* RowFor(const UDataTable* EffectTable,
											 const TCHAR* Node, int32 Option,
											 const TCHAR* Stat)
	{
		const FCataclysmPassiveEffectRow* Found = nullptr;
		for (const FCataclysmPassiveEffectRow* Row :
			 UCataclysmPassiveTree::EffectsFor(EffectTable, FName(Node)))
		{
			if (Row && Row->Option == Option && Row->Stat == Stat)
			{
				Found = Row;
			}
		}
		return Found;
	}

	/** What spending these points, and choosing this option, grants one stat. */
	const TArray<FCataclysmStatModifier>* Granted(
		TMap<FName, TArray<FCataclysmStatModifier>>& Out,
		const UDataTable* NodeTable, const UDataTable* EffectTable,
		const TCHAR* Node, int32 Points, int32 Option, const TCHAR* Stat)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(Node), Points);
		if (Option > 0)
		{
			Allocation.SetChosenOption(FName(Node), Option);
		}
		Out = UCataclysmPassiveTree::ModifiersFor(Allocation, NodeTable, EffectTable,
												  {FName(TEXT("Demonic"))});
		return Out.Find(FName(Stat));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveWeightAgainstThemRowTest,
	"Cataclysm.Passives.WeightAgainstThemGrantsAttackDamageForEveryTwoPercentOfDamageReduction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Third Onslaught's second option, Weight Against Them, read out of the tables
 * the game loads. Issue #1515.
 *
 * "Your Armor is also offence: +1% increased Attack Damage for every 2% of Damage
 * Reduction you have."
 *
 * Through the tree's own accumulation and the stat pipeline, as Headlong's test
 * reads its clause, with the damage reduction stated in the conditions. Where a
 * real character's reading comes from, and the cap it stops at, is
 * `Cataclysm.ConditionalDamage.AttackDamageGrowsWithTheDamageReductionTheCharacterHasUpToItsCap`.
 */
bool FCataclysmPassiveWeightAgainstThemRowTest::RunTest(const FString&)
{
	using namespace CataclysmFourRowsReadTest;

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FCataclysmPassiveEffectRow* Row =
		RowFor(EffectTable, TEXT("Ravager_capstone_100"), 2, TEXT("attack_damage"));
	if (!TestNotNull(TEXT("Weight Against Them has an attack damage row"), Row))
	{
		return false;
	}
	TestEqual(TEXT("stated as an increase"), Row->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("of one"), Row->ValuePerPoint, 1.0f);
	TestEqual(TEXT("for no skill in particular"), Row->RequiredTags, FString());
	TestEqual(TEXT("under no condition"), Row->Condition, FString());
	TestEqual(TEXT("scaled by damage reduction"), Row->Scale,
			  FString(TEXT("damage_reduction")));
	TestEqual(TEXT("in steps of two"), Row->ScaleStep, 2.0f);

	TMap<FName, TArray<FCataclysmStatModifier>> Out;
	const TArray<FCataclysmStatModifier>* AttackDamage = Granted(
		Out, NodeTable, EffectTable, TEXT("Ravager_capstone_100"), 1, 2,
		TEXT("attack_damage"));
	if (!TestNotNull(TEXT("choosing Weight Against Them grants attack damage"),
					 AttackDamage))
	{
		return false;
	}

	const auto IncreasesWith = [AttackDamage](float Percent)
	{
		FCataclysmStatConditions State;
		State.DamageReductionPercent = Percent;
		return UCataclysmStatPipeline::Evaluate(100.0f, *AttackDamage,
												FGameplayTagContainer(), State)
			.SumOfIncreases;
	};

	TestEqual(TEXT("ten per cent of damage reduction grants five"),
			  IncreasesWith(10.0f), 5.0f, 0.001f);
	TestEqual(TEXT("nine grants four, in whole steps"), IncreasesWith(9.0f), 4.0f,
			  0.001f);
	TestEqual(TEXT("and an unknown reading grants nothing"), IncreasesWith(-1.0f),
			  0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveDrawnDeepRowTest,
	"Cataclysm.Passives.DrawnDeepGrantsSpellDamageForEveryFullTwoHundredMaximumMana",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_basic_d_a2` Drawn Deep, read out of the tables the game loads.
 * Issue #1515.
 *
 * "+1% increased Spell Damage per point for every full 200 maximum mana you have."
 *
 * Six points and 450 maximum mana are two full steps of six, twelve percentage
 * points; 199 is not one full step. Where a real character's reading comes from
 * is `Cataclysm.ConditionalDamage.SpellDamageGrowsWithTheMaximumManaAndNotTheManaInHand`.
 */
bool FCataclysmPassiveDrawnDeepRowTest::RunTest(const FString&)
{
	using namespace CataclysmFourRowsReadTest;

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, FName(TEXT("Ritualist_basic_d_a2")));
	if (!TestEqual(TEXT("Drawn Deep grants one stat"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is spell damage"), Effects[0]->Stat,
			  FString(TEXT("spell_damage")));
	TestEqual(TEXT("stated as an increase"), Effects[0]->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("of one a point"), Effects[0]->ValuePerPoint, 1.0f);
	TestEqual(TEXT("scaled by maximum mana"), Effects[0]->Scale,
			  FString(TEXT("max_mana")));
	TestEqual(TEXT("in steps of 200"), Effects[0]->ScaleStep, 200.0f);

	TMap<FName, TArray<FCataclysmStatModifier>> Out;
	const TArray<FCataclysmStatModifier>* SpellDamage = Granted(
		Out, NodeTable, EffectTable, TEXT("Ritualist_basic_d_a2"), 6, 0,
		TEXT("spell_damage"));
	if (!TestNotNull(TEXT("six points grant spell damage"), SpellDamage))
	{
		return false;
	}

	const auto IncreasesWith = [SpellDamage](float Mana)
	{
		FCataclysmStatConditions State;
		State.MaximumMana = Mana;
		return UCataclysmStatPipeline::Evaluate(100.0f, *SpellDamage,
												FGameplayTagContainer(), State)
			.SumOfIncreases;
	};

	TestEqual(TEXT("450 maximum mana at six points grants twelve"),
			  IncreasesWith(450.0f), 12.0f, 0.001f);
	TestEqual(TEXT("399 grants six, one full step"), IncreasesWith(399.0f), 6.0f,
			  0.001f);
	TestEqual(TEXT("and 199 grants nothing"), IncreasesWith(199.0f), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveLongHoldRowTest,
	"Cataclysm.Passives.LongHoldGrantsFivePercentOfMaximumHealthOnAKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Second Onslaught's third option, Long Hold, read out of the tables the game
 * loads. Issue #1515.
 *
 * "Killing an enemy restores 5% of your maximum health."
 *
 * THE ROW, AND THE ATTRIBUTE IT LANDS ON. A flat stat with no attribute in
 * `UCataclysmPlayerClassStats::StatToAttribute` would be dropped by `ApplyTo`,
 * so the map entry is asserted beside the row. What a kill does with the stat is
 * `Cataclysm.DeathRewards.LongHoldRestoresHealthOnAKillAndNothingOnADeathItDidNotCause`.
 */
bool FCataclysmPassiveLongHoldRowTest::RunTest(const FString&)
{
	using namespace CataclysmFourRowsReadTest;

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FCataclysmPassiveEffectRow* Row = RowFor(
		EffectTable, TEXT("Ravager_capstone_50"), 3,
		UCataclysmFervour::HealthRestoredOnKillAtNoCostStat);
	if (!TestNotNull(TEXT("Long Hold has a row"), Row))
	{
		return false;
	}
	TestEqual(TEXT("stated flat"), Row->ValueKind, FString(TEXT("flat")));
	TestEqual(TEXT("of five"), Row->ValuePerPoint, 5.0f);
	TestEqual(TEXT("under no condition"), Row->Condition, FString());
	TestEqual(TEXT("and on no scale"), Row->Scale, FString());

	TMap<FName, TArray<FCataclysmStatModifier>> Out;
	const TArray<FCataclysmStatModifier>* Restored = Granted(
		Out, NodeTable, EffectTable, TEXT("Ravager_capstone_50"), 1, 3,
		UCataclysmFervour::HealthRestoredOnKillAtNoCostStat);
	if (!TestNotNull(TEXT("choosing Long Hold grants the stat"), Restored))
	{
		return false;
	}
	TestEqual(TEXT("worth five per cent"),
			  UCataclysmStatPipeline::Evaluate(0.0f, *Restored,
											   FGameplayTagContainer()).Final,
			  5.0f, 0.001f);
	TestNotNull(TEXT("and the stat has an attribute for ApplyTo to write"),
				UCataclysmPlayerClassStats::StatToAttribute().Find(
					FString(UCataclysmFervour::HealthRestoredOnKillAtNoCostStat)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveFedByTheFallenRowTest,
	"Cataclysm.Passives.FedByTheFallenGrantsTenFervourForAnEnemyDyingNearby",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Second Pact's second option, Fed by the Fallen, read out of the tables the
 * game loads. Issue #1515.
 *
 * "You gain 10 Fervour whenever an enemy dies within 10 metres of you."
 *
 * The row and the attribute it lands on, as Long Hold's test reads its own. The
 * ten metres are a constant in code, not a column. What a death does with the
 * stat is `Cataclysm.DeathRewards.FedByTheFallenGrantsFervourForAnEnemyDyingWithinTenMetres`.
 */
bool FCataclysmPassiveFedByTheFallenRowTest::RunTest(const FString&)
{
	using namespace CataclysmFourRowsReadTest;

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FCataclysmPassiveEffectRow* Row = RowFor(
		EffectTable, TEXT("Ritualist_capstone_50"), 2,
		UCataclysmFervour::OnEnemyDeathNearbyStat);
	if (!TestNotNull(TEXT("Fed by the Fallen has a row"), Row))
	{
		return false;
	}
	TestEqual(TEXT("stated flat"), Row->ValueKind, FString(TEXT("flat")));
	TestEqual(TEXT("of ten"), Row->ValuePerPoint, 10.0f);
	TestEqual(TEXT("under no condition"), Row->Condition, FString());
	TestEqual(TEXT("and on no scale"), Row->Scale, FString());

	TMap<FName, TArray<FCataclysmStatModifier>> Out;
	const TArray<FCataclysmStatModifier>* Gained = Granted(
		Out, NodeTable, EffectTable, TEXT("Ritualist_capstone_50"), 1, 2,
		UCataclysmFervour::OnEnemyDeathNearbyStat);
	if (!TestNotNull(TEXT("choosing Fed by the Fallen grants the stat"), Gained))
	{
		return false;
	}
	TestEqual(TEXT("worth ten Fervour"),
			  UCataclysmStatPipeline::Evaluate(0.0f, *Gained,
											   FGameplayTagContainer()).Final,
			  10.0f, 0.001f);
	TestNotNull(TEXT("and the stat has an attribute for ApplyTo to write"),
				UCataclysmPlayerClassStats::StatToAttribute().Find(
					FString(UCataclysmFervour::OnEnemyDeathNearbyStat)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveNeverLetsGoRowTest,
	"Cataclysm.Passives.NeverLetsGoAlwaysCripplesAndAddsDamageAgainstCrippledEnemies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The First Onslaught's second option, Never Lets Go, read out of the tables the
 * game loads. Issue #1515.
 *
 * "Enemies you hit are Crippled for 4 seconds, and your attacks deal 20%
 * increased damage to Crippled enemies."
 *
 * ITS TWO ROWS ARE DELIVERED BY DIFFERENT ROUTES, which is why both are asserted
 * here. The Cripple chance carries no condition, so it folds into the gameplay
 * attribute that the ailment roll reads -- `UCataclysmAilments` asks
 * `GetCrippleChanceAttribute`, and a conditioned row would never reach it. The
 * attack damage row does carry a condition, so it is resolved at the blow with
 * the target's debuffs in hand and never touches an attribute at all.
 *
 * FOUR SECONDS IS NOT A ROW. Cripple already lasts four seconds in
 * `game/Data/StatusEffects.csv`, so the sentence's figure is true without a row
 * granting it, and nothing here pretends one does.
 */
bool FCataclysmPassiveNeverLetsGoRowTest::RunTest(const FString&)
{
	using namespace CataclysmFourRowsReadTest;

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FCataclysmPassiveEffectRow* Chance = RowFor(
		EffectTable, TEXT("Ravager_capstone_25"), 2, TEXT("cripple_chance"));
	if (!TestNotNull(TEXT("Never Lets Go has a cripple chance row"), Chance))
	{
		return false;
	}
	TestEqual(TEXT("stated flat"), Chance->ValueKind, FString(TEXT("flat")));
	TestEqual(TEXT("of a hundred"), Chance->ValuePerPoint, 100.0f);
	TestEqual(TEXT("under no condition, so it folds into the attribute"),
			  Chance->Condition, FString());
	TestEqual(TEXT("and on no scale"), Chance->Scale, FString());

	const FCataclysmPassiveEffectRow* Damage = RowFor(
		EffectTable, TEXT("Ravager_capstone_25"), 2, TEXT("attack_damage"));
	if (!TestNotNull(TEXT("and an attack damage row"), Damage))
	{
		return false;
	}
	TestEqual(TEXT("stated as an increase"), Damage->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("of twenty"), Damage->ValuePerPoint, 20.0f);
	TestEqual(TEXT("only against a crippled target"), Damage->Condition,
			  FString(TEXT("target_carries_cripple")));
	TestEqual(TEXT("for every attack rather than melee alone"),
			  Damage->RequiredTags, FString());

	TMap<FName, TArray<FCataclysmStatModifier>> Out;
	const TArray<FCataclysmStatModifier>* Chances = Granted(
		Out, NodeTable, EffectTable, TEXT("Ravager_capstone_25"), 1, 2,
		TEXT("cripple_chance"));
	if (!TestNotNull(TEXT("choosing Never Lets Go grants the cripple chance"),
					 Chances))
	{
		return false;
	}
	TestEqual(TEXT("worth a hundred"),
			  UCataclysmStatPipeline::Evaluate(0.0f, *Chances,
											   FGameplayTagContainer()).Final,
			  100.0f, 0.001f);
	TestNotNull(TEXT("and the chance has the attribute the ailment roll reads"),
				UCataclysmPlayerClassStats::StatToAttribute().Find(
					FString(TEXT("cripple_chance"))));

	const TArray<FCataclysmStatModifier>* AttackDamage =
		Out.Find(FName(TEXT("attack_damage")));
	if (!TestNotNull(TEXT("and the attack damage"), AttackDamage))
	{
		return false;
	}
	// ONE ROW AND NOT THE OTHER OPTIONS' TWO. The First Onslaught's first and
	// third options both grant attack damage as well, so a chosen option that
	// was not honoured would show up here as three modifiers.
	TestEqual(TEXT("one attack damage row, the chosen option's"),
			  AttackDamage->Num(), 1);

	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();
	if (!TestTrue(TEXT("the Cripple tag exists in the vocabulary"),
				  Cripple.IsValid()))
	{
		return false;
	}

	FCataclysmStatConditions Crippled;
	Crippled.TargetDebuffs.AddTag(Cripple);
	TestEqual(TEXT("twenty against a crippled target"),
			  UCataclysmStatPipeline::Evaluate(100.0f, *AttackDamage,
											   FGameplayTagContainer(), Crippled)
				  .SumOfIncreases,
			  20.0f, 0.001f);
	TestEqual(TEXT("and nothing against a target that is not"),
			  UCataclysmStatPipeline::Evaluate(100.0f, *AttackDamage,
											   FGameplayTagContainer(),
											   FCataclysmStatConditions())
				  .SumOfIncreases,
			  0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveEveryOneBurstsRowTest,
	"Cataclysm.Passives.EveryOneBurstsGrantsTheFlagThatExplodesAMinionOnItsDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_keystone_b_kB` Every One Bursts, read out of the tables the game
 * loads. Issue #1515.
 *
 * "Every minion explodes when it dies, as one destroyed to make room for another
 * does, with the radius and damage of the skill that brought it."
 *
 * A FLAG OF ONE, because the death path asks whether the stat is above zero
 * rather than how much of it there is. What a death then does with it is
 * `Cataclysm.MinionDeath.AFlaggedMinionsDeathExplodesAndHurtsOnlyWhatIsInsideTheRadius`;
 * this reads the row and the delivery.
 *
 * THE STAT HAS NO GAMEPLAY ATTRIBUTE ON PURPOSE, so the assertion that matters
 * for delivery is that the engine records it: a name missing from
 * `UCataclysmPlayerClassStats::StatsWithNoAttribute` is recorded nowhere, and
 * `StatForSkill` would answer nothing however well the row was written.
 */
bool FCataclysmPassiveEveryOneBurstsRowTest::RunTest(const FString&)
{
	using namespace CataclysmFourRowsReadTest;

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FCataclysmPassiveEffectRow* Row = RowFor(
		EffectTable, TEXT("Ritualist_keystone_b_kB"), 0,
		TEXT("minion_explodes_on_death"));
	if (!TestNotNull(TEXT("Every One Bursts has the explosion flag"), Row))
	{
		return false;
	}
	TestEqual(TEXT("stated flat"), Row->ValueKind, FString(TEXT("flat")));
	TestEqual(TEXT("of one"), Row->ValuePerPoint, 1.0f);
	TestEqual(TEXT("under no condition"), Row->Condition, FString());
	TestEqual(TEXT("and on no scale"), Row->Scale, FString());

	TMap<FName, TArray<FCataclysmStatModifier>> Out;
	const TArray<FCataclysmStatModifier>* Flag = Granted(
		Out, NodeTable, EffectTable, TEXT("Ritualist_keystone_b_kB"), 1, 0,
		TEXT("minion_explodes_on_death"));
	if (!TestNotNull(TEXT("taking the keystone grants the flag"), Flag))
	{
		return false;
	}
	TestEqual(TEXT("worth one"),
			  UCataclysmStatPipeline::Evaluate(0.0f, *Flag,
											   FGameplayTagContainer()).Final,
			  1.0f, 0.001f);
	TestTrue(TEXT("and the engine records the stat, which has no attribute"),
			 UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(
				 FString(TEXT("minion_explodes_on_death"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveVolatileRowTest,
	"Cataclysm.Passives.VolatileGrantsThreePerCentOfExplosionDamageAPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_basic_b_a2` Volatile, read out of the tables the game loads.
 * Issue #1515.
 *
 * "+3% increased damage of the explosion a minion leaves per point."
 *
 * EIGHT POINTS IS THE WHOLE NODE and +24%, which is worth asserting rather than
 * the single point: a row whose value per point was written as the total would
 * pass a one-point reading and be worth an eighth of what it says.
 *
 * WHICH EXPLOSION IT REACHES is `Cataclysm.MinionDeath.` -- the death's and the
 * summon cap's alike, because the stat is read inside `ACataclysmMinion::Explode`
 * rather than at either caller.
 */
bool FCataclysmPassiveVolatileRowTest::RunTest(const FString&)
{
	using namespace CataclysmFourRowsReadTest;

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FCataclysmPassiveEffectRow* Row = RowFor(
		EffectTable, TEXT("Ritualist_basic_b_a2"), 0,
		TEXT("minion_explosion_damage"));
	if (!TestNotNull(TEXT("Volatile has an explosion damage row"), Row))
	{
		return false;
	}
	TestEqual(TEXT("stated as an increase"), Row->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("of three a point"), Row->ValuePerPoint, 3.0f);
	TestEqual(TEXT("under no condition"), Row->Condition, FString());
	TestEqual(TEXT("and on no scale"), Row->Scale, FString());

	TMap<FName, TArray<FCataclysmStatModifier>> Out;
	const TArray<FCataclysmStatModifier>* Damage = Granted(
		Out, NodeTable, EffectTable, TEXT("Ritualist_basic_b_a2"), 8, 0,
		TEXT("minion_explosion_damage"));
	if (!TestNotNull(TEXT("eight points grant explosion damage"), Damage))
	{
		return false;
	}
	TestEqual(TEXT("eight points are twenty-four per cent"),
			  UCataclysmStatPipeline::Evaluate(100.0f, *Damage,
											   FGameplayTagContainer())
				  .SumOfIncreases,
			  24.0f, 0.001f);
	TestTrue(TEXT("and the engine records the stat, which has no attribute"),
			 UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(
				 FString(TEXT("minion_explosion_damage"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveRitualFocusRowTest,
	"Cataclysm.Passives.RitualFocusTakesASkillsManaCostToNothingWhileStandingStill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_keystone_d_kB` Ritual Focus, read out of the tables the game loads.
 * Issue #1515.
 *
 * "Skills you cast while standing still cost no mana."
 *
 * A REMOVAL RATHER THAN A REDUCTION, and the first removal row in
 * `game/Data/PassiveEffects.csv`. The pipeline clamps a Less multiplier at -99
 * on purpose, so a rule that says "no mana" cannot be written as a reduction at
 * all; twenty-five removals exist on the enchantment side and this is the shape
 * they use.
 *
 * BOTH STATES ARE MEASURED, and that is the point of the case. A removal under a
 * condition that is never true costs nothing and reads exactly like one that is
 * always true, if only the standing-still half is asked.
 *
 * THE COST IS A FIGURE THE CALLER SUPPLIES rather than one the character has,
 * which is why the mechanism added
 * `UCataclysmAbilitySystemComponent::StatAppliedTo`. This case runs the same
 * pipeline on the same figure, so it reads the row rather than the ability.
 */
bool FCataclysmPassiveRitualFocusRowTest::RunTest(const FString&)
{
	using namespace CataclysmFourRowsReadTest;

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FCataclysmPassiveEffectRow* Row = RowFor(
		EffectTable, TEXT("Ritualist_keystone_d_kB"), 0,
		UCataclysmSkillSlots::ManaCostStat);
	if (!TestNotNull(TEXT("Ritual Focus has a mana cost row"), Row))
	{
		return false;
	}
	TestEqual(TEXT("stated as a removal"), Row->ValueKind,
			  FString(TEXT("removed")));
	TestEqual(TEXT("of one, which is how a removal states itself"),
			  Row->ValuePerPoint, 1.0f);
	TestEqual(TEXT("while standing still"), Row->Condition,
			  FString(TEXT("while_stationary")));
	TestEqual(TEXT("and on no scale"), Row->Scale, FString());

	TMap<FName, TArray<FCataclysmStatModifier>> Out;
	const TArray<FCataclysmStatModifier>* Cost = Granted(
		Out, NodeTable, EffectTable, TEXT("Ritualist_keystone_d_kB"), 1, 0,
		UCataclysmSkillSlots::ManaCostStat);
	if (!TestNotNull(TEXT("taking the keystone grants the removal"), Cost))
	{
		return false;
	}

	// FORTY IS A SKILL'S STATED COST, handed in as the figure the stat applies
	// to, which is what `StatAppliedTo` does with a skill's own cost.
	const float Stated = 40.0f;

	FCataclysmStatConditions Still;
	Still.bIsMoving = false;
	Still.SecondsSinceMoved = 1.0f;
	TestEqual(TEXT("standing still, the skill costs nothing"),
			  UCataclysmStatPipeline::Evaluate(Stated, *Cost,
											   FGameplayTagContainer(), Still)
				  .Final,
			  0.0f, 0.001f);

	FCataclysmStatConditions Moving;
	Moving.bIsMoving = true;
	Moving.SecondsSinceMoved = 0.0f;
	TestEqual(TEXT("and moving, it costs what it states"),
			  UCataclysmStatPipeline::Evaluate(Stated, *Cost,
											   FGameplayTagContainer(), Moving)
				  .Final,
			  Stated, 0.001f);

	TestTrue(TEXT("and the engine records the stat, which has no attribute"),
			 UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(
				 FString(UCataclysmSkillSlots::ManaCostStat)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveConduitRowTest,
	"Cataclysm.Passives.ConduitGrantsTheFlagThatMakesAMinionsHitItsSummonersOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_keystone_spine_003` Conduit, read out of the tables the game loads.
 * Issue #1515.
 *
 * "Damage dealt by your minions counts as damage you dealt, for every effect of
 * yours that asks."
 *
 * THE ENGINE READ THIS STAT BEFORE ANYTHING GRANTED IT, which is the reason this
 * case exists rather than trusting the two that measure the crediting.
 * `UCataclysmCombatEvents::NoteBlow` asked for `minion_hits_count_as_yours` from
 * the morning of 2026-09-17; no row supplied it until this one, and both cases
 * that measure the keystone put the stat on by hand. So the keystone was
 * written, built and tested, and a player who took it got nothing. A case that
 * reads the ROW is what would have caught that.
 *
 * A FLAG OF ONE, as `Every One Bursts` is: the crediting asks whether the stat
 * stands above zero rather than how much of it there is.
 *
 * WHAT A CREDITED HIT THEN DOES is
 * `Cataclysm.CombatEvents.TheConduitKeystoneCreditsAMinionsHitAndKillToItsSummoner`
 * and its partner for a summoner without the keystone; this reads the row.
 */
bool FCataclysmPassiveConduitRowTest::RunTest(const FString&)
{
	using namespace CataclysmFourRowsReadTest;

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FCataclysmPassiveEffectRow* Row = RowFor(
		EffectTable, TEXT("Ritualist_keystone_spine_003"), 0,
		TEXT("minion_hits_count_as_yours"));
	if (!TestNotNull(TEXT("Conduit has the crediting flag"), Row))
	{
		return false;
	}
	TestEqual(TEXT("stated flat"), Row->ValueKind, FString(TEXT("flat")));
	TestEqual(TEXT("of one"), Row->ValuePerPoint, 1.0f);
	TestEqual(TEXT("under no condition"), Row->Condition, FString());
	TestEqual(TEXT("and on no scale"), Row->Scale, FString());

	TMap<FName, TArray<FCataclysmStatModifier>> Out;
	const TArray<FCataclysmStatModifier>* Flag = Granted(
		Out, NodeTable, EffectTable, TEXT("Ritualist_keystone_spine_003"), 1, 0,
		TEXT("minion_hits_count_as_yours"));
	if (!TestNotNull(TEXT("taking the keystone grants the flag"), Flag))
	{
		return false;
	}
	TestEqual(TEXT("worth one"),
			  UCataclysmStatPipeline::Evaluate(0.0f, *Flag,
											   FGameplayTagContainer()).Final,
			  1.0f, 0.001f);
	TestTrue(TEXT("and the engine records the stat, which has no attribute"),
			 UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(
				 FString(TEXT("minion_hits_count_as_yours"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSetAgainstItTest,
	"Cataclysm.Passives.SetAgainstItsOwnRowsTurnOnAtFiftyFervour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Set Against It works from the rows the game loads, at the Fervour they name.
 *
 * WHY IT DOES NOT READ AN ATTRIBUTE, unlike the armour test above. Both of this
 * node's rows are CONDITIONED, and a conditioned modifier is never folded into a
 * gameplay attribute -- it would be stale the moment the reading moved. So the
 * bonus exists only where something asks for the stat through the pipeline, and
 * that is what this asks.
 *
 * AND WHY IT READS THE ROWS RATHER THAN GRANTING THE STATS BY HAND. A test that
 * writes the two modifiers itself passes with no row in the data at all, which
 * is how `Ritualist_capstone_200#3` granted nothing from the day it was written
 * and how the Conduit keystone was read by the engine for a day before any row
 * supplied it. This one takes the node's rows out of the imported table, states
 * no figures of its own beyond what they carry, and fails with a message naming
 * the regeneration command when they are absent.
 *
 * THE THRESHOLD IS MOVED ACROSS, NOT JUST SATISFIED. One Fervour below the row's
 * own number the bonuses are absent; at it they are there. A condition stuck on
 * "true" passes the second half alone. Issue #1515.
 */
bool FCataclysmSetAgainstItTest::RunTest(const FString&)
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
	if (!TestNotNull(TEXT("the effect table loads"),
					 const_cast<UDataTable*>(EffectTable)))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	// THE NODE'S OWN ROWS. Two of them, one per bonus the sentence promises.
	const FName Node(TEXT("Ravager_basic_a_b1"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
	if (!TestEqual(TEXT("Set Against It carries two rows"), Effects.Num(), 2))
	{
		AddError(TEXT("The node's rows are missing from the data, so it grants "
					  "nothing in play however the condition behaves. Author "
					  "them in the Passive Effects sheet of "
					  "docs/All_Things_Cataclysm.xlsx and regenerate."));
		return false;
	}

	// THE FIGURES COME FROM THE ROWS AND NOT FROM HERE.
	float Threshold = -1.0f;
	float ReductionPerPoint = 0.0f;
	float DamagePerPoint = 0.0f;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		TestEqual(*FString::Printf(TEXT("%s is conditioned"), *Row->Stat),
				  Row->Condition,
				  FString(TEXT("class_resource_points_at_least")));
		Threshold = Row->ConditionValue;
		if (Row->Stat == FString(TEXT("damage_reduction")))
		{
			ReductionPerPoint = Row->ValuePerPoint;
		}
		else if (Row->Stat == FString(TEXT("attack_damage")))
		{
			DamagePerPoint = Row->ValuePerPoint;
		}
	}
	if (!TestTrue(*FString::Printf(TEXT("the rows state a threshold: %.0f"),
								   Threshold), Threshold > 0.0f)
		|| !TestTrue(TEXT("and both bonuses"),
					 ReductionPerPoint > 0.0f && DamagePerPoint > 0.0f))
	{
		return false;
	}

	constexpr int32 Points = 3;
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, Points);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	const auto HoldFervour = [AbilitySystem](float Amount)
	{
		AbilitySystem->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
			Amount);
	};
	const auto IncreaseIn = [AbilitySystem](const TCHAR* Stat)
	{
		return AbilitySystem->IncreasesForStat(FName(Stat),
											   FGameplayTagContainer());
	};

	// ONE SHORT OF WHAT THE ROW ASKS FOR: neither bonus is there.
	HoldFervour(Threshold - 1.0f);
	TestEqual(*FString::Printf(
				  TEXT("at %.0f Fervour there is no damage reduction bonus"),
				  Threshold - 1.0f),
			  IncreaseIn(TEXT("damage_reduction")), 0.0f, 0.0001f);
	TestEqual(*FString::Printf(
				  TEXT("and no attack damage bonus at %.0f"), Threshold - 1.0f),
			  IncreaseIn(TEXT("attack_damage")), 0.0f, 0.0001f);

	// AT IT: both, worth the row's own figure for every point spent.
	HoldFervour(Threshold);
	TestEqual(*FString::Printf(
				  TEXT("at %.0f Fervour the damage reduction bonus is there"),
				  Threshold),
			  IncreaseIn(TEXT("damage_reduction")),
			  ReductionPerPoint * Points / 100.0f, 0.0001f);
	TestEqual(*FString::Printf(TEXT("and the attack damage bonus at %.0f"),
							   Threshold),
			  IncreaseIn(TEXT("attack_damage")),
			  DamagePerPoint * Points / 100.0f, 0.0001f);

	return true;
}

// ---------------------------------------------------------------------------
// A maximum of one pool granting another, read out of the rows. Issue #1515.
//
// WHY THESE TWO EXIST. Every other test of Weight Bearing and Vessel puts the
// scaled modifier on by hand, and so would pass with no row in
// `game/Data/PassiveEffects.csv` at all -- which is how
// `Ritualist_capstone_200#3` and the Conduit keystone each came to grant nothing
// in play. These take the node's row out of the imported table, spend the point
// through the player state as the passive screen does, and state no figure of
// their own beyond the row's value and step.
//
// THE MAXIMUM IS MOVED, NOT JUST READ. The owner's delegation ruled that the
// grant tracks the maximum as it moves (docs/DECISIONS.md, 2026-09-23), so each
// test writes two maxima and watches the grant follow. The first is 37.6 steps,
// where rounding down (37), rounding to nearest (38) and not rounding at all
// (37.6) give three different answers, so only the ruled arithmetic passes.
//
// EACH GRANT IS A DIFFERENCE from the same character with the point given back,
// at the same maximum, so the class line's own figure never enters an assertion.
// ---------------------------------------------------------------------------

namespace CataclysmMaximumGrantTest
{
	/** The node's one row, checked for the kind, stat and scale it must carry. */
	const FCataclysmPassiveEffectRow* OneScaledRow(
		FAutomationTestBase& Test, const UDataTable* EffectTable, FName Node,
		const TCHAR* Stat, const TCHAR* Scale)
	{
		const TArray<const FCataclysmPassiveEffectRow*> Effects =
			UCataclysmPassiveTree::EffectsFor(EffectTable, Node);
		if (!Test.TestEqual(*FString::Printf(TEXT("%s carries one row"),
											 *Node.ToString()),
							Effects.Num(), 1))
		{
			Test.AddError(TEXT("The node's row is missing from the data, so it "
							   "grants nothing in play. Author it in the Passive "
							   "Effects sheet of docs/All_Things_Cataclysm.xlsx and "
							   "regenerate."));
			return nullptr;
		}
		const FCataclysmPassiveEffectRow* Row = Effects[0];
		Test.TestEqual(TEXT("the row grants the stat"), Row->Stat, FString(Stat));
		Test.TestEqual(TEXT("stated flat"), Row->ValueKind, FString(TEXT("flat")));
		Test.TestEqual(TEXT("under no condition"), Row->Condition, FString());
		Test.TestEqual(TEXT("on the maximum's scale"), Row->Scale, FString(Scale));
		if (!Test.TestTrue(*FString::Printf(
								TEXT("the row states a value and a step: %.2f per %.2f"),
								Row->ValuePerPoint, Row->ScaleStep),
							Row->ValuePerPoint > 0.0f && Row->ScaleStep > 0.0f))
		{
			return nullptr;
		}
		return Row;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveWeightBearingRowTest,
	"Cataclysm.Passives.WeightBearingsRowGrantsArmourThatFollowsMaximumHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_keystone_a_kC` Weight Bearing, from its row, on a real Ravager.
 * Issue #1515.
 *
 * "Your Maximum Health also grants Armor: 1 Armor for every 10 maximum health you
 * have."
 *
 * THE ARMOUR IS ASKED FOR THE WAY A BLOW ASKS FOR IT. A scaled row is never
 * folded into the armour attribute, so reading the attribute would read no grant
 * whatever the row said. `UCataclysmDamageCalculation` asks
 * `StatForSkill("armor", ..., the attribute)` of the defender, and so does this.
 */
bool FCataclysmPassiveWeightBearingRowTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_keystone_a_kC"));
	const FCataclysmPassiveEffectRow* Row = CataclysmMaximumGrantTest::OneScaledRow(
		*this, Player.EffectTable, Node, TEXT("armor"), TEXT("max_health"));
	if (!Row)
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem = Player.AbilitySystem;

	// WRITTEN AFTER EVERY SPEND, because spending refreshes the class line and
	// the refresh writes the maximum back. Read back, so a clamp on the write
	// shows as its own failure rather than as a wrong grant.
	const auto HoldMaximum = [this, AbilitySystem](float Maximum)
	{
		AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(),
											   Maximum);
		TestEqual(*FString::Printf(TEXT("the maximum holds %.1f"), Maximum),
				  AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute()),
				  Maximum, 0.001f);
	};
	const auto ArmourAsABlowMeetsIt = [AbilitySystem]()
	{
		return AbilitySystem->StatForSkill(
			FName(TEXT("armor")), FGameplayTagContainer(),
			AbilitySystem->GetNumericAttribute(Combat::GetArmorAttribute()));
	};
	const auto Spend = [&Player, Node](int32 Points)
	{
		FCataclysmPassiveAllocation Allocation;
		if (Points > 0)
		{
			Allocation.Add(Node, Points);
		}
		Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
		Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	};

	const float Step = Row->ScaleStep;
	const float Smaller = Step * 37.6f;
	const float Larger = Step * 52.2f;

	// THE SAME CHARACTER WITHOUT THE NODE, at each maximum: the figure every
	// grant below is measured from.
	Spend(0);
	HoldMaximum(Smaller);
	const float UnspentAtSmaller = ArmourAsABlowMeetsIt();
	HoldMaximum(Larger);
	const float UnspentAtLarger = ArmourAsABlowMeetsIt();
	TestEqual(TEXT("without the node, the maximum does not move the armour"),
			  UnspentAtLarger, UnspentAtSmaller, 0.001f);

	Spend(1);
	HoldMaximum(Smaller);
	TestEqual(*FString::Printf(
				  TEXT("at %.1f maximum health the node grants %.0f whole steps "
					   "of armour, rounded down"), Smaller, 37.0f),
			  ArmourAsABlowMeetsIt() - UnspentAtSmaller,
			  Row->ValuePerPoint * 37.0f, 0.001f);

	// AND IT FOLLOWS THE MAXIMUM UP, with no refresh in between.
	HoldMaximum(Larger);
	TestEqual(*FString::Printf(
				  TEXT("raise the maximum to %.1f and the grant is %.0f steps"),
				  Larger, 52.0f),
			  ArmourAsABlowMeetsIt() - UnspentAtLarger,
			  Row->ValuePerPoint * 52.0f, 0.001f);

	// AND DOWN AGAIN.
	HoldMaximum(Smaller);
	TestEqual(TEXT("lower it back and the grant falls back"),
			  ArmourAsABlowMeetsIt() - UnspentAtSmaller,
			  Row->ValuePerPoint * 37.0f, 0.001f);

	// GIVING THE POINT BACK takes the grant away, which a build that granted it
	// once and never worked it out again would not.
	Spend(0);
	HoldMaximum(Larger);
	TestEqual(TEXT("and giving the point back takes the armour away"),
			  ArmourAsABlowMeetsIt() - UnspentAtLarger, 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveVesselRowTest,
	"Cataclysm.Passives.VesselsRowGrantsMaximumFervourThatFollowsMaximumMana",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_keystone_d_kC` Vessel, from its row, on a real Ritualist.
 * Issue #1515.
 *
 * "Your Maximum Mana also grants maximum Fervour: 1 Fervour for every 20 maximum
 * mana you have."
 *
 * THE MAXIMUM IS ASKED FOR THROUGH `MaximumClassResource`, which thirteen of the
 * fourteen readers of the Fervour maximum use. The attribute never holds a scaled
 * row, so reading it would read no grant whatever the row said.
 */
bool FCataclysmPassiveVesselRowTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using Vital = UCataclysmVitalAttributeSet;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_keystone_d_kC"));
	const FCataclysmPassiveEffectRow* Row = CataclysmMaximumGrantTest::OneScaledRow(
		*this, Player.EffectTable, Node, TEXT("class_resource"),
		TEXT("max_mana"));
	if (!Row)
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem = Player.AbilitySystem;

	const auto HoldMaximum = [this, AbilitySystem](float Maximum)
	{
		AbilitySystem->SetNumericAttributeBase(Vital::GetMaxManaAttribute(),
											   Maximum);
		TestEqual(*FString::Printf(TEXT("the maximum mana holds %.1f"), Maximum),
				  AbilitySystem->GetNumericAttribute(Vital::GetMaxManaAttribute()),
				  Maximum, 0.001f);
	};
	const auto Spend = [&Player, Node](int32 Points)
	{
		FCataclysmPassiveAllocation Allocation;
		if (Points > 0)
		{
			Allocation.Add(Node, Points);
		}
		Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
		Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	};

	const float Step = Row->ScaleStep;
	const float Smaller = Step * 37.6f;
	const float Larger = Step * 52.2f;

	Spend(0);
	HoldMaximum(Smaller);
	const float UnspentAtSmaller = AbilitySystem->MaximumClassResource();
	HoldMaximum(Larger);
	const float UnspentAtLarger = AbilitySystem->MaximumClassResource();
	TestEqual(TEXT("without the node, maximum mana does not move maximum Fervour"),
			  UnspentAtLarger, UnspentAtSmaller, 0.001f);

	Spend(1);
	HoldMaximum(Smaller);
	TestEqual(*FString::Printf(
				  TEXT("at %.1f maximum mana the node grants %.0f whole steps of "
					   "maximum Fervour, rounded down"), Smaller, 37.0f),
			  AbilitySystem->MaximumClassResource() - UnspentAtSmaller,
			  Row->ValuePerPoint * 37.0f, 0.001f);

	HoldMaximum(Larger);
	TestEqual(*FString::Printf(
				  TEXT("raise maximum mana to %.1f and the grant is %.0f steps"),
				  Larger, 52.0f),
			  AbilitySystem->MaximumClassResource() - UnspentAtLarger,
			  Row->ValuePerPoint * 52.0f, 0.001f);

	HoldMaximum(Smaller);
	TestEqual(TEXT("lower it back and the grant falls back"),
			  AbilitySystem->MaximumClassResource() - UnspentAtSmaller,
			  Row->ValuePerPoint * 37.0f, 0.001f);

	Spend(0);
	HoldMaximum(Larger);
	TestEqual(TEXT("and giving the point back takes the maximum Fervour away"),
			  AbilitySystem->MaximumClassResource() - UnspentAtLarger, 0.0f,
			  0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// A maximum's lookup counts an unconditioned row once. Issue #1515.
//
// THE FAULT THESE TWO GUARD, found while writing the two cases above. The
// refresh writes a maximum's attribute from every row on the stat, so a flat row
// or an increase is already inside it. Both lookups then ran every row over that
// attribute a second time, so a Ritualist holding Room for One More and points
// in Wider Circle had a larger Fervour bar than its attribute, and one holding
// Second Sight a larger shield bar. Neither case above could see it: Vessel's
// one row is scaled, and a scaled row is folded as nothing.
//
// EACH ALSO CHECKS THAT THE ROWS REACHED THE ATTRIBUTE AT ALL. Without that, the
// lookup equalling the attribute would pass just as well for a character whose
// rows had been dropped.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveMaximumFervourCountedOnceTest,
	"Cataclysm.Passives.AMaximumFervourRowIsCountedOnceAndVesselsGrantIsIncreased",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Room for One More, Wider Circle and Vessel, from their rows, on a real
 * Ritualist.
 *
 * `Ritualist_keystone_spine_001` Room for One More: "+30 maximum Fervour".
 * `Ritualist_basic_spine_001` Wider Circle: "+2% increased maximum Fervour per
 * point". Both unconditioned, so both are inside the attribute, and the lookup
 * must add nothing for them.
 *
 * THEN VESSEL, whose grant is the one thing the attribute lacks. It is a flat
 * row, so the pipeline adds it before the increases multiply: the lookup must
 * add the grant times Wider Circle's increase, and not the bare grant.
 */
bool FCataclysmPassiveMaximumFervourCountedOnceTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using Vital = UCataclysmVitalAttributeSet;
	using Resource = UCataclysmClassResourceAttributeSet;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Flat(TEXT("Ritualist_keystone_spine_001"));
	const FName Increase(TEXT("Ritualist_basic_spine_001"));
	const FName Vessel(TEXT("Ritualist_keystone_d_kC"));

	const TArray<const FCataclysmPassiveEffectRow*> IncreaseRows =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Increase);
	if (!TestEqual(TEXT("Wider Circle carries one row"), IncreaseRows.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("on maximum Fervour"), IncreaseRows[0]->Stat,
			  FString(TEXT("class_resource")));
	TestEqual(TEXT("stated as an increase"), IncreaseRows[0]->ValueKind,
			  FString(TEXT("increased")));
	const TArray<const FCataclysmPassiveEffectRow*> FlatRows =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Flat);
	if (!TestEqual(TEXT("Room for One More carries one row"), FlatRows.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("on maximum Fervour, too"), FlatRows[0]->Stat,
			  FString(TEXT("class_resource")));
	TestEqual(TEXT("stated flat"), FlatRows[0]->ValueKind, FString(TEXT("flat")));

	const FCataclysmPassiveEffectRow* VesselRow =
		CataclysmMaximumGrantTest::OneScaledRow(
			*this, Player.EffectTable, Vessel, TEXT("class_resource"),
			TEXT("max_mana"));
	if (!VesselRow)
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem = Player.AbilitySystem;
	const auto Attribute = [AbilitySystem]()
	{
		return AbilitySystem->GetNumericAttribute(
			Resource::GetMaxClassResourceAttribute());
	};
	const auto Spend = [&Player](const FCataclysmPassiveAllocation& Allocation)
	{
		Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
		Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	};

	constexpr int32 IncreasePoints = 5;

	Spend(FCataclysmPassiveAllocation());
	const float Unspent = Attribute();

	FCataclysmPassiveAllocation TwoNodes;
	TwoNodes.Add(Flat, 1);
	TwoNodes.Add(Increase, IncreasePoints);
	Spend(TwoNodes);

	// THE ROWS REACHED THE ATTRIBUTE, so the equality below is about something.
	TestTrue(*FString::Printf(TEXT("the two nodes raise the attribute: %.2f to %.2f"),
							  Unspent, Attribute()),
			 Attribute() > Unspent);
	TestEqual(TEXT("and the lookup is the attribute, not the rows applied twice"),
			  AbilitySystem->MaximumClassResource(), Attribute(), 0.001f);

	// AND VESSEL BESIDE THEM, at a maximum mana where rounding matters.
	FCataclysmPassiveAllocation ThreeNodes = TwoNodes;
	ThreeNodes.Add(Vessel, 1);
	Spend(ThreeNodes);
	const float Maximum = VesselRow->ScaleStep * 37.6f;
	AbilitySystem->SetNumericAttributeBase(Vital::GetMaxManaAttribute(), Maximum);
	TestEqual(TEXT("the maximum mana holds what was written"),
			  AbilitySystem->GetNumericAttribute(Vital::GetMaxManaAttribute()),
			  Maximum, 0.001f);

	const float Increased =
		1.0f + IncreaseRows[0]->ValuePerPoint * IncreasePoints / 100.0f;
	TestEqual(*FString::Printf(
				  TEXT("Vessel adds 37 whole steps, raised by Wider Circle's "
					   "%.0f%%, and nothing else"),
				  (Increased - 1.0f) * 100.0f),
			  AbilitySystem->MaximumClassResource() - Attribute(),
			  VesselRow->ValuePerPoint * 37.0f * Increased, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveMaximumShieldCountedOnceTest,
	"Cataclysm.Passives.AMaximumEnergyShieldRowIsCountedOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_basic_spine_004` Second Sight, from its row, on a real Ritualist.
 *
 * An unconditioned increase to maximum energy shield. It is inside the
 * attribute, so `MaximumEnergyShield` -- which the three shield clamps and the
 * overlay bar ask -- must answer the attribute. From issue #1973 until this
 * change it answered the attribute raised by the increase a second time.
 */
bool FCataclysmPassiveMaximumShieldCountedOnceTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using Vital = UCataclysmVitalAttributeSet;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_basic_spine_004"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Second Sight carries one row"), Effects.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("on maximum energy shield"), Effects[0]->Stat,
			  FString(TEXT("max_energy_shield")));
	TestEqual(TEXT("stated as an increase"), Effects[0]->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("under no condition"), Effects[0]->Condition, FString());
	TestEqual(TEXT("and on no scale"), Effects[0]->Scale, FString());

	UCataclysmAbilitySystemComponent* AbilitySystem = Player.AbilitySystem;
	const auto Attribute = [AbilitySystem]()
	{
		return AbilitySystem->GetNumericAttribute(
			Vital::GetMaxEnergyShieldAttribute());
	};

	Player.State->SetPassiveAllocation(FCataclysmPassiveAllocation(),
									   TArray<FName>());
	Player.Equipment->RefreshAttributes(AbilitySystem);
	const float Unspent = Attribute();

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 5);
	Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
	Player.Equipment->RefreshAttributes(AbilitySystem);

	TestTrue(*FString::Printf(TEXT("five points raise the attribute: %.2f to %.2f"),
							  Unspent, Attribute()),
			 Attribute() > Unspent);
	TestEqual(TEXT("and the lookup is the attribute, not the increase applied "
				   "twice"),
			  AbilitySystem->MaximumEnergyShield(), Attribute(), 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// Attrition, from its rows. Issue #1515.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveAttritionOnARealCharacterTest,
	"Cataclysm.Passives.AttritionMakesARealRavagersMeleeBlowCrippleAndWeakenWithoutARoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_keystone_c_kA` Attrition, from its rows, on a real Ravager.
 *
 * "Your melee attacks always Cripple and always Weaken, with no chance roll."
 *
 * TWO ROWS AND NO NEW MECHANISM. Each grants a hundred chance, the whole of the
 * roll's range, to melee skills only. The chance is asked for with the skill's
 * tags by `UCataclysmAilments::ChancesFor`, which is what made a row scoped to
 * melee reach the roll at all; `docs/DECISIONS.md` recorded the node as
 * unwritable until that was so.
 *
 * "ALWAYS" IS PROVED AT THE TOP OF THE ROLL. The roll is pinned at 99.99, the
 * highest a draw from 0 to 100 can come, so only a chance of the whole range
 * passes. A node granting half would pass a roll pinned at nought.
 *
 * THE TENTH-OF-MAXIMUM-HEALTH RULE STILL APPLIES, settled by the documents and
 * not ruled here: the project owner's rule of 2026-09-02 (#917) covers every
 * ailment that does not come from the skill's own row, and the sentence removes
 * the roll, not the threshold. So a melee blow taking less than a tenth applies
 * neither.
 *
 * EVERY CLAIM HAS ITS CONTROL: the same blow without the point, the same blow
 * with a spell's tags, and a blow below the threshold, each applying neither.
 */
bool FCataclysmPassiveAttritionOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using namespace CataclysmEnemiesStruckRowTest;
	using Combat = UCataclysmCombatAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_keystone_c_kA"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Attrition carries two rows"), Effects.Num(), 2))
	{
		AddError(TEXT("The node's rows are missing from the data, so it grants "
					  "nothing in play. Author them in the Passive Effects sheet "
					  "of docs/All_Things_Cataclysm.xlsx and regenerate."));
		return false;
	}

	TSet<FString> Stats;
	for (const FCataclysmPassiveEffectRow* Row : Effects)
	{
		Stats.Add(Row->Stat);
		TestEqual(*FString::Printf(TEXT("%s is stated flat"), *Row->Stat),
				  Row->ValueKind, FString(TEXT("flat")));
		TestTrue(*FString::Printf(TEXT("%s is the whole roll: %.1f"), *Row->Stat,
								  Row->ValuePerPoint),
				 Row->ValuePerPoint >= UCataclysmAilments::ChanceCap);
		TestEqual(*FString::Printf(TEXT("%s is for melee attacks only"),
								   *Row->Stat),
				  Row->RequiredTags, FString(TEXT("Type.Melee")));
		TestEqual(*FString::Printf(TEXT("%s carries no condition"), *Row->Stat),
				  Row->Condition, FString());
	}
	TestTrue(TEXT("one row is the chance to Cripple"),
			 Stats.Contains(TEXT("cripple_chance")));
	TestTrue(TEXT("and the other the chance to Weaken"),
			 Stats.Contains(TEXT("weaken_chance")));

	const FGameplayTagContainer Melee = MeleeTags();
	const FGameplayTagContainer Spell(UCataclysmSkillEffects::SpellTag());
	const FCataclysmAilmentKind* Cripple =
		UCataclysmAilments::KindNamed(TEXT("Cripple"));
	const FCataclysmAilmentKind* Weaken =
		UCataclysmAilments::KindNamed(TEXT("Weaken"));
	if (!TestEqual(TEXT("the melee tag exists"), Melee.Num(), 1)
		|| !TestEqual(TEXT("and the spell tag"), Spell.Num(), 1)
		|| !TestNotNull(TEXT("Cripple is an ailment"), Cripple)
		|| !TestNotNull(TEXT("and Weaken"), Weaken))
	{
		return false;
	}

	const auto Spend = [&Player, Node](int32 Points)
	{
		FCataclysmPassiveAllocation Allocation;
		if (Points > 0)
		{
			Allocation.Add(Node, Points);
		}
		Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
		Player.Equipment->RefreshAttributes(Player.AbilitySystem);

		// A WEAPON'S WORTH OF DAMAGE, AFTER THE REFRESH. A test player has none.
		Player.AbilitySystem->SetNumericAttributeBase(
			Combat::GetAttackDamageAttribute(), 1000.0f);
	};

	// ONE BLOW WITH THESE TAGS AT A FRESH, UNDEFENDED ENEMY WITH THIS MUCH
	// HEALTH. Says what the blow took, and whether each ailment is on it after.
	struct FStruck
	{
		float Taken = 0.0f;
		bool bCrippled = false;
		bool bWeakened = false;
	};
	const auto Strike = [&](const FGameplayTagContainer& Tags, float MaxHealth)
	{
		FStruck Out;
		ACataclysmEnemyCharacter* Enemy = SpawnUndefendedEnemy(World);
		UAbilitySystemComponent* EnemySystem =
			Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
		if (!EnemySystem)
		{
			AddError(TEXT("An enemy could not be spawned."));
			return Out;
		}
		EnemySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(),
											 MaxHealth);
		EnemySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
											 MaxHealth);

		UCataclysmSkillEffects::ApplyHit(Player.Character, Enemy,
										 /*DamagePercent=*/100.0f, Tags);

		Out.Taken = MaxHealth
			- EnemySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		Out.bCrippled = EnemySystem->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(FName(Cripple->TagName)));
		Out.bWeakened = EnemySystem->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(FName(Weaken->TagName)));
		Enemy->Destroy();
		return Out;
	};

	// THE TOP OF THE ROLL. Only a chance of the whole range passes this.
	const CataclysmTestWorld::FScopedAilmentRoll AtTheTop(99.99f);

	// A POOL THE BLOW TAKES WELL OVER A TENTH OF, and one it takes far less of.
	constexpr float SmallPool = 4000.0f;
	constexpr float DeepPool = 1'000'000.0f;

	// WITHOUT THE POINT: a melee blow over the threshold applies neither.
	Spend(0);
	const FStruck Unspent = Strike(Melee, SmallPool);
	TestTrue(*FString::Printf(TEXT("without Attrition the blow takes over a "
								   "tenth: %.1f"), Unspent.Taken),
			 Unspent.Taken >= SmallPool / 10.0f);
	TestFalse(TEXT("and does not Cripple"), Unspent.bCrippled);
	TestFalse(TEXT("or Weaken"), Unspent.bWeakened);

	Spend(1);

	// THE CHANCE ASKED WITH EACH SKILL'S TAGS, as the roll asks it.
	const TPair<const TCHAR*, FGameplayAttribute> Chances[] = {
		{TEXT("cripple_chance"), Combat::GetCrippleChanceAttribute()},
		{TEXT("weaken_chance"), Combat::GetWeakenChanceAttribute()},
	};
	for (const TPair<const TCHAR*, FGameplayAttribute>& Chance : Chances)
	{
		const float Held =
			Player.AbilitySystem->GetNumericAttribute(Chance.Value);
		TestTrue(*FString::Printf(TEXT("a melee skill asks the whole roll of %s"),
								  Chance.Key),
				 Player.AbilitySystem->StatForSkill(FName(Chance.Key), Melee, Held)
					 >= UCataclysmAilments::ChanceCap);
		TestEqual(*FString::Printf(TEXT("and a spell asks none of %s"),
								   Chance.Key),
				  Player.AbilitySystem->StatForSkill(FName(Chance.Key), Spell, Held),
				  0.0f, 0.001f);
	}

	const FStruck MeleeBlow = Strike(Melee, SmallPool);
	TestTrue(*FString::Printf(TEXT("with Attrition a melee blow takes over a "
								   "tenth: %.1f of %.0f"),
							  MeleeBlow.Taken, SmallPool),
			 MeleeBlow.Taken >= SmallPool / 10.0f);
	TestTrue(TEXT("and Cripples at the top of the roll"), MeleeBlow.bCrippled);
	TestTrue(TEXT("and Weakens at the top of the roll"), MeleeBlow.bWeakened);

	const FStruck SpellBlow = Strike(Spell, SmallPool);
	TestTrue(TEXT("a spell's blow takes over a tenth as well"),
			 SpellBlow.Taken >= SmallPool / 10.0f);
	TestFalse(TEXT("and does not Cripple"), SpellBlow.bCrippled);
	TestFalse(TEXT("or Weaken"), SpellBlow.bWeakened);

	const FStruck Small = Strike(Melee, DeepPool);
	TestTrue(*FString::Printf(TEXT("a melee blow that takes under a tenth lands: "
								   "%.1f of %.0f"), Small.Taken, DeepPool),
			 Small.Taken > 0.0f && Small.Taken < DeepPool / 10.0f);
	TestFalse(TEXT("and does not Cripple, the threshold still applying"),
			  Small.bCrippled);
	TestFalse(TEXT("or Weaken"), Small.bWeakened);

	return true;
}

// ---------------------------------------------------------------------------
// Kept Longer, from its row. Issue #1515.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveKeptLongerOnARealCharacterTest,
	"Cataclysm.Passives.KeptLongerLengthensWhatARealRitualistSummons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_basic_b_c0` Kept Longer, from its row, on a real Ritualist.
 *
 * "+3% increased duration of what you summon per point."
 *
 * READ FROM THE ROW AND SPENT THROUGH THE PLAYER STATE, not granted by hand,
 * so this fails while the row is missing. The stat is read at the summoning by
 * `ACataclysmMinion::Spawn`, which both the summon and the deploy skills call,
 * so a summoning here is that call with the Ritualist as the summoner.
 *
 * THREE READINGS: unspent, the stated lifetime; with five points, the stated
 * lifetime raised by five times the row's own figure; and the points given
 * back, the stated lifetime again. The only figures the test states are the
 * lifetime handed in and the number of points.
 */
bool FCataclysmPassiveKeptLongerOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_basic_b_c0"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Kept Longer carries one row"), Effects.Num(), 1))
	{
		AddError(TEXT("The node's row is missing from the data, so it grants "
					  "nothing in play. Author it in the Passive Effects sheet "
					  "of docs/All_Things_Cataclysm.xlsx and regenerate."));
		return false;
	}
	const FCataclysmPassiveEffectRow* Row = Effects[0];
	TestEqual(TEXT("on the duration of what is summoned"), Row->Stat,
			  FString(TEXT("minion_duration")));
	TestEqual(TEXT("stated as an increase"), Row->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("under no condition"), Row->Condition, FString());
	TestEqual(TEXT("for every summoning"), Row->RequiredTags, FString());
	if (!TestTrue(*FString::Printf(TEXT("of a figure above nothing: %.1f"),
								   Row->ValuePerPoint),
				  Row->ValuePerPoint > 0.0f))
	{
		return false;
	}

	const auto Spend = [&Player, Node](int32 Points)
	{
		FCataclysmPassiveAllocation Allocation;
		if (Points > 0)
		{
			Allocation.Add(Node, Points);
		}
		Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
		Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	};

	// ONE SUMMONING WITH THIS LIFETIME, and how long the minion is given. A
	// test world's clock does not move, so that is the whole of it.
	constexpr float Stated = 20.0f;
	const auto LifespanOfOneSummoned = [&]()
	{
		ACataclysmMinion* Minion = ACataclysmMinion::Spawn(
			Player.Character, FVector(200.0f, 0.0f, 0.0f), Stated,
			/*bBurns=*/false, TEXT("Imp"));
		if (!Minion)
		{
			AddError(TEXT("A minion could not be summoned."));
			return -1.0f;
		}
		const float Lifespan = Minion->GetLifeSpan();
		Minion->Destroy();
		return Lifespan;
	};

	constexpr int32 Points = 5;

	Spend(0);
	TestEqual(TEXT("unspent, a minion is given the lifetime stated"),
			  LifespanOfOneSummoned(), Stated, 0.01f);

	Spend(Points);
	TestEqual(*FString::Printf(TEXT("with %d points it is given %.0f%% more"),
							   Points, Row->ValuePerPoint * Points),
			  LifespanOfOneSummoned(),
			  Stated * (1.0f + Row->ValuePerPoint * Points / 100.0f), 0.01f);

	Spend(0);
	TestEqual(TEXT("and with the points given back, the lifetime stated again"),
			  LifespanOfOneSummoned(), Stated, 0.01f);

	return true;
}

// ---------------------------------------------------------------------------
// Deeper Hurt, from its row. Issue #1515.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveDeeperHurtOnARealCharacterTest,
	"Cataclysm.Passives.DeeperHurtLengthensTheCrippleAndWeakenARealRavagerApplies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_basic_c_stem2` Deeper Hurt, from its row, on a real Ravager.
 *
 * "+3% increased duration of Cripple and Weaken you apply per point."
 *
 * ATTRITION IS HELD THROUGHOUT, so every melee blow over a tenth of the
 * target's maximum health Cripples and Weakens, and Deeper Hurt is the only
 * thing that changes between the readings. Both nodes are spent through the
 * player state; nothing is granted by hand, so this fails while the row is
 * missing.
 *
 * RATIOS, NOT SECONDS. What a debuff lasts on the target also passes through
 * the target's own `DurationOn`; the same kind of enemy is struck each time, so
 * that cancels and only the applier's figure is left.
 *
 * AND A SPREAD COPY KEEPS THE ROW'S DURATION, by a ruling of 2026-09-23 under
 * the project owner's delegation: a debuff copied to another enemy is not
 * "Cripple and Weaken you apply". With the points still spent, a Cripple and a
 * Weaken copied by `UCataclysmContagion::SpreadOne` last what the unspent
 * character's did.
 */
bool FCataclysmPassiveDeeperHurtOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;
	using namespace CataclysmEnemiesStruckRowTest;
	using Combat = UCataclysmCombatAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_basic_c_stem2"));
	const FName Attrition(TEXT("Ravager_keystone_c_kA"));

	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Deeper Hurt carries one row"), Effects.Num(), 1))
	{
		AddError(TEXT("The node's row is missing from the data, so it grants "
					  "nothing in play. Author it in the Passive Effects sheet "
					  "of docs/All_Things_Cataclysm.xlsx and regenerate."));
		return false;
	}
	const FCataclysmPassiveEffectRow* Row = Effects[0];
	TestEqual(TEXT("on the duration of a Cripple and a Weaken applied"),
			  Row->Stat, FString(UCataclysmAilments::CrippleAndWeakenDurationStat));
	TestEqual(TEXT("stated as an increase"), Row->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("under no condition"), Row->Condition, FString());
	TestEqual(TEXT("for every skill"), Row->RequiredTags, FString());
	if (!TestTrue(*FString::Printf(TEXT("of a figure above nothing: %.1f"),
								   Row->ValuePerPoint),
				  Row->ValuePerPoint > 0.0f)
		|| !TestEqual(TEXT("and Attrition still carries its two rows"),
					  UCataclysmPassiveTree::EffectsFor(Player.EffectTable,
													   Attrition).Num(), 2))
	{
		return false;
	}

	const FGameplayTagContainer Melee = MeleeTags();
	const FGameplayTag CrippleTag = UCataclysmDebuffs::CrippleTag();
	const FGameplayTag WeakenTag = UCataclysmDebuffs::WeakenTag();
	if (!TestEqual(TEXT("the melee tag exists"), Melee.Num(), 1)
		|| !TestTrue(TEXT("and both debuff tags"),
					 CrippleTag.IsValid() && WeakenTag.IsValid()))
	{
		return false;
	}

	const auto Spend = [&Player, Node, Attrition](int32 Points)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(Attrition, 1);
		if (Points > 0)
		{
			Allocation.Add(Node, Points);
		}
		Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
		Player.Equipment->RefreshAttributes(Player.AbilitySystem);

		// A WEAPON'S WORTH OF DAMAGE, AFTER THE REFRESH. A test player has none.
		Player.AbilitySystem->SetNumericAttributeBase(
			Combat::GetAttackDamageAttribute(), 1000.0f);
	};

	const auto SecondsLeftOn = [](const UAbilitySystemComponent* System,
								  const FGameplayTag& Tag)
	{
		float Longest = 0.0f;
		for (const float Seconds : System->GetActiveEffectsTimeRemaining(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
					 FGameplayTagContainer(Tag))))
		{
			Longest = FMath::Max(Longest, Seconds);
		}
		return Longest;
	};

	// A FRESH, UNDEFENDED ENEMY WITH A POOL A 1000 BLOW TAKES A QUARTER OF.
	const auto FreshEnemy = [&]() -> ACataclysmEnemyCharacter*
	{
		ACataclysmEnemyCharacter* Enemy = SpawnUndefendedEnemy(World);
		UAbilitySystemComponent* System =
			Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
		if (!System)
		{
			AddError(TEXT("An enemy could not be spawned."));
			return nullptr;
		}
		System->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 4000.0f);
		System->SetNumericAttributeBase(Vital::GetHealthAttribute(), 4000.0f);
		return Enemy;
	};

	struct FLasting
	{
		float Cripple = 0.0f;
		float Weaken = 0.0f;
	};

	// ONE MELEE BLOW, and how long each debuff it applied lasts.
	const auto Strike = [&]()
	{
		FLasting Out;
		ACataclysmEnemyCharacter* Enemy = FreshEnemy();
		if (!Enemy)
		{
			return Out;
		}
		UCataclysmSkillEffects::ApplyHit(Player.Character, Enemy,
										 /*DamagePercent=*/100.0f, Melee);
		const UAbilitySystemComponent* System = Enemy->GetAbilitySystemComponent();
		Out.Cripple = SecondsLeftOn(System, CrippleTag);
		Out.Weaken = SecondsLeftOn(System, WeakenTag);
		Enemy->Destroy();
		return Out;
	};

	// ATTRITION'S CHANCE IS THE WHOLE ROLL; PINNED AT NOUGHT all the same, so
	// the roll is never what this test is about.
	const CataclysmTestWorld::FScopedAilmentRoll Always(0.0f);

	constexpr int32 Points = 5;
	const float Longer = 1.0f + Row->ValuePerPoint * Points / 100.0f;

	Spend(0);
	const FLasting Unspent = Strike();
	if (!TestTrue(*FString::Printf(
					  TEXT("with Attrition alone the blow Cripples and Weakens: "
						   "%.2f and %.2f seconds"),
					  Unspent.Cripple, Unspent.Weaken),
				  Unspent.Cripple > 0.0f && Unspent.Weaken > 0.0f))
	{
		return false;
	}

	Spend(Points);
	const FLasting Spent = Strike();
	TestEqual(*FString::Printf(TEXT("with %d points of Deeper Hurt the Cripple "
									"lasts %.2f times as long"),
							   Points, Longer),
			  Spent.Cripple / Unspent.Cripple, Longer, 0.001f);
	TestEqual(TEXT("and so does the Weaken"),
			  Spent.Weaken / Unspent.Weaken, Longer, 0.001f);

	// A SPREAD COPY, with the points still spent: the status row's duration.
	for (const TPair<FGameplayTag, float>& Copied :
		 {TPair<FGameplayTag, float>(CrippleTag, Unspent.Cripple),
		  TPair<FGameplayTag, float>(WeakenTag, Unspent.Weaken)})
	{
		ACataclysmEnemyCharacter* Enemy = FreshEnemy();
		if (!Enemy)
		{
			return false;
		}
		TestTrue(*FString::Printf(TEXT("%s is copied to a fresh enemy"),
								  *Copied.Key.ToString()),
				 UCataclysmContagion::SpreadOne(Player.Character, Enemy,
												Copied.Key));
		TestEqual(*FString::Printf(TEXT("and the copy of %s lasts what an "
										"unspent character's does"),
								   *Copied.Key.ToString()),
				  SecondsLeftOn(Enemy->GetAbilitySystemComponent(), Copied.Key),
				  Copied.Value, 0.01f);
		Enemy->Destroy();
	}

	// AND THE POINTS GIVEN BACK, the row's own duration again.
	Spend(0);
	const FLasting Back = Strike();
	TestEqual(TEXT("with the points given back the Cripple lasts as it did"),
			  Back.Cripple, Unspent.Cripple, 0.01f);

	return true;
}

// ---------------------------------------------------------------------------
// Overreach, from its row. Issue #1515.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveOverreachOnARealCharacterTest,
	"Cataclysm.Passives.OverreachLengthensARealRavagersBasicAttackReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_keystone_b_kC` Overreach, from its row, on a real Ravager.
 *
 * "Your melee attacks reach 2 metres further than the skill states."
 *
 * THE REAL BASIC ATTACK OF A REAL SWORD, equipped on the character's own weapon
 * slots, whose reach comes from the item base table. Read from the row and
 * spent through the player state, so this fails while the row is missing. The
 * reach gained is a difference from the same character with no point spent, so
 * the only figure it states is the row's own.
 */
bool FCataclysmPassiveOverreachOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	UCataclysmWeaponSlotsComponent* Slots = Player.Character
		? Player.Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>()
		: nullptr;
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete())
		|| !TestNotNull(TEXT("and weapon slots"), Slots))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_keystone_b_kC"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Overreach carries one row"), Effects.Num(), 1))
	{
		AddError(TEXT("The node's row is missing from the data, so it grants "
					  "nothing in play. Author it in the Passive Effects sheet "
					  "of docs/All_Things_Cataclysm.xlsx and regenerate."));
		return false;
	}
	const FCataclysmPassiveEffectRow* Row = Effects[0];
	TestEqual(TEXT("on melee reach, in metres"), Row->Stat,
			  FString(UCataclysmSkillTemplate::MeleeReachMetresStat));
	TestEqual(TEXT("stated flat"), Row->ValueKind, FString(TEXT("flat")));
	TestEqual(TEXT("for melee attacks only"), Row->RequiredTags,
			  FString(TEXT("Type.Melee")));
	if (!TestTrue(*FString::Printf(TEXT("of a figure above nothing: %.1f"),
								   Row->ValuePerPoint),
				  Row->ValuePerPoint > 0.0f))
	{
		return false;
	}

	const auto Spend = [&Player, Node](int32 Points)
	{
		FCataclysmPassiveAllocation Allocation;
		if (Points > 0)
		{
			Allocation.Add(Node, Points);
		}
		Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
		Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	};

	Slots->EquipWeaponType(TEXT("Sword"));

	Spend(0);
	const float Unspent = UCataclysmBasicAttack::ReachCmOf(Player.AbilitySystem);
	if (!TestTrue(*FString::Printf(TEXT("a Sword's basic attack reaches: %.0f cm"),
								   Unspent),
				  Unspent > 0.0f))
	{
		return false;
	}

	Spend(1);
	TestEqual(*FString::Printf(TEXT("with Overreach it reaches %.1f m further"),
							   Row->ValuePerPoint),
			  UCataclysmBasicAttack::ReachCmOf(Player.AbilitySystem) - Unspent,
			  Row->ValuePerPoint * 100.0f, 0.01f);

	Spend(0);
	TestEqual(TEXT("and with the point given back, no further"),
			  UCataclysmBasicAttack::ReachCmOf(Player.AbilitySystem), Unspent,
			  0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// Press-Ganged and Rekindled, from their rows. Issue #1515.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveReplacementRowsTest,
	"Cataclysm.Passives.PressGangedAndRekindledRowsReplaceARealRitualistsLostImp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_keystone_a_kB` Press-Ganged and `Ritualist_keystone_b_kC`
 * Rekindled, from their rows, on a real Ritualist holding Summon Imp.
 *
 * READ FROM THE ROWS AND SPENT THROUGH THE PLAYER STATE, not granted by hand,
 * so this fails while either row is missing. Each row's value is the seconds
 * between replacements, the number its sentence states.
 *
 * TWO LOSSES: an imp that dies, with Press-Ganged held; and an imp that
 * explodes on its death, with Rekindled and Every One Bursts
 * (`Ritualist_keystone_b_kB`, whose row makes a minion's death an explosion)
 * held. Each is replaced; with nothing spent, neither is.
 */
bool FCataclysmPassiveReplacementRowsTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	if (!TestTrue(TEXT("a possessed Ritualist with an effect table"),
				  Player.IsComplete()))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName PressGanged(TEXT("Ritualist_keystone_a_kB"));
	const FName Rekindled(TEXT("Ritualist_keystone_b_kC"));
	const FName EveryOneBursts(TEXT("Ritualist_keystone_b_kB"));

	const auto OneRow = [&](FName Node, const TCHAR* Stat) -> bool
	{
		const TArray<const FCataclysmPassiveEffectRow*> Effects =
			UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
		if (!TestEqual(*FString::Printf(TEXT("%s carries one row"),
										*Node.ToString()),
					   Effects.Num(), 1))
		{
			AddError(TEXT("The node's row is missing from the data, so it grants "
						  "nothing in play. Author it in the Passive Effects "
						  "sheet of docs/All_Things_Cataclysm.xlsx and "
						  "regenerate."));
			return false;
		}
		TestEqual(*FString::Printf(TEXT("%s grants its stat"), *Node.ToString()),
				  Effects[0]->Stat, FString(Stat));
		TestEqual(TEXT("stated flat"), Effects[0]->ValueKind, FString(TEXT("flat")));
		return TestTrue(TEXT("of the seconds between replacements, above nothing"),
						Effects[0]->ValuePerPoint > 0.0f);
	};
	if (!OneRow(PressGanged, UCataclysmSummonSkill::ReplacedOnDeathStat)
		|| !OneRow(Rekindled, UCataclysmSummonSkill::ReplacedOnExplosionStat))
	{
		return false;
	}

	// SUMMON IMP ON THE REAL CHARACTER, with its own figures.
	UCataclysmAbilitySystemComponent* System = Player.AbilitySystem;
	const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
		UCataclysmSummonSkill::StaticClass(), ECataclysmAbilitySlot::Special,
		/*Level=*/100, Player.Character);
	FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
	UCataclysmSummonSkill* Skill =
		Spec ? Cast<UCataclysmSummonSkill>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("Summon Imp is granted"), Skill))
	{
		return false;
	}
	Skill->Params = UCataclysmSkillShapes::ParseParams(
		TEXT("Count=1; MaxActive=3; Duration=20; Radius=3; Minions=Imp:1"));

	const auto Spend = [&Player](const TArray<FName>& Nodes)
	{
		FCataclysmPassiveAllocation Allocation;
		for (const FName& Node : Nodes)
		{
			Allocation.Add(Node, 1);
		}
		Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
		Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	};

	// ONE LOSS: summon an imp, kill it, and say how many the skill holds after.
	const auto LivingAfterALoss = [&]()
	{
		ACataclysmMinion* Imp = Skill->SummonOne();
		UAbilitySystemComponent* ImpSystem =
			UCataclysmTargeting::AbilitySystemOf(Imp);
		if (!ImpSystem)
		{
			AddError(TEXT("The imp has no ability system."));
			return -1;
		}
		ImpSystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
		const int32 Living = Skill->LivingMinionCount();
		for (ACataclysmMinion* Left : Skill->Minions)
		{
			if (IsValid(Left))
			{
				Left->Destroy();
			}
		}
		Skill->LivingMinionCount();
		return Living;
	};

	Spend({});
	TestEqual(TEXT("with nothing spent a dead imp is not replaced"),
			  LivingAfterALoss(), 0);

	Spend({PressGanged});
	TestEqual(TEXT("with Press-Ganged a dead imp is replaced"),
			  LivingAfterALoss(), 1);

	Spend({Rekindled, EveryOneBursts});
	TestEqual(TEXT("with Rekindled an imp that explodes on its death is replaced"),
			  LivingAfterALoss(), 1);

	return true;
}

// ---------------------------------------------------------------------------
// Two Hands, from its row. Issue #1515.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveTwoHandsOnARealCharacterTest,
	"Cataclysm.Passives.TwoHandsRaisesARealRavagersAttackDamageOnlyWithATwoHandedWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_basic_b_b0` Two Hands, from its row, on a real Ravager.
 *
 * "Two-handed weapons only. +3% increased Attack Damage per point."
 *
 * READ FROM THE ROW AND SPENT THROUGH THE PLAYER STATE, not granted by hand, so
 * this fails while the row is missing. The weapon is equipped on the
 * character's own weapon slots, whose hands come from the item base table: a
 * Greatsword takes two and a Sword one.
 *
 * TWO POINTS, NOT ONE, so a row that paid its figure once rather than per point
 * reads differently. Each reading is a difference from the same character with
 * no point spent, so the only figure it states is the row's own.
 */
bool FCataclysmPassiveTwoHandsOnARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmFourRowTest;

	FScopedPlayerClass AsRavager(TEXT("Ravager"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRavager.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FRealCharacter Player = Spawn(World);
	UCataclysmWeaponSlotsComponent* Slots = Player.Character
		? Player.Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>()
		: nullptr;
	if (!TestTrue(TEXT("a possessed Ravager with an effect table"),
				  Player.IsComplete())
		|| !TestNotNull(TEXT("and weapon slots"), Slots))
	{
		AddError(TEXT("If the effect table is what is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ravager_basic_b_b0"));
	const TArray<const FCataclysmPassiveEffectRow*> Effects =
		UCataclysmPassiveTree::EffectsFor(Player.EffectTable, Node);
	if (!TestEqual(TEXT("Two Hands carries one row"), Effects.Num(), 1))
	{
		AddError(TEXT("The node's row is missing from the data, so it grants "
					  "nothing in play. Author it in the Passive Effects sheet "
					  "of docs/All_Things_Cataclysm.xlsx and regenerate."));
		return false;
	}
	const FCataclysmPassiveEffectRow* Row = Effects[0];
	TestEqual(TEXT("on attack damage"), Row->Stat,
			  FString(TEXT("attack_damage")));
	TestEqual(TEXT("joining the additive sum"), Row->ValueKind,
			  FString(TEXT("increased")));
	TestEqual(TEXT("while wielding a two-handed weapon"), Row->Condition,
			  FString(TEXT("wielding_two_handed_weapon")));
	if (!TestTrue(*FString::Printf(TEXT("of a figure above nothing: %.1f"),
								   Row->ValuePerPoint),
				  Row->ValuePerPoint > 0.0f))
	{
		return false;
	}

	const auto Spend = [&Player, Node](int32 Points)
	{
		FCataclysmPassiveAllocation Allocation;
		if (Points > 0)
		{
			Allocation.Add(Node, Points);
		}
		Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
		Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	};
	const auto Gained = [&](const TCHAR* Weapon)
	{
		Slots->EquipWeaponType(Weapon);
		Spend(0);
		const float Unspent = IncreasesOn(Player.AbilitySystem, TEXT("attack_damage"));
		Spend(2);
		const float Spent = IncreasesOn(Player.AbilitySystem, TEXT("attack_damage"));
		Spend(0);
		return Spent - Unspent;
	};

	Slots->EquipWeaponType(TEXT("Greatsword"));
	TestEqual(TEXT("a Greatsword takes two hands"), Slots->GetEquippedWeaponHands(), 2);
	TestEqual(*FString::Printf(
				  TEXT("with a Greatsword two points add %.1f percentage points"),
				  Row->ValuePerPoint * 2.0f),
			  Gained(TEXT("Greatsword")), Row->ValuePerPoint * 2.0f, 0.01f);

	Slots->EquipWeaponType(TEXT("Sword"));
	TestEqual(TEXT("a Sword takes one hand"), Slots->GetEquippedWeaponHands(), 1);
	TestEqual(TEXT("and with a Sword they add nothing"),
			  Gained(TEXT("Sword")), 0.0f, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// Set Upon and Set the Pack On, from their rows. Issue #1515.
// ---------------------------------------------------------------------------

namespace CataclysmDamagedByYouRowTest
{
	/** A bare actor with an ability system: something the record can live on. */
	struct FScopedEnemy
	{
		explicit FScopedEnemy(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FScopedEnemy()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * Fill the Ritualist tree to a capstone's threshold, skipping the capstone
	 * and Set Upon. Set Upon is skipped so the one row under the window in the
	 * spend is the option's own, and the struck and unstruck readings differ
	 * by that row alone.
	 */
	int32 FillRitualistTreeToOpen(const UDataTable* NodeTable, const FName& Capstone,
								  const FName& Skip,
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
			if (Row->Tree != TEXT("Ritualist") || Pair.Key == Capstone
				|| Pair.Key == Skip || Row->MaxPoints <= 0)
			{
				continue;
			}
			const int32 Take = FMath::Min(Row->MaxPoints, Threshold - OutFilled);
			Allocation.Add(Pair.Key, Take);
			OutFilled += Take;
		}
		return Threshold;
	}

	/** The rows a node grants under one option, 0 meaning the node's own. */
	TArray<const FCataclysmPassiveEffectRow*> RowsOf(const UDataTable* EffectTable,
													 const FName& Node, int32 Option)
	{
		TArray<const FCataclysmPassiveEffectRow*> Mine;
		for (const FCataclysmPassiveEffectRow* Row :
			 UCataclysmPassiveTree::EffectsFor(EffectTable, Node))
		{
			if (Row->Option == Option)
			{
				Mine.Add(Row);
			}
		}
		return Mine;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveSetUponRowTest,
	"Cataclysm.Passives.SetUponRaisesARealRitualistsMinionDamageOnlyAgainstAnEnemyItDamagedWithinTwoSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_basic_a_b0` Set Upon, from its row, on a real Ritualist. Issue
 * #1515. "+2% increased Minion Damage per point against enemies you have
 * damaged in the last 2 seconds."
 *
 * READ FROM THE ROW AND SPENT THROUGH THE PLAYER STATE, so this fails while the
 * row is missing; every other test grants the stat by hand. Two enemies, one
 * the character struck a moment ago and one it never struck, and the same
 * enemy again after the window.
 */
bool FCataclysmPassiveSetUponRowTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmDamagedByYouRowTest;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	ACataclysmPlayerState* State =
		Character ? Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
	UCataclysmEquipmentComponent* Equipment =
		Character ? Character->GetEquipment() : nullptr;
	UCataclysmAbilitySystemComponent* AbilitySystem =
		State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!State || !Equipment || !AbilitySystem
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("A possessed Ritualist with an effect table was not built. "
					  "If the table is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_basic_a_b0"));
	const TArray<const FCataclysmPassiveEffectRow*> Rows =
		RowsOf(EffectTable, Node, 0);
	if (!TestEqual(TEXT("Set Upon carries one row"), Rows.Num(), 1))
	{
		AddError(TEXT("The node's row is missing from the data, so it grants "
					  "nothing in play. Author it in the Passive Effects sheet "
					  "of docs/All_Things_Cataclysm.xlsx and regenerate."));
		return false;
	}
	const FCataclysmPassiveEffectRow* Row = Rows[0];
	TestEqual(TEXT("on minion damage"), Row->Stat, FString(TEXT("minion_damage")));
	TestEqual(TEXT("in the increases"), Row->ValueKind, FString(TEXT("increased")));
	TestEqual(TEXT("against enemies you damaged"), Row->Condition,
			  FString(TEXT("target_damaged_by_you_within_seconds")));
	TestEqual(TEXT("in the last 2 seconds"), Row->ConditionValue, 2.0f, 0.001f);
	if (!TestTrue(*FString::Printf(TEXT("of a figure above nothing: %.1f"),
								   Row->ValuePerPoint),
				  Row->ValuePerPoint > 0.0f))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Node, 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(AbilitySystem);

	FScopedEnemy Struck(World);
	FScopedEnemy Untouched(World);
	Struck.AbilitySystem->NoteStruckBy(AbilitySystem, /*bCritical=*/false);

	const FGameplayTagContainer NoTags;
	const auto Multiplier = [&](const FScopedEnemy& Enemy)
	{
		return AbilitySystem->MultiplierForStatAgainst(
			FName(TEXT("minion_damage")), NoTags, Enemy.Actor);
	};

	const float Unstruck = Multiplier(Untouched);
	TestEqual(*FString::Printf(TEXT("one point adds %.1f%% against an enemy "
									"struck a moment ago"),
							   Row->ValuePerPoint),
			  Multiplier(Struck) - Unstruck, Row->ValuePerPoint / 100.0f, 0.0001f);

	World->TimeSeconds += 2.5f;
	TestEqual(TEXT("and nothing once 2.5 seconds have passed"),
			  Multiplier(Struck), Unstruck, 0.0001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveSetThePackOnRowTest,
	"Cataclysm.Passives.SetThePackOnMultipliesARealRitualistsMinionDamageAgainstAnEnemyItDamagedWithinTwoSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_capstone_100` option 1, Set the Pack On, from its row, on a real
 * Ritualist. Issue #1515. "Enemies you have damaged in the last 2 seconds take
 * 25% more damage from your minions."
 *
 * A HUNDRED POINTS FIRST, because that is when the capstone opens, placed
 * everywhere in the tree except Set Upon, so the one row under the window is
 * this option's and the struck reading is the unstruck one times its "more".
 */
bool FCataclysmPassiveSetThePackOnRowTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveTest;
	using namespace CataclysmDamagedByYouRowTest;

	FScopedPlayerClass AsRitualist(TEXT("Ritualist"));
	if (!TestTrue(TEXT("the class console variable exists"),
				  AsRitualist.IsUsable()))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	ACataclysmPlayerState* State =
		Character ? Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
	UCataclysmEquipmentComponent* Equipment =
		Character ? Character->GetEquipment() : nullptr;
	UCataclysmAbilitySystemComponent* AbilitySystem =
		State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!State || !Equipment || !AbilitySystem
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable)
		|| !TestNotNull(TEXT("the node table loads"), NodeTable))
	{
		AddError(TEXT("A possessed Ritualist with both passive tables was not "
					  "built. If a table is missing, run  python "
					  "tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Node(TEXT("Ritualist_capstone_100"));
	const TArray<const FCataclysmPassiveEffectRow*> Rows =
		RowsOf(EffectTable, Node, 1);
	if (!TestEqual(TEXT("Set the Pack On carries one row"), Rows.Num(), 1))
	{
		AddError(TEXT("The option's row is missing from the data, so it grants "
					  "nothing in play. Author it in the Passive Effects sheet "
					  "of docs/All_Things_Cataclysm.xlsx and regenerate."));
		return false;
	}
	const FCataclysmPassiveEffectRow* Row = Rows[0];
	TestEqual(TEXT("on minion damage"), Row->Stat, FString(TEXT("minion_damage")));
	TestEqual(TEXT("as a multiplier of its own"), Row->ValueKind,
			  FString(TEXT("more")));
	TestEqual(TEXT("against enemies you damaged"), Row->Condition,
			  FString(TEXT("target_damaged_by_you_within_seconds")));
	TestEqual(TEXT("in the last 2 seconds"), Row->ConditionValue, 2.0f, 0.001f);
	if (!TestTrue(*FString::Printf(TEXT("of a figure above nothing: %.1f"),
								   Row->ValuePerPoint),
				  Row->ValuePerPoint > 0.0f))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	int32 Filled = 0;
	const int32 Threshold = FillRitualistTreeToOpen(
		NodeTable, Node, FName(TEXT("Ritualist_basic_a_b0")), Allocation, Filled);
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

	FScopedEnemy Struck(World);
	FScopedEnemy Untouched(World);
	Struck.AbilitySystem->NoteStruckBy(AbilitySystem, /*bCritical=*/false);

	const FGameplayTagContainer NoTags;
	const auto Multiplier = [&](const FScopedEnemy& Enemy)
	{
		return AbilitySystem->MultiplierForStatAgainst(
			FName(TEXT("minion_damage")), NoTags, Enemy.Actor);
	};

	// NOTHING BEFORE THE OPTION IS CHOSEN, which is the control: the points in
	// the node alone must not grant an option's row.
	TestEqual(TEXT("with no option chosen, a struck enemy is no different"),
			  Multiplier(Struck), Multiplier(Untouched), 0.0001f);

	FString Refusal;
	if (!TestTrue(TEXT("the first option can be chosen"),
				  State->ChoosePassiveOption(Node, 1, Refusal)))
	{
		AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
		return false;
	}
	Equipment->RefreshAttributes(AbilitySystem);

	const float Unstruck = Multiplier(Untouched);
	TestEqual(*FString::Printf(TEXT("against an enemy struck a moment ago, %.0f%% more"),
							   Row->ValuePerPoint),
			  Multiplier(Struck), Unstruck * (1.0f + Row->ValuePerPoint / 100.0f),
			  0.0001f);

	World->TimeSeconds += 2.5f;
	TestEqual(TEXT("and no more once 2.5 seconds have passed"),
			  Multiplier(Struck), Unstruck, 0.0001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassPointsReachTheConditionsTest,
	"Cataclysm.Passives.ThePointsSpentReachTheStatConditionsAndOnlyAPlayerHasThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `FCataclysmStatConditions::ClassPointsSpent` is the allocation's total,
 * read through the player state that owns a player's ability system. Issue
 * #1686. An ability system with no player state reads -1, which gives a class
 * point row nothing.
 *
 * A NODE IN NO TREE HOLDS THE POINTS, so they grant nothing of their own and
 * the count is the only thing that moves. The allocation is not validated
 * when it is set, the way a loaded save is not.
 */
bool FCataclysmClassPointsReachTheConditionsTest::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* ASC =
		State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("a player state's ability system"), ASC))
	{
		return false;
	}

	TestEqual(TEXT("a player that has spent nothing reads nought"),
		ASC->CurrentConditions().ClassPointsSpent, 0);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Test_node_in_no_tree")), 137);
	State->SetPassiveAllocation(Allocation, {});
	TestEqual(TEXT("and one that has spent 137 reads 137"),
		ASC->CurrentConditions().ClassPointsSpent, 137);

	AActor* Creature = World->SpawnActor<AActor>();
	UCataclysmAbilitySystemComponent* Other = Creature
		? NewObject<UCataclysmAbilitySystemComponent>(Creature) : nullptr;
	if (!TestNotNull(TEXT("an ability system with no player state"), Other))
	{
		return false;
	}
	Other->RegisterComponent();
	Other->InitAbilityActorInfo(Creature, Creature);
	TestEqual(TEXT("an ability system no player state owns reads -1"),
		Other->CurrentConditions().ClassPointsSpent, -1);

	return true;
}

// ---------------------------------------------------------------------------
// One class tree per damage type. Issue #2064.
// ---------------------------------------------------------------------------

namespace CataclysmOneClassTest
{
	const FName RavagerRoot(TEXT("Ravager_basic_spine_000"));
	const FName RavagerNext(TEXT("Ravager_basic_spine_001"));
	const FName MasochistRoot(TEXT("Masochist_basic_spine_000"));
	const FName MasochistCapstone(TEXT("Masochist_capstone_25"));
	const FName BulwarkRoot(TEXT("Bulwark_basic_trunk_000"));

	/** A possessed Demonic player at level 100, so points never run out. */
	ACataclysmPlayerState* DemonicPlayer(FAutomationTestBase& Test, UWorld* World,
										 APlayerController*& OutController)
	{
		ACataclysmPlayerCharacter* Character =
			CataclysmPassiveTest::SpawnPossessedPlayer(World);
		OutController = Character ? Cast<APlayerController>(Character->GetController())
								  : nullptr;
		ACataclysmPlayerState* State =
			Character ? Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
		if (!Test.TestNotNull(TEXT("a possessed player with a player state"), State))
		{
			return nullptr;
		}
		State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("Demonic")));
		State->SetLevelAndExperience(100, 0);
		return State;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOneClassFirstPointChoosesTest,
	"Cataclysm.Passives.OneClass.TheFirstPointChoosesTheDamageTypesClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Demonic character's first point, in Ravager, makes Ravager its Demonic
 * class: Masochist then refuses a point and a capstone option, with the reason,
 * and Ravager goes on taking them.
 */
bool FCataclysmOneClassFirstPointChoosesTest::RunTest(const FString&)
{
	using namespace CataclysmOneClassTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APlayerController* Controller = nullptr;
	ACataclysmPlayerState* State = DemonicPlayer(*this, World, Controller);
	if (!State)
	{
		return false;
	}

	FString Reason;
	if (!TestTrue(TEXT("the first point goes into Ravager"),
				  State->SpendPassivePoint(RavagerRoot, Reason)))
	{
		AddError(Reason);
		return false;
	}

	Reason.Empty();
	TestFalse(TEXT("then Masochist refuses a point"),
			  State->SpendPassivePoint(MasochistRoot, Reason));
	TestEqual(TEXT("and says why"), Reason,
			  FString(TEXT("Ravager is your Demonic class. A respec frees the choice.")));
	TestEqual(TEXT("and Masochist holds nothing"),
			  State->GetPassiveAllocation().PointsIn(MasochistRoot), 0);

	Reason.Empty();
	TestFalse(TEXT("a Masochist capstone option is refused as well"),
			  State->ChoosePassiveOption(MasochistCapstone, 1, Reason));
	TestTrue(TEXT("for the same reason"),
			 Reason.StartsWith(TEXT("Ravager is your Demonic class.")));

	Reason.Empty();
	TestTrue(TEXT("and Ravager takes a second point"),
			 State->SpendPassivePoint(RavagerNext, Reason));
	TestEqual(TEXT("so two points are spent, both in Ravager"),
			  State->GetPassiveAllocation().Total(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOneClassRespecFreesTest,
	"Cataclysm.Passives.OneClass.TheRespecFreesTheChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Emptying the trees frees the choice, and the next first point chooses again. */
bool FCataclysmOneClassRespecFreesTest::RunTest(const FString&)
{
	using namespace CataclysmOneClassTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APlayerController* Controller = nullptr;
	ACataclysmPlayerState* State = DemonicPlayer(*this, World, Controller);
	if (!State)
	{
		return false;
	}

	FString Reason;
	if (!TestTrue(TEXT("a point in Ravager"), State->SpendPassivePoint(RavagerRoot, Reason)))
	{
		AddError(Reason);
		return false;
	}

	State->ResetPassivePoints();

	Reason.Empty();
	TestTrue(TEXT("after a respec Masochist takes a point"),
			 State->SpendPassivePoint(MasochistRoot, Reason));
	Reason.Empty();
	TestFalse(TEXT("and now Ravager is the one refused"),
			  State->SpendPassivePoint(RavagerRoot, Reason));
	TestTrue(TEXT("because Masochist is the Demonic class now"),
			 Reason.StartsWith(TEXT("Masochist is your Demonic class.")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOneClassPerDamageTypeTest,
	"Cataclysm.Passives.OneClass.AnotherDamageTypeChoosesItsOwnClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The choice is one per damage type, not one for the character: a Demonic
 * class chosen does not stop a War tree from taking a point once the character
 * carries War. The Ravager point stays spent, as a weapon change leaves it.
 */
bool FCataclysmOneClassPerDamageTypeTest::RunTest(const FString&)
{
	using namespace CataclysmOneClassTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APlayerController* Controller = nullptr;
	ACataclysmPlayerState* State = DemonicPlayer(*this, World, Controller);
	if (!State)
	{
		return false;
	}

	FString Reason;
	if (!TestTrue(TEXT("a point in Ravager"), State->SpendPassivePoint(RavagerRoot, Reason)))
	{
		AddError(Reason);
		return false;
	}

	State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("War")));

	Reason.Empty();
	TestTrue(TEXT("carrying War, Bulwark takes a point"),
			 State->SpendPassivePoint(BulwarkRoot, Reason));
	TestEqual(TEXT("and the Ravager point is still spent"),
			  State->GetPassiveAllocation().PointsIn(RavagerRoot), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOneClassScreenDimsTest,
	"Cataclysm.Passives.OneClass.TheScreenDimsTheClassesNotChosen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * After a Ravager point, the screen draws Masochist's tree button dimmed but
 * still enabled, and a Masochist node as not takeable, both with the reason as
 * their tool tip; Ravager's are unchanged.
 *
 * ON BUTTONS THIS TEST MADE. A headless test has no Widget Blueprint, so the
 * screen's own panels never exist; `DescribeButtonForTests` runs the function
 * those panels run. Whether the tool tip is shown on hover is not something a
 * headless test can see.
 */
bool FCataclysmOneClassScreenDimsTest::RunTest(const FString&)
{
	using namespace CataclysmOneClassTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	APlayerController* Controller = nullptr;
	ACataclysmPlayerState* State = DemonicPlayer(*this, World, Controller);
	if (!State || !TestNotNull(TEXT("a player controller"), Controller))
	{
		return false;
	}

	FString Reason;
	if (!TestTrue(TEXT("a point in Ravager"), State->SpendPassivePoint(RavagerRoot, Reason)))
	{
		AddError(Reason);
		return false;
	}

	// `NewObject` WITH THE CONTROLLER, as `TheScreenSpendsThroughTheCharacterAndNotIntoItself`
	// makes it and for its reason.
	UCataclysmPassiveTreeWidget* Screen = NewObject<UCataclysmPassiveTreeWidget>(Controller);
	Screen->SetPlayerStateForTests(State);

	const FString Why(TEXT("Ravager is your Demonic class. A respec frees the choice."));

	UCataclysmChoiceButton* MasochistTree = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*MasochistTree, FName(TEXT("Masochist")), true);
	TestTrue(TEXT("the Masochist tree button is dimmed"), MasochistTree->IsDimmed());
	TestTrue(TEXT("but can still be clicked, to read the tree"),
			 MasochistTree->IsAvailable());
	TestEqual(TEXT("and its tool tip says why"),
			  MasochistTree->GetToolTipText().ToString(), Why);

	UCataclysmChoiceButton* RavagerTree = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*RavagerTree, FName(TEXT("Ravager")), true);
	TestFalse(TEXT("the Ravager tree button is not dimmed"), RavagerTree->IsDimmed());
	TestTrue(TEXT("and has no tool tip"), RavagerTree->GetToolTipText().IsEmpty());

	UCataclysmChoiceButton* MasochistNode = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*MasochistNode, MasochistRoot, false);
	TestFalse(TEXT("the Masochist root cannot be taken"), MasochistNode->IsAvailable());
	TestEqual(TEXT("and its tool tip says why, on the node"),
			  MasochistNode->GetToolTipText().ToString(), Why);

	UCataclysmChoiceButton* RavagerNode = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*RavagerNode, RavagerNext, false);
	TestTrue(TEXT("the next Ravager node can be taken"), RavagerNode->IsAvailable());
	TestTrue(TEXT("and has no tool tip, on the node"),
			 RavagerNode->GetToolTipText().IsEmpty());
	return true;
}

// ---------------------------------------------------------------------------
// The nine Demonic options built engine first, from their rows. Issue #1515.
// ---------------------------------------------------------------------------

namespace CataclysmDemonicRowsTest
{
	using namespace CataclysmPassiveTest;

	/** A stat an option's row carries, named by the constant the engine reads
	 *  it through, and the figure the option's sentence states. */
	struct FStatFigure
	{
		const TCHAR* Stat;
		float Value;
	};

	/**
	 * Fill one class's tree to a capstone's threshold, skipping the capstone.
	 * `FillTreeToOpen` above does this for the Masochist's tree and
	 * `FillRitualistTreeToOpen` for the Ritualist's; this one names the tree,
	 * because the Ravager's options need it too.
	 */
	int32 FillClassTreeToOpen(const UDataTable* NodeTable, const TCHAR* Tree,
							  const FName& Capstone,
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
			if (Row->Tree != Tree || Pair.Key == Capstone || Row->MaxPoints <= 0)
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
	 * THE TEST EACH OF THE NINE RUNS, with its own node, option and figures.
	 *
	 * On a real player of the class, spending real points: the option's rows
	 * are exactly the stats named, each flat and of the figure its sentence
	 * states. Each stat, read through the constant the engine reads it by, is
	 * nothing before the points are spent; for a capstone it is still nothing
	 * with the points spent and no option chosen; and it is the figure once
	 * the option is chosen, or once a keystone's point is spent.
	 *
	 * SO A ROW THAT IS MISSING, MISSPELT OR OF THE WRONG FIGURE FAILS HERE,
	 * which the engine tests, granting each stat by hand, cannot see.
	 *
	 * Option 0 is a keystone: the node's own rows, one point, no choice.
	 */
	bool WearsTheRows(FAutomationTestBase& Test, const TCHAR* ClassName,
					  const FName& Node, int32 Option,
					  const TArray<FStatFigure>& Figures)
	{
		FScopedPlayerClass AsClass(ClassName);
		if (!Test.TestTrue(TEXT("the class console variable exists"),
						   AsClass.IsUsable()))
		{
			return false;
		}

		UWorld* World = MakeWorldThatHasBegunPlay();
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
		ACataclysmPlayerState* State = Character
			? Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
		UCataclysmEquipmentComponent* Equipment =
			Character ? Character->GetEquipment() : nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem =
			State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
		const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
		const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
		if (!State || !Equipment || !AbilitySystem
			|| !Test.TestNotNull(TEXT("the effect table loads"), EffectTable)
			|| !Test.TestNotNull(TEXT("the node table loads"), NodeTable))
		{
			Test.AddError(TEXT("A possessed player with both passive tables was "
							   "not built. If a table is missing, run  python "
							   "tools/run_editor_python.py "
							   "tools/generate_datatable_assets.py"));
			return false;
		}

		const TArray<const FCataclysmPassiveEffectRow*> Rows =
			CataclysmDamagedByYouRowTest::RowsOf(EffectTable, Node, Option);
		if (!Test.TestEqual(*FString::Printf(TEXT("%s option %d carries %d rows"),
											 *Node.ToString(), Option,
											 Figures.Num()),
							Rows.Num(), Figures.Num()))
		{
			Test.AddError(TEXT("The option's rows are missing from the data, so "
							   "it grants nothing in play. Author them in the "
							   "Passive Effects sheet of "
							   "docs/All_Things_Cataclysm.xlsx and regenerate."));
			return false;
		}
		for (const FStatFigure& Figure : Figures)
		{
			const FCataclysmPassiveEffectRow* const* Found = Rows.FindByPredicate(
				[&Figure](const FCataclysmPassiveEffectRow* Row)
				{
					return Row->Stat == Figure.Stat;
				});
			if (!Test.TestNotNull(*FString::Printf(
									  TEXT("a row grants %s, the stat the engine reads"),
									  Figure.Stat),
								  Found))
			{
				return false;
			}
			Test.TestEqual(*FString::Printf(TEXT("%s is stated flat"), Figure.Stat),
						   (*Found)->ValueKind, FString(TEXT("flat")));
			Test.TestEqual(*FString::Printf(TEXT("%s is %g, the figure its sentence states"),
											Figure.Stat, Figure.Value),
						   (*Found)->ValuePerPoint, Figure.Value, 0.001f);
		}

		const FGameplayTagContainer NoTags;
		const auto Read = [AbilitySystem, &NoTags](const FStatFigure& Figure)
		{
			return AbilitySystem->StatForSkill(FName(Figure.Stat), NoTags, 0.0f);
		};

		for (const FStatFigure& Figure : Figures)
		{
			Test.TestEqual(*FString::Printf(TEXT("with nothing spent, %s is nothing"),
											Figure.Stat),
						   Read(Figure), 0.0f, 0.001f);
		}

		FCataclysmPassiveAllocation Allocation;
		if (Option > 0)
		{
			int32 Filled = 0;
			const int32 Threshold = FillClassTreeToOpen(
				NodeTable, ClassName, Node, Allocation, Filled);
			if (!Test.TestTrue(TEXT("the capstone states a threshold"), Threshold > 0)
				|| !Test.TestEqual(*FString::Printf(
									   TEXT("the tree can hold the %d points it opens at"),
									   Threshold),
								   Filled, Threshold))
			{
				return false;
			}
		}
		Allocation.Add(Node, 1);
		State->SetPassiveAllocation(Allocation, TArray<FName>());
		Equipment->RefreshAttributes(AbilitySystem);

		if (Option > 0)
		{
			// THE CONTROL: the points in the capstone alone must not grant an
			// option's rows.
			for (const FStatFigure& Figure : Figures)
			{
				Test.TestEqual(*FString::Printf(
								   TEXT("with the points spent and no option chosen, %s is nothing"),
								   Figure.Stat),
							   Read(Figure), 0.0f, 0.001f);
			}

			FString Refusal;
			if (!Test.TestTrue(TEXT("the option can be chosen"),
							   State->ChoosePassiveOption(Node, Option, Refusal)))
			{
				Test.AddError(FString::Printf(TEXT("Refused: %s"), *Refusal));
				return false;
			}
			Equipment->RefreshAttributes(AbilitySystem);
		}

		for (const FStatFigure& Figure : Figures)
		{
			Test.TestEqual(*FString::Printf(TEXT("held, %s reads %g"),
											Figure.Stat, Figure.Value),
						   Read(Figure), Figure.Value, 0.001f);
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsSharedRuinTest,
	"Cataclysm.DemonicRows.SharedRuinReachesARealRitualistFromItsRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ritualist_capstone_100` option 2, Shared Ruin: "When a minion of yours
 *  dies, everything within 4 metres takes damage equal to 20% of that
 *  minion's maximum health." */
bool FCataclysmDemonicRowsSharedRuinTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ritualist"), FName(TEXT("Ritualist_capstone_100")), 2,
		{{ACataclysmMinion::DeathBlastPercentOfMaximumHealthStat, 20.0f},
		 {ACataclysmMinion::DeathBlastRadiusMetresStat, 4.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsNothingStopsItTest,
	"Cataclysm.DemonicRows.NothingStopsItReachesARealRavagerFromItsRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_capstone_200` option 3, Nothing Stops It: "You cannot be brought
 *  below 1 health by a single hit. When a hit would have done so you take no
 *  damage for 2 seconds, no more than once every 20 seconds." */
bool FCataclysmDemonicRowsNothingStopsItTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ravager"), FName(TEXT("Ravager_capstone_200")), 3,
		{{UCataclysmAbilitySystemComponent::LethalHitSurvivedEverySecondsStat, 20.0f},
		 {UCataclysmAbilitySystemComponent::ImmuneAfterLethalHitSecondsStat, 2.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsSacrificialWardTest,
	"Cataclysm.DemonicRows.SacrificialWardReachesARealRitualistFromItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ritualist_keystone_c_kC` Sacrificial Ward: "Damage that would break your
 *  Energy Shield instead destroys the minion with the least health remaining,
 *  no more than once every 3 seconds." */
bool FCataclysmDemonicRowsSacrificialWardTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ritualist"), FName(TEXT("Ritualist_keystone_c_kC")), 0,
		{{UCataclysmAbilitySystemComponent::ShieldBreakDestroysMinionEverySecondsStat, 3.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsCastFromWardTest,
	"Cataclysm.DemonicRows.CastFromWardReachesARealRitualistFromItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ritualist_capstone_50` option 3, Cast from Ward: "A skill may be paid for
 *  with Energy Shield when your mana is not enough." A flag of 1. */
bool FCataclysmDemonicRowsCastFromWardTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ritualist"), FName(TEXT("Ritualist_capstone_50")), 3,
		{{UCataclysmGameplayAbility::CostPaidFromEnergyShieldStat, 1.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsNoSecondWindTest,
	"Cataclysm.DemonicRows.NoSecondWindReachesARealRavagerFromItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_keystone_c_kB` No Second Wind: "Cripple and Weaken you applied do
 *  not expire while that enemy is within 4 metres of you." */
bool FCataclysmDemonicRowsNoSecondWindTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ravager"), FName(TEXT("Ravager_keystone_c_kB")), 0,
		{{UCataclysmDebuffs::AppliedHeldWithinMetresStat, 4.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsRenderingBlowsTest,
	"Cataclysm.DemonicRows.RenderingBlowsReachesARealRavagerFromItsRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_capstone_50` option 1, Rendering Blows: "Every third melee attack
 *  against the same enemy removes 20% of its Armor for 6 seconds." */
bool FCataclysmDemonicRowsRenderingBlowsTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ravager"), FName(TEXT("Ravager_capstone_50")), 1,
		{{UCataclysmAbilitySystemComponent::RendPercentStat, 20.0f},
		 {UCataclysmAbilitySystemComponent::RendSecondsStat, 6.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsGroundDownTest,
	"Cataclysm.DemonicRows.GroundDownReachesARealRavagerFromItsRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_capstone_100` option 1, Ground Down: "Enemies within 4 metres of
 *  you have 15% reduced Movement Speed and 15% reduced Attack Speed." */
bool FCataclysmDemonicRowsGroundDownTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ravager"), FName(TEXT("Ravager_capstone_100")), 1,
		{{UCataclysmDebuffs::GroundDownMetresStat, 4.0f},
		 {UCataclysmDebuffs::GroundDownPercentStat, 15.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsNothingWastedTest,
	"Cataclysm.DemonicRows.NothingWastedReachesARealRavagerFromItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_capstone_50` option 2, Nothing Wasted: "Damage your Armor and
 *  Damage Reduction remove is added to your next melee attack, up to 100% of
 *  that attack's damage." */
bool FCataclysmDemonicRowsNothingWastedTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ravager"), FName(TEXT("Ravager_capstone_50")), 2,
		{{UCataclysmAbilitySystemComponent::MitigatedAddedCapStat, 100.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsSharedBloodTest,
	"Cataclysm.DemonicRows.SharedBloodReachesARealRitualistFromItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ritualist_capstone_50` option 1, Shared Blood: "Your minions each have 20%
 *  of your Maximum Energy Shield as their own, and it recharges when yours
 *  does." */
bool FCataclysmDemonicRowsSharedBloodTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ritualist"), FName(TEXT("Ritualist_capstone_50")), 1,
		{{UCataclysmRegeneration::SharedBloodStat, 20.0f}});
}

// ---------------------------------------------------------------------------
// The last six Demonic options built engine first, issue #1515. Their engine
// entries each said the rows change must add a test that wears the real row.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsNowhereToRunTest,
	"Cataclysm.DemonicRows.NowhereToRunReachesARealRavagerFromItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_capstone_200` option 1, Nowhere to Run: "Enemies within 8 metres of
 *  you cannot move away from you. They may move toward you or around you, but
 *  not further away. Your Fervour does not decay while any enemy is held this
 *  way." */
bool FCataclysmDemonicRowsNowhereToRunTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ravager"), FName(TEXT("Ravager_capstone_200")), 1,
		{{UCataclysmDebuffs::NowhereToRunMetresStat, 8.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsBothHandsFullTest,
	"Cataclysm.DemonicRows.BothHandsFullReachesARealRavagerFromItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_capstone_200` option 2, Both Hands Full: "You may hold a two-handed
 *  weapon in each hand. Both contribute their damage, their affixes and their
 *  sockets." A flag of 1. */
bool FCataclysmDemonicRowsBothHandsFullTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ravager"), FName(TEXT("Ravager_capstone_200")), 2,
		{{UCataclysmEquipmentComponent::BothHandsFullStat, 1.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsShoulderThroughTest,
	"Cataclysm.DemonicRows.ShoulderThroughReachesARealRavagerFromItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_capstone_100` option 3, Shoulder Through: "Moving into an enemy
 *  pushes it aside and deals your melee damage to it." A flag of 1. */
bool FCataclysmDemonicRowsShoulderThroughTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ravager"), FName(TEXT("Ravager_capstone_100")), 3,
		{{UCataclysmShoulderThrough::Stat, 1.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsFollowThroughTest,
	"Cataclysm.DemonicRows.FollowThroughReachesARealRavagerFromItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ravager_keystone_b_kB` Follow Through: "Killing an enemy with a melee attack
 *  immediately repeats that attack at no cost, no more than once every 3
 *  seconds." */
bool FCataclysmDemonicRowsFollowThroughTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ravager"), FName(TEXT("Ravager_keystone_b_kB")), 0,
		{{UCataclysmFollowThrough::EverySecondsStat, 3.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsASecondSelfTest,
	"Cataclysm.DemonicRows.ASecondSelfReachesARealRitualistFromItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ritualist_capstone_200` option 1, A Second Self: "The minion you have held
 *  longest becomes your equal: it has your Maximum Health, your Spell Damage
 *  and your Area of Effect, and reserves twice the Fervour it would. When it is
 *  gone, the next longest-held takes its place." A flag of 1. */
bool FCataclysmDemonicRowsASecondSelfTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ritualist"), FName(TEXT("Ritualist_capstone_200")), 1,
		{{UCataclysmSecondSelf::Stat, 1.0f}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonicRowsChorusTest,
	"Cataclysm.DemonicRows.ChorusReachesARealRitualistFromItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** `Ritualist_capstone_200` option 3, Chorus: "Your minions repeat each skill you
 *  cast, dealing 30% of its damage." A flag of 1; the 30% is
 *  `UCataclysmChorus::SharePercent`, not a row. */
bool FCataclysmDemonicRowsChorusTest::RunTest(const FString&)
{
	return CataclysmDemonicRowsTest::WearsTheRows(
		*this, TEXT("Ritualist"), FName(TEXT("Ritualist_capstone_200")), 3,
		{{UCataclysmChorus::Stat, 1.0f}});
}

#endif // WITH_AUTOMATION_TESTS
