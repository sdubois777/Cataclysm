// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

/**
 * THE PASSIVE ROW PORT OF 2026-11-24 (`plan_death_and_war.md`, slice B28).
 *
 * A passive row may now carry the enchantment row's action columns. A row stating `Action`, or scaled by
 * `own_stacks` or `consecutive_hits`, is not made a modifier by `UCataclysmPassiveTree::AccumulateInto`:
 * `UCataclysmPassiveTree::EnchantmentRowsFor` copies it into an enchantment row, and the equipment's refresh hands
 * the copies to `UCataclysmItemModifiers::AccumulateEnchantmentsInto`, the builder every worn row goes through. A stat
 * row may also carry a second condition and an offset on its scale, and a threshold row may be paid per point from a
 * point on (`PerPointFrom`).
 *
 * The generator's half is `tools/tests/test_passive_rows_carry_the_action_columns.py`.
 */

namespace CataclysmPassiveRowPortTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/**
	 * Four nodes in two trees: a War tree (`Bulwark`) with a root, an eight-point node and a capstone, and a Demonic
	 * node (`Ravager`), so a character carrying one damage type reaches exactly one of them. Names no real tree uses.
	 */
	UDataTable* MakeRowPortNodeTable(FAutomationTestBase& Test)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmPassiveNodeRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromCSVString(TEXT(
			"Name,Tree,NodeId,Kind,NodeName,Description,MaxPoints,Threshold,PositionX,PositionY,Option1Name,Option1Description,Option2Name,Option2Description,Option3Name,Option3Description\r\n"
			"Bulwark_portroot,Bulwark,portroot,basic,Port Root,Unlocks.,1,0,0,0,,,,,,\r\n"
			"Bulwark_portwall,Bulwark,portwall,basic,Port Wall,Per point.,8,0,0,100,,,,,,\r\n"
			"Bulwark_portcap,Bulwark,portcap,capstone,Port Oath,Choose one.,1,4,100,0,Iron,On block 50.,Stone,On block 20.,Ash,Nothing.\r\n"
			"Ravager_portside,Ravager,portside,basic,Port Side,Per point.,5,0,0,0,,,,,,\r\n"));
		for (const FString& Problem : Problems)
		{
			Test.AddError(Problem);
		}
		return Problems.Num() == 0 ? Table : nullptr;
	}

	/**
	 * The rows the tests below read, one per case.
	 *
	 * THE FOURTEEN COLUMNS ARE NAMED ONLY WHERE A ROW USES THEM, which `DataTableImportOptional` on the port's fields
	 * allows; the fourteen before them are all here, as the importer requires.
	 */
	UDataTable* MakeRowPortEffectTable(FAutomationTestBase& Test)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmPassiveEffectRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromCSVString(TEXT(
			"Name,Node,Stat,ValueKind,ValuePerPoint,RequiredTags,Condition,ConditionValue,Scale,ScaleStep,Option,ReachMetres,MinPoints,ScaleMaxSteps,Action,ActionEvent,FractionOf,StackSeconds,ScaleOffset,Condition2,ConditionValue2,TriggerCooldown,PerPointFrom\r\n"
			// An action: 2 Fervour per point on a block.
			"Bulwark_portwall#1,Bulwark_portwall,,,2,,,0,,0,0,-1,0,0,class_resource,block,maximum,0,0,,0,0.25,0\r\n"
			// An own stack: 1% armour a stack, a stack on each hit taken for 5 s, up to 10, from 6 points and once.
			"Bulwark_portwall#2,Bulwark_portwall,armor,increased,1,,,0,own_stacks,1,0,-1,6,10,,hit_taken,,5,0,,0,0,0\r\n"
			// A plain stat row, the control: still made a modifier here.
			"Bulwark_portwall#3,Bulwark_portwall,armor,increased,3,,,0,,0,0,-1,0,0,,,,0,0,,0,0,0\r\n"
			// Two conditions.
			"Bulwark_portwall#4,Bulwark_portwall,attack_damage,increased,2,,wielding_two_handed_weapon,0,,0,0,-1,0,0,,,,0,0,target_is_staggered,0,0,0\r\n"
			// An offset on a scale: per 10 class points spent above 100.
			"Bulwark_portwall#5,Bulwark_portwall,max_health,increased,1,,,0,class_points_spent,10,0,-1,0,0,,,,0,100,,0,0,0\r\n"
			// Paid per point from 6 points, on every point.
			"Bulwark_portwall#6,Bulwark_portwall,armor,flat,1,,,0,,0,0,-1,6,0,,,,0,0,,0,0,1\r\n"
			// Two options of one oath, each an action.
			"Bulwark_portcap#1,Bulwark_portcap,,,50,,,0,,0,1,-1,0,0,health,block,maximum,0,0,,0,0.25,0\r\n"
			"Bulwark_portcap#2,Bulwark_portcap,,,20,,,0,,0,2,-1,0,0,health,block,maximum,0,0,,0,0.25,0\r\n"
			// An action in the Demonic tree.
			"Ravager_portside#1,Ravager_portside,,,5,,,0,,0,0,-1,0,0,mana,dodge,maximum,0,0,,0,0.25,0\r\n"));
		for (const FString& Problem : Problems)
		{
			Test.AddError(Problem);
		}
		return Problems.Num() == 0 ? Table : nullptr;
	}

	/** Eight points in the wall, the oath's point with its first option, and three points in the Demonic node. */
	FCataclysmPassiveAllocation FullAllocation()
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(TEXT("Bulwark_portroot")), 1);
		Allocation.Add(FName(TEXT("Bulwark_portwall")), 8);
		Allocation.Add(FName(TEXT("Bulwark_portcap")), 1);
		Allocation.SetChosenOption(FName(TEXT("Bulwark_portcap")), 1);
		Allocation.Add(FName(TEXT("Ravager_portside")), 3);
		return Allocation;
	}

	/** The copies carrying this name and this action, in the order they came back. */
	TArray<const FCataclysmEnchantmentEffectRow*> CopiesNamed(const TArray<FCataclysmEnchantmentEffectRow>& Rows,
															  const TCHAR* Name, const TCHAR* Action)
	{
		TArray<const FCataclysmEnchantmentEffectRow*> Found;
		for (const FCataclysmEnchantmentEffectRow& Row : Rows)
		{
			if (Row.Enchantment == Name && Row.Action == Action)
			{
				Found.Add(&Row);
			}
		}
		return Found;
	}

	/** A possessed player, its state and its parts. */
	struct FRowPortPlayer
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;

		bool IsComplete() const
		{
			return Character && State && Equipment && AbilitySystem;
		}
	};

	/** A level 10 Demonic Greataxe player, wearing nothing, refreshed once. */
	FRowPortPlayer SpawnRowPortPlayer(UWorld* World)
	{
		FRowPortPlayer Made;
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		Made.Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState || !Controller || !Made.Character)
		{
			return Made;
		}
		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Made.Character);

		Made.State = Made.Character->GetPlayerState<ACataclysmPlayerState>();
		Made.Equipment = Made.Character->GetEquipment();
		Made.AbilitySystem = Made.State ? Made.State->GetCataclysmAbilitySystemComponent() : nullptr;
		if (Made.IsComplete())
		{
			Made.State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("Demonic")));
			Made.State->SetLevelAndExperience(10, 0);
			Made.Equipment->RefreshAttributes(Made.AbilitySystem);
		}
		return Made;
	}

	/** Sets the passive effect table `LoadEffectTable` hands back, and puts the generated one back at scope exit. */
	struct FScopedRowPortEffectTable
	{
		explicit FScopedRowPortEffectTable(const UDataTable* Table)
		{
			UCataclysmPassiveTree::SetEffectTableForTests(Table);
		}
		~FScopedRowPortEffectTable()
		{
			UCataclysmPassiveTree::SetEffectTableForTests(nullptr);
		}
	};
}

// ---------------------------------------------------------------------------
// How many times a row's value is paid
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveRowPortPointsPaidTest,
	"Cataclysm.PassiveRowPort.APointsRowIsPaidByTheThreeRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmPassiveTree::PointsPaid`, with no table: no threshold pays every point; a threshold pays nothing below
 * it and once from it (issue #1755); a threshold with `PerPointFrom` pays every point from that one on. Tempered
 * Steel's "At 6 points: These bonuses are doubled" is the third with 1, and Tinkerer's "per point invested beyond
 * 6" with 7 and a threshold of 7.
 */
bool FCataclysmPassiveRowPortPointsPaidTest::RunTest(const FString&)
{
	FCataclysmPassiveEffectRow Plain;
	TestEqual(TEXT("no threshold: five points pay five times"), UCataclysmPassiveTree::PointsPaid(Plain, 5), 5);
	TestEqual(TEXT("no threshold: no points pay nothing"), UCataclysmPassiveTree::PointsPaid(Plain, 0), 0);

	FCataclysmPassiveEffectRow Once;
	Once.MinPoints = 6;
	TestEqual(TEXT("a threshold of 6: five points pay nothing"), UCataclysmPassiveTree::PointsPaid(Once, 5), 0);
	TestEqual(TEXT("a threshold of 6: six points pay once"), UCataclysmPassiveTree::PointsPaid(Once, 6), 1);
	TestEqual(TEXT("a threshold of 6: eight points still pay once"), UCataclysmPassiveTree::PointsPaid(Once, 8), 1);

	FCataclysmPassiveEffectRow EveryPoint;
	EveryPoint.MinPoints = 6;
	EveryPoint.PerPointFrom = 1;
	TestEqual(TEXT("from 6, every point: five points pay nothing"),
			  UCataclysmPassiveTree::PointsPaid(EveryPoint, 5), 0);
	TestEqual(TEXT("from 6, every point: six points pay six times"),
			  UCataclysmPassiveTree::PointsPaid(EveryPoint, 6), 6);
	TestEqual(TEXT("from 6, every point: eight points pay eight times"),
			  UCataclysmPassiveTree::PointsPaid(EveryPoint, 8), 8);

	FCataclysmPassiveEffectRow Beyond;
	Beyond.MinPoints = 7;
	Beyond.PerPointFrom = 7;
	TestEqual(TEXT("beyond 6: six points pay nothing"), UCataclysmPassiveTree::PointsPaid(Beyond, 6), 0);
	TestEqual(TEXT("beyond 6: seven points pay once"), UCataclysmPassiveTree::PointsPaid(Beyond, 7), 1);
	TestEqual(TEXT("beyond 6: eight points pay twice"), UCataclysmPassiveTree::PointsPaid(Beyond, 8), 2);
	return true;
}

// ---------------------------------------------------------------------------
// Which rows are copied, and which are made modifiers
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveRowPortCopiedNotModifiedTest,
	"Cataclysm.PassiveRowPort.AnActionRowIsCopiedForTheBuilderAndNotMadeAModifier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * With every node held and the War tree reached: the action row, the own-stack row and the taken option's action are
 * copied, at their points, under the node's name; the option not taken and the Demonic node are not. `AccumulateInto`
 * makes a modifier of each of the four stat rows and of none of the copied ones.
 */
bool FCataclysmPassiveRowPortCopiedNotModifiedTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveRowPortTest;

	const UDataTable* Nodes = MakeRowPortNodeTable(*this);
	const UDataTable* Effects = MakeRowPortEffectTable(*this);
	if (!Nodes || !Effects)
	{
		return false;
	}
	const TArray<FName> War = {FName(TEXT("War"))};
	const FCataclysmPassiveAllocation Allocation = FullAllocation();

	const TArray<FCataclysmEnchantmentEffectRow> Rows =
		UCataclysmPassiveTree::EnchantmentRowsFor(Allocation, Nodes, Effects, War);
	TestEqual(TEXT("three rows are copied: the action, the own stack and the taken option"), Rows.Num(), 3);

	const TArray<const FCataclysmEnchantmentEffectRow*> Fervour =
		CopiesNamed(Rows, TEXT("Passive_Bulwark_portwall"), TEXT("class_resource"));
	if (TestEqual(TEXT("the wall's action is copied once, under the node's name"), Fervour.Num(), 1))
	{
		TestEqual(TEXT("at its eight points: 16"), Fervour[0]->ValueLow, 16.0f, 0.001f);
		TestEqual(TEXT("at both ends of the range"), Fervour[0]->ValueHigh, 16.0f, 0.001f);
		TestEqual(TEXT("on its event"), Fervour[0]->ActionEvent, FString(TEXT("block")));
		TestEqual(TEXT("with its trigger cooldown"), Fervour[0]->TriggerCooldown, 0.25f, 0.001f);
	}

	const TArray<const FCataclysmEnchantmentEffectRow*> Stacks =
		CopiesNamed(Rows, TEXT("Passive_Bulwark_portwall"), TEXT(""));
	if (TestEqual(TEXT("the own-stack row is copied once"), Stacks.Num(), 1))
	{
		TestEqual(TEXT("its stat"), Stacks[0]->Stat, FString(TEXT("armor")));
		TestEqual(TEXT("once, at its threshold: 1 a stack"), Stacks[0]->ValueLow, 1.0f, 0.001f);
		TestEqual(TEXT("its scale"), Stacks[0]->Scale, FString(TEXT("own_stacks")));
		TestEqual(TEXT("its cap"), Stacks[0]->ScaleMaxSteps, 10);
		TestEqual(TEXT("its stacks' seconds"), Stacks[0]->StackSeconds, 5.0f, 0.001f);
		TestEqual(TEXT("its event"), Stacks[0]->ActionEvent, FString(TEXT("hit_taken")));
	}

	TestEqual(TEXT("the taken option's action is copied, under the node and the option"),
			  CopiesNamed(Rows, TEXT("Passive_Bulwark_portcap_Option1"), TEXT("health")).Num(), 1);
	TestEqual(TEXT("the option not taken is not"),
			  CopiesNamed(Rows, TEXT("Passive_Bulwark_portcap_Option2"), TEXT("health")).Num(), 0);
	TestEqual(TEXT("the Demonic node, in a tree no damage type reaches, is not"),
			  CopiesNamed(Rows, TEXT("Passive_Ravager_portside"), TEXT("mana")).Num(), 0);

	// THE OTHER HALF: what `AccumulateInto` makes from the same allocation.
	TMap<FName, TArray<FCataclysmStatModifier>> Totals;
	const int32 Added = UCataclysmPassiveTree::AccumulateInto(Totals, Allocation, Nodes, Effects, War);
	TestEqual(TEXT("four modifiers: the four stat rows and no copied row"), Added, 4);

	const TArray<FCataclysmStatModifier>* Armour = Totals.Find(FName(TEXT("armor")));
	if (TestNotNull(TEXT("armour has modifiers"), Armour))
	{
		TestEqual(TEXT("two on armour: the plain row and the per-point-from-6 row"), Armour->Num(), 2);
		for (const FCataclysmStatModifier& Modifier : *Armour)
		{
			TestNotEqual(TEXT("and neither counts own stacks"), Modifier.Scale, ECataclysmStatScale::PerOwnStack);
		}
	}
	TestFalse(TEXT("no pool is a stat: nothing on class_resource"), Totals.Contains(FName(TEXT("class_resource"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveRowPortChoicesTest,
	"Cataclysm.PassiveRowPort.ACopyFollowsTheTreeTheOptionAndTheThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The same choices `AccumulateInto` makes, made for a copy: below its threshold the own-stack row is not copied, and
 * at it it is; with no option chosen neither option's action is; with the Demonic type carried the Demonic node's
 * action is, at its three points, and no War row is.
 */
bool FCataclysmPassiveRowPortChoicesTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveRowPortTest;

	const UDataTable* Nodes = MakeRowPortNodeTable(*this);
	const UDataTable* Effects = MakeRowPortEffectTable(*this);
	if (!Nodes || !Effects)
	{
		return false;
	}
	const TArray<FName> War = {FName(TEXT("War"))};

	FCataclysmPassiveAllocation Five;
	Five.Add(FName(TEXT("Bulwark_portwall")), 5);
	Five.Add(FName(TEXT("Bulwark_portcap")), 1);
	const TArray<FCataclysmEnchantmentEffectRow> AtFive =
		UCataclysmPassiveTree::EnchantmentRowsFor(Five, Nodes, Effects, War);
	TestEqual(TEXT("at five points the own stack, from 6, is not copied"),
			  CopiesNamed(AtFive, TEXT("Passive_Bulwark_portwall"), TEXT("")).Num(), 0);
	TestEqual(TEXT("and the action, paid per point, is"),
			  CopiesNamed(AtFive, TEXT("Passive_Bulwark_portwall"), TEXT("class_resource")).Num(), 1);
	TestEqual(TEXT("with no option chosen, the first option's action is not copied"),
			  CopiesNamed(AtFive, TEXT("Passive_Bulwark_portcap_Option1"), TEXT("health")).Num(), 0);
	TestEqual(TEXT("nor the second's"),
			  CopiesNamed(AtFive, TEXT("Passive_Bulwark_portcap_Option2"), TEXT("health")).Num(), 0);

	FCataclysmPassiveAllocation Six = Five;
	Six.Add(FName(TEXT("Bulwark_portwall")), 1);
	TestEqual(TEXT("at six points the own stack is copied"),
			  CopiesNamed(UCataclysmPassiveTree::EnchantmentRowsFor(Six, Nodes, Effects, War),
						  TEXT("Passive_Bulwark_portwall"), TEXT("")).Num(), 1);

	const TArray<FCataclysmEnchantmentEffectRow> Demonic = UCataclysmPassiveTree::EnchantmentRowsFor(
		FullAllocation(), Nodes, Effects, {FName(TEXT("Demonic"))});
	const TArray<const FCataclysmEnchantmentEffectRow*> Mana =
		CopiesNamed(Demonic, TEXT("Passive_Ravager_portside"), TEXT("mana"));
	if (TestEqual(TEXT("carrying Demonic, the Demonic node's action is copied"), Mana.Num(), 1))
	{
		TestEqual(TEXT("at its three points: 15"), Mana[0]->ValueLow, 15.0f, 0.001f);
	}
	TestEqual(TEXT("and it is the only copy: no War row"), Demonic.Num(), 1);
	return true;
}

// ---------------------------------------------------------------------------
// A stat row's second condition and offset
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveRowPortStatColumnsTest,
	"Cataclysm.PassiveRowPort.AStatRowCarriesASecondConditionAndAnOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `AccumulateInto` copies the second condition, by the pipeline's own reading of the name, and the scale's offset,
 * onto the modifier; and pays the per-point-from row for every point. The plain armour row is the control: no second
 * condition and no offset.
 */
bool FCataclysmPassiveRowPortStatColumnsTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveRowPortTest;

	const UDataTable* Nodes = MakeRowPortNodeTable(*this);
	const UDataTable* Effects = MakeRowPortEffectTable(*this);
	if (!Nodes || !Effects)
	{
		return false;
	}

	ECataclysmStatCondition TwoHanded = ECataclysmStatCondition::Always;
	ECataclysmStatCondition Staggered = ECataclysmStatCondition::Always;
	ECataclysmStatScale ClassPoints = ECataclysmStatScale::Fixed;
	if (!TestTrue(TEXT("set-up: the pipeline knows wielding_two_handed_weapon"),
				  UCataclysmStatPipeline::ConditionNamed(TEXT("wielding_two_handed_weapon"), TwoHanded))
		|| !TestTrue(TEXT("set-up: and target_is_staggered"),
					 UCataclysmStatPipeline::ConditionNamed(TEXT("target_is_staggered"), Staggered))
		|| !TestTrue(TEXT("set-up: and the scale class_points_spent"),
					 UCataclysmStatPipeline::ScaleNamed(TEXT("class_points_spent"), ClassPoints)))
	{
		return false;
	}

	TMap<FName, TArray<FCataclysmStatModifier>> Totals;
	UCataclysmPassiveTree::AccumulateInto(Totals, FullAllocation(), Nodes, Effects, {FName(TEXT("War"))});

	const TArray<FCataclysmStatModifier>* Damage = Totals.Find(FName(TEXT("attack_damage")));
	if (TestTrue(TEXT("one modifier on attack damage"), Damage && Damage->Num() == 1))
	{
		const FCataclysmStatModifier& Modifier = (*Damage)[0];
		TestEqual(TEXT("its first condition"), Modifier.Condition, TwoHanded);
		TestEqual(TEXT("its second condition"), Modifier.Condition2, Staggered);
		TestEqual(TEXT("paid for eight points: 16"), Modifier.Value, 16.0f, 0.001f);
	}

	const TArray<FCataclysmStatModifier>* Health = Totals.Find(FName(TEXT("max_health")));
	if (TestTrue(TEXT("one modifier on maximum health"), Health && Health->Num() == 1))
	{
		const FCataclysmStatModifier& Modifier = (*Health)[0];
		TestEqual(TEXT("its scale"), Modifier.Scale, ClassPoints);
		TestEqual(TEXT("its step"), Modifier.ScaleStep, 10.0f, 0.001f);
		TestEqual(TEXT("its offset"), Modifier.ScaleOffset, 100.0f, 0.001f);
	}

	const TArray<FCataclysmStatModifier>* Armour = Totals.Find(FName(TEXT("armor")));
	if (TestNotNull(TEXT("armour has modifiers"), Armour))
	{
		bool bSawPlain = false;
		bool bSawPerPointFrom = false;
		for (const FCataclysmStatModifier& Modifier : *Armour)
		{
			if (Modifier.Bucket == ECataclysmStatBucket::Increased)
			{
				bSawPlain = true;
				TestEqual(TEXT("the control: the plain row has no second condition"), Modifier.Condition2,
						  ECataclysmStatCondition::Always);
				TestEqual(TEXT("and no offset"), Modifier.ScaleOffset, 0.0f, 0.001f);
				TestEqual(TEXT("and is paid per point: 24"), Modifier.Value, 24.0f, 0.001f);
			}
			else if (Modifier.Bucket == ECataclysmStatBucket::Flat)
			{
				bSawPerPointFrom = true;
				TestEqual(TEXT("the per-point-from-6 row is paid for all eight points: 8"), Modifier.Value, 8.0f,
						  0.001f);
			}
		}
		TestTrue(TEXT("the plain row was found"), bSawPlain);
		TestTrue(TEXT("the per-point-from row was found"), bSawPerPointFrom);
	}
	return true;
}

// ---------------------------------------------------------------------------
// The builder makes the copies into actions and a stack
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveRowPortBuilderTest,
	"Cataclysm.PassiveRowPort.ACopiedRowIsBuiltIntoAnActionAndAStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The copies, handed to the enchantment rows' builder as the refresh hands them: the wall's action moves Fervour on a
 * block, keyed by the node; the oath's option restores health on a block, keyed by the node and the option; and the
 * own-stack row becomes a stack gained on a hit taken and a modifier on armour that reads it, both under one key.
 */
bool FCataclysmPassiveRowPortBuilderTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveRowPortTest;

	const UDataTable* Nodes = MakeRowPortNodeTable(*this);
	const UDataTable* Effects = MakeRowPortEffectTable(*this);
	const UDataTable* EnchantmentEffects = UCataclysmItemModifiers::LoadEnchantmentEffectTable();
	const UDataTable* Positive = UCataclysmDropRoll::LoadPositiveEnchantmentTable();
	const UDataTable* Negative = UCataclysmDropRoll::LoadNegativeEnchantmentTable();
	if (!Nodes || !Effects
		|| !TestTrue(TEXT("set-up: the three enchantment tables load"), EnchantmentEffects && Positive && Negative))
	{
		return false;
	}

	const TArray<FCataclysmEnchantmentEffectRow> Rows =
		UCataclysmPassiveTree::EnchantmentRowsFor(FullAllocation(), Nodes, Effects, {FName(TEXT("War"))});
	TMap<FName, TArray<FCataclysmStatModifier>> Totals;
	TArray<FCataclysmPoolAction> Actions;
	UCataclysmItemModifiers::AccumulateEnchantmentsInto(Totals, TArray<FCataclysmItem>(), EnchantmentEffects, Positive,
														Negative, &Actions, &Rows);
	TestEqual(TEXT("three actions: the Fervour, the health and the stack"), Actions.Num(), 3);

	const FName StackKey(TEXT("Passive_Bulwark_portwall:armor"));
	bool bSawFervour = false;
	bool bSawHealth = false;
	bool bSawStack = false;
	for (const FCataclysmPoolAction& Action : Actions)
	{
		if (Action.Pool == FName(TEXT("class_resource")))
		{
			bSawFervour = true;
			TestEqual(TEXT("the Fervour moves on a block"), Action.Event, FName(TEXT("block")));
			TestEqual(TEXT("by 16"), Action.Percent, 16.0f, 0.001f);
			TestEqual(TEXT("keyed by the node, the action and the event"), Action.TriggerKey,
					  FName(TEXT("Passive_Bulwark_portwall:class_resource:block")));
			TestEqual(TEXT("with its quarter second"), Action.TriggerCooldownSeconds, 0.25f, 0.001f);
		}
		else if (Action.Pool == FName(TEXT("health")))
		{
			bSawHealth = true;
			TestEqual(TEXT("the oath's health moves on a block"), Action.Event, FName(TEXT("block")));
			TestEqual(TEXT("by 50"), Action.Percent, 50.0f, 0.001f);
			TestEqual(TEXT("keyed by the node, the option, the action and the event"), Action.TriggerKey,
					  FName(TEXT("Passive_Bulwark_portcap_Option1:health:block")));
		}
		else if (Action.StackKey == StackKey)
		{
			bSawStack = true;
			TestEqual(TEXT("the stack is gained on a hit taken"), Action.Event, FName(TEXT("hit_taken")));
			TestEqual(TEXT("lasts 5 seconds"), Action.StackSeconds, 5.0f, 0.001f);
			TestEqual(TEXT("up to 10"), Action.StackCap, 10);
		}
	}
	TestTrue(TEXT("the Fervour action was built"), bSawFervour);
	TestTrue(TEXT("the health action was built"), bSawHealth);
	TestTrue(TEXT("the stack action was built"), bSawStack);

	const TArray<FCataclysmStatModifier>* Armour = Totals.Find(FName(TEXT("armor")));
	if (TestTrue(TEXT("one modifier on armour, the own stack's"), Armour && Armour->Num() == 1))
	{
		const FCataclysmStatModifier& Modifier = (*Armour)[0];
		TestEqual(TEXT("it counts its own stacks"), Modifier.Scale, ECataclysmStatScale::PerOwnStack);
		TestEqual(TEXT("under the stack action's key"), Modifier.StackKey, StackKey);
		TestEqual(TEXT("1 a stack"), Modifier.Value, 1.0f, 0.001f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Through the real refresh
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveRowPortRefreshTest,
	"Cataclysm.PassiveRowPort.ANodesActionReachesTheCharacterThroughTheRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A real level 10 Demonic character, the real node table, and an effect table of this test's own giving Pain
 * Tolerance (`Masochist_basic_spine_001`) an action, an own stack and a plain row. With four points in it the
 * equipment's refresh hands the character the action and the stack, and puts the own-stack modifier and the plain
 * row on the stat line. THE CONTROL: with no point in it, the refresh hands over none of them and the actions are
 * what they were before any point was spent.
 */
bool FCataclysmPassiveRowPortRefreshTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveRowPortTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	TStrongObjectPtr<UDataTable> Effects(NewObject<UDataTable>());
	Effects->RowStruct = FCataclysmPassiveEffectRow::StaticStruct();
	const TArray<FString> Problems = Effects->CreateTableFromCSVString(TEXT(
		"Name,Node,Stat,ValueKind,ValuePerPoint,RequiredTags,Condition,ConditionValue,Scale,ScaleStep,Option,ReachMetres,MinPoints,ScaleMaxSteps,Action,ActionEvent,FractionOf,StackSeconds,TriggerCooldown\r\n"
		"Masochist_basic_spine_001#1,Masochist_basic_spine_001,,,2,,,0,,0,0,-1,0,0,class_resource,block,maximum,0,0.25\r\n"
		"Masochist_basic_spine_001#2,Masochist_basic_spine_001,armor,increased,1,,,0,own_stacks,1,0,-1,0,10,,hit_taken,,5,0\r\n"
		"Masochist_basic_spine_001#3,Masochist_basic_spine_001,max_health,increased,1,,,0,,0,0,-1,0,0,,,,0,0\r\n"));
	for (const FString& Problem : Problems)
	{
		AddError(Problem);
	}
	if (Problems.Num() > 0)
	{
		return false;
	}
	const FScopedRowPortEffectTable UseThisTable(Effects.Get());

	FRowPortPlayer Player = SpawnRowPortPlayer(World);
	if (!TestTrue(TEXT("set-up: a possessed player with its parts"), Player.IsComplete())
		|| !TestTrue(TEXT("set-up: it reaches the Masochist tree"),
					 Player.State->ReachableTrees().Contains(TEXT("Masochist"))))
	{
		return false;
	}

	const FName Root(TEXT("Masochist_basic_spine_000"));
	const FName PainTolerance(TEXT("Masochist_basic_spine_001"));
	const FName StackKey(TEXT("Passive_Masochist_basic_spine_001:armor"));
	const FName FervourKey(TEXT("Passive_Masochist_basic_spine_001:class_resource:block"));

	const auto Find = [&Player](const TFunctionRef<bool(const FCataclysmPoolAction&)>& Matches)
	{
		int32 Count = 0;
		for (const FCataclysmPoolAction& Action : Player.AbilitySystem->GetPoolActions())
		{
			Count += Matches(Action) ? 1 : 0;
		}
		return Count;
	};
	const auto IsTheFervour = [&FervourKey](const FCataclysmPoolAction& Action)
	{
		return Action.TriggerKey == FervourKey;
	};
	const auto IsTheStack = [&StackKey](const FCataclysmPoolAction& Action)
	{
		return Action.StackKey == StackKey && Action.Pool.IsNone();
	};

	// THE CONTROL FIRST: the root alone.
	FCataclysmPassiveAllocation RootOnly;
	RootOnly.Add(Root, 1);
	Player.State->SetPassiveAllocation(RootOnly, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	const int32 ActionsBefore = Player.AbilitySystem->GetPoolActions().Num();
	TestEqual(TEXT("control: with no point in Pain Tolerance, no Fervour action is handed over"),
			  Find(IsTheFervour), 0);
	TestEqual(TEXT("control: nor its stack"), Find(IsTheStack), 0);

	FCataclysmPassiveAllocation Four = RootOnly;
	Four.Add(PainTolerance, 4);
	Player.State->SetPassiveAllocation(Four, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);

	TestEqual(TEXT("with four points, two more actions are handed over"),
			  Player.AbilitySystem->GetPoolActions().Num(), ActionsBefore + 2);
	TestEqual(TEXT("the Fervour action is among them, once"), Find(IsTheFervour), 1);
	TestEqual(TEXT("and the stack, once"), Find(IsTheStack), 1);
	for (const FCataclysmPoolAction& Action : Player.AbilitySystem->GetPoolActions())
	{
		if (Action.TriggerKey == FervourKey)
		{
			TestEqual(TEXT("the Fervour action moves 8 at four points"), Action.Percent, 8.0f, 0.001f);
			TestEqual(TEXT("on a block"), Action.Event, FName(TEXT("block")));
		}
	}

	const FCataclysmStatInputs* Armour = Player.AbilitySystem->GetStatInputs(FName(TEXT("armor")));
	int32 OwnStackModifiers = 0;
	int32 KeyedOwnStackModifiers = 0;
	if (TestNotNull(TEXT("armour is on the stat line"), Armour))
	{
		for (const FCataclysmStatModifier& Modifier : Armour->Modifiers)
		{
			if (Modifier.Scale != ECataclysmStatScale::PerOwnStack)
			{
				continue;
			}
			++OwnStackModifiers;
			if (Modifier.StackKey == StackKey)
			{
				++KeyedOwnStackModifiers;
				TestEqual(TEXT("the own-stack modifier is 4 a stack at four points"), Modifier.Value, 4.0f, 0.001f);
			}
		}
	}
	// ONE IN ALL, AS WELL AS ONE UNDER THE KEY: a row made a modifier by the tree as well as by the builder would add a
	// second, with no key, which reads no stacks.
	TestEqual(TEXT("one own-stack modifier on armour in all"), OwnStackModifiers, 1);
	TestEqual(TEXT("and it is under the stack's key"), KeyedOwnStackModifiers, 1);

	float FromTheTree = 0.0f;
	if (const FCataclysmStatInputs* Health = Player.AbilitySystem->GetStatInputs(FName(TEXT("max_health"))))
	{
		for (const FCataclysmStatModifier& Modifier : Health->Modifiers)
		{
			if (Modifier.Source == ECataclysmModifierSource::PassiveKeystone)
			{
				FromTheTree += Modifier.Value;
			}
		}
	}
	TestEqual(TEXT("the plain row is still made a modifier by the tree: 4"), FromTheTree, 4.0f, 0.001f);

	// AND BACK: the point taken out takes the actions with it.
	Player.State->SetPassiveAllocation(RootOnly, TArray<FName>());
	Player.Equipment->RefreshAttributes(Player.AbilitySystem);
	TestEqual(TEXT("with the points taken out, the Fervour action is gone"), Find(IsTheFervour), 0);
	TestEqual(TEXT("and the actions are what they were"), Player.AbilitySystem->GetPoolActions().Num(),
			  ActionsBefore);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
