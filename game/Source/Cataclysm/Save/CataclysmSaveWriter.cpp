// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveWriter.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/World.h"
#include "Player/CataclysmGameInstance.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"

UCataclysmSaveWriter* UCataclysmSaveWriter::In(const UWorld* World)
{
	return World ? World->GetSubsystem<UCataclysmSaveWriter>() : nullptr;
}

void UCataclysmSaveWriter::NoteTriggerIn(const UWorld* World,
										 ECataclysmSaveTrigger Trigger)
{
	// NULL IS THE ORDINARY CASE AT A CALL SITE. An enemy dying in a world built
	// by a test that does not care about saving has no writer, and neither does
	// an actor being torn down as a level unloads. Every call site is one line
	// because of this check.
	if (UCataclysmSaveWriter* Writer = In(World))
	{
		Writer->NoteTrigger(Trigger);
	}
}

void UCataclysmSaveWriter::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SecondsSinceClockWrite = 0.0f;
}

void UCataclysmSaveWriter::Deinitialize()
{
	LastRun = nullptr;
	LastCharacter = nullptr;
	Super::Deinitialize();
}

TStatId UCataclysmSaveWriter::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCataclysmSaveWriter, STATGROUP_Tickables);
}

void UCataclysmSaveWriter::BeginRun(const FGuid& InRunId, const FGuid& InCharacterId,
									FName InDungeon, int32 InFloor)
{
	RunId = InRunId;
	CharacterId = InCharacterId;
	Dungeon = InDungeon;
	Floor = InFloor;

	// THE CLOCK STARTS FROM THE BEGINNING OF THE RUN rather than from whenever
	// the subsystem was made, so the first write is one interval into play and
	// not one interval after the level loaded.
	SecondsSinceClockWrite = 0.0f;
	LastHealthFraction = 1.0f;

	UE_LOG(LogCataclysm, Log,
		TEXT("The save writer is writing run %s and character %s."),
		*RunId.ToString(EGuidFormats::Digits),
		*CharacterId.ToString(EGuidFormats::Digits));
}

void UCataclysmSaveWriter::SetFloor(FName InDungeon, int32 InFloor)
{
	if (Dungeon == InDungeon && Floor == InFloor)
	{
		return;
	}

	Dungeon = InDungeon;
	Floor = InFloor;

	NoteTrigger(ECataclysmSaveTrigger::ChangedFloor);
}

FString UCataclysmSaveWriter::RunSlotName() const
{
	return UCataclysmSavePartition::RunSlotName(RunId);
}

FString UCataclysmSaveWriter::CharacterSlotName() const
{
	return UCataclysmSavePartition::CharacterSlotName(CharacterId);
}

bool UCataclysmSaveWriter::NoteTrigger(ECataclysmSaveTrigger Trigger)
{
	LastTriggerSeen = Trigger;

	if (!IsWriting())
	{
		// NOTHING HAS SAID WHICH RUN THIS IS, so there is no slot. Counted
		// rather than logged, because in the sandbox this would be every kill.
		++NowhereToWriteCount;
		return false;
	}

	bool bStartedAnything = false;

	if (UCataclysmSaveTriggers::WritesTheRunRecord(Trigger))
	{
		bStartedAnything |= WriteTheRunRecord(Trigger);
	}

	if (UCataclysmSaveTriggers::WritesTheCharacterRecord(Trigger))
	{
		bStartedAnything |= WriteTheCharacterRecord(Trigger);
	}

	// THE ACCOUNT RECORD IS NOT WRITTEN HERE, AND THE REASON IS NOT THAT IT IS
	// FORGOTTEN. `UCataclysmSaveTriggers::WritesTheAccountRecord` answers true
	// for exactly one trigger, and nothing raises that trigger, because nothing
	// in the game changes an account record: there are no banked empire upgrade
	// points and no stash. The rule is built and has nothing to act on yet.

	// AND THE CLOCK IS RESET BY ANY WRITE, not only by its own. A run record
	// written because a creature died is as current as one written by the
	// clock, so restarting the interval is what stops the two writing twice in
	// quick succession for the same state.
	if (bStartedAnything)
	{
		SecondsSinceClockWrite = 0.0f;
	}

	return bStartedAnything;
}

bool UCataclysmSaveWriter::WriteTheRunRecord(ECataclysmSaveTrigger Trigger)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// ONCE A FRAME, UNLESS IT IS A DEATH. Four creatures killed by one blow
	// raise four triggers in one frame, and the first three writes would each
	// be replaced by the next before reaching the disk. The state the last one
	// writes is the state all four would have written, because the gather runs
	// after all of them have died.
	const bool bMustWriteNow =
		UCataclysmSaveTriggers::MustBeWrittenBeforeTheFrameEnds(Trigger);
	if (!bMustWriteNow && LastRunWriteFrame == GFrameCounter && StartedCount > 0)
	{
		return false;
	}
	LastRunWriteFrame = GFrameCounter;

	if (!LastRun)
	{
		LastRun = NewObject<UCataclysmRunSave>(this);
	}

	LastRun->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;
	LastRun->RunId = RunId;
	LastRun->Characters = { CharacterId };
	LastRun->Floor = FCataclysmSaveGather::FloorFrom(*World, Dungeon, Floor, CharacterId);

	// AND THE EMPIRE'S CLOCK, WHEN THERE IS A RUN TO READ IT FROM. Both halves:
	// the whole days and the time spent that has not yet added up to one. A save
	// that kept only the first loses up to just under a day of empire progress
	// every time it is written, which became possible when a dungeon's walk time
	// was separated from its floor count. Issue #1299.
	//
	// THIS COMMENT USED TO SAY THE DAY WAS NOT SET, BECAUSE THE EMPIRE MODULE
	// WAS "a build file with nothing in it". That was true when it was written
	// and stopped being true when `UCataclysmDayClock` landed. The source it
	// said did not exist is `UCataclysmEmpireRun::Day`.
	//
	// NOT STARTING A RUN TO SAVE ONE. `EmpireRunFor` is asked not to begin a
	// campaign, so a save written while nobody is playing one leaves these
	// fields alone rather than writing a zero over what the record already held.
	// That was the one good half of the old behaviour and it is kept.
	if (const UCataclysmEmpireRun* Run =
			UCataclysmGameInstance::EmpireRunFor(World, /*bStartIfNone*/ false))
	{
		FCataclysmSaveGather::RunClockFrom(*Run, *LastRun);

		// AND THE 25 CITIES: what each has left, what it has bought, and whether
		// it still stands. Only the mutable half of each one is written; see
		// `FCataclysmCity`. Issue #1307.
		//
		// THE DUNGEONS AND THE SURGE SCHEDULE ARE STILL NOT WRITTEN, which is
		// the rest of that issue. A restore would put the cities back and find
		// no dungeons standing on them.
		FCataclysmSaveGather::CitiesFrom(*Run, *LastRun);
	}

	return Write(LastRun, RunSlotName(), Trigger);
}

bool UCataclysmSaveWriter::WriteTheCharacterRecord(ECataclysmSaveTrigger Trigger)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const ACataclysmPlayerCharacter* Character = FCataclysmSaveGather::CharacterIn(*World);
	if (!Character)
	{
		return false;
	}

	if (!LastCharacter)
	{
		LastCharacter = NewObject<UCataclysmCharacterSave>(this);
	}

	LastCharacter->SchemaVersion = UCataclysmCharacterSave::SchemaVersionNow;
	LastCharacter->CharacterId = CharacterId;

	if (!FCataclysmSaveGather::CharacterFrom(*Character, *LastCharacter))
	{
		return false;
	}

	return Write(LastCharacter, CharacterSlotName(), Trigger);
}

bool UCataclysmSaveWriter::Write(const UCataclysmSaveRecord* Record,
								 const FString& SlotName,
								 ECataclysmSaveTrigger Trigger)
{
	if (!Record || SlotName.IsEmpty())
	{
		return false;
	}

	FString Error;

	if (UCataclysmSaveTriggers::MustBeWrittenBeforeTheFrameEnds(Trigger))
	{
		// THE ONE RULE THAT CANNOT BE RELAXED. Section 6: a death is written "in
		// the same frame the health reaches zero, through the synchronous write,
		// before the death is otherwise processed". An asynchronous write here
		// would finish a moment later, and that moment is exactly the window a
		// player closing the game would use -- which is the escape this whole
		// feature exists to close.
		const bool bWritten =
			FCataclysmSaveStorage::WriteToSlot(Record, SlotName, UserIndex, Error);

		++StartedCount;
		++SynchronousCount;

		if (!bWritten)
		{
			UE_LOG(LogCataclysm, Error,
				TEXT("A %s record could not be written to '%s' for %s: %s"),
				*Record->RecordType().ToString(), *SlotName,
				UCataclysmSaveTriggers::NameOf(Trigger), *Error);
		}

		return bWritten;
	}

	const bool bStarted = FCataclysmSaveStorage::WriteToSlotAsync(
		Record, SlotName, UserIndex,
		[Type = Record->RecordType(), SlotName, Trigger](bool bSucceeded)
		{
			if (!bSucceeded)
			{
				UE_LOG(LogCataclysm, Error,
					TEXT("A %s record was handed to the save system for '%s' after "
						 "%s and the write failed."),
					*Type.ToString(), *SlotName,
					UCataclysmSaveTriggers::NameOf(Trigger));
			}
		},
		Error);

	if (bStarted)
	{
		++StartedCount;
	}
	else
	{
		UE_LOG(LogCataclysm, Error,
			TEXT("A %s record could not be handed to the save system for '%s' "
				 "after %s: %s"),
			*Record->RecordType().ToString(), *SlotName,
			UCataclysmSaveTriggers::NameOf(Trigger), *Error);
	}

	return bStarted;
}

void UCataclysmSaveWriter::CheckHealth()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const ACataclysmPlayerCharacter* Character = FCataclysmSaveGather::CharacterIn(*World);
	if (!Character)
	{
		return;
	}

	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Character);
	if (!AbilitySystem)
	{
		return;
	}

	const float Health = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	const float MaxHealth = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());

	if (MaxHealth <= 0.0f)
	{
		// THE ATTRIBUTES HAVE NOT ARRIVED YET. A character on the frame it
		// spawns has no maximum health, and treating that as zero health would
		// fire the threshold on the first tick of every session.
		return;
	}

	const float Now = UCataclysmSaveTriggers::HealthFraction(Health, MaxHealth);
	const bool bCrossed = UCataclysmSaveTriggers::HealthCrossedTheThreshold(
		LastHealthFraction, Now,
		UCataclysmSaveTriggers::HealthFractionThatForcesAWrite);

	LastHealthFraction = Now;

	if (bCrossed)
	{
		NoteTrigger(ECataclysmSaveTrigger::HealthCrossedThreshold);
	}
}

void UCataclysmSaveWriter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsWriting())
	{
		return;
	}

	// HEALTH IS SAMPLED ONCE A FRAME RATHER THAN CHECKED INSIDE THE DAMAGE
	// CALCULATION, and that is a deliberate choice rather than the easy one.
	// Section 6 calls health falling through a threshold an event that writes
	// "immediately"; a check here is at most one frame later than the blow that
	// caused it, and it keeps the save system out of the hot path that every
	// hit in the game runs through. The alternative -- a call from
	// `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` -- would put a
	// map lookup and a branch on every point of damage dealt by anything.
	CheckHealth();

	SecondsSinceClockWrite += DeltaTime;

	if (UCataclysmSaveTriggers::ClockHasElapsed(
			SecondsSinceClockWrite,
			UCataclysmSaveTriggers::SecondsBetweenClockWrites))
	{
		// NoteTrigger RESETS THE CLOCK WHEN IT WRITES. It is reset here as well
		// for the case where the write did not start -- no character in the
		// world, for instance -- so a failing clock write does not retry on
		// every frame from then on.
		SecondsSinceClockWrite = 0.0f;
		NoteTrigger(ECataclysmSaveTrigger::Clock);
	}
}
