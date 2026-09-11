// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * Tests for where an enchantment's number lands inside its stated range.
 * Issue #45.
 *
 * THE RULING. The project owner ruled on 2026-09-11: "When an enchantment is
 * put on an item, it rolls a value evenly inside its range and keeps it, and
 * the hover text shows it. Upgrading the item from +0 to +10 does not change
 * it." Before this, 385 of the 575 enchantment rows stated a range, and no item
 * had a number for any of them.
 *
 * WHAT IS CHECKED: the number a roll gives, the sentence the hover text shows,
 * that the stat receives the number the hover text shows, that the upgrade
 * level moves neither, that a drop gives each half a roll of its own, that a
 * benefit on two pieces uses the higher roll, and that the game reads as many
 * ranges in the two tables as the generator does.
 */
namespace CataclysmEnchantmentRollTest
{
	using FValues = UCataclysmItemValues;
	using FTotals = TMap<FName, TArray<FCataclysmStatModifier>>;

	/** A real benefit stating one range: "Your block chance is increased by 10%-20%". */
	const TCHAR* BlockBenefit =
		TEXT("Positive_Your_block_chance_is_increased_by_10_20");

	/** A real drawback stating one range: "Your attack speed is reduced by 20%-35%". */
	const TCHAR* SlowerDrawback =
		TEXT("Negative_Your_attack_speed_is_reduced_by_20_35");

	/** A real drawback with no effect written, to fill the other half of a pair. */
	const TCHAR* DrawbackWithNoEffect = TEXT("Negative_Can_t_use_a_basic_attack");

	/**
	 * How many ranges the two enchantment tables state, measured on 2026-09-11
	 * with a separate search of the two CSV files. The generator is held to the
	 * same number by STATED_RANGES in
	 * tools/tests/test_enchantment_effects_match_the_row_text.py, so the game
	 * and the generator cannot read the sentences differently and both pass.
	 */
	constexpr int32 StatedRanges = 390;

	/** Loads a generated table so tests read the real data, not a fixture. */
	template <typename RowType>
	UDataTable* LoadCsv(const TCHAR* FileName)
	{
		FString Contents;
		const FString Path = FPaths::ProjectDir() / TEXT("Data") / FileName;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			return nullptr;
		}

		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = RowType::StaticStruct();
		if (Table->CreateTableFromCSVString(Contents).Num() > 0)
		{
			return nullptr;
		}
		return Table;
	}

	/**
	 * An effect table with one ranged row for each of the two real rows above.
	 *
	 * A FIXTURE, BECAUSE THE REAL SHEET HOLDS NO RANGED ROW YET. The rows are
	 * written the way the generator requires: the sentence's two numbers in
	 * its order, with the row's sign.
	 */
	UDataTable* RangedEffects(FAutomationTestBase& Test)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmEnchantmentEffectRow::StaticStruct();
		const FString Csv =
			FString(TEXT("Name,Enchantment,Stat,ValueKind,ValueLow,ValueHigh,"
						 "RequiredTags,Condition,ConditionValue,Scale,ScaleStep\n"))
			+ FString::Printf(TEXT("%s#1,%s,block_chance,increased,10,20,,,0,,0\n"),
							  BlockBenefit, BlockBenefit)
			+ FString::Printf(TEXT("%s#1,%s,attack_speed,increased,-20,-35,,,0,,0\n"),
							  SlowerDrawback, SlowerDrawback);
		const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
		for (const FString& Problem : Problems)
		{
			Test.AddError(Problem);
		}
		return Problems.Num() == 0 ? Table : nullptr;
	}

	/** An item of a real base carrying one enchantment pair at these rolls. */
	FCataclysmItem Carrying(const TCHAR* Base, const TCHAR* Positive,
							float PositiveRoll, const TCHAR* Negative,
							float NegativeRoll)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);

		FCataclysmRolledEnchantment Rolled;
		Rolled.Positive = FName(Positive);
		Rolled.Negative = FName(Negative);
		Rolled.PositiveRoll = PositiveRoll;
		Rolled.NegativeRoll = NegativeRoll;
		Item.Enchantments.Add(Rolled);
		Item.EnchantmentCount = 1;
		return Item;
	}

	/** The one modifier the totals hold for a stat, or null with an error. */
	const FCataclysmStatModifier* OnlyModifier(FAutomationTestBase& Test,
											   const FTotals& Totals,
											   const TCHAR* Stat)
	{
		const TArray<FCataclysmStatModifier>* Modifiers =
			Totals.Find(FName(Stat));
		if (!Modifiers || Modifiers->Num() != 1)
		{
			Test.AddError(FString::Printf(
				TEXT("expected one modifier on %s, found %d"), Stat,
				Modifiers ? Modifiers->Num() : 0));
			return nullptr;
		}
		return &(*Modifiers)[0];
	}

	/**
	 * Whether a sentence still holds a number, a hyphen and a number.
	 *
	 * WRITTEN SEPARATELY FROM THE GAME'S READER ON PURPOSE. It knows nothing of
	 * commas or decimal points and only skips a percent sign, so a range the
	 * reader failed to replace is still visible to it.
	 */
	bool StillHoldsARange(const FString& Text)
	{
		for (int32 At = 0; At < Text.Len(); ++At)
		{
			if (!FChar::IsDigit(Text[At]))
			{
				continue;
			}
			int32 Cursor = At + 1;
			while (Cursor < Text.Len()
				   && (Text[Cursor] == TEXT('%') || Text[Cursor] == TEXT(' ')))
			{
				++Cursor;
			}
			if (Cursor < Text.Len() && Text[Cursor] == TEXT('-'))
			{
				++Cursor;
				while (Cursor < Text.Len() && Text[Cursor] == TEXT(' '))
				{
					++Cursor;
				}
				if (Cursor < Text.Len() && FChar::IsDigit(Text[Cursor]))
				{
					return true;
				}
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentRollEvenTest,
	"Cataclysm.Enchantments.EveryWrittenValueOfARangeIsEquallyLikely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentRollEvenTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentRollTest;

	// "1-4" IS FOUR VALUES, SO EACH QUARTER OF THE ROLL GIVES ONE OF THEM.
	// Rounding a continuous draw instead would give 1 and 4 a sixth of the
	// rolls each, and 2 and 3 a third each.
	struct FCase
	{
		float Roll;
		float Expected;
	};
	const FCase Cases[] = {
		{0.0f, 1.0f}, {0.24f, 1.0f}, {0.25f, 2.0f}, {0.49f, 2.0f},
		{0.5f, 3.0f}, {0.74f, 3.0f}, {0.75f, 4.0f}, {0.99f, 4.0f},
		{1.0f, 4.0f}};
	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("1-4 at a roll of %.2f"), Case.Roll),
				  FValues::EnchantmentValue(1.0f, 4.0f, Case.Roll), Case.Expected);
	}

	// "10%-30%" IS THE 21 WHOLE NUMBERS FROM 10 TO 30.
	TestEqual(TEXT("10-30 at 0"), FValues::EnchantmentValue(10.0f, 30.0f, 0.0f),
			  10.0f);
	TestEqual(TEXT("10-30 at 0.5 is the eleventh value"),
			  FValues::EnchantmentValue(10.0f, 30.0f, 0.5f), 20.0f);
	TestEqual(TEXT("10-30 at 1"), FValues::EnchantmentValue(10.0f, 30.0f, 1.0f),
			  30.0f);

	// A SENTENCE STATING ONE NUMBER GIVES IT WHATEVER THE ROLL.
	TestEqual(TEXT("100 at 0.3"),
			  FValues::EnchantmentValue(100.0f, 100.0f, 0.3f), 100.0f);

	// AND AN ENCHANTMENT NOBODY ROLLED STATES THE TOP OF ITS RANGE, which is the
	// default an affix's roll has too.
	const FCataclysmRolledEnchantment Unrolled;
	TestEqual(TEXT("an unrolled benefit sits at the top"), Unrolled.PositiveRoll,
			  1.0f);
	TestEqual(TEXT("an unrolled drawback sits at the top"),
			  Unrolled.NegativeRoll, 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentRollPrecisionTest,
	"Cataclysm.Enchantments.ARangeRollsAtThePrecisionItIsWrittenIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentRollPrecisionTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentRollTest;

	// "0.5-1" IS SIX TENTHS, AND HALF WAY IS THE FOURTH OF THEM.
	TestEqual(TEXT("0.5-1 at 0.5"), FValues::EnchantmentValue(0.5f, 1.0f, 0.5f),
			  0.8f, 1.0e-4f);
	TestEqual(TEXT("2.5-5 at 1"), FValues::EnchantmentValue(2.5f, 5.0f, 1.0f),
			  5.0f, 1.0e-4f);
	TestEqual(TEXT("100,000-500,000 at 0"),
			  FValues::EnchantmentValue(100000.0f, 500000.0f, 0.0f), 100000.0f);

	// A DRAWBACK'S RANGE ROLLS TOWARD ITS SECOND NUMBER. "Reduced by 30%-50%"
	// is -30 to -50, and the top roll is the larger reduction.
	TestEqual(TEXT("-30 to -50 at 0"),
			  FValues::EnchantmentValue(-30.0f, -50.0f, 0.0f), -30.0f);
	TestEqual(TEXT("-30 to -50 at 1"),
			  FValues::EnchantmentValue(-30.0f, -50.0f, 1.0f), -50.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentRollTextTest,
	"Cataclysm.Enchantments.TheHoverTextShowsTheNumberTheItemRolled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentRollTextTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentRollTest;

	struct FCase
	{
		const TCHAR* What;
		const TCHAR* Sentence;
		float Roll;
		const TCHAR* Expected;
	};
	const FCase Cases[] = {
		{TEXT("the top of a range"),
		 TEXT("Your block chance is increased by 10%-20%"), 1.0f,
		 TEXT("Your block chance is increased by 20%")},
		{TEXT("the bottom of a range"),
		 TEXT("Your block chance is increased by 10%-20%"), 0.0f,
		 TEXT("Your block chance is increased by 10%")},
		{TEXT("a percent sign on one end only"),
		 TEXT("You lose 1-4% max resistances"), 1.0f,
		 TEXT("You lose 4% max resistances")},
		{TEXT("thousands commas and a spaced hyphen"),
		 TEXT("for every 100,000 - 500,000 kills"), 0.0f,
		 TEXT("for every 100,000 kills")},
		{TEXT("a range of tenths"), TEXT("for 0.5-1 seconds"), 0.5f,
		 TEXT("for 0.8 seconds")},
		{TEXT("two ranges read one roll"),
		 TEXT("a 20%-35% slow for 2-4 seconds"), 1.0f,
		 TEXT("a 35% slow for 4 seconds")},
		{TEXT("a hyphen between a number and a word is left alone"),
		 TEXT("Archon's Aegis (2-Piece Bonus): Your block chance is increased "
			  "by 25%"), 0.0f,
		 TEXT("Archon's Aegis (2-Piece Bonus): Your block chance is increased "
			  "by 25%")},
		{TEXT("a full stop after a number is left alone"),
		 TEXT("You have 20% less hp."), 0.3f, TEXT("You have 20% less hp.")},
	};
	for (const FCase& Case : Cases)
	{
		TestEqual(Case.What,
				  FValues::EnchantmentTextAtRoll(Case.Sentence, Case.Roll),
				  FString(Case.Expected));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentRollTablesTest,
	"Cataclysm.Enchantments.EveryRangeTheTablesStateIsFoundAndReplaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentRollTablesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentRollTest;

	int32 Ranges = 0;
	TArray<FString> StillShowing;
	for (const TCHAR* File :
		 {TEXT("EnchantmentsPositive.csv"), TEXT("EnchantmentsNegative.csv")})
	{
		UDataTable* Table = LoadCsv<FCataclysmEnchantmentRow>(File);
		if (!TestNotNull(FString::Printf(TEXT("%s loads"), File), Table))
		{
			return false;
		}
		Table->ForeachRow<FCataclysmEnchantmentRow>(TEXT("EveryRange"),
			[&](const FName& Key, const FCataclysmEnchantmentRow& Row)
			{
				Ranges += FValues::EnchantmentRanges(Row.Effect).Num();
				const FString Shown =
					FValues::EnchantmentTextAtRoll(Row.Effect, 0.5f);
				if (StillHoldsARange(Shown))
				{
					StillShowing.Add(FString::Printf(TEXT("%s reads '%s'"),
						*Key.ToString(), *Shown));
				}
			});
	}

	TestEqual(TEXT("ranges the two tables state"), Ranges, StatedRanges);
	TestEqual(TEXT("sentences still showing a range once an item has rolled"),
			  StillShowing.Num(), 0);
	for (const FString& Each : StillShowing)
	{
		AddError(Each);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentRollWornTest,
	"Cataclysm.Enchantments.AWornRangeGrantsTheNumberItsHoverTextShows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentRollWornTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentRollTest;

	UDataTable* Positive =
		LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	UDataTable* Negative =
		LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
	UDataTable* Bases = LoadCsv<FCataclysmItemBaseRow>(TEXT("ItemBases.csv"));
	UDataTable* Affixes = LoadCsv<FCataclysmAffixRow>(TEXT("Affixes.csv"));
	UDataTable* Effects = RangedEffects(*this);
	if (!Positive || !Negative || !Bases || !Affixes || !Effects)
	{
		AddError(TEXT("A table could not be read from game/Data. Run python "
					  "tools/generate_datatables.py."));
		return false;
	}

	// A HELM WHOSE BENEFIT ROLLED HALF WAY AND WHOSE DRAWBACK A QUARTER. Block
	// chance 10 to 20 is eleven values, and half way is the sixth, 15. Attack
	// speed -20 to -35 is sixteen values, and a quarter is the fifth, -24.
	const FCataclysmItem Helm = Carrying(TEXT("Head_Helm"), BlockBenefit, 0.5f,
										 SlowerDrawback, 0.25f);
	FTotals Totals;
	UCataclysmItemModifiers::AccumulateEnchantmentsInto(
		Totals, {Helm}, Effects, Positive, Negative);

	const FCataclysmStatModifier* Block =
		OnlyModifier(*this, Totals, TEXT("block_chance"));
	const FCataclysmStatModifier* Speed =
		OnlyModifier(*this, Totals, TEXT("attack_speed"));
	if (!Block || !Speed)
	{
		return false;
	}
	TestEqual(TEXT("block chance got the rolled number"), Block->Value, 15.0f);
	TestEqual(TEXT("attack speed got the rolled number"), Speed->Value, -24.0f);

	// THE HOVER TEXT STATES THE SAME TWO NUMBERS.
	const TArray<FString> Lines = UCataclysmItemTooltip::EnchantmentLines(
		Helm.Enchantments[0], Positive, Negative);
	if (TestEqual(TEXT("two lines"), Lines.Num(), 2))
	{
		TestEqual(TEXT("the benefit line"), Lines[0],
				  FString(TEXT("Your block chance is increased by 15%")));
		TestEqual(TEXT("the drawback line"), Lines[1],
				  FString(UCataclysmItemTooltip::DrawbackPrefix)
					  + TEXT("Your attack speed is reduced by 24%"));
	}

	// AND UPGRADING THE HELM MOVES NEITHER, which is the second half of the
	// ruling.
	FCataclysmCarriedSlot AtZero;
	AtZero.Item = Helm;
	AtZero.Item.GearLevel = 0;
	FCataclysmCarriedSlot AtTen;
	AtTen.Item = Helm;
	AtTen.Item.GearLevel = 10;
	const FString Stated = TEXT("Your block chance is increased by 15%");
	TestTrue(TEXT("the +0 tool tip states 15%"),
			 UCataclysmItemTooltip::LinesFor(AtZero, Bases, Affixes, nullptr,
											 Positive, Negative)
				 .Contains(Stated));
	TestTrue(TEXT("the +10 tool tip states the same 15%"),
			 UCataclysmItemTooltip::LinesFor(AtTen, Bases, Affixes, nullptr,
											 Positive, Negative)
				 .Contains(Stated));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentRollDuplicateTest,
	"Cataclysm.Enchantments.ABenefitOnTwoPiecesIsGrantedOnceAtTheHigherRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentRollDuplicateTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentRollTest;

	UDataTable* Positive =
		LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	UDataTable* Negative =
		LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
	UDataTable* Effects = RangedEffects(*this);
	if (!Positive || !Negative || !Effects)
	{
		AddError(TEXT("A table could not be read from game/Data."));
		return false;
	}

	// BLOCK CHANCE 10 TO 20 ON TWO PIECES, ROLLED LOW AND HIGH. A roll of 0.1 is
	// 11 and a roll of 0.9 is 19. The benefit applies once, at 19, whichever
	// piece is listed first.
	const FCataclysmItem Low = Carrying(TEXT("Head_Helm"), BlockBenefit, 0.1f,
										DrawbackWithNoEffect, 1.0f);
	const FCataclysmItem High = Carrying(TEXT("Boots_Sabatons"), BlockBenefit,
										 0.9f, DrawbackWithNoEffect, 1.0f);
	const TArray<TArray<FCataclysmItem>> Orders = {{Low, High}, {High, Low}};
	for (const TArray<FCataclysmItem>& Worn : Orders)
	{
		FTotals Totals;
		UCataclysmItemModifiers::AccumulateEnchantmentsInto(
			Totals, Worn, Effects, Positive, Negative);
		if (const FCataclysmStatModifier* Block =
				OnlyModifier(*this, Totals, TEXT("block_chance")))
		{
			TestEqual(TEXT("once, at the higher roll"), Block->Value, 19.0f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentRollDropTest,
	"Cataclysm.Enchantments.ADroppedItemGivesEachHalfARollOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnchantmentRollDropTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnchantmentRollTest;

	UDataTable* Positive =
		LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
	UDataTable* Negative =
		LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
	if (!Positive || !Negative)
	{
		AddError(TEXT("An enchantment table could not be read from game/Data."));
		return false;
	}

	FRandomStream Stream(20260911);
	TSet<float> Seen;
	int32 Halves = 0;
	int32 OutOfRange = 0;
	for (int32 Drop = 0; Drop < 25; ++Drop)
	{
		TArray<FCataclysmRolledEnchantment> Rolled;
		if (!TestTrue(TEXT("four enchantments were drawn"),
					  UCataclysmDropRoll::RollEnchantments(
						  Positive, Negative, TEXT("Head"), 4, Stream, Rolled)))
		{
			return false;
		}
		for (const FCataclysmRolledEnchantment& Each : Rolled)
		{
			for (const float Roll : {Each.PositiveRoll, Each.NegativeRoll})
			{
				++Halves;
				Seen.Add(Roll);
				if (Roll < 0.0f || Roll >= 1.0f)
				{
					++OutOfRange;
				}
			}
		}
	}

	TestEqual(TEXT("two hundred halves were rolled"), Halves, 200);

	// BELOW 1, SO NOT ONE OF THEM WAS LEFT AT THE DEFAULT.
	TestEqual(TEXT("every roll is at least 0 and below 1"), OutOfRange, 0);

	// AND EACH HALF HAS ITS OWN, rather than one number shared by the item.
	TestTrue(FString::Printf(TEXT("%d different rolls among %d halves"),
							 Seen.Num(), Halves),
			 Seen.Num() >= 190);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
