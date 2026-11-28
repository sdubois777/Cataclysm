// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmClassStats.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "HAL/IConsoleManager.h"

/**
 * WHICH CLASS STAT LINE A CHARACTER STANDS ON, issue #932 (2026-11-27). `UCataclysmPlayerClassStats::ClassFor` against
 * the generated node table: no point and the creation damage type, the most points, a tie, and the console variable set
 * by hand. And the three War lines are in the class table.
 */
namespace CataclysmClassLineTest
{
	/** Sets `Cataclysm.PlayerClass` for the scope and puts it back. */
	struct FScopedClassVariable
	{
		explicit FScopedClassVariable(const TCHAR* Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.PlayerClass"));
			if (Variable)
			{
				Previous = Variable->GetString();
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}
		~FScopedClassVariable()
		{
			if (Variable)
			{
				Variable->Set(*Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		FString Previous;
	};

	FCataclysmPassiveAllocation Spent(std::initializer_list<TPair<const TCHAR*, int32>> Nodes)
	{
		FCataclysmPassiveAllocation Allocation;
		for (const TPair<const TCHAR*, int32>& Each : Nodes)
		{
			FCataclysmSpentNode Node;
			Node.Node = FName(Each.Key);
			Node.Points = Each.Value;
			Allocation.Nodes.Add(Node);
		}
		return Allocation;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassLineRuleTest,
	"Cataclysm.ClassLine.TheMostPointsWinATieGoesToTheFirstChosenAndNoPointsTakeTheDamageTypesFirstClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClassLineRuleTest::RunTest(const FString&)
{
	using namespace CataclysmClassLineTest;
	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the passive node table loads"), Nodes))
	{
		return false;
	}
	const FScopedClassVariable Unset(TEXT(""));

	// NO POINT SPENT: the damage type's first class, and with no damage type the starting class.
	TestEqual(TEXT("War with no point is the Bulwark"),
			  UCataclysmPlayerClassStats::ClassFor(Nodes, FCataclysmPassiveAllocation(), FName(TEXT("War"))),
			  FString(TEXT("Bulwark")));
	TestEqual(TEXT("Death with no point is the Soul Collector"),
			  UCataclysmPlayerClassStats::ClassFor(Nodes, FCataclysmPassiveAllocation(), FName(TEXT("Death"))),
			  FString(TEXT("Soul Collector")));
	TestEqual(TEXT("control: no damage type and no point is the starting class"),
			  UCataclysmPlayerClassStats::ClassFor(Nodes, FCataclysmPassiveAllocation(), NAME_None),
			  UCataclysmPlayerClassStats::StartingClassName);

	// THE MOST POINTS: two in the Berserker tree against one in the Masochist's, whichever came first.
	TestEqual(TEXT("the class with the most points wins"),
			  UCataclysmPlayerClassStats::ClassFor(
				  Nodes, Spent({{TEXT("Masochist_basic_spine_000"), 1}, {TEXT("Berserker_trunk_007"), 2}}),
				  FName(TEXT("War"))),
			  FString(TEXT("Berserker")));

	// A TIE: one each, and the first spent wins, both ways round.
	TestEqual(TEXT("a tie goes to the class chosen first: the Masochist"),
			  UCataclysmPlayerClassStats::ClassFor(
				  Nodes, Spent({{TEXT("Masochist_basic_spine_000"), 1}, {TEXT("Berserker_trunk_007"), 1}}),
				  FName(TEXT("War"))),
			  FString(TEXT("Masochist")));
	TestEqual(TEXT("and the other way round, the Berserker"),
			  UCataclysmPlayerClassStats::ClassFor(
				  Nodes, Spent({{TEXT("Berserker_trunk_007"), 1}, {TEXT("Masochist_basic_spine_000"), 1}}),
				  FName(TEXT("War"))),
			  FString(TEXT("Berserker")));

	// SET BY HAND, IT WINS over the points.
	{
		const FScopedClassVariable ByHand(TEXT("Ritualist"));
		TestEqual(TEXT("the console variable set by hand wins"),
				  UCataclysmPlayerClassStats::ClassFor(Nodes, Spent({{TEXT("Berserker_trunk_007"), 3}}),
													   FName(TEXT("War"))),
				  FString(TEXT("Ritualist")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClassLineWarLinesTest,
	"Cataclysm.ClassLine.TheThreeWarClassesHaveLinesOfTheirOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Each War class's maximum health at level 1 is its own line's figure, not the Default line's 100. */
bool FCataclysmClassLineWarLinesTest::RunTest(const FString&)
{
	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!TestNotNull(TEXT("the class stat table loads"), Table))
	{
		return false;
	}
	const TPair<const TCHAR*, float> Expected[] = {
		{TEXT("Bulwark"), 140.0f}, {TEXT("Berserker"), 120.0f}, {TEXT("Saboteur"), 95.0f}};
	for (const TPair<const TCHAR*, float>& Each : Expected)
	{
		TestEqual(FString::Printf(TEXT("%s's maximum health at level 1"), Each.Key),
				  UCataclysmClassStats::BaseFor(Table, Each.Key, TEXT("max_health"), 1), Each.Value, 0.001f);
	}
	TestEqual(TEXT("control: the Default line's is 100"),
			  UCataclysmClassStats::BaseFor(Table, UCataclysmClassStats::DefaultClassName, TEXT("max_health"), 1),
			  100.0f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
