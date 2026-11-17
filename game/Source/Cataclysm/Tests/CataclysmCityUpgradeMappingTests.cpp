// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmCityUpgradeMapping.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

/**
 * Tests for the join between `game/Data/CityUpgrades.csv` and the effects the
 * empire layer can act on.
 *
 * WHY THESE LIVE HERE AND THE REST LIVE IN `CataclysmEmpire`. That module must
 * not depend on this one, so it cannot see a DataTable at all. It tests what an
 * upgrade DOES; these test that every row of the table is recognised, which only
 * this side can ask.
 *
 * THEY READ THE ASSET RATHER THAN THE CSV, which is what the game loads and what
 * a packaged build ships. A CSV changed without
 * `tools/generate_datatable_assets.py` being run leaves the asset holding the
 * previous data, and these would then be checking the previous data too --
 * `tools/tests/test_datatable_assets_are_current.py` is what catches that.
 */

// ---------------------------------------------------------------------------
// Every row is recognised
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeMappingCoversTableTest,
	"Cataclysm.CityUpgrade.EveryRowOfTheTableMapsToAnEffect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeMappingCoversTableTest::RunTest(
	const FString& Parameters)
{
	const TArray<FName> RowNames = UCataclysmCityUpgradeMapping::AllRowNames();

	// THE GUARD AGAINST THIS WHOLE TEST BEING VACUOUS. Every assertion below
	// walks that list, so an empty one passes while checking nothing.
	if (!TestEqual(TEXT("the table holds twenty-four upgrades"),
				   RowNames.Num(), 24))
	{
		return false;
	}

	TSet<ECataclysmCityUpgradeEffect> Seen;

	for (const FName RowName : RowNames)
	{
		const ECataclysmCityUpgradeEffect Effect =
			UCataclysmCityUpgradeMapping::EffectFor(RowName);

		// `None` MEANS THE MAPPING IS OUT OF DATE, not that the upgrade does
		// nothing. A row name is built from the branch and the first characters
		// of the effect sentence, so rewording a sentence in the workbook
		// renames the row and this is where that surfaces.
		TestTrue(*FString::Printf(
					 TEXT("%s is recognised. If the sentence was reworded in "
						  "the workbook the row name changed, and "
						  "UCataclysmCityUpgradeMapping needs the new one."),
					 *RowName.ToString()),
				 Effect != ECataclysmCityUpgradeEffect::None);

		// AND NO TWO ROWS SHARE AN EFFECT. Two rows mapped to one value would
		// make one of them silently do the other's job.
		TestFalse(*FString::Printf(TEXT("%s does not share an effect with an "
									   "earlier row"),
								   *RowName.ToString()),
				  Seen.Contains(Effect));

		Seen.Add(Effect);
	}

	TestEqual(TEXT("so all twenty-four effects are used exactly once"),
			  Seen.Num(), 24);

	return true;
}

// ---------------------------------------------------------------------------
// The four rows that are told apart only by a suffix
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeMappingSubTypesTest,
	"Cataclysm.CityUpgrade.EachSubTypeRowMapsToTheSubTypeItsSentenceNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeMappingSubTypesTest::RunTest(
	const FString& Parameters)
{
	const UDataTable* Table = UCataclysmCityUpgradeMapping::LoadGeneratedTable();

	if (!TestNotNull(TEXT("the table loaded"), Table))
	{
		return false;
	}

	// FOUR ROWS BEGIN WITH THE SAME WORDS -- "Increases the chance of dungeons
	// on this city being ..." -- so three of them are told apart only by the _1
	// and _2 the generator appends to keep names unique, and the fourth by a
	// spelling mistake in the sheet. Reordering those rows in the workbook would
	// move the suffixes and swap Elite with Sacrificial, silently. Checking each
	// row's own sentence names the sub-type it was mapped to is what catches it.
	const TMap<ECataclysmCityUpgradeEffect, FString> Expected = {
		{ ECataclysmCityUpgradeEffect::SubTypeChanceHorde,		TEXT("Horde") },
		{ ECataclysmCityUpgradeEffect::SubTypeChanceElite,		TEXT("Elite") },
		{ ECataclysmCityUpgradeEffect::SubTypeChanceSacrificial, TEXT("Sacrificial") },
		{ ECataclysmCityUpgradeEffect::SubTypeChanceVolatile,	TEXT("Volatile") },
	};

	int32 Checked = 0;

	for (const FName RowName : UCataclysmCityUpgradeMapping::AllRowNames())
	{
		const ECataclysmCityUpgradeEffect Effect =
			UCataclysmCityUpgradeMapping::EffectFor(RowName);

		const FString* Word = Expected.Find(Effect);
		if (Word == nullptr)
		{
			continue;
		}

		const FCataclysmCityUpgradeRow* Row =
			Table->FindRow<FCataclysmCityUpgradeRow>(RowName, TEXT("SubTypes"));

		if (!TestNotNull(*FString::Printf(TEXT("%s is in the table"),
										  *RowName.ToString()),
						 Row))
		{
			continue;
		}

		++Checked;

		TestTrue(*FString::Printf(
					 TEXT("%s says %s, which is what it was mapped to. Its "
						  "sentence is \"%s\""),
					 *RowName.ToString(), **Word, *Row->Effect),
				 Row->Effect.Contains(*Word, ESearchCase::CaseSensitive));
	}

	// WITHOUT THIS THE LOOP COULD MATCH NOTHING and the test would pass having
	// checked no row at all.
	TestEqual(TEXT("all four sub-type rows were checked"), Checked, 4);

	return true;
}

// ---------------------------------------------------------------------------
// What Make reads
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeMappingReadsTierOneTest,
	"Cataclysm.CityUpgrade.AnUpgradeIsBuiltFromTheTierOneColumns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeMappingReadsTierOneTest::RunTest(
	const FString& Parameters)
{
	// TIER 1 IS WHAT A NEWLY BOUGHT UPGRADE IS WORTH. Those columns were only
	// published on 2026-09-05; before that the tier 1 magnitude lived solely
	// inside the effect sentence and nothing could read it. Issue #1262.
	const FCataclysmCityUpgrade MaxDefence =
		UCataclysmCityUpgradeMapping::MakeFromTable(
			TEXT("Architect_Increase_max_defense_by_20"));

	TestEqual(TEXT("it is the raise-maximum-defence effect"),
			  MaxDefence.Effect, ECataclysmCityUpgradeEffect::MaxDefence);
	TestEqual(TEXT("worth a fifth, which is what its sentence says"),
			  MaxDefence.Value, 0.2f, 0.0001f);
	TestEqual(TEXT("bought at tier one"), MaxDefence.Tier, 1);
	TestFalse(TEXT("and it is a standing improvement"),
			  MaxDefence.bOneTimeUse);

	// THE INTERVAL HALF IS READ TOO, and it is the half a naive reader of the
	// sentence loses: "Every 20 days this city's defenses heal 5%" states the
	// interval first and the magnitude second.
	const FCataclysmCityUpgrade Heal =
		UCataclysmCityUpgradeMapping::MakeFromTable(
			TEXT("Architect_Every_20_days_this_city_s_defenses_heal"));

	TestEqual(TEXT("the repair-every-N-days effect"),
			  Heal.Effect, ECataclysmCityUpgradeEffect::HealDefenceEvery);
	TestEqual(TEXT("repairs a twentieth"), Heal.Value, 0.05f, 0.0001f);
	TestEqual(TEXT("every twenty days"), Heal.IntervalDays, 20.0f, 0.0001f);

	// A ONE-TIME UPGRADE IS MARKED AS ONE. The asterisk on the branch name in
	// the workbook is what says so; the generator strips it into a flag.
	const FCataclysmCityUpgrade Remove =
		UCataclysmCityUpgradeMapping::MakeFromTable(
			TEXT("Architect_Remove_25_of_dungeons_on_this_city"));

	TestEqual(TEXT("the remove-dungeons effect"),
			  Remove.Effect, ECataclysmCityUpgradeEffect::RemoveDungeons);
	TestEqual(TEXT("a quarter of them"), Remove.Value, 0.25f, 0.0001f);
	TestTrue(TEXT("and it fires once and is spent"), Remove.bOneTimeUse);

	// A ROW THAT IS NOT THERE IS NOT AN UPGRADE, rather than a default one that
	// something might try to buy.
	const FCataclysmCityUpgrade Missing =
		UCataclysmCityUpgradeMapping::MakeFromTable(TEXT("NoSuchRow"));

	TestEqual(TEXT("an unknown row is not an upgrade"),
			  Missing.Effect, ECataclysmCityUpgradeEffect::None);
	TestFalse(TEXT("and says so"), Missing.IsValid());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
