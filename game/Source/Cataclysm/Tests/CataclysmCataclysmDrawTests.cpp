// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Player/CataclysmGameInstance.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "UObject/Package.h"

/**
 * A character keeps its Cataclysm draw across runs. Issue #1324 slice 6a, ruled by the coordinating session on
 * 2026-11-02 from `docs/Cataclysm_GDD_v2.md`, section II, Ending a Run: "The same tier means the same Cataclysms, by
 * name and not only by count." and "Only defeating the boss dungeon adds a Cataclysm to the next run."
 *
 * THE RULING: the character record carries a Cataclysm order seed, drawn once when the character is created, and every
 * run's Cataclysm list is the first N (N the tier's count) of `UCataclysmRoster::OrderFor(that seed)`. A character file
 * from before the field migrates with the seed its identifier gives (`UCataclysmCharacterSave::CataclysmOrderSeedFor`).
 *
 * THE RUN TESTS GO THROUGH `UCataclysmGameInstance::BeginACharactersEmpireRun`, the door both play paths use, on a
 * game instance with no world; each names its tier, so none reads the game's. The two play paths themselves -- a new
 * character and a loaded character's new run -- are in `CataclysmSaveLoadTests.cpp`, which has the scene they need.
 *
 * NO TEST HERE COMPARES WITH A LIST OF CATACLYSM NAMES WRITTEN OUT. `OrderFor` shuffles with `FRandomStream`, and the
 * orders this file's comments quote were worked out by a Python copy of that generator, not by the game; each is printed
 * by the test's info line so a run shows the real one. Every assertion compares two of the game's own answers.
 */

namespace CataclysmDrawTest
{
	/** A run's Cataclysms as their names, comma separated, in the run's order. */
	FString NamesOf(const TArray<ECataclysmType>& Cataclysms)
	{
		TArray<FString> Out;
		for (const ECataclysmType Cataclysm : Cataclysms)
		{
			Out.Add(UCataclysmRoster::NameFor(Cataclysm).ToString());
		}
		return FString::Join(Out, TEXT(", "));
	}

	/** The Cataclysms a run just begun faces, as names, or empty with no run. */
	FString NamesOfRun(const UCataclysmEmpireRun* Run)
	{
		return Run ? NamesOf(Run->ActiveCataclysms) : FString();
	}

	/** A character's seed the tests below use, chosen so it is no run seed they use. */
	constexpr int32 ACharactersSeed = 24681357;
}

/**
 * Two runs of one character at one tier face the same Cataclysms, by name and in order, though each run's own seed --
 * its chance -- differs. Prints "SAME TIER".
 *
 * CONTROL: two runs that name no character, at the same two run seeds, face different lists, so it is the character's
 * seed and not luck that makes the two agree. By the Python copy of the generator: the character's (24681357) tier 3
 * list is Demonic, Death, Pestilence; run seed 11's own is Death, Demonic, Chaos and 12's Demonic, Death, War. SINCE
 * THE PROJECT OWNER'S RULING OF 2026-11-26 every tier 3 list is the two built Cataclysms (`UCataclysmRoster::Built`)
 * in one of two orders and then one of six unbuilt ones, so two lists drawn by chance agree about once in twelve. With
 * War also counted as built, as it was until the owner took it off the list the same day, the three lists were Demonic,
 * War, Death; Death, Demonic, War; and War, Demonic, Death. Until the ruling they were Demonic, Pestilence, War; Death,
 * Chaos, Void; and War, Famine, Demonic.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDrawSameTierTest,
	"Cataclysm.CataclysmDraw.TwoRunsOfOneCharacterAtOneTierFaceTheSameCataclysmsInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDrawSameTierTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDrawTest;

	UCataclysmGameInstance* Instance = NewObject<UCataclysmGameInstance>();
	const FString First = NamesOfRun(Instance->BeginACharactersEmpireRun(/*Seed*/ 11, ACharactersSeed,
																		 ECataclysmSurgeMode::Static, 0, /*Tier*/ 3));
	const FString Second = NamesOfRun(Instance->BeginACharactersEmpireRun(/*Seed*/ 12, ACharactersSeed,
																		  ECataclysmSurgeMode::Static, 0, /*Tier*/ 3));
	const FString Drawn = NamesOf(UCataclysmRoster::ActiveFor(ACharactersSeed, 3));
	const FString OwnEleven = NamesOfRun(Instance->BeginEmpireRun(/*Seed*/ 11, ECataclysmSurgeMode::Static, 0, 3));
	const FString OwnTwelve = NamesOfRun(Instance->BeginEmpireRun(/*Seed*/ 12, ECataclysmSurgeMode::Static, 0, 3));

	AddInfo(FString::Printf(TEXT("SAME TIER: the character's tier 3 draw [%s]; its run on seed 11 [%s], on seed 12 [%s]; "
								 "runs naming no character, seed 11 [%s], seed 12 [%s]"),
							*Drawn, *First, *Second, *OwnEleven, *OwnTwelve));

	TestEqual(TEXT("set-up: the character's draw at tier 3 is three Cataclysms"),
			  UCataclysmRoster::ActiveFor(ACharactersSeed, 3).Num(), 3);
	TestTrue(TEXT("the first run faces the character's draw, by name and in order"), First == Drawn);
	TestTrue(TEXT("the second run faces the same Cataclysms as the first, by name and in order"), Second == First);
	TestTrue(TEXT("CONTROL: two runs naming no character, on the same two seeds, face different Cataclysms"),
			 OwnEleven != OwnTwelve);
	return true;
}

/**
 * A run one tier higher faces the lower tier's Cataclysms, in the same order, and exactly one more, at the end, that it
 * did not face before: "Only defeating the boss dungeon adds a Cataclysm to the next run." Each run has its own seed, so
 * the agreement is the character's. Prints "TIER HIGHER".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDrawTierHigherTest,
	"Cataclysm.CataclysmDraw.ARunATierHigherFacesTheSameCataclysmsAndOneMoreAtTheEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDrawTierHigherTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDrawTest;

	UCataclysmGameInstance* Instance = NewObject<UCataclysmGameInstance>();
	UCataclysmEmpireRun* AtThree = Instance->BeginACharactersEmpireRun(/*Seed*/ 11, ACharactersSeed,
																	   ECataclysmSurgeMode::Static, 0, /*Tier*/ 3);
	const TArray<ECataclysmType> Lower = AtThree ? AtThree->ActiveCataclysms : TArray<ECataclysmType>();
	UCataclysmEmpireRun* AtFour = Instance->BeginACharactersEmpireRun(/*Seed*/ 12, ACharactersSeed,
																	  ECataclysmSurgeMode::Static, 0, /*Tier*/ 4);
	const TArray<ECataclysmType> Higher = AtFour ? AtFour->ActiveCataclysms : TArray<ECataclysmType>();

	AddInfo(FString::Printf(TEXT("TIER HIGHER: tier 3 [%s]; tier 4 [%s]"), *NamesOf(Lower), *NamesOf(Higher)));

	if (!TestEqual(TEXT("set-up: the tier 3 run faces three"), Lower.Num(), 3)
		|| !TestEqual(TEXT("set-up: the tier 4 run faces four"), Higher.Num(), 4))
	{
		return false;
	}

	const TArray<ECataclysmType> HigherFront(Higher.GetData(), 3);
	TestTrue(TEXT("the tier 4 run's first three are the tier 3 run's, by name and in order"),
			 NamesOf(HigherFront) == NamesOf(Lower));
	TestFalse(TEXT("and the one added, at the end, is not one the tier 3 run faced"), Lower.Contains(Higher[3]));
	return true;
}

/**
 * Two characters with different seeds can face different Cataclysms, though their runs share one seed. Seeds 7 and 8
 * were picked because the Python copy of the generator gives them different tier 3 lists: Celestial, Death, Void and
 * Void, Demonic, Death. Prints "TWO CHARACTERS".
 *
 * TIER 4 SINCE THE PROJECT OWNER'S RULING OF 2026-11-26, which put the built Cataclysms first in every draw
 * (`UCataclysmRoster::Built`). With two built (Demonic and Death, War having been taken off the list the same day), by
 * the Python copy the two tier 4 lists are Death, Demonic, Celestial, Void and Demonic, Death, Void, Chaos -- different
 * names, not only a different order. With three built they were Death, Demonic, War, Celestial and Demonic, Death,
 * War, Void.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDrawTwoCharactersTest,
	"Cataclysm.CataclysmDraw.TwoCharactersWithDifferentSeedsCanFaceDifferentCataclysms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDrawTwoCharactersTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDrawTest;

	UCataclysmGameInstance* Instance = NewObject<UCataclysmGameInstance>();
	const FString Seven = NamesOfRun(Instance->BeginACharactersEmpireRun(/*Seed*/ 5, /*CataclysmOrderSeed*/ 7,
																		 ECataclysmSurgeMode::Static, 0, /*Tier*/ 4));
	const FString Eight = NamesOfRun(Instance->BeginACharactersEmpireRun(/*Seed*/ 5, /*CataclysmOrderSeed*/ 8,
																		 ECataclysmSurgeMode::Static, 0, /*Tier*/ 4));

	AddInfo(FString::Printf(TEXT("TWO CHARACTERS: on run seed 5 at tier 4, the character of seed 7 [%s], of seed 8 [%s]"),
							*Seven, *Eight));

	TestTrue(TEXT("the character of seed 7 faces its own draw"), Seven == NamesOf(UCataclysmRoster::ActiveFor(7, 4)));
	TestTrue(TEXT("the character of seed 8 faces its own draw"), Eight == NamesOf(UCataclysmRoster::ActiveFor(8, 4)));
	TestTrue(TEXT("the two characters face different Cataclysms"), Seven != Eight);
	return true;
}

/**
 * The committed character file reads into the seed it states, and the seed survives being written out and read back.
 * It read `Character_v7.json` until the character record went to version 8 on 2026-11-18 (issue #2742), and
 * `Character_v8.json` until it went to version 9 the same day (issue #2741 slice 1c), and `Character_v9.json` until it
 * went to version 10 on 2026-11-19 (issue #2764), `Character_v14.json` until it went to version 15 on 2026-11-24
 * (issue #2848), and `Character_v15.json` until it went to version 16 the same day (issue #2849); each states the same
 * seed.
 * The file states 975318642, which is not the seed its identifier gives, so a seed worked out again on load cannot pass.
 * Prints "DRAW FILE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDrawFixtureTest,
	"Cataclysm.CataclysmDraw.TheCommittedCharacterFileKeepsItsSeedThroughARoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDrawFixtureTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Character_v18.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Character_v18.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	FString Written;
	FString Error;
	const bool bWritten = FCataclysmSaveStorage::ToJson(Read, Written, Error);
	ECataclysmSaveLoadResult BackResult = ECataclysmSaveLoadResult::NotValidJson;
	FString BackMessage;
	const UCataclysmCharacterSave* Back = bWritten
		? Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
			Written, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), BackResult, BackMessage))
		: nullptr;

	AddInfo(FString::Printf(TEXT("DRAW FILE: version %d, seed %d, the identifier's %d; written and read back: %s"),
							Read->SchemaVersion, Read->CataclysmOrderSeed,
							UCataclysmCharacterSave::CataclysmOrderSeedFor(Read->CharacterId),
							Back ? *FString::FromInt(Back->CataclysmOrderSeed) : *Error));

	// THE CURRENT VERSION SINCE `Character_v18.json` BECAME THE COMMITTED FILE (#47 slice F6a); it was 17 from issue #837 slice 1, 16 from issue #2849, 15 from issue #2848, 14 from issue #2741 slice 9, 13 from issue #46,
	// the gems window, 12 from issue #2741 slice 8, 11 from issue #2741 slice 6e, 10 from issue #2764, 9 from issue #2741 slice 1c, 8 until then.
	TestEqual(TEXT("set-up: the file is the current version"), Read->SchemaVersion,
			  UCataclysmCharacterSave::SchemaVersionNow);
	TestEqual(TEXT("the seed the file states"), Read->CataclysmOrderSeed, CataclysmSaveFixtures::CommittedCharacterOrderSeed);
	TestTrue(TEXT("CONTROL: which is not the seed its identifier gives"),
			 Read->CataclysmOrderSeed != UCataclysmCharacterSave::CataclysmOrderSeedFor(Read->CharacterId));
	if (TestNotNull(TEXT("the record written out reads back"), Back))
	{
		TestEqual(TEXT("holding the same seed"), Back->CataclysmOrderSeed, CataclysmSaveFixtures::CommittedCharacterOrderSeed);
	}
	return true;
}

/**
 * The seed an identifier gives is fixed, as numbers: the committed files' identifier gives 1412640493, the zero
 * identifier 1768495365 and the identifier 1-2-3-4 1069075193, each worked out in Python by
 * `tools/tests/test_a_character_keeps_its_cataclysm_draw.py`. A change to the hash -- or an engine whose
 * `GetTypeHash(FGuid)` it had come to lean on -- would move every migrated character's Cataclysms, and this is what says
 * so. Prints "SEED FOR".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDrawSeedForTest,
	"Cataclysm.CataclysmDraw.TheSeedAnIdentifierGivesIsFixed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDrawSeedForTest::RunTest(const FString& Parameters)
{
	FGuid Committed;
	const bool bParsed = FGuid::Parse(TEXT("5C0B1A4E5B7F4D2E9A3C6F81D24E7B03"), Committed);
	const int32 FromCommitted = UCataclysmCharacterSave::CataclysmOrderSeedFor(Committed);
	const int32 FromZero = UCataclysmCharacterSave::CataclysmOrderSeedFor(FGuid());
	const int32 FromOneTwoThreeFour = UCataclysmCharacterSave::CataclysmOrderSeedFor(FGuid(1, 2, 3, 4));

	AddInfo(FString::Printf(TEXT("SEED FOR: the committed identifier %d, zero %d, 1-2-3-4 %d"), FromCommitted, FromZero,
							FromOneTwoThreeFour));

	TestTrue(TEXT("set-up: the committed identifier parses"), bParsed);
	TestEqual(TEXT("the committed files' identifier gives 1412640493"), FromCommitted,
			  CataclysmSaveFixtures::FixtureIdentifiersOrderSeed);
	TestEqual(TEXT("the zero identifier gives 1768495365"), FromZero, 1768495365);
	TestEqual(TEXT("the identifier 1-2-3-4 gives 1069075193"), FromOneTwoThreeFour, 1069075193);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
