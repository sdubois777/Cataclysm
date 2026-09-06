// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSaveRecord.h"
#include "Empire/CataclysmSurge.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/CataclysmSaveFixtures.h"

/**
 * The three save records, and the text they turn into. Issue #529.
 *
 * WHAT IS BEING GUARDED HERE IS QUIET. A field dropped from a save does not
 * crash, does not log and does not fail to load. It comes back at its default,
 * and the only thing that notices is a player whose character lost its level.
 * So the checks below are about what reaches the file rather than about whether
 * the call returned true.
 */
namespace CataclysmSaveRecordTest
{
	/** A character record with something in every kind of field it has. */
	static UCataclysmCharacterSave* AFilledCharacter()
	{
		UCataclysmCharacterSave* Record = NewObject<UCataclysmCharacterSave>();
		Record->SchemaVersion = UCataclysmCharacterSave::SchemaVersionNow;
		Record->CharacterId = FGuid(0x5C0B1A4E, 0x5B7F4D2E, 0x9A3C6F81, 0xD24E7B03);
		Record->CharacterName = TEXT("Vesper");
		Record->Partition.Lethality = ECataclysmLethality::Hardcore;
		Record->Partition.Population = ECataclysmPopulation::Online;
		Record->Partition.bSoloSelfFound = false;
		Record->Level = 42;
		Record->Experience = 12884901888;
		Record->CataclysmicResidue = 13.5f;

		FCataclysmRolledAffix Affix;
		Affix.Affix = FName(TEXT("PrefixLifeFlat"));
		Affix.Tier = 5;
		Affix.Roll = 0.75f;

		FCataclysmCarriedSlot Gear;
		Gear.Item.Base = FName(TEXT("Circlet"));
		Gear.Item.GearLevel = 7;
		Gear.Item.Sockets = 2;
		Gear.Item.Residue = 4.25f;
		Gear.Item.Affixes.Add(Affix);
		Record->CarriedSlots.Add(Gear);

		FCataclysmCarriedSlot Material;
		Material.Material = FName(TEXT("Whispering_Ash"));
		Material.Quantity = 12;
		Record->CarriedSlots.Add(Material);

		// A WEAPON, BECAUSE ONLY A WEAPON CARRIES DAMAGE TYPES. The gear above
		// is a Circlet and every item in the committed fixtures is one, so
		// without this the whole save suite only ever writes an empty
		// Item.DamageTypes and a fault serialising a filled one would not show.
		// War and Void are both designed for a Greatsword. Issue #857.
		FCataclysmCarriedSlot Weapon;
		Weapon.Item.Base = FName(TEXT("Greatsword"));
		Weapon.Item.GearLevel = 3;
		Weapon.Item.DamageTypes.Add(FName(TEXT("War")));
		Weapon.Item.DamageTypes.Add(FName(TEXT("Void")));
		Record->CarriedSlots.Add(Weapon);

		return Record;
	}

	/** The first key in a piece of JSON text, or empty when there is none. */
	static FString FirstKeyIn(const FString& Json)
	{
		const int32 Open = Json.Find(TEXT("{"));
		if (Open == INDEX_NONE)
		{
			return FString();
		}

		const int32 QuoteStart = Json.Find(TEXT("\""), ESearchCase::CaseSensitive,
										   ESearchDir::FromStart, Open);
		if (QuoteStart == INDEX_NONE)
		{
			return FString();
		}

		const int32 QuoteEnd = Json.Find(TEXT("\""), ESearchCase::CaseSensitive,
										 ESearchDir::FromStart, QuoteStart + 1);
		if (QuoteEnd == INDEX_NONE)
		{
			return FString();
		}

		return Json.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
	}

	/** The fixture that holds each record type at its only version so far. */
	struct FFixture
	{
		TSubclassOf<UCataclysmSaveRecord> RecordClass;
		const TCHAR* FileName;
	};

	static TArray<FFixture> Fixtures()
	{
		return {
			{ UCataclysmAccountSave::StaticClass(),   TEXT("Account_v1.json") },
			// CHARACTER IS AT v2 SINCE 2026-08-24, when the attribute
			// allocation became a field on it. Character_v1.json is still
			// committed and still read, by the migration test further down.
			// THIS LIST IS THE CURRENT SHAPE OF EACH RECORD, and an out-of-date
			// file here would leave the completeness check below comparing a
			// record against a fixture that no longer describes it. Issue #50.
			{ UCataclysmCharacterSave::StaticClass(), TEXT("Character_v2.json") },
			{ UCataclysmRunSave::StaticClass(),       TEXT("Run_v1.json") },
		};
	}
}

/**
 * Every record type has its own name, its own version, and a chain that reaches
 * that version.
 *
 * THE THIRD PART IS THE ONE THAT MATTERS LATER. `StepsCoverEveryVersion` is
 * asked of every record class here, so the day somebody bumps a version and
 * forgets to write the step, this fails immediately rather than years later when
 * a player with an old file tries to load it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveRecordsAreThreeDistinctKinds,
	"Cataclysm.SaveRecords.EachRecordTypeHasItsOwnNameVersionAndCompleteChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveRecordsAreThreeDistinctKinds::RunTest(const FString&)
{
	const TArray<TSubclassOf<UCataclysmSaveRecord>> Classes = CataclysmSaveRecordClasses();
	TestEqual(TEXT("the design defines three records"), Classes.Num(), 3);

	TSet<FName> Names;
	for (const TSubclassOf<UCataclysmSaveRecord>& Class : Classes)
	{
		const UCataclysmSaveRecord* Default = Class.GetDefaultObject();
		if (Default == nullptr)
		{
			AddError(TEXT("a record class has no default object"));
			return false;
		}

		const FName Name = Default->RecordType();
		if (Name.IsNone())
		{
			AddError(FString::Printf(TEXT("%s does not say what kind of record it is"),
				*Class->GetName()));
			return false;
		}

		bool bAlreadyThere = false;
		Names.Add(Name, &bAlreadyThere);
		if (bAlreadyThere)
		{
			AddError(FString::Printf(TEXT("two record classes both call themselves '%s'"),
				*Name.ToString()));
			return false;
		}

		const int32 Version = Default->CurrentSchemaVersion();
		if (Version < UCataclysmSaveRecord::FirstSchemaVersion)
		{
			AddError(FString::Printf(TEXT("the %s record is at version %d, below the first real version %d"),
				*Name.ToString(), Version, UCataclysmSaveRecord::FirstSchemaVersion));
			return false;
		}

		FString Reason;
		if (!FCataclysmSaveMigration::StepsCoverEveryVersion(
				Default->MigrationSteps(), Version, Reason))
		{
			AddError(FString::Printf(TEXT("the %s record's migration chain is broken: %s"),
				*Name.ToString(), *Reason));
			return false;
		}
	}

	return true;
}

/**
 * The schema version is the first thing in the file.
 *
 * WHY THIS IS A TEST AND NOT A COMMENT. `docs/Save_System_Design.md` section 5
 * rule 1 requires the version first "so it can be read without parsing the
 * rest". A JSON object has no inherent order, so nothing in the language makes
 * this true: it is true because `FCataclysmSaveStorage::ToJson` puts that key in
 * the object before the converter adds any other. That is an arrangement, and an
 * arrangement can be undone by a tidy-up that looks harmless. This reads the
 * produced text and looks at what is actually first.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveVersionComesFirst,
	"Cataclysm.SaveRecords.TheSchemaVersionIsTheFirstThingInTheFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveVersionComesFirst::RunTest(const FString&)
{
	using namespace CataclysmSaveRecordTest;

	for (const TSubclassOf<UCataclysmSaveRecord>& Class : CataclysmSaveRecordClasses())
	{
		UCataclysmSaveRecord* Record = NewObject<UCataclysmSaveRecord>(GetTransientPackage(), Class);
		Record->SchemaVersion = Record->CurrentSchemaVersion();

		FString Json;
		FString Error;
		if (!FCataclysmSaveStorage::ToJson(Record, Json, Error))
		{
			AddError(FString::Printf(TEXT("a %s record would not convert to JSON: %s"),
				*Record->RecordType().ToString(), *Error));
			return false;
		}

		const FString First = FirstKeyIn(Json);
		if (First != UCataclysmSaveRecord::SchemaVersionField)
		{
			AddError(FString::Printf(
				TEXT("the first key in a %s record's file is '%s', and it has to be '%s'"),
				*Record->RecordType().ToString(), *First,
				UCataclysmSaveRecord::SchemaVersionField));
			return false;
		}
	}

	return true;
}

/**
 * Only fields marked `UPROPERTY(SaveGame)` reach the file.
 *
 * WITHOUT A FIELD THAT MUST NOT BE WRITTEN, THIS CANNOT BE TESTED AT ALL. A
 * converter that ignores the flag and writes every property would pass every
 * other check in this file, because every other field IS supposed to be there.
 * `SlotItWasReadFrom` is the one field that must not appear.
 *
 * IT HAS ALREADY BEEN WORTHLESS ONCE. `SlotItWasReadFrom` was declared
 * `Transient`, and the engine's converter drops a Transient property whatever
 * the check flags say, so converting every property instead of only the marked
 * ones left this test passing. Its header says the whole of it. Breaking a guard
 * on purpose is the only thing that found it.
 *
 * IT ALSO CHECKS THE FLAG REACHES INSIDE A NESTED STRUCT, by looking for a
 * carried item's affix tier. The converter carries the flag down into structs
 * held by structs, so an item whose own fields were not marked would serialise as
 * an empty object -- which is what would happen today if
 * `FCataclysmRolledAffix::Tier` lost its `SaveGame`.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveWritesOnlyMarkedFields,
	"Cataclysm.SaveRecords.AFieldWithoutTheSaveGameFlagDoesNotReachTheFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveWritesOnlyMarkedFields::RunTest(const FString&)
{
	using namespace CataclysmSaveRecordTest;

	UCataclysmCharacterSave* Record = AFilledCharacter();
	Record->SlotItWasReadFrom = TEXT("Character_ThisMustNotBeWritten");

	FString Json;
	FString Error;
	if (!FCataclysmSaveStorage::ToJson(Record, Json, Error))
	{
		AddError(FString::Printf(TEXT("a character record would not convert to JSON: %s"), *Error));
		return false;
	}

	const TSharedPtr<FJsonObject> Written = CataclysmSaveFixtures::Parse(Json);
	if (!Written.IsValid())
	{
		AddError(TEXT("what ToJson produced is not a JSON object"));
		return false;
	}

	if (Written->HasField(TEXT("SlotItWasReadFrom")))
	{
		AddError(TEXT("the transient field SlotItWasReadFrom reached the file, so the "
					  "SaveGame flag is not deciding what is persisted"));
		return false;
	}

	// AND THE MARKED FIELDS DID GET THERE, all the way down. A converter that
	// wrote nothing at all would also pass the check above.
	const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
	if (!Written->TryGetArrayField(TEXT("CarriedSlots"), Slots) || Slots->Num() != 3)
	{
		AddError(TEXT("the three carried slots did not reach the file"));
		return false;
	}

	const TSharedPtr<FJsonObject> FirstSlot = (*Slots)[0]->AsObject();
	const TSharedPtr<FJsonObject>* Item = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Affixes = nullptr;
	if (!FirstSlot.IsValid()
		|| !FirstSlot->TryGetObjectField(TEXT("Item"), Item)
		|| !Item->IsValid()
		|| !(*Item)->TryGetArrayField(TEXT("Affixes"), Affixes)
		|| Affixes->Num() != 1)
	{
		AddError(TEXT("the carried item's affix did not reach the file"));
		return false;
	}

	const TSharedPtr<FJsonObject> Affix = (*Affixes)[0]->AsObject();
	if (!Affix.IsValid())
	{
		AddError(TEXT("the affix in the file is not an object"));
		return false;
	}

	TestEqual(TEXT("the affix's tier survived two levels of nesting"),
		static_cast<int32>(Affix->GetNumberField(TEXT("Tier"))), 5);
	TestEqual(TEXT("the affix's name survived two levels of nesting"),
		Affix->GetStringField(TEXT("Affix")), FString(TEXT("PrefixLifeFlat")));

	return true;
}

/**
 * A record survives being written out and read back with every field intact.
 *
 * THIS IS THE WEAKEST TEST IN THE FILE AND IT IS HERE ON PURPOSE. Section 5 says
 * why: "a test that writes a save with the current code and reads it back proves
 * only that the code agrees with itself". It catches a field whose type does not
 * round-trip -- an enum written one way and read another, a 64-bit number losing
 * its top bits -- and nothing about older files. The fixture tests are what cover
 * that.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveRoundTripKeepsEveryField,
	"Cataclysm.SaveRecords.AFilledRecordSurvivesBeingWrittenAndReadBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveRoundTripKeepsEveryField::RunTest(const FString&)
{
	using namespace CataclysmSaveRecordTest;

	UCataclysmCharacterSave* Written = AFilledCharacter();

	FString Json;
	FString Error;
	if (!FCataclysmSaveStorage::ToJson(Written, Json, Error))
	{
		AddError(FString::Printf(TEXT("a character record would not convert to JSON: %s"), *Error));
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmSaveRecord* Back = FCataclysmSaveStorage::FromJson(
		Json, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message);

	if (Back == nullptr)
	{
		AddError(FString::Printf(TEXT("a character record would not read back: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(Back);
	if (Read == nullptr)
	{
		AddError(TEXT("what came back is not a character record"));
		return false;
	}

	TestEqual(TEXT("the schema version came back"), Read->SchemaVersion,
		UCataclysmCharacterSave::SchemaVersionNow);
	TestEqual(TEXT("the identifier came back"), Read->CharacterId, Written->CharacterId);
	TestEqual(TEXT("the name came back"), Read->CharacterName, Written->CharacterName);
	TestEqual(TEXT("the lethality mode came back"),
		static_cast<int32>(Read->Partition.Lethality),
		static_cast<int32>(ECataclysmLethality::Hardcore));
	TestEqual(TEXT("the population came back"),
		static_cast<int32>(Read->Partition.Population),
		static_cast<int32>(ECataclysmPopulation::Online));
	TestFalse(TEXT("the Solo Self-Found flag came back"), Read->Partition.bSoloSelfFound);
	TestEqual(TEXT("the level came back"), Read->Level, 42);

	// 12,884,901,888 IS PAST WHAT 32 BITS HOLD, on purpose. A field silently
	// narrowed to int32 would come back as zero.
	TestEqual(TEXT("an experience total past two billion came back whole"),
		Read->Experience, static_cast<int64>(12884901888));
	TestEqual(TEXT("the residue came back"), Read->CataclysmicResidue, 13.5f);

	if (Read->CarriedSlots.Num() != 3)
	{
		AddError(FString::Printf(TEXT("%d carried slots came back, expected 3"),
			Read->CarriedSlots.Num()));
		return false;
	}

	TestEqual(TEXT("the gear's base came back"), Read->CarriedSlots[0].Item.Base,
		FName(TEXT("Circlet")));
	TestEqual(TEXT("the gear's upgrade level came back"), Read->CarriedSlots[0].Item.GearLevel, 7);
	TestEqual(TEXT("the gear's sockets came back"), Read->CarriedSlots[0].Item.Sockets, 2);
	TestEqual(TEXT("the gear's residue came back"), Read->CarriedSlots[0].Item.Residue, 4.25f);

	if (Read->CarriedSlots[0].Item.Affixes.Num() != 1)
	{
		AddError(TEXT("the gear's affix did not come back"));
		return false;
	}
	TestEqual(TEXT("the affix's tier came back"), Read->CarriedSlots[0].Item.Affixes[0].Tier, 5);
	TestEqual(TEXT("the affix's roll came back"), Read->CarriedSlots[0].Item.Affixes[0].Roll, 0.75f);

	TestEqual(TEXT("the material's name came back"), Read->CarriedSlots[1].Material,
		FName(TEXT("Whispering_Ash")));
	TestEqual(TEXT("the material's count came back"), Read->CarriedSlots[1].Quantity, 12);

	// THE WEAPON'S DAMAGE TYPES, IN ORDER. A list that came back reordered
	// would still hold the same types and would still be wrong, because the
	// order is what makes one seed roll one weapon. Issue #857.
	TestEqual(TEXT("the weapon's base came back"), Read->CarriedSlots[2].Item.Base,
		FName(TEXT("Greatsword")));
	if (Read->CarriedSlots[2].Item.DamageTypes.Num() != 2)
	{
		AddError(FString::Printf(
			TEXT("%d of the weapon's damage types came back, expected 2"),
			Read->CarriedSlots[2].Item.DamageTypes.Num()));
		return false;
	}
	TestEqual(TEXT("the weapon's first damage type came back"),
		Read->CarriedSlots[2].Item.DamageTypes[0], FName(TEXT("War")));
	TestEqual(TEXT("the weapon's second damage type came back"),
		Read->CarriedSlots[2].Item.DamageTypes[1], FName(TEXT("Void")));

	return true;
}

/**
 * Every committed example save file loads, and holds every field its record
 * writes.
 *
 * THIS IS THE ACCEPTANCE CRITERION FROM ISSUE #21, by way of #529: a test that
 * loads a save the current code did not write. The files in
 * `game/Tests/SaveFixtures/` were written by hand.
 *
 * THE SECOND HALF IS WHAT KEEPS THEM HONEST. The fixture is loaded, written back
 * out, and the two are compared field by field. A field added to a record but not
 * to its fixture appears on one side only and fails here -- otherwise a fixture
 * would slowly stop covering the record it is named for, while still passing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveFixturesAreComplete,
	"Cataclysm.SaveRecords.EveryFixtureHoldsEveryFieldItsRecordWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveFixturesAreComplete::RunTest(const FString&)
{
	using namespace CataclysmSaveRecordTest;

	for (const FFixture& Fixture : Fixtures())
	{
		FString Text;
		FString Reason;
		if (!CataclysmSaveFixtures::Read(Fixture.FileName, Text, Reason))
		{
			AddError(Reason);
			return false;
		}

		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		FString Message;
		UCataclysmSaveRecord* Loaded = FCataclysmSaveStorage::FromJson(
			Text, Fixture.RecordClass, GetTransientPackage(), Result, Message);

		if (Loaded == nullptr)
		{
			AddError(FString::Printf(TEXT("%s would not load: %s -- %s"),
				Fixture.FileName, FCataclysmSaveStorage::Describe(Result), *Message));
			return false;
		}

		FString Rewritten;
		FString Error;
		if (!FCataclysmSaveStorage::ToJson(Loaded, Rewritten, Error))
		{
			AddError(FString::Printf(TEXT("%s loaded but would not write back out: %s"),
				Fixture.FileName, *Error));
			return false;
		}

		const TSharedPtr<FJsonObject> FromFile = CataclysmSaveFixtures::Parse(Text);
		const TSharedPtr<FJsonObject> FromRecord = CataclysmSaveFixtures::Parse(Rewritten);
		if (!FromFile.IsValid() || !FromRecord.IsValid())
		{
			AddError(FString::Printf(TEXT("%s or what it wrote back is not a JSON object"),
				Fixture.FileName));
			return false;
		}

		FString Where;
		if (!CataclysmSaveFixtures::SameObject(FromFile, FromRecord, Where))
		{
			AddError(FString::Printf(
				TEXT("%s and the record it loads into disagree at %s. Either the fixture is ")
				TEXT("missing a field the record now has, or it holds one the record dropped"),
				Fixture.FileName, *Where));
			return false;
		}
	}

	return true;
}

/**
 * The character fixture holds the values it is supposed to hold.
 *
 * SEPARATE FROM THE COMPLETENESS CHECK ABOVE, AND NOT THE SAME QUESTION. That
 * one asks whether the fixture and the record agree with each other; this asks
 * whether the record ends up holding what the file actually says. A reader that
 * put every value in the wrong field would satisfy the first and fail this.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveCharacterFixtureReadsCorrectly,
	"Cataclysm.SaveRecords.TheCommittedCharacterFileReadsIntoTheValuesItStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveCharacterFixtureReadsCorrectly::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Character_v2.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));

	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Character_v2.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	TestEqual(TEXT("it says it is version 2"), Read->SchemaVersion, 2);
	TestEqual(TEXT("the name in the file"), Read->CharacterName, FString(TEXT("Vesper")));
	TestEqual(TEXT("the identifier in the file"),
		Read->CharacterId.ToString(EGuidFormats::Digits),
		FString(TEXT("5C0B1A4E5B7F4D2E9A3C6F81D24E7B03")));
	TestEqual(TEXT("the lethality mode in the file"),
		static_cast<int32>(Read->Partition.Lethality),
		static_cast<int32>(ECataclysmLethality::Hardcore));
	TestEqual(TEXT("the population in the file"),
		static_cast<int32>(Read->Partition.Population),
		static_cast<int32>(ECataclysmPopulation::Online));
	TestFalse(TEXT("it is not Solo Self-Found"), Read->Partition.bSoloSelfFound);
	TestEqual(TEXT("the level in the file"), Read->Level, 42);
	TestEqual(TEXT("the experience in the file"), Read->Experience,
		static_cast<int64>(12884901888));
	TestEqual(TEXT("the residue in the file"), Read->CataclysmicResidue, 13.5f);

	if (Read->CarriedSlots.Num() != 2)
	{
		AddError(FString::Printf(TEXT("%d carried slots read, expected 2"), Read->CarriedSlots.Num()));
		return false;
	}

	TestEqual(TEXT("the gear's base"), Read->CarriedSlots[0].Item.Base, FName(TEXT("Circlet")));
	TestEqual(TEXT("the gear's affix count"), Read->CarriedSlots[0].Item.Affixes.Num(), 2);
	TestEqual(TEXT("the resistance affix names its damage type"),
		Read->CarriedSlots[0].Item.Affixes[1].DamageTypes.Num(), 1);
	TestEqual(TEXT("and that damage type is the one in the file"),
		Read->CarriedSlots[0].Item.Affixes[1].DamageTypes[0], FName(TEXT("Fire")));
	TestEqual(TEXT("the material stack"), Read->CarriedSlots[1].Quantity, 12);

	// THE PRIVATE STASH IS EMPTY BECAUSE THIS CHARACTER IS NOT SOLO SELF-FOUND,
	// which is the design's rule rather than an accident of the fixture: an
	// ordinary character's stash lives in an account record.
	TestEqual(TEXT("a character that is not Solo Self-Found has no private stash"),
		Read->PrivateStash.Num(), 0);
	TestEqual(TEXT("and no private empire upgrade points"),
		Read->PrivateEmpireUpgradePoints, 0);

	// THE ATTRIBUTE ALLOCATION, which the v2 fixture is the reason for. Issue
	// #50. Read field by field rather than only as a total, because a reader
	// that put every count in the wrong attribute would still total 42.
	const FCataclysmAttributePoints& Points = Read->SpentAttributePoints;
	TestEqual(TEXT("the ferocity in the file"), Points.Ferocity, 10);
	TestEqual(TEXT("the constitution in the file"), Points.Constitution, 12);
	TestEqual(TEXT("the vitality in the file"), Points.Vitality, 20);
	TestEqual(TEXT("and nothing in agility"), Points.Agility, 0);

	// AND THE TOTAL IS THE CHARACTER'S LEVEL, which is not a coincidence in the
	// fixture: the design gives one attribute point per level, so a fully spent
	// level 42 character has spent exactly 42. A fixture that broke that rule
	// would be describing a character the game cannot produce.
	TestEqual(TEXT("a fully spent character has one point for every level"),
		Points.Total(), Read->Level);

	return true;
}

/**
 * A character file written before attribute allocation existed still loads.
 *
 * THIS IS WHAT Character_v1.json IS FOR NOW. It was the current fixture until
 * 2026-08-24 and is kept because a migration step with no file to run against is
 * a migration step nobody has ever executed. Issue #50.
 *
 * WHAT IT PROVES IS THAT NOTHING WAS LOST. The file has no attribute allocation
 * at all, so the character must arrive with none spent -- not with a garbage
 * count, and not refused for having a field missing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveCharacterV1Migrates,
	"Cataclysm.SaveRecords.ACharacterFileFromBeforeAttributesStillLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveCharacterV1Migrates::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Character_v1.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));

	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Character_v1.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	TestEqual(TEXT("it arrives at the current version"),
		Read->SchemaVersion, UCataclysmCharacterSave::SchemaVersionNow);
	TestEqual(TEXT("a character from before allocation has spent nothing"),
		Read->SpentAttributePoints.Total(), 0);

	// AND EVERYTHING ELSE SURVIVED THE MIGRATION. A step that dropped a field
	// while adding one would otherwise pass the two checks above.
	TestEqual(TEXT("the name still reads"), Read->CharacterName, FString(TEXT("Vesper")));
	TestEqual(TEXT("the level still reads"), Read->Level, 42);
	TestEqual(TEXT("the carried slots still read"), Read->CarriedSlots.Num(), 2);

	return true;
}

/**
 * The run fixture keeps the floor as it stood, which is what section 6 is for.
 *
 * THE BOSS'S HEALTH IS THE POINT OF THE WHOLE FEATURE. The project owner set the
 * rule on 2026-08-20: a Hardcore character must not be able to leave a losing
 * boss fight by closing the game, so a boss comes back at the health it had.
 * `RarityStep` 4 is the first boss rung.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveRunFixtureKeepsTheFloor,
	"Cataclysm.SaveRecords.TheCommittedRunFileKeepsTheFloorAndTheDamageDoneToIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveRunFixtureKeepsTheFloor::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v1.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));

	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Run_v1.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	TestEqual(TEXT("the day"), Read->Day, 118);

	// AND THE PART OF A DAY THE FIXTURE CARRIES. Issue #1299 added `PartialDay`
	// and this file gained it under the exception documented in
	// `game/Tests/SaveFixtures/README.md`: nothing has ever loaded a save, so no
	// file this fixture describes exists on anybody's disk and it may be edited
	// to match a field its record gained.
	//
	// A QUARTER RATHER THAN ZERO, DELIBERATELY. Zero is the field's default, so a
	// fixture holding zero would pass whether the field was read or not. That the
	// value is absent from a file at all is covered separately, by
	// `AFileWithoutThePartOfADayStillLoads` below.
	TestEqual(TEXT("and the part of a day it carries"),
		Read->PartialDay, 0.25f, 0.0001f);

	TestTrue(TEXT("somebody is on a floor"), Read->Floor.IsOccupied());
	TestEqual(TEXT("which floor"), Read->Floor.Floor, 6);
	TestEqual(TEXT("which dungeon"), Read->Floor.Dungeon, FName(TEXT("Hell_On_Earth")));

	if (Read->Floor.Creatures.Num() != 2)
	{
		AddError(FString::Printf(TEXT("%d creatures read, expected 2"), Read->Floor.Creatures.Num()));
		return false;
	}

	const FCataclysmSavedCreature& Boss = Read->Floor.Creatures[0];
	TestEqual(TEXT("the boss's archetype"), Boss.ArchetypeRow, FName(TEXT("Brute")));
	TestEqual(TEXT("the boss's rung on the rarity ladder"), Boss.RarityStep, 4);
	TestEqual(TEXT("the boss keeps every point taken off it"), Boss.Health, 812.25f);
	TestEqual(TEXT("the boss's modifier"), Boss.ModifierRows.Num(), 1);
	TestEqual(TEXT("and which modifier it is"), Boss.ModifierRows[0],
		FName(TEXT("Demonic_Hellfire_Aura")));
	TestEqual(TEXT("where the boss was standing"), Boss.Location, FVector(1250.5, -640.25, 96.0));

	if (Read->Floor.Characters.Num() != 1)
	{
		AddError(TEXT("the character's placement did not read"));
		return false;
	}
	TestEqual(TEXT("the character's health"), Read->Floor.Characters[0].Health, 318.75f);
	TestEqual(TEXT("the character's mana"), Read->Floor.Characters[0].Mana, 82.5f);
	TestEqual(TEXT("the character's energy shield"),
		Read->Floor.Characters[0].EnergyShield, 25.0f);

	TestEqual(TEXT("an item left on the floor"), Read->Floor.GroundItems.Num(), 1);

	return true;
}

/**
 * A file that predates a field still loads, and the field takes its default.
 *
 * WHAT THIS IS THE TEST FOR. Issue #1299 added `UCataclysmRunSave::PartialDay`
 * WITHOUT bumping the schema version, on the strength of
 * `docs/Save_System_Design.md` section 5: "Adding a field with a sensible default
 * is not a version bump -- `UPROPERTY(SaveGame)` already handles a missing field
 * by leaving the default." That is a claim about what the engine does, and this
 * is what makes it a tested claim rather than an asserted one.
 *
 * IT BUILDS THE OLD SHAPE RATHER THAN READING A COMMITTED FILE, and it has to.
 * `Run_v1.json` gained the field under the exception in
 * `game/Tests/SaveFixtures/README.md`, and
 * `Cataclysm.SaveRecords.EveryFixtureHoldsEveryFieldItsRecordWrites` requires
 * every fixture to hold every field its record writes -- so no committed fixture
 * can be missing one. Taking the fixture and removing the line is the only way
 * to have a file in the shape a player's disk would hold.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveRunFileWithoutThePartOfADay,
	"Cataclysm.SaveRecords.AFileWithoutThePartOfADayStillLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveRunFileWithoutThePartOfADay::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v1.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	// THE CONTROL. If the line were not there, removing it would do nothing and
	// the assertion below would pass for the wrong reason.
	const FString Line = TEXT("\t\"PartialDay\": 0.25,\r\n");
	const FString Alternate = TEXT("\t\"PartialDay\": 0.25,\n");

	FString Older = Text;
	if (Older.Contains(Line))
	{
		Older = Older.Replace(*Line, TEXT(""));
	}
	else if (Older.Contains(Alternate))
	{
		Older = Older.Replace(*Alternate, TEXT(""));
	}
	else
	{
		AddError(TEXT("Run_v1.json no longer carries a PartialDay line to "
					  "remove, so this test would prove nothing. If the field "
					  "was renamed, rename it here."));
		return false;
	}

	if (!TestFalse(TEXT("the older shape really is missing the field"),
				   Older.Contains(TEXT("PartialDay"))))
	{
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(
		FCataclysmSaveStorage::FromJson(Older, UCataclysmRunSave::StaticClass(),
										GetTransientPackage(), Result, Message));

	// IT LOADS AT ALL, which is the half a player would notice. A missing field
	// that refused the file would lose the whole save rather than one number.
	if (!TestNotNull(TEXT("a file without the field still loads"), Read))
	{
		AddError(FString::Printf(TEXT("it was refused: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	TestEqual(TEXT("and the missing field takes its default"),
		Read->PartialDay, 0.0f, 0.0001f);

	// AND NOTHING ELSE WAS LOST WITH IT. A load that quietly dropped the rest of
	// the record would also satisfy the two lines above.
	TestEqual(TEXT("the day still reads"), Read->Day, 118);
	TestTrue(TEXT("and somebody is still on a floor"), Read->Floor.IsOccupied());
	TestEqual(TEXT("on the floor the file names"), Read->Floor.Floor, 6);

	return true;
}

/**
 * The committed run file's surge schedule reads into the fields it names.
 *
 * NOT THE SAME QUESTION AS THE COMPLETENESS CHECK, and this is the record where
 * the difference bites hardest. That test loads a fixture, writes it back out and
 * compares -- so it catches a field the fixture has forgotten, but two integer
 * fields whose values were swapped agree with themselves perfectly and pass it.
 * `SurgeIndex` and `SurgesFired` are two integers side by side that mean
 * different things, and reading them back one at a time is the only thing that
 * would notice.
 *
 * SO THE FIXTURE HOLDS 7 AND 9 RATHER THAN THE SAME NUMBER TWICE, and every one
 * of the six values differs from its field's default. See
 * `game/Tests/SaveFixtures/README.md`.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveRunFixtureKeepsTheSchedule,
	"Cataclysm.SaveRecords.TheCommittedRunFileKeepsTheSurgeSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveRunFixtureKeepsTheSchedule::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v1.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(
		FCataclysmSaveStorage::FromJson(Text, UCataclysmRunSave::StaticClass(),
										GetTransientPackage(), Result, Message));

	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Run_v1.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	// THE ENUM BY NAME, WHICH IS HOW THE FILE SPELLS IT. A mode read as its
	// default would be `Static`, and the file says `Both`.
	TestEqual(TEXT("the escalation the run is played under"),
			  static_cast<uint8>(Read->SurgeMode),
			  static_cast<uint8>(ECataclysmSurgeMode::Both));

	TestEqual(TEXT("the lethality rung"), Read->SurgeLethalityRung, 2);

	// TWO COUNTS THAT ARE NOT THE SAME NUMBER, on purpose. Swap them and one of
	// these two lines fails; make them equal and neither could.
	TestEqual(TEXT("how far escalation has got"), Read->SurgeIndex, 7);
	TestEqual(TEXT("how many waves have landed"), Read->SurgesFired, 9);

	TestEqual(TEXT("which day the next wave is due on"), Read->NextSurgeDay,
			  122.5f, 0.0001f);

	// AND WHERE THE RUN'S CHANCE HAD GOT TO. A position in a sequence rather
	// than a seed somebody chose; see `UCataclysmRunSave::RandomStreamSeed`.
	TestEqual(TEXT("where the run's chance had got to"),
			  Read->RandomStreamSeed, 1743984213);

	return true;
}


/**
 * A save written before the surge schedule existed still loads.
 *
 * THE SAME CLAIM `AFileWithoutThePartOfADayStillLoads` MAKES, PUT TO A DIFFERENT
 * KIND OF FIELD. That test covers one float. `SurgeMode` is an enum, and an enum
 * property missing from the JSON is a different path through the reader from a
 * missing number -- a reader that refused it, or that left the property
 * uninitialised rather than at its declared default, would satisfy that test and
 * fail this one.
 *
 * IT BUILDS THE OLD SHAPE RATHER THAN READING A COMMITTED FILE, and it has to:
 * `Cataclysm.SaveRecords.EveryFixtureHoldsEveryFieldItsRecordWrites` requires
 * every fixture to hold every field its record writes, so no committed file can
 * be missing one.
 *
 * WHAT IT IS EVIDENCE FOR is the reason issue #1307 added six fields at schema
 * version 1 with no migration step. `docs/Save_System_Design.md` section 5 says
 * "adding a field with a sensible default is not a version bump", which is a
 * claim about what the engine does; this is what makes it a tested claim.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveRunFileWithoutTheSchedule,
	"Cataclysm.SaveRecords.AFileWithoutTheSurgeScheduleStillLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveRunFileWithoutTheSchedule::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v1.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	// EACH LINE EXACTLY AS THE FILE SPELLS IT, so that a renamed field fails
	// here with a message saying so rather than quietly removing nothing and
	// leaving the assertions below to pass for the wrong reason.
	const TArray<TPair<FString, FString>> Lines = {
		{ TEXT("SurgeMode"), TEXT("\"Both\"") },
		{ TEXT("SurgeLethalityRung"), TEXT("2") },
		{ TEXT("SurgeIndex"), TEXT("7") },
		{ TEXT("SurgesFired"), TEXT("9") },
		{ TEXT("NextSurgeDay"), TEXT("122.5") },
		{ TEXT("RandomStreamSeed"), TEXT("1743984213") },
	};

	FString Older = Text;
	for (const TPair<FString, FString>& Field : Lines)
	{
		const FString Line = FString::Printf(TEXT("\t\"%s\": %s,"), *Field.Key,
											 *Field.Value);

		bool bRemoved = false;
		for (const TCHAR* Ending : { TEXT("\r\n"), TEXT("\n") })
		{
			const FString Whole = Line + Ending;
			if (Older.Contains(Whole))
			{
				Older = Older.Replace(*Whole, TEXT(""));
				bRemoved = true;
				break;
			}
		}

		if (!bRemoved)
		{
			AddError(FString::Printf(
				TEXT("Run_v1.json no longer carries the line `%s`, so this test "
					 "would prove nothing. If the field was renamed or its value "
					 "changed, change it here too."), *Line));
			return false;
		}

		if (!TestFalse(*FString::Printf(TEXT("%s really is gone"), *Field.Key),
					   Older.Contains(Field.Key)))
		{
			return false;
		}
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(
		FCataclysmSaveStorage::FromJson(Older, UCataclysmRunSave::StaticClass(),
										GetTransientPackage(), Result, Message));

	// IT LOADS AT ALL, which is the half a player would notice. A missing field
	// that refused the file would lose the whole save rather than six numbers.
	if (!TestNotNull(TEXT("a file without the schedule still loads"), Read))
	{
		AddError(FString::Printf(TEXT("it was refused: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	TestEqual(TEXT("the missing mode takes its default"),
			  static_cast<uint8>(Read->SurgeMode),
			  static_cast<uint8>(ECataclysmSurgeMode::Static));
	TestEqual(TEXT("the missing lethality rung"), Read->SurgeLethalityRung, 0);
	TestEqual(TEXT("the missing surge index"), Read->SurgeIndex, 0);
	TestEqual(TEXT("the missing wave count"), Read->SurgesFired, 0);
	TestEqual(TEXT("the missing due day"), Read->NextSurgeDay, 0.0f, 0.0001f);
	TestEqual(TEXT("and the missing stream position"), Read->RandomStreamSeed, 0);

	// AND NOTHING ELSE WENT WITH THEM. A load that quietly dropped the rest of
	// the record would also satisfy every line above.
	TestEqual(TEXT("the day still reads"), Read->Day, 118);
	TestEqual(TEXT("the cities still read"), Read->Cities.Num(), 2);
	TestEqual(TEXT("the dungeons still read"), Read->Dungeons.Num(), 1);
	TestTrue(TEXT("and somebody is still on a floor"), Read->Floor.IsOccupied());

	return true;
}


#endif // WITH_AUTOMATION_TESTS
