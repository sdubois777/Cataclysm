// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

// For the leech row's alias test (#1618): the enchantment row and the loaded table.
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmEnchantmentRenames.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"

/**
 * The enchantment name alias. Issue #1799, the owner's decision of 2026-09-30.
 */
namespace CataclysmEnchantmentRenameTest
{
	/** A renamed row from the "critical hits" pass, and the row it means now. */
	const TCHAR* OldPositive = TEXT("Positive_Critical_hits_restore_2_4_of_your_maximum_HP");
	const TCHAR* NewPositive = TEXT("Positive_Critical_strikes_restore_2_4_of_your_maximum_H");
	const TCHAR* OldNegative = TEXT("Negative_Critical_hits_drain_3_6_of_your_current_HP");
	const TCHAR* NewNegative = TEXT("Negative_Critical_strikes_drain_3_6_of_your_current_HP");

	/** An item carrying one enchantment pair. */
	FCataclysmItem Carrying(const TCHAR* Positive, const TCHAR* Negative)
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		FCataclysmRolledEnchantment Rolled;
		Rolled.Positive = FName(Positive);
		Rolled.Negative = FName(Negative);
		Rolled.PositiveRoll = 0.25f;
		Item.Enchantments.Add(Rolled);
		Item.EnchantmentCount = 1;
		return Item;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSavedOldEnchantmentNameTest,
	"Cataclysm.SaveStorage.AnItemSavedUnderAnOldEnchantmentNameLoadsUnderItsNewRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character record whose worn helm carries two enchantments under their
 * names from before the "critical strikes" rename is written out and read back,
 * and the helm comes back carrying the current names, with its roll untouched.
 * Issue #1799.
 *
 * THROUGH THE WHOLE ROUND TRIP, `ToJson` then `FromJson`, rather than calling
 * the rename directly, because what matters is that the one door every record
 * comes in by applies it. A second helm carrying current names is the control:
 * nothing moves it.
 */
bool FCataclysmSavedOldEnchantmentNameTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentRenameTest;

	UCataclysmCharacterSave* Written = NewObject<UCataclysmCharacterSave>();
	Written->SchemaVersion = UCataclysmCharacterSave::SchemaVersionNow;
	FCataclysmWornItem Old;
	Old.Slot = ECataclysmGearSlot::Head;
	Old.Item = Carrying(OldPositive, OldNegative);
	FCataclysmWornItem Current;
	Current.Slot = ECataclysmGearSlot::Chest;
	Current.Item = Carrying(NewPositive, NewNegative);
	Written->WornGear = {Old, Current};

	FString Json;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("the record writes out: %s"), *Error),
				  FCataclysmSaveStorage::ToJson(Written, Json, Error)))
	{
		return false;
	}
	TestTrue(TEXT("the written text still holds the old name"), Json.Contains(OldPositive));

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(
		FCataclysmSaveStorage::FromJson(Json, UCataclysmCharacterSave::StaticClass(),
										nullptr, Result, Message));
	if (!TestNotNull(FString::Printf(TEXT("the record reads back: %s"), *Message), Read)
		|| !TestEqual(TEXT("two worn items"), Read->WornGear.Num(), 2)
		|| !TestEqual(TEXT("one enchantment on the old helm"),
					  Read->WornGear[0].Item.Enchantments.Num(), 1))
	{
		return false;
	}
	const FCataclysmRolledEnchantment& Loaded = Read->WornGear[0].Item.Enchantments[0];
	TestEqual(TEXT("the old positive name loads as its current row"),
			  Loaded.Positive, FName(NewPositive));
	TestEqual(TEXT("and the old negative name too"), Loaded.Negative, FName(NewNegative));
	TestEqual(TEXT("the roll is carried as it was"), Loaded.PositiveRoll, 0.25f, 0.0001f);
	TestEqual(TEXT("a helm already on current names is left as it was"),
			  Read->WornGear[1].Item.Enchantments[0].Positive, FName(NewPositive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnchantmentRenameWalkTest,
	"Cataclysm.SaveStorage.TheRenameWalkChangesOnlyRenamedNamesAndCountsThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The walk over a bare item: a renamed name moves and is counted, a current
 * name and an unknown one stay as they are, and a second walk finds nothing
 * left to move. Issue #1799.
 */
bool FCataclysmEnchantmentRenameWalkTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentRenameTest;

	FCataclysmItem Item = Carrying(OldPositive, TEXT("Negative_A_name_no_row_ever_had"));
	Item.Enchantments.Add(Carrying(NewPositive, NewNegative).Enchantments[0]);

	TestEqual(TEXT("one renamed name moves"),
			  FCataclysmEnchantmentRenames::RenameInStruct(FCataclysmItem::StaticStruct(), &Item), 1);
	TestEqual(TEXT("to its current row"), Item.Enchantments[0].Positive, FName(NewPositive));
	TestEqual(TEXT("an unknown name is left as it was"),
			  Item.Enchantments[0].Negative, FName(TEXT("Negative_A_name_no_row_ever_had")));
	TestEqual(TEXT("current names are left as they were"),
			  Item.Enchantments[1].Negative, FName(NewNegative));
	TestEqual(TEXT("and a second walk finds nothing to move"),
			  FCataclysmEnchantmentRenames::RenameInStruct(FCataclysmItem::StaticStruct(), &Item), 0);
	return true;
}

// THE LEECH ROW'S REWORD, 2026-10-28 (#1618): "50% less tick rate for your leech
// effects" became the owner's "Your leech pays out 50% more slowly", which
// renamed its row. The two tests above check the alias on the "critical
// strikes" pair only, and the Python checks read the table's text; this one
// carries the leech row's old name through a save, and asks the table the game
// compiled whether every old name means a current row.
namespace CataclysmEnchantmentAliasTest
{
	const TCHAR* OldLeech = TEXT("Negative_50_less_tick_rate_for_your_leech_effects");
	const TCHAR* NewLeech = TEXT("Negative_Your_leech_pays_out_50_more_slowly");

	/** Loads a generated enchantment table, as the roll tests do, so the real rows are read. */
	UDataTable* LoadEnchantmentCsv(const TCHAR* FileName)
	{
		FString Contents;
		const FString Path = FPaths::ProjectDir() / TEXT("Data") / FileName;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmEnchantmentRow::StaticStruct();
		if (Table->CreateTableFromCSVString(Contents).Num() > 0)
		{
			return nullptr;
		}
		return Table;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryEnchantmentAliasResolvesTest,
	"Cataclysm.EnchantmentAliases.AHelmSavedUnderTheLeechRowsOldNameLoadsAsItsNewRowAndEveryOldNameMeansACurrentRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character record whose worn helm carries the leech row under its name from
 * before #1618 is written out and read back, and the helm comes back carrying
 * the row's current name. A second helm already on the current name is the
 * control, read first: nothing moves it.
 *
 * THEN EVERY PAIR IN `Aliases()`, the table the save loader applies: its target
 * is a row of the generated table of its kind, its old name is not, and the
 * target is not itself an old name, so one step reaches a row.
 */
bool FCataclysmEveryEnchantmentAliasResolvesTest::RunTest(const FString&)
{
	using namespace CataclysmEnchantmentAliasTest;

	const UDataTable* Positive = LoadEnchantmentCsv(TEXT("EnchantmentsPositive.csv"));
	const UDataTable* Negative = LoadEnchantmentCsv(TEXT("EnchantmentsNegative.csv"));
	if (!TestNotNull(TEXT("set-up: EnchantmentsPositive.csv can be read"), Positive)
		|| !TestNotNull(TEXT("set-up: EnchantmentsNegative.csv can be read"), Negative)
		|| !TestTrue(TEXT("set-up: the leech row's current name is a row of EnchantmentsNegative.csv"),
					 Negative->GetRowMap().Contains(FName(NewLeech)))
		|| !TestFalse(TEXT("set-up: its old name is not"), Negative->GetRowMap().Contains(FName(OldLeech))))
	{
		return false;
	}

	UCataclysmCharacterSave* Written = NewObject<UCataclysmCharacterSave>();
	Written->SchemaVersion = UCataclysmCharacterSave::SchemaVersionNow;
	FCataclysmWornItem Old;
	Old.Slot = ECataclysmGearSlot::Head;
	Old.Item = CataclysmEnchantmentRenameTest::Carrying(CataclysmEnchantmentRenameTest::NewPositive, OldLeech);
	FCataclysmWornItem Current;
	Current.Slot = ECataclysmGearSlot::Chest;
	Current.Item = CataclysmEnchantmentRenameTest::Carrying(CataclysmEnchantmentRenameTest::NewPositive, NewLeech);
	Written->WornGear = {Old, Current};

	FString Json;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("set-up: the record writes out: %s"), *Error),
				  FCataclysmSaveStorage::ToJson(Written, Json, Error))
		|| !TestTrue(TEXT("set-up: the written text holds the leech row's old name"), Json.Contains(OldLeech)))
	{
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(
		FCataclysmSaveStorage::FromJson(Json, UCataclysmCharacterSave::StaticClass(),
										nullptr, Result, Message));
	if (!TestNotNull(FString::Printf(TEXT("set-up: the record reads back: %s"), *Message), Read)
		|| !TestEqual(TEXT("set-up: two worn items"), Read->WornGear.Num(), 2)
		|| !TestEqual(TEXT("set-up: one enchantment on the old helm"), Read->WornGear[0].Item.Enchantments.Num(), 1)
		|| !TestEqual(TEXT("set-up: one enchantment on the control's helm"),
					  Read->WornGear[1].Item.Enchantments.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("control: a helm already on the leech row's current name is left as it was"),
			  Read->WornGear[1].Item.Enchantments[0].Negative, FName(NewLeech));
	TestEqual(TEXT("the leech row's old name loads as its current row"),
			  Read->WornGear[0].Item.Enchantments[0].Negative, FName(NewLeech));
	TestEqual(TEXT("and the benefit beside it, a current name, is left as it was"),
			  Read->WornGear[0].Item.Enchantments[0].Positive, FName(CataclysmEnchantmentRenameTest::NewPositive));

	const TMap<FName, FName>& Aliases = FCataclysmEnchantmentRenames::Aliases();
	if (!TestTrue(TEXT("set-up: the alias table holds pairs"), Aliases.Num() > 0))
	{
		return false;
	}
	TArray<FString> NotARow;
	TArray<FString> StillARow;
	TArray<FString> AChain;
	for (const TPair<FName, FName>& Pair : Aliases)
	{
		const UDataTable* Table = Pair.Value.ToString().StartsWith(TEXT("Positive_")) ? Positive : Negative;
		if (!Table->GetRowMap().Contains(Pair.Value))
		{
			NotARow.Add(Pair.Key.ToString());
		}
		if (Positive->GetRowMap().Contains(Pair.Key) || Negative->GetRowMap().Contains(Pair.Key))
		{
			StillARow.Add(Pair.Key.ToString());
		}
		if (Aliases.Contains(Pair.Value))
		{
			AChain.Add(Pair.Key.ToString());
		}
	}
	TestTrue(FString::Printf(TEXT("every old name's target is a row of its kind's table; not: %s"),
							 *FString::Join(NotARow, TEXT(", "))),
			 NotARow.Num() == 0);
	TestTrue(FString::Printf(TEXT("no old name is still a current row; these are: %s"),
							 *FString::Join(StillARow, TEXT(", "))),
			 StillARow.Num() == 0);
	TestTrue(FString::Printf(TEXT("no target is itself an old name, so one step reaches a row; chains: %s"),
							 *FString::Join(AChain, TEXT(", "))),
			 AChain.Num() == 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
