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
	// 50, not 46. Four player-applied debuffs were defined: Madness, Cripple,
	// Shred and Weaken. All four were already applied by gems and by affixes,
	// and none of them said what they did.
	CHECK_TABLE(FCataclysmStatusEffectRow,      "StatusEffects.csv",          50)
	// 27, not 26. The Of Wasting gem was added to apply Necrosis, which was the
	// one status effect in the data that nothing applied, and the Of Embers gem
	// to apply Burn, which every designed Demonic skill applies and which no gem
	// and no affix could reach.
	CHECK_TABLE(FCataclysmGemRow,               "Gems.csv",                   27)
	CHECK_TABLE(FCataclysmCityUpgradeRow,       "CityUpgrades.csv",           24)
	CHECK_TABLE(FCataclysmCraftingMaterialRow,  "CraftingMaterials.csv",      37)
	// 55 bases across 11 slots, at least three per slot, because one base in a
	// slot is not a choice.
	CHECK_TABLE(FCataclysmItemBaseRow,          "ItemBases.csv",              55)
	// 70: 45 single-stat affixes, 3 resistance families, 10 ailments and 12
	// hybrids. The single-stat count rose from 35 on 2026-08-04: eight when
	// gear began granting a percentage increase to each primary attribute, and
	// two when mana leech and energy shield leech were added for #214.
	CHECK_TABLE(FCataclysmAffixRow,             "Affixes.csv",                70)
	// 30: nine stats on the shared default line, plus what the Ravager,
	// Ritualist and Masochist each override.
	CHECK_TABLE(FCataclysmClassStatRow,         "ClassStats.csv",             30)
	// 17: eight attributes, each raising two stats, except Efficacy raising
	// three.
	CHECK_TABLE(FCataclysmAttributeEffectRow,   "Attributes.csv",             17)
	// 7: the seven skill slots from the design document's Skill Slots table.
	// Six a player chooses between plus the automatic Basic Attack.
	CHECK_TABLE(FCataclysmSkillSlotRow,         "SkillSlots.csv",              7)

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


/**
 * Every generated CSV has a DataTable asset, and the two agree.
 *
 * WHY THIS IS THE ONLY GUARANTEE THERE IS. tools/generate_datatable_assets.py
 * imports each CSV under game/Data/ into an asset under /Game/Data/. It cannot
 * offer a --check mode the way tools/generate_datatables.py does, because a
 * .uasset carries generated identifiers that differ between runs, so two runs
 * over unchanged input do not produce identical bytes and a byte comparison
 * would always report a difference.
 *
 * So the asset is compared against the CSV it came from instead. An asset that
 * was never regenerated after a workbook change has the wrong rows, and the
 * game would load stale data with nothing reporting it -- the CSV in a pull
 * request would show the change and the game would not have it.
 *
 * Row NAMES are compared and not merely counts. A table that gained one row and
 * lost another has the same count.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDataTableAssetsTest,
	"Cataclysm.Data.EveryGeneratedTableHasAnAssetThatMatchesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDataTableAssetsTest::RunTest(const FString& Parameters)
{
	// Asset name against CSV file. Every table the generator produces is here;
	// one missing from this list would be one nothing ever checks.
	struct FPair { const TCHAR* Asset; const TCHAR* File; };
	const FPair Tables[] = {
		{ TEXT("DT_Affixes"),               TEXT("Affixes.csv") },
		{ TEXT("DT_Attributes"),            TEXT("Attributes.csv") },
		{ TEXT("DT_CityUpgrades"),          TEXT("CityUpgrades.csv") },
		{ TEXT("DT_ClassStats"),            TEXT("ClassStats.csv") },
		{ TEXT("DT_CraftingMaterials"),     TEXT("CraftingMaterials.csv") },
		{ TEXT("DT_DungeonModifiers"),      TEXT("DungeonModifiers.csv") },
		{ TEXT("DT_EnchantmentsNegative"),  TEXT("EnchantmentsNegative.csv") },
		{ TEXT("DT_EnchantmentsPositive"),  TEXT("EnchantmentsPositive.csv") },
		{ TEXT("DT_EnemyModifiers"),        TEXT("EnemyModifiers.csv") },
		{ TEXT("DT_Gems"),                  TEXT("Gems.csv") },
		{ TEXT("DT_ItemBases"),             TEXT("ItemBases.csv") },
		{ TEXT("DT_SkillSlots"),            TEXT("SkillSlots.csv") },
		{ TEXT("DT_StatusEffects"),         TEXT("StatusEffects.csv") },
		{ TEXT("DT_WeaponSkills"),          TEXT("WeaponSkills.csv") },
	};

	for (const FPair& Pair : Tables)
	{
		const FString AssetPath =
			FString::Printf(TEXT("/Game/Data/%s.%s"), Pair.Asset, Pair.Asset);

		const UDataTable* Table = LoadObject<UDataTable>(nullptr, *AssetPath);
		if (!Table)
		{
			AddError(FString::Printf(
				TEXT("%s does not exist. Run tools/generate_datatable_assets.py."),
				*AssetPath));
			continue;
		}

		// The CSV it should have come from, read the same way the test above
		// reads it, so the two halves cannot disagree about what the file is.
		const FString Path = DataDir() / Pair.File;
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			AddError(FString::Printf(TEXT("could not read %s"), *Path));
			continue;
		}

		UDataTable* FromCsv = NewObject<UDataTable>();
		FromCsv->RowStruct = Table->RowStruct;
		FromCsv->CreateTableFromCSVString(Contents);

		TSet<FName> InAsset;
		for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
		{
			InAsset.Add(Row.Key);
		}

		TSet<FName> InCsv;
		for (const TPair<FName, uint8*>& Row : FromCsv->GetRowMap())
		{
			InCsv.Add(Row.Key);
		}

		const TSet<FName> MissingFromAsset = InCsv.Difference(InAsset);
		const TSet<FName> NotInCsv = InAsset.Difference(InCsv);

		TestEqual(FString::Printf(
			TEXT("%s has every row %s has"), Pair.Asset, Pair.File),
			MissingFromAsset.Num(), 0);
		TestEqual(FString::Printf(
			TEXT("%s has no row %s does not"), Pair.Asset, Pair.File),
			NotInCsv.Num(), 0);

		if (MissingFromAsset.Num() > 0 || NotInCsv.Num() > 0)
		{
			AddError(FString::Printf(
				TEXT("%s is stale. Run tools/generate_datatable_assets.py. ")
				TEXT("%d row(s) only in the CSV, %d only in the asset."),
				Pair.Asset, MissingFromAsset.Num(), NotInCsv.Num()));
			continue;
		}

		// THE ROW NAMES MATCHING IS NOT ENOUGH, and this half was missing.
		//
		// Comparing only the key set says nothing about what is in the rows. It
		// let a real staleness through: the sixteen Demonic skills landed in the
		// CSV, the DataTable asset was never regenerated, and every row name
		// still matched because the rows had always existed -- they were simply
		// empty. The asset shipped with no Demonic skill in it and this test
		// passed.
		//
		// Both sides are re-exported through the same function so the comparison
		// is of contents rather than of file formatting.
		const FString AssetAsCsv = Table->GetTableAsCSV();
		const FString FileAsCsv = FromCsv->GetTableAsCSV();
		if (AssetAsCsv != FileAsCsv)
		{
			// Name the first row that differs. "Something differs somewhere in
			// 398 rows" is not actionable.
			TArray<FString> AssetLines, FileLines;
			AssetAsCsv.ParseIntoArrayLines(AssetLines);
			FileAsCsv.ParseIntoArrayLines(FileLines);

			FString FirstDifference = TEXT("(could not isolate a line)");
			for (int32 Line = 0; Line < FMath::Min(AssetLines.Num(), FileLines.Num()); ++Line)
			{
				if (AssetLines[Line] != FileLines[Line])
				{
					FirstDifference = FString::Printf(
						TEXT("line %d:\n    asset: %s\n    csv:   %s"),
						Line + 1,
						*AssetLines[Line].Left(200),
						*FileLines[Line].Left(200));
					break;
				}
			}

			AddError(FString::Printf(
				TEXT("%s has the right rows but different contents from %s, so ")
				TEXT("it is stale. Run tools/generate_datatable_assets.py. ")
				TEXT("First difference at %s"),
				Pair.Asset, Pair.File, *FirstDifference));
		}
	}

	return true;
}
#endif // WITH_AUTOMATION_TESTS
