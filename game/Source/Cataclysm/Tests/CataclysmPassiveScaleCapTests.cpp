// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

/**
 * A passive row's cap on its scale, the `ScaleMaxSteps` column of `game/Data/PassiveEffects.csv`. The demonic tree
 * rework of 2026-11-24: the Ritualist's Wall of Servants reads "You take 1% less damage for each minion you have, up
 * to 20% less", and the owner answered that the per-minion damage reduction is capped at 20%. Until then the column
 * was an enchantment's alone (issue #1815) and a passive modifier carried no cap.
 *
 * MADE-UP TABLES, READ THROUGH THE REAL IMPORT AND THE REAL `UCataclysmPassiveTree::ModifiersFor`, so what is measured
 * is the column travelling from a CSV row to the modifier the pipeline caps by.
 */
namespace CataclysmPassiveScaleCapTest
{
	UDataTable* MakeCapNodeTable(FAutomationTestBase& Test)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmPassiveNodeRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromCSVString(TEXT(
			"Name,Tree,NodeId,Kind,NodeName,Description,MaxPoints,Threshold,PositionX,PositionY,Option1Name,"
			"Option1Description,Option2Name,Option2Description,Option3Name,Option3Description\r\n"
			"Ravager_wall,Ravager,wall,basic,Wall,Up to 20% less.,1,0,0,0,,,,,,\r\n"));
		for (const FString& Problem : Problems)
		{
			Test.AddError(Problem);
		}
		return Problems.Num() == 0 ? Table : nullptr;
	}

	/** Wall of Servants' row, capped at 20, and a second row on the node scaled the same way and capped at nothing. */
	UDataTable* MakeCapEffectTable(FAutomationTestBase& Test)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmPassiveEffectRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromCSVString(TEXT(
			"Name,Node,Stat,ValueKind,ValuePerPoint,RequiredTags,Condition,ConditionValue,Scale,ScaleStep,Option,"
			"ReachMetres,MinPoints,ScaleMaxSteps\r\n"
			"Ravager_wall#1,Ravager_wall,damage_taken,more,-1.0,,,0,minions_held,1,0,-1,0,20\r\n"
			"Ravager_wall#2,Ravager_wall,spell_damage,increased,1.0,,,0,minions_held,1,0,-1,0,0\r\n"));
		for (const FString& Problem : Problems)
		{
			Test.AddError(Problem);
		}
		return Problems.Num() == 0 ? Table : nullptr;
	}

	/** The one modifier a stat was given, or null. */
	const FCataclysmStatModifier* OnlyModifier(const TMap<FName, TArray<FCataclysmStatModifier>>& Granted,
											   const TCHAR* Stat)
	{
		const TArray<FCataclysmStatModifier>* Found = Granted.Find(FName(Stat));
		return Found && Found->Num() == 1 ? &(*Found)[0] : nullptr;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a
// `using namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPassiveScaleCapTravelsTest,
	"Cataclysm.PassiveScaleCap.ARowsCapReachesItsModifierAndStopsTheScaleAtTwentyMinions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE CONTROL IS THE SECOND ROW: the same scale with no cap carries 0 and keeps counting. The capped row's modifier
 * carries 20, and the pipeline's own `ScaledValue` stops it there: 10 minions give 10% less, 20 give 20% less, and 25
 * still give 20% less, where the uncapped row goes on to 25.
 */
bool FCataclysmPassiveScaleCapTravelsTest::RunTest(const FString&)
{
	using namespace CataclysmPassiveScaleCapTest;

	UDataTable* NodeTable = MakeCapNodeTable(*this);
	UDataTable* EffectTable = MakeCapEffectTable(*this);
	if (!NodeTable || !EffectTable)
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Ravager_wall")), 1);
	const TMap<FName, TArray<FCataclysmStatModifier>> Granted = UCataclysmPassiveTree::ModifiersFor(
		Allocation, NodeTable, EffectTable, {FName(TEXT("Demonic"))});

	const FCataclysmStatModifier* Capped = OnlyModifier(Granted, TEXT("damage_taken"));
	const FCataclysmStatModifier* Uncapped = OnlyModifier(Granted, TEXT("spell_damage"));
	if (!TestNotNull(TEXT("set-up: the capped row became one modifier"), Capped)
		|| !TestNotNull(TEXT("set-up: and the uncapped row another"), Uncapped))
	{
		return false;
	}
	TestEqual(TEXT("the capped row's cap reached its modifier"), Capped->ScaleMaxSteps, 20);
	TestEqual(TEXT("control: the uncapped row's modifier carries none"), Uncapped->ScaleMaxSteps, 0);
	TestTrue(TEXT("and the capped modifier is still the scale the row named"),
			 Capped->Scale == ECataclysmStatScale::PerMinionHeld);
	TestTrue(TEXT("the pipeline accepts the capped modifier"),
			 UCataclysmStatPipeline::ValidateModifier(*Capped).IsEmpty());

	FCataclysmStatConditions Ten;
	Ten.MinionsHeld = 10;
	FCataclysmStatConditions Twenty;
	Twenty.MinionsHeld = 20;
	FCataclysmStatConditions TwentyFive;
	TwentyFive.MinionsHeld = 25;
	TestEqual(TEXT("ten minions give 10% less"), UCataclysmStatPipeline::ScaledValue(*Capped, Ten), -10.0f, 0.001f);
	TestEqual(TEXT("twenty give 20% less"), UCataclysmStatPipeline::ScaledValue(*Capped, Twenty), -20.0f, 0.001f);
	TestEqual(TEXT("and twenty-five still 20% less: the cap holds"),
			  UCataclysmStatPipeline::ScaledValue(*Capped, TwentyFive), -20.0f, 0.001f);
	TestEqual(TEXT("control: the uncapped row goes on to 25"),
			  UCataclysmStatPipeline::ScaledValue(*Uncapped, TwentyFive), 25.0f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
