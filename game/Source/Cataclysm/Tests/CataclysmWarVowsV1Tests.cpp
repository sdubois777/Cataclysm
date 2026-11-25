// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THE WAR VOW OPTIONS' FIRST SLICE, 2026-11-25: nine rows on eight options, each on a stat, scale, condition or pool
 * action the game already reads, on the coordinating session's rulings on the vow survey that day.
 *
 * WHAT IS EXERCISED: the generated table, as the game loads it, through the two ways a passive row reaches a character
 * -- `UCataclysmPassiveTree::ModifiersFor` for a stat row and `EnchantmentRowsFor` for a row stating an action or a
 * second condition -- with the option taken and, as the control, another option of the same capstone taken; and the two
 * health oaths handed to a real War player's pool actions by `ACataclysmPlayerState::SetPassiveAllocation`. What each
 * pool action then does on a block or a second is the pool actions' own tests'.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV1Test
{
	/** A capstone with one point and `Option` taken. */
	FCataclysmPassiveAllocation WarVowsV1Taking(const TCHAR* Node, int32 Option)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(Node), 1);
		Allocation.SetChosenOption(FName(Node), Option);
		return Allocation;
	}

	/** What the generated stat rows of `Node`'s `Option` give `Stat`, judged by the pipeline from `Base` in `State`. */
	FCataclysmStatBreakdown WarVowsV1Stat(const UDataTable* Nodes, const UDataTable* Effects, const TCHAR* Node,
										  int32 Option, const TCHAR* Stat, float Base,
										  const FCataclysmStatConditions& State)
	{
		const TMap<FName, TArray<FCataclysmStatModifier>> Granted = UCataclysmPassiveTree::ModifiersFor(
			WarVowsV1Taking(Node, Option), Nodes, Effects, {FName(TEXT("War"))});
		const TArray<FCataclysmStatModifier>* Rows = Granted.Find(FName(Stat));
		if (!Rows)
		{
			FCataclysmStatBreakdown None;
			None.Final = Base;
			return None;
		}
		return UCataclysmStatPipeline::Evaluate(Base, *Rows, FGameplayTagContainer(), State);
	}

	/** The rows of `Node`'s `Option` copied for the enchantment builder, with this stat or action. */
	TArray<FCataclysmEnchantmentEffectRow> WarVowsV1Copies(const UDataTable* Nodes, const UDataTable* Effects,
														   const TCHAR* Node, int32 Option, const TCHAR* StatOrAction)
	{
		TArray<FCataclysmEnchantmentEffectRow> Found;
		for (const FCataclysmEnchantmentEffectRow& Row : UCataclysmPassiveTree::EnchantmentRowsFor(
				 WarVowsV1Taking(Node, Option), Nodes, Effects, {FName(TEXT("War"))}))
		{
			if (Row.Stat == StatOrAction || Row.Action == StatOrAction)
			{
				Found.Add(Row);
			}
		}
		return Found;
	}

	/** How many of `System`'s pool actions move health on `Event`. */
	TArray<FCataclysmPoolAction> WarVowsV1HealthActions(const UCataclysmAbilitySystemComponent* System, const TCHAR* Event)
	{
		TArray<FCataclysmPoolAction> Found;
		for (const FCataclysmPoolAction& Action : System->GetPoolActions())
		{
			if (Action.Pool == FName(TEXT("health")) && Action.Event == FName(Event))
			{
				Found.Add(Action);
			}
		}
		return Found;
	}
}

#define CATACLYSM_WAR_VOWS_V1_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The stat rows
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V1_TEST(FCataclysmWarVowsV1StatRowsTest,
	"Cataclysm.WarVowsV1.EachOptionsStatRowsGiveItsFiguresAndAnotherOptionGivesNone")
{
	using namespace CataclysmWarVowsV1Test;
	// THE GENERATED STAT ROWS, judged by the pipeline, each against another option of its capstone (the control):
	// Eternal Hunger 50% increased leech; Ragnarok's drain of 5 removed in Wrath and kept out of it; Colossus Eternal
	// doubled healing received; Wrath Incarnate 10% retaliation at 1,000 armour; Bloodied but Unbroken 30% at 30% of
	// maximum health missing.
	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table loads"), Nodes)
		|| !TestNotNull(TEXT("set-up: the effect table loads"), Effects))
	{
		return false;
	}
	// THREE AND TWO SINCE THE SECOND SLICE, V2a, the same day: Ragnarok's doubling and Bastion.
	struct FCapstoneRows
	{
		const TCHAR* Node;
		int32 Rows;
	};
	for (const FCapstoneRows& Each : TArray<FCapstoneRows>{{TEXT("Berserker_capstone_200"), 3},
														   {TEXT("Bulwark_capstone_25"), 2},
														   {TEXT("Bulwark_capstone_100"), 2},
														   {TEXT("Bulwark_capstone_200"), 2}})
	{
		const int32 Held = UCataclysmPassiveTree::EffectsFor(Effects, FName(Each.Node)).Num();
		if (Held != Each.Rows)
		{
			AddError(FString::Printf(
				TEXT("DT_PassiveEffects holds %d rows for %s, not %d. If game/Data/PassiveEffects.csv has them, the "
					 "asset is older than the CSV: run  python tools/run_editor_python.py "
					 "tools/generate_datatable_assets.py"),
				Held, Each.Node, Each.Rows));
			return false;
		}
	}
	FCataclysmStatConditions Anywhere;
	const auto Stat = [&](const TCHAR* Node, int32 Option, const TCHAR* Name, float Base,
						  const FCataclysmStatConditions& State)
	{
		return WarVowsV1Stat(Nodes, Effects, Node, Option, Name, Base, State);
	};

	TestEqual(TEXT("Eternal Hunger: 50% increased life leech"),
			  Stat(TEXT("Berserker_capstone_200"), 3, TEXT("life_leech"), 0.0f, Anywhere).SumOfIncreases, 50.0f, 0.001f);
	TestEqual(TEXT("control: Thousand Deaths gives none"),
			  Stat(TEXT("Berserker_capstone_200"), 2, TEXT("life_leech"), 0.0f, Anywhere).SumOfIncreases, 0.0f, 0.001f);

	FCataclysmStatConditions InWrath;
	InWrath.bInWrath = true;
	TestEqual(TEXT("Ragnarok: a drain of 5 is removed in Wrath"),
			  Stat(TEXT("Berserker_capstone_200"), 1, TEXT("fervour_drain_per_second"), 5.0f, InWrath).Final, 0.0f, 0.001f);
	TestEqual(TEXT("control: and kept out of Wrath"),
			  Stat(TEXT("Berserker_capstone_200"), 1, TEXT("fervour_drain_per_second"), 5.0f, Anywhere).Final, 5.0f,
			  0.001f);
	TestEqual(TEXT("control: Eternal Hunger removes no drain in Wrath"),
			  Stat(TEXT("Berserker_capstone_200"), 3, TEXT("fervour_drain_per_second"), 5.0f, InWrath).Final, 5.0f,
			  0.001f);

	TestEqual(TEXT("Colossus Eternal: healing received doubled"),
			  Stat(TEXT("Bulwark_capstone_200"), 2, TEXT("healing_received"), 0.0f, Anywhere).MoreMultiplier, 2.0f,
			  0.001f);
	TestEqual(TEXT("control: Wrath Incarnate does not double it"),
			  Stat(TEXT("Bulwark_capstone_200"), 3, TEXT("healing_received"), 0.0f, Anywhere).MoreMultiplier, 1.0f,
			  0.001f);

	FCataclysmStatConditions Armoured;
	Armoured.Armor = 1000.0f;
	TestEqual(TEXT("Wrath Incarnate: 10% retaliation at 1,000 armour"),
			  Stat(TEXT("Bulwark_capstone_200"), 3, TEXT("retaliation"), 0.0f, Armoured).SumOfIncreases, 10.0f, 0.001f);
	TestEqual(TEXT("control: Colossus Eternal gives none"),
			  Stat(TEXT("Bulwark_capstone_200"), 2, TEXT("retaliation"), 0.0f, Armoured).SumOfIncreases, 0.0f, 0.001f);

	FCataclysmStatConditions Hurt;
	Hurt.HealthPercent = 70.0f;
	TestEqual(TEXT("Bloodied but Unbroken: 30% retaliation at 30% of maximum health missing"),
			  Stat(TEXT("Bulwark_capstone_100"), 2, TEXT("retaliation"), 0.0f, Hurt).SumOfIncreases, 30.0f, 0.001f);
	TestEqual(TEXT("control: Bastion gives none"),
			  Stat(TEXT("Bulwark_capstone_100"), 1, TEXT("retaliation"), 0.0f, Hurt).SumOfIncreases, 0.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// The rows copied for the enchantment builder
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V1_TEST(FCataclysmWarVowsV1CopiedRowsTest,
	"Cataclysm.WarVowsV1.UnstoppableAndTheHealthOathsAreCopiedForTheBuilderOnlyWhenTaken")
{
	using namespace CataclysmWarVowsV1Test;
	// A ROW WITH A SECOND CONDITION OR AN ACTION IS COPIED FOR THE ENCHANTMENT BUILDER. Unstoppable's two flags, above
	// 75% Fervour with two hands; Shield Oath's 2% of maximum health on a block; Flesh Oath's 1% of current health every
	// second. THE CONTROLS: the same capstones' third options copy none of them.
	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table loads"), Nodes)
		|| !TestNotNull(TEXT("set-up: the effect table loads"), Effects))
	{
		return false;
	}
	const auto Copies = [&](const TCHAR* Node, int32 Option, const TCHAR* StatOrAction)
	{
		return WarVowsV1Copies(Nodes, Effects, Node, Option, StatOrAction);
	};

	for (const TCHAR* Flag : {TEXT("movement_speed_reduction_suppressed"), TEXT("knockback_suppressed")})
	{
		const TArray<FCataclysmEnchantmentEffectRow> Taken = Copies(TEXT("Berserker_capstone_50"), 1, Flag);
		if (TestEqual(*FString::Printf(TEXT("Unstoppable copies %s once"), Flag), Taken.Num(), 1))
		{
			TestEqual(TEXT("as a flag of 1"), Taken[0].ValueLow, 1.0f, 0.001f);
			TestEqual(TEXT("above"), Taken[0].Condition, FString(TEXT("class_resource_above")));
			TestEqual(TEXT("75% Fervour"), Taken[0].ConditionValue, 75.0f, 0.001f);
			TestEqual(TEXT("with two hands"), Taken[0].Condition2, FString(TEXT("wielding_two_handed_weapon")));
		}
		TestEqual(*FString::Printf(TEXT("control: Thrill of the Hunt copies no %s"), Flag),
				  Copies(TEXT("Berserker_capstone_50"), 3, Flag).Num(), 0);
	}

	const TArray<FCataclysmEnchantmentEffectRow> Shield = Copies(TEXT("Bulwark_capstone_25"), 1, TEXT("health"));
	if (TestEqual(TEXT("Shield Oath copies one health action"), Shield.Num(), 1))
	{
		TestEqual(TEXT("2%"), Shield[0].ValueLow, 2.0f, 0.001f);
		TestEqual(TEXT("on a block"), Shield[0].ActionEvent, FString(TEXT("block")));
		TestEqual(TEXT("of maximum health"), Shield[0].FractionOf, FString(TEXT("maximum")));
	}
	const TArray<FCataclysmEnchantmentEffectRow> Flesh = Copies(TEXT("Bulwark_capstone_25"), 2, TEXT("health"));
	if (TestEqual(TEXT("Flesh Oath copies one health action"), Flesh.Num(), 1))
	{
		TestEqual(TEXT("1%"), Flesh[0].ValueLow, 1.0f, 0.001f);
		TestEqual(TEXT("on the clock"), Flesh[0].ActionEvent, FString(TEXT("every_seconds")));
		TestEqual(TEXT("every second"), Flesh[0].EverySeconds, 1.0f, 0.001f);
		TestEqual(TEXT("of current health"), Flesh[0].FractionOf, FString(TEXT("current")));
	}
	TestEqual(TEXT("control: Blood Oath copies no health action"),
			  Copies(TEXT("Bulwark_capstone_25"), 3, TEXT("health")).Num(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// A real War player
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V1_TEST(FCataclysmWarVowsV1PlayerTest,
	"Cataclysm.WarVowsV1.AWarPlayerTakingAHealthOathIsHandedItsHealthAction")
{
	using namespace CataclysmWarVowsV1Test;
	// A LEVEL 10 WAR GREATAXE PLAYER, through `SetPassiveAllocation`, the way the game hands a build over. THE CONTROL:
	// the First Oath's third option, Blood Oath, hands over no health action. Shield Oath hands over 2% of maximum health
	// on a block; Flesh Oath, 1% of current health every second.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!State || !Controller || !Character)
	{
		AddError(TEXT("set-up: a player state, a controller and a character"));
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Character);
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	if (!TestNotNull(TEXT("set-up: equipment"), Equipment) || !TestNotNull(TEXT("set-up: an ability system"), System))
	{
		return false;
	}
	State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("War")));
	State->SetLevelAndExperience(10, 0);
	Equipment->RefreshAttributes(System);
	if (!TestTrue(TEXT("set-up: it reaches the Bulwark tree"), State->ReachableTrees().Contains(TEXT("Bulwark"))))
	{
		return false;
	}
	const auto Take = [&](int32 Option)
	{
		State->SetPassiveAllocation(WarVowsV1Taking(TEXT("Bulwark_capstone_25"), Option), TArray<FName>());
		Equipment->RefreshAttributes(System);
	};

	Take(3);
	TestEqual(TEXT("control: Blood Oath hands over no health action on a block"),
			  WarVowsV1HealthActions(System, TEXT("block")).Num(), 0);
	TestEqual(TEXT("control: nor on the clock"), WarVowsV1HealthActions(System, TEXT("every_seconds")).Num(), 0);

	Take(1);
	const TArray<FCataclysmPoolAction> Shield = WarVowsV1HealthActions(System, TEXT("block"));
	if (TestEqual(TEXT("Shield Oath hands over one health action on a block"), Shield.Num(), 1))
	{
		TestEqual(TEXT("of 2%"), Shield[0].Percent, 2.0f, 0.001f);
		TestTrue(TEXT("of maximum health"), Shield[0].Base == ECataclysmPoolActionBase::Maximum);
	}
	TestEqual(TEXT("and none on the clock"), WarVowsV1HealthActions(System, TEXT("every_seconds")).Num(), 0);

	Take(2);
	const TArray<FCataclysmPoolAction> Flesh = WarVowsV1HealthActions(System, TEXT("every_seconds"));
	if (TestEqual(TEXT("Flesh Oath hands over one health action on the clock"), Flesh.Num(), 1))
	{
		TestEqual(TEXT("of 1%"), Flesh[0].Percent, 1.0f, 0.001f);
		TestTrue(TEXT("of current health"), Flesh[0].Base == ECataclysmPoolActionBase::Current);
		TestEqual(TEXT("every second"), Flesh[0].EverySeconds, 1.0f, 0.001f);
	}
	TestEqual(TEXT("and none on a block"), WarVowsV1HealthActions(System, TEXT("block")).Num(), 0);
	return true;
}

#undef CATACLYSM_WAR_VOWS_V1_TEST

#endif // WITH_AUTOMATION_TESTS
