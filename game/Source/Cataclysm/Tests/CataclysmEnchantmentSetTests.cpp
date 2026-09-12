// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * Tests for a named set turning on by how many worn pieces carry it. Issue #45.
 *
 * WHAT A SET IS, ruled by the project owner on 2026-09-08: an enchantment
 * rather than an item. An item that rolls Archon's Aegis becomes a piece of it,
 * and that set's 2-piece, 6-piece and 10-piece bonuses turn on by how many
 * equipped items carry it. The set's drawback applies once for the whole set,
 * joining at the first bonus's threshold, so one piece does nothing and costs
 * nothing.
 *
 * MADE-UP TABLES, BECAUSE THE REAL ONES CANNOT REACH THE HIGHER THRESHOLDS.
 * Four of the fourteen sets have rows written, and only their 2-piece rows: the
 * 6-piece and 10-piece rows say things the game cannot do yet. The thresholds
 * are the mechanism this file is about, so it builds three sets of its own --
 * one with all three thresholds, one with only a 2-piece row, and one with no
 * drawback at all -- and one ordinary enchantment as a control.
 *
 * EVERY MADE-UP ROW GRANTS A DIFFERENT STAT, so a test can say which row was
 * granted rather than only how many were.
 *
 * THE LAST TEST READS THE REAL FILES: two worn pieces of Archon's Aegis against
 * what its two written rows say.
 */
namespace CataclysmEnchantmentSetTest
{
	using FModifiers = UCataclysmItemModifiers;
	using FTotals = TMap<FName, TArray<FCataclysmStatModifier>>;

	/** Made-up rows. Set A is 7, set B is 8, and set C, with no drawback, is 9. */
	const TCHAR* SetATwo = TEXT("Set_A_Two");
	const TCHAR* SetASix = TEXT("Set_A_Six");
	const TCHAR* SetATen = TEXT("Set_A_Ten");
	const TCHAR* SetADrawback = TEXT("Set_A_Drawback");
	const TCHAR* SetBTwo = TEXT("Set_B_Two");
	const TCHAR* SetBDrawback = TEXT("Set_B_Drawback");
	const TCHAR* SetCTwo = TEXT("Set_C_Two");
	const TCHAR* Ordinary = TEXT("Ordinary");
	const TCHAR* OrdinaryDrawback = TEXT("Ordinary_Drawback");

	/** Real row names, for the one test that reads the real tables. */
	const TCHAR* ArchonMarker =
		TEXT("Positive_Archon_s_Aegis_2_Piece_Bonus_Your_block_chanc");
	const TCHAR* ArchonDrawback =
		TEXT("Negative_Your_movement_speed_is_reduced_by_10");

	/**
	 * The benefits: three thresholds on set A, one on set B, one on set C, and
	 * one ordinary row. A set row states its threshold in its own words, which
	 * is where `UCataclysmDropRoll` reads it from.
	 */
	const TCHAR* PositiveCsv =
		TEXT("Name,Effect,EnchantmentType,Weight,Tags,IsNegative\n")
		TEXT("Set_A_Two,Test Set A (2-Piece Bonus): armour,Set,7,,False\n")
		TEXT("Set_A_Six,Test Set A (6-Piece Bonus): critical chance,Set,7,,False\n")
		TEXT("Set_A_Ten,Test Set A (10-Piece Bonus): evasion,Set,7,,False\n")
		TEXT("Set_B_Two,Test Set B (2-Piece Bonus): health,Set,8,,False\n")
		TEXT("Set_C_Two,Test Set C (2-Piece Bonus): block,Set,9,,False\n")
		TEXT("Ordinary,An ordinary benefit,Generic,1,,False\n");

	/** The costs. Set C has none, which is what makes it grant nothing. */
	const TCHAR* NegativeCsv =
		TEXT("Name,Effect,EnchantmentType,Weight,Tags,IsNegative\n")
		TEXT("Set_A_Drawback,Test Set A costs movement speed,Set,7,,True\n")
		TEXT("Set_B_Drawback,Test Set B costs mana,Set,8,,True\n")
		TEXT("Ordinary_Drawback,An ordinary drawback,Generic,3,,True\n");

	/** What each made-up row grants. One stat each, so a test can tell them apart. */
	const TCHAR* EffectCsv =
		TEXT("Name,Enchantment,Stat,ValueKind,ValueLow,ValueHigh,RequiredTags,")
		TEXT("Condition,ConditionValue,Scale,ScaleStep\n")
		TEXT("Set_A_Two#1,Set_A_Two,armor,increased,10,10,,,0,,0\n")
		TEXT("Set_A_Six#1,Set_A_Six,crit_chance,increased,20,20,,,0,,0\n")
		TEXT("Set_A_Ten#1,Set_A_Ten,evasion,increased,30,30,,,0,,0\n")
		TEXT("Set_A_Drawback#1,Set_A_Drawback,movement_speed,increased,-10,-10,,,0,,0\n")
		TEXT("Set_B_Two#1,Set_B_Two,max_health,increased,10,10,,,0,,0\n")
		TEXT("Set_B_Drawback#1,Set_B_Drawback,max_mana,increased,-10,-10,,,0,,0\n")
		TEXT("Set_C_Two#1,Set_C_Two,block_chance,increased,10,10,,,0,,0\n")
		TEXT("Ordinary#1,Ordinary,max_energy_shield,more,100,100,,,0,,0\n")
		TEXT("Ordinary_Drawback#1,Ordinary_Drawback,attack_speed,increased,-5,-5,,,0,,0\n");

	struct FTables
	{
		UDataTable* Effects = nullptr;
		UDataTable* Positive = nullptr;
		UDataTable* Negative = nullptr;
	};

	/** A table of one row type, built from CSV text, or null if it did not read. */
	template <typename RowType>
	UDataTable* TableFrom(const FString& Csv)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = RowType::StaticStruct();
		return Table->CreateTableFromCSVString(Csv).Num() == 0 ? Table : nullptr;
	}

	/** The three made-up tables, or false with an error naming the one that failed. */
	bool MadeUpTables(FAutomationTestBase& Test, FTables& Out)
	{
		Out.Positive = TableFrom<FCataclysmEnchantmentRow>(PositiveCsv);
		Out.Negative = TableFrom<FCataclysmEnchantmentRow>(NegativeCsv);
		Out.Effects = TableFrom<FCataclysmEnchantmentEffectRow>(EffectCsv);
		if (!Out.Positive || !Out.Negative || !Out.Effects)
		{
			Test.AddError(TEXT("A made-up table in this file did not read. Its "
							   "columns must match the generated CSV's."));
			return false;
		}
		return true;
	}

	/** Loads a generated table so the last test reads the real data. */
	template <typename RowType>
	UDataTable* LoadCsv(const TCHAR* FileName)
	{
		FString Contents;
		const FString Path = FPaths::ProjectDir() / TEXT("Data") / FileName;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			return nullptr;
		}
		return TableFrom<RowType>(Contents);
	}

	/** The three real tables, or false with an error. */
	bool RealTables(FAutomationTestBase& Test, FTables& Out)
	{
		Out.Effects =
			LoadCsv<FCataclysmEnchantmentEffectRow>(TEXT("EnchantmentEffects.csv"));
		Out.Positive =
			LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
		Out.Negative =
			LoadCsv<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
		if (!Out.Effects || !Out.Positive || !Out.Negative)
		{
			Test.AddError(TEXT("An enchantment table could not be read from "
							   "game/Data. Run python tools/generate_datatables.py."));
			return false;
		}
		return true;
	}

	/**
	 * One worn item carrying one enchantment pair.
	 *
	 * THE BASE ONLY HAS TO BE A NAME. `AccumulateEnchantmentsInto` reads no item
	 * base table; it skips an item whose base is none, and nothing else about
	 * the base reaches it. That is what lets a test wear ten pieces.
	 */
	FCataclysmItem Piece(int32 Index, const TCHAR* Positive, const TCHAR* Negative)
	{
		FCataclysmItem Item;
		Item.Base = FName(*FString::Printf(TEXT("Piece_%d"), Index));

		FCataclysmRolledEnchantment Rolled;
		Rolled.Positive = FName(Positive);
		Rolled.Negative = FName(Negative);
		Item.Enchantments.Add(Rolled);
		Item.EnchantmentCount = 1;
		return Item;
	}

	/** `Count` worn pieces, each carrying the same pair. */
	TArray<FCataclysmItem> Pieces(int32 Count, const TCHAR* Positive,
								  const TCHAR* Negative)
	{
		TArray<FCataclysmItem> Worn;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Worn.Add(Piece(Index, Positive, Negative));
		}
		return Worn;
	}

	FTotals Gather(const FTables& Tables, const TArray<FCataclysmItem>& Worn,
				   int32& OutAdded)
	{
		FTotals Totals;
		OutAdded = FModifiers::AccumulateEnchantmentsInto(
			Totals, Worn, Tables.Effects, Tables.Positive, Tables.Negative);
		return Totals;
	}

	/** How many modifiers one stat was given. */
	int32 CountOn(const FTotals& Totals, const TCHAR* Stat)
	{
		const TArray<FCataclysmStatModifier>* Found = Totals.Find(FName(Stat));
		return Found ? Found->Num() : 0;
	}

	/** The value of one stat's only modifier, or 0 when it has none. */
	float OnlyValueOn(const FTotals& Totals, const TCHAR* Stat)
	{
		const TArray<FCataclysmStatModifier>* Found = Totals.Find(FName(Stat));
		return Found && Found->Num() == 1 ? (*Found)[0].Value : 0.0f;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmSetTwoPiecesTest,
	"Cataclysm.EnchantmentSets.TwoPiecesGrantTheFirstBonusAndTheDrawbackOnce")
{
	using namespace CataclysmEnchantmentSetTest;

	FTables Tables;
	if (!MadeUpTables(*this, Tables))
	{
		return false;
	}

	// TWO PIECES ARE THE FIRST THRESHOLD, and the drawback joins there. The
	// owner's words on 2026-09-08: one piece of a set does nothing and costs
	// nothing, and the second turns the first bonus and the cost on together.
	int32 Added = 0;
	const FTotals Totals =
		Gather(Tables, Pieces(2, SetATwo, SetADrawback), Added);

	TestEqual(TEXT("the two-piece bonus once"), CountOn(Totals, TEXT("armor")), 1);
	TestEqual(TEXT("worth what its row says"),
			  OnlyValueOn(Totals, TEXT("armor")), 10.0f);
	TestEqual(TEXT("the drawback once"),
			  CountOn(Totals, TEXT("movement_speed")), 1);
	TestEqual(TEXT("worth what its row says"),
			  OnlyValueOn(Totals, TEXT("movement_speed")), -10.0f);

	TestEqual(TEXT("and not the six-piece bonus"),
			  CountOn(Totals, TEXT("crit_chance")), 0);
	TestEqual(TEXT("nor the ten-piece bonus"),
			  CountOn(Totals, TEXT("evasion")), 0);
	TestEqual(TEXT("two modifiers in all"), Added, 2);

	return true;
}

CATACLYSM_TEST(FCataclysmSetThresholdsTest,
	"Cataclysm.EnchantmentSets.EachThresholdAddsItsBonusToTheOnesBelowIt")
{
	using namespace CataclysmEnchantmentSetTest;

	FTables Tables;
	if (!MadeUpTables(*this, Tables))
	{
		return false;
	}

	// A HIGHER THRESHOLD ADDS TO THE LOWER ONES RATHER THAN REPLACING THEM.
	// That is a labelled judgement of 2026-09-11 in `docs/DECISIONS.md`: no
	// design text says a lower bonus turns off, and Diablo III writes its
	// 6-piece bonuses to work with what the lower ones grant.
	struct FExpected
	{
		int32 Pieces;
		int32 Armor;
		int32 Crit;
		int32 Evasion;
		int32 Speed;
	};
	static const FExpected Expected[] = {
		{1, 0, 0, 0, 0},
		{2, 1, 0, 0, 1},
		{5, 1, 0, 0, 1},
		{6, 1, 1, 0, 1},
		{9, 1, 1, 0, 1},
		{10, 1, 1, 1, 1},
		{11, 1, 1, 1, 1},
	};

	for (const FExpected& Case : Expected)
	{
		int32 Added = 0;
		const FTotals Totals =
			Gather(Tables, Pieces(Case.Pieces, SetATwo, SetADrawback), Added);

		TestEqual(FString::Printf(TEXT("%d pieces: the two-piece bonus"),
								  Case.Pieces),
				  CountOn(Totals, TEXT("armor")), Case.Armor);
		TestEqual(FString::Printf(TEXT("%d pieces: the six-piece bonus"),
								  Case.Pieces),
				  CountOn(Totals, TEXT("crit_chance")), Case.Crit);
		TestEqual(FString::Printf(TEXT("%d pieces: the ten-piece bonus"),
								  Case.Pieces),
				  CountOn(Totals, TEXT("evasion")), Case.Evasion);
		TestEqual(FString::Printf(TEXT("%d pieces: the drawback"), Case.Pieces),
				  CountOn(Totals, TEXT("movement_speed")), Case.Speed);
		TestEqual(FString::Printf(TEXT("%d pieces: modifiers in all"),
								  Case.Pieces),
				  Added, Case.Armor + Case.Crit + Case.Evasion + Case.Speed);
	}

	return true;
}

CATACLYSM_TEST(FCataclysmSetDrawbackOnceTest,
	"Cataclysm.EnchantmentSets.TheDrawbackAppliesOnceHoweverManyPiecesAreWorn")
{
	using namespace CataclysmEnchantmentSetTest;

	FTables Tables;
	if (!MadeUpTables(*this, Tables))
	{
		return false;
	}

	// THE OWNER'S WORDS, 2026-09-08: "The set downsides are applied once, for
	// the entire set. It doesn't stack."
	int32 Added = 0;
	const FTotals FromSet =
		Gather(Tables, Pieces(10, SetATwo, SetADrawback), Added);

	TestEqual(TEXT("ten pieces, one drawback"),
			  CountOn(FromSet, TEXT("movement_speed")), 1);
	TestEqual(TEXT("at its own value, not ten times it"),
			  OnlyValueOn(FromSet, TEXT("movement_speed")), -10.0f);

	// THE CONTROL, AND THE RULE IT IS DIFFERENT FROM. An ordinary drawback
	// applies for every piece carrying it, which is what makes the line above a
	// statement about sets rather than about this function.
	int32 OrdinaryAdded = 0;
	const FTotals FromOrdinary =
		Gather(Tables, Pieces(10, Ordinary, OrdinaryDrawback), OrdinaryAdded);

	TestEqual(TEXT("ten ordinary pieces, ten drawbacks"),
			  CountOn(FromOrdinary, TEXT("attack_speed")), 10);
	TestEqual(TEXT("and their benefit once"),
			  CountOn(FromOrdinary, TEXT("max_energy_shield")), 1);

	return true;
}

CATACLYSM_TEST(FCataclysmSetTwoSetsOnOneItemTest,
	"Cataclysm.EnchantmentSets.AnItemCarryingTwoSetsIsAPieceOfEach")
{
	using namespace CataclysmEnchantmentSetTest;

	FTables Tables;
	if (!MadeUpTables(*this, Tables))
	{
		return false;
	}

	// AN ITEM HAS UP TO FOUR ENCHANTMENT SLOTS, and the drop lets different sets
	// take more than one of them. Two such items are two pieces of both sets.
	TArray<FCataclysmItem> Worn;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FCataclysmItem Item = Piece(Index, SetATwo, SetADrawback);

		FCataclysmRolledEnchantment Second;
		Second.Positive = FName(SetBTwo);
		Second.Negative = FName(SetBDrawback);
		Item.Enchantments.Add(Second);
		Item.EnchantmentCount = 2;

		Worn.Add(Item);
	}

	int32 Added = 0;
	const FTotals Totals = Gather(Tables, Worn, Added);

	TestEqual(TEXT("set A's two-piece bonus"), CountOn(Totals, TEXT("armor")), 1);
	TestEqual(TEXT("set A's drawback"),
			  CountOn(Totals, TEXT("movement_speed")), 1);
	TestEqual(TEXT("set B's two-piece bonus"),
			  CountOn(Totals, TEXT("max_health")), 1);
	TestEqual(TEXT("set B's drawback"), CountOn(Totals, TEXT("max_mana")), 1);
	TestEqual(TEXT("four modifiers in all"), Added, 4);

	return true;
}

CATACLYSM_TEST(FCataclysmSetDifferentSetsTest,
	"Cataclysm.EnchantmentSets.PiecesOfDifferentSetsDoNotAddUp")
{
	using namespace CataclysmEnchantmentSetTest;

	FTables Tables;
	if (!MadeUpTables(*this, Tables))
	{
		return false;
	}

	// ONE PIECE OF EACH OF TWO SETS IS NOT TWO PIECES OF EITHER. A count that
	// ignored which set a piece belongs to would turn both sets on here.
	int32 Added = 0;
	const FTotals Totals = Gather(
		Tables,
		{Piece(0, SetATwo, SetADrawback), Piece(1, SetBTwo, SetBDrawback)},
		Added);

	TestEqual(TEXT("neither set turns on"), Added, 0);
	TestEqual(TEXT("and no stat is touched"), Totals.Num(), 0);

	return true;
}

CATACLYSM_TEST(FCataclysmSetWithoutADrawbackTest,
	"Cataclysm.EnchantmentSets.ASetWithNoDrawbackGrantsNothing")
{
	using namespace CataclysmEnchantmentSetTest;

	FTables Tables;
	if (!MadeUpTables(*this, Tables))
	{
		return false;
	}

	// NEVER A BONUS WITHOUT ITS COST. A set's drawback is guaranteed rather than
	// drawn, so a set with no drawback row written cannot be paid for, and the
	// draw already refuses to offer one. Set C has none. The log says so once
	// per run; a warning does not fail an automation test.
	int32 Added = 0;
	const FTotals Totals =
		Gather(Tables, Pieces(2, SetCTwo, OrdinaryDrawback), Added);

	TestEqual(TEXT("the set's bonus is not granted"),
			  CountOn(Totals, TEXT("block_chance")), 0);

	// THE CONTROL: the ordinary drawback these two pieces also carry does apply,
	// so the zero above is the missing cost rather than a table nothing read.
	TestEqual(TEXT("while the ordinary drawback on both pieces does"),
			  CountOn(Totals, TEXT("attack_speed")), 2);

	return true;
}

CATACLYSM_TEST(FCataclysmSetRealArchonsAegisTest,
	"Cataclysm.EnchantmentSets.TwoArchonsAegisPiecesRaiseBlockAndLowerMovement")
{
	using namespace CataclysmEnchantmentSetTest;

	FTables Tables;
	if (!RealTables(*this, Tables))
	{
		return false;
	}

	// THE REAL ROWS: "Archon's Aegis (2-Piece Bonus): Your block chance is
	// increased by 25%" and the set's drawback, "Your movement speed is reduced
	// by 10%". This is the end-to-end reading of what the sheet says.
	int32 Added = 0;
	const FTotals Totals =
		Gather(Tables, Pieces(2, ArchonMarker, ArchonDrawback), Added);

	TestEqual(TEXT("block chance is raised once"),
			  CountOn(Totals, TEXT("block_chance")), 1);
	TestEqual(TEXT("by 25, as its words say"),
			  OnlyValueOn(Totals, TEXT("block_chance")), 25.0f);
	TestEqual(TEXT("movement speed is lowered once"),
			  CountOn(Totals, TEXT("movement_speed")), 1);
	TestEqual(TEXT("by 10, as its words say"),
			  OnlyValueOn(Totals, TEXT("movement_speed")), -10.0f);
	TestEqual(TEXT("two modifiers in all"), Added, 2);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
