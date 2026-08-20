// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Save/CataclysmSaveTriggers.h"

/**
 * When a save is written, decided. Issue #750.
 *
 * WHY THESE RULES ARE WORTH A TEST FILE OF THEIR OWN. Every one of them fails
 * quietly and none of them fails visibly. A death written asynchronously still
 * writes, a moment later, and that moment is the window a player closing the
 * game would use -- which is the whole thing `docs/Save_System_Design.md`
 * section 6 exists to close. An account record written on the clock still holds
 * the right thing, and costs a 600-slot stash serialised every interval to
 * record that nothing happened to it.
 *
 * NO WORLD AND NO FILESYSTEM. These are static functions over plain values, the
 * same split `UCataclysmEnemyDeath` makes between deciding what dying looks like
 * and playing it.
 */
namespace CataclysmSaveTriggerTest
{
	using FTriggers = UCataclysmSaveTriggers;

	/** How many values the enum declares, counted from the enum itself. */
	static int32 DeclaredTriggerCount()
	{
		const UEnum* Enum = StaticEnum<ECataclysmSaveTrigger>();
		if (!Enum)
		{
			return 0;
		}

		// MINUS THE HIDDEN LAST ENTRY. Unreal Header Tool appends a `_MAX`
		// enumerator to every UENUM, and it is not a trigger.
		return Enum->NumEnums() - 1;
	}
}

/**
 * Exactly one trigger is written before the frame ends, and it is death.
 *
 * BOTH HALVES MATTER. Death being synchronous is the rule section 6 says cannot
 * be relaxed. A SECOND trigger being synchronous would be trading frame time for
 * nothing, and the one most likely to be made synchronous by mistake is a
 * creature dying -- it is the one that reads most like death.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveOnlyDeathIsSynchronous,
	"Cataclysm.SaveTriggers.OnlyTheCharacterDyingIsWrittenBeforeTheFrameEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveOnlyDeathIsSynchronous::RunTest(const FString&)
{
	using namespace CataclysmSaveTriggerTest;

	int32 Synchronous = 0;
	for (const ECataclysmSaveTrigger Trigger : FTriggers::Every())
	{
		const bool bNow = FTriggers::MustBeWrittenBeforeTheFrameEnds(Trigger);
		const bool bExpected = Trigger == ECataclysmSaveTrigger::CharacterDied;

		if (bNow != bExpected)
		{
			AddError(FString::Printf(
				TEXT("'%s' %s written before the frame ends, and it %s be. Section 6 "
					 "names the character dying and nothing else."),
				FTriggers::NameOf(Trigger),
				bNow ? TEXT("is") : TEXT("is not"),
				bExpected ? TEXT("has to") : TEXT("must not")));
			return false;
		}

		Synchronous += bNow ? 1 : 0;
	}

	TestEqual(TEXT("exactly one trigger is synchronous"), Synchronous, 1);
	return true;
}

/**
 * The run record is written for everything except an account change, and the
 * account record only for an account change.
 *
 * THAT SPLIT IS WHAT MAKES A FREQUENT SAVE AFFORDABLE. Section 6: "the run
 * record on the cadence above, the account record only when it actually
 * changes." The account record is the one carrying a 600-slot stash, and writing
 * it every interval would serialise all of that to say nothing had happened.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveRecordsAreWrittenForTheRightReasons,
	"Cataclysm.SaveTriggers.EachRecordIsWrittenForTheReasonsThatChangeIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveRecordsAreWrittenForTheRightReasons::RunTest(const FString&)
{
	using namespace CataclysmSaveTriggerTest;

	int32 WroteTheAccount = 0;

	for (const ECataclysmSaveTrigger Trigger : FTriggers::Every())
	{
		const bool bAccount = Trigger == ECataclysmSaveTrigger::AccountChanged;

		TestEqual(FString::Printf(TEXT("'%s' writes the run record"),
					FTriggers::NameOf(Trigger)),
			FTriggers::WritesTheRunRecord(Trigger), !bAccount);

		TestEqual(FString::Printf(TEXT("'%s' writes the account record"),
					FTriggers::NameOf(Trigger)),
			FTriggers::WritesTheAccountRecord(Trigger), bAccount);

		WroteTheAccount += bAccount ? 1 : 0;

		// THE CHARACTER RECORD IS WRITTEN FOR THE TWO THINGS THAT CHANGE IT.
		// What a character is carrying lives in its own record, so an item has
		// to reach it; and a death is written whole.
		const bool bCharacter = Trigger == ECataclysmSaveTrigger::InventoryChanged
			|| Trigger == ECataclysmSaveTrigger::CharacterDied;
		TestEqual(FString::Printf(TEXT("'%s' writes the character record"),
					FTriggers::NameOf(Trigger)),
			FTriggers::WritesTheCharacterRecord(Trigger), bCharacter);
	}

	TestEqual(TEXT("exactly one trigger writes the account record"), WroteTheAccount, 1);
	return true;
}

/**
 * Every trigger the enum declares is in the list this file answers for, and
 * every one of them has a name.
 *
 * WITHOUT THIS, ADDING A TRIGGER COVERS IT WITH NOTHING. Every test above walks
 * `Every()`, which is written out by hand; a trigger added to the enum and not
 * to that list would be tested by nothing at all, and every check here would go
 * on passing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveEveryTriggerIsAccountedFor,
	"Cataclysm.SaveTriggers.EveryTriggerIsInTheListThisFileAnswersFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveEveryTriggerIsAccountedFor::RunTest(const FString&)
{
	using namespace CataclysmSaveTriggerTest;

	const TArray<ECataclysmSaveTrigger> Listed = FTriggers::Every();
	const int32 Declared = DeclaredTriggerCount();

	if (Declared <= 0)
	{
		AddError(TEXT("ECataclysmSaveTrigger has no reflection, so the enum cannot "
					  "be walked and this check would pass for free."));
		return false;
	}

	TestEqual(TEXT("the list holds every trigger the enum declares"),
		Listed.Num(), Declared);

	for (int32 Value = 0; Value < Declared; ++Value)
	{
		const ECataclysmSaveTrigger Trigger = static_cast<ECataclysmSaveTrigger>(Value);
		if (!Listed.Contains(Trigger))
		{
			AddError(FString::Printf(
				TEXT("the trigger numbered %d is declared and is not in "
					 "UCataclysmSaveTriggers::Every(), so nothing in this file tests "
					 "it."), Value));
			return false;
		}

		const FString Name = FTriggers::NameOf(Trigger);
		if (Name == TEXT("an unnamed trigger"))
		{
			AddError(FString::Printf(
				TEXT("the trigger numbered %d has no wording in NameOf, so every log "
					 "line and failure about it says 'an unnamed trigger'."), Value));
			return false;
		}
	}

	// AND NO TWO SHARE A WORDING, which would make a log line ambiguous about
	// which of them wrote.
	TSet<FString> Names;
	for (const ECataclysmSaveTrigger Trigger : Listed)
	{
		bool bAlready = false;
		Names.Add(FTriggers::NameOf(Trigger), &bAlready);
		if (bAlready)
		{
			AddError(FString::Printf(TEXT("two triggers are both called '%s'"),
				FTriggers::NameOf(Trigger)));
			return false;
		}
	}

	return true;
}

/**
 * The clock waits its interval, and an interval nobody set does not write at all.
 *
 * AN INTERVAL OF ZERO IS THE CASE WORTH GUARDING. Read as a cadence it means
 * "write every frame", which would serialise the whole floor sixty times a
 * second for the rest of the session. There is no configuration that switches
 * the clock off -- section 6 requires one -- so a zero is a number nobody set.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveClockWaitsItsInterval,
	"Cataclysm.SaveTriggers.TheClockWaitsItsIntervalAndAnUnsetOneNeverWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveClockWaitsItsInterval::RunTest(const FString&)
{
	using namespace CataclysmSaveTriggerTest;

	TestFalse(TEXT("half an interval is not enough"),
		FTriggers::ClockHasElapsed(7.5f, 15.0f));
	TestTrue(TEXT("exactly an interval is enough"),
		FTriggers::ClockHasElapsed(15.0f, 15.0f));
	TestTrue(TEXT("more than an interval is enough"),
		FTriggers::ClockHasElapsed(15.001f, 15.0f));
	TestFalse(TEXT("no time at all is not enough"),
		FTriggers::ClockHasElapsed(0.0f, 15.0f));

	TestFalse(TEXT("an interval of zero never writes"),
		FTriggers::ClockHasElapsed(1000.0f, 0.0f));
	TestFalse(TEXT("a negative interval never writes"),
		FTriggers::ClockHasElapsed(1000.0f, -1.0f));

	// THE INTERVAL THE GAME ACTUALLY USES IS A REAL ONE. A constant left at zero
	// would switch the clock off and every check above would still pass.
	TestTrue(TEXT("the interval the game uses is a real one"),
		FTriggers::SecondsBetweenClockWrites > 0.0f);

	return true;
}

/**
 * Health falling through the threshold writes, and nothing else does.
 *
 * THREE MISTAKES THIS CATCHES, and each of them looks right in the code:
 * writing while health is merely BELOW the line, which writes on every frame a
 * wounded character stands still; writing when health rises back THROUGH it,
 * which writes for good news; and a threshold of zero, which fires on the frame
 * health reaches zero and doubles the death write.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveHealthThresholdFiresOnTheCrossing,
	"Cataclysm.SaveTriggers.HealthWritesOnTheWayThroughTheLineAndNotWhileBelowIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveHealthThresholdFiresOnTheCrossing::RunTest(const FString&)
{
	using namespace CataclysmSaveTriggerTest;

	constexpr float Half = 0.5f;

	TestTrue(TEXT("falling from above the line to below it writes"),
		FTriggers::HealthCrossedTheThreshold(0.9f, 0.4f, Half));
	TestTrue(TEXT("falling from exactly the line to below it writes"),
		FTriggers::HealthCrossedTheThreshold(0.5f, 0.49f, Half));

	TestFalse(TEXT("staying below the line does not write again"),
		FTriggers::HealthCrossedTheThreshold(0.4f, 0.3f, Half));
	TestFalse(TEXT("staying above the line does not write"),
		FTriggers::HealthCrossedTheThreshold(0.9f, 0.6f, Half));
	TestFalse(TEXT("healing back through the line does not write"),
		FTriggers::HealthCrossedTheThreshold(0.3f, 0.8f, Half));
	TestFalse(TEXT("landing exactly on the line does not write"),
		FTriggers::HealthCrossedTheThreshold(0.9f, 0.5f, Half));

	TestFalse(TEXT("a threshold of zero never writes"),
		FTriggers::HealthCrossedTheThreshold(1.0f, 0.0f, 0.0f));

	// THE THRESHOLD THE GAME USES IS A REAL SHARE OF HEALTH. Zero would switch
	// the check off and one would fire on the first scratch.
	TestTrue(TEXT("the threshold the game uses is above zero"),
		FTriggers::HealthFractionThatForcesAWrite > 0.0f);
	TestTrue(TEXT("and below full health"),
		FTriggers::HealthFractionThatForcesAWrite < 1.0f);

	return true;
}

/**
 * A share of maximum health, including the case where there is no maximum yet.
 *
 * A CHARACTER ON THE FRAME IT SPAWNS HAS NO MAXIMUM HEALTH, because the
 * attributes have not been applied. Dividing by that would be a division by
 * zero; treating it as full health would hide a character that really is nearly
 * dead. Zero is the honest answer and the writer refuses to act on it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveHealthFractionHandlesNoMaximum,
	"Cataclysm.SaveTriggers.AShareOfHealthWithNoMaximumIsZeroRatherThanFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveHealthFractionHandlesNoMaximum::RunTest(const FString&)
{
	using namespace CataclysmSaveTriggerTest;

	TestEqual(TEXT("half of a hundred"), FTriggers::HealthFraction(50.0f, 100.0f), 0.5f);
	TestEqual(TEXT("full"), FTriggers::HealthFraction(100.0f, 100.0f), 1.0f);
	TestEqual(TEXT("none"), FTriggers::HealthFraction(0.0f, 100.0f), 0.0f);

	TestEqual(TEXT("no maximum at all"), FTriggers::HealthFraction(50.0f, 0.0f), 0.0f);
	TestEqual(TEXT("a negative maximum"), FTriggers::HealthFraction(50.0f, -1.0f), 0.0f);

	// CLAMPED AT BOTH ENDS. Overhealing past the maximum is a share of one
	// rather than more than one, so a crossing test cannot be confused by it.
	TestEqual(TEXT("more health than the maximum is still full"),
		FTriggers::HealthFraction(150.0f, 100.0f), 1.0f);
	TestEqual(TEXT("negative health is still none"),
		FTriggers::HealthFraction(-10.0f, 100.0f), 0.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
