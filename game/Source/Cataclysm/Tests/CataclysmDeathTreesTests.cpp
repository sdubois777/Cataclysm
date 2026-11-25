// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmClassStats.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
// For the bucket, condition and scale an authored row's modifier carries.
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmPassiveTreeWidget.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

/**
 * The three Death class trees, entered as data on 2026-11-24.
 *
 * WHAT IS NEW HERE AND COULD GO WRONG WITHOUT AN ERROR. The Soul Collector is the
 * first class whose name holds a space. The tree files, the generator and the
 * save step each spell it their own way: the file is
 * `docs/Soul_Collector_Class_Tree_Final.json`, every row is named
 * `SoulCollector_...` because that is how the save step reads a node's tree back
 * out of its name, and the node table's `Tree` column says "Soul Collector",
 * because that is how `UCataclysmCharacterCreation::ClassesFor` spells the class
 * and `UCataclysmPassiveTree::TreeIsReachable` compares the two as written. A
 * slip in any one of those leaves a tree a Death character cannot reach, a node
 * no lookup finds, or a class line nothing reads -- and each of those imports
 * cleanly and fails silently.
 *
 * WHAT THESE ARE NOT. The Death trees' own mechanics -- Fervour per kill, its
 * decay, remains, the Siphon -- are later slices. These cover the data and the
 * forty-odd rows that are stats the game already had.
 */

namespace CataclysmDeathTreesTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/** A possessed player character in this world, or null. The shape
	 *  `CataclysmPassiveTreeTests.cpp` uses for the screen test. */
	ACataclysmPlayerCharacter* SpawnPossessedDeathTreesPlayer(UWorld* World)
	{
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector,
														 FRotator::ZeroRotator);
		if (!PlayerState || !Controller || !Character)
		{
			return nullptr;
		}
		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Character);
		return Character;
	}

	/** The one modifier a stat holds in a set of totals, or null when it holds
	 *  none or several. */
	const FCataclysmStatModifier* OnlyModifierOf(
		const TMap<FName, TArray<FCataclysmStatModifier>>& Totals, const TCHAR* Stat)
	{
		const TArray<FCataclysmStatModifier>* Found = Totals.Find(FName(Stat));
		return (Found && Found->Num() == 1) ? &(*Found)[0] : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathTreesReachableTest,
	"Cataclysm.DeathTrees.ADeathWeaponReachesTheThreeDeathTreesAndNoOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathTreesReachableTest::RunTest(const FString&)
{
	const FName Death(TEXT("Death"));
	const FName Demonic(TEXT("Demonic"));

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("the passive node table loads"), NodeTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	// EXACTLY THE THREE, in the table's sorted order. A Death weapon reaches no
	// Demonic or War tree, and every Death class has a tree to spend in.
	const TArray<FString> ForDeath =
		UCataclysmPassiveTree::ReachableTrees(NodeTable, {Death});
	TestEqual(TEXT("a Death character reaches three trees"), ForDeath.Num(), 3);
	TestTrue(TEXT("the Necromancer's"), ForDeath.Contains(TEXT("Necromancer")));
	TestTrue(TEXT("the Shadow's"), ForDeath.Contains(TEXT("Shadow")));
	TestTrue(TEXT("and the Soul Collector's, spelt with its space"),
			 ForDeath.Contains(TEXT("Soul Collector")));

	// THE CONTROL: a Demonic character reaches its own three and none of these.
	const TArray<FString> ForDemonic =
		UCataclysmPassiveTree::ReachableTrees(NodeTable, {Demonic});
	TestTrue(TEXT("a Demonic character reaches the Ravager"),
			 ForDemonic.Contains(TEXT("Ravager")));
	TestFalse(TEXT("and not the Soul Collector"),
			  ForDemonic.Contains(TEXT("Soul Collector")));
	TestFalse(TEXT("nor the Shadow"), ForDemonic.Contains(TEXT("Shadow")));

	// AND THE SPACE IS WHAT MAKES IT A CLASS. The row names drop it; the tree's
	// name must not, or no damage type would reach the tree at all.
	TestFalse(TEXT("\"SoulCollector\" is no class a damage type unlocks"),
			  UCataclysmPassiveTree::TreeIsReachable(TEXT("SoulCollector"), {Death}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathTreesNamedTest,
	"Cataclysm.DeathTrees.ADeathNodeIsNamedForItsTreeAndEachTreeStartsAtFervour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathTreesNamedTest::RunTest(const FString&)
{
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EdgeTable = UCataclysmPassiveTree::LoadEdgeTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the edge table loads"), EdgeTable))
	{
		return false;
	}

	// THE ROW NAME DROPS THE SPACE AND THE TREE KEEPS IT.
	const FCataclysmPassiveNodeRow* Root = UCataclysmPassiveTree::FindNode(
		NodeTable, FName(TEXT("SoulCollector_basic_spine_000")));
	if (!TestNotNull(TEXT("the Soul Collector's starting node is found by its row name"),
					 Root))
	{
		return false;
	}
	TestEqual(TEXT("its tree is the class as the game spells it"), Root->Tree,
			  FString(TEXT("Soul Collector")));
	TestEqual(TEXT("and it is the Fervour node"), Root->NodeName, FString(TEXT("Fervour")));
	TestNull(TEXT("no row is named with the space"),
			 UCataclysmPassiveTree::FindNode(
				 NodeTable, FName(TEXT("Soul Collector_basic_spine_000"))));

	// EVERY NODE OF THE THREE FILES, AND NONE UNDER THE SPACE-FREE NAME.
	TestEqual(TEXT("the Soul Collector has its 73 nodes"),
			  UCataclysmPassiveTree::NodesIn(NodeTable, TEXT("Soul Collector")).Num(), 73);
	// 73 SINCE THE OWNER'S ORDER FIXES OF 2026-11-24 added Many Made One.
	TestEqual(TEXT("the Necromancer its 73"),
			  UCataclysmPassiveTree::NodesIn(NodeTable, TEXT("Necromancer")).Num(), 73);
	TestEqual(TEXT("the Shadow its 74"),
			  UCataclysmPassiveTree::NodesIn(NodeTable, TEXT("Shadow")).Num(), 74);
	TestEqual(TEXT("and no tree is called \"SoulCollector\""),
			  UCataclysmPassiveTree::NodesIn(NodeTable, TEXT("SoulCollector")).Num(), 0);

	// EACH STARTS FROM ONE NODE, AND IT IS THE ONE NAMED FERVOUR: the node that
	// says how the class fills the shared bar. An edge copied with the wrong
	// prefix would leave a second node open, or none.
	FCataclysmPassiveAllocation Nothing;
	for (const TCHAR* Tree : {TEXT("Soul Collector"), TEXT("Necromancer"), TEXT("Shadow")})
	{
		TArray<FString> Open;
		for (const FName& Node : UCataclysmPassiveTree::NodesIn(NodeTable, Tree))
		{
			const FCataclysmPassiveNodeRow* Row = UCataclysmPassiveTree::FindNode(NodeTable, Node);
			if (Row && Row->Kind != UCataclysmPassiveTree::CapstoneKind
				&& UCataclysmPassiveTree::EdgesAllow(NodeTable, EdgeTable, Nothing, Node))
			{
				Open.Add(Row->NodeName);
			}
		}
		TestEqual(*FString::Printf(TEXT("%s opens at one node"), Tree), Open.Num(), 1);
		TestTrue(*FString::Printf(TEXT("%s's open node is Fervour"), Tree),
				 Open.Num() == 1 && Open[0] == TEXT("Fervour"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathTreesOneClassTest,
	"Cataclysm.DeathTrees.ADeathCharacterSpendsInOnlyOneOfTheThreeDeathTrees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathTreesOneClassTest::RunTest(const FString&)
{
	using namespace CataclysmDeathTreesTest;

	const FName Death(TEXT("Death"));
	const FName Demonic(TEXT("Demonic"));

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A PLAYER STATE WITH NO PAWN, so the damage type it carries is its creation
	// type alone (`ACataclysmPlayerState::CarriedDamageTypes`).
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("a player state was spawned"), State))
	{
		return false;
	}
	State->SetCreationChoice(FName(TEXT("Dagger")), Death);

	const TArray<FString> Reachable = State->ReachableTrees();
	TestTrue(TEXT("a Death character reaches the Soul Collector's tree"),
			 Reachable.Contains(TEXT("Soul Collector")));

	// ONE POINT IN THE SOUL COLLECTOR CHOOSES IT AS THE DEATH CLASS.
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("SoulCollector_basic_spine_000")), 1);
	State->SetPassiveAllocation(Allocation, {});

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	TestEqual(TEXT("the Soul Collector is the Death class chosen"),
			  UCataclysmPassiveTree::ClassChosenFor(NodeTable, State->GetPassiveAllocation(), Death),
			  FString(TEXT("Soul Collector")));
	TestEqual(TEXT("and no Demonic class is chosen by it"),
			  UCataclysmPassiveTree::ClassChosenFor(NodeTable, State->GetPassiveAllocation(), Demonic),
			  FString());

	// SO A POINT IN THE SHADOW IS REFUSED, AND THE REASON NAMES THE CLASS HELD.
	FString Reason;
	TestFalse(TEXT("a point in the Shadow's starting node is refused"),
			  State->SpendPassivePoint(FName(TEXT("Shadow_basic_spine_000")), Reason));
	TestTrue(FString::Printf(TEXT("and the reason names the Soul Collector: \"%s\""), *Reason),
			 Reason.Contains(TEXT("Soul Collector is your Death class")));

	// THE CONTROL: the Soul Collector's own tree is not refused for its class.
	TestTrue(TEXT("nothing refuses the class its own tree"),
			 UCataclysmPassiveTree::RefusalForClassChoice(
				 NodeTable, State->GetPassiveAllocation(), TEXT("Soul Collector"),
				 {Death}).IsEmpty());

	// AND A DEMONIC TREE IS REFUSED FOR THE WEAPON, not for the class.
	Reason.Reset();
	TestFalse(TEXT("a point in the Ravager is refused"),
			  State->SpendPassivePoint(FName(TEXT("Ravager_basic_spine_000")), Reason));
	TestTrue(FString::Printf(TEXT("because no weapon carries Demonic: \"%s\""), *Reason),
			 Reason.Contains(TEXT("No equipped weapon")));
	TestEqual(TEXT("and nothing was spent"), State->GetPassiveAllocation().Total(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathTreesRowsTest,
	"Cataclysm.DeathTrees.ADeathNodesRowGrantsItsStatOnlyWhileADeathWeaponIsCarried",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathTreesRowsTest::RunTest(const FString&)
{
	using namespace CataclysmDeathTreesTest;

	const FName Death(TEXT("Death"));
	const FName Demonic(TEXT("Demonic"));

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		return false;
	}

	// THE SHADOW'S Keen Edge, TWO POINTS: "+3% increased Critical Strike Chance
	// per point."
	FCataclysmPassiveAllocation KeenEdge;
	KeenEdge.Add(FName(TEXT("Shadow_basic_b_stem0")), 2);
	const TMap<FName, TArray<FCataclysmStatModifier>> WithDeath =
		UCataclysmPassiveTree::ModifiersFor(KeenEdge, NodeTable, EffectTable, {Death});
	const FCataclysmStatModifier* Chance = OnlyModifierOf(WithDeath, TEXT("crit_chance"));
	if (TestNotNull(TEXT("Keen Edge grants one critical strike chance modifier"), Chance))
	{
		TestEqual(TEXT("in the increased bucket"), Chance->Bucket, ECataclysmStatBucket::Increased);
		TestEqual(TEXT("worth 3 a point for 2 points"), Chance->Value, 6.0f);
	}

	// THE CONTROL: the same points with only a Demonic weapon grant nothing,
	// which is the owner's rule of 2026-08-25 reaching the new trees.
	const TMap<FName, TArray<FCataclysmStatModifier>> WithDemonic =
		UCataclysmPassiveTree::ModifiersFor(KeenEdge, NodeTable, EffectTable, {Demonic});
	TestNull(TEXT("with a Demonic weapon Keen Edge grants nothing"),
			 WithDemonic.Find(FName(TEXT("crit_chance"))));

	// A SCALED ROW AND A CONDITIONED ROW CARRY THEIR SCALE AND CONDITION. Held
	// Breath: "+1% increased damage per point for every full 20 Fervour you
	// currently hold". Executioner: "100% more damage against enemies below 30%
	// health".
	FCataclysmPassiveAllocation Shadow;
	Shadow.Add(FName(TEXT("Shadow_basic_spine_005")), 3);
	Shadow.Add(FName(TEXT("Shadow_keystone_b_kA")), 1);
	const TMap<FName, TArray<FCataclysmStatModifier>> ShadowRows =
		UCataclysmPassiveTree::ModifiersFor(Shadow, NodeTable, EffectTable, {Death});
	const TArray<FCataclysmStatModifier>* Attack = ShadowRows.Find(FName(TEXT("attack_damage")));
	if (TestNotNull(TEXT("the two nodes grant attack damage"), Attack)
		&& TestEqual(TEXT("two attack damage modifiers"), Attack->Num(), 2))
	{
		const FCataclysmStatModifier* Held = nullptr;
		const FCataclysmStatModifier* Low = nullptr;
		for (const FCataclysmStatModifier& Each : *Attack)
		{
			(Each.Bucket == ECataclysmStatBucket::More ? Low : Held) = &Each;
		}
		if (TestNotNull(TEXT("Held Breath's"), Held) && TestNotNull(TEXT("Executioner's"), Low))
		{
			TestEqual(TEXT("Held Breath is 1 a point for 3 points"), Held->Value, 3.0f);
			TestEqual(TEXT("scaled by the Fervour held"), Held->Scale,
					  ECataclysmStatScale::PerPointOfClassResourceHeld);
			TestEqual(TEXT("in steps of 20"), Held->ScaleStep, 20.0f);
			TestEqual(TEXT("Executioner is 100% more"), Low->Value, 100.0f);
			TestEqual(TEXT("against a target below a share of its health"), Low->Condition,
					  ECataclysmStatCondition::TargetHealthBelowPercent);
			TestEqual(TEXT("of 30%"), Low->ConditionValue, 30.0f);
		}
	}

	// AND A SOUL COLLECTOR ROW, under the space-free row name. Deep Urn: "+2%
	// increased maximum Fervour per point".
	FCataclysmPassiveAllocation DeepUrn;
	DeepUrn.Add(FName(TEXT("SoulCollector_basic_spine_008")), 4);
	// KEPT IN A LOCAL: the modifier found is a pointer into this map.
	const TMap<FName, TArray<FCataclysmStatModifier>> DeepUrnRows =
		UCataclysmPassiveTree::ModifiersFor(DeepUrn, NodeTable, EffectTable, {Death});
	const FCataclysmStatModifier* Pool = OnlyModifierOf(DeepUrnRows, TEXT("class_resource"));
	if (TestNotNull(TEXT("Deep Urn grants maximum Fervour"), Pool))
	{
		TestEqual(TEXT("2 a point for 4 points"), Pool->Value, 8.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathTreesClassLinesTest,
	"Cataclysm.DeathTrees.TheThreeDeathClassLinesAreReadUnderTheClassesOwnNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathTreesClassLinesTest::RunTest(const FString&)
{
	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!TestNotNull(TEXT("the class stat table loads"), Table))
	{
		return false;
	}

	// LEVEL 20, SO THE PER-LEVEL GAIN IS IN EVERY FIGURE: base + 19 x gain.
	// The Soul Collector's health is 115 + 19 x 17 and the Necromancer's armour
	// 15 + 19 x 2. The Shadow's area of effect has no gain: 90 at every level.
	TestEqual(TEXT("the Soul Collector's health, read with the space in its name"),
			  UCataclysmClassStats::BaseFor(Table, TEXT("Soul Collector"), TEXT("max_health"), 20),
			  438.0f);
	TestEqual(TEXT("the Necromancer's armour"),
			  UCataclysmClassStats::BaseFor(Table, TEXT("Necromancer"), TEXT("armor"), 20), 53.0f);
	TestEqual(TEXT("the Shadow's smaller area of effect"),
			  UCataclysmClassStats::BaseFor(Table, TEXT("Shadow"), TEXT("area_of_effect"), 20), 90.0f);

	// WHAT EACH REFUSES IS NOUGHT, and that is the design: the Shadow takes no
	// armour, and no class -- the Shadow included -- has a base of evasion; the
	// Shadow's comes from its tree's Counterstroke.
	TestEqual(TEXT("the Shadow has no armour"),
			  UCataclysmClassStats::BaseFor(Table, TEXT("Shadow"), TEXT("armor"), 20), 0.0f);
	TestEqual(TEXT("the Shadow has no evasion from its class"),
			  UCataclysmClassStats::BaseFor(Table, TEXT("Shadow"), TEXT("evasion"), 20), 0.0f);

	// THE CONTROL: the space-free spelling names no line, so it falls back to
	// the shared default, 100 + 19 x 15. A character on that line would have
	// none of what its class is for.
	TestEqual(TEXT("\"SoulCollector\" reads the default line"),
			  UCataclysmClassStats::BaseFor(Table, TEXT("SoulCollector"), TEXT("max_health"), 20),
			  385.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathTreesScreenTest,
	"Cataclysm.DeathTrees.ThePassiveScreenShowsTheSoulCollectorsWholeTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathTreesScreenTest::RunTest(const FString&)
{
	using namespace CataclysmDeathTreesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedDeathTreesPlayer(World);
	if (!TestNotNull(TEXT("a possessed player character"), Character))
	{
		return false;
	}
	APlayerController* Controller = Cast<APlayerController>(Character->GetController());
	ACataclysmPlayerState* State = Character->GetPlayerState<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("a controller"), Controller) || !TestNotNull(TEXT("a player state"), State))
	{
		return false;
	}

	// `NewObject` RATHER THAN `CreateWidget`, for the reason the screen test in
	// `CataclysmPassiveTreeTests.cpp` gives: a world made for a test has no
	// local player, and `CreateWidget` refuses a controller without one.
	UCataclysmPassiveTreeWidget* Screen = NewObject<UCataclysmPassiveTreeWidget>(Controller);
	if (!TestNotNull(TEXT("the screen was created"), Screen))
	{
		return false;
	}
	Screen->SetPlayerStateForTests(State);

	TestTrue(TEXT("the Soul Collector's tree can be shown"), Screen->ShowTree(TEXT("Soul Collector")));
	TestEqual(TEXT("and it is the one shown"), Screen->GetShownTree(), FString(TEXT("Soul Collector")));
	TestEqual(TEXT("with all 73 of its nodes"), Screen->ShownNodes().Num(), 73);

	// THE CONTROL: the row names' spelling is not a tree the screen knows, and
	// asking for it leaves the shown tree where it was.
	TestFalse(TEXT("\"SoulCollector\" is not a tree"), Screen->ShowTree(TEXT("SoulCollector")));
	TestEqual(TEXT("so the Soul Collector's is still shown"), Screen->GetShownTree(),
			  FString(TEXT("Soul Collector")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
