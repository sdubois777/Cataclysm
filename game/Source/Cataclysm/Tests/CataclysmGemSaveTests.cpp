// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "UObject/Package.h"

/**
 * The gems window's save steps, issue #46: the character record at version 13 (slices G2 and G3) and the run record at
 * version 14 (slices G3 and G5). An item's `Sockets` was a count and is a list of typed sockets; the coordinating
 * session ruled on 2026-11-21 that "sockets migrated from today's count fit any type", so a count of N becomes N empty
 * sockets of type Any. A carried or stashed slot and a ground item hold no gem after the step.
 */

namespace CataclysmGemSaveTest
{
	UCataclysmSaveRecord* ReadRecordText(FAutomationTestBase& Test, const FString& Text, UClass* Class,
										 const TCHAR* Name)
	{
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		FString Message;
		UCataclysmSaveRecord* Read = Cast<UCataclysmSaveRecord>(
			FCataclysmSaveStorage::FromJson(Text, Class, GetTransientPackage(), Result, Message));
		if (Read == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s would not load: %s -- %s"), Name,
										  FCataclysmSaveStorage::Describe(Result), *Message));
		}
		return Read;
	}

	/** How many of the item's sockets are empty and fit any type. */
	int32 EmptyAnySockets(const FCataclysmItem& Item)
	{
		int32 Count = 0;
		for (const FCataclysmSocket& Socket : Item.Sockets)
		{
			Count += Socket.Type == ECataclysmSocketType::Any && Socket.Gem.IsNone() ? 1 : 0;
		}
		return Count;
	}

	/** The socket types in order, as their display names joined by commas, for the printed line. */
	FString TypesOf(const FCataclysmItem& Item)
	{
		const UEnum* Types = StaticEnum<ECataclysmSocketType>();
		FString Out;
		for (const FCataclysmSocket& Socket : Item.Sockets)
		{
			Out += (Out.IsEmpty() ? TEXT("") : TEXT(",")) + Types->GetNameStringByValue(static_cast<int64>(Socket.Type));
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemSaveCharacterMigrationTest,
	"Cataclysm.GemSave.ACharacterFileFromVersion12HasEachSocketCountAsThatManyEmptySocketsOfAnyType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Character_v12.json`, written before version 13, migrates with its carried piece's 2 sockets, its worn greatsword's 3
 * and its held build's greatsword's 3 as that many empty sockets of type Any, no gem in either carried slot, and every
 * other field kept. CONTROL: the committed character file, `Character_v14.json` since issue #2741 slice 9, reads its carried piece's sockets as
 * Attack holding Of The Viper at Quality, then Defense, and its worn greatsword's as Attack, Defense and Utility.
 * Prints "GEM CHARACTER MIGRATED".
 */
bool FCataclysmGemSaveCharacterMigrationTest::RunTest(const FString&)
{
	using namespace CataclysmGemSaveTest;

	FString Text;
	FString CurrentText;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Character_v12.json"), Text, Reason)
		|| !CataclysmSaveFixtures::Read(TEXT("Character_v14.json"), CurrentText, Reason))
	{
		AddError(Reason);
		return false;
	}
	const UCataclysmCharacterSave* Migrated = Cast<UCataclysmCharacterSave>(
		ReadRecordText(*this, Text, UCataclysmCharacterSave::StaticClass(), TEXT("Character_v12.json")));
	const UCataclysmCharacterSave* Current = Cast<UCataclysmCharacterSave>(
		ReadRecordText(*this, CurrentText, UCataclysmCharacterSave::StaticClass(), TEXT("Character_v14.json")));
	if (!Migrated || !Current
		|| !TestEqual(TEXT("set-up: Character_v12.json carries two slots"), Migrated->CarriedSlots.Num(), 2)
		|| !TestEqual(TEXT("set-up: and wears two items"), Migrated->WornGear.Num(), 2)
		|| !TestEqual(TEXT("set-up: and holds a build wearing one"), Migrated->ConsumptionBuild.Worn.Num(), 1)
		|| !TestEqual(TEXT("set-up: Character_v14.json carries two slots"), Current->CarriedSlots.Num(), 2)
		|| !TestEqual(TEXT("set-up: and wears two items"), Current->WornGear.Num(), 2))
	{
		return false;
	}

	// EVERY OTHER FIELD KEPT: the version 12 file at the current version with what the step writes, compared with what
	// the record writes back.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmCharacterSave::SchemaVersionNow);
	CataclysmSaveFixtures::AddTheCharactersVersion13Defaults(*Expected);
	CataclysmSaveFixtures::AddTheCharactersVersion14Defaults(*Expected); // And issue #2741 slice 9's step.
	FString Rewritten;
	FString Error;
	FString Where;
	const bool bSame = FCataclysmSaveStorage::ToJson(Migrated, Rewritten, Error)
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	const FCataclysmItem& Carried = Migrated->CarriedSlots[0].Item;
	const FCataclysmItem& Worn = Migrated->WornGear[0].Item;
	const FCataclysmItem& Held = Migrated->ConsumptionBuild.Worn[0].Item;
	int32 GemSlots = 0;
	for (const FCataclysmCarriedSlot& Slot : Migrated->CarriedSlots)
	{
		GemSlots += Slot.Gem.IsNone() ? 0 : 1;
	}
	const FCataclysmItem& CurrentCarried = Current->CarriedSlots[0].Item;
	const FCataclysmItem& CurrentWorn = Current->WornGear[0].Item;

	AddInfo(FString::Printf(TEXT("GEM CHARACTER MIGRATED: version %d; carried %d socket(s) (%s), worn %d (%s), held %d "
								 "(%s); %d slot(s) holding a gem; the rest %s; Character_v14.json carried %s with %s at "
								 "rarity %d, worn %s"),
							Migrated->SchemaVersion, Carried.Sockets.Num(), *TypesOf(Carried), Worn.Sockets.Num(),
							*TypesOf(Worn), Held.Sockets.Num(), *TypesOf(Held), GemSlots, bSame ? TEXT("kept") : *Where,
							*TypesOf(CurrentCarried),
							CurrentCarried.Sockets.IsEmpty() ? TEXT("") : *CurrentCarried.Sockets[0].Gem.ToString(),
							CurrentCarried.Sockets.IsEmpty() ? -1 : static_cast<int32>(CurrentCarried.Sockets[0].GemRarity),
							*TypesOf(CurrentWorn)));

	TestEqual(TEXT("it arrives at the current version"), Migrated->SchemaVersion,
			  UCataclysmCharacterSave::SchemaVersionNow);
	TestEqual(TEXT("the carried piece's 2 sockets are 2 empty sockets of type Any"), EmptyAnySockets(Carried), 2);
	TestEqual(TEXT("and no more"), Carried.Sockets.Num(), 2);
	TestEqual(TEXT("the worn greatsword's 3 are 3"), EmptyAnySockets(Worn), 3);
	TestEqual(TEXT("and no more"), Worn.Sockets.Num(), 3);
	TestEqual(TEXT("the held build's greatsword's 3 are 3"), EmptyAnySockets(Held), 3);
	TestEqual(TEXT("and no more"), Held.Sockets.Num(), 3);
	TestEqual(TEXT("the second worn item's none is none"), Migrated->WornGear[1].Item.Sockets.Num(), 0);
	TestEqual(TEXT("no carried slot holds a gem"), GemSlots, 0);
	TestTrue(TEXT("and every other field is kept"), bSame);
	TestEqual(TEXT("CONTROL: Character_v14.json's carried piece reads its own types"), TypesOf(CurrentCarried),
			  FString(TEXT("Attack,Defense")));
	TestTrue(TEXT("CONTROL: with Of The Viper at Quality in its first"),
			 CurrentCarried.Sockets.Num() == 2 && CurrentCarried.Sockets[0].Gem == FName(TEXT("Gem_Of_The_Viper"))
				 && CurrentCarried.Sockets[0].GemRarity == ECataclysmRarity::Quality);
	TestEqual(TEXT("CONTROL: and its worn greatsword reads Attack, Defense and Utility"), TypesOf(CurrentWorn),
			  FString(TEXT("Attack,Defense,Utility")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemSaveRunMigrationTest,
	"Cataclysm.GemSave.ARunFileFromVersion13LoadsWithSocketListsAndNoGemOnTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Run_v13.json`, written before version 14, migrates and loads: each ground item's socket count of 0 becomes an empty
 * list -- a count left as a number would refuse the whole record, since the list cannot be read from one -- and both
 * ground items hold no gem, every other field kept. CONTROL: the committed run file, `Run_v17.json` since issue #2840 (`Run_v16.json` from issue #2838, `Run_v15.json` from issue #2741 slice 9), reads its two
 * ground items the same. Prints "GEM RUN MIGRATED".
 */
bool FCataclysmGemSaveRunMigrationTest::RunTest(const FString&)
{
	using namespace CataclysmGemSaveTest;

	FString Text;
	FString CurrentText;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v13.json"), Text, Reason)
		|| !CataclysmSaveFixtures::Read(TEXT("Run_v17.json"), CurrentText, Reason))
	{
		AddError(Reason);
		return false;
	}
	const UCataclysmRunSave* Migrated = Cast<UCataclysmRunSave>(
		ReadRecordText(*this, Text, UCataclysmRunSave::StaticClass(), TEXT("Run_v13.json")));
	const UCataclysmRunSave* Current = Cast<UCataclysmRunSave>(
		ReadRecordText(*this, CurrentText, UCataclysmRunSave::StaticClass(), TEXT("Run_v17.json")));
	if (!Migrated || !Current
		|| !TestEqual(TEXT("set-up: Run_v13.json has two ground items"), Migrated->Floor.GroundItems.Num(), 2)
		|| !TestEqual(TEXT("set-up: Run_v17.json has two ground items"), Current->Floor.GroundItems.Num(), 2))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmRunSave::SchemaVersionNow);
	CataclysmSaveFixtures::AddTheRunsVersion14Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion15Defaults(*Expected); // And issue #2741 slice 9's step.
	CataclysmSaveFixtures::AddTheRunsVersion16Defaults(*Expected); // And issue #2838's step.
	CataclysmSaveFixtures::AddTheRunsVersion17Defaults(*Expected); // And issue #2840's step.
	FString Rewritten;
	FString Error;
	FString Where;
	const bool bSame = FCataclysmSaveStorage::ToJson(Migrated, Rewritten, Error)
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	int32 GemDrops = 0;
	int32 Sockets = 0;
	for (const FCataclysmSavedGroundItem& Drop : Migrated->Floor.GroundItems)
	{
		GemDrops += Drop.Gem.IsNone() && Drop.GemQuantity == 0 ? 0 : 1;
		Sockets += Drop.Item.Sockets.Num();
	}
	int32 CurrentGemDrops = 0;
	for (const FCataclysmSavedGroundItem& Drop : Current->Floor.GroundItems)
	{
		CurrentGemDrops += Drop.Gem.IsNone() && Drop.GemQuantity == 0 ? 0 : 1;
	}

	AddInfo(FString::Printf(TEXT("GEM RUN MIGRATED: version %d; %d ground item(s), %d socket(s), %d holding gems; the "
								 "rest %s; Run_v17.json %d holding gems"),
							Migrated->SchemaVersion, Migrated->Floor.GroundItems.Num(), Sockets, GemDrops,
							bSame ? TEXT("kept") : *Where, CurrentGemDrops));

	TestEqual(TEXT("it arrives at the current version"), Migrated->SchemaVersion, UCataclysmRunSave::SchemaVersionNow);
	TestEqual(TEXT("its ground items' counts of 0 are no sockets"), Sockets, 0);
	TestEqual(TEXT("no ground item holds gems"), GemDrops, 0);
	TestTrue(TEXT("and every other field is kept"), bSame);
	TestEqual(TEXT("CONTROL: nor does Run_v17.json's"), CurrentGemDrops, 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
