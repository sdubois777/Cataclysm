// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Items/CataclysmEnchantmentRenames.h"
#include "Items/CataclysmItem.h"
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

#endif // WITH_AUTOMATION_TESTS
