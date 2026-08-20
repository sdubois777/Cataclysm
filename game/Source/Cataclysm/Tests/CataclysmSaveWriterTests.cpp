// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The clock, the triggers and the death write. Issue #750.
 *
 * THE ONE PROPERTY THE WHOLE FEATURE RESTS ON is that a death is on disk before
 * the call that wrote it returns. `docs/Save_System_Design.md` section 6 calls it
 * the one rule that cannot be relaxed: for a Hardcore character, death "is
 * written in the same frame the health reaches zero, through the synchronous
 * write, before the death is otherwise processed". An asynchronous death write
 * still writes, a moment later, and that moment is exactly the window a player
 * closing the game would use.
 *
 * THESE TESTS TOUCH THE DISK. Every slot they use is named for this test and
 * deleted again on every path out, including the failing ones.
 */
namespace CataclysmSaveWriterTest
{
	static const TCHAR* SlotPrefix = TEXT("CataclysmAutomationTest_Writer_");
	static constexpr int32 UserIndex = 0;

	/**
	 * A run and a character identifier that name slots nothing else uses.
	 *
	 * GENERATED PER TEST rather than fixed, so two tests running in either order
	 * cannot read each other's files.
	 */
	struct FRun
	{
		FGuid RunId = FGuid::NewGuid();
		FGuid CharacterId = FGuid::NewGuid();
	};

	/**
	 * How long to let a background write finish before deleting its slot.
	 *
	 * **DELETING A FILE OUT FROM UNDER A WRITE IS WHAT MADE THIS SUITE FLAKY.**
	 * An asynchronous write runs on a background thread, so a test that started
	 * one and then cleaned up was racing it: the write landed on a file that had
	 * just been deleted, failed, and `UCataclysmSaveWriter::Write` logged an
	 * Error -- which fails whichever automation test is running when the callback
	 * arrives, and that need not be the test that started the write. It passed
	 * four full runs before it failed.
	 *
	 * TWO SECONDS IS FAR MORE THAN A TWO-KILOBYTE WRITE NEEDS and costs nothing
	 * when the file is already there, which is the ordinary case: the wait ends
	 * as soon as it appears.
	 */
	static constexpr float LongestWaitForAWriteSeconds = 2.0f;

	/** Wait, briefly, for a slot to appear. Returns whether it did. */
	static bool WaitForSlot(const FString& SlotName)
	{
		if (SlotName.IsEmpty())
		{
			return false;
		}

		const double GiveUpAt = FPlatformTime::Seconds() + LongestWaitForAWriteSeconds;
		while (FPlatformTime::Seconds() < GiveUpAt)
		{
			if (UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
			{
				return true;
			}
			FPlatformProcess::Sleep(0.01f);
		}
		return false;
	}

	/**
	 * Delete whatever a writer wrote, once anything in flight has landed.
	 *
	 * @param bWriteWasStarted  whether a write was started that has not been
	 *                          seen to finish. When it was, this waits for the
	 *                          file before deleting it, so the background write
	 *                          cannot arrive afterwards and fail.
	 */
	static void Forget(const UCataclysmSaveWriter* Writer,
					   bool bWriteWasStarted = false)
	{
		if (!Writer)
		{
			return;
		}

		for (const FString& Slot : { Writer->RunSlotName(), Writer->CharacterSlotName() })
		{
			if (Slot.IsEmpty())
			{
				continue;
			}

			if (bWriteWasStarted)
			{
				WaitForSlot(Slot);
			}

			if (UGameplayStatics::DoesSaveGameExist(Slot, UserIndex))
			{
				UGameplayStatics::DeleteGameInSlot(Slot, UserIndex);
			}
		}
	}

	/**
	 * A world with a save writer in it, or null.
	 *
	 * A WORLD SUBSYSTEM EXISTS IN A TEST WORLD because `UWorld::CreateWorld`
	 * reaches `UWorld::InitWorld`, which initialises the subsystem collection.
	 * That is checked rather than assumed: every test below fails plainly if the
	 * writer is not there.
	 */
	static UCataclysmSaveWriter* WriterIn(UWorld* World)
	{
		return UCataclysmSaveWriter::In(World);
	}

	/** The player pawn and the player state its ability system lives on. */
	static ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World)
	{
		ACataclysmPlayerCharacter* Pawn =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector,
														 FRotator::ZeroRotator);
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		if (Pawn && State)
		{
			Pawn->SetPlayerState(State);
			Pawn->OnRep_PlayerState();
		}
		return Pawn;
	}

	static bool SetHealth(AActor* Actor, float Health, float MaxHealth)
	{
		UAbilitySystemComponent* AbilitySystem = const_cast<UAbilitySystemComponent*>(
			UCataclysmTargeting::AbilitySystemOf(Actor));
		if (!AbilitySystem)
		{
			return false;
		}

		const FGameplayAttribute Max = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
		const FGameplayAttribute Now = UCataclysmVitalAttributeSet::GetHealthAttribute();
		if (!AbilitySystem->HasAttributeSetForAttribute(Max)
			|| !AbilitySystem->HasAttributeSetForAttribute(Now))
		{
			return false;
		}

		// MAXIMUM FIRST, THEN CURRENT. The vital attribute set clamps health to
		// the maximum, so raising the current value first would clamp it back
		// down to whatever the old maximum was.
		AbilitySystem->SetNumericAttributeBase(Max, MaxHealth);
		AbilitySystem->SetNumericAttributeBase(Now, Health);
		return true;
	}
}

/**
 * Nothing is written until something says which run is being played.
 *
 * NOT A SWITCH, A CONSEQUENCE. A record is stored in a slot named after a
 * generated identifier, and `FCataclysmSaveStorage::WriteToSlot` refuses an
 * empty slot name on purpose -- writing to one would put every character in the
 * game into a single shared file. Until `BeginRun` supplies an identifier there
 * is nowhere to write, and every trigger is counted rather than logged.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveWriterWaitsForARun,
	"Cataclysm.SaveWriter.NothingIsWrittenUntilSomethingSaysWhichRunItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveWriterWaitsForARun::RunTest(const FString&)
{
	using namespace CataclysmSaveWriterTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	UCataclysmSaveWriter* Writer = WriterIn(World);
	if (!TestNotNull(TEXT("a save writer in the world"), Writer))
	{
		World->DestroyWorld(false);
		return false;
	}

	TestFalse(TEXT("it is not writing yet"), Writer->IsWriting());
	TestTrue(TEXT("and it has no run slot to write to"), Writer->RunSlotName().IsEmpty());

	TestFalse(TEXT("a creature dying writes nothing"),
		Writer->NoteTrigger(ECataclysmSaveTrigger::CreatureDied));
	TestFalse(TEXT("and neither does the character dying"),
		Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied));

	TestEqual(TEXT("nothing was written at all"), Writer->WritesStarted(), 0);
	TestEqual(TEXT("and both triggers were counted as having nowhere to go"),
		Writer->TriggersWithNowhereToWrite(), 2);

	// AND THE TRIGGER IS STILL REMEMBERED, so a caller can tell that the writer
	// saw the event and had nowhere to put it, rather than never being told.
	TestEqual(TEXT("the last trigger is remembered even so"),
		static_cast<int32>(Writer->LastTrigger()),
		static_cast<int32>(ECataclysmSaveTrigger::CharacterDied));

	const FRun Run;
	Writer->BeginRun(Run.RunId, Run.CharacterId, FName(TEXT("Sandbox")), 1);

	TestTrue(TEXT("once a run has begun it is writing"), Writer->IsWriting());
	TestEqual(TEXT("and its run slot is the one the partition rule names"),
		Writer->RunSlotName(), UCataclysmSavePartition::RunSlotName(Run.RunId));
	TestEqual(TEXT("and its character slot likewise"),
		Writer->CharacterSlotName(),
		UCataclysmSavePartition::CharacterSlotName(Run.CharacterId));

	Forget(Writer, /*bWriteWasStarted=*/true);
	World->DestroyWorld(false);
	return true;
}

/**
 * A death is on disk before the call that wrote it returns.
 *
 * **THIS IS THE TEST THE FEATURE EXISTS FOR.** Section 6 names one rule that
 * cannot be relaxed and this is it. A write handed to a background thread would
 * still finish, a moment later, and the moment later is the window a player
 * closing the game would use -- which is the escape the project owner set out to
 * close on 2026-08-20.
 *
 * TWO THINGS ARE CHECKED AND BOTH ARE NEEDED. That the write was counted as a
 * synchronous one, which fails deterministically if the rule is relaxed, and
 * that the file is really there the instant the call returns, which is the thing
 * a player would actually notice.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveWriterDeathIsOnDiskImmediately,
	"Cataclysm.SaveWriter.ADeathIsOnDiskBeforeTheCallThatWroteItReturns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveWriterDeathIsOnDiskImmediately::RunTest(const FString&)
{
	using namespace CataclysmSaveWriterTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	UCataclysmSaveWriter* Writer = WriterIn(World);
	if (!TestNotNull(TEXT("a save writer in the world"), Writer))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FRun Run;
	Writer->BeginRun(Run.RunId, Run.CharacterId, FName(TEXT("Sandbox")), 1);
	Forget(Writer);

	if (!TestFalse(TEXT("there is nothing in the slot to begin with"),
				   UGameplayStatics::DoesSaveGameExist(Writer->RunSlotName(), UserIndex)))
	{
		Forget(Writer);
		World->DestroyWorld(false);
		return false;
	}

	const bool bWrote = Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);

	// NOTHING BETWEEN THE CALL ABOVE AND THE CHECK BELOW. No tick, no sleep, no
	// flush. If the write were handed to a background thread this would be a
	// race, and it is not allowed to be one.
	const bool bOnDisk =
		UGameplayStatics::DoesSaveGameExist(Writer->RunSlotName(), UserIndex);

	TestTrue(TEXT("the death write reported that it wrote"), bWrote);
	TestTrue(TEXT("the file is on disk the instant the call returns"), bOnDisk);
	TestEqual(TEXT("and it was counted as a synchronous write"),
		Writer->SynchronousWrites(), 1);

	Forget(Writer, /*bWriteWasStarted=*/true);
	World->DestroyWorld(false);
	return true;
}

/**
 * Everything except a death is handed over rather than written on the spot.
 *
 * THE CONTRAST WITH THE TEST ABOVE IS THE POINT. A creature dying starts a write
 * and is not counted as a synchronous one; the character dying is. Section 6:
 * "Everything else may be written asynchronously."
 *
 * WHAT IS NOT CHECKED HERE is whether the asynchronous file has arrived, and
 * that is deliberate: asserting that it has NOT arrived would be asserting a
 * race, and asserting that it HAS would be waiting on a background thread.
 * What is checked is the classification, which is what decides the behaviour.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveWriterOtherWritesAreHandedOver,
	"Cataclysm.SaveWriter.EverythingExceptADeathIsHandedOverRatherThanWrittenNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveWriterOtherWritesAreHandedOver::RunTest(const FString&)
{
	using namespace CataclysmSaveWriterTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	UCataclysmSaveWriter* Writer = WriterIn(World);
	if (!TestNotNull(TEXT("a save writer in the world"), Writer))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FRun Run;
	Writer->BeginRun(Run.RunId, Run.CharacterId, FName(TEXT("Sandbox")), 1);
	Forget(Writer);

	TestTrue(TEXT("a creature dying starts a write"),
		Writer->NoteTrigger(ECataclysmSaveTrigger::CreatureDied));
	TestEqual(TEXT("one write was started"), Writer->WritesStarted(), 1);
	TestEqual(TEXT("and none of them was synchronous"), Writer->SynchronousWrites(), 0);

	Forget(Writer, /*bWriteWasStarted=*/true);
	World->DestroyWorld(false);
	return true;
}

/**
 * The run record is written at most once a frame, unless it is a death.
 *
 * FOUR CREATURES KILLED BY ONE BLOW RAISE FOUR TRIGGERS IN ONE FRAME, which is
 * ordinary rather than unusual. Each would otherwise build the whole floor into
 * JSON and hand it over separately, and every one of those writes but the last
 * would be overwritten before it reached the disk. The state the last one writes
 * is the state all four would have written, because the gather runs after all of
 * them are dead.
 *
 * A DEATH IGNORES THE LIMIT, which is what the second half of this checks. The
 * one rule section 6 does not relax cannot be skipped because something else
 * wrote in the same frame.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveWriterRunRecordOncePerFrame,
	"Cataclysm.SaveWriter.TheRunRecordIsWrittenOncePerFrameUnlessItIsADeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveWriterRunRecordOncePerFrame::RunTest(const FString&)
{
	using namespace CataclysmSaveWriterTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	UCataclysmSaveWriter* Writer = WriterIn(World);
	if (!TestNotNull(TEXT("a save writer in the world"), Writer))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FRun Run;
	Writer->BeginRun(Run.RunId, Run.CharacterId, FName(TEXT("Sandbox")), 1);
	Forget(Writer);

	TestTrue(TEXT("the first creature to die starts a write"),
		Writer->NoteTrigger(ECataclysmSaveTrigger::CreatureDied));
	TestFalse(TEXT("the second in the same frame does not"),
		Writer->NoteTrigger(ECataclysmSaveTrigger::CreatureDied));
	TestFalse(TEXT("nor the third"),
		Writer->NoteTrigger(ECataclysmSaveTrigger::CreatureDied));
	TestFalse(TEXT("nor a fight starting in the same frame"),
		Writer->NoteTrigger(ECataclysmSaveTrigger::FightStarted));

	TestEqual(TEXT("so one write was started for four events"),
		Writer->WritesStarted(), 1);

	// AND A DEATH IN THE SAME FRAME WRITES ANYWAY.
	TestTrue(TEXT("the character dying writes despite the limit"),
		Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied));
	TestEqual(TEXT("which makes two writes"), Writer->WritesStarted(), 2);
	TestEqual(TEXT("one of them synchronous"), Writer->SynchronousWrites(), 1);

	Forget(Writer, /*bWriteWasStarted=*/true);
	World->DestroyWorld(false);
	return true;
}

/**
 * The clock writes after its interval and not before.
 *
 * WHAT THE CLOCK IS FOR is the case where nothing happened: everything that
 * happens inside a fight raises a trigger of its own, so this is the gap a
 * player loses after walking around a city and quitting.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveWriterClockWaits,
	"Cataclysm.SaveWriter.TheClockWritesAfterItsIntervalAndNotBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveWriterClockWaits::RunTest(const FString&)
{
	using namespace CataclysmSaveWriterTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	UCataclysmSaveWriter* Writer = WriterIn(World);
	if (!TestNotNull(TEXT("a save writer in the world"), Writer))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FRun Run;
	Writer->BeginRun(Run.RunId, Run.CharacterId, FName(TEXT("Sandbox")), 1);
	Forget(Writer);

	const float Interval = UCataclysmSaveTriggers::SecondsBetweenClockWrites;

	// A HAIR UNDER THE INTERVAL, IN ONE STEP. Splitting it into many small ticks
	// would test the same thing and would also be a test of floating point
	// accumulation, which is not what this is about.
	Writer->Tick(Interval - 0.01f);
	TestEqual(TEXT("nothing is written before the interval is up"),
		Writer->WritesStarted(), 0);

	Writer->Tick(0.02f);
	TestEqual(TEXT("and one write when it is"), Writer->WritesStarted(), 1);
	TestEqual(TEXT("and the clock says so"),
		static_cast<int32>(Writer->LastTrigger()),
		static_cast<int32>(ECataclysmSaveTrigger::Clock));

	// AND THE CLOCK STARTS AGAIN rather than writing on every tick from then on.
	TestTrue(TEXT("the clock was restarted"),
		Writer->SecondsSinceTheClockWrote() < Interval);

	Forget(Writer, /*bWriteWasStarted=*/true);
	World->DestroyWorld(false);
	return true;
}

/**
 * Health falling through the line writes, and health above it does not.
 *
 * WHAT THIS TEST CANNOT CHECK, SAID PLAINLY. A check written as "is health below
 * the line" rather than "did it just fall through" would write the whole floor on
 * every frame a wounded character stood still. **This test cannot see that**, and
 * breaking the rule on purpose proved it: an automation test runs inside a single
 * frame, so the writer's once-a-frame limit suppresses the extra writes and the
 * count stays at one either way.
 *
 * `Cataclysm.SaveTriggers.HealthWritesOnTheWayThroughTheLineAndNotWhileBelowIt`
 * is what guards the crossing rule, and it does catch that break. What is checked
 * here is the half a rule about plain numbers cannot reach: that the writer looks
 * at the right character's health at all, and writes for the right reason.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveWriterHealthThreshold,
	"Cataclysm.SaveWriter.HealthFallingThroughTheLineIsWhatMakesTheWriterWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveWriterHealthThreshold::RunTest(const FString&)
{
	using namespace CataclysmSaveWriterTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	UCataclysmSaveWriter* Writer = WriterIn(World);
	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);

	if (!Writer || !Player)
	{
		AddError(TEXT("a writer and a player character were both needed"));
		World->DestroyWorld(false);
		return false;
	}

	constexpr float MaxHealth = 1000.0f;
	if (!SetHealth(Player, MaxHealth, MaxHealth))
	{
		AddError(TEXT("the player's health could not be set, so this would pass for "
					  "the wrong reason"));
		World->DestroyWorld(false);
		return false;
	}

	const FRun Run;
	Writer->BeginRun(Run.RunId, Run.CharacterId, FName(TEXT("Sandbox")), 1);
	Forget(Writer);

	// A SMALL TICK, SO THE CLOCK CANNOT BE WHAT WRITES. Everything below has to
	// be attributable to health.
	constexpr float Sliver = 0.01f;

	Writer->Tick(Sliver);
	TestEqual(TEXT("a character at full health writes nothing"),
		Writer->WritesStarted(), 0);

	// HURT, BUT NOT THROUGH THE LINE.
	const float Line = UCataclysmSaveTriggers::HealthFractionThatForcesAWrite;
	SetHealth(Player, MaxHealth * (Line + 0.1f), MaxHealth);
	Writer->Tick(Sliver);
	TestEqual(TEXT("hurt but still above the line writes nothing"),
		Writer->WritesStarted(), 0);

	// AND THROUGH IT.
	SetHealth(Player, MaxHealth * (Line - 0.1f), MaxHealth);
	Writer->Tick(Sliver);
	TestEqual(TEXT("falling through the line writes once"), Writer->WritesStarted(), 1);
	TestEqual(TEXT("and says that is why"),
		static_cast<int32>(Writer->LastTrigger()),
		static_cast<int32>(ECataclysmSaveTrigger::HealthCrossedThreshold));

	// AND STAYING THERE DOES NOT WRITE AGAIN, IN THIS FRAME. Three more ticks at
	// a lower share each time. Read this for what it is: inside one frame the
	// writer's once-a-frame limit would suppress these even if the crossing rule
	// were broken, so this line does not prove the crossing rule. The trigger
	// test named in the comment above is what does.
	SetHealth(Player, MaxHealth * (Line - 0.2f), MaxHealth);
	Writer->Tick(Sliver);
	SetHealth(Player, MaxHealth * (Line - 0.3f), MaxHealth);
	Writer->Tick(Sliver);
	SetHealth(Player, MaxHealth * (Line - 0.4f), MaxHealth);
	Writer->Tick(Sliver);

	TestEqual(TEXT("staying below the line writes nothing further this frame"),
		Writer->WritesStarted(), 1);

	Forget(Writer, /*bWriteWasStarted=*/true);
	World->DestroyWorld(false);
	return true;
}

/**
 * The game mode is what turns the writer on, and until it does nothing is
 * written.
 *
 * WHY THIS TEST EXISTS AT ALL. Every other test in this file calls
 * `BeginRun` itself, so all of them would go on passing with the game never
 * calling it -- and the save system would be built, tested, and switched off in
 * the running game with nothing saying so.
 *
 * IT CALLS `BeginSavingThisRun` RATHER THAN `StartPlay`. StartPlay wants a
 * player controller and a pawn and cannot be reached from an automation test,
 * which is the same reason `CataclysmSandboxTests.cpp` calls the three
 * spawners directly. The one line StartPlay contributes is the call itself.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveWriterTurnedOnByTheGameMode,
	"Cataclysm.SaveWriter.TheGameModeIsWhatTurnsTheWriterOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveWriterTurnedOnByTheGameMode::RunTest(const FString&)
{
	using namespace CataclysmSaveWriterTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	UCataclysmSaveWriter* Writer = WriterIn(World);
	ACataclysmGameMode* GameMode = World->SpawnActor<ACataclysmGameMode>();

	if (!Writer || !GameMode)
	{
		AddError(TEXT("a writer and a game mode were both needed"));
		World->DestroyWorld(false);
		return false;
	}

	TestFalse(TEXT("the writer is off before the game mode says anything"),
		Writer->IsWriting());

	TestTrue(TEXT("the game mode found the writer and told it"),
		GameMode->BeginSavingThisRun());

	TestTrue(TEXT("and the writer is now writing"), Writer->IsWriting());
	TestFalse(TEXT("it has a run slot to write to"),
		Writer->RunSlotName().IsEmpty());
	TestFalse(TEXT("and a character slot"),
		Writer->CharacterSlotName().IsEmpty());

	// AND IT REALLY WRITES. Being switched on is not the same as writing, and
	// a test that stopped at the flag would pass with the storage layer gone.
	Forget(Writer);
	TestTrue(TEXT("a death now reaches the disk"),
		Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied));
	TestTrue(TEXT("and the file is there"),
		UGameplayStatics::DoesSaveGameExist(Writer->RunSlotName(), UserIndex));

	// TWO SESSIONS DO NOT SHARE A SLOT. Each run gets its own identifier, so
	// one session cannot overwrite another's record -- which is what a fixed
	// slot name would do.
	const FString FirstRun = Writer->RunSlotName();
	GameMode->BeginSavingThisRun();
	TestNotEqual(TEXT("a second run is written somewhere else"),
		Writer->RunSlotName(), FirstRun);

	Forget(Writer);
	if (UGameplayStatics::DoesSaveGameExist(FirstRun, UserIndex))
	{
		UGameplayStatics::DeleteGameInSlot(FirstRun, UserIndex);
	}

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
