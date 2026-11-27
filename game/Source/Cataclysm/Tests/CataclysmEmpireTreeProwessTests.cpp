// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmEmpireTreeLayout.h"
#include "Player/CataclysmEmpireTree.h"
#include "Tests/CataclysmEmpireTreeTestTables.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"

/**
 * The empire upgrade tree's slice 8: Imperial Prowess. Issue #2741. The project owner, 2026-11-21, verbatim:
 * "Keystones are tier gated behind point thresholds. Imperial prowess lets you pick an additional keystone from a
 * previous tier." The keystones with point thresholds are the four centre milestones' options (25, 50, 100, 200 points
 * in the whole tree); the branch nodes of kind "keystone" open through an edge, not a threshold. So Imperial Prowess,
 * The Imperial Zenith's first option, lets one more option of an earlier milestone be taken beside the one chosen there.
 * The coordinating session's J1 to J5 of the same day: the earlier milestone's own option chosen and its point bought
 * first (J1); one extra option in total (J2); the account and character records move on (J3); The Soul Forge and The
 * Master Tinkerer stay refused while crafting is missing (J4; #47 slice F6 built crafting for them, so J4 is shown on a
 * copy of the table); the extra option stands until a respec (J5).
 *
 * `Cataclysm.EmpireTreeProwess.` IS A NEW GROUP and selects these and nothing else.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1 of the slice: every figure and sentence below is reasoned from the code, and the run
 * that proves them is registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmEmpireTreeProwessTest
{
	const FName Foundations(TEXT("capstone_0000000m"));		// 25: The Aegis of Hope, The Delver, The Hoarder
	const FName EdictsOfPower(TEXT("capstone_0000000n"));	// 50: The Sentinel, The Collector's Decree, The Soul Forge
	const FName ImperialZenith(TEXT("capstone_0000000p"));	// 200: Imperial Prowess, The Last Stand, The Flood Barrier
	const FName Foundation(TEXT("basic_0000000v"));			// an Architect node, which offers no choice

	/** A milestone's option chosen and its one point bought, by hand: the extra option's rules ask nothing of the path. */
	void Take(FCataclysmPassiveAllocation& Tree, FName Milestone, int32 Option)
	{
		Tree.SetChosenOption(Milestone, Option);
		Tree.Add(Milestone, 1);
	}
}

/**
 * Imperial Prowess's row gives one extra option (J2), and only while it is The Imperial Zenith's choice and the
 * milestone's point is bought: chosen and bought, 1; chosen and not bought, 0; The Last Stand chosen and bought, 0. The
 * name the rules give the pick is "Imperial Prowess". Prints "PROWESS TABLE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeProwessTableTest,
	"Cataclysm.EmpireTreeProwess.ImperialProwessGivesOneExtraOption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeProwessTableTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeProwessTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	FCataclysmPassiveAllocation Bought;
	Take(Bought, ImperialZenith, 1);
	FCataclysmPassiveAllocation Unbought;
	Unbought.SetChosenOption(ImperialZenith, 1);
	FCataclysmPassiveAllocation LastStand;
	Take(LastStand, ImperialZenith, 2);
	const int32 WithIt = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, Bought).ExtraEarlierMilestoneOptions;
	const int32 NotBought = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, Unbought).ExtraEarlierMilestoneOptions;
	const int32 Other = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, LastStand).ExtraEarlierMilestoneOptions;
	const FString Granter = UCataclysmEmpireTree::ExtraOptionGranterName(Nodes, Effects);
	AddInfo(FString::Printf(TEXT("PROWESS TABLE: chosen and bought %d, not bought %d, The Last Stand %d; granted by %s"),
							WithIt, NotBought, Other, *Granter));

	TestEqual(TEXT("Imperial Prowess chosen and its point bought: one extra option"), WithIt, 1);
	TestEqual(TEXT("chosen and not bought: none"), NotBought, 0);
	TestEqual(TEXT("CONTROL: The Last Stand chosen and bought: none"), Other, 0);
	TestEqual(TEXT("the rules name the pick's source"), Granter, FString(TEXT("Imperial Prowess")));
	return true;
}

/**
 * Each refusal, in its order, with Foundations of the Empire's The Delver chosen and bought:
 *   without Imperial Prowess, Foundations' The Aegis of Hope is refused for that;
 *   with it, an Architect node offers no choice, and an option 4 does not exist;
 *   The Imperial Zenith is not an earlier tier than itself;
 *   Edicts of Power, its option not chosen, must be chosen and bought first (J1);
 *   The Delver is already Foundations' choice;
 *   The Soul Forge, with Edicts of Power's The Sentinel chosen and bought, waits on crafting (J4) -- on a copy of the
 *   node table in which it still waits, since #47 slice F6 built it; in the generated table it is open;
 *   The Aegis of Hope is taken; then The Collector's Decree is refused, the one extra option taken (J2, J5).
 * CONTROL: The Aegis of Hope is open before it is taken. Prints "PROWESS REFUSALS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeProwessRefusalsTest,
	"Cataclysm.EmpireTreeProwess.EachRefusalComesInItsOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeProwessRefusalsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeProwessTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	auto Refusal = [Nodes, Effects](const FCataclysmPassiveAllocation& Tree, FName Node, int32 Option)
	{
		return UCataclysmEmpireTree::RefusalForChoosingExtraOption(Nodes, Effects, Tree, Node, Option);
	};

	FCataclysmPassiveAllocation Tree;
	Take(Tree, Foundations, 2);
	const FString NoProwess = Refusal(Tree, Foundations, 1);
	Take(Tree, ImperialZenith, 1);
	const FString NoChoice = Refusal(Tree, Foundation, 1);
	const FString NoSuchOption = Refusal(Tree, Foundations, 4);
	const FString NotEarlier = Refusal(Tree, ImperialZenith, 2);
	const FString NotChosen = Refusal(Tree, EdictsOfPower, 2);
	const FString AlreadyChosen = Refusal(Tree, Foundations, 2);
	const FString Open = Refusal(Tree, Foundations, 1);
	Take(Tree, EdictsOfPower, 1);
	// J4 ON A COPY IN WHICH THE SOUL FORGE STILL WAITS: since #47 slice F6 no option of the generated table waits.
	const UDataTable* Waiting = CataclysmEmpireTreeTestTables::WithAnOptionWaiting(
		Nodes, EdictsOfPower, 3, CataclysmEmpireTreeTestTables::CraftingReason());
	const FString Crafting = UCataclysmEmpireTree::RefusalForChoosingExtraOption(Waiting, Effects, Tree, EdictsOfPower, 3);
	const FString SoulForgeOpen = Refusal(Tree, EdictsOfPower, 3);
	FString Reason;
	const bool bTaken = UCataclysmEmpireTree::ChooseExtraOption(Nodes, Effects, Tree, Foundations, 1, Reason);
	const FString Second = Refusal(Tree, EdictsOfPower, 2);
	AddInfo(FString::Printf(TEXT("PROWESS REFUSALS: [%s] [%s] [%s] [%s] [%s] [%s] open [%s] [%s]; taken %d [%s]; [%s]"),
							*NoProwess, *NoChoice, *NoSuchOption, *NotEarlier, *NotChosen, *AlreadyChosen, *Open,
							*Crafting, bTaken ? 1 : 0, *Reason, *Second));

	TestEqual(TEXT("without Imperial Prowess"), NoProwess,
			  FString(TEXT("A second option of Foundations of the Empire can be taken only with Imperial Prowess, chosen "
						   "and its point bought.")));
	TestEqual(TEXT("an Architect node offers no choice"), NoChoice, FString(TEXT("Foundation offers no choice.")));
	TestEqual(TEXT("an option that does not exist"), NoSuchOption, FString(TEXT("Choose 1, 2 or 3, not 4.")));
	TestEqual(TEXT("The Imperial Zenith is not an earlier tier"), NotEarlier,
			  FString(TEXT("Imperial Prowess takes an option from a tier before The Imperial Zenith, and The Imperial "
						   "Zenith is not one.")));
	TestEqual(TEXT("J1: Edicts of Power's own option first"), NotChosen,
			  FString(TEXT("Edicts of Power's own option must be chosen and its point bought before Imperial Prowess can "
						   "add another.")));
	TestEqual(TEXT("The Delver is already the choice"), AlreadyChosen,
			  FString(TEXT("The Delver is already Foundations of the Empire's choice.")));
	TestTrue(TEXT("CONTROL: The Aegis of Hope is open"), Open.IsEmpty());
	TestTrue(TEXT("CONTROL: in the generated table The Soul Forge is open as the extra option, since #47 slice F6"),
			 SoulForgeOpen.IsEmpty());
	TestEqual(TEXT("J4: The Soul Forge, while it waits on crafting"), Crafting,
			  FString(TEXT("The Soul Forge cannot be chosen yet: it needs crafting, which the game does not have yet "
						   "(issue #47).")));
	TestTrue(TEXT("The Aegis of Hope is taken"), bTaken && Reason.IsEmpty());
	TestEqual(TEXT("and held as the extra option"), Tree.ExtraOptionIn(Foundations), 1);
	TestEqual(TEXT("J2, J5: one extra option, until a respec"), Second,
			  FString(TEXT("Imperial Prowess's extra option is already taken: The Aegis of Hope. A respec of the empire "
						   "tree frees it.")));
	return true;
}

/**
 * The extra option acts beside the choice. Foundations of the Empire with The Delver chosen and bought, and The Aegis of
 * Hope taken as the extra option: the city bonuses hold The Delver's 5 days fewer and The Aegis of Hope's +50% Outpost
 * defence. CONTROL: without the extra option, the defence is 0; and an extra option on a milestone whose point is not
 * bought acts not at all (Edicts of Power's The Collector's Decree, +10 floors, gives 0). Prints "PROWESS ACTS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeProwessActsTest,
	"Cataclysm.EmpireTreeProwess.TheExtraOptionActsBesideTheChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeProwessActsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeProwessTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	FCataclysmPassiveAllocation Without;
	Take(Without, Foundations, 2);
	FCataclysmPassiveAllocation With = Without;
	With.SetExtraOption(Foundations, 1);
	FCataclysmPassiveAllocation Unbought;
	Unbought.SetChosenOption(EdictsOfPower, 1);
	Unbought.SetExtraOption(EdictsOfPower, 2);

	const FCataclysmEmpireTreeBonuses Both = UCataclysmEmpireTree::CityBonusesFor(Nodes, Effects, With);
	const FCataclysmEmpireTreeBonuses ChoiceAlone = UCataclysmEmpireTree::CityBonusesFor(Nodes, Effects, Without);
	const FCataclysmEmpireTreeBonuses NotBought = UCataclysmEmpireTree::CityBonusesFor(Nodes, Effects, Unbought);
	AddInfo(FString::Printf(TEXT("PROWESS ACTS: with the extra %.1f days fewer and %.2f Outpost defence; the choice alone "
								 "%.1f and %.2f; an unbought milestone's extra %d floors"),
							Both.WalkDaysFewer, Both.MaxDefenceOutposts, ChoiceAlone.WalkDaysFewer,
							ChoiceAlone.MaxDefenceOutposts, NotBought.FloorsAdded));

	TestEqual(TEXT("The Delver, the choice: 5 days fewer"), Both.WalkDaysFewer, 5.0f, 1.0e-4f);
	TestEqual(TEXT("The Aegis of Hope, the extra option: +50% Outpost defence"), Both.MaxDefenceOutposts, 0.50f, 1.0e-4f);
	TestEqual(TEXT("CONTROL: the choice alone still 5 days fewer"), ChoiceAlone.WalkDaysFewer, 5.0f, 1.0e-4f);
	TestEqual(TEXT("CONTROL: and no Outpost defence"), ChoiceAlone.MaxDefenceOutposts, 0.0f, 1.0e-6f);
	TestEqual(TEXT("an unbought milestone's extra option acts not at all"), NotBought.FloorsAdded, 0);
	return true;
}

/**
 * A respec clears the extra option with every node and choice (J5): Foundations of the Empire and The Imperial Zenith
 * bought with Imperial Prowess and The Aegis of Hope taken, a respec with a run returns both points and leaves no extra
 * option, its node None. Prints "PROWESS RESPEC".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeProwessRespecTest,
	"Cataclysm.EmpireTreeProwess.ARespecClearsTheExtraOption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeProwessRespecTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeProwessTest;

	FCataclysmPassiveAllocation Tree;
	Take(Tree, Foundations, 2);
	Take(Tree, ImperialZenith, 1);
	Tree.SetExtraOption(Foundations, 1);
	double Bank = 0.0;
	FString Reason;
	const int32 Returned = UCataclysmEmpireTree::Respec(Tree, Bank, /*bRunBeingPlayed=*/true, Reason);
	AddInfo(FString::Printf(TEXT("PROWESS RESPEC: %d returned, bank %.1f, extra option %s %d, %d nodes"), Returned, Bank,
							*Tree.ExtraOptionNode.ToString(), Tree.ExtraOption, Tree.Nodes.Num()));

	TestEqual(TEXT("both points come back"), Returned, 2);
	TestEqual(TEXT("no node is left"), Tree.Nodes.Num(), 0);
	TestEqual(TEXT("no extra option is left"), Tree.ExtraOption, 0);
	TestTrue(TEXT("and its node is None"), Tree.ExtraOptionNode.IsNone());
	TestEqual(TEXT("Foundations holds none"), Tree.ExtraOptionIn(Foundations), 0);
	return true;
}

/**
 * The screen's words for the extra option: an option taken as one says "(taken with Imperial Prowess)" in the list
 * ("1  The Aegis of Hope  (taken with Imperial Prowess)"), and the line under the branch names both, the choice and the
 * extra option. CONTROL: an option taken as neither carries no mark. Prints "PROWESS SCREEN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeProwessScreenTest,
	"Cataclysm.EmpireTreeProwess.TheScreenMarksTheExtraOption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeProwessScreenTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeProwessTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const FCataclysmEmpireTreeNodeRow* Row = UCataclysmEmpireTree::FindNode(Nodes, Foundations);
	if (!TestNotNull(TEXT("set-up: Foundations of the Empire's row"), Row))
	{
		return false;
	}
	const FString Taken = UCataclysmEmpireTreeLayout::OptionLabel(1, TEXT("The Aegis of Hope"), false, FString(),
																  TEXT("Imperial Prowess"));
	const FString Plain = UCataclysmEmpireTreeLayout::OptionLabel(3, TEXT("The Hoarder"), false, FString());
	const FString Detail = UCataclysmEmpireTreeLayout::NodeDetail(*Row, 1, 2, FString(), 1, TEXT("Imperial Prowess"));
	AddInfo(FString::Printf(TEXT("PROWESS SCREEN: [%s] [%s] [%s]"), *Taken, *Plain, *Detail));

	TestEqual(TEXT("the extra option's label"), Taken,
			  FString(TEXT("1  The Aegis of Hope  (taken with Imperial Prowess)")));
	TestEqual(TEXT("CONTROL: an option taken as neither"), Plain, FString(TEXT("3  The Hoarder")));
	TestTrue(TEXT("the line names the extra option"),
			 Detail.Contains(TEXT("1 The Aegis of Hope (taken with Imperial Prowess); 2 The Delver (chosen); 3 The "
								  "Hoarder")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
