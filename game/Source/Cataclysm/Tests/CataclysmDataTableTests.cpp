// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "GameplayTagsManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/CataclysmTestSkip.h"

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

	// EVERY GENERATED TABLE IMPORTS THROUGH ITS ROW STRUCT, WITH ROWS. The row counts were pinned here until issue #2928;
	// they only restated the CSVs (`tools/tests/test_unreal_pinned_row_counts.py` kept the two equal) and moved with
	// every row added. What only this test sees is an import problem, which `CreateTableFromCSVString` reports.
	#define CHECK_TABLE(RowType, File) \
		Rows = 0; Error.Empty(); \
		if (!LoadTable<RowType>(TEXT(File), Rows, Error)) \
		{ AddError(Error); } \
		else { TestTrue(TEXT(File) TEXT(" has rows"), Rows > 0); }

	CHECK_TABLE(FCataclysmDungeonModifierRow,   "DungeonModifiers.csv")
	CHECK_TABLE(FCataclysmWeaponSkillRow,       "WeaponSkills.csv")
	CHECK_TABLE(FCataclysmEnchantmentRow,       "EnchantmentsPositive.csv")
	CHECK_TABLE(FCataclysmEnchantmentRow,       "EnchantmentsNegative.csv")
	CHECK_TABLE(FCataclysmEnemyModifierRow,     "EnemyModifiers.csv")
	CHECK_TABLE(FCataclysmStatusEffectRow,      "StatusEffects.csv")
	CHECK_TABLE(FCataclysmGemRow,               "Gems.csv")
	CHECK_TABLE(FCataclysmCityUpgradeRow,       "CityUpgrades.csv")
	CHECK_TABLE(FCataclysmCraftingMaterialRow,  "CraftingMaterials.csv")
	CHECK_TABLE(FCataclysmForgeOperationRow,    "ForgeOperations.csv")
	CHECK_TABLE(FCataclysmItemBaseRow,          "ItemBases.csv")
	CHECK_TABLE(FCataclysmItemSocketRow,        "ItemSockets.csv")
	CHECK_TABLE(FCataclysmAffixRow,             "Affixes.csv")
	CHECK_TABLE(FCataclysmAffixTierRow,         "AffixTiers.csv")
	CHECK_TABLE(FCataclysmGearRarityRow,        "GearRarity.csv")
	CHECK_TABLE(FCataclysmClassStatRow,         "ClassStats.csv")
	CHECK_TABLE(FCataclysmMinionTypeRow,        "MinionTypes.csv")
	CHECK_TABLE(FCataclysmMinionScalingRow,     "MinionScaling.csv")
	CHECK_TABLE(FCataclysmAttributeEffectRow,   "Attributes.csv")
	CHECK_TABLE(FCataclysmSkillSlotRow,         "SkillSlots.csv")
	CHECK_TABLE(FCataclysmEnemyArchetypeRow,    "EnemyArchetypes.csv")
	CHECK_TABLE(FCataclysmEnemyRarityRow,       "EnemyRarities.csv")
	CHECK_TABLE(FCataclysmEnemyDropRow,         "EnemyDrops.csv")
	CHECK_TABLE(FCataclysmMaterialTierRow,      "MaterialTiers.csv")
	CHECK_TABLE(FCataclysmGoldRow,              "Gold.csv")
	CHECK_TABLE(FCataclysmElementVisualRow,     "ElementVisuals.csv")
	CHECK_TABLE(FCataclysmPassiveNodeRow,       "PassiveNodes.csv")
	CHECK_TABLE(FCataclysmPassiveEdgeRow,       "PassiveEdges.csv")
	CHECK_TABLE(FCataclysmEmpireTreeNodeRow,    "EmpireTreeNodes.csv")
	CHECK_TABLE(FCataclysmEmpireTreeEdgeRow,    "EmpireTreeEdges.csv")
	CHECK_TABLE(FCataclysmEmpireTreeEffectRow,  "EmpireTreeEffects.csv")
	CHECK_TABLE(FCataclysmPassiveEffectRow,     "PassiveEffects.csv")

	CHECK_TABLE(FCataclysmEnchantmentEffectRow, "EnchantmentEffects.csv")

	CHECK_TABLE(FCataclysmWeaponMeshRow,        "WeaponMeshes.csv")

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

	// COMPARED AGAINST THE TABLE'S OWN ROW COUNT, not against a literal.
	//
	// All three of these read 116 until issue #363. Adding the Corrupted
	// Stalker for issue #504 took the table to 117 and made all three false,
	// and the number said nothing about what the test is for: the claim is that
	// EVERY modifier carries these, not that 116 of them do. Written this way
	// the assertion states the claim and cannot go stale when a modifier is
	// added.
	//
	// The guard here is only against comparing zero with zero if the table
	// failed to load at all.
	const int32 Total = Table->GetRowMap().Num();
	TestTrue(TEXT("the dungeon modifier table loaded rows"), Total > 0);
	TestEqual(TEXT("every dungeon modifier has a non-zero weight"), WithWeight, Total);
	TestEqual(TEXT("every dungeon modifier has a description"), WithDescription, Total);
	TestEqual(TEXT("every dungeon modifier names a Cataclysm"), WithCataclysm, Total);

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
	//
	// TWO WERE MISSING FOR THREE DAYS. DT_MinionTypes and DT_MinionScaling
	// landed with issues #337 and #338 and were never added here, so neither
	// asset was ever compared against its CSV and every test still passed. The
	// sibling list of CHECK_TABLE lines above did not have the same hole,
	// because tools/tests/test_unreal_pinned_row_counts.py fails when a
	// generated CSV has no CHECK_TABLE line. This list now has a matching guard,
	// tools/tests/test_unreal_asset_check_covers_every_table.py. Issue #702.
	struct FPair { const TCHAR* Asset; const TCHAR* File; };
	const FPair Tables[] = {
		{ TEXT("DT_Affixes"),               TEXT("Affixes.csv") },
		{ TEXT("DT_AffixTiers"),            TEXT("AffixTiers.csv") },
		{ TEXT("DT_Attributes"),            TEXT("Attributes.csv") },
		{ TEXT("DT_CityUpgrades"),          TEXT("CityUpgrades.csv") },
		{ TEXT("DT_ClassStats"),            TEXT("ClassStats.csv") },
		{ TEXT("DT_CraftingMaterials"),     TEXT("CraftingMaterials.csv") },
		{ TEXT("DT_ForgeOperations"),       TEXT("ForgeOperations.csv") },
		{ TEXT("DT_DungeonModifiers"),      TEXT("DungeonModifiers.csv") },
		{ TEXT("DT_ElementVisuals"),        TEXT("ElementVisuals.csv") },
		{ TEXT("DT_EmpireTreeEdges"),       TEXT("EmpireTreeEdges.csv") },
		{ TEXT("DT_EmpireTreeEffects"),     TEXT("EmpireTreeEffects.csv") },
		{ TEXT("DT_EmpireTreeNodes"),       TEXT("EmpireTreeNodes.csv") },
		{ TEXT("DT_EnchantmentEffects"),    TEXT("EnchantmentEffects.csv") },
		{ TEXT("DT_EnchantmentsNegative"),  TEXT("EnchantmentsNegative.csv") },
		{ TEXT("DT_EnchantmentsPositive"),  TEXT("EnchantmentsPositive.csv") },
		{ TEXT("DT_EnemyArchetypes"),       TEXT("EnemyArchetypes.csv") },
		{ TEXT("DT_EnemyDrops"),            TEXT("EnemyDrops.csv") },
		{ TEXT("DT_EnemyModifiers"),        TEXT("EnemyModifiers.csv") },
		{ TEXT("DT_EnemyRarities"),         TEXT("EnemyRarities.csv") },
		{ TEXT("DT_GearRarity"),            TEXT("GearRarity.csv") },
		{ TEXT("DT_Gems"),                  TEXT("Gems.csv") },
		{ TEXT("DT_Gold"),                  TEXT("Gold.csv") },
		{ TEXT("DT_ItemBases"),             TEXT("ItemBases.csv") },
		{ TEXT("DT_ItemSockets"),           TEXT("ItemSockets.csv") },
		{ TEXT("DT_MaterialTiers"),         TEXT("MaterialTiers.csv") },
		{ TEXT("DT_MinionScaling"),         TEXT("MinionScaling.csv") },
		{ TEXT("DT_PassiveEdges"),          TEXT("PassiveEdges.csv") },
		{ TEXT("DT_PassiveEffects"),        TEXT("PassiveEffects.csv") },
		{ TEXT("DT_PassiveNodes"),          TEXT("PassiveNodes.csv") },
		{ TEXT("DT_MinionTypes"),           TEXT("MinionTypes.csv") },
		{ TEXT("DT_SkillSlots"),            TEXT("SkillSlots.csv") },
		{ TEXT("DT_StatusEffects"),         TEXT("StatusEffects.csv") },
		{ TEXT("DT_WeaponMeshes"),          TEXT("WeaponMeshes.csv") },
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
		//
		// EDITOR ONLY, AND THAT IS THE ENGINE'S CHOICE RATHER THAN THIS TEST'S.
		// UDataTable::GetTableAsCSV is declared inside #if WITH_EDITOR in
		// Engine/Classes/Engine/DataTable.h. This file compiles under
		// WITH_AUTOMATION_TESTS, which is true in a Development PACKAGED build
		// and does not imply WITH_EDITOR, so without this guard the packaged
		// `Cataclysm` target fails to compile while the `CataclysmEditor` target
		// builds fine. It did fail, from 2026-09-01 to 2026-09-02, and nothing
		// noticed because continuous integration does not build the C++ at all
		// (that is #20). Issue #1196.
		//
		// There is no runtime substitute. The row-name comparison above uses
		// GetRowMap and runs in both targets; only this contents comparison is
		// lost, and the #else below says so rather than passing quietly.
#if WITH_EDITOR
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
#endif // WITH_EDITOR
	}

#if !WITH_EDITOR
	// SAID ONCE, NOT ONCE PER TABLE. The loop above covers every generated
	// DataTable, and one report naming the whole comparison is what a reader
	// needs; twenty-eight identical lines would bury the run's other output.
	CataclysmTestSkip::ReportSkippedHalf(*this,
		TEXT("whether each DataTable asset's row CONTENTS match its CSV is not "
			 "checked; that every row name is present in both is. "
			 "UDataTable::GetTableAsCSV is editor-only and this is a packaged "
			 "build, so the comparison cannot be made here. Run the automation "
			 "tests against the CataclysmEditor target to check it."));
#endif // !WITH_EDITOR

	return true;
}

/**
 * The DataTable asset holds a damage type LIMIT on every weapon, not a count.
 *
 * The column was renamed from DamageTypeSlots to MaxDamageTypes for #218,
 * because the old name read as a count and the value has meant a maximum since
 * #217. A rename like that can go wrong in a way nothing else here notices: the
 * import test above loads the CSV through the struct and would catch a column
 * with no matching property, but the asset comparison would not, because both
 * halves would import the same missing column the same way and agree.
 *
 * So this reads the shipped asset and checks the numbers are the design's
 * limits. Four for a one-hander and eight for a two-hander, from the Weapon
 * Bases table in docs/Cataclysm_GDD_v2.md, and zero on anything that is not a
 * weapon. Written here rather than read from the table under test.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemBaseMaxDamageTypesTest,
	"Cataclysm.Data.ItemBasesHoldADamageTypeLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemBaseMaxDamageTypesTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_ItemBases.DT_ItemBases"));
	if (!Table)
	{
		AddError(TEXT("DT_ItemBases does not exist. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	int32 OneHanders = 0;
	int32 TwoHanders = 0;
	int32 NotWeapons = 0;

	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FCataclysmItemBaseRow* Row =
			reinterpret_cast<const FCataclysmItemBaseRow*>(Pair.Value);

		if (Row->Hands == 1)
		{
			++OneHanders;
			TestEqual(FString::Printf(
				TEXT("%s is one-handed and tops out at four damage types"),
				*Row->BaseName), Row->MaxDamageTypes, 4);
		}
		else if (Row->Hands == 2)
		{
			++TwoHanders;
			TestEqual(FString::Printf(
				TEXT("%s is two-handed and tops out at eight damage types"),
				*Row->BaseName), Row->MaxDamageTypes, 8);
		}
		else
		{
			++NotWeapons;
			TestEqual(FString::Printf(
				TEXT("%s is not a weapon and holds no damage types"),
				*Row->BaseName), Row->MaxDamageTypes, 0);
		}
	}

	// Without these the loop above passes on an empty table, which is the
	// failure the rename could actually cause.
	TestTrue(TEXT("the table has one-handed weapons"), OneHanders > 0);
	TestTrue(TEXT("the table has two-handed weapons"), TwoHanders > 0);
	TestTrue(TEXT("the table has bases that are not weapons"), NotWeapons > 0);

	return true;
}

/**
 * The effect palette arrives in the engine as colours and tags, not as defaults.
 *
 * WHAT THIS ADDS THAT THE PYTHON TESTS CANNOT. Two things, and both need the
 * engine.
 *
 * FIRST, THE TYPES. FCataclysmElementVisualRow is the only row struct in this
 * project holding an FLinearColor or an FGameplayTag, and both are imported from
 * text by code that reports nothing when it fails: a cell Unreal cannot parse
 * leaves the property at its C++ default and the row still imports. Comparing
 * CSV headers against property names, which is what
 * tools/tests/test_csv_columns_match_their_row_structs.py does, cannot see that
 * -- the names match perfectly and the values are gone. So every default on that
 * struct is deliberately a value the table never carries, and this test is what
 * makes that arrangement worth having.
 *
 * SECOND, THE CONVERSION. tools/generate_datatables.py converts the design
 * document's sRGB hex to linear in Python. The engine has its own conversion,
 * FLinearColor::FromSRGBColor, which is what ACataclysmTelegraphMarker uses on
 * the same document's hex. Those are two implementations of one curve, and
 * nothing compared them until here. One row is checked in full against the
 * engine's own function, which is all it takes to catch a curve that disagrees;
 * tools/tests/test_element_visuals_match_the_design.py pins all eight rows to
 * the design document, so the palette itself is covered there and is not copied
 * into this file.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmElementVisualsTest,
	"Cataclysm.Data.ElementVisualsCarryTheDesignedValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmElementVisualsTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_ElementVisuals.DT_ElementVisuals"));
	if (!Table)
	{
		AddError(TEXT("DT_ElementVisuals does not exist. Run "
					  "tools/generate_datatable_assets.py through "
					  "tools/run_editor_python.py."));
		return false;
	}

	// The CSV is written to six decimal places, so the engine's own conversion
	// and the generator's agree to about a millionth. A tolerance and not an
	// equality for that reason alone.
	const float Tolerance = 1.0e-4f;

	const TCHAR* ElementPrefix = TEXT("Element.");

	TSet<FString> PrimariesSeen;
	int32 Checked = 0;

	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FCataclysmElementVisualRow* Row =
			reinterpret_cast<const FCataclysmElementVisualRow*>(Pair.Value);
		const FString Key = Pair.Key.ToString();

		// An FGameplayTag that failed to import, or that names something the
		// engine does not declare, is invalid. A skill asking an invalid tag for
		// its colours matches no row and still plays, with no complaint.
		if (!Row->ElementTag.IsValid())
		{
			AddError(FString::Printf(
				TEXT("row %s has no valid ElementTag, so nothing holding a "
					 "damage type tag can find it."), *Key));
			continue;
		}

		const FString TagText = Row->ElementTag.ToString();
		TestTrue(FString::Printf(TEXT("%s is keyed on a damage type"), *Key),
			TagText.StartsWith(ElementPrefix));

		// The row key is the tag's leaf, which is what lets anything holding a
		// tag reach its row without a second lookup table.
		FString Leaf;
		TagText.Split(TEXT("."), nullptr, &Leaf, ESearchCase::CaseSensitive,
					  ESearchDir::FromEnd);
		TestEqual(FString::Printf(
			TEXT("row %s is named after the leaf of %s"), *Key, *TagText),
			Leaf, Key);

		// EVERY ROW HAVING A DIFFERENT PRIMARY IS THE CHECK THAT CATCHES THE
		// WHOLE TABLE FAILING TO IMPORT. Eight rows all sitting on the struct's
		// white default would pass every per-row check that only asks whether a
		// colour is present.
		const FString Primary = Row->PrimaryColour.ToString();
		TestFalse(FString::Printf(
			TEXT("%s has a primary colour no other damage type has"), *Key),
			PrimariesSeen.Contains(Primary));
		PrimariesSeen.Add(Primary);

		// The secondary's job is to stay readable where the primary matches the
		// floor, which it can only do by being much darker.
		TestTrue(FString::Printf(
			TEXT("%s's secondary is darker than its primary"), *Key),
			Row->SecondaryColour.GetLuminance() < Row->PrimaryColour.GetLuminance());

		// 1.0 and not 0.0. The struct defaults these to zero precisely so that
		// this assertion can fail: the generator refuses a zero, so a row
		// reading zero here did not import.
		TestEqual(FString::Printf(TEXT("%s emissive multiplier"), *Key),
			Row->EmissiveMultiplier, 1.0f, Tolerance);
		TestEqual(FString::Printf(TEXT("%s spawn rate scale"), *Key),
			Row->SpawnRateScale, 1.0f, Tolerance);
		TestEqual(FString::Printf(TEXT("%s velocity scale"), *Key),
			Row->VelocityScale, 1.0f, Tolerance);

		++Checked;
	}

	// Without this the loop above passes on an empty table, which is what a
	// stale or unbuilt asset actually looks like.
	TestEqual(TEXT("every damage type was checked"), Checked, 8);

	// The two conversions compared. Demonic, from the effect palette table in
	// section XIII of docs/Cataclysm_GDD_v2.md.
	const FCataclysmElementVisualRow* Demonic =
		Table->FindRow<FCataclysmElementVisualRow>(
			FName(TEXT("Demonic")), TEXT("ElementVisuals conversion check"));
	if (!Demonic)
	{
		AddError(TEXT("DT_ElementVisuals has no row named Demonic."));
		return false;
	}

	const FLinearColor ExpectedPrimary =
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("FF7A2E")));
	const FLinearColor ExpectedSecondary =
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("3A0A02")));

	TestTrue(FString::Printf(
		TEXT("Demonic's primary is #FF7A2E converted to linear. Expected %s, "
			 "got %s"), *ExpectedPrimary.ToString(),
		*Demonic->PrimaryColour.ToString()),
		Demonic->PrimaryColour.Equals(ExpectedPrimary, Tolerance));

	TestTrue(FString::Printf(
		TEXT("Demonic's secondary is #3A0A02 converted to linear. Expected %s, "
			 "got %s"), *ExpectedSecondary.ToString(),
		*Demonic->SecondaryColour.ToString()),
		Demonic->SecondaryColour.Equals(ExpectedSecondary, Tolerance));

	return true;
}
#endif // WITH_AUTOMATION_TESTS
