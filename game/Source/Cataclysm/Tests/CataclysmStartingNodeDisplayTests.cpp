// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmCharacterCreation.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * What a player sees after putting a point in a class's starting node. The owner's play-test of 2026-11-26, on a Death
 * Crossbow character: "I put a point into the fervour generator, it says it's spent, but the fervour node reads 0/1 and
 * I don't have a fervour bar".
 *
 * THE BAR. The heads-up display draws the Fervour bar only for a character `UCataclysmCombatOverlay::FervourOf` says can
 * move Fervour. That asked the rate attributes and the War rates, and so saw no generator in the Soul Collector's,
 * the Necromancer's or the Ravager's starting node. Every class's starting node is read here from the generated table,
 * as the game reads it (`UCataclysmPassiveTree::ModifiersFor`), and each must draw the bar.
 *
 * THE NODE. The point was spent and kept (the owner's save holds `SoulCollector_basic_spine_000` at 1). The passive
 * screen opened on the first reachable tree by name, which for a Death character is the Necromancer's, whose starting
 * node is also "Fervour" and held 0 of 1. It opens on the class chosen (`UCataclysmPassiveTree::TreeToOpenOn`).
 */

namespace CataclysmStartingNodeDisplayTest
{
	/** Each class's starting node, by tree, as `game/Data/PassiveNodes.csv` holds it. */
	const TArray<TPair<const TCHAR*, const TCHAR*>>& StartingNodes()
	{
		static const TArray<TPair<const TCHAR*, const TCHAR*>> Nodes = {
			{TEXT("Berserker"), TEXT("Berserker_trunk_000")},
			{TEXT("Bulwark"), TEXT("Bulwark_basic_trunk_000")},
			{TEXT("Saboteur"), TEXT("Saboteur_basic_trunk_000")},
			{TEXT("Masochist"), TEXT("Masochist_basic_spine_000")},
			{TEXT("Ravager"), TEXT("Ravager_basic_spine_000")},
			{TEXT("Ritualist"), TEXT("Ritualist_basic_spine_000")},
			{TEXT("Soul Collector"), TEXT("SoulCollector_basic_spine_000")},
			{TEXT("Necromancer"), TEXT("Necromancer_basic_spine_000")},
			{TEXT("Shadow"), TEXT("Shadow_basic_spine_000")},
		};
		return Nodes;
	}

	/** The damage type whose three classes include this one. */
	FName DamageTypeOf(const TCHAR* Class)
	{
		for (const TCHAR* Type : {TEXT("War"), TEXT("Demonic"), TEXT("Death")})
		{
			if (UCataclysmCharacterCreation::ClassesFor(FName(Type)).Contains(FName(Class)))
			{
				return FName(Type);
			}
		}
		return NAME_None;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStartingNodeDrawsTheBarTest,
	"Cataclysm.StartingNodeDisplay.EveryClasssStartingNodeDrawsTheFervourBar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * For each of the nine class trees, a creature with a Fervour pool of 100 is given that tree's starting node's rows from
 * the generated table, and the bar must be drawn (`UCataclysmCombatOverlay::FervourOf`). THE CONTROL: with no node, no
 * bar. Before 2026-11-26 the Soul Collector, the Necromancer and the Ravager failed here, and so did the Masochist and
 * the Ritualist, whose rate attributes stat lines alone do not fill.
 */
bool FCataclysmStartingNodeDrawsTheBarTest::RunTest(const FString&)
{
	using namespace CataclysmStartingNodeDisplayTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}

	int32 Index = 0;
	const auto BarFor = [World, Nodes, Effects, &Index](const TCHAR* Node, FName DamageType, bool& bOutDrawn) -> bool
	{
		ACataclysmEnemyCharacter* Holder = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(100.0f * 100.0f * ++Index, 0.0f, 0.0f), FRotator::ZeroRotator);
		UCataclysmAbilitySystemComponent* System =
			Holder ? Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent()) : nullptr;
		if (!System)
		{
			return false;
		}
		System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);

		FCataclysmPassiveAllocation Allocation;
		if (Node)
		{
			Allocation.Add(FName(Node), 1);
		}
		TMap<FName, FCataclysmStatInputs> Lines;
		for (const TPair<FName, TArray<FCataclysmStatModifier>>& Row :
			 UCataclysmPassiveTree::ModifiersFor(Allocation, Nodes, Effects, {DamageType}))
		{
			Lines.FindOrAdd(Row.Key).Modifiers = Row.Value;
		}
		System->SetStatInputs(MoveTemp(Lines));

		float Fervour = 0.0f;
		float Maximum = 0.0f;
		bOutDrawn = UCataclysmCombatOverlay::FervourOf(Holder, Fervour, Maximum);
		return true;
	};

	bool bControl = true;
	if (!TestTrue(TEXT("set-up: the control holder"), BarFor(nullptr, FName(TEXT("Death")), bControl)))
	{
		return false;
	}
	TestFalse(TEXT("control: with no starting node, no bar"), bControl);

	for (const TPair<const TCHAR*, const TCHAR*>& Start : StartingNodes())
	{
		const FName DamageType = DamageTypeOf(Start.Key);
		if (!TestTrue(FString::Printf(TEXT("set-up: %s has a damage type"), Start.Key), !DamageType.IsNone())
			|| !TestNotNull(FString::Printf(TEXT("set-up: %s is in the node table"), Start.Value),
							UCataclysmPassiveTree::FindNode(Nodes, FName(Start.Value))))
		{
			continue;
		}
		bool bDrawn = false;
		if (TestTrue(FString::Printf(TEXT("set-up: a %s holder"), Start.Key), BarFor(Start.Value, DamageType, bDrawn)))
		{
			TestTrue(FString::Printf(TEXT("A %s HOLDING ITS STARTING NODE, %s, DRAWS THE FERVOUR BAR"), Start.Key,
									 Start.Value),
					 bDrawn);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveScreenOpensOnTheChosenClassTest,
	"Cataclysm.StartingNodeDisplay.ThePassiveScreenOpensOnTheClassChosenNotTheFirstByName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmPassiveTree::TreeToOpenOn`, which the passive screen asks when it opens. For each damage type, a character
 * with one point in the starting node of the class sorted LAST of its three opens on that class. THE CONTROL: with no
 * point spent it opens on the first of the three by name, as before.
 */
bool FCataclysmPassiveScreenOpensOnTheChosenClassTest::RunTest(const FString&)
{
	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes))
	{
		return false;
	}

	struct FCase
	{
		const TCHAR* DamageType;
		const TCHAR* FirstByName;
		const TCHAR* Chosen;
		const TCHAR* ChosenStartingNode;
	};
	const FCase Cases[] = {
		{TEXT("Death"), TEXT("Necromancer"), TEXT("Soul Collector"), TEXT("SoulCollector_basic_spine_000")},
		{TEXT("Demonic"), TEXT("Masochist"), TEXT("Ritualist"), TEXT("Ritualist_basic_spine_000")},
		{TEXT("War"), TEXT("Berserker"), TEXT("Saboteur"), TEXT("Saboteur_basic_trunk_000")},
	};
	for (const FCase& Case : Cases)
	{
		const TArray<FName> Carried = {FName(Case.DamageType)};
		TestEqual(FString::Printf(TEXT("control: a %s character with no point opens on %s"), Case.DamageType,
								  Case.FirstByName),
				  UCataclysmPassiveTree::TreeToOpenOn(Nodes, FCataclysmPassiveAllocation(), Carried),
				  FString(Case.FirstByName));

		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(Case.ChosenStartingNode), 1);
		TestEqual(FString::Printf(TEXT("A %s CHARACTER WITH A POINT IN %s OPENS ON %s"), Case.DamageType,
								  Case.ChosenStartingNode, Case.Chosen),
				  UCataclysmPassiveTree::TreeToOpenOn(Nodes, Allocation, Carried), FString(Case.Chosen));
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
