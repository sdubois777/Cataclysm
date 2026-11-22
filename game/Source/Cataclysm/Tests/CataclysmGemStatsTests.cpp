// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Data/CataclysmDataRows.h"
#include "Items/CataclysmGems.h"
#include "Items/CataclysmItem.h"

/**
 * WHAT A GEM GIVES THE CHARACTER. Issue #46, slice G1.
 *
 * Ruled by the coordinating session on 2026-11-21: an ailment gem is a flat chance on the ailment's chance stat, as
 * the ailment affixes are; every other gem is an increase, by its text; the sheet's fraction is percentage points in
 * the stat pipeline, so 0.2 is 20. The rows here are built by hand with Of The Viper's sheet values, because the
 * generated table carries a gem's stat only from this slice's workbook stage.
 */
namespace CataclysmGemStatsTest
{
	/** Of The Viper's eight sheet values, `game/Data/Gems.csv`. */
	FCataclysmGemRow Viper()
	{
		FCataclysmGemRow Row;
		Row.GemName = TEXT("Of The Viper");
		Row.Effect = TEXT("20% chance to apply poison");
		Row.GemType = TEXT("Attack");
		Row.Everyday = 0.2f;
		Row.Quality = 0.4f;
		Row.Superb = 0.6f;
		Row.Masterful = 0.8f;
		Row.Legendary = 1.0f;
		Row.Mythical = 1.2f;
		Row.Ascendant = 1.4f;
		Row.Cataclysmic = 1.8f;
		return Row;
	}

	const FCataclysmStatModifier* OnlyModifierOf(const TMap<FName, TArray<FCataclysmStatModifier>>& Totals, FName Stat)
	{
		const TArray<FCataclysmStatModifier>* Found = Totals.Find(Stat);
		return Found && Found->Num() == 1 ? &(*Found)[0] : nullptr;
	}
}

// --------------------------------------------------------------------------------------------------------------------
// GS1. A gem's sheet value at each rarity is its column: Everyday from the effect text, then the seven numeric ones.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemStatsSheetValueTest,
	"Cataclysm.GemStats.AGemsSheetValueAtEachRarityIsItsColumn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemStatsSheetValueTest::RunTest(const FString&)
{
	using namespace CataclysmGemStatsTest;
	const FCataclysmGemRow Row = Viper();
	const ECataclysmRarity Rarities[] = {
		ECataclysmRarity::Everyday, ECataclysmRarity::Quality, ECataclysmRarity::Superb, ECataclysmRarity::Masterful,
		ECataclysmRarity::Legendary, ECataclysmRarity::Mythical, ECataclysmRarity::Ascendant,
		ECataclysmRarity::Cataclysmic};
	const float Expected[] = {0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 1.2f, 1.4f, 1.8f};
	for (int32 Index = 0; Index < 8; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Of The Viper at rarity %d is its sheet's %.1f"), Index + 1, Expected[Index]),
				  UCataclysmGems::SheetValueAt(Row, Rarities[Index]), Expected[Index], 0.0001f);
	}
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GS2. An ailment gem is a flat chance and any other gem an increase, from source Gem, in percentage points, and each
// reaches its stat through the pipeline.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemStatsBucketTest,
	"Cataclysm.GemStats.AnAilmentGemIsAFlatChanceAndAnyOtherGemAnIncrease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemStatsBucketTest::RunTest(const FString&)
{
	using namespace CataclysmGemStatsTest;
	const FName Poison(TEXT("poison_chance"));
	const FName Health(TEXT("max_health"));
	TMap<FName, TArray<FCataclysmStatModifier>> Totals;

	// OF THE VIPER, EVERYDAY: +20 POISON CHANCE, FLAT.
	TestTrue(TEXT("Of The Viper's Everyday 0.2 is added"),
			 UCataclysmGems::AddModifier(Poison, TEXT("flat"),
										 UCataclysmGems::SheetValueAt(Viper(), ECataclysmRarity::Everyday), Totals));
	const FCataclysmStatModifier* PoisonModifier = OnlyModifierOf(Totals, Poison);
	if (TestNotNull(TEXT("poison_chance holds exactly one modifier"), PoisonModifier))
	{
		TestEqual(TEXT("as 20 percentage points"), PoisonModifier->Value, 20.0f, 0.001f);
		TestTrue(TEXT("flat"), PoisonModifier->Bucket == ECataclysmStatBucket::Flat);
		TestTrue(TEXT("from a gem"), PoisonModifier->Source == ECataclysmModifierSource::Gem);
	}

	// OF LIFE, CATACLYSMIC: 85% INCREASED MAXIMUM HEALTH, THE KIND NAMED IN ANOTHER CASE.
	TestTrue(TEXT("Of Life's Cataclysmic 0.85 is added"),
			 UCataclysmGems::AddModifier(Health, TEXT("Increased"), 0.85f, Totals));
	const FCataclysmStatModifier* HealthModifier = OnlyModifierOf(Totals, Health);
	if (TestNotNull(TEXT("max_health holds exactly one modifier"), HealthModifier))
	{
		TestEqual(TEXT("as 85 percentage points"), HealthModifier->Value, 85.0f, 0.001f);
		TestTrue(TEXT("increased"), HealthModifier->Bucket == ECataclysmStatBucket::Increased);
		TestTrue(TEXT("from a gem"), HealthModifier->Source == ECataclysmModifierSource::Gem);
	}

	// AND THROUGH THE PIPELINE: 85% INCREASED ON A BASE OF 1,000 IS 1,850; A FLAT 20 ON A BASE OF NOUGHT IS 20.
	if (PoisonModifier && HealthModifier)
	{
		const float HealthFinal = UCataclysmStatPipeline::Evaluate(1000.0f, Totals[Health], FGameplayTagContainer()).Final;
		const float PoisonFinal = UCataclysmStatPipeline::Evaluate(0.0f, Totals[Poison], FGameplayTagContainer()).Final;
		TestEqual(TEXT("the increase reaches the stat: 1,000 x 1.85"), HealthFinal, 1850.0f, 0.01f);
		TestEqual(TEXT("the flat chance reaches the stat: 0 + 20"), PoisonFinal, 20.0f, 0.01f);
		AddInfo(FString::Printf(TEXT("GEM STATS: Of The Viper Everyday gives %.1f poison chance; Of Life Cataclysmic "
									 "takes 1,000 maximum health to %.1f"),
								PoisonFinal, HealthFinal));
	}
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GS3. CONTROL: a gem that names no stat, a kind that is not flat or increased, or a value of nought adds nothing.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemStatsRefusalTest,
	"Cataclysm.GemStats.AGemThatNamesNoStatOrKindOrValueAddsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGemStatsRefusalTest::RunTest(const FString&)
{
	const FName Health(TEXT("max_health"));

	// FOUR REFUSALS, EACH LOGGED.
	AddExpectedError(TEXT("grants nothing"), EAutomationExpectedErrorFlags::Contains, 4);
	TMap<FName, TArray<FCataclysmStatModifier>> Totals;
	TestFalse(TEXT("no stat"), UCataclysmGems::AddModifier(NAME_None, TEXT("flat"), 0.2f, Totals));
	TestFalse(TEXT("the kind more, since no gem's text says more"),
			  UCataclysmGems::AddModifier(Health, TEXT("more"), 0.2f, Totals));
	TestFalse(TEXT("no kind"), UCataclysmGems::AddModifier(Health, TEXT(""), 0.2f, Totals));
	TestFalse(TEXT("a value of nought"), UCataclysmGems::AddModifier(Health, TEXT("increased"), 0.0f, Totals));
	TestEqual(TEXT("and nothing was added"), Totals.Num(), 0);

	ECataclysmStatBucket Bucket = ECataclysmStatBucket::More;
	TestTrue(TEXT("CONTROL: a kind with spaces round it is still read"),
			 UCataclysmGems::BucketNamed(TEXT(" Flat "), Bucket) && Bucket == ECataclysmStatBucket::Flat);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
