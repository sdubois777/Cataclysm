// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"

/**
 * Every capstone option of the three Demonic trees grants its own rows and no
 * other option's. Issue #1755.
 *
 * WHAT IT GUARDS: the routing, the one line in `UCataclysmPassiveTree::AccumulateInto`
 * that compares a row's `Option` with the option chosen. Every capstone row is
 * read through `ModifiersFor`, which calls `AccumulateInto`, so a row with a
 * condition, a scale or a required tag is carried and checked, not evaluated,
 * and no row has to be skipped.
 *
 * WHAT IT CANNOT GUARD: a row whose `Option` names the wrong option in the data.
 * This test takes each option's rows from that same column, so a misfiled row is
 * expected where it is filed and found there. The words of each option catch
 * that, in `tools/tests/test_passive_effects_match_the_node_text.py`.
 *
 * FOR EACH CAPSTONE NODE, the modifiers with the node taken and no option chosen
 * are the base. Choosing option N must add, stat by stat, exactly option N's rows
 * to that base: each in the bucket its `ValueKind` names, at its figure, with its
 * condition, scale, required tags and reach. A modifier added that is not one of
 * option N's rows fails, which is what "the other options grant nothing" means.
 */
namespace CataclysmCapstoneOptionTest
{
	/** Per tree, the options and rows counted on development cd6e3f81. */
	constexpr int32 FewestOptions = 12;
	constexpr int32 FewestMasochistRows = 28;
	constexpr int32 FewestRavagerRows = 18;
	constexpr int32 FewestRitualistRows = 18;

	/**
	 * The modifier a row should become at one point spent, or false with the
	 * reason. THE BUCKET IS SPELT OUT HERE rather than read from the game, so a
	 * row routed into the wrong bucket fails rather than agreeing with itself;
	 * the condition and scale names are the pipeline's own vocabulary.
	 */
	bool Expected(const FCataclysmPassiveEffectRow& Row, FCataclysmStatModifier& Out,
				  FString& Why)
	{
		Out = FCataclysmStatModifier();
		Out.Value = Row.ValuePerPoint;
		if (Row.ValueKind.Equals(TEXT("more"), ESearchCase::IgnoreCase))
		{
			Out.Bucket = ECataclysmStatBucket::More;
		}
		else if (Row.ValueKind.Equals(TEXT("increased"), ESearchCase::IgnoreCase))
		{
			Out.Bucket = ECataclysmStatBucket::Increased;
		}
		else if (Row.ValueKind.Equals(TEXT("flat"), ESearchCase::IgnoreCase))
		{
			Out.Bucket = ECataclysmStatBucket::Flat;
		}
		else if (Row.ValueKind.Equals(TEXT("removed"), ESearchCase::IgnoreCase))
		{
			Out.Bucket = ECataclysmStatBucket::Removed;
		}
		else
		{
			Why = FString::Printf(TEXT("value kind '%s' is none of more, increased, flat, removed"),
								  *Row.ValueKind);
			return false;
		}
		TArray<FString> Tags;
		Row.RequiredTags.ParseIntoArray(Tags, TEXT(","), /*InCullEmpty=*/true);
		for (FString& Tag : Tags)
		{
			Tag.TrimStartAndEndInline();
			if (!Tag.IsEmpty())
			{
				Out.RequiredTags.AddTag(
					FGameplayTag::RequestGameplayTag(FName(*Tag), /*ErrorIfNotFound=*/false));
			}
		}
		if (!Row.Condition.IsEmpty())
		{
			if (!UCataclysmStatPipeline::ConditionNamed(Row.Condition, Out.Condition))
			{
				Why = FString::Printf(TEXT("condition '%s' is not in the pipeline's vocabulary"),
									  *Row.Condition);
				return false;
			}
			if (UCataclysmStatPipeline::ConditionTakesAValue(Out.Condition))
			{
				Out.ConditionValue = Row.ConditionValue;
			}
		}
		if (!Row.Scale.IsEmpty())
		{
			if (!UCataclysmStatPipeline::ScaleNamed(Row.Scale, Out.Scale))
			{
				Why = FString::Printf(TEXT("scale '%s' is not in the pipeline's vocabulary"),
									  *Row.Scale);
				return false;
			}
			Out.ScaleStep = Row.ScaleStep;
		}
		Out.ReachMetres = Row.ReachMetres;
		return true;
	}

	/** The fields a row decides. `Source` and the stack fields are not the row's. */
	bool Same(const FCataclysmStatModifier& A, const FCataclysmStatModifier& B)
	{
		return A.Bucket == B.Bucket && A.Value == B.Value && A.Condition == B.Condition
			&& A.ConditionValue == B.ConditionValue && A.Scale == B.Scale
			&& A.ScaleStep == B.ScaleStep && A.RequiredTags == B.RequiredTags
			&& A.ReachMetres == B.ReachMetres;
	}

	/** Remove one modifier equal to `Wanted` from `From`, or false if none is. */
	bool TakeOne(TArray<FCataclysmStatModifier>& From, const FCataclysmStatModifier& Wanted)
	{
		for (int32 Index = 0; Index < From.Num(); ++Index)
		{
			if (Same(From[Index], Wanted))
			{
				From.RemoveAt(Index);
				return true;
			}
		}
		return false;
	}

	struct FOptionRow
	{
		FName Name;
		const FCataclysmPassiveEffectRow* Row = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryDemonicCapstoneOptionTest,
	"Cataclysm.CapstoneOptions.EveryDemonicCapstoneOptionGrantsItsRowsAndNoOthers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEveryDemonicCapstoneOptionTest::RunTest(const FString&)
{
	using namespace CataclysmCapstoneOptionTest;

	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	// EVERY CAPSTONE ROW OF THE THREE TREES, by node and option, in row order.
	TMap<FString, TMap<FString, TMap<int32, TArray<FOptionRow>>>> ByTree;
	for (const TPair<FName, uint8*>& Entry : EffectTable->GetRowMap())
	{
		const FCataclysmPassiveEffectRow* Row =
			reinterpret_cast<const FCataclysmPassiveEffectRow*>(Entry.Value);
		if (!Row || Row->Option == 0)
		{
			continue;
		}
		for (const TCHAR* Tree : {TEXT("Masochist"), TEXT("Ravager"), TEXT("Ritualist")})
		{
			if (Row->Node.StartsWith(FString(Tree) + TEXT("_")))
			{
				ByTree.FindOrAdd(Tree).FindOrAdd(Row->Node).FindOrAdd(Row->Option).Add(
					FOptionRow{Entry.Key, Row});
			}
		}
	}

	const TMap<FString, int32> FewestRows = {
		{TEXT("Masochist"), FewestMasochistRows},
		{TEXT("Ravager"), FewestRavagerRows},
		{TEXT("Ritualist"), FewestRitualistRows}};
	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};

	for (const TPair<FString, int32>& Tree : FewestRows)
	{
		int32 OptionsChecked = 0;
		int32 RowsChecked = 0;
		const TMap<FString, TMap<int32, TArray<FOptionRow>>>* Nodes = ByTree.Find(Tree.Key);
		if (Nodes)
		{
			for (const TPair<FString, TMap<int32, TArray<FOptionRow>>>& Node : *Nodes)
			{
				const FName NodeName(*Node.Key);
				FCataclysmPassiveAllocation Taken;
				Taken.Add(NodeName, 1);
				const TMap<FName, TArray<FCataclysmStatModifier>> Base =
					UCataclysmPassiveTree::ModifiersFor(Taken, NodeTable, EffectTable, Demonic);

				for (const TPair<int32, TArray<FOptionRow>>& Option : Node.Value)
				{
					FCataclysmPassiveAllocation Chosen = Taken;
					Chosen.SetChosenOption(NodeName, Option.Key);
					TMap<FName, TArray<FCataclysmStatModifier>> Added =
						UCataclysmPassiveTree::ModifiersFor(Chosen, NodeTable, EffectTable,
															Demonic);

					// WHAT CHOOSING THE OPTION ADDED: the base taken away, stat by stat.
					for (const TPair<FName, TArray<FCataclysmStatModifier>>& Stat : Base)
					{
						TArray<FCataclysmStatModifier>* Now = Added.Find(Stat.Key);
						for (const FCataclysmStatModifier& Was : Stat.Value)
						{
							if (!Now || !TakeOne(*Now, Was))
							{
								AddError(FString::Printf(
									TEXT("%s option %d: choosing it took away a %s modifier the node "
										 "grants with no option chosen"),
									*Node.Key, Option.Key, *Stat.Key.ToString()));
							}
						}
					}

					// EACH OF OPTION N'S ROWS, IN THE BUCKET ITS KIND NAMES.
					for (const FOptionRow& Wanted : Option.Value)
					{
						++RowsChecked;
						FCataclysmStatModifier Expect;
						FString Why;
						if (!Expected(*Wanted.Row, Expect, Why))
						{
							AddError(FString::Printf(TEXT("%s cannot be matched: %s"),
													 *Wanted.Name.ToString(), *Why));
							continue;
						}
						TArray<FCataclysmStatModifier>* OnStat =
							Added.Find(FName(*Wanted.Row->Stat));
						TestTrue(*FString::Printf(
									 TEXT("%s (%s %s %g) is granted by option %d, in that bucket"),
									 *Wanted.Name.ToString(), *Wanted.Row->Stat,
									 *Wanted.Row->ValueKind, Wanted.Row->ValuePerPoint,
									 Option.Key),
								 OnStat && TakeOne(*OnStat, Expect));
					}

					// AND NOTHING ELSE: another option's row would be left over here.
					for (const TPair<FName, TArray<FCataclysmStatModifier>>& Left : Added)
					{
						TestEqual(*FString::Printf(
									  TEXT("%s option %d grants no %s beyond its own rows"),
									  *Node.Key, Option.Key, *Left.Key.ToString()),
								  Left.Value.Num(), 0);
					}
					++OptionsChecked;
				}
			}
		}

		AddInfo(FString::Printf(TEXT("%s: %d capstone options, %d rows checked, "
									 "at least %d and %d expected"),
								*Tree.Key, OptionsChecked, RowsChecked, FewestOptions,
								Tree.Value));
		TestTrue(*FString::Printf(TEXT("%s: at least %d options checked, %d were"), *Tree.Key,
								  FewestOptions, OptionsChecked),
				 OptionsChecked >= FewestOptions);
		TestTrue(*FString::Printf(TEXT("%s: at least %d rows checked, %d were"), *Tree.Key,
								  Tree.Value, RowsChecked),
				 RowsChecked >= Tree.Value);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
