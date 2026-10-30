// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveLoad.h"

#include "Cataclysm.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveWriter.h"
#include "UObject/Package.h"

namespace CataclysmSaveLoad
{
	/**
	 * Whether the slot holds a record written by a newer build than this one, and its version.
	 *
	 * READ FROM THE FILE'S OWN VERSION FIELD rather than from the read's result, because the result for a newer file
	 * is the one the migration chain gives every refusal. Section 5 rule 1 puts the version first so that it can be
	 * read without the rest.
	 */
	bool IsNewerThanThisBuild(const FString& SlotName, int32 ThisBuildReads, int32& OutVersion)
	{
		// ASKED WHETHER IT EXISTS FIRST. Reading a file that is not there logs a warning from the engine, and a warning
		// fails whichever automation test is running.
		TArray<uint8> Bytes;
		if (SlotName.IsEmpty()
			|| !UGameplayStatics::DoesSaveGameExist(SlotName, UCataclysmSaveWriter::UserIndex)
			|| !UGameplayStatics::LoadDataFromSlot(Bytes, SlotName, UCataclysmSaveWriter::UserIndex)
			|| Bytes.Num() == 0)
		{
			return false;
		}

		FString Json;
		FFileHelper::BufferToString(Json, Bytes.GetData(), Bytes.Num());
		return FCataclysmSaveStorage::ReadSchemaVersion(Json, OutVersion) && OutVersion > ThisBuildReads;
	}

	FCataclysmLoadReport Refused(const FString& Why)
	{
		FCataclysmLoadReport Report;
		Report.Outcome = ECataclysmLoadOutcome::Refused;
		Report.Why = Why;
		return Report;
	}
}

FCataclysmLoadReport FCataclysmSaveLoad::CharacterAndRun(UWorld& World, ACataclysmPlayerCharacter& Character,
														 const FGuid& CharacterId)
{
	using namespace CataclysmSaveLoad;

	UCataclysmSaveWriter* Writer = UCataclysmSaveWriter::In(&World);
	if (!Writer || !Writer->IsWriting())
	{
		return Refused(TEXT("this world is not saving a run, so nothing names the floor to load onto"));
	}

	UCataclysmGameInstance* Instance = Cast<UCataclysmGameInstance>(World.GetGameInstance());
	if (!Instance)
	{
		return Refused(TEXT("there is no game instance of this game's class to hold the run"));
	}

	if (!CharacterId.IsValid())
	{
		return Refused(TEXT("no character was named"));
	}

	// 1. THE CHARACTER.
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
	FString Message;
	const FString CharacterSlot = UCataclysmSavePartition::CharacterSlotName(CharacterId);
	UCataclysmCharacterSave* SavedCharacter = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::ReadFromSlot(
		CharacterSlot, UCataclysmSaveWriter::UserIndex, UCataclysmCharacterSave::StaticClass(),
		GetTransientPackage(), Result, Message));
	if (!SavedCharacter || !FCataclysmSaveStorage::Succeeded(Result))
	{
		int32 Version = 0;
		if (IsNewerThanThisBuild(CharacterSlot, UCataclysmCharacterSave::SchemaVersionNow, Version))
		{
			return Refused(FString::Printf(
				TEXT("the character record in '%s' was written by a newer build, at version %d where this build reads "
					 "version %d, and is refused rather than guessed at"),
				*CharacterSlot, Version, UCataclysmCharacterSave::SchemaVersionNow));
		}
		return Refused(FString::Printf(TEXT("the character record in '%s' could not be read: %s%s%s"),
									   *CharacterSlot, FCataclysmSaveStorage::Describe(Result),
									   Message.IsEmpty() ? TEXT("") : TEXT(", "), *Message));
	}
	if (SavedCharacter->CharacterId != CharacterId)
	{
		return Refused(FString::Printf(TEXT("the character record in '%s' names another character, %s"),
									   *CharacterSlot, *SavedCharacter->CharacterId.ToString(EGuidFormats::Digits)));
	}

	// 2. THE RUN IT NAMES.
	UCataclysmRunSave* SavedRun = nullptr;
	FString WhyNoRun;
	const FString RunSlot = UCataclysmSavePartition::RunSlotName(SavedCharacter->CurrentRunId);
	if (!SavedCharacter->CurrentRunId.IsValid())
	{
		WhyNoRun = TEXT("the character record names no run");
	}
	else
	{
		SavedRun = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::ReadFromSlot(
			RunSlot, UCataclysmSaveWriter::UserIndex, UCataclysmRunSave::StaticClass(), GetTransientPackage(),
			Result, Message));
		if (!SavedRun || !FCataclysmSaveStorage::Succeeded(Result))
		{
			SavedRun = nullptr;

			// A NEWER RUN REFUSES EVERYTHING, ruled 2026-10-30. A new run begun here would be written over the
			// character's `CurrentRunId` at its next write, and the newer run would be lost to it.
			int32 Version = 0;
			if (IsNewerThanThisBuild(RunSlot, UCataclysmRunSave::SchemaVersionNow, Version))
			{
				return Refused(FString::Printf(
					TEXT("the run record in '%s' was written by a newer build, at version %d where this build reads "
						 "version %d; it is refused, and so is the character, because beginning a new run would "
						 "replace the character's run and lose the newer one"),
					*RunSlot, Version, UCataclysmRunSave::SchemaVersionNow));
			}
			WhyNoRun = Result == ECataclysmSaveLoadResult::SlotIsEmpty
				? FString::Printf(TEXT("there is no run record in '%s'"), *RunSlot)
				: FString::Printf(TEXT("the run record in '%s' could not be read: %s%s%s"), *RunSlot,
								  FCataclysmSaveStorage::Describe(Result), Message.IsEmpty() ? TEXT("") : TEXT(", "),
								  *Message);
		}
		else if (SavedRun->RunId != SavedCharacter->CurrentRunId)
		{
			WhyNoRun = FString::Printf(TEXT("the run record in '%s' names another run, %s"), *RunSlot,
									   *SavedRun->RunId.ToString(EGuidFormats::Digits));
			SavedRun = nullptr;
		}
	}

	// 3. WHERE THE RUN WAS SAVED.
	if (SavedRun)
	{
		// REQUIRED, NOT CONVENIENT. A death's days are charged only while the dungeon is entered, and nothing here
		// enters one, so a character who died inside would stand up without paying. #753's next step enters it.
		if (SavedRun->CurrentDungeonId != INDEX_NONE)
		{
			return Refused(FString::Printf(
				TEXT("the run was saved inside empire dungeon %d, and loading inside a dungeon is not built yet: a "
					 "character who died there would stand up without paying the death's days"),
				SavedRun->CurrentDungeonId));
		}

		if (SavedRun->Floor.IsOccupied()
			&& (SavedRun->Floor.Dungeon != Writer->HoldsTheDungeon() || SavedRun->Floor.Floor != Writer->HoldsTheFloor()))
		{
			return Refused(FString::Printf(
				TEXT("the run was saved on floor %d of '%s', and this level is floor %d of '%s'"),
				SavedRun->Floor.Floor, *SavedRun->Floor.Dungeon.ToString(), Writer->HoldsTheFloor(),
				*Writer->HoldsTheDungeon().ToString()));
		}
	}

	// 4. THE EMPIRE, INTO AN OBJECT NOTHING HOLDS YET. A record saved with no empire run going holds no cities, and
	// it is loaded as that: no empire, which the game starts when something asks for one.
	UCataclysmEmpireRun* RestoredEmpire = nullptr;
	if (SavedRun && SavedRun->Cities.Num() > 0)
	{
		RestoredEmpire = NewObject<UCataclysmEmpireRun>(Instance);
		FString Why;
		if (!FCataclysmSaveApply::RunInto(*SavedRun, *RestoredEmpire, Why))
		{
			WhyNoRun = FString::Printf(TEXT("the run record in '%s' would not go back: %s"), *RunSlot, *Why);
			SavedRun = nullptr;
			RestoredEmpire = nullptr;
		}
	}

	// 5. THE CHARACTER, WHICH REFUSES BEFORE IT CHANGES ANYTHING.
	FCataclysmLoadReport Report;
	{
		FString Why;
		if (!FCataclysmSaveApply::CharacterInto(*SavedCharacter, Character, Report.SkillsRefused, Why))
		{
			return Refused(FString::Printf(TEXT("the character record would not go back: %s"), *Why));
		}
	}

	// NOTHING BELOW REFUSES. The run becomes the game's, a new one when it could not be read.
	FGuid RunId;
	if (SavedRun)
	{
		RunId = SavedRun->RunId;
		Instance->EmpireRun = RestoredEmpire;
		if (ACataclysmPlayerState* State = Character.GetPlayerState<ACataclysmPlayerState>())
		{
			State->RestoreRunKills(SavedRun->RunKills);
		}
		if (SavedRun->Floor.IsOccupied())
		{
			Report.CreaturesRestored = FCataclysmSaveApply::FloorInto(World, SavedRun->Floor).Creatures;
		}
	}
	else
	{
		RunId = FGuid::NewGuid();
		const int32 Seed = static_cast<int32>(FDateTime::Now().GetTicks() & 0x7FFFFFFF);
		Instance->BeginEmpireRun(Seed, ECataclysmSurgeMode::Static,
								 static_cast<int32>(SavedCharacter->Partition.Lethality));
	}

	// THE WRITER GOES ON INTO THE LOADED SLOTS, FROM THE LOADED RECORDS.
	Writer->BeginRun(RunId, CharacterId, Writer->HoldsTheDungeon(), Writer->HoldsTheFloor());
	Writer->AdoptLoaded(SavedRun, SavedCharacter);

	// THE PLACEMENT LAST: a character saved at no health dies here, and its death's write goes to the slots above.
	bool bPlaced = false;
	if (SavedRun)
	{
		for (const FCataclysmSavedCharacterPlacement& Placement : SavedRun->Floor.Characters)
		{
			if (Placement.CharacterId == CharacterId)
			{
				bPlaced = FCataclysmSaveApply::PlacementInto(Character, Placement);
				break;
			}
		}
	}

	if (SavedRun)
	{
		Report.Outcome = ECataclysmLoadOutcome::Loaded;
		Report.Why = FString::Printf(
			TEXT("character %s and run %s were put back: %s, %d creatures, %s"),
			*CharacterId.ToString(EGuidFormats::Digits), *RunId.ToString(EGuidFormats::Digits),
			RestoredEmpire ? *FString::Printf(TEXT("day %d"), SavedRun->Day) : TEXT("no empire run"),
			Report.CreaturesRestored, bPlaced ? TEXT("placed where it was saved") : TEXT("with no placement saved"));
	}
	else
	{
		Report.Outcome = ECataclysmLoadOutcome::LoadedWithANewRun;
		Report.Why = FString::Printf(TEXT("character %s was put back and a new run, %s, was begun, because %s"),
									 *CharacterId.ToString(EGuidFormats::Digits),
									 *RunId.ToString(EGuidFormats::Digits), *WhyNoRun);
	}
	return Report;
}

const TCHAR* FCataclysmSaveLoad::Describe(ECataclysmLoadOutcome Outcome)
{
	switch (Outcome)
	{
		case ECataclysmLoadOutcome::Loaded:            return TEXT("Loaded");
		case ECataclysmLoadOutcome::LoadedWithANewRun: return TEXT("Loaded with a new run");
		case ECataclysmLoadOutcome::Refused:           return TEXT("Refused, and nothing was changed");
	}
	return TEXT("An unnamed outcome");
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmLoadCharacter(
	TEXT("Cataclysm.LoadCharacter"),
	TEXT("Load a saved character and the run it is in, by the character's id: the 32 hex digits after "
		 "'Character_' in its save file's name. Issue #753."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
			ACataclysmPlayerCharacter* Character =
				Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
			if (!Character)
			{
				Ar.Log(TEXT("There is no player character. Press Play first."));
				return;
			}

			FGuid CharacterId;
			if (Args.Num() < 1 || !FGuid::Parse(Args[0], CharacterId))
			{
				Ar.Log(TEXT("Cataclysm.LoadCharacter <character id>. The id is the 32 hex digits after 'Character_' "
							"in the save file's name, in the game's SaveGames folder."));
				return;
			}

			const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*World, *Character, CharacterId);
			const FString Line = FString::Printf(TEXT("%s: %s. %d saved skill choices refused."),
												 FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
												 Report.SkillsRefused);
			Ar.Log(*Line);
			UE_LOG(LogCataclysm, Log, TEXT("Cataclysm.LoadCharacter: %s"), *Line);
		}));
