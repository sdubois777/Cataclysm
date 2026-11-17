// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSaveRecord.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveExampleRecord.h"
#include "Tests/CataclysmSaveFixtures.h"

/**
 * The migration chain. Issue #529, `docs/Save_System_Design.md` section 5.
 *
 * WHY THE EXAMPLE RECORD RATHER THAN A REAL ONE. All three real records are at
 * schema version 1, and version 1 is the first, so not one of them has a
 * migration to run. Inventing a fake version bump on the character record would
 * mean committing an example save file for a version that never existed, and
 * every future reader having to work out that it is a lie.
 * `UCataclysmSaveExampleRecord` is a record whose only purpose is to be old.
 *
 * THE MACHINERY IS TESTED SEPARATELY FROM THE STEPS. The chains built inside
 * this file exercise ordering, refusal and gaps with steps that count how often
 * they ran; the example record's own two steps are the ones that do real work on
 * committed files.
 */
namespace CataclysmSaveMigrationTest
{
	/**
	 * How many times each counting step below has run.
	 *
	 * FILE-SCOPE COUNTERS BECAUSE A STEP IS A PLAIN FUNCTION POINTER, which
	 * cannot capture. That is the price of steps being named functions rather
	 * than lambdas, and it is worth paying: a named function can be found, and
	 * broken on purpose by `prove_cpp_guard`.
	 */
	static int32 RanFirst = 0;
	static int32 RanSecond = 0;
	static int32 RanThird = 0;

	/** The order the steps ran in, as text: "1,2,3". */
	static FString Order;

	static void Reset()
	{
		RanFirst = 0;
		RanSecond = 0;
		RanThird = 0;
		Order.Reset();
	}

	static void Note(const TCHAR* Which)
	{
		if (!Order.IsEmpty())
		{
			Order += TEXT(",");
		}
		Order += Which;
	}

	bool CountFirst(const TSharedRef<FJsonObject>&, FString&)
	{
		++RanFirst;
		Note(TEXT("1"));
		return true;
	}

	bool CountSecond(const TSharedRef<FJsonObject>&, FString&)
	{
		++RanSecond;
		Note(TEXT("2"));
		return true;
	}

	bool CountThird(const TSharedRef<FJsonObject>&, FString&)
	{
		++RanThird;
		Note(TEXT("3"));
		return true;
	}

	bool AlwaysFails(const TSharedRef<FJsonObject>&, FString& OutError)
	{
		OutError = TEXT("this step is here to fail");
		return false;
	}

	/** A step that does its work but forgets to touch the version field. */
	bool ForgetsTheVersion(const TSharedRef<FJsonObject>& Record, FString&)
	{
		Record->SetStringField(TEXT("Label"), TEXT("changed"));
		return true;
	}

	/** A record holding nothing but a version. */
	static TSharedRef<FJsonObject> AtVersion(int32 Version)
	{
		const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
		Record->SetNumberField(UCataclysmSaveRecord::SchemaVersionField, Version);
		return Record;
	}

	/** The version a record now claims to be. -1 when it does not say. */
	static int32 VersionOf(const TSharedRef<FJsonObject>& Record)
	{
		int32 Version = 0;
		return FCataclysmSaveMigration::ReadVersion(Record, Version) ? Version : -1;
	}
}

/**
 * Every version of the example record loads into exactly the same thing.
 *
 * THIS IS THE TEST ISSUE #21 ASKED FOR and the reason the fixtures are
 * committed. Three files, written by hand at three different schema versions,
 * describing one record. If the chain runs the wrong steps, runs them in the
 * wrong order, or runs one twice, the three results stop agreeing.
 *
 * COMPARING THEM AGAINST EACH OTHER IS NOT ENOUGH ON ITS OWN, because a chain
 * that threw everything away would make all three agree on nothing. So the
 * values are also checked against what the files say: 725 copper is 7 gold and
 * 25 copper.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveEveryOldVersionLoadsTheSame,
	"Cataclysm.SaveMigration.EveryCommittedVersionOfTheExampleLoadsToTheSameRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveEveryOldVersionLoadsTheSame::RunTest(const FString&)
{
	struct FCase
	{
		const TCHAR* FileName;
		int32 VersionInTheFile;
		bool bShouldMigrate;
	};

	const FCase Cases[] = {
		{ TEXT("Example_v1.json"), 1, true },
		{ TEXT("Example_v2.json"), 2, true },
		{ TEXT("Example_v3.json"), 3, false },
	};

	for (const FCase& Case : Cases)
	{
		FString Text;
		FString Reason;
		if (!CataclysmSaveFixtures::Read(Case.FileName, Text, Reason))
		{
			AddError(Reason);
			return false;
		}

		// THE FILE SAYS WHAT VERSION IT IS BEFORE ANYTHING INTERPRETS IT, which
		// is what section 5 rule 1 exists for.
		int32 OnDisk = 0;
		if (!FCataclysmSaveStorage::ReadSchemaVersion(Text, OnDisk))
		{
			AddError(FString::Printf(TEXT("%s does not say what version it is"), Case.FileName));
			return false;
		}
		TestEqual(FString::Printf(TEXT("%s says which version it is"), Case.FileName),
			OnDisk, Case.VersionInTheFile);

		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		FString Message;
		UCataclysmSaveExampleRecord* Read = Cast<UCataclysmSaveExampleRecord>(
			FCataclysmSaveStorage::FromJson(Text, UCataclysmSaveExampleRecord::StaticClass(),
											GetTransientPackage(), Result, Message));

		if (Read == nullptr)
		{
			AddError(FString::Printf(TEXT("%s would not load: %s -- %s"),
				Case.FileName, FCataclysmSaveStorage::Describe(Result), *Message));
			return false;
		}

		TestEqual(FString::Printf(TEXT("%s was %s"), Case.FileName,
				Case.bShouldMigrate ? TEXT("migrated") : TEXT("already current")),
			static_cast<int32>(Result),
			static_cast<int32>(Case.bShouldMigrate
				? ECataclysmSaveLoadResult::Migrated
				: ECataclysmSaveLoadResult::Loaded));

		TestEqual(FString::Printf(TEXT("%s ends at the version this build understands"),
				Case.FileName),
			Read->SchemaVersion, UCataclysmSaveExampleRecord::SchemaVersionNow);

		// THE VALUES THE FILES ACTUALLY STATE. `Title` became `Label` at version
		// 2, and 725 copper became 7 gold and 25 copper at version 3.
		TestEqual(FString::Printf(TEXT("%s: the label"), Case.FileName),
			Read->Label, FString(TEXT("The Example Record")));
		TestEqual(FString::Printf(TEXT("%s: the gold"), Case.FileName), Read->Gold, 7);
		TestEqual(FString::Printf(TEXT("%s: the copper"), Case.FileName), Read->Copper, 25);
	}

	return true;
}

/**
 * A record written by a newer build is refused rather than loaded.
 *
 * SECTION 5, RULE 5, AND IT IS A RULE RATHER THAN A PREFERENCE. Loading a newer
 * save silently drops whatever the newer build added, and the player finds out
 * when the thing they care about is gone. Refusing is the only answer that tells
 * them something is wrong while it can still be fixed by running the newer build.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveNewerRecordIsRefused,
	"Cataclysm.SaveMigration.ARecordFromANewerBuildIsRefusedRatherThanGuessedAt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveNewerRecordIsRefused::RunTest(const FString&)
{
	using namespace CataclysmSaveMigrationTest;

	const TSharedRef<FJsonObject> Record = AtVersion(4);
	FString Message;

	const ECataclysmSaveMigrationResult Result = FCataclysmSaveMigration::Migrate(
		UCataclysmSaveExampleRecord::StaticClass()->GetDefaultObject<UCataclysmSaveExampleRecord>()
			->MigrationSteps(),
		UCataclysmSaveExampleRecord::SchemaVersionNow, Record, Message);

	TestEqual(TEXT("a version 4 record read by a version 3 build is refused"),
		static_cast<int32>(Result),
		static_cast<int32>(ECataclysmSaveMigrationResult::NewerThanThisBuild));
	TestTrue(TEXT("and the refusal says both version numbers"),
		Message.Contains(TEXT("version 4")) && Message.Contains(TEXT("version 3")));

	// AND IT IS REFUSED ALL THE WAY OUT, not just by the chain. A caller that
	// only looked at whether an object came back would otherwise never see it.
	const FString Json = TEXT("{ \"SchemaVersion\": 4, \"Label\": \"from the future\" }");
	ECataclysmSaveLoadResult LoadResult = ECataclysmSaveLoadResult::Loaded;
	FString LoadMessage;
	UCataclysmSaveRecord* Loaded = FCataclysmSaveStorage::FromJson(
		Json, UCataclysmSaveExampleRecord::StaticClass(), GetTransientPackage(),
		LoadResult, LoadMessage);

	TestNull(TEXT("nothing comes back from a newer record"), Loaded);
	TestEqual(TEXT("and the reason names the migration chain"),
		static_cast<int32>(LoadResult),
		static_cast<int32>(ECataclysmSaveLoadResult::MigrationRefusedIt));

	return true;
}

/**
 * The steps run in order, from the record's version to the current one, and each
 * runs exactly once.
 *
 * THREE MISTAKES THIS CATCHES, AND EACH OF THEM LOOKS FINE IN THE CODE: running
 * the chain from the beginning rather than from the record's own version, so an
 * already-applied step runs again; running the steps in the order they were
 * declared rather than the order the versions need; and running one step and
 * stopping.
 *
 * THE CHAIN IS DECLARED BACKWARDS ON PURPOSE. `Migrate` finds a step by the
 * version it leads out of, so a chain listed in any order must still run
 * forwards. Declaring it in order would let a bug that simply walked the array
 * pass.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveStepsRunInVersionOrder,
	"Cataclysm.SaveMigration.StepsRunInVersionOrderAndEachRunsExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveStepsRunInVersionOrder::RunTest(const FString&)
{
	using namespace CataclysmSaveMigrationTest;

	static const FCataclysmSaveMigrationStep DeclaredBackwards[] = {
		{ 3, TEXT("Migrate_3_to_4"), &CountThird },
		{ 2, TEXT("Migrate_2_to_3"), &CountSecond },
		{ 1, TEXT("Migrate_1_to_2"), &CountFirst },
	};

	Reset();
	const TSharedRef<FJsonObject> FromTheStart = AtVersion(1);
	FString Message;
	ECataclysmSaveMigrationResult Result = FCataclysmSaveMigration::Migrate(
		DeclaredBackwards, 4, FromTheStart, Message);

	TestEqual(TEXT("a version 1 record reaches version 4"),
		static_cast<int32>(Result), static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestEqual(TEXT("and the steps ran forwards"), Order, FString(TEXT("1,2,3")));
	TestEqual(TEXT("the first step ran once"), RanFirst, 1);
	TestEqual(TEXT("the second step ran once"), RanSecond, 1);
	TestEqual(TEXT("the third step ran once"), RanThird, 1);
	TestEqual(TEXT("and the record says it is version 4"), VersionOf(FromTheStart), 4);

	// STARTING PART WAY ALONG. The first step must not run at all: it has
	// already been applied to this file by whichever build wrote it.
	Reset();
	const TSharedRef<FJsonObject> PartWayAlong = AtVersion(3);
	Result = FCataclysmSaveMigration::Migrate(DeclaredBackwards, 4, PartWayAlong, Message);

	TestEqual(TEXT("a version 3 record reaches version 4"),
		static_cast<int32>(Result), static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestEqual(TEXT("only the last step ran"), Order, FString(TEXT("3")));
	TestEqual(TEXT("the first step did not run again"), RanFirst, 0);
	TestEqual(TEXT("nor did the second"), RanSecond, 0);

	// ALREADY CURRENT. Nothing runs and nothing is a failure.
	Reset();
	const TSharedRef<FJsonObject> Current = AtVersion(4);
	Result = FCataclysmSaveMigration::Migrate(DeclaredBackwards, 4, Current, Message);

	TestEqual(TEXT("a record already at the current version is left alone"),
		static_cast<int32>(Result),
		static_cast<int32>(ECataclysmSaveMigrationResult::AlreadyCurrent));
	TestEqual(TEXT("and no step ran"), Order, FString());

	return true;
}

/**
 * A gap in the chain is refused, rather than the missing version being skipped.
 *
 * A SKIPPED VERSION IS THE WORST OUTCOME AVAILABLE. The record arrives claiming
 * to be current while holding fields in the old shape, so everything downstream
 * of it reads plausible rubbish. Refusing loses the load; skipping loses the
 * save.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveGapInTheChainIsRefused,
	"Cataclysm.SaveMigration.AGapInTheChainIsRefusedRatherThanSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveGapInTheChainIsRefused::RunTest(const FString&)
{
	using namespace CataclysmSaveMigrationTest;

	// NO STEP LEADS OUT OF VERSION 2.
	static const FCataclysmSaveMigrationStep WithAGap[] = {
		{ 1, TEXT("Migrate_1_to_2"), &CountFirst },
		{ 3, TEXT("Migrate_3_to_4"), &CountThird },
	};

	Reset();
	const TSharedRef<FJsonObject> Record = AtVersion(1);
	FString Message;
	const ECataclysmSaveMigrationResult Result =
		FCataclysmSaveMigration::Migrate(WithAGap, 4, Record, Message);

	TestEqual(TEXT("the chain stops at the gap"),
		static_cast<int32>(Result),
		static_cast<int32>(ECataclysmSaveMigrationResult::NoStepForVersion));
	TestTrue(TEXT("and says which version has nothing leading out of it"),
		Message.Contains(TEXT("version 2")));
	TestEqual(TEXT("the step before the gap ran"), RanFirst, 1);
	TestEqual(TEXT("the step past the gap did not"), RanThird, 0);

	// THE RECORD SAYS HOW FAR IT ACTUALLY GOT, not how far it was meant to. A
	// record left claiming version 4 would be refused for being from the future
	// on the next load.
	TestEqual(TEXT("and the record says it reached version 2"), VersionOf(Record), 2);

	// THE SAME GAP IS FOUND WITHOUT RUNNING ANYTHING, which is what makes a
	// version bump that forgets its step fail on the day it is written.
	FString Reason;
	TestFalse(TEXT("a chain with a gap does not cover every version"),
		FCataclysmSaveMigration::StepsCoverEveryVersion(WithAGap, 4, Reason));
	TestTrue(TEXT("and the reason names the missing version"),
		Reason.Contains(TEXT("version 2")));

	return true;
}

/**
 * A step that fails stops the chain where it got to, and says which step it was.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveFailingStepStopsTheChain,
	"Cataclysm.SaveMigration.AStepThatFailsStopsTheChainAndNamesItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveFailingStepStopsTheChain::RunTest(const FString&)
{
	using namespace CataclysmSaveMigrationTest;

	static const FCataclysmSaveMigrationStep WithAFailure[] = {
		{ 1, TEXT("Migrate_1_to_2"), &CountFirst },
		{ 2, TEXT("Migrate_2_to_3"), &AlwaysFails },
		{ 3, TEXT("Migrate_3_to_4"), &CountThird },
	};

	Reset();
	const TSharedRef<FJsonObject> Record = AtVersion(1);
	FString Message;
	const ECataclysmSaveMigrationResult Result =
		FCataclysmSaveMigration::Migrate(WithAFailure, 4, Record, Message);

	TestEqual(TEXT("the chain reports the failure"),
		static_cast<int32>(Result),
		static_cast<int32>(ECataclysmSaveMigrationResult::StepFailed));
	TestTrue(TEXT("and names the step that failed"), Message.Contains(TEXT("Migrate_2_to_3")));
	TestTrue(TEXT("and passes on what the step said"),
		Message.Contains(TEXT("this step is here to fail")));
	TestEqual(TEXT("the step after the failure did not run"), RanThird, 0);
	TestEqual(TEXT("and the record says it reached version 2"), VersionOf(Record), 2);

	return true;
}

/**
 * The chain writes the version, so a step that forgets to cannot leave a record
 * claiming to be older than it is.
 *
 * WHY THIS IS WORTH A TEST OF ITS OWN. A step that transformed the fields and
 * left the version alone would be run again on the next load, over data that has
 * already been transformed once. For a rename that is harmless; for the split in
 * the example record it would divide by a hundred twice.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveChainOwnsTheVersionField,
	"Cataclysm.SaveMigration.TheChainWritesTheVersionEvenWhenAStepDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveChainOwnsTheVersionField::RunTest(const FString&)
{
	using namespace CataclysmSaveMigrationTest;

	static const FCataclysmSaveMigrationStep Forgetful[] = {
		{ 1, TEXT("Migrate_1_to_2"), &ForgetsTheVersion },
	};

	const TSharedRef<FJsonObject> Record = AtVersion(1);
	FString Message;
	const ECataclysmSaveMigrationResult Result =
		FCataclysmSaveMigration::Migrate(Forgetful, 2, Record, Message);

	TestEqual(TEXT("the record migrated"),
		static_cast<int32>(Result), static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestEqual(TEXT("and it says version 2 even though the step never wrote it"),
		VersionOf(Record), 2);
	TestEqual(TEXT("and the step did do its own work"),
		Record->GetStringField(TEXT("Label")), FString(TEXT("changed")));

	return true;
}

/**
 * A record with no usable version is refused, and so is one claiming a version
 * below the first real one.
 *
 * ZERO IS NOT VERSION 1. A record holding zero came from a file with no version
 * field or from an object nobody filled in. Treating it as version 1 would run
 * the whole chain over a record that may be in any shape at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveUnversionedRecordIsRefused,
	"Cataclysm.SaveMigration.ARecordWithNoUsableVersionIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveUnversionedRecordIsRefused::RunTest(const FString&)
{
	using namespace CataclysmSaveMigrationTest;

	static const FCataclysmSaveMigrationStep Steps[] = {
		{ 1, TEXT("Migrate_1_to_2"), &CountFirst },
	};

	FString Message;

	const TSharedRef<FJsonObject> NoField = MakeShared<FJsonObject>();
	NoField->SetStringField(TEXT("Label"), TEXT("nothing said what shape this is"));
	TestEqual(TEXT("a record with no version field is refused"),
		static_cast<int32>(FCataclysmSaveMigration::Migrate(Steps, 2, NoField, Message)),
		static_cast<int32>(ECataclysmSaveMigrationResult::NoVersionField));

	const TSharedRef<FJsonObject> NotAWholeNumber = MakeShared<FJsonObject>();
	NotAWholeNumber->SetNumberField(UCataclysmSaveRecord::SchemaVersionField, 1.5);
	TestEqual(TEXT("a version of 1.5 is refused rather than truncated to 1"),
		static_cast<int32>(FCataclysmSaveMigration::Migrate(Steps, 2, NotAWholeNumber, Message)),
		static_cast<int32>(ECataclysmSaveMigrationResult::NoVersionField));

	TestEqual(TEXT("a version of zero is refused"),
		static_cast<int32>(FCataclysmSaveMigration::Migrate(Steps, 2, AtVersion(0), Message)),
		static_cast<int32>(ECataclysmSaveMigrationResult::VersionIsNotReal));

	TestEqual(TEXT("and so is a negative one"),
		static_cast<int32>(FCataclysmSaveMigration::Migrate(Steps, 2, AtVersion(-3), Message)),
		static_cast<int32>(ECataclysmSaveMigrationResult::VersionIsNotReal));

	// A VERSION WRITTEN AS TEXT IS ACCEPTED. A file edited by hand is the
	// ordinary way an old record reaches a new build.
	const TSharedRef<FJsonObject> AsText = MakeShared<FJsonObject>();
	AsText->SetStringField(UCataclysmSaveRecord::SchemaVersionField, TEXT("1"));
	Reset();
	TestEqual(TEXT("a version written as text still migrates"),
		static_cast<int32>(FCataclysmSaveMigration::Migrate(Steps, 2, AsText, Message)),
		static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestEqual(TEXT("and it is left as a number afterwards"), VersionOf(AsText), 2);

	return true;
}

/**
 * A migration reads only what is in the record.
 *
 * SECTION 5, RULE 4. A step that reached for a data table would break the moment
 * that table changed, which is the thing most likely to change, and the breakage
 * would show up as a corrupt save rather than as an error. This checks the
 * example record's own steps against a record holding nothing but a version:
 * every step still succeeds, because there is nothing outside the record it
 * needs.
 *
 * THE STRONGER HALF OF THIS RULE IS CHECKED IN PYTHON, by
 * `tools/tests/test_save_migrations_are_single_steps.py`, which reads the source
 * of every step and fails if one mentions a data table at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveMigrationNeedsNothingOutsideTheRecord,
	"Cataclysm.SaveMigration.AStepNeedsNothingOutsideTheRecordItIsGiven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveMigrationNeedsNothingOutsideTheRecord::RunTest(const FString&)
{
	using namespace CataclysmSaveMigrationTest;

	const UCataclysmSaveExampleRecord* Default =
		UCataclysmSaveExampleRecord::StaticClass()->GetDefaultObject<UCataclysmSaveExampleRecord>();

	const TSharedRef<FJsonObject> Bare = AtVersion(1);
	FString Message;
	const ECataclysmSaveMigrationResult Result = FCataclysmSaveMigration::Migrate(
		Default->MigrationSteps(), UCataclysmSaveExampleRecord::SchemaVersionNow, Bare, Message);

	TestEqual(TEXT("a record holding nothing but a version still migrates"),
		static_cast<int32>(Result), static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestEqual(TEXT("and reaches the current version"), VersionOf(Bare),
		UCataclysmSaveExampleRecord::SchemaVersionNow);

	// THE FIELDS THAT WERE NOT THERE ARE STILL NOT THERE. A step that invented a
	// value for a missing field would be reading something the record did not
	// carry.
	TestFalse(TEXT("no label was invented"), Bare->HasField(TEXT("Label")));
	TestFalse(TEXT("no gold was invented"), Bare->HasField(TEXT("Gold")));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
