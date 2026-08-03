// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "GameplayTagsManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * Loads every generated CSV through the struct it is supposed to match.
 *
 * This is the half a Python test cannot do. The generator can produce a
 * perfectly well-formed CSV whose columns do not match the USTRUCT, and Unreal
 * will import it without complaint -- the mismatched column simply arrives as
 * its default value. Nothing reports it. The first symptom is a weight of zero
 * or an empty description somewhere far from the cause.
 *
 * CreateTableFromCSVString returns one string per problem, so an unmatched
 * column or a bad value fails the test with the engine's own message.
 */

namespace
{
	FString DataDir()
	{
		return FPaths::ProjectDir() / TEXT("Data");
	}

	/** Loads one CSV through RowStruct. Returns false and fills OutError on failure. */
	template <typename RowType>
	bool LoadTable(const TCHAR* FileName, int32& OutRowCount, FString& OutError)
	{
		const FString Path = DataDir() / FileName;

		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			OutError = FString::Printf(TEXT("could not read %s"), *Path);
			return false;
		}

		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = RowType::StaticStruct();

		const TArray<FString> Problems = Table->CreateTableFromCSVString(Contents);
		if (Problems.Num() > 0)
		{
			OutError = FString::Printf(TEXT("%s: %d import problem(s): %s"),
				FileName, Problems.Num(), *FString::Join(Problems, TEXT(" | ")));
			return false;
		}

		OutRowCount = Table->GetRowMap().Num();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDataTablesImportTest,
	"Cataclysm.Data.EveryGeneratedTableImports",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDataTablesImportTest::RunTest(const FString& Parameters)
{
	int32 Rows = 0;
	FString Error;

	// Row counts are pinned. A generator change that silently drops rows -- a
	// header misread, an off-by-one on a column -- shows up here rather than as
	// missing content much later.
	struct FExpectation { const TCHAR* File; int32 ExpectedRows; };

	#define CHECK_TABLE(RowType, File, Expected) \
		Rows = 0; Error.Empty(); \
		if (!LoadTable<RowType>(TEXT(File), Rows, Error)) \
		{ AddError(Error); } \
		else { TestEqual(TEXT(File) TEXT(" row count"), Rows, Expected); }

	CHECK_TABLE(FCataclysmDungeonModifierRow,   "DungeonModifiers.csv",      116)
	CHECK_TABLE(FCataclysmWeaponSkillRow,       "WeaponSkills.csv",          398)
	// 380, not 381. One weight 1 positive enchantment was removed: it read "Your
	// block chance applies to AOE damage at 50% effectiveness", which became
	// strictly harmful once block was decided to apply to area damage by
	// default at full effectiveness.
	CHECK_TABLE(FCataclysmEnchantmentRow,       "EnchantmentsPositive.csv",  380)
	CHECK_TABLE(FCataclysmEnchantmentRow,       "EnchantmentsNegative.csv",  195)
	CHECK_TABLE(FCataclysmEnemyModifierRow,     "EnemyModifiers.csv",         79)
	// 49, not 46. Three player-applied debuffs were defined: Madness, Cripple
	// and Shred. All three were already applied by gems and by affixes, and none
	// of them said what they did.
	CHECK_TABLE(FCataclysmStatusEffectRow,      "StatusEffects.csv",          49)
	CHECK_TABLE(FCataclysmGemRow,               "Gems.csv",                   25)
	CHECK_TABLE(FCataclysmCityUpgradeRow,       "CityUpgrades.csv",           24)
	CHECK_TABLE(FCataclysmCraftingMaterialRow,  "CraftingMaterials.csv",      37)

	#undef CHECK_TABLE

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDataTableValuesTest,
	"Cataclysm.Data.ValuesSurviveImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDataTableValuesTest::RunTest(const FString& Parameters)
{
	// Guards the test above. A row count can be right while every field is
	// empty, because a column whose name does not match the struct imports as a
	// default rather than an error.
	FString Contents;
	const FString Path = DataDir() / TEXT("DungeonModifiers.csv");
	if (!FFileHelper::LoadFileToString(Contents, *Path))
	{
		AddError(FString::Printf(TEXT("could not read %s"), *Path));
		return false;
	}

	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FCataclysmDungeonModifierRow::StaticStruct();
	Table->CreateTableFromCSVString(Contents);

	int32 WithWeight = 0;
	int32 WithDescription = 0;
	int32 WithCataclysm = 0;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const auto* Row = reinterpret_cast<FCataclysmDungeonModifierRow*>(Pair.Value);
		if (Row->Weight > 0.0f)          { ++WithWeight; }
		if (!Row->Description.IsEmpty()) { ++WithDescription; }
		if (!Row->CataclysmType.IsEmpty()) { ++WithCataclysm; }
	}

	TestEqual(TEXT("every dungeon modifier has a non-zero weight"), WithWeight, 116);
	TestEqual(TEXT("every dungeon modifier has a description"), WithDescription, 116);
	TestEqual(TEXT("every dungeon modifier names a Cataclysm"), WithCataclysm, 116);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDataTableTagsTest,
	"Cataclysm.Data.EveryReferencedTagResolves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDataTableTagsTest::RunTest(const FString& Parameters)
{
	// The generator checks tags against the spreadsheet. This checks them against
	// the engine, which is what actually matters at runtime: a tag the engine
	// does not know matches nothing and reports nothing.
	const TCHAR* Files[] = { TEXT("WeaponSkills.csv"),
							 TEXT("EnchantmentsPositive.csv"),
							 TEXT("EnchantmentsNegative.csv") };

	int32 Checked = 0;
	for (const TCHAR* File : Files)
	{
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *(DataDir() / File)))
		{
			AddError(FString::Printf(TEXT("could not read %s"), File));
			continue;
		}

		TArray<FString> Lines;
		Contents.ParseIntoArrayLines(Lines);
		for (int32 Index = 1; Index < Lines.Num(); ++Index)
		{
			// Tags are the trailing quoted field in every one of these files.
			int32 Start = INDEX_NONE;
			if (!Lines[Index].FindLastChar(TEXT('"'), Start))
			{
				continue;
			}
			FString Segment = Lines[Index];
			TArray<FString> Parts;
			Segment.ParseIntoArray(Parts, TEXT(","), true);
			for (FString Part : Parts)
			{
				Part.TrimStartAndEndInline();
				Part.ReplaceInline(TEXT("\""), TEXT(""));
				// Only consider things shaped like a tag: dotted, capitalised.
				if (!Part.Contains(TEXT(".")) || Part.Contains(TEXT(" ")))
				{
					continue;
				}
				if (!FChar::IsUpper(Part[0]))
				{
					continue;
				}
				const FGameplayTag Tag = UGameplayTagsManager::Get()
					.RequestGameplayTag(FName(*Part), /*ErrorIfNotFound=*/false);
				if (!Tag.IsValid())
				{
					AddError(FString::Printf(
						TEXT("%s line %d references unknown tag %s"),
						File, Index + 1, *Part));
				}
				++Checked;
			}
		}
	}

	TestTrue(TEXT("some tags were actually checked"), Checked > 100);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
