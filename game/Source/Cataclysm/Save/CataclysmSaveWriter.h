// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Subsystems/WorldSubsystem.h"
#include "CataclysmSaveWriter.generated.h"

class UCataclysmCharacterSave;
class UCataclysmSaveRecord;
class UCataclysmRunSave;
struct FCataclysmSavedFloor;

/**
 * The thing that decides a save is due and writes it.
 *
 * **THE GAME SAVES ITSELF AND THERE IS NO MANUAL SAVE.** The project owner set
 * that on 2026-08-20: a Hardcore character must not be able to leave a losing
 * boss fight by closing the game, and a player who is cut off must come back
 * where they were. `docs/Save_System_Design.md` section 6 is the whole of it.
 *
 * A WORLD SUBSYSTEM, so there is exactly one per world and anything with a world
 * pointer can reach it without being handed one. `NoteTriggerIn` below is the
 * one-line form every call site uses.
 *
 * IT WRITES NOTHING UNTIL SOMETHING SAYS WHICH RUN IS BEING PLAYED. A record is
 * stored in a slot named after a generated identifier -- see
 * `UCataclysmSavePartition` -- and until `BeginRun` supplies one there is no
 * slot to write to. That is not a switch: `FCataclysmSaveStorage::WriteToSlot`
 * refuses an empty slot name on purpose, because writing to one would put every
 * character in the game into a single shared file.
 *
 * WHAT IS DELIBERATELY NOT HERE. Loading. Nothing reads a save back at start-up,
 * because nothing chooses between starting a new run and continuing one -- there
 * is no such screen and no such design. The storage layer can read a record
 * today; what is missing is whoever decides which one.
 */
UCLASS()
class CATACLYSM_API UCataclysmSaveWriter : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Reaching it.

	/** The writer for a world, or null when there is no world. */
	static UCataclysmSaveWriter* In(const UWorld* World);

	/**
	 * Tell the writer something happened, from anywhere that has a world.
	 *
	 * SAFE WHEN THERE IS NO WRITER AT ALL, which is what every call site needs:
	 * an enemy dying in a test world with no subsystem must not have to check.
	 */
	static void NoteTriggerIn(const UWorld* World, ECataclysmSaveTrigger Trigger);

	//~ Being told what to do.

	/**
	 * Say which run and which character this session is playing, and where they
	 * are.
	 *
	 * NOTHING BUT THE GAME MODE CALLS THIS, and what it passes is a run started
	 * fresh each session. **There is no new-game or continue flow**, so a run
	 * begun here is written and never read back. That is the honest state of it:
	 * the writing half of the save system is built and the choosing half is not.
	 */
	void BeginRun(const FGuid& InRunId, const FGuid& InCharacterId,
				  FName InDungeon, int32 InFloor);

	/** Whether a run has been begun, so there is somewhere to write. */
	bool IsWriting() const { return RunId.IsValid() && CharacterId.IsValid(); }

	/**
	 * Something happened that the design says is worth writing for.
	 *
	 * A DEATH IS WRITTEN BEFORE THIS RETURNS. Everything else is handed to the
	 * save system and this returns immediately.
	 *
	 * @return whether a write was started
	 */
	bool NoteTrigger(ECataclysmSaveTrigger Trigger);

	/**
	 * Which floor the party is on.
	 *
	 * `ACataclysmDungeonGameMode::GoToFloor` calls it when the player takes the
	 * stairs down. It was written when the save system was built and nothing
	 * called it until then, because nothing changed floors.
	 *
	 * AND WHETHER THAT FLOOR IS THE DARK FLOOR A FALL LEADS TO, with the floor fallen from. Issues #1820 and #41,
	 * 2026-10-09. The dark floor carries the number of the floor its stairs lead to, so leaving it by its stairs
	 * changes neither the dungeon nor the number. This returns without writing only when all four are what it
	 * holds, so that change is written too. Both default to "not the dark floor", and a caller that passes
	 * neither is answered exactly as before.
	 */
	void SetFloor(FName InDungeon, int32 InFloor, bool bInOnTheDarkFloor = false,
				  int32 InDarkFloorFellFrom = 0);

	/**
	 * The floor as a run record written now would hold it: what `FCataclysmSaveGather::FloorFrom` reads off the
	 * world, with the dark floor's two fields. `WriteTheRunRecord` fills the record's floor with this and nothing
	 * else, so a test can read what a write stores without waiting for one: the run record is written at most
	 * once a frame, and a test's whole body is one frame. False, and nothing filled, with no world.
	 */
	bool FloorRecordNow(FCataclysmSavedFloor& OutFloor) const;

	/** Whether the writer holds the floor as the dark floor, and the floor fallen from. For a test. */
	bool HoldsTheDarkFloor() const { return bOnTheDarkFloor; }
	int32 HoldsTheFloorFallenFrom() const { return DarkFloorFellFrom; }

	/** How many times `SetFloor` was told something it did not already hold, and so raised its trigger. */
	int32 FloorChangesNoted() const { return FloorChangesNotedCount; }

	//~ The clock, and health.

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Seconds since the clock last wrote. */
	float SecondsSinceTheClockWrote() const { return SecondsSinceClockWrite; }

	/** The share of maximum health the character had when it was last looked at. */
	float LastHealthFractionSeen() const { return LastHealthFraction; }

	//~ What happened, for a test and for a log line.

	/** How many writes have been started, of any kind. */
	int32 WritesStarted() const { return StartedCount; }

	/** How many of those were written before the frame ended. */
	int32 SynchronousWrites() const { return SynchronousCount; }

	/** How many triggers were refused because no run has been begun. */
	int32 TriggersWithNowhereToWrite() const { return NowhereToWriteCount; }

	/** The last trigger that was noted, whether or not it wrote. */
	ECataclysmSaveTrigger LastTrigger() const { return LastTriggerSeen; }

	/** The run record as it was last built. Null until something has been written. */
	const UCataclysmRunSave* LastRunRecord() const { return LastRun; }

	//~ Which slots it writes to.

	FString RunSlotName() const;
	FString CharacterSlotName() const;

	/**
	 * Which player this is, for the save system's own user index.
	 *
	 * ZERO, AND THERE IS NO SECOND PLAYER TO BE ANYTHING ELSE. Split-screen is
	 * not in the design; co-operative play is networked, so each player has
	 * their own machine and their own index zero.
	 */
	static constexpr int32 UserIndex = 0;

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	/** Build the run record from the world as it stands, and write it. */
	bool WriteTheRunRecord(ECataclysmSaveTrigger Trigger);

	/** Build the character record from the world as it stands, and write it. */
	bool WriteTheCharacterRecord(ECataclysmSaveTrigger Trigger);

	/** Send one record to its slot, synchronously or not as the trigger requires. */
	bool Write(const UCataclysmSaveRecord* Record, const FString& SlotName,
			   ECataclysmSaveTrigger Trigger);

	/** Look at the character's health and write if it fell through the line. */
	void CheckHealth();

	UPROPERTY(Transient)
	TObjectPtr<UCataclysmRunSave> LastRun;

	UPROPERTY(Transient)
	TObjectPtr<UCataclysmCharacterSave> LastCharacter;

	FGuid RunId;
	FGuid CharacterId;
	FName Dungeon;
	int32 Floor = 0;

	/** Whether the floor is the dark floor a fall leads to, and the floor fallen from. See `SetFloor`. */
	bool bOnTheDarkFloor = false;
	int32 DarkFloorFellFrom = 0;
	int32 FloorChangesNotedCount = 0;

	float SecondsSinceClockWrite = 0.0f;

	/**
	 * The character's share of maximum health when it was last looked at.
	 *
	 * STARTS AT ONE RATHER THAN ZERO, so that a character which begins the
	 * session below the threshold writes on the first tick rather than never.
	 * Starting at zero would mean the "was above, is below" test never saw a
	 * crossing for a character that was already hurt.
	 */
	float LastHealthFraction = 1.0f;

	/**
	 * The frame the run record was last written on.
	 *
	 * WHAT IT IS FOR: several creatures dying in one frame is ordinary, and
	 * every one of them raises a trigger. Without this each would build the
	 * whole floor into JSON and hand it over separately, and every one of
	 * those writes but the last would be overwritten by the next before it
	 * reached the disk.
	 *
	 * A SYNCHRONOUS WRITE IGNORES IT. A death must be written whatever else
	 * happened in the frame, which is the one rule section 6 does not relax.
	 */
	uint64 LastRunWriteFrame = 0;

	int32 StartedCount = 0;
	int32 SynchronousCount = 0;
	int32 NowhereToWriteCount = 0;

	ECataclysmSaveTrigger LastTriggerSeen = ECataclysmSaveTrigger::Clock;
};
