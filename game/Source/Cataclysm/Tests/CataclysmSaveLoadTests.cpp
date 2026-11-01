// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmRetaliation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmCorruptedStalkerCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Dungeon/CataclysmDungeonStairs.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Dungeon/CataclysmFloorObject.h"
#include "Dungeon/CataclysmFloorObstacle.h"
#include "Dungeon/CataclysmFloorPopulation.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Items/CataclysmDroppedItem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveLoad.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"

/**
 * A character and its run, written by one world and loaded by another. Issue #753, the start-up step, ruled
 * 2026-10-30. `FCataclysmSaveLoad` is what is tested; `Cataclysm.LoadCharacter` is one line around it.
 *
 * THESE TESTS TOUCH THE DISK. Every slot they use is named by an identifier generated for the test, and deleted again
 * on every path out, after any write still in flight has finished (issue #2495).
 */
namespace CataclysmSaveLoadTest
{
	static constexpr int32 UserIndex = UCataclysmSaveWriter::UserIndex;

	/** The name the save gives the character, which nothing in play writes and the next write must keep. */
	const TCHAR* SavedName = TEXT("Load test character");

	/** Waits, briefly, for a slot to exist with no write to it in flight. The writer tests' `WaitForSlot`. */
	bool WaitForSlot(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FPlatformTime::Seconds() < GiveUpAt)
		{
			if (UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex)
				&& !FCataclysmSaveStorage::HasAWriteInFlight(SlotName))
			{
				return true;
			}
			FPlatformProcess::Sleep(0.01f);
		}
		return false;
	}

	/** Deletes a slot once nothing is writing it. */
	void Delete(const FString& SlotName)
	{
		if (SlotName.IsEmpty())
		{
			return;
		}
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		if (UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
		{
			UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
		}
	}

	/** A world with a game instance, a possessed player, and a save writer writing a run of its own. */
	struct FLoadScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmSaveWriter* Writer = nullptr;

		bool IsUsable() const
		{
			return World && Instance && State && Character && Character->GetAbilitySystemComponent() && Writer;
		}
	};

	/**
	 * The scene, as the game makes it at Play: the game mode begins a run with two identifiers of its own on the
	 * sandbox's floor 1 (`ACataclysmGameMode::BeginSavingThisRun`), which a load then replaces.
	 */
	FLoadScene MakeScene()
	{
		FLoadScene Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}

		Out.Instance = NewObject<UCataclysmGameInstance>();
		Out.World->SetGameInstance(Out.Instance);

		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Out.State && Controller && Out.Character)
		{
			Controller->SetPlayerState(Out.State);
			Controller->Possess(Out.Character);
		}

		Out.Writer = UCataclysmSaveWriter::In(Out.World);
		if (Out.Writer)
		{
			Out.Writer->BeginRun(FGuid::NewGuid(), FGuid::NewGuid(), FName(TEXT("Sandbox")), 1);
		}
		return Out;
	}

	void SetHealth(ACataclysmPlayerCharacter& Character, float Health)
	{
		if (UAbilitySystemComponent* AbilitySystem = Character.GetAbilitySystemComponent())
		{
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		}
	}

	float HealthOf(const ACataclysmPlayerCharacter& Character)
	{
		const UAbilitySystemComponent* AbilitySystem = UCataclysmTargeting::AbilitySystemOf(&Character);
		return AbilitySystem ? AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute())
							 : -1.0f;
	}

	/** Every value the test saves, as the scene now holds it. */
	FString Held(const FLoadScene& Scene)
	{
		const UCataclysmEmpireRun* Run = Scene.Instance->EmpireRun.Get();
		float Defence = 0.0f;
		if (Run && Run->Map)
		{
			for (const FCataclysmCity& City : Run->Map->Cities)
			{
				Defence += City.Defence;
			}
		}
		const FVector At = Scene.Character->GetActorLocation();
		return FString::Printf(TEXT("level %d exp %lld lifetime kills %d run kills %d health %.1f at %.0f %.0f %.0f "
									"| day %d defence %.1f"),
							   Scene.State->GetCharacterLevel(), Scene.State->GetExperienceIntoLevel(),
							   Scene.State->GetLifetimeKills(), Scene.State->GetRunKills(), HealthOf(*Scene.Character),
							   At.X, At.Y, At.Z, Run ? Run->Day() : -1, Defence);
	}

	/** What a saved world left behind: its identifiers, and the values it held. */
	struct FSaved
	{
		FGuid RunId;
		FGuid CharacterId;
		FString Held;

		FString RunSlot() const { return UCataclysmSavePartition::RunSlotName(RunId); }
		FString CharacterSlot() const { return UCataclysmSavePartition::CharacterSlotName(CharacterId); }
		void Forget() const
		{
			Delete(RunSlot());
			Delete(CharacterSlot());
		}
	};

	/** Reads a record out of a slot, or null. */
	template <typename TRecord>
	TRecord* Read(const FString& SlotName)
	{
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
		FString Message;
		return Cast<TRecord>(FCataclysmSaveStorage::ReadFromSlot(SlotName, UserIndex, TRecord::StaticClass(),
																	GetTransientPackage(), Result, Message));
	}

	/**
	 * Plays a character in a world of its own and saves it, through the writer, with the death trigger, which writes
	 * both records before it returns. Then gives the saved character record a name, which nothing in play writes.
	 *
	 * @param bDies  kill the character instead, so `HandleDeath` writes it at no health.
	 */
	bool SaveACharacter(FAutomationTestBase& Test, bool bDies, FSaved& Out)
	{
		FLoadScene Scene = MakeScene();
		if (!Test.TestTrue(TEXT("set-up: the saving world was built"), Scene.IsUsable()))
		{
			if (Scene.World)
			{
				Scene.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Out.RunId = FGuid::NewGuid();
		Out.CharacterId = FGuid::NewGuid();
		Scene.Writer->BeginRun(Out.RunId, Out.CharacterId, FName(TEXT("Sandbox")), 1);

		if (!bDies)
		{
			UCataclysmEmpireRun* Run = Scene.Instance->BeginEmpireRun(7);
			if (!Test.TestNotNull(TEXT("set-up: an empire run was begun"), Run))
			{
				return false;
			}
			Run->AdvanceDays(12);
		}

		Scene.State->SetLevelAndExperience(9, 77);
		Scene.State->SetLifetimeKills(40);
		Scene.State->NoteKill();
		Scene.State->NoteKill();
		Scene.State->NoteKill();
		Scene.Character->SetActorLocation(FVector(250.0f, -125.0f, 90.0f));
		SetHealth(*Scene.Character, 37.0f);

		if (bDies)
		{
			UCataclysmSkillEffects::ReduceHealthDirectly(Scene.Character, Scene.Character, 1000000.0f);
			if (!Test.TestTrue(TEXT("set-up: the character died, which wrote the save"),
							   UCataclysmSkillEffects::IsDead(Scene.Character)))
			{
				return false;
			}
		}
		else
		{
			Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		}
		Out.Held = Held(Scene);

		UCataclysmCharacterSave* Saved = Read<UCataclysmCharacterSave>(Out.CharacterSlot());
		if (!Test.TestNotNull(TEXT("set-up: the character record was written"), Saved)
			|| !Test.TestNotNull(TEXT("set-up: the run record was written"), Read<UCataclysmRunSave>(Out.RunSlot())))
		{
			return false;
		}
		Saved->CharacterName = SavedName;
		FString Error;
		return Test.TestTrue(TEXT("set-up: the named character record was written back"),
							 FCataclysmSaveStorage::WriteToSlot(Saved, Out.CharacterSlot(), UserIndex, Error));
	}

	/** Reads the character record, changes it, and writes it back. */
	bool RewriteCharacter(FAutomationTestBase& Test, const FSaved& Saved, TFunctionRef<void(UCataclysmCharacterSave&)> Change)
	{
		UCataclysmCharacterSave* Record = Read<UCataclysmCharacterSave>(Saved.CharacterSlot());
		if (!Test.TestNotNull(TEXT("set-up: the character record reads"), Record))
		{
			return false;
		}
		Change(*Record);
		FString Error;
		return Test.TestTrue(TEXT("set-up: the changed character record was written"),
							 FCataclysmSaveStorage::WriteToSlot(Record, Saved.CharacterSlot(), UserIndex, Error));
	}

	/** Writes raw text to a slot. */
	bool WriteText(FAutomationTestBase& Test, const FString& SlotName, const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		return Test.TestTrue(TEXT("set-up: the text was written to its slot"),
							 UGameplayStatics::SaveDataToSlot(Bytes, SlotName, UserIndex));
	}

	/** The first Outpost of the map that has not fallen, or `INDEX_NONE`. */
	int32 AnOutpostOf(const UCataclysmEmpireRun& Run)
	{
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.Tier == ECataclysmCityTier::Outpost && !City.bFallen)
			{
				return City.CityId;
			}
		}
		return INDEX_NONE;
	}

	/** Puts one ordinary dungeon on a city by hand, with its timer: `CataclysmDungeonCostsDaysTests.cpp`'s way. */
	int32 PlaceByHand(UCataclysmEmpireRun& Run, int32 CityId)
	{
		const FCataclysmCity* City = Run.Map->Find(CityId);
		if (City == nullptr || Run.Clock == nullptr)
		{
			return INDEX_NONE;
		}

		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = Run.NextDungeonId;
		Dungeon.Type = ECataclysmDungeonType::Basic;
		Dungeon.SubType = ECataclysmDungeonSubType::None;
		Dungeon.CityId = CityId;
		Dungeon.CityTier = City->Tier;
		Dungeon.Floors = 20;
		Dungeon.ResolveDays = 100.0f;
		Dungeon.SpawnedDay = Run.Day();

		Run.NextDungeonId = Dungeon.DungeonId + 1;
		Run.Dungeons.Add(Dungeon);
		if (!Run.Clock->AddDungeon(Dungeon.DungeonId, Dungeon.Floors)
			|| !Run.Clock->SetResolveDays(Dungeon.DungeonId, 100.0f))
		{
			return INDEX_NONE;
		}
		return Dungeon.DungeonId;
	}
}

// ---------------------------------------------------------------------------
// The round trip
// ---------------------------------------------------------------------------

/**
 * A character and its run come back from their slots into another world, and the writing goes on into the same slots.
 *
 * THE CONTROL is the loading world before the load: it must differ from what was saved in every value compared, or a
 * load that did nothing would match. THE NAME is what the writer's adoption of the loaded record is for: nothing in
 * play writes it, so a writer starting from a fresh record would write it as empty at its next write.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadRoundTripTest,
	"Cataclysm.SaveLoad.ACharacterAndItsRunComeBackFromTheirSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadRoundTripTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	if (!SaveACharacter(*this, /*bDies=*/false, Saved))
	{
		return false;
	}

	FLoadScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the loading world was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FString Before = Held(Scene);
	TestTrue(TEXT("CONTROL: before the load the loading world holds other values"), Before != Saved.Held);

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.World, *Scene.Character,
																			 Saved.CharacterId);
	const FString After = Held(Scene);
	const bool bSameSlots = Scene.Writer->RunSlotName() == Saved.RunSlot()
		&& Scene.Writer->CharacterSlotName() == Saved.CharacterSlot();

	// THE NEXT WRITE, with the trigger that writes both records before it returns.
	Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
	const UCataclysmCharacterSave* Written = Read<UCataclysmCharacterSave>(Saved.CharacterSlot());
	const bool bNameKept = Written && Written->CharacterName == SavedName;
	const bool bRunKept = Written && Written->CurrentRunId == Saved.RunId;

	AddInfo(FString::Printf(TEXT("LOAD ROUND TRIP: %s (%s); held values %s; %s; name %s; run id %s"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
							After == Saved.Held ? TEXT("match") : TEXT("DIFFER"),
							bSameSlots ? TEXT("the same slots") : TEXT("OTHER slots"),
							bNameKept ? TEXT("kept") : TEXT("LOST"), bRunKept ? TEXT("kept") : TEXT("LOST")));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestEqual(TEXT("every value saved came back"), After, Saved.Held);
	TestTrue(TEXT("the writer goes on into the slots the load came from"), bSameSlots);
	TestTrue(TEXT("the next write keeps the name, which nothing in play writes"), bNameKept);
	TestTrue(TEXT("and still names the run"), bRunKept);
	return true;
}

/**
 * A character's name and partition, set as the creator will set them, are written by the save, come back with a load, and
 * are written again by the next save. Until this nothing in play wrote either, so every record held an empty name and the
 * default mode, and the character select screen would have had nothing to list. Issue #753, select-screen step 1, ruled
 * 2026-11-01. Prints "LOAD IDENTITY".
 *
 * THE CONTROL is the loading character before the load: it has no name or partition set.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadIdentityTest,
	"Cataclysm.SaveLoad.ACharactersNameAndModeComeBackFromALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadIdentityTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	const FString Name = TEXT("Vesper of the Ash");
	FCataclysmCharacterPartition Chosen;
	Chosen.Lethality = ECataclysmLethality::Hardcore;
	Chosen.Population = ECataclysmPopulation::Online;
	Chosen.bSoloSelfFound = true;
	auto Same = [&Chosen](const FCataclysmCharacterPartition& One)
	{
		return One.Lethality == Chosen.Lethality && One.Population == Chosen.Population
			&& One.bSoloSelfFound == Chosen.bSoloSelfFound;
	};
	auto Text = [](const FCataclysmCharacterPartition& One)
	{
		return FString::Printf(TEXT("lethality %d, population %d, %s"), static_cast<int32>(One.Lethality),
							   static_cast<int32>(One.Population), One.bSoloSelfFound ? TEXT("Solo Self-Found") : TEXT("shared"));
	};

	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	FString WrittenName;
	FCataclysmCharacterPartition WrittenPartition;
	{
		FLoadScene Scene = MakeScene();
		if (!TestTrue(TEXT("set-up: the saving world was built"), Scene.IsUsable()))
		{
			if (Scene.World)
			{
				Scene.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Saved.RunId = FGuid::NewGuid();
		Saved.CharacterId = FGuid::NewGuid();
		Scene.Writer->BeginRun(Saved.RunId, Saved.CharacterId, FName(TEXT("Sandbox")), 1);
		if (!TestNotNull(TEXT("set-up: an empire run was begun"), Scene.Instance->BeginEmpireRun(7)))
		{
			return false;
		}

		// AS THE CREATOR WILL SET THEM, and the save written with the trigger that writes both records before it returns.
		Scene.State->SetIdentity(Name, Chosen);
		Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		const UCataclysmCharacterSave* Written = Read<UCataclysmCharacterSave>(Saved.CharacterSlot());
		if (!TestNotNull(TEXT("set-up: the character record was written"), Written))
		{
			return false;
		}
		WrittenName = Written->CharacterName;
		WrittenPartition = Written->Partition;
	}

	FLoadScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the loading world was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const bool bHadBefore = Scene.State->HasIdentity();
	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.World, *Scene.Character,
																			 Saved.CharacterId);
	const FString NameAfter = Scene.State->GetCharacterName();
	const FCataclysmCharacterPartition PartitionAfter = Scene.State->GetPartition();

	// THE NEXT WRITE, from the loaded character.
	Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
	const UCataclysmCharacterSave* Rewritten = Read<UCataclysmCharacterSave>(Saved.CharacterSlot());
	const bool bRewritten = Rewritten && Rewritten->CharacterName == Name && Same(Rewritten->Partition);

	AddInfo(FString::Printf(TEXT("LOAD IDENTITY: written '%s' (%s); %s (%s); %s before the load, '%s' (%s) after it; the "
								 "next write %s"),
							*WrittenName, *Text(WrittenPartition), FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
							bHadBefore ? TEXT("SET") : TEXT("none set"), *NameAfter, *Text(PartitionAfter),
							bRewritten ? TEXT("keeps both") : TEXT("DOES NOT KEEP BOTH")));

	TestEqual(TEXT("the save wrote the name"), WrittenName, Name);
	TestTrue(TEXT("the save wrote the partition"), Same(WrittenPartition));
	TestFalse(TEXT("CONTROL: the loading character had none set before the load"), bHadBefore);
	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestEqual(TEXT("the load put the name back"), NameAfter, Name);
	TestTrue(TEXT("the load put the partition back"), Same(PartitionAfter));
	TestTrue(TEXT("and the next write keeps both"), bRewritten);
	return true;
}

// ---------------------------------------------------------------------------
// A run that cannot be read
// ---------------------------------------------------------------------------

/**
 * A run that cannot be read begins a new one and says so; a run written by a newer build, or saved inside an empire
 * dungeon, refuses the whole load and changes nothing. #753's acceptance criteria, and the rulings of 2026-10-30.
 *
 * "CHANGES NOTHING" IS MEASURED on the character's level and on which run the game holds, against a run begun before
 * each refused load. A NEW RUN IS MEASURED by the character coming back and the game holding a run on day 0.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadUnreadableRunTest,
	"Cataclysm.SaveLoad.ARunThatCannotBeReadStartsANewOneAndSaysSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadUnreadableRunTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	FSaved Saved;
	const FGuid EmptyRun = FGuid::NewGuid();
	const FGuid TextRun = FGuid::NewGuid();
	const FGuid NewerRun = FGuid::NewGuid();
	ON_SCOPE_EXIT
	{
		Saved.Forget();
		Delete(UCataclysmSavePartition::RunSlotName(TextRun));
		Delete(UCataclysmSavePartition::RunSlotName(NewerRun));
	};
	if (!SaveACharacter(*this, /*bDies=*/false, Saved))
	{
		return false;
	}

	TArray<FString> Lines;

	/** One load into a fresh world. Answers the outcome, and whether the character and the game's run are unchanged. */
	auto LoadOnce = [this, &Lines](const TCHAR* Case, const FGuid& CharacterId, ECataclysmLoadOutcome Expected,
										   const TCHAR* Says)
	{
		FLoadScene Scene = MakeScene();
		if (!TestTrue(FString::Printf(TEXT("%s: set-up: the loading world was built"), Case), Scene.IsUsable()))
		{
			if (Scene.World)
			{
				Scene.World->DestroyWorld(false);
			}
			return;
		}
		ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const UCataclysmEmpireRun* RunBefore = Scene.Instance->BeginEmpireRun(5);
		const int32 LevelBefore = Scene.State->GetCharacterLevel();

		const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.World, *Scene.Character,
																				 CharacterId);
		Lines.Add(FString::Printf(TEXT("%s: %s (%s)"), Case, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why));

		TestEqual(FString::Printf(TEXT("%s: the outcome"), Case), static_cast<int32>(Report.Outcome),
				  static_cast<int32>(Expected));
		TestTrue(FString::Printf(TEXT("%s: the report says '%s'"), Case, Says), Report.Why.Contains(Says));

		if (Expected == ECataclysmLoadOutcome::Refused)
		{
			TestEqual(FString::Printf(TEXT("%s: the character is unchanged"), Case),
					  Scene.State->GetCharacterLevel(), LevelBefore);
			TestTrue(FString::Printf(TEXT("%s: the game holds the run it held"), Case),
					 Scene.Instance->EmpireRun.Get() == RunBefore);
		}
		else
		{
			TestEqual(FString::Printf(TEXT("%s: the character came back"), Case), Scene.State->GetCharacterLevel(), 9);
			const UCataclysmEmpireRun* Now = Scene.Instance->EmpireRun.Get();
			TestTrue(FString::Printf(TEXT("%s: the game holds a new run on day 0"), Case),
					 Now != nullptr && Now != RunBefore && Now->Day() == 0);
		}
	};

	LoadOnce(TEXT("no character record"), FGuid::NewGuid(), ECataclysmLoadOutcome::Refused, TEXT("could not be read"));

	if (RewriteCharacter(*this, Saved, [](UCataclysmCharacterSave& Record) { Record.CurrentRunId.Invalidate(); }))
	{
		LoadOnce(TEXT("no run named"), Saved.CharacterId, ECataclysmLoadOutcome::LoadedWithANewRun,
				 TEXT("names no run"));
	}

	if (RewriteCharacter(*this, Saved, [&EmptyRun](UCataclysmCharacterSave& Record) { Record.CurrentRunId = EmptyRun; }))
	{
		LoadOnce(TEXT("an empty run slot"), Saved.CharacterId, ECataclysmLoadOutcome::LoadedWithANewRun,
				 TEXT("there is no run record"));
	}

	if (WriteText(*this, UCataclysmSavePartition::RunSlotName(TextRun), TEXT("this is not a save"))
		&& RewriteCharacter(*this, Saved, [&TextRun](UCataclysmCharacterSave& Record) { Record.CurrentRunId = TextRun; }))
	{
		LoadOnce(TEXT("a run slot that is not JSON"), Saved.CharacterId, ECataclysmLoadOutcome::LoadedWithANewRun,
				 TEXT("could not be read"));
	}

	if (WriteText(*this, UCataclysmSavePartition::RunSlotName(NewerRun),
				  FString::Printf(TEXT("{\"SchemaVersion\": 99, \"RunId\": \"%s\"}"),
								  *NewerRun.ToString(EGuidFormats::Digits)))
		&& RewriteCharacter(*this, Saved, [&NewerRun](UCataclysmCharacterSave& Record) { Record.CurrentRunId = NewerRun; }))
	{
		LoadOnce(TEXT("a run written by a newer build"), Saved.CharacterId, ECataclysmLoadOutcome::Refused,
				 TEXT("newer build"));
	}

	// INSIDE AN EMPIRE DUNGEON: the real run record, with the dungeon it was saved in set.
	UCataclysmRunSave* Inside = Read<UCataclysmRunSave>(Saved.RunSlot());
	FString Error;
	if (TestNotNull(TEXT("set-up: the run record reads"), Inside))
	{
		Inside->CurrentDungeonId = 3;
		if (TestTrue(TEXT("set-up: the run record was written inside a dungeon"),
					 FCataclysmSaveStorage::WriteToSlot(Inside, Saved.RunSlot(), UserIndex, Error))
			&& RewriteCharacter(*this, Saved, [&Saved](UCataclysmCharacterSave& Record) { Record.CurrentRunId = Saved.RunId; }))
		{
			LoadOnce(TEXT("a run saved inside an empire dungeon"), Saved.CharacterId, ECataclysmLoadOutcome::Refused,
					 TEXT("inside empire dungeon 3"));
		}
	}

	AddInfo(FString::Printf(TEXT("LOAD REFUSALS: %s"), *FString::Join(Lines, TEXT("; "))));
	return true;
}

// ---------------------------------------------------------------------------
// A character saved dead
// ---------------------------------------------------------------------------

/**
 * A character saved where it fell is loaded dead, and the death runs again from the load: ruled 2026-10-29, "Restore
 * it as saved, where it fell, so the death path runs on load".
 *
 * THE DEATH IS THE GAME'S OWN, NOT THE LOADER'S. Writing no health reaches `PostAttributeBaseChange`, which calls
 * `HandleDeath`; the death's own write then goes to the loaded slots. Standing back up raises the write that says the
 * death was paid for. THE CONTROL is the loading character before the load, alive.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadDeadTest,
	"Cataclysm.SaveLoad.ACharacterSavedDeadIsLoadedDead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadDeadTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	if (!SaveACharacter(*this, /*bDies=*/true, Saved))
	{
		return false;
	}

	FLoadScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the loading world was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	TestFalse(TEXT("CONTROL: before the load the character is alive"), UCataclysmSkillEffects::IsDead(Scene.Character));

	// THE SLOTS THE LOADING WORLD BEGAN WITH, which nothing may write. Until 2026-10-30 the death the load ran was
	// written there, before the writer was told the loaded run. Deleted on the way out either way.
	const FString BeganRunSlot = Scene.Writer->RunSlotName();
	const FString BeganCharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT
	{
		Delete(BeganRunSlot);
		Delete(BeganCharacterSlot);
	};

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.World, *Scene.Character,
																			 Saved.CharacterId);
	const bool bDeadAfterTheLoad = UCataclysmSkillEffects::IsDead(Scene.Character);
	const ECataclysmSaveTrigger AfterTheLoad = Scene.Writer->LastTrigger();
	const int32 Synchronous = Scene.Writer->SynchronousWrites();
	const bool bSameSlots = Scene.Writer->RunSlotName() == Saved.RunSlot();
	const bool bNothingWhereTheWorldBegan = !UGameplayStatics::DoesSaveGameExist(BeganRunSlot, UserIndex)
		&& !UGameplayStatics::DoesSaveGameExist(BeganCharacterSlot, UserIndex);

	Scene.Character->Revive();
	const bool bStandingAfterRevive = !UCataclysmSkillEffects::IsDead(Scene.Character);
	const ECataclysmSaveTrigger AfterRevive = Scene.Writer->LastTrigger();
	WaitForSlot(Saved.RunSlot());

	AddInfo(FString::Printf(TEXT("LOAD DEAD: %s (%s); %s after the load, last trigger '%s', %d synchronous writes, "
								 "%s in the slots the world began with; %s after standing up, last trigger '%s'"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
							bDeadAfterTheLoad ? TEXT("dead") : TEXT("ALIVE"),
							UCataclysmSaveTriggers::NameOf(AfterTheLoad), Synchronous,
							bNothingWhereTheWorldBegan ? TEXT("nothing") : TEXT("A WRITE"),
							bStandingAfterRevive ? TEXT("standing") : TEXT("STILL DEAD"),
							UCataclysmSaveTriggers::NameOf(AfterRevive)));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestTrue(TEXT("the character is dead after the load"), bDeadAfterTheLoad);
	TestEqual(TEXT("its death raised the death's own write"),
			  static_cast<int32>(AfterTheLoad), static_cast<int32>(ECataclysmSaveTrigger::CharacterDied));
	TestTrue(TEXT("which was written before the load returned"), Synchronous > 0);
	TestTrue(TEXT("the writer now names the slots the load came from"), bSameSlots);
	TestTrue(TEXT("and nothing was written to the slots the loading world began with"), bNothingWhereTheWorldBegan);
	TestTrue(TEXT("it stands back up"), bStandingAfterRevive);
	TestEqual(TEXT("and standing up raised the write that says the death was paid for"),
			  static_cast<int32>(AfterRevive), static_cast<int32>(ECataclysmSaveTrigger::DeathProcessed));
	return true;
}

// ---------------------------------------------------------------------------
// Processing a death writes the save
// ---------------------------------------------------------------------------

/**
 * After a death in an empire dungeon is paid for, the run record on disk holds it paid: the days charged and the
 * character standing. Without this write the record held the unpaid death until the clock wrote, and a crash in that
 * gap would charge it twice on load. Ruled 2026-10-30.
 *
 * THE KILL AND THE STANDING UP ARE IN ONE FRAME HERE, because a test world is never ticked, which also covers a
 * processing write in a frame another write already took: the run record is otherwise written at most once a frame.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadDeathProcessedTest,
	"Cataclysm.SaveLoad.ProcessingADeathWritesTheSaveAfterThePenalty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadDeathProcessedTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	FLoadScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the world was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE RUN, ON THE GAME INSTANCE, so the dungeon and the writer both find it. Nothing standing on it, as the death
	// tests' scene has it.
	UCataclysmEmpireRun* Run = Scene.Instance->BeginEmpireRun(1, ECataclysmSurgeMode::Static, 0);
	if (!TestTrue(TEXT("set-up: a run was begun"), Run && Run->Map && Run->Clock))
	{
		return false;
	}
	Run->AdvanceDay();
	Run->Dungeons.Empty();
	Run->Clock->Timers.Empty();

	ACataclysmDungeonGameMode* Mode = Scene.World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("set-up: a dungeon game mode"), Mode))
	{
		return false;
	}
	Mode->StartPlay();

	// THE TEST'S OWN SLOTS BEFORE THE DUNGEON IS ENTERED, so the floor change entering it raises writes into slots
	// this test deletes rather than into the ones `StartPlay` named.
	FSaved Ids;
	Ids.RunId = FGuid::NewGuid();
	Ids.CharacterId = FGuid::NewGuid();
	ON_SCOPE_EXIT { Ids.Forget(); };
	Scene.Writer->BeginRun(Ids.RunId, Ids.CharacterId, Scene.Writer->HoldsTheDungeon(), Scene.Writer->HoldsTheFloor());

	const int32 DungeonId = PlaceByHand(*Run, AnOutpostOf(*Run));
	if (!TestTrue(TEXT("set-up: a dungeon was placed on an Outpost"), DungeonId != INDEX_NONE)
		|| !TestTrue(TEXT("set-up: the dungeon was entered"), Mode->EnterEmpireDungeon(DungeonId)))
	{
		return false;
	}
	Mode->ClearFloorEnemies();
	Mode->PlaceAtEntrance(Scene.Character);

	const int32 DayInside = Run->Day() + Run->DaysOwed();
	UCataclysmSkillEffects::ReduceHealthDirectly(Scene.Character, Scene.Character, 1000000.0f);
	if (!TestTrue(TEXT("set-up: the character died"), UCataclysmSkillEffects::IsDead(Scene.Character)))
	{
		return false;
	}
	Scene.Character->Revive();
	if (!TestFalse(TEXT("set-up: the character stood back up"), UCataclysmSkillEffects::IsDead(Scene.Character)))
	{
		return false;
	}
	const ECataclysmSaveTrigger Last = Scene.Writer->LastTrigger();

	if (!TestTrue(TEXT("set-up: the run record reached the disk"), WaitForSlot(Ids.RunSlot())))
	{
		return false;
	}
	const UCataclysmRunSave* Written = Read<UCataclysmRunSave>(Ids.RunSlot());
	if (!TestNotNull(TEXT("set-up: the run record reads"), Written))
	{
		return false;
	}
	float WrittenHealth = -1.0f;
	for (const FCataclysmSavedCharacterPlacement& Placement : Written->Floor.Characters)
	{
		if (Placement.CharacterId == Ids.CharacterId)
		{
			WrittenHealth = Placement.Health;
		}
	}

	AddInfo(FString::Printf(TEXT("DEATH PROCESSED: entered on day %d, the death charged %d days, the record holds day "
								 "%d and health %.0f; the last trigger was '%s'"),
							DayInside, Run->DeathDayCost(), Written->Day, WrittenHealth,
							UCataclysmSaveTriggers::NameOf(Last)));

	TestEqual(TEXT("the last write was the one that says the death was paid for"),
			  static_cast<int32>(Last), static_cast<int32>(ECataclysmSaveTrigger::DeathProcessed));
	TestEqual(TEXT("the record holds the day after the death's days"), Written->Day, DayInside + Run->DeathDayCost());
	TestTrue(TEXT("and the character standing"), WrittenHealth > 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
// Inside an empire dungeon
// ---------------------------------------------------------------------------

namespace CataclysmSaveLoadTest
{
	/** A loading or saving world with a dungeon game mode that has begun play, as `L_Dungeon` has. */
	struct FDungeonScene
	{
		FLoadScene Base;
		ACataclysmDungeonGameMode* Mode = nullptr;

		bool IsUsable() const { return Base.IsUsable() && Mode; }
	};

	FDungeonScene MakeDungeonScene()
	{
		FDungeonScene Out;
		Out.Base = MakeScene();
		if (Out.Base.World)
		{
			Out.Mode = Out.Base.World->SpawnActor<ACataclysmDungeonGameMode>();
			if (Out.Mode)
			{
				Out.Mode->StartPlay();
			}
		}
		return Out;
	}

	/** What a world saved inside a dungeon left behind, beside its identifiers. */
	struct FSavedInADungeon : FSaved
	{
		int32 DungeonId = INDEX_NONE;
		int32 Floor = 0;
		int32 PlanSeed = 0;
		int32 CreaturesSaved = 0;
		float HurtHealth = -1.0f;
		int32 Day = 0;
		float PartialDay = 0.0f;
		int32 DaysOwed = 0;
	};

	float HealthOfActor(const AActor& Actor)
	{
		const UAbilitySystemComponent* AbilitySystem = UCataclysmTargeting::AbilitySystemOf(&Actor);
		return AbilitySystem ? AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute())
							 : -1.0f;
	}

	/** The character's place, health and potions, which the placement holds. */
	FString PlacementHeld(const FLoadScene& Scene)
	{
		const UCataclysmAbilitySystemComponent* Holder = Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Scene.Character));
		const FVector At = Scene.Character->GetActorLocation();
		return FString::Printf(TEXT("health %.1f at %.0f %.0f %.0f potions %.0f %.0f %.0f %.0f drunk %d"),
							   HealthOf(*Scene.Character), At.X, At.Y, At.Z,
							   Holder ? Holder->GetPotionCharges(0) : -1.0f, Holder ? Holder->GetPotionCharges(1) : -1.0f,
							   Holder ? Holder->GetPotionCharges(2) : -1.0f, Holder ? Holder->GetPotionCharges(3) : -1.0f,
							   Holder ? Holder->GetPotionsDrunk() : -1);
	}

	/** The creatures of the dungeon's list that no rule raised, standing. */
	TArray<ACataclysmEnemyCharacter*> UnruledStanding(const ACataclysmDungeonGameMode& Mode)
	{
		TArray<ACataclysmEnemyCharacter*> Out;
		for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
		{
			if (IsValid(Enemy) && !Enemy->bRaisedByARule && !UCataclysmSkillEffects::IsDead(Enemy))
			{
				Out.Add(Enemy.Get());
			}
		}
		return Out;
	}

	/**
	 * Walks a dungeon placed by hand down to a floor, in a world of its own, and saves there through the writer: with
	 * the death trigger, which writes both records before it returns, or by killing the character, which writes it at
	 * no health. One creature is hurt, the potions are spent below full, and the character is moved and hurt first.
	 *
	 * @param bWithTheGuide  the dungeon carries Demonic Guide, whose guide is a creature the floor's rule raises
	 */
	bool SaveInADungeon(FAutomationTestBase& Test, bool bDies, bool bWithTheGuide, int32 Floor, FSavedInADungeon& Out)
	{
		FDungeonScene Scene = MakeDungeonScene();
		if (!Test.TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		// THE TEST'S OWN SLOTS BEFORE THE DUNGEON IS ENTERED, so the floor changes write where this test deletes.
		Out.RunId = FGuid::NewGuid();
		Out.CharacterId = FGuid::NewGuid();
		Scene.Base.Writer->BeginRun(Out.RunId, Out.CharacterId, Scene.Mode->DungeonName, Scene.Mode->FloorNumber);

		UCataclysmEmpireRun* Run = Scene.Base.Instance->BeginEmpireRun(1, ECataclysmSurgeMode::Static, 0);
		if (!Test.TestTrue(TEXT("set-up: an empire run was begun"), Run && Run->Map && Run->Clock))
		{
			return false;
		}
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();

		Out.DungeonId = PlaceByHand(*Run, AnOutpostOf(*Run));
		if (!Test.TestTrue(TEXT("set-up: a dungeon was placed on an Outpost"), Out.DungeonId != INDEX_NONE))
		{
			return false;
		}
		if (bWithTheGuide)
		{
			for (FCataclysmDungeon& Dungeon : Run->Dungeons)
			{
				if (Dungeon.DungeonId == Out.DungeonId)
				{
					Dungeon.Modifiers = {FName(UCataclysmDungeonModifierEffects::DemonicGuideKey)};
				}
			}
		}
		if (!Test.TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(Out.DungeonId))
			|| !Test.TestTrue(TEXT("set-up: the walk reached the floor"), Scene.Mode->GoToFloor(Floor))
			|| !Test.TestNotNull(TEXT("set-up: the floor was built"), Scene.Mode->CurrentFloor.Get()))
		{
			return false;
		}
		if (bWithTheGuide
			&& !Test.TestNotNull(TEXT("set-up: the guide stands on the floor"), Scene.Mode->DemonicGuideOnTheFloor()))
		{
			return false;
		}
		Out.Floor = Floor;
		Out.PlanSeed = Scene.Mode->CurrentFloor->GetPlan().Seed;

		const TArray<ACataclysmEnemyCharacter*> Standing = UnruledStanding(*Scene.Mode);
		if (!Test.TestTrue(TEXT("set-up: the floor has a creature to hurt"), Standing.Num() > 0))
		{
			return false;
		}
		if (UAbilitySystemComponent* Hurt = UCataclysmTargeting::AbilitySystemOf(Standing[0]))
		{
			Hurt->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 41.0f);
		}
		Out.HurtHealth = HealthOfActor(*Standing[0]);

		if (UCataclysmAbilitySystemComponent* Holder = Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Scene.Base.Character)))
		{
			Holder->SetPotionCharges(0, 25.0f);
			Holder->SetPotionCharges(1, 15.0f);
			Holder->SetPotionCharges(2, 5.0f);
			Holder->SetPotionCharges(3, 0.0f);
			Holder->SetPotionsDrunk(2);
		}
		Scene.Base.Character->SetActorLocation(Scene.Base.Character->GetActorLocation() + FVector(120.0f, 60.0f, 0.0f));
		SetHealth(*Scene.Base.Character, 37.0f);
		Out.Held = PlacementHeld(Scene.Base);

		if (bDies)
		{
			UCataclysmSkillEffects::ReduceHealthDirectly(Scene.Base.Character, Scene.Base.Character, 1000000.0f);
			if (!Test.TestTrue(TEXT("set-up: the character died, which wrote the save"),
							   UCataclysmSkillEffects::IsDead(Scene.Base.Character)))
			{
				return false;
			}
		}
		else
		{
			Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		}

		WaitForSlot(Out.RunSlot());
		const UCataclysmRunSave* Written = Read<UCataclysmRunSave>(Out.RunSlot());
		if (!Test.TestNotNull(TEXT("set-up: the run record was written"), Written)
			|| !Test.TestEqual(TEXT("set-up: the run record says it was saved inside the dungeon"),
							   Written->CurrentDungeonId, Out.DungeonId))
		{
			return false;
		}
		Out.CreaturesSaved = Written->Floor.Creatures.Num();
		Out.Day = Written->Day;
		Out.PartialDay = Written->PartialDay;
		Out.DaysOwed = Written->DaysOwed;
		return Test.TestNotNull(TEXT("set-up: the character record was written"),
								Read<UCataclysmCharacterSave>(Out.CharacterSlot()));
	}
}

/**
 * A character saved on a dungeon's third floor comes back on that floor of that dungeon: entered again, with the same
 * layout, its creatures back with their health, the guide the floor's rule raises still standing, its potions as
 * saved, and no day charged by the load. Issue #753, the dungeon step, ruled 2026-10-30.
 *
 * THE CONTROL is the loading dungeon before the load, walking nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadDungeonTest,
	"Cataclysm.SaveLoad.ACharacterSavedInADungeonComesBackOnItsFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadDungeonTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	FSavedInADungeon Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	if (!SaveInADungeon(*this, /*bDies=*/false, /*bWithTheGuide=*/true, /*Floor=*/3, Saved))
	{
		return false;
	}

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	TestEqual(TEXT("CONTROL: before the load the dungeon is walking nothing"), Scene.Mode->EmpireDungeonId,
			  static_cast<int32>(INDEX_NONE));

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																			 Saved.CharacterId);

	const UCataclysmEmpireRun* Run = Scene.Base.Instance->EmpireRun.Get();
	const int32 Seed = Scene.Mode->CurrentFloor ? Scene.Mode->CurrentFloor->GetPlan().Seed : 0;
	const TArray<ACataclysmEnemyCharacter*> Standing = UnruledStanding(*Scene.Mode);
	bool bHurtOneBack = false;
	for (const ACataclysmEnemyCharacter* Creature : Standing)
	{
		bHurtOneBack |= FMath::IsNearlyEqual(HealthOfActor(*Creature), Saved.HurtHealth, 0.5f);
	}
	const bool bGuideStanding = IsValid(Scene.Mode->DemonicGuideOnTheFloor());
	const int32 Day = Run ? Run->Day() : -1;
	const float PartialDay = Run && Run->Clock ? Run->Clock->PartialDay : -1.0f;
	const int32 DaysOwed = Run ? Run->DaysOwed() : -1;
	const FString Held = PlacementHeld(Scene.Base);

	AddInfo(FString::Printf(TEXT("LOAD DUNGEON: %s (%s); dungeon %d floor %d (saved %d floor %d); seed %s; %d of %d "
								 "creatures standing, the hurt one %s; the guide %s; day %d part %.3f owed %d (saved %d "
								 "%.3f %d); placement %s"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, Scene.Mode->EmpireDungeonId,
							Scene.Mode->FloorNumber, Saved.DungeonId, Saved.Floor,
							Seed == Saved.PlanSeed ? TEXT("the same") : TEXT("DIFFERS"), Standing.Num(),
							Saved.CreaturesSaved, bHurtOneBack ? TEXT("back") : TEXT("NOT BACK"),
							bGuideStanding ? TEXT("standing") : TEXT("GONE"), Day, PartialDay, DaysOwed, Saved.Day,
							Saved.PartialDay, Saved.DaysOwed, Held == Saved.Held ? TEXT("matches") : TEXT("DIFFERS")));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestEqual(TEXT("the dungeon it was saved in is the one being walked"), Scene.Mode->EmpireDungeonId, Saved.DungeonId);
	TestEqual(TEXT("on the floor it was saved on"), Scene.Mode->FloorNumber, Saved.Floor);
	TestEqual(TEXT("built from the same seed"), Seed, Saved.PlanSeed);
	TestEqual(TEXT("every creature saved is standing on the floor and in its list"), Standing.Num(),
			  Saved.CreaturesSaved);
	TestTrue(TEXT("the hurt one with the health it had"), bHurtOneBack);
	TestTrue(TEXT("the creature the floor's rule raised is still standing"), bGuideStanding);
	TestEqual(TEXT("no day was charged by the load"), Day, Saved.Day);
	TestEqual(TEXT("no part of a day"), PartialDay, Saved.PartialDay, 0.0001f);
	TestEqual(TEXT("and the walk owes the days it owed"), DaysOwed, Saved.DaysOwed);
	TestEqual(TEXT("the character's place, health and potions came back"), Held, Saved.Held);
	return true;
}

/**
 * A character that died inside a dungeon and is loaded there pays the death's days when it stands back up, which is
 * the reason a load enters the dungeon again. Issue #753, the dungeon step.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadDungeonDeathTest,
	"Cataclysm.SaveLoad.ADeathInADungeonIsPaidForAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadDungeonDeathTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	FSavedInADungeon Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	if (!SaveInADungeon(*this, /*bDies=*/true, /*bWithTheGuide=*/false, /*Floor=*/2, Saved))
	{
		return false;
	}

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	TestFalse(TEXT("CONTROL: before the load the character is alive"),
			  UCataclysmSkillEffects::IsDead(Scene.Base.Character));

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																			 Saved.CharacterId);
	const bool bDeadAfterTheLoad = UCataclysmSkillEffects::IsDead(Scene.Base.Character);
	const UCataclysmEmpireRun* Run = Scene.Base.Instance->EmpireRun.Get();
	const int32 Cost = Run ? Run->DeathDayCost() : -1;

	Scene.Base.Character->Revive();
	const int32 DayAfter = Run ? Run->Day() : -1;
	const bool bResolved = Run && Run->FindDungeon(Saved.DungeonId) == nullptr;
	const ECataclysmSaveTrigger Last = Scene.Base.Writer->LastTrigger();
	WaitForSlot(Saved.RunSlot());

	AddInfo(FString::Printf(TEXT("LOAD DUNGEON DEATH: %s (%s); %s after the load; saved on day %d owing %d, the death "
								 "costs %d, day %d after standing up; walking %d; the dungeon %s; last trigger '%s'"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
							bDeadAfterTheLoad ? TEXT("dead") : TEXT("ALIVE"), Saved.Day, Saved.DaysOwed, Cost,
							DayAfter, Scene.Mode->EmpireDungeonId,
							bResolved ? TEXT("resolved and left the map") : TEXT("is STILL ON THE MAP"),
							UCataclysmSaveTriggers::NameOf(Last)));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestTrue(TEXT("the character is dead after the load"), bDeadAfterTheLoad);
	TestEqual(TEXT("standing up ran the walk's days and charged the death's"), DayAfter,
			  Saved.Day + Saved.DaysOwed + Cost);
	TestTrue(TEXT("the ordinary dungeon resolved on the death"), bResolved);
	TestEqual(TEXT("and the walk ended"), Scene.Mode->EmpireDungeonId, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("and the paid death was written"), static_cast<int32>(Last),
			  static_cast<int32>(ECataclysmSaveTrigger::DeathProcessed));
	return true;
}

/**
 * A run saved inside a dungeon that cannot be entered again here is refused, and nothing changes: no dungeon in the
 * level, a walk already under way, a dungeon the record does not hold, a floor the dungeon does not have.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadDungeonRefusedTest,
	"Cataclysm.SaveLoad.ARunSavedInADungeonIsRefusedWhereItCannotBeResumed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadDungeonRefusedTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	FSavedInADungeon Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	if (!SaveInADungeon(*this, /*bDies=*/false, /*bWithTheGuide=*/false, /*Floor=*/2, Saved))
	{
		return false;
	}

	TArray<FString> Lines;

	/** One refused load: the report says why, and the character, the game's run and the walk are unchanged. */
	auto RefusedOnce = [this, &Saved, &Lines](const TCHAR* Case, bool bWithADungeon, bool bAlreadyWalking,
											  const TCHAR* Says)
	{
		FDungeonScene Scene;
		if (bWithADungeon)
		{
			Scene = MakeDungeonScene();
		}
		else
		{
			Scene.Base = MakeScene();
		}
		if (!TestTrue(FString::Printf(TEXT("%s: set-up: the loading world was built"), Case),
					  Scene.Base.IsUsable() && (!bWithADungeon || Scene.Mode)))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		UCataclysmEmpireRun* RunBefore = Scene.Base.Instance->BeginEmpireRun(5);
		int32 WalkingBefore = INDEX_NONE;
		if (bAlreadyWalking && RunBefore && Scene.Mode)
		{
			RunBefore->Dungeons.Empty();
			if (RunBefore->Clock)
			{
				RunBefore->Clock->Timers.Empty();
			}
			const int32 Walking = PlaceByHand(*RunBefore, AnOutpostOf(*RunBefore));
			if (!TestTrue(FString::Printf(TEXT("%s: set-up: the loading dungeon is walking one of its own"), Case),
						  Walking != INDEX_NONE && Scene.Mode->EnterEmpireDungeon(Walking)))
			{
				return;
			}
			WalkingBefore = Walking;
		}
		const int32 LevelBefore = Scene.Base.State->GetCharacterLevel();

		const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World,
																				 *Scene.Base.Character,
																				 Saved.CharacterId);
		Lines.Add(FString::Printf(TEXT("%s: %s (%s)"), Case, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why));

		TestEqual(FString::Printf(TEXT("%s: refused"), Case), static_cast<int32>(Report.Outcome),
				  static_cast<int32>(ECataclysmLoadOutcome::Refused));
		TestTrue(FString::Printf(TEXT("%s: the report says '%s'"), Case, Says), Report.Why.Contains(Says));
		TestEqual(FString::Printf(TEXT("%s: the character is unchanged"), Case), Scene.Base.State->GetCharacterLevel(),
				  LevelBefore);
		TestTrue(FString::Printf(TEXT("%s: the game holds the run it held"), Case),
				 Scene.Base.Instance->EmpireRun.Get() == RunBefore);
		if (Scene.Mode)
		{
			TestEqual(FString::Printf(TEXT("%s: the walk is unchanged"), Case), Scene.Mode->EmpireDungeonId,
					  WalkingBefore);
		}
	};

	RefusedOnce(TEXT("no dungeon in the level"), /*bWithADungeon=*/false, false, TEXT("has no dungeon to enter it in"));
	RefusedOnce(TEXT("a walk under way"), /*bWithADungeon=*/true, /*bAlreadyWalking=*/true, TEXT("leave it first"));

	FString Error;
	UCataclysmRunSave* Record = Read<UCataclysmRunSave>(Saved.RunSlot());
	if (TestNotNull(TEXT("set-up: the run record reads"), Record))
	{
		Record->CurrentDungeonId = 999;
		if (TestTrue(TEXT("set-up: the run record names a dungeon its map does not hold"),
					 FCataclysmSaveStorage::WriteToSlot(Record, Saved.RunSlot(), UserIndex, Error)))
		{
			RefusedOnce(TEXT("a dungeon not on the record's map"), true, false, TEXT("holds no such dungeon"));
		}
		Record->CurrentDungeonId = Saved.DungeonId;
		Record->Floor.Floor = 99;
		if (TestTrue(TEXT("set-up: the run record names a floor the dungeon does not have"),
					 FCataclysmSaveStorage::WriteToSlot(Record, Saved.RunSlot(), UserIndex, Error)))
		{
			RefusedOnce(TEXT("a floor the dungeon does not have"), true, false, TEXT("that dungeon has 20 floors"));
		}
	}

	AddInfo(FString::Printf(TEXT("LOAD DUNGEON REFUSALS: %s"), *FString::Join(Lines, TEXT("; "))));
	return true;
}

// ---------------------------------------------------------------------------
// What a dungeon's rules built up across its floors
// ---------------------------------------------------------------------------

namespace CataclysmSaveLoadTest
{
	/** Every field of a walk, on one line, so two walks compare as one string. */
	FString WalkLine(const FCataclysmSavedWalk& Walk)
	{
		auto Join = [](const TArray<int32>& Counts)
		{
			FString Out;
			for (const int32 Count : Counts)
			{
				Out += FString::Printf(TEXT("%d,"), Count);
			}
			return Out;
		};
		return FString::Printf(
			TEXT("march %d pact %d %d [%s] %d debt %d price %d infernal %d rift %d sewage %d pestilent %d wasting %d "
				 "starvation %d %d chaos [%s] void %.2f %.2f %.2f %.2f trick %d %d echo %d %d wild %d %s souls %d"),
			Walk.MarchOfProgressCommandersKilled, Walk.PactBuffNow, Walk.PactBuffNext, *Join(Walk.PactCurseCounts),
			Walk.PactsTaken, Walk.BloodDebtPaid, Walk.BloodPriceStacks, Walk.InfernalBeaconStacks,
			Walk.AbyssalRiftSuccesses, Walk.RawSewageStacks, Walk.PestilentBeaconsLeftStanding,
			Walk.WastingSicknessStacks, Walk.StarvationCurseMovementStacks, Walk.StarvationCurseHealthStacks,
			*Join(Walk.ChaosTouchedStacks), Walk.NothingIsForgottenHealth, Walk.NothingIsForgottenDamage,
			Walk.NothingIsForgottenHealthGiven, Walk.NothingIsForgottenDamageGiven, Walk.TrickOrTreatPickups,
			Walk.TrickOrTreatRaised, Walk.EchoChamberCopiesMade, Walk.EchoChamberHitsOnThePlayer,
			Walk.WildMagicTriggered, *Walk.WildMagicLast.ToString(), Walk.SoulHarvestGiven);
	}

	/** A walk with every field set, each to a value no other field holds. */
	FCataclysmSavedWalk EveryFieldSet()
	{
		FCataclysmSavedWalk Walk;
		Walk.MarchOfProgressCommandersKilled = 4;
		Walk.PactBuffNow = 1;
		Walk.PactBuffNext = 2;
		Walk.PactCurseCounts = {1, 0, 2, 0, 3};
		Walk.PactsTaken = 6;
		Walk.BloodDebtPaid = 7;
		Walk.BloodPriceStacks = 8;
		Walk.InfernalBeaconStacks = 9;
		Walk.AbyssalRiftSuccesses = 10;
		Walk.RawSewageStacks = 11;
		Walk.PestilentBeaconsLeftStanding = 12;
		Walk.WastingSicknessStacks = 13;
		Walk.StarvationCurseMovementStacks = 1;
		Walk.StarvationCurseHealthStacks = 2;
		Walk.ChaosTouchedStacks = {1, 0, 0, 2, 0, 0, 0, 1};
		Walk.NothingIsForgottenHealth = 14.5f;
		Walk.NothingIsForgottenDamage = 15.25f;
		Walk.NothingIsForgottenHealthGiven = 16.0f;
		Walk.NothingIsForgottenDamageGiven = 17.0f;
		Walk.TrickOrTreatPickups = 18;
		Walk.TrickOrTreatRaised = 19;
		Walk.EchoChamberCopiesMade = 20;
		Walk.EchoChamberHitsOnThePlayer = 21;
		Walk.WildMagicTriggered = 22;
		Walk.WildMagicLast = FName(TEXT("Load_test_skill"));
		Walk.SoulHarvestGiven = 23;
		return Walk;
	}

	/** A dungeon placed by hand on a run of a world's own game instance, carrying the given modifiers. */
	int32 PlaceADungeonCarrying(FAutomationTestBase& Test, const FDungeonScene& Scene, const TArray<FName>& Modifiers)
	{
		UCataclysmEmpireRun* Run = Scene.Base.Instance->BeginEmpireRun(1, ECataclysmSurgeMode::Static, 0);
		if (!Test.TestTrue(TEXT("set-up: an empire run was begun"), Run && Run->Map && Run->Clock))
		{
			return INDEX_NONE;
		}
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		const int32 DungeonId = PlaceByHand(*Run, AnOutpostOf(*Run));
		for (FCataclysmDungeon& Dungeon : Run->Dungeons)
		{
			if (Dungeon.DungeonId == DungeonId)
			{
				Dungeon.Modifiers = Modifiers;
			}
		}
		return DungeonId;
	}

	/** The player's walking speed and maximum health, which the Starvation Curse lowers. */
	FString StarvedFigures(const ACataclysmPlayerCharacter& Character)
	{
		const UAbilitySystemComponent* AbilitySystem = UCataclysmTargeting::AbilitySystemOf(&Character);
		const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
		return FString::Printf(TEXT("speed %.1f max health %.1f"), Movement ? Movement->MaxWalkSpeed : -1.0f,
							   AbilitySystem ? AbilitySystem->GetNumericAttribute(
												   UCataclysmVitalAttributeSet::GetMaxHealthAttribute())
											 : -1.0f);
	}
}

/**
 * Every counter a dungeon's rules build up across its floors comes back with the run. Issue #753, Group 1, ruled
 * 2026-10-30: quitting buys "a breather and not a reset". THE CONTROL is the loading dungeon's walk before the load,
 * which holds none of them.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadWalkTest,
	"Cataclysm.SaveLoad.ADungeonWalksCountersComeBackFromALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadWalkTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	const FCataclysmSavedWalk Set = EveryFieldSet();
	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	{
		FDungeonScene Scene = MakeDungeonScene();
		if (!TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Saved.RunId = FGuid::NewGuid();
		Saved.CharacterId = FGuid::NewGuid();
		Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName, Scene.Mode->FloorNumber);
		const int32 DungeonId = PlaceADungeonCarrying(*this, Scene, {});
		if (!TestTrue(TEXT("set-up: a dungeon was placed"), DungeonId != INDEX_NONE)
			|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
			|| !TestTrue(TEXT("set-up: the walk reached floor 2"), Scene.Mode->GoToFloor(2)))
		{
			return false;
		}
		Scene.Mode->RestoreWalkState(Set);
		if (!TestEqual(TEXT("set-up: the walk holds every field set"), WalkLine(Scene.Mode->WalkStateNow()),
					   WalkLine(Set)))
		{
			return false;
		}
		Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		WaitForSlot(Saved.RunSlot());
	}

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const bool bNothingBefore = WalkLine(Scene.Mode->WalkStateNow()) != WalkLine(Set);

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																			 Saved.CharacterId);
	const FString After = WalkLine(Scene.Mode->WalkStateNow());

	AddInfo(FString::Printf(TEXT("LOAD WALK: %s (%s); %s; saved '%s'; loaded '%s'"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
							After == WalkLine(Set) ? TEXT("every counter matches") : TEXT("the counters DIFFER"),
							*WalkLine(Set), *After));

	TestTrue(TEXT("CONTROL: before the load the dungeon's walk held other counters"), bNothingBefore);
	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestEqual(TEXT("every counter of the walk came back"), After, WalkLine(Set));
	return true;
}

/**
 * A floor's start adds a Starvation curse; a load enters the saved floor again, and must not add the saved floor's
 * stack a second time. And the stacks reach the player. Issue #753, Group 1, ruled 2026-10-30.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadFloorStartOnceTest,
	"Cataclysm.SaveLoad.AFloorStartIsNotCountedTwiceByALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadFloorStartOnceTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	int32 StacksSaved = -1;
	FString FiguresSaved;
	{
		FDungeonScene Scene = MakeDungeonScene();
		if (!TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Saved.RunId = FGuid::NewGuid();
		Saved.CharacterId = FGuid::NewGuid();
		Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName, Scene.Mode->FloorNumber);
		const int32 DungeonId = PlaceADungeonCarrying(
			*this, Scene, {FName(UCataclysmDungeonModifierEffects::StarvationCurseKey)});
		if (!TestTrue(TEXT("set-up: a dungeon carrying the Starvation Curse was placed"), DungeonId != INDEX_NONE)
			|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
			|| !TestTrue(TEXT("set-up: the walk reached floor 2"), Scene.Mode->GoToFloor(2)))
		{
			return false;
		}
		const FCataclysmSavedWalk Walk = Scene.Mode->WalkStateNow();
		StacksSaved = Walk.StarvationCurseMovementStacks + Walk.StarvationCurseHealthStacks;
		if (!TestEqual(TEXT("set-up: two floor starts gave two Starvation stacks"), StacksSaved, 2))
		{
			return false;
		}
		FiguresSaved = StarvedFigures(*Scene.Base.Character);
		Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		WaitForSlot(Saved.RunSlot());
	}

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																			 Saved.CharacterId);
	const FCataclysmSavedWalk Walk = Scene.Mode->WalkStateNow();
	const int32 StacksAfter = Walk.StarvationCurseMovementStacks + Walk.StarvationCurseHealthStacks;
	const FString FiguresAfter = StarvedFigures(*Scene.Base.Character);

	AddInfo(FString::Printf(TEXT("LOAD STARVATION: %s (%s); %d stacks saved, %d after the load; the player saved at "
								 "%s, loaded at %s"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, StacksSaved, StacksAfter,
							*FiguresSaved, *FiguresAfter));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestEqual(TEXT("the saved floor's start was not counted a second time"), StacksAfter, StacksSaved);
	TestEqual(TEXT("and the stacks reach the player as they did"), FiguresAfter, FiguresSaved);
	return true;
}

/**
 * A thrall comes back under the player's command, as it was: the same commander and side, the Second Self, the Fervour
 * it holds back, and its maximum and current health, and the player's own Fervour. Issue #753, Group 2, ruled
 * 2026-10-30.
 *
 * REWRITTEN IN PLACE FROM GROUP 1'S `APlayersThrallIsNotSavedAsACreature`, by ruling R1: that assertion recorded the
 * interim state D3 (a) gave (the thrall skipped) before D3 (b) kept it. THE CONTROL is kept: the ordinary creature
 * beside the thrall comes back an ordinary enemy. A thrall hurt below full shows a load that took it again, which
 * heals to full.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadThrallTest,
	"Cataclysm.SaveLoad.APlayersThrallComesBackUnderThePlayersCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadThrallTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Resource = UCataclysmClassResourceAttributeSet;

	auto Brute = [](const FVector& At)
	{
		FCataclysmSavedCreature Saved;
		Saved.ArchetypeRow = FName(TEXT("Brute"));
		Saved.Location = At;
		Saved.Health = 100.0f;
		Saved.MaxHealth = 100.0f;
		return Saved;
	};
	auto Figures = [](const ACataclysmEnemyCharacter& Creature)
	{
		const UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(&Creature);
		return FString::Printf(TEXT("max %.1f health %.1f"),
							   Abilities ? Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) : -1.0f,
							   Abilities ? Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f);
	};
	auto Fervour = [](const ACataclysmPlayerCharacter& Player)
	{
		const UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(&Player);
		return Abilities ? Abilities->GetNumericAttribute(Resource::GetClassResourceAttribute()) : -1.0f;
	};
	auto RoomForFervour = [](ACataclysmPlayerCharacter& Player)
	{
		if (UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(&Player))
		{
			Abilities->SetNumericAttributeBase(Resource::GetMaxClassResourceAttribute(), 100.0f);
		}
	};

	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	FString FiguresSaved;
	float ReserveSaved = -1.0f;
	float FervourSaved = -1.0f;
	{
		FLoadScene Scene = MakeScene();
		if (!TestTrue(TEXT("set-up: the saving world was built"), Scene.IsUsable()))
		{
			if (Scene.World)
			{
				Scene.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Saved.RunId = FGuid::NewGuid();
		Saved.CharacterId = FGuid::NewGuid();
		Scene.Writer->BeginRun(Saved.RunId, Saved.CharacterId, FName(TEXT("Sandbox")), 1);

		ACataclysmEnemyCharacter* Taken = FCataclysmSaveApply::CreatureInto(*Scene.World, Brute(FVector(500.0f, 0.0f, 100.0f)));
		ACataclysmEnemyCharacter* Wild = FCataclysmSaveApply::CreatureInto(*Scene.World, Brute(FVector(-500.0f, 0.0f, 100.0f)));
		if (!TestNotNull(TEXT("set-up: the first Brute stands"), Taken)
			|| !TestNotNull(TEXT("set-up: the second Brute stands"), Wild)
			|| !TestTrue(TEXT("set-up: the player took the first"), UCataclysmCommand::Subjugate(Scene.Character, Taken)))
		{
			return false;
		}
		// AS THE SKILL LEAVES IT: 30 Fervour held back, and the Second Self. Then hurt below full.
		Taken->ReservedFervour = 30.0f;
		Taken->bIsSecondSelf = true;
		if (UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Taken))
		{
			Abilities->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(),
				Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) * 0.6f);
		}
		RoomForFervour(*Scene.Character);
		if (UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Scene.Character))
		{
			Abilities->SetNumericAttributeBase(Resource::GetClassResourceAttribute(), 20.0f);
		}
		FiguresSaved = Figures(*Taken);
		ReserveSaved = UCataclysmCommand::ReservedFervourOf(Scene.Character);
		FervourSaved = Fervour(*Scene.Character);
		if (!TestEqual(TEXT("set-up: the thrall holds 30 Fervour back"), ReserveSaved, 30.0f)
			|| !TestEqual(TEXT("set-up: the player holds 20 Fervour"), FervourSaved, 20.0f))
		{
			return false;
		}
		Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		WaitForSlot(Saved.RunSlot());
	}

	FLoadScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the loading world was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	RoomForFervour(*Scene.Character);

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.World, *Scene.Character,
																			 Saved.CharacterId);
	ACataclysmEnemyCharacter* Thrall = nullptr;
	ACataclysmEnemyCharacter* Other = nullptr;
	for (TActorIterator<ACataclysmEnemyCharacter> It(Scene.World); It; ++It)
	{
		if (!IsValid(*It))
		{
			continue;
		}
		if (FVector::Dist2D(It->GetActorLocation(), FVector(500.0f, 0.0f, 0.0f)) < 1.0f)
		{
			Thrall = *It;
		}
		else if (FVector::Dist2D(It->GetActorLocation(), FVector(-500.0f, 0.0f, 0.0f)) < 1.0f)
		{
			Other = *It;
		}
	}
	if (!TestNotNull(TEXT("the thrall stands after the load"), Thrall)
		|| !TestNotNull(TEXT("and the creature beside it"), Other))
	{
		return false;
	}
	const bool bCommanded = UCataclysmCommand::CommanderOf(Thrall) == Scene.Character;
	const bool bOnThePlayersSide = Thrall->GetGenericTeamId() == UCataclysmTeams::TeamOf(Scene.Character);
	const FString FiguresAfter = Figures(*Thrall);
	const float ReserveAfter = UCataclysmCommand::ReservedFervourOf(Scene.Character);
	const float FervourAfter = Fervour(*Scene.Character);
	const bool bOtherIsAnEnemy = UCataclysmCommand::CommanderOf(Other) == nullptr
		&& UCataclysmTargeting::IsHostileTo(Other, Scene.Character);

	AddInfo(FString::Printf(TEXT("THRALL LOAD: %s (%s); the thrall %s, %s, %s; saved %s, loaded %s; reserve %.0f saved, "
								 "%.0f loaded; Fervour %.0f saved, %.0f loaded; the other %s"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
							bCommanded ? TEXT("commanded by the player") : TEXT("NOT COMMANDED"),
							bOnThePlayersSide ? TEXT("on the player's side") : TEXT("ON ANOTHER SIDE"),
							Thrall->bIsSecondSelf ? TEXT("the Second Self") : TEXT("NOT THE SECOND SELF"),
							*FiguresSaved, *FiguresAfter, ReserveSaved, ReserveAfter, FervourSaved, FervourAfter,
							bOtherIsAnEnemy ? TEXT("an enemy") : TEXT("NOT AN ENEMY")));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestTrue(TEXT("the thrall is commanded by the player"), bCommanded);
	TestTrue(TEXT("and on the player's side"), bOnThePlayersSide);
	TestTrue(TEXT("and still the Second Self"), Thrall->bIsSecondSelf);
	TestEqual(TEXT("with the maximum and the health it had, not healed"), FiguresAfter, FiguresSaved);
	TestEqual(TEXT("holding back the Fervour it held"), ReserveAfter, ReserveSaved);
	TestEqual(TEXT("and the player has the Fervour it had"), FervourAfter, FervourSaved);
	TestTrue(TEXT("CONTROL: the creature beside it is an ordinary enemy"), bOtherIsAnEnemy);
	return true;
}

// ---------------------------------------------------------------------------
// The walk's state held for creatures
// ---------------------------------------------------------------------------

namespace CataclysmSaveLoadTest
{
	/** Group 2's walk fields, on one line. */
	FString CreatureWalkLine(const FCataclysmSavedWalk& Walk)
	{
		auto Echoes = [](const TArray<FCataclysmSavedEcho>& List)
		{
			FString Out;
			for (const FCataclysmSavedEcho& One : List)
			{
				Out += FString::Printf(TEXT("%s/%d/%d,"), *One.Kind.ToString(), One.Rung, One.Attack);
			}
			return Out;
		};
		FString Potions;
		for (const float Charges : Walk.CorruptedStalkerPotionCharges)
		{
			Potions += FString::Printf(TEXT("%.0f,"), Charges);
		}
		return FString::Printf(TEXT("stalker %s %s %.2f [%s] echoes this [%s] last [%s] walked %.1f"),
							   Walk.bCorruptedStalkerHasHunted ? TEXT("hunted") : TEXT("not-hunted"),
							   Walk.bCorruptedStalkerSlain ? TEXT("slain") : TEXT("standing"),
							   Walk.CorruptedStalkerHealthShare, *Potions, *Echoes(Walk.EchoesThisFloor),
							   *Echoes(Walk.EchoesFromLastFloor), Walk.MetresWalkedSinceCleanse);
	}

	FCataclysmSavedEcho AnEcho(ECataclysmDungeonCreature Kind, int32 Rung, int32 Attack)
	{
		FCataclysmSavedEcho Echo;
		Echo.Kind = FName(CataclysmDungeonCreatureName(Kind));
		Echo.Rung = Rung;
		Echo.Attack = Attack;
		return Echo;
	}

	/** Every Group 1 counter set, and Group 2's fields set to values no other holds. */
	FCataclysmSavedWalk EveryCreatureFieldSet()
	{
		FCataclysmSavedWalk Walk = EveryFieldSet();
		Walk.bCorruptedStalkerHasHunted = true;
		Walk.bCorruptedStalkerSlain = false;
		Walk.CorruptedStalkerHealthShare = 0.65f;
		Walk.CorruptedStalkerPotionCharges = {25.0f, 15.0f, 5.0f, 0.0f};
		Walk.EchoesThisFloor = {AnEcho(ECataclysmDungeonCreature::Brute, 2, 1),
								AnEcho(ECataclysmDungeonCreature::Imp, 0, -2)};
		Walk.EchoesFromLastFloor = {AnEcho(ECataclysmDungeonCreature::Hellhound, 1, 0)};
		Walk.MetresWalkedSinceCleanse = 37.5f;
		return Walk;
	}

	/** A creature's maximum health and attack damage. */
	FString CreatureFigures(const ACataclysmEnemyCharacter& Creature)
	{
		const UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(&Creature);
		return FString::Printf(TEXT("max %.1f damage %.1f"),
							   Abilities ? Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) : -1.0f,
							   Abilities ? Abilities->GetNumericAttribute(UCataclysmCombatAttributeSet::GetAttackDamageAttribute()) : -1.0f);
	}
}

/**
 * The walk's state held for creatures that needs no creature identity comes back with the run: the Corrupted
 * Stalker's hunt, Echoes of the Past's two lists, and Nihil's Embrace's metres walked since the last cleanse. Issue
 * #753, Group 2. THE CONTROL is the loading dungeon's walk before the load.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadWalkCreaturesTest,
	"Cataclysm.SaveLoad.AWalksCreatureStateComesBackFromALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadWalkCreaturesTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	const FCataclysmSavedWalk Set = EveryCreatureFieldSet();
	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	{
		FDungeonScene Scene = MakeDungeonScene();
		if (!TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Saved.RunId = FGuid::NewGuid();
		Saved.CharacterId = FGuid::NewGuid();
		Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName, Scene.Mode->FloorNumber);
		const int32 DungeonId = PlaceADungeonCarrying(*this, Scene, {});
		if (!TestTrue(TEXT("set-up: a dungeon was placed"), DungeonId != INDEX_NONE)
			|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
			|| !TestTrue(TEXT("set-up: the walk reached floor 2"), Scene.Mode->GoToFloor(2)))
		{
			return false;
		}
		Scene.Mode->RestoreWalkState(Set);
		if (!TestEqual(TEXT("set-up: the walk holds every creature field set"),
					   CreatureWalkLine(Scene.Mode->WalkStateNow()), CreatureWalkLine(Set)))
		{
			return false;
		}
		Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		WaitForSlot(Saved.RunSlot());
	}

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const bool bOtherBefore = CreatureWalkLine(Scene.Mode->WalkStateNow()) != CreatureWalkLine(Set);
	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																			 Saved.CharacterId);
	const FString After = CreatureWalkLine(Scene.Mode->WalkStateNow());

	AddInfo(FString::Printf(TEXT("LOAD WALK CREATURES: %s (%s); %s; saved '%s'; loaded '%s'"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
							After == CreatureWalkLine(Set) ? TEXT("every field matches") : TEXT("the fields DIFFER"),
							*CreatureWalkLine(Set), *After));

	TestTrue(TEXT("CONTROL: before the load the dungeon's walk held other values"), bOtherBefore);
	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestEqual(TEXT("the walk's state held for creatures came back"), After, CreatureWalkLine(Set));
	return true;
}

/**
 * A creature holding Soul Harvest's souls, and the boss Nothing Is Forgotten fed, come back as what they were: the
 * souls held and their figures, and the void's boss with its figures, its health not given twice. Issue #753, Group 2.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadCreatureRolesTest,
	"Cataclysm.SaveLoad.ACreaturesHeldSoulsAndTheFedBossComeBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadCreatureRolesTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	FString HolderSaved;
	FString BossSaved;
	{
		FDungeonScene Scene = MakeDungeonScene();
		if (!TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Saved.RunId = FGuid::NewGuid();
		Saved.CharacterId = FGuid::NewGuid();
		Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName, Scene.Mode->FloorNumber);
		const int32 DungeonId = PlaceADungeonCarrying(*this, Scene, {});
		if (!TestTrue(TEXT("set-up: a dungeon was placed"), DungeonId != INDEX_NONE)
			|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
		{
			return false;
		}
		const TArray<ACataclysmEnemyCharacter*> Standing = UnruledStanding(*Scene.Mode);
		if (!TestTrue(TEXT("set-up: the floor has two creatures"), Standing.Num() >= 2))
		{
			return false;
		}

		// THE VOID'S FIGURES FIRST, through the walk, then the two roles through the restore's own call, which is
		// how a rule's state is put on a creature from outside the rule.
		FCataclysmSavedWalk Walk = Scene.Mode->WalkStateNow();
		Walk.NothingIsForgottenHealth = 400.0f;
		Walk.NothingIsForgottenDamage = 50.0f;
		Walk.NothingIsForgottenHealthGiven = 400.0f;
		Walk.NothingIsForgottenDamageGiven = 12.0f;
		Scene.Mode->RestoreWalkState(Walk);
		FCataclysmSavedCreature Holder;
		Holder.SoulHarvestSouls = 3;
		Scene.Mode->RestoreCreatureRoles(*Standing[0], Holder);
		FCataclysmSavedCreature Boss;
		Boss.bFedByNothingIsForgotten = true;
		Scene.Mode->RestoreCreatureRoles(*Standing[1], Boss);

		HolderSaved = FString::Printf(TEXT("%s at %.0f %.0f"), *CreatureFigures(*Standing[0]),
									  Standing[0]->GetActorLocation().X, Standing[0]->GetActorLocation().Y);
		BossSaved = FString::Printf(TEXT("%s at %.0f %.0f"), *CreatureFigures(*Standing[1]),
									Standing[1]->GetActorLocation().X, Standing[1]->GetActorLocation().Y);
		Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		WaitForSlot(Saved.RunSlot());
	}

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																			 Saved.CharacterId);
	FString HolderAfter = TEXT("(no creature holds souls)");
	FString BossAfter = TEXT("(no boss)");
	int32 SoulsAfter = 0;
	for (ACataclysmEnemyCharacter* Creature : UnruledStanding(*Scene.Mode))
	{
		int32 Souls = 0;
		float HealthAdded = 0.0f;
		Scene.Mode->SoulHarvestHeldBy(Creature, Souls, HealthAdded);
		if (Souls > 0)
		{
			SoulsAfter = Souls;
			HolderAfter = FString::Printf(TEXT("%s at %.0f %.0f"), *CreatureFigures(*Creature),
										  Creature->GetActorLocation().X, Creature->GetActorLocation().Y);
		}
		if (Scene.Mode->IsTheVoidsBoss(Creature))
		{
			BossAfter = FString::Printf(TEXT("%s at %.0f %.0f"), *CreatureFigures(*Creature),
										Creature->GetActorLocation().X, Creature->GetActorLocation().Y);
		}
	}

	AddInfo(FString::Printf(TEXT("LOAD SOULS: %s (%s); %d souls held after the load; the holder saved %s, loaded %s; "
								 "the boss saved %s, loaded %s"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, SoulsAfter, *HolderSaved,
							*HolderAfter, *BossSaved, *BossAfter));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestEqual(TEXT("the creature still holds its 3 souls"), SoulsAfter, 3);
	TestEqual(TEXT("with the figures they gave it"), HolderAfter, HolderSaved);
	TestEqual(TEXT("and the void's boss is the void's boss, its health not given twice"), BossAfter, BossSaved);
	return true;
}

// ---------------------------------------------------------------------------
// A restored creature's attack damage
// ---------------------------------------------------------------------------

/**
 * A creature put back from a save is the creature it was: its attack damage, its armour, the starting health its
 * maximum is rebuilt from, and on a dungeon floor the damage multiplier its placement gave it; and an illusion is not
 * saved as a creature at all. Issue #753, Group 2, ruled 2026-10-30. THE FAULTS IT GUARDS, all since #751: nothing saved
 * the damage, the armour or the starting health and the restore set none of them, and an illusion came back real.
 *
 * THE STARTING HEALTH IS CHECKED THROUGH A REBUILD: after the save and after the load, the same
 * `SetCommanderHealthMultiplier(1.5)` call rebuilds each creature's maximum from its starting health, which is what
 * taking a restored creature with `Subjugate` does in play.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadCreatureDamageTest,
	"Cataclysm.SaveLoad.ARestoredCreatureHitsAsHardAsItDid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadCreatureDamageTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	auto Figure = [](const ACataclysmEnemyCharacter& Creature, const FGameplayAttribute& Attribute)
	{
		const UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(&Creature);
		return Abilities ? Abilities->GetNumericAttribute(Attribute) : -1.0f;
	};
	auto Line = [&Figure](ACataclysmEnemyCharacter& Creature)
	{
		const float Damage = Figure(Creature, UCataclysmCombatAttributeSet::GetAttackDamageAttribute());
		const float Armour = Figure(Creature, UCataclysmCombatAttributeSet::GetArmorAttribute());
		Creature.SetCommanderHealthMultiplier(1.5f);
		const float Rebuilt = Figure(Creature, UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		return FString::Printf(TEXT("damage %.1f armour %.1f rebuilt max %.1f"), Damage, Armour, Rebuilt);
	};
	auto CreatureAt = [](UWorld* World, const FVector& At) -> ACataclysmEnemyCharacter*
	{
		for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
		{
			if (IsValid(*It) && FVector::Dist2D(It->GetActorLocation(), At) < 1.0f)
			{
				return *It;
			}
		}
		return nullptr;
	};

	// IN A LEVEL WORLD: a Brute given its own damage, armour and starting health, beside an illusion.
	FString LevelSaved = TEXT("(not saved)");
	FString LevelLoaded = TEXT("(not loaded)");
	bool bIllusionSaved = true;
	bool bBesideItSaved = false;
	const FVector LevelAt(700.0f, 300.0f, 100.0f);
	const FVector IllusionAt(-700.0f, 300.0f, 100.0f);
	{
		FSaved Saved;
		ON_SCOPE_EXIT { Saved.Forget(); };
		{
			FLoadScene Scene = MakeScene();
			if (!TestTrue(TEXT("level: set-up: the saving world was built"), Scene.IsUsable()))
			{
				if (Scene.World)
				{
					Scene.World->DestroyWorld(false);
				}
				return false;
			}
			ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
			Saved.RunId = FGuid::NewGuid();
			Saved.CharacterId = FGuid::NewGuid();
			Scene.Writer->BeginRun(Saved.RunId, Saved.CharacterId, FName(TEXT("Sandbox")), 1);
			auto Brute = [](const FVector& At)
			{
				FCataclysmSavedCreature Record;
				Record.ArchetypeRow = FName(TEXT("Brute"));
				Record.Location = At;
				Record.Health = 100.0f;
				Record.MaxHealth = 100.0f;
				return Record;
			};
			ACataclysmEnemyCharacter* Creature = FCataclysmSaveApply::CreatureInto(*Scene.World, Brute(LevelAt));
			ACataclysmEnemyCharacter* Illusion = FCataclysmSaveApply::CreatureInto(*Scene.World, Brute(IllusionAt));
			if (!TestNotNull(TEXT("level: set-up: the Brute stands"), Creature)
				|| !TestNotNull(TEXT("level: set-up: the illusion stands"), Illusion))
			{
				return false;
			}
			Creature->SetHealth(250.0f);
			Creature->SetArmour(55.0f);
			Creature->SetAttackDamage(37.5f);
			Illusion->SetIsAnIllusion(true);
			Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
			WaitForSlot(Saved.RunSlot());
			if (const UCataclysmRunSave* Written = Read<UCataclysmRunSave>(Saved.RunSlot()))
			{
				bIllusionSaved = false;
				for (const FCataclysmSavedCreature& Record : Written->Floor.Creatures)
				{
					bIllusionSaved |= FVector::Dist2D(Record.Location, IllusionAt) < 1.0f;
					bBesideItSaved |= FVector::Dist2D(Record.Location, LevelAt) < 1.0f;
				}
			}
			LevelSaved = Line(*Creature);
		}
		FLoadScene Scene = MakeScene();
		if (!TestTrue(TEXT("level: set-up: the loading world was built"), Scene.IsUsable()))
		{
			if (Scene.World)
			{
				Scene.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FCataclysmSaveLoad::CharacterAndRun(*Scene.World, *Scene.Character, Saved.CharacterId);
		if (ACataclysmEnemyCharacter* Back = CreatureAt(Scene.World, LevelAt))
		{
			LevelLoaded = Line(*Back);
		}
	}

	// ON A DUNGEON FLOOR: a creature the dungeon placed with its kind's figures, given a placed damage multiplier.
	FString DungeonSaved = TEXT("(not saved)");
	FString DungeonLoaded = TEXT("(not loaded)");
	{
		FSaved Saved;
		ON_SCOPE_EXIT { Saved.Forget(); };
		FVector DungeonAt = FVector::ZeroVector;
		{
			FDungeonScene Scene = MakeDungeonScene();
			if (!TestTrue(TEXT("dungeon: set-up: the saving dungeon world was built"), Scene.IsUsable()))
			{
				if (Scene.Base.World)
				{
					Scene.Base.World->DestroyWorld(false);
				}
				return false;
			}
			ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
			Saved.RunId = FGuid::NewGuid();
			Saved.CharacterId = FGuid::NewGuid();
			Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName,
										Scene.Mode->FloorNumber);
			const int32 DungeonId = PlaceADungeonCarrying(*this, Scene, {});
			if (!TestTrue(TEXT("dungeon: set-up: a dungeon was placed"), DungeonId != INDEX_NONE)
				|| !TestTrue(TEXT("dungeon: set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
			{
				return false;
			}
			const TArray<ACataclysmEnemyCharacter*> Standing = UnruledStanding(*Scene.Mode);
			if (!TestTrue(TEXT("dungeon: set-up: the floor has a creature"), Standing.Num() > 0))
			{
				return false;
			}
			Standing[0]->SetPlacedDamageMultiplier(0.6f);
			DungeonAt = Standing[0]->GetActorLocation();
			Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
			WaitForSlot(Saved.RunSlot());
			DungeonSaved = Line(*Standing[0]);
		}
		FDungeonScene Scene = MakeDungeonScene();
		if (!TestTrue(TEXT("dungeon: set-up: the loading dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character, Saved.CharacterId);
		if (ACataclysmEnemyCharacter* Back = CreatureAt(Scene.Base.World, DungeonAt))
		{
			DungeonLoaded = Line(*Back);
		}
	}

	AddInfo(FString::Printf(TEXT("LOAD DAMAGE: in a level, saved %s, loaded %s; on a dungeon floor, saved %s, loaded "
								 "%s; the illusion %s, the creature beside it %s"),
							*LevelSaved, *LevelLoaded, *DungeonSaved, *DungeonLoaded,
							bIllusionSaved ? TEXT("SAVED") : TEXT("not saved"),
							bBesideItSaved ? TEXT("saved") : TEXT("NOT SAVED")));

	TestEqual(TEXT("in a level world the restored creature is the creature it was"), LevelLoaded, LevelSaved);
	TestEqual(TEXT("and on a dungeon floor"), DungeonLoaded, DungeonSaved);
	TestFalse(TEXT("an illusion is not saved as a creature"), bIllusionSaved);
	TestTrue(TEXT("CONTROL: the creature beside it is"), bBesideItSaved);
	return true;
}

namespace CataclysmSaveLoadTest
{
	/** Each rule's placed cells, named, in the order the rule placed them. */
	using FRuleCells = TArray<TPair<FString, TArray<FIntPoint>>>;

	/** The cells some actors stand on, in the order given, leaving out any not standing. */
	template <typename TActor>
	TArray<FIntPoint> CellsOfActors(const ACataclysmDungeonGameMode& Mode, const TArray<TActor*>& Actors)
	{
		TArray<FIntPoint> Cells;
		for (const TActor* Actor : Actors)
		{
			if (IsValid(Actor) && Mode.CurrentFloor)
			{
				Cells.Add(Mode.CurrentFloor->CellOfWorld(Actor->GetActorLocation()));
			}
		}
		return Cells;
	}

	TArray<FIntPoint> CellsOfPoints(const ACataclysmDungeonGameMode& Mode, const TArray<FVector>& Points)
	{
		TArray<FIntPoint> Cells;
		for (const FVector& Point : Points)
		{
			if (Mode.CurrentFloor)
			{
				Cells.Add(Mode.CurrentFloor->CellOfWorld(Point));
			}
		}
		return Cells;
	}

	TArray<FIntPoint> GateCellsOf(const ACataclysmDungeonGameMode& Mode, const TCHAR* RowKey)
	{
		TArray<FIntPoint> Cells;
		for (const FCataclysmFloorShortcut& Shortcut : Mode.GatedShortcutsOf(FName(RowKey)))
		{
			Cells.Append(Shortcut.Gate);
		}
		return Cells;
	}

	/** Every cell a standing pillar or pit covers, sorted: the world's order is not the placement's. */
	TArray<FIntPoint> ObstacleCells(UWorld& World)
	{
		TArray<FIntPoint> Cells;
		for (TActorIterator<ACataclysmFloorObstacle> It(&World); It; ++It)
		{
			if (IsValid(*It))
			{
				Cells.Append(It->CoveredCells());
			}
		}
		Cells.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
		return Cells;
	}

	FString CellsText(const TArray<FIntPoint>& Cells)
	{
		FString Text;
		for (const FIntPoint& Cell : Cells)
		{
			Text += FString::Printf(TEXT("(%d,%d)"), Cell.X, Cell.Y);
		}
		return Text;
	}

	/**
	 * THE FIRST FLOOR'S RULES: the objects a choice is made at, and the rules that close cells -- the statues, the
	 * chasms, the Soul Chains gates and the section barriers. Shadowy Enemies cannot stand with Lightforged Walls, so it
	 * is on the second floor. Issue #753, Group 3a.
	 */
	TArray<FName> ChoiceAndCellRules()
	{
		using Effects = UCataclysmDungeonModifierEffects;
		return {FName(Effects::ForcedTithesKey), FName(Effects::PactOfTemptationKey),
				FName(Effects::SanctionedPassageKey), FName(Effects::BattlefieldRelicsKey),
				FName(Effects::PandorasBoxKey), FName(Effects::CarrionFeastKey), FName(Effects::InfernalBeaconsKey),
				FName(Effects::WarBannerKey), FName(Effects::GrimTotemsKey), FName(Effects::RealityRiftsKey),
				FName(Effects::LuxuryHoardersKey), FName(Effects::AngelicWardensKey), FName(Effects::ThoseInTheDarkKey),
				FName(Effects::LightforgedWallsKey), FName(Effects::SoulChainsKey)};
	}

	/** THE SECOND FLOOR'S RULES: the creatures and the cells a rule places, and Warzone's points and gates. */
	TArray<FName> CreatureAndZoneRules()
	{
		using Effects = UCataclysmDungeonModifierEffects;
		return {FName(Effects::EternalChorusKey), FName(Effects::NecroticBloomKey), FName(Effects::GoldenSpiresKey),
				FName(Effects::PestilentEmpowermentKey), FName(Effects::PortalUnleashingKey),
				FName(Effects::QuarantineBreachKey), FName(Effects::InfectionBloomKey), FName(Effects::AbyssalRiftsKey),
				FName(Effects::InfestedVeinsKey), FName(Effects::ObsidianSarcophagiKey),
				FName(Effects::WarzoneControlPointsKey), FName(Effects::ShadowyEnemiesKey)};
	}

	/**
	 * What the given floor's rules placed, read through the game mode's public lists. The Locust shelters, Raw Sewage's
	 * rivers and the Void Parasite's light are drawn as zones on the first beat from cells no public list gives, so they
	 * are not here; each is rebuilt fresh, as ruled.
	 */
	FRuleCells RuleCellsNow(const ACataclysmDungeonGameMode& Mode, bool bChoiceAndCellRules)
	{
		using Effects = UCataclysmDungeonModifierEffects;
		FRuleCells Rules;
		const auto One = [&Mode](ACataclysmFloorObject* Object)
		{
			return CellsOfActors(Mode, TArray<ACataclysmFloorObject*>{Object});
		};
		const auto OneCreature = [&Mode](ACataclysmEnemyCharacter* Creature)
		{
			return CellsOfActors(Mode, TArray<ACataclysmEnemyCharacter*>{Creature});
		};
		if (bChoiceAndCellRules)
		{
			Rules.Emplace(TEXT("tithe altar"), One(Mode.TitheAltarNow()));
			Rules.Emplace(TEXT("pact altar"), One(Mode.PactAltarNow()));
			Rules.Emplace(TEXT("divine gate"), One(Mode.DivineGateNow()));
			Rules.Emplace(TEXT("relics"), CellsOfActors(Mode, Mode.BattlefieldRelicsNow()));
			Rules.Emplace(TEXT("boxes"), CellsOfActors(Mode, Mode.PandorasBoxesNow()));
			Rules.Emplace(TEXT("purification altar"), One(Mode.PurificationAltarNow()));
			Rules.Emplace(TEXT("infernal beacons"), CellsOfActors(Mode, Mode.InfernalBeaconsNow()));
			Rules.Emplace(TEXT("war banner"), One(Mode.WarBannerNow()));
			Rules.Emplace(TEXT("totems"), CellsOfActors(Mode, Mode.GrimTotemsNow()));
			Rules.Emplace(TEXT("reality rifts"), Mode.RealityRiftCellsNow());
			Rules.Emplace(TEXT("hoards"), CellsOfPoints(Mode, Mode.LuxuryHoardsNow()));
			Rules.Emplace(TEXT("statues"), Mode.AngelicStatueCellsStanding());
			Rules.Emplace(TEXT("chasms"), Mode.ThoseInTheDarkChasmCellsNow());
			Rules.Emplace(TEXT("soul chains gates"), GateCellsOf(Mode, Effects::SoulChainsKey));
			Rules.Emplace(TEXT("closed barriers"), Mode.ClosedSectionBarrierCells());
		}
		else
		{
			Rules.Emplace(TEXT("chorus sources"), CellsOfActors(Mode, Mode.EternalChorusSourcesNow()));
			Rules.Emplace(TEXT("bloom flowers"), CellsOfActors(Mode, Mode.NecroticBloomFlowersNow()));
			Rules.Emplace(TEXT("spires"), CellsOfActors(Mode, Mode.GoldenSpiresStanding()));
			Rules.Emplace(TEXT("plague beacons"), CellsOfActors(Mode, Mode.PlagueBeaconsStanding()));
			Rules.Emplace(TEXT("portals"), CellsOfActors(Mode, Mode.VoidPortalsNow()));
			Rules.Emplace(TEXT("quarantine"), OneCreature(Mode.QuarantineNow()));
			Rules.Emplace(TEXT("infection bloom"), OneCreature(Mode.InfectionBloomNow()));
			Rules.Emplace(TEXT("abyssal rift"), OneCreature(Mode.AbyssalRiftNow()));
			Rules.Emplace(TEXT("veins"), CellsOfActors(Mode, Mode.InfestedVeinsStanding()));
			Rules.Emplace(TEXT("sarcophagi"), CellsOfActors(Mode, Mode.SarcophagiNow()));
			Rules.Emplace(TEXT("warzone points"), Mode.WarzonePointCellsNow());
			Rules.Emplace(TEXT("warzone gates"), GateCellsOf(Mode, Effects::WarzoneControlPointsKey));
			Rules.Emplace(TEXT("shadow lights"), CellsOfPoints(Mode, Mode.ShadowyEnemiesLightsNow()));
		}
		return Rules;
	}

	/** A dungeon placed by hand as `PlaceADungeonCarrying` places one, of the given sub-type. */
	int32 PlaceADungeonOfSubType(FAutomationTestBase& Test, const FDungeonScene& Scene, const TArray<FName>& Modifiers,
								 ECataclysmDungeonSubType SubType)
	{
		const int32 DungeonId = PlaceADungeonCarrying(Test, Scene, Modifiers);
		if (UCataclysmEmpireRun* Run = Scene.Base.Instance->GetOrBeginEmpireRun())
		{
			for (FCataclysmDungeon& Dungeon : Run->Dungeons)
			{
				if (Dungeon.DungeonId == DungeonId)
				{
					Dungeon.SubType = SubType;
				}
			}
		}
		return DungeonId;
	}

	/**
	 * One floor carrying the given rules, saved on floor 2 and loaded into a fresh world: every rule's cells and every
	 * pillar's, before and after. Then the CONTROL, a build of the same floor on another seed. Issue #753, Group 3a.
	 */
	void RoundTripTheRulesOfOneFloor(FAutomationTestBase& Test, const TCHAR* Which, const TArray<FName>& Modifiers,
									 bool bChoiceAndCellRules, bool bToAFloorWithAClosedBarrier)
	{
		FSaved Saved;
		ON_SCOPE_EXIT { Saved.Forget(); };
		int32 Floor = 2;
		FRuleCells Before;
		TArray<FIntPoint> PillarsBefore;
		{
			FDungeonScene Scene = MakeDungeonScene();
			if (!Test.TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
			{
				if (Scene.Base.World)
				{
					Scene.Base.World->DestroyWorld(false);
				}
				return;
			}
			ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

			Saved.RunId = FGuid::NewGuid();
			Saved.CharacterId = FGuid::NewGuid();
			Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName,
										Scene.Mode->FloorNumber);
			const int32 DungeonId = PlaceADungeonCarrying(Test, Scene, Modifiers);
			if (!Test.TestTrue(TEXT("set-up: a dungeon carrying the rules was placed"), DungeonId != INDEX_NONE)
				|| !Test.TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
			{
				return;
			}
			// FLOOR 2, OR FOR THE BARRIERS THE FIRST FLOOR FROM 2 WITH A CLOSED SECTION BARRIER, so the barriers' list is
			// not compared empty with empty. Not the last of the dungeon's twenty, which places no altar and no chasm.
			bool bReached = Scene.Mode->GoToFloor(Floor);
			while (bReached && bToAFloorWithAClosedBarrier && Scene.Mode->ClosedSectionBarrierCells().IsEmpty()
				   && Floor < 19)
			{
				bReached = Scene.Mode->GoToFloor(++Floor);
			}
			if (!Test.TestTrue(FString::Printf(TEXT("set-up: the walk reached floor %d"), Floor), bReached)
				|| (bToAFloorWithAClosedBarrier
					&& !Test.TestTrue(TEXT("set-up: a floor from 2 to 19 has a closed section barrier"),
									  !Scene.Mode->ClosedSectionBarrierCells().IsEmpty())))
			{
				return;
			}
			Before = RuleCellsNow(*Scene.Mode, bChoiceAndCellRules);
			PillarsBefore = ObstacleCells(*Scene.Base.World);
			for (const TPair<FString, TArray<FIntPoint>>& Rule : Before)
			{
				Test.TestTrue(FString::Printf(TEXT("set-up: %s placed at least one, so equal is not empty and empty"),
											  *Rule.Key),
							  Rule.Value.Num() > 0);
			}
			Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
			WaitForSlot(Saved.RunSlot());
		}

		FDungeonScene Scene = MakeDungeonScene();
		if (!Test.TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World,
																				 *Scene.Base.Character,
																				 Saved.CharacterId);
		const FRuleCells After = RuleCellsNow(*Scene.Mode, bChoiceAndCellRules);
		const TArray<FIntPoint> PillarsAfter = ObstacleCells(*Scene.Base.World);

		FString Counts;
		for (int32 Index = 0; Index < Before.Num(); ++Index)
		{
			const int32 Loaded = After.IsValidIndex(Index) ? After[Index].Value.Num() : -1;
			const bool bSame = After.IsValidIndex(Index) && CellsText(After[Index].Value) == CellsText(Before[Index].Value);
			Counts += FString::Printf(TEXT("%s %d/%d%s; "), *Before[Index].Key, Before[Index].Value.Num(), Loaded,
									  bSame ? TEXT("") : TEXT(" DIFFER"));
		}
		Test.AddInfo(FString::Printf(TEXT("LOAD REBUILD %s on floor %d: %s (%s); %spillar cells %d/%d%s"), Which, Floor,
									 FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, *Counts,
									 PillarsBefore.Num(), PillarsAfter.Num(),
									 CellsText(PillarsAfter) == CellsText(PillarsBefore) ? TEXT("") : TEXT(" DIFFER")));

		Test.TestEqual(TEXT("the load put the character and its run back"),
					   static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
		Test.TestEqual(TEXT("set-up: the same rules were read after the load"), After.Num(), Before.Num());
		for (int32 Index = 0; Index < Before.Num() && Index < After.Num(); ++Index)
		{
			Test.TestEqual(FString::Printf(TEXT("%s: the same cells in the same order"), *Before[Index].Key),
						   CellsText(After[Index].Value), CellsText(Before[Index].Value));
		}
		Test.TestEqual(TEXT("and every pillar and pit stands where it stood"), CellsText(PillarsAfter),
					   CellsText(PillarsBefore));

		// THE CONTROL: the same floor built on another seed places at least one rule's objects elsewhere, so the lists
		// above are the seed's and not the same for any floor.
		Scene.Mode->DungeonSeed += 7919;
		bool bAnotherSeedDiffers = false;
		if (Scene.Mode->GoToFloor(Floor))
		{
			const FRuleCells Other = RuleCellsNow(*Scene.Mode, bChoiceAndCellRules);
			for (int32 Index = 0; Index < Before.Num() && Index < Other.Num(); ++Index)
			{
				bAnotherSeedDiffers |= CellsText(Other[Index].Value) != CellsText(Before[Index].Value);
			}
		}
		Test.TestTrue(TEXT("CONTROL: another seed places at least one rule's objects elsewhere"), bAnotherSeedDiffers);
	}
}

/**
 * A rebuilt floor places every rule's objects, and the pillars and pits, on the cells and in the order it placed them
 * before the save, which is what a save keyed by rule and placement index rests on. Issue #753, Group 3a, ruled
 * 2026-10-30. Two floors, because Shadowy Enemies cannot stand with Lightforged Walls. Each prints "LOAD REBUILD".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadRebuildOrderTest,
	"Cataclysm.SaveLoad.AFloorsRuleObjectsAreRebuiltInTheSameOrderByALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadRebuildOrderTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	RoundTripTheRulesOfOneFloor(*this, TEXT("choices and cells"), ChoiceAndCellRules(), /*bChoiceAndCellRules=*/true,
								/*bToAFloorWithAClosedBarrier=*/true);
	RoundTripTheRulesOfOneFloor(*this, TEXT("creatures and zones"), CreatureAndZoneRules(),
								/*bChoiceAndCellRules=*/false, /*bToAFloorWithAClosedBarrier=*/false);
	return true;
}

/**
 * A Horde arena saved on its third wave comes back as that arena with its objects. The load builds a floor at
 * `StartPlay` first, and the same-arena flag alone kept that floor's geometry and placed none of the arena's objects.
 * Issue #753, Group 3a. Prints "LOAD HORDE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadHordeArenaTest,
	"Cataclysm.SaveLoad.AHordeArenaLoadedPastItsFirstWaveIsTheArenaWithItsObjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadHordeArenaTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	const TArray<FName> ArenaRules = {FName(Effects::EternalChorusKey), FName(Effects::NecroticBloomKey),
									  FName(Effects::GoldenSpiresKey), FName(Effects::PortalUnleashingKey)};
	const auto ArenaText = [](const ACataclysmDungeonGameMode& Mode)
	{
		const FCataclysmFloorPlan& Plan = Mode.CurrentFloor->GetPlan();
		return FString::Printf(TEXT("%d x %d entrance %s"), Plan.Width, Plan.Height, *Plan.Entrance.ToString());
	};
	const auto ObjectsText = [](const ACataclysmDungeonGameMode& Mode)
	{
		return FString::Printf(TEXT("chorus %s bloom %s spires %s portals %s"),
							   *CellsText(CellsOfActors(Mode, Mode.EternalChorusSourcesNow())),
							   *CellsText(CellsOfActors(Mode, Mode.NecroticBloomFlowersNow())),
							   *CellsText(CellsOfActors(Mode, Mode.GoldenSpiresStanding())),
							   *CellsText(CellsOfActors(Mode, Mode.VoidPortalsNow())));
	};

	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	FString ArenaSaved;
	FString ObjectsSaved;
	int32 EachSaved = 0;
	{
		FDungeonScene Scene = MakeDungeonScene();
		if (!TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Saved.RunId = FGuid::NewGuid();
		Saved.CharacterId = FGuid::NewGuid();
		Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName, Scene.Mode->FloorNumber);
		const int32 DungeonId = PlaceADungeonOfSubType(*this, Scene, ArenaRules, ECataclysmDungeonSubType::Horde);
		if (!TestTrue(TEXT("set-up: a Horde dungeon was placed"), DungeonId != INDEX_NONE)
			|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
			|| !TestTrue(TEXT("set-up: the second wave walked in"), Scene.Mode->GoToFloor(2))
			|| !TestTrue(TEXT("set-up: and the third"), Scene.Mode->GoToFloor(3))
			|| !TestTrue(TEXT("set-up: it is a wave in the same arena"), Scene.Mode->FloorBrief.bSameArenaAsLastFloor))
		{
			return false;
		}
		ArenaSaved = ArenaText(*Scene.Mode);
		ObjectsSaved = ObjectsText(*Scene.Mode);
		EachSaved = FMath::Min(FMath::Min(Scene.Mode->EternalChorusSourcesNow().Num(),
										  Scene.Mode->NecroticBloomFlowersNow().Num()),
							   FMath::Min(Scene.Mode->GoldenSpiresStanding().Num(), Scene.Mode->VoidPortalsNow().Num()));
		if (!TestTrue(TEXT("set-up: each of the four rules placed an object in the arena"), EachSaved > 0))
		{
			return false;
		}
		Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		WaitForSlot(Saved.RunSlot());
	}

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE START'S FLOOR IS ANOTHER SHAPE, so keeping it cannot pass for the arena.
	const FString ArenaAtTheStart = ArenaText(*Scene.Mode);
	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																			 Saved.CharacterId);
	const FString ArenaLoaded = ArenaText(*Scene.Mode);
	const FString ObjectsLoaded = ObjectsText(*Scene.Mode);

	AddInfo(FString::Printf(TEXT("LOAD HORDE: %s (%s); the start's floor %s; saved on wave 3 the arena %s, loaded %s; "
								 "saved %s; loaded %s"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, *ArenaAtTheStart, *ArenaSaved,
							*ArenaLoaded, *ObjectsSaved, *ObjectsLoaded));

	TestNotEqual(TEXT("set-up: the floor the start built is not the arena's shape"), ArenaAtTheStart, ArenaSaved);
	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestEqual(TEXT("the arena is the one saved"), ArenaLoaded, ArenaSaved);
	TestEqual(TEXT("and its objects stand where they stood"), ObjectsLoaded, ObjectsSaved);
	return true;
}

/**
 * A restored creature sees as far as it did and keeps the row of its modifiers the floor gave it. Placement sets both
 * and nothing set either again. Issue #753, Group 3a. THE SIGHT IS SET BY HAND to thirty, the Horde figure, because a
 * Horde floor's reality needs a wave past the first; the reality row comes from Unstable Dimensions on floor 2. Prints
 * "LOAD SIGHT".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadSightAndRealityTest,
	"Cataclysm.SaveLoad.ARestoredCreatureKeepsItsSightAndTheFloorsReality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadSightAndRealityTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	const auto CreaturesText = [](const ACataclysmDungeonGameMode& Mode)
	{
		TArray<FString> Each;
		for (const TObjectPtr<ACataclysmEnemyCharacter>& Creature : Mode.FloorEnemies)
		{
			if (IsValid(Creature))
			{
				Each.Add(FString::Printf(TEXT("[%.1f %s]"), Creature->SightRadiusMultiplier,
										 *Creature->FloorRealityRow.ToString()));
			}
		}
		Each.Sort();
		return FString::Join(Each, TEXT(""));
	};

	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	FString CreaturesSaved;
	int32 WithAReality = 0;
	{
		FDungeonScene Scene = MakeDungeonScene();
		if (!TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Saved.RunId = FGuid::NewGuid();
		Saved.CharacterId = FGuid::NewGuid();
		Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName, Scene.Mode->FloorNumber);
		const int32 DungeonId = PlaceADungeonCarrying(
			*this, Scene, {FName(FCataclysmDungeonFloorRules::UnstableDimensionsKey)});
		if (!TestTrue(TEXT("set-up: a dungeon carrying Unstable Dimensions was placed"), DungeonId != INDEX_NONE)
			|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
			|| !TestTrue(TEXT("set-up: the walk reached floor 2"), Scene.Mode->GoToFloor(2)))
		{
			return false;
		}
		for (const TObjectPtr<ACataclysmEnemyCharacter>& Creature : Scene.Mode->FloorEnemies)
		{
			if (IsValid(Creature))
			{
				Creature->SightRadiusMultiplier = 30.0f;
				WithAReality += Creature->FloorRealityRow.IsNone() ? 0 : 1;
			}
		}
		CreaturesSaved = CreaturesText(*Scene.Mode);
		if (!TestTrue(TEXT("set-up: a creature carries the floor's reality"), WithAReality > 0))
		{
			return false;
		}
		Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		WaitForSlot(Saved.RunSlot());
	}

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																			 Saved.CharacterId);
	const FString CreaturesLoaded = CreaturesText(*Scene.Mode);

	AddInfo(FString::Printf(TEXT("LOAD SIGHT: %s (%s); %d of the saved carry a reality; the sight and reality of each, "
								 "saved %s, loaded %s"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, WithAReality,
							*CreaturesSaved.Left(300), *CreaturesLoaded.Left(300)));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestEqual(TEXT("each restored creature sees as far as it did and keeps the floor's row"), CreaturesLoaded,
			  CreaturesSaved);
	return true;
}

/**
 * The floor's objects do not depend on what stood when they were placed: the player on the first statue's cell, a
 * thrall on the first chasm's and a second flight of stairs on the second statue's give the same statues and chasms,
 * in the same order. Walking down and loading leave different things standing, and every later index of a rule moved
 * with one refused cell. Ruled 2026-10-30. Issue #753, Group 3a. Prints "PLACED AROUND".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadPlacedAroundTest,
	"Cataclysm.SaveLoad.AFloorsObjectsDoNotDependOnWhatStoodBeforeThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadPlacedAroundTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 DungeonId = PlaceADungeonCarrying(
		*this, Scene, {FName(Effects::AngelicWardensKey), FName(Effects::ThoseInTheDarkKey)});
	if (!TestTrue(TEXT("set-up: a dungeon carrying statues and chasms was placed"), DungeonId != INDEX_NONE)
		|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
		|| !TestTrue(TEXT("set-up: the walk reached floor 2"), Scene.Mode->GoToFloor(2)))
	{
		return false;
	}
	const TArray<FIntPoint> Statues = Scene.Mode->AngelicStatueCellsStanding();
	const TArray<FIntPoint> Chasms = Scene.Mode->ThoseInTheDarkChasmCellsNow();
	if (!TestTrue(TEXT("set-up: two statues were placed"), Statues.Num() >= 2)
		|| !TestTrue(TEXT("set-up: a chasm was placed"), Chasms.Num() >= 1))
	{
		return false;
	}

	// WHAT STANDS ON THE CELLS THE RULES CHOSE, BEFORE THE SAME FLOOR IS BUILT AGAIN.
	const ACataclysmDungeonFloor& Floor = *Scene.Mode->CurrentFloor;
	const FVector Up(0.0f, 0.0f, 100.0f);
	Scene.Base.Character->SetActorLocation(Floor.WorldOfCell(Statues[0]) + Up);
	FCataclysmSavedCreature ThrallRecord;
	ThrallRecord.ArchetypeRow = FName(TEXT("Brute"));
	ThrallRecord.Location = Floor.WorldOfCell(Chasms[0]) + Up;
	ThrallRecord.Health = 100.0f;
	ThrallRecord.MaxHealth = 100.0f;
	ACataclysmEnemyCharacter* Thrall = FCataclysmSaveApply::CreatureInto(*Scene.Base.World, ThrallRecord);
	ACataclysmDungeonStairs* SecondStairs = Scene.Base.World->SpawnActor<ACataclysmDungeonStairs>(
		ACataclysmDungeonStairs::StaticClass(), Floor.WorldOfCell(Statues[1]), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: the thrall stands"), Thrall)
		|| !TestTrue(TEXT("set-up: the player took it"), UCataclysmCommand::Subjugate(Scene.Base.Character, Thrall))
		|| !TestNotNull(TEXT("set-up: the second stairs stand"), SecondStairs))
	{
		return false;
	}

	// THE CONTROL: the player stands on the first statue's cell as the floor is built again.
	const bool bThePlayerHeldItsCell = Floor.CellOfWorld(Scene.Base.Character->GetActorLocation()) == Statues[0];

	const bool bBuiltAgain = Scene.Mode->GoToFloor(2);
	const int32 LeftHeldOut = Scene.Mode->ActorsHeldOutOfPlacementNow();
	const TArray<FIntPoint> StatuesAgain = Scene.Mode->AngelicStatueCellsStanding();
	const TArray<FIntPoint> ChasmsAgain = Scene.Mode->ThoseInTheDarkChasmCellsNow();

	AddInfo(FString::Printf(TEXT("PLACED AROUND: statues %s, again %s; chasms %s, again %s; the player stood on the "
								 "first statue's cell %s; %d actors still held out after the floor was populated"),
							*CellsText(Statues), *CellsText(StatuesAgain), *CellsText(Chasms), *CellsText(ChasmsAgain),
							bThePlayerHeldItsCell ? TEXT("yes") : TEXT("NO"), LeftHeldOut));

	TestTrue(TEXT("CONTROL: the player stood on the first statue's cell"), bThePlayerHeldItsCell);
	TestTrue(TEXT("the same floor was built again"), bBuiltAgain);
	TestEqual(TEXT("the same statues in the same order"), CellsText(StatuesAgain), CellsText(Statues));
	TestEqual(TEXT("the same chasms in the same order"), CellsText(ChasmsAgain), CellsText(Chasms));
	TestEqual(TEXT("nothing is held out of the cells once the floor is populated"), LeftHeldOut, 0);

	// THE SECOND CASE: THE LAST FLOOR'S PACK SITES. `PopulateFloor` sets them after the objects are placed, so while the
	// statues are placed they are the last floor's, and a statue that avoided one stood elsewhere after a walk than
	// after a load (window w106). A floor from 3 to 19 is walked until two or more of its creatures share a pack middle
	// on one of the statues' cells -- a pack site, a creature's own cell being held by no other -- and floor 2 is built
	// again from it. Ruled 2026-10-30.
	int32 PackSiteFloor = INDEX_NONE;
	FIntPoint OnTheStatue(-1, -1);
	for (int32 Next = 3; Next <= 19 && PackSiteFloor == INDEX_NONE; ++Next)
	{
		if (!Scene.Mode->GoToFloor(Next))
		{
			break;
		}
		TMap<FIntPoint, int32> Middles;
		for (const TObjectPtr<ACataclysmEnemyCharacter>& Creature : Scene.Mode->FloorEnemies)
		{
			if (IsValid(Creature) && Creature->PackMiddleCell != FIntPoint(-1, -1))
			{
				++Middles.FindOrAdd(Creature->PackMiddleCell);
			}
		}
		for (const FIntPoint& Statue : Statues)
		{
			if (Middles.FindRef(Statue) >= 2)
			{
				PackSiteFloor = Next;
				OnTheStatue = Statue;
				break;
			}
		}
	}
	if (!TestTrue(TEXT("set-up: a floor from 3 to 19 left a pack site on one of the statues' cells"),
				  PackSiteFloor != INDEX_NONE))
	{
		return false;
	}
	const bool bBuiltAfterIt = Scene.Mode->GoToFloor(2);
	const TArray<FIntPoint> StatuesAfterIt = Scene.Mode->AngelicStatueCellsStanding();

	AddInfo(FString::Printf(TEXT("PLACED AFTER: floor %d left a pack site on the statue's cell (%d,%d); statues %s, "
								 "built again after it %s"),
							PackSiteFloor, OnTheStatue.X, OnTheStatue.Y, *CellsText(Statues),
							*CellsText(StatuesAfterIt)));

	TestTrue(TEXT("floor 2 was built again after it"), bBuiltAfterIt);
	TestEqual(TEXT("after a floor whose pack site was a statue's cell, the same statues in the same order"),
			  CellsText(StatuesAfterIt), CellsText(Statues));
	return true;
}

namespace CataclysmSaveLoadTest
{
	/** Every floor object standing, as "rule#placement index", sorted: the world's order is not the placement's. */
	FString StandingFloorObjects(UWorld& World)
	{
		TArray<FString> Each;
		for (TActorIterator<ACataclysmFloorObject> It(&World); It; ++It)
		{
			if (IsValid(*It))
			{
				Each.Add(FString::Printf(TEXT("%s#%d"), *It->RuleKey.ToString(), It->PlacementIndex));
			}
		}
		Each.Sort();
		return FString::Join(Each, TEXT(" "));
	}

	/** The uses the game mode holds for the save, in the order made, as "rule#index:choice". */
	FString UsedFloorObjectsLine(const ACataclysmDungeonGameMode& Mode)
	{
		TArray<FString> Each;
		for (const FCataclysmSavedUsedObject& One : Mode.UsedFloorObjectsNow())
		{
			Each.Add(FString::Printf(TEXT("%s#%d:%s"), *One.Rule.ToString(), One.Index, *One.Choice.ToString()));
		}
		return FString::Join(Each, TEXT(" "));
	}

	int32 DropsLying(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<ACataclysmDroppedItem> It(&World); It; ++It)
		{
			Count += IsValid(*It) ? 1 : 0;
		}
		return Count;
	}

	/** The game mode's beat, as `Tick` gives it once `SecondsBetweenWaveChecks` have passed. */
	void BeatOnce(ACataclysmDungeonGameMode& Mode)
	{
		Mode.Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	}

	/** The player stood on a cell of the floor, at the height they stand at. */
	void StandOnCell(ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, FIntPoint Cell)
	{
		const FVector At = Mode.CurrentFloor->WorldOfCell(Cell);
		Player.SetActorLocation(FVector(At.X, At.Y, Player.GetActorLocation().Z));
	}

	/**
	 * THE PACT OFFER A FLOOR DRAWS FIRST, before it is drawn again for matching the last floor's: the shuffle
	 * `ACataclysmDungeonGameMode::PlaceThePactAltar` makes on the floor's own stream, its first three, sorted. The test's
	 * oracle for whether a floor's offer was drawn again.
	 */
	TArray<int32> FirstPactShuffle(int32 PlanSeed, int32 Floor)
	{
		using Effects = UCataclysmDungeonModifierEffects;
		FRandomStream Offers(FCataclysmFloorGenerator::SeedForFloor(
			FCataclysmFloorGenerator::SeedForFloor(PlanSeed, Effects::PactOfTemptationOfferSalt), Floor));
		TArray<int32> Pacts = {Effects::PactWrath, Effects::PactHaste, Effects::PactBulwark, Effects::PactGreed,
							   Effects::PactBlood};
		for (int32 Index = Pacts.Num() - 1; Index > 0; --Index)
		{
			Pacts.Swap(Index, Offers.RandRange(0, Index));
		}
		TArray<int32> Offer(Pacts.GetData(), Effects::PactsOffered);
		Offer.Sort();
		return Offer;
	}

	/** `Cataclysm.DungeonSeed` pinned for as long as this stands, and put back after; empire dungeons read it too. */
	struct FPinnedDungeonSeed
	{
		FPinnedDungeonSeed()
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.DungeonSeed"));
			if (Variable)
			{
				Previous = Variable->GetString();
			}
		}
		~FPinnedDungeonSeed()
		{
			if (Variable)
			{
				Variable->Set(*Previous, ECVF_SetByConsole);
			}
		}
		void Pin(int32 Seed)
		{
			if (Variable)
			{
				Variable->Set(*FString::FromInt(Seed), ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		FString Previous;
	};

	FString PactsText(const TArray<int32>& Pacts)
	{
		TArray<FString> Each;
		for (const int32 Pact : Pacts)
		{
			Each.Add(FString::FromInt(Pact));
		}
		return FString::Join(Each, TEXT(","));
	}
}

/**
 * The floor objects a player used come back used after a load, by rule and placement index, and nothing they paid or
 * charged is paid or charged again: the tithe refused, the pact taken, the gate's channel begun, a relic, a box, the
 * altar, a beacon, the banner and both totems -- the first and then the one placed second, by then first in its list --
 * and the rifts' gift and chamber reward. Issue #753, Group 3b, ruled 2026-10-30. On the first floor from 2 with a
 * hidden chamber. Prints "LOAD USED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadUsedChoicesTest,
	"Cataclysm.SaveLoad.AFloorsUsedChoicesComeBackUsedAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadUsedChoicesTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	const TArray<FName> Rules = {FName(Effects::ForcedTithesKey), FName(Effects::PactOfTemptationKey),
								 FName(Effects::SanctionedPassageKey), FName(Effects::BattlefieldRelicsKey),
								 FName(Effects::PandorasBoxKey), FName(Effects::CarrionFeastKey),
								 FName(Effects::InfernalBeaconsKey), FName(Effects::WarBannerKey),
								 FName(Effects::GrimTotemsKey), FName(Effects::RealityRiftsKey)};
	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	int32 Floor = 2;
	FString StandingSaved;
	FString UsedSaved;
	int32 DropsSaved = -1;
	int32 StacksSaved = -1;
	int32 PactsSaved = -1;
	{
		FDungeonScene Scene = MakeDungeonScene();
		if (!TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Saved.RunId = FGuid::NewGuid();
		Saved.CharacterId = FGuid::NewGuid();
		Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName, Scene.Mode->FloorNumber);
		const int32 DungeonId = PlaceADungeonCarrying(*this, Scene, Rules);
		if (!TestTrue(TEXT("set-up: a dungeon carrying the ten rules was placed"), DungeonId != INDEX_NONE)
			|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
		{
			return false;
		}
		ACataclysmDungeonGameMode& Mode = *Scene.Mode;
		ACataclysmPlayerCharacter& Player = *Scene.Base.Character;
		bool bReached = Mode.GoToFloor(Floor);
		while (bReached && !Mode.RealityChamberStands() && Floor < 19)
		{
			bReached = Mode.GoToFloor(++Floor);
		}
		if (!TestTrue(FString::Printf(TEXT("set-up: the walk reached floor %d"), Floor), bReached)
			|| !TestTrue(TEXT("set-up: a floor from 2 to 19 has a hidden chamber"), Mode.RealityChamberStands()))
		{
			return false;
		}

		// THE RIFTS' GIFT AND THE CHAMBER'S REWARD, taken as their steps take them: the rifts drawn on a beat, the
		// player stood on the gift and then on the chamber's rift, a beat each, and stood back at the entrance.
		BeatOnce(Mode);
		StandOnCell(Mode, Player, Mode.RealityRiftCellsNow()[2 * Effects::RealityRiftPairs]);
		BeatOnce(Mode);
		StandOnCell(Mode, Player, Mode.RealityChamberRiftCellNow());
		BeatOnce(Mode);
		const FVector Entrance = Mode.CurrentFloor->EntranceWorld();
		Player.SetActorLocation(FVector(Entrance.X, Entrance.Y, Player.GetActorLocation().Z));
		if (!TestTrue(TEXT("set-up: the gift was taken"), Mode.RealityGiftTaken())
			|| !TestTrue(TEXT("set-up: the chamber paid its reward"), Mode.RealityChamberRewardGiven()))
		{
			return false;
		}

		// EACH OBJECT USED AS A PLAYER USES IT, through the choice panel's own call.
		const auto Choose = [this, &Mode](ACataclysmFloorObject* Object, FName Key, const TCHAR* What)
		{
			return TestTrue(FString::Printf(TEXT("set-up: %s acted"), What),
							Object != nullptr && Mode.ChooseAtFloorObject(Object, Key));
		};
		const auto First = [](const TArray<ACataclysmFloorObject*>& Objects)
		{
			return Objects.IsEmpty() ? nullptr : Objects[0];
		};
		const TArray<int32> Offer = Mode.PactsOfferedNow();
		bool bAll = Choose(Mode.TitheAltarNow(), FName(Effects::ForcedTithesRefuse), TEXT("refusing the tithe"));
		bAll &= Choose(Mode.PactAltarNow(), Offer.IsEmpty() ? NAME_None : FName(Effects::PactName(Offer[0])),
					   TEXT("taking a pact"));
		bAll &= Choose(Mode.DivineGateNow(), FName(Effects::SanctionedPassageChannel), TEXT("beginning the channel"));
		bAll &= Choose(First(Mode.BattlefieldRelicsNow()), FName(Effects::BattlefieldRelicsActivate),
					   TEXT("a relic"));
		bAll &= Choose(First(Mode.PandorasBoxesNow()), FName(Effects::PandorasBoxOpen), TEXT("a box"));
		bAll &= Choose(Mode.PurificationAltarNow(), FName(Effects::CarrionFeastConsecrate), TEXT("the altar"));
		bAll &= Choose(First(Mode.InfernalBeaconsNow()), FName(Effects::InfernalBeaconsActivate), TEXT("a beacon"));
		bAll &= Choose(Mode.WarBannerNow(), FName(Effects::WarBannerPlant), TEXT("the banner"));
		bAll &= Choose(First(Mode.GrimTotemsNow()), FName(Effects::GrimTotemsEmbrace), TEXT("the first totem"));
		bAll &= Choose(First(Mode.GrimTotemsNow()), FName(Effects::GrimTotemsCleanse),
					   TEXT("the totem placed second, first in its list by then"));
		if (!bAll || !TestEqual(TEXT("set-up: ten uses are recorded"), Mode.UsedFloorObjectsNow().Num(), 10))
		{
			return false;
		}
		StandingSaved = StandingFloorObjects(*Scene.Base.World);
		UsedSaved = UsedFloorObjectsLine(Mode);
		DropsSaved = DropsLying(*Scene.Base.World);
		StacksSaved = Mode.InfernalBeaconStacksNow();
		PactsSaved = Mode.WalkStateNow().PactsTaken;
		Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		WaitForSlot(Saved.RunSlot());
	}

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																			 Saved.CharacterId);
	const ACataclysmDungeonGameMode& Mode = *Scene.Mode;
	const FString StandingLoaded = StandingFloorObjects(*Scene.Base.World);
	const FString UsedLoaded = UsedFloorObjectsLine(Mode);
	const int32 DropsLoaded = DropsLying(*Scene.Base.World);

	AddInfo(FString::Printf(TEXT("LOAD USED on floor %d: %s (%s); standing saved '%s', loaded '%s'; used saved '%s', "
								 "loaded '%s'; drops %d/%d; beacon stacks %d/%d; pacts taken %d/%d; gift %s, chamber %s, "
								 "altar %s, banner %s, channel %s"),
							Floor, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, *StandingSaved,
							*StandingLoaded, *UsedSaved, *UsedLoaded, DropsSaved, DropsLoaded, StacksSaved,
							Mode.InfernalBeaconStacksNow(), PactsSaved, Mode.WalkStateNow().PactsTaken,
							Mode.RealityGiftTaken() ? TEXT("taken") : TEXT("NOT TAKEN"),
							Mode.RealityChamberRewardGiven() ? TEXT("paid") : TEXT("NOT PAID"),
							Mode.AltarIsConsecrated() ? TEXT("consecrated") : TEXT("NOT CONSECRATED"),
							Mode.WarBannerIsPlanted() ? TEXT("planted") : TEXT("NOT PLANTED"),
							Mode.DivineGateChannelBegun() ? TEXT("begun again") : TEXT("NOT BEGUN")));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestEqual(TEXT("the same floor objects stand, by rule and placement index"), StandingLoaded, StandingSaved);
	TestEqual(TEXT("and the uses are held again for the next save"), UsedLoaded, UsedSaved);
	TestEqual(TEXT("no drop was paid again"), DropsLoaded, DropsSaved);
	TestEqual(TEXT("no beacon stack was added again"), Mode.InfernalBeaconStacksNow(), StacksSaved);
	TestEqual(TEXT("no pact was taken again"), Mode.WalkStateNow().PactsTaken, PactsSaved);
	TestTrue(TEXT("the gift stays taken"), Mode.RealityGiftTaken());
	TestTrue(TEXT("the chamber's reward stays paid"), Mode.RealityChamberRewardGiven());
	TestTrue(TEXT("the altar stays consecrated"), Mode.AltarIsConsecrated());
	TestTrue(TEXT("the banner stays planted"), Mode.WarBannerIsPlanted());
	TestTrue(TEXT("the gate's channel, begun and not finished, begins again"), Mode.DivineGateChannelBegun());
	return true;
}

/**
 * A pact altar after a load offers the pacts it offered, on a floor whose offer was drawn again because its first draw
 * matched the last floor's; and the next floor's draw avoids it as the walk's did. Issue #753, Group 3b. Prints "LOAD
 * PACT".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadPactOfferTest,
	"Cataclysm.SaveLoad.ALoadedPactAltarOffersThePactsItOffered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CataclysmSaveLoadTest
{
	/** The pact test's body, which pins `Cataclysm.DungeonSeed` in a guard that puts it back on every way out. */
	bool ThePactOfferRoundTrip(FAutomationTestBase& Test)
	{
		using namespace CataclysmSaveLoadTest;
		using Effects = UCataclysmDungeonModifierEffects;

		FSaved Saved;
		ON_SCOPE_EXIT { Saved.Forget(); };
		// THE DUNGEON'S SEED PINNED FOR THE WHOLE TEST, THE LOAD INCLUDED, so the floor found is the floor loaded.
		FPinnedDungeonSeed PinnedSeed;
		if (!Test.TestNotNull(TEXT("set-up: the dungeon's seed can be pinned"), PinnedSeed.Variable))
		{
			return false;
		}
		int32 Seed = INDEX_NONE;
		int32 Agreed = 0;
		int32 Disagreed = 0;
		int32 Floor = INDEX_NONE;
		TArray<int32> OfferSaved;
		TArray<int32> FirstDraw;
		TArray<int32> NextFloorsOffer;
		{
			FDungeonScene Scene = MakeDungeonScene();
			if (!Test.TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
			{
				if (Scene.Base.World)
				{
					Scene.Base.World->DestroyWorld(false);
				}
				return false;
			}
			ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

			Saved.RunId = FGuid::NewGuid();
			Saved.CharacterId = FGuid::NewGuid();
			Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName, Scene.Mode->FloorNumber);
			const int32 DungeonId = PlaceADungeonCarrying(Test, Scene, {FName(Effects::PactOfTemptationKey)});
			if (!Test.TestTrue(TEXT("set-up: a dungeon carrying the Pact of Temptation was placed"), DungeonId != INDEX_NONE)
				|| !Test.TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
			{
				return false;
			}
			ACataclysmDungeonGameMode& Mode = *Scene.Mode;

			// THE FIRST FLOOR WHOSE FIRST DRAW MATCHED THE LAST FLOOR'S OFFER, so its offer was drawn again: a load drawing
			// it with no last offer would offer the first draw instead. Floors 2 to 18 on dungeon seeds 1 to 12 in turn, one
			// floor in ten or so being drawn again. AND THE ORACLE CHECKED on every other floor: its first draw is what it
			// offered.
			TArray<int32> LastOffer = Mode.PactsOfferedNow();
			for (int32 Pinned = 1; Pinned <= 12 && Floor == INDEX_NONE; ++Pinned)
			{
				PinnedSeed.Pin(Pinned);
				for (int32 Next = 2; Next <= 18 && Floor == INDEX_NONE; ++Next)
				{
					if (!Mode.GoToFloor(Next))
					{
						break;
					}
					const TArray<int32> First = FirstPactShuffle(Mode.CurrentFloor->GetPlan().Seed, Next);
					if (First == LastOffer)
					{
						Floor = Next;
						Seed = Pinned;
						FirstDraw = First;
					}
					else if (First == Mode.PactsOfferedNow())
					{
						++Agreed;
					}
					else
					{
						++Disagreed;
					}
					LastOffer = Mode.PactsOfferedNow();
				}
			}
			if (!Test.TestEqual(TEXT("set-up: the oracle's first draw is what every floor not drawn again offered"), Disagreed, 0)
				|| !Test.TestTrue(TEXT("set-up: a floor on seeds 1 to 12 drew its offer again"), Floor != INDEX_NONE))
			{
				return false;
			}
			OfferSaved = Mode.PactsOfferedNow();
			Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
			WaitForSlot(Saved.RunSlot());

			// AND WHAT THE WALK'S NEXT FLOOR OFFERS, after the save is written.
			if (!Test.TestTrue(TEXT("set-up: the walk went on to the next floor"), Mode.GoToFloor(Floor + 1)))
			{
				return false;
			}
			NextFloorsOffer = Mode.PactsOfferedNow();
		}

		FDungeonScene Scene = MakeDungeonScene();
		if (!Test.TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																				 Saved.CharacterId);
		const TArray<int32> OfferLoaded = Scene.Mode->PactsOfferedNow();
		const bool bNextReached = Scene.Mode->GoToFloor(Floor + 1);
		const TArray<int32> NextLoaded = Scene.Mode->PactsOfferedNow();

		Test.AddInfo(FString::Printf(TEXT("LOAD PACT on seed %d floor %d (the oracle agreed on %d floors, disagreed on %d): %s (%s); "
									 "first draw %s; offered %s, after the load %s; the next floor offered %s, after the load %s"),
								Seed, Floor, Agreed, Disagreed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
								*PactsText(FirstDraw),
								*PactsText(OfferSaved), *PactsText(OfferLoaded), *PactsText(NextFloorsOffer),
								*PactsText(NextLoaded)));

		Test.TestNotEqual(TEXT("CONTROL: the floor's first draw is not what it offered"), PactsText(FirstDraw),
					 PactsText(OfferSaved));
		Test.TestEqual(TEXT("the load put the character and its run back"),
				  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
		Test.TestEqual(TEXT("the altar offers the pacts it offered"), PactsText(OfferLoaded), PactsText(OfferSaved));
		Test.TestTrue(TEXT("the next floor was reached"), bNextReached);
		Test.TestEqual(TEXT("and the next floor offers what the walk's did"), PactsText(NextLoaded), PactsText(NextFloorsOffer));
		return true;
	}
}

bool FCataclysmSaveLoadPactOfferTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	// THE DUNGEON'S SEED AS IT WAS, read before the body pins it and compared after the body's guard has put it back,
	// so a later test never runs on a pinned seed. Asked by the coordinating session, 2026-10-30.
	IConsoleVariable* SeedVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.DungeonSeed"));
	const FString SeedBefore = SeedVariable ? SeedVariable->GetString() : FString();
	const bool bRan = ThePactOfferRoundTrip(*this);
	TestEqual(TEXT("the dungeon's seed is put back as it was"), SeedVariable ? SeedVariable->GetString() : FString(),
			  SeedBefore);
	return bRan;
}

/**
 * A tithe left unpaid on the last floor is still owed after a load taken before the beat that brings the angels, and
 * the angels come on the beat after it. Issue #753, Group 3b. Prints "LOAD TITHE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadTitheOwedTest,
	"Cataclysm.SaveLoad.ATitheLeftUnpaidIsStillOwedAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadTitheOwedTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	{
		FDungeonScene Scene = MakeDungeonScene();
		if (!TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Saved.RunId = FGuid::NewGuid();
		Saved.CharacterId = FGuid::NewGuid();
		Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName, Scene.Mode->FloorNumber);
		const int32 DungeonId = PlaceADungeonCarrying(*this, Scene, {FName(Effects::ForcedTithesKey)});
		if (!TestTrue(TEXT("set-up: a dungeon carrying Forced Tithes was placed"), DungeonId != INDEX_NONE)
			|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
			|| !TestNotNull(TEXT("set-up: floor 1 has its altar"), Scene.Mode->TitheAltarNow())
			|| !TestTrue(TEXT("set-up: the walk left it unpaid for floor 2"), Scene.Mode->GoToFloor(2))
			|| !TestTrue(TEXT("set-up: the angels are owed"), Scene.Mode->TitheAngelsAreOwed())
			|| !TestEqual(TEXT("set-up: and none has come yet"), Scene.Mode->TitheAngelsStanding().Num(), 0))
		{
			return false;
		}
		Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		WaitForSlot(Saved.RunSlot());
	}

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																			 Saved.CharacterId);
	const bool bOwed = Scene.Mode->TitheAngelsAreOwed();
	BeatOnce(*Scene.Mode);
	const int32 Came = Scene.Mode->TitheAngelsStanding().Num();

	AddInfo(FString::Printf(TEXT("LOAD TITHE: %s (%s); owed after the load %s; %d angel(s) came on the beat"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
							bOwed ? TEXT("yes") : TEXT("NO"), Came));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestTrue(TEXT("the angels are still owed"), bOwed);
	TestEqual(TEXT("and they come on the next beat"), Came, Effects::ForcedTithesAngelCount);
	return true;
}

/**
 * A Divine Gate whose channel finished stays open after a load, the stairs unsealed; the rebuilt floor places the gate
 * again and the load takes it away without a channel. Issue #753, Group 3b. Prints "LOAD GATE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadGateOpenedTest,
	"Cataclysm.SaveLoad.ADivineGateOpenedStaysOpenAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadGateOpenedTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	{
		FDungeonScene Scene = MakeDungeonScene();
		if (!TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Saved.RunId = FGuid::NewGuid();
		Saved.CharacterId = FGuid::NewGuid();
		Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName, Scene.Mode->FloorNumber);
		const int32 DungeonId = PlaceADungeonCarrying(*this, Scene, {FName(Effects::SanctionedPassageKey)});
		if (!TestTrue(TEXT("set-up: a dungeon carrying Sanctioned Passage was placed"), DungeonId != INDEX_NONE)
			|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
			|| !TestTrue(TEXT("set-up: the walk reached floor 2"), Scene.Mode->GoToFloor(2)))
		{
			return false;
		}
		ACataclysmDungeonGameMode& Mode = *Scene.Mode;
		ACataclysmFloorObject* Gate = Mode.DivineGateNow();
		if (!TestNotNull(TEXT("set-up: the floor has its gate"), Gate)
			|| !TestTrue(TEXT("set-up: and it seals the stairs"), Mode.SanctionedPassageSealsTheStairs()))
		{
			return false;
		}
		// THE CHANNEL TO ITS END, the player at the gate and no creature on the floor to come for them.
		Mode.ClearFloorEnemies();
		const FVector At = Gate->GetActorLocation();
		Scene.Base.Character->SetActorLocation(FVector(At.X, At.Y, Scene.Base.Character->GetActorLocation().Z));
		if (!TestTrue(TEXT("set-up: the channel began"),
					  Mode.ChooseAtFloorObject(Gate, FName(Effects::SanctionedPassageChannel))))
		{
			return false;
		}
		const int32 Beats = FMath::CeilToInt(Effects::SanctionedPassageChannelSeconds
											  / ACataclysmDungeonGameMode::SecondsBetweenWaveChecks) + 1;
		for (int32 Index = 0; Index < Beats && Mode.DivineGateNow(); ++Index)
		{
			BeatOnce(Mode);
		}
		if (!TestNull(TEXT("set-up: the channel finished and the gate is gone"), Mode.DivineGateNow())
			|| !TestFalse(TEXT("set-up: and the stairs are unsealed"), Mode.SanctionedPassageSealsTheStairs()))
		{
			return false;
		}
		Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		WaitForSlot(Saved.RunSlot());
	}

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																			 Saved.CharacterId);
	const bool bGateGone = Scene.Mode->DivineGateNow() == nullptr;
	const bool bSealed = Scene.Mode->SanctionedPassageSealsTheStairs();

	AddInfo(FString::Printf(TEXT("LOAD GATE: %s (%s); the gate %s after the load; the stairs %s; channel %s"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
							bGateGone ? TEXT("gone") : TEXT("STANDS"), bSealed ? TEXT("SEALED") : TEXT("unsealed"),
							Scene.Mode->DivineGateChannelBegun() ? TEXT("BEGUN") : TEXT("not begun")));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestTrue(TEXT("the gate is gone"), bGateGone);
	TestFalse(TEXT("and the stairs are unsealed"), bSealed);
	TestFalse(TEXT("and no channel runs"), Scene.Mode->DivineGateChannelBegun());
	return true;
}

namespace CataclysmSaveLoadTest
{
	/**
	 * A creature killed by one blow from the player, as the floor rules' tests kill one: its sacrificial bond taken off,
	 * its health, shield and evasion set so the blow is the kill, and the blow through the game's own hit, so its death
	 * notice reaches the floor's rules. Copied from `CataclysmDungeonModifierEffectsTests.cpp`'s `KillIt`.
	 */
	bool KillOnTheFloor(FAutomationTestBase& Test, ACataclysmPlayerCharacter& Player, ACataclysmEnemyCharacter* Victim,
						const TCHAR* What)
	{
		if (!Test.TestNotNull(FString::Printf(TEXT("set-up: %s stands"), What), Victim))
		{
			return false;
		}
		Victim->ModifierRows.Remove(FName(UCataclysmEnemyModifiers::SacrificialBondRow));
		if (UAbilitySystemComponent* Abilities = Victim->GetAbilitySystemComponent())
		{
			Abilities->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100.0f);
			Abilities->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
			Abilities->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		}
		// A TEST'S KILL DOES NOTHING TO THE KILLER: the hit carries the game's own no-retaliation mark, which Thorns of
		// Glass's retaliation and a reflected melee hit both read and answer nothing to. Without it a 100000 hit on a
		// creature carrying Thorns of Glass dealt half of it back and killed the test player, and its placement was not
		// saved (window w114, 2026-10-31). The mark and not the creature's Retaliation attribute at nought, because the
		// amount is read through the stat pipeline with the attribute only as its fallback. Issue #753, Group 3d-2.
		FCataclysmHitDelivery NoRetaliation;
		NoRetaliation.bCannotBeRetaliatedAgainst = true;
		UCataclysmSkillEffects::ApplyHit(&Player, Victim, 100000.0f, FGameplayTagContainer(), NoRetaliation);
		return Test.TestTrue(FString::Printf(TEXT("set-up: %s died"), What), UCataclysmSkillEffects::IsDead(Victim));
	}

	/** The living creatures the floor's rules did not raise: what a save keeps and a load puts back. */
	int32 OrdinaryCreaturesStanding(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<ACataclysmEnemyCharacter> It(&World); It; ++It)
		{
			Count += IsValid(*It) && !It->bRaisedByARule && !UCataclysmSkillEffects::IsDead(*It) ? 1 : 0;
		}
		return Count;
	}

	/** The test body of the rule creatures' round trip; its seed pin is put back by its guard. */
	bool TheRuleCreaturesRoundTrip(FAutomationTestBase& Test)
	{
		using Effects = UCataclysmDungeonModifierEffects;

		const TArray<FName> Rules = {FName(Effects::QuarantineBreachKey), FName(Effects::InfectionBloomKey),
									 FName(Effects::AbyssalRiftsKey), FName(Effects::InfestedVeinsKey),
									 FName(Effects::PestilentEmpowermentKey), FName(Effects::ObsidianSarcophagiKey),
									 FName(Effects::AngelicWardensKey)};
		FSaved Saved;
		ON_SCOPE_EXIT { Saved.Forget(); };
		FPinnedDungeonSeed PinnedSeed;
		if (!Test.TestNotNull(TEXT("set-up: the dungeon's seed can be pinned"), PinnedSeed.Variable))
		{
			return false;
		}
		int32 Seed = INDEX_NONE;
		int32 PatchesSaved = -1;
		float BurnSaved = -1.0f;
		int32 VeinsStandingSaved = -1;
		int32 VeinsDestroyedSaved = -1;
		int32 BeaconsStandingSaved = -1;
		int32 SuccessesSaved = -1;
		FString StatuesSaved;
		FIntPoint WokenCell(-1, -1);
		int32 CreaturesSaved = -1;
		int32 DropsSaved = -1;
		{
			FDungeonScene Scene = MakeDungeonScene();
			if (!Test.TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
			{
				if (Scene.Base.World)
				{
					Scene.Base.World->DestroyWorld(false);
				}
				return false;
			}
			ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

			Saved.RunId = FGuid::NewGuid();
			Saved.CharacterId = FGuid::NewGuid();
			Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName,
										Scene.Mode->FloorNumber);
			const int32 DungeonId = PlaceADungeonCarrying(Test, Scene, Rules);
			if (!Test.TestTrue(TEXT("set-up: a dungeon carrying the seven rules was placed"), DungeonId != INDEX_NONE)
				|| !Test.TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
			{
				return false;
			}
			ACataclysmDungeonGameMode& Mode = *Scene.Mode;
			ACataclysmPlayerCharacter& Player = *Scene.Base.Character;

			// FLOOR 2 ON THE FIRST PINNED SEED WHOSE FLOOR HOLDS EVERY RULE'S CREATURE, statues placing fewer on some.
			for (int32 Pinned = 1; Pinned <= 20 && Seed == INDEX_NONE; ++Pinned)
			{
				PinnedSeed.Pin(Pinned);
				if (Mode.GoToFloor(2) && Mode.QuarantineNow() && Mode.InfectionBloomNow() && Mode.AbyssalRiftNow()
					&& !Mode.InfestedVeinsStanding().IsEmpty() && !Mode.PlagueBeaconsStanding().IsEmpty()
					&& !Mode.SarcophagiNow().IsEmpty() && !Mode.AngelicStatueCellsStanding().IsEmpty())
				{
					Seed = Pinned;
				}
			}
			if (!Test.TestTrue(TEXT("set-up: a seed from 1 to 20 places every rule's creature on floor 2"),
							   Seed != INDEX_NONE))
			{
				return false;
			}

			// EACH EVENT THROUGH THE GAME'S OWN CALLS.
			bool bAll = KillOnTheFloor(Test, Player, Mode.QuarantineNow(), TEXT("the containment"));
			const TArray<ACataclysmEnemyCharacter*> Released = Mode.QuarantineReleasedStanding();
			bAll = bAll && KillOnTheFloor(Test, Player, Released.IsEmpty() ? nullptr : Released[0],
										  TEXT("a released creature"));
			bAll = bAll && KillOnTheFloor(Test, Player, Mode.InfectionBloomNow(), TEXT("the bloom"));
			bAll = bAll && KillOnTheFloor(Test, Player, Mode.InfestedVeinsStanding()[0], TEXT("a vein"));
			bAll = bAll && KillOnTheFloor(Test, Player, Mode.PlagueBeaconsStanding()[0], TEXT("a beacon"));
			ACataclysmEnemyCharacter* Coffin = Mode.SarcophagiNow()[0];
			for (int32 Death = 0; bAll && Death < 2; ++Death)
			{
				FCataclysmSavedCreature Record;
				Record.ArchetypeRow = FName(TEXT("Brute"));
				Record.Location = Coffin->GetActorLocation() + FVector(300.0f, 100.0f * Death, 0.0f);
				Record.Health = 100.0f;
				Record.MaxHealth = 100.0f;
				ACataclysmEnemyCharacter* Beside = FCataclysmSaveApply::CreatureInto(*Scene.Base.World, Record);
				if (Beside)
				{
					Beside->bDiesUnpaid = false;
					Mode.FloorEnemies.Add(Beside);
				}
				bAll = KillOnTheFloor(Test, Player, Beside, TEXT("a creature beside the first coffin"));
			}
			WokenCell = Mode.AngelicStatueCellsStanding()[0];
			bAll = bAll && Test.TestEqual(TEXT("set-up: a divine law broken at a statue woke one"),
										  Mode.NoteDivineLawViolated(Mode.CurrentFloor->WorldOfCell(WokenCell)), 1);
			// THE RIFT OPENED AS THE PLAYER OPENS IT, and the beat also counts the vein destroyed.
			const FVector RiftAt = Mode.AbyssalRiftNow()->GetActorLocation();
			Player.SetActorLocation(FVector(RiftAt.X + 300.0f, RiftAt.Y, Player.GetActorLocation().Z));
			BeatOnce(Mode);
			const FVector Entrance = Mode.CurrentFloor->EntranceWorld();
			Player.SetActorLocation(FVector(Entrance.X, Entrance.Y, Player.GetActorLocation().Z));
			if (!bAll
				|| !Test.TestEqual(TEXT("set-up: the rift opened"), static_cast<int32>(Mode.AbyssalRiftStateNow()),
								   static_cast<int32>(ACataclysmDungeonGameMode::ERiftState::Open))
				|| !Test.TestEqual(TEXT("set-up: one vein counted destroyed"), Mode.InfestedVeinsDestroyedHere(), 1)
				|| !Test.TestEqual(TEXT("set-up: two paid deaths beside the first coffin"),
								   Mode.SarcophagusDeathsBeside(Coffin), 2)
				|| !Test.TestEqual(TEXT("set-up: one patch burns"), Mode.QuarantinePatchesNow().Num(), 1))
			{
				return false;
			}
			PatchesSaved = Mode.QuarantinePatchesNow().Num();
			BurnSaved = Mode.QuarantinePatchesNow()[0]->DamagePerTick;
			VeinsStandingSaved = Mode.InfestedVeinsStanding().Num();
			VeinsDestroyedSaved = Mode.InfestedVeinsDestroyedHere();
			BeaconsStandingSaved = Mode.PlagueBeaconsStanding().Num();
			SuccessesSaved = Mode.AbyssalRiftSuccessesHeld();
			StatuesSaved = CellsText(Mode.AngelicStatueCellsStanding());
			CreaturesSaved = OrdinaryCreaturesStanding(*Scene.Base.World);
			DropsSaved = DropsLying(*Scene.Base.World);
			Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
			WaitForSlot(Saved.RunSlot());
		}

		FDungeonScene Scene = MakeDungeonScene();
		if (!Test.TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World,
																				 *Scene.Base.Character,
																				 Saved.CharacterId);
		ACataclysmDungeonGameMode& Mode = *Scene.Mode;
		// ONE BEAT FIRST, so a vein counted destroyed again, guardians brought or a gate opened would show.
		BeatOnce(Mode);
		const TArray<ACataclysmGroundZone*> Patches = Mode.QuarantinePatchesNow();
		const float BurnLoaded = Patches.IsEmpty() ? -1.0f : Patches[0]->DamagePerTick;
		TArray<FIntPoint> Pillars;
		for (TActorIterator<ACataclysmFloorObstacle> It(Scene.Base.World); It; ++It)
		{
			if (IsValid(*It))
			{
				Pillars.Append(It->CoveredCells());
			}
		}
		const TArray<ACataclysmEnemyCharacter*> Coffins = Mode.SarcophagiNow();
		const int32 DeathsLoaded = Coffins.IsEmpty() ? -1 : Mode.SarcophagusDeathsBeside(Coffins[0]);
		const bool bLordLoaded = !Coffins.IsEmpty() && Mode.SarcophagusLordCame(Coffins[0]);

		Test.AddInfo(FString::Printf(
			TEXT("LOAD RULE CREATURES on seed %d: %s (%s); containment %s, patches %d/%d burn %.2f/%.2f; bloom %s; rift "
				 "state %d, successes %d/%d; veins standing %d/%d destroyed %d/%d guardians %s; beacons standing %d/%d; "
				 "coffin deaths 2/%d lord %s; statues %s/%s, woken cell pillared %s; creatures %d/%d; drops %d/%d"),
			Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
			Mode.QuarantineNow() ? TEXT("STANDS") : TEXT("broken"), PatchesSaved, Patches.Num(), BurnSaved, BurnLoaded,
			Mode.InfectionBloomNow() ? TEXT("STANDS") : TEXT("destroyed"),
			static_cast<int32>(Mode.AbyssalRiftStateNow()), SuccessesSaved, Mode.AbyssalRiftSuccessesHeld(),
			VeinsStandingSaved, Mode.InfestedVeinsStanding().Num(), VeinsDestroyedSaved,
			Mode.InfestedVeinsDestroyedHere(), Mode.InfestedVeinsGuardiansCame() ? TEXT("CAME") : TEXT("none"),
			BeaconsStandingSaved, Mode.PlagueBeaconsStanding().Num(), DeathsLoaded, bLordLoaded ? TEXT("CAME") : TEXT("none"),
			*StatuesSaved, *CellsText(Mode.AngelicStatueCellsStanding()),
			Pillars.Contains(WokenCell) ? TEXT("YES") : TEXT("no"), CreaturesSaved,
			OrdinaryCreaturesStanding(*Scene.Base.World), DropsSaved, DropsLying(*Scene.Base.World)));

		Test.TestEqual(TEXT("the load put the character and its run back"),
					   static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
		Test.TestNull(TEXT("the containment stays broken"), Mode.QuarantineNow());
		Test.TestEqual(TEXT("its patch burns again"), Patches.Num(), PatchesSaved);
		Test.TestEqual(TEXT("at the burn it was charged at"), BurnLoaded, BurnSaved, 0.001f);
		Test.TestNull(TEXT("the bloom stays destroyed"), Mode.InfectionBloomNow());
		Test.TestEqual(TEXT("the rift, opened, is closed"), static_cast<int32>(Mode.AbyssalRiftStateNow()),
					   static_cast<int32>(ACataclysmDungeonGameMode::ERiftState::Closed));
		Test.TestEqual(TEXT("and failed: no success counted"), Mode.AbyssalRiftSuccessesHeld(), SuccessesSaved);
		Test.TestEqual(TEXT("the veins standing are as saved"), Mode.InfestedVeinsStanding().Num(), VeinsStandingSaved);
		Test.TestEqual(TEXT("the vein destroyed is counted once, after a beat"), Mode.InfestedVeinsDestroyedHere(),
					   VeinsDestroyedSaved);
		Test.TestFalse(TEXT("and no guardians came"), Mode.InfestedVeinsGuardiansCame());
		Test.TestEqual(TEXT("the beacons standing are as saved"), Mode.PlagueBeaconsStanding().Num(),
					   BeaconsStandingSaved);
		Test.TestEqual(TEXT("the first coffin holds its two deaths"), DeathsLoaded, 2);
		Test.TestFalse(TEXT("and no lord came"), bLordLoaded);
		Test.TestEqual(TEXT("the statues standing are as saved"), CellsText(Mode.AngelicStatueCellsStanding()),
					   StatuesSaved);
		Test.TestFalse(TEXT("and the woken statue's cell holds no pillar"), Pillars.Contains(WokenCell));
		Test.TestEqual(TEXT("nothing was released, raised or brought again"), OrdinaryCreaturesStanding(*Scene.Base.World),
					   CreaturesSaved);
		Test.TestEqual(TEXT("and nothing was paid again"), DropsLying(*Scene.Base.World), DropsSaved);
		return true;
	}

	/** The test body of the Soul Chains round trip; its seed pin is put back by its guard. */
	bool TheSoulChainsRoundTrip(FAutomationTestBase& Test)
	{
		using Effects = UCataclysmDungeonModifierEffects;
		const FName Row(Effects::SoulChainsKey);

		FSaved Saved;
		ON_SCOPE_EXIT { Saved.Forget(); };
		FPinnedDungeonSeed PinnedSeed;
		if (!Test.TestNotNull(TEXT("set-up: the dungeon's seed can be pinned"), PinnedSeed.Variable))
		{
			return false;
		}
		int32 Seed = INDEX_NONE;
		int32 DropsSaved = -1;
		int32 RollsSaved = -1;
		{
			FDungeonScene Scene = MakeDungeonScene();
			if (!Test.TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
			{
				if (Scene.Base.World)
				{
					Scene.Base.World->DestroyWorld(false);
				}
				return false;
			}
			ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

			Saved.RunId = FGuid::NewGuid();
			Saved.CharacterId = FGuid::NewGuid();
			Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName,
										Scene.Mode->FloorNumber);
			const int32 DungeonId = PlaceADungeonCarrying(Test, Scene, {Row});
			if (!Test.TestTrue(TEXT("set-up: a dungeon carrying Soul Chains was placed"), DungeonId != INDEX_NONE)
				|| !Test.TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
			{
				return false;
			}
			ACataclysmDungeonGameMode& Mode = *Scene.Mode;

			// A HALLS FLOOR WITH TWO GATES, EACH WITH TWO BEARERS, on the first pinned seed that gives one.
			for (int32 Pinned = 1; Pinned <= 20 && Seed == INDEX_NONE; ++Pinned)
			{
				PinnedSeed.Pin(Pinned);
				if (Mode.GoToFloor(2) && Mode.GatedShortcutsOf(Row).Num() == 2
					&& Mode.SoulChainBearersNow(0).Num() == 2 && Mode.SoulChainBearersNow(1).Num() == 2)
				{
					Seed = Pinned;
				}
			}
			if (!Test.TestTrue(TEXT("set-up: a seed from 1 to 20 gives two gates of two bearers"), Seed != INDEX_NONE))
			{
				return false;
			}
			bool bAll = true;
			for (ACataclysmEnemyCharacter* Bearer : Mode.SoulChainBearersNow(0))
			{
				bAll = bAll && KillOnTheFloor(Test, *Scene.Base.Character, Bearer, TEXT("a bearer of the first gate"));
			}
			BeatOnce(Mode);
			if (!bAll || !Test.TestTrue(TEXT("set-up: the first gate opened"), Mode.GatedShortcutIsOpen(Row, 0))
				|| !Test.TestFalse(TEXT("set-up: the second stays closed"), Mode.GatedShortcutIsOpen(Row, 1)))
			{
				return false;
			}
			RollsSaved = Mode.SoulChainsRewardRollsMade();
			DropsSaved = DropsLying(*Scene.Base.World);
			Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
			WaitForSlot(Saved.RunSlot());
		}

		FDungeonScene Scene = MakeDungeonScene();
		if (!Test.TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World,
																				 *Scene.Base.Character,
																				 Saved.CharacterId);
		ACataclysmDungeonGameMode& Mode = *Scene.Mode;
		BeatOnce(Mode);
		const bool bFirstOpen = Mode.GatedShortcutIsOpen(Row, 0);
		const bool bSecondOpen = Mode.GatedShortcutIsOpen(Row, 1);
		const int32 Bearers = Mode.SoulChainBearersNow(1).Num();
		const int32 Drops = DropsLying(*Scene.Base.World);
		const int32 RollsAfter = Mode.SoulChainsRewardRollsMade();

		Test.AddInfo(FString::Printf(TEXT("LOAD SOUL CHAINS on seed %d: %s (%s); after a beat the first gate %s, the "
										  "second %s with %d bearer(s); rolls before the save %d, after the load %d; "
										  "drops %d/%d"),
									 Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
									 bFirstOpen ? TEXT("open") : TEXT("CLOSED"), bSecondOpen ? TEXT("OPEN") : TEXT("closed"),
									 Bearers, RollsSaved, RollsAfter, DropsSaved, Drops));

		Test.TestEqual(TEXT("CONTROL: the first gate paid once before the save"), RollsSaved, 1);
		Test.TestEqual(TEXT("the load put the character and its run back"),
					   static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
		Test.TestTrue(TEXT("the gate opened before the save stays open"), bFirstOpen);
		Test.TestFalse(TEXT("the closed gate stays closed after a beat"), bSecondOpen);
		Test.TestEqual(TEXT("with its two bearers"), Bearers, 2);
		Test.TestEqual(TEXT("and no gate rolled its reward again"), RollsAfter, 0);
		Test.TestEqual(TEXT("nor dropped anything"), Drops, DropsSaved);
		return true;
	}
}

/**
 * The floor's rule creatures and their counters come back after a load as they were, and nothing their deaths paid or
 * charged is paid or charged again: the containment broken and its patch at the burn charged, the bloom destroyed, an
 * opened rift closed and failed (U2), a vein destroyed and counted once, a beacon destroyed, a coffin's two deaths, a
 * statue woken. One beat runs after the load first. Issue #753, Group 3c-1, ruled 2026-10-30. Prints "LOAD RULE
 * CREATURES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadRuleCreaturesTest,
	"Cataclysm.SaveLoad.ARulesCreaturesAndCountersComeBackAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadRuleCreaturesTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	IConsoleVariable* SeedVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.DungeonSeed"));
	const FString SeedBefore = SeedVariable ? SeedVariable->GetString() : FString();
	const bool bRan = TheRuleCreaturesRoundTrip(*this);
	TestEqual(TEXT("the dungeon's seed is put back as it was"), SeedVariable ? SeedVariable->GetString() : FString(),
			  SeedBefore);
	return bRan;
}

/**
 * An Ascendant's Heavenly Core destroyed stays destroyed after a load, the stairs unsealed. Issue #753, Group 3c-1.
 * Prints "LOAD CORE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadCoreTest,
	"Cataclysm.SaveLoad.AHeavenlyCoreDestroyedStaysDestroyedAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadCoreTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	FSaved Saved;
	ON_SCOPE_EXIT { Saved.Forget(); };
	const int32 Floors = 3;
	{
		FDungeonScene Scene = MakeDungeonScene();
		if (!TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		Saved.RunId = FGuid::NewGuid();
		Saved.CharacterId = FGuid::NewGuid();
		Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName, Scene.Mode->FloorNumber);
		const int32 DungeonId = PlaceADungeonCarrying(*this, Scene, {});
		// AN ASCENDANT, AS THE CORE'S OWN TEST MAKES ONE: a Celestial quest of three floors.
		if (UCataclysmEmpireRun* Run = Scene.Base.Instance->GetOrBeginEmpireRun())
		{
			for (FCataclysmDungeon& Dungeon : Run->Dungeons)
			{
				if (Dungeon.DungeonId == DungeonId)
				{
					Dungeon.Floors = Floors;
					Dungeon.Type = ECataclysmDungeonType::Quest;
					Dungeon.SubType = ECataclysmDungeonSubType::Ascendant;
					Dungeon.Cataclysm = ECataclysmType::Celestial;
				}
			}
		}
		if (!TestTrue(TEXT("set-up: an Ascendant was placed"), DungeonId != INDEX_NONE)
			|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
			|| !TestTrue(TEXT("set-up: its last floor was reached"), Scene.Mode->GoToFloor(Floors))
			|| !TestTrue(TEXT("set-up: the core seals the stairs"), Scene.Mode->HeavenlyCoreSealsTheStairs())
			|| !KillOnTheFloor(*this, *Scene.Base.Character, Scene.Mode->HeavenlyCoreNow(), TEXT("the core"))
			|| !TestFalse(TEXT("set-up: and no longer seals them"), Scene.Mode->HeavenlyCoreSealsTheStairs()))
		{
			return false;
		}
		Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
		WaitForSlot(Saved.RunSlot());
	}

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World, *Scene.Base.Character,
																			 Saved.CharacterId);
	const bool bCoreGone = Scene.Mode->HeavenlyCoreNow() == nullptr;
	const bool bSealed = Scene.Mode->HeavenlyCoreSealsTheStairs();

	AddInfo(FString::Printf(TEXT("LOAD CORE: %s (%s); the core %s after the load; the stairs %s"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
							bCoreGone ? TEXT("gone") : TEXT("STANDS"), bSealed ? TEXT("SEALED") : TEXT("unsealed")));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestTrue(TEXT("the core stays destroyed"), bCoreGone);
	TestFalse(TEXT("and the stairs stay unsealed"), bSealed);
	return true;
}

/**
 * A Soul Chains gate neither opens nor pays on a load: a gate opened before the save stays open without paying again,
 * and a closed gate keeps its bearers, which the load re-links instead of choosing. Until this a load destroyed the
 * rebuilt floor's bearers and every closed gate opened and paid on the next beat. Issue #753, Group 3c-1, ruled
 * 2026-10-30. Prints "LOAD SOUL CHAINS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadSoulChainsTest,
	"Cataclysm.SaveLoad.ASoulChainsGateNeitherOpensNorPaysOnALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadSoulChainsTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	IConsoleVariable* SeedVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.DungeonSeed"));
	const FString SeedBefore = SeedVariable ? SeedVariable->GetString() : FString();
	const bool bRan = TheSoulChainsRoundTrip(*this);
	TestEqual(TEXT("the dungeon's seed is put back as it was"), SeedVariable ? SeedVariable->GetString() : FString(),
			  SeedBefore);
	return bRan;
}

namespace CataclysmSaveLoadTest
{
	/** A console variable held at a value for as long as this stands, and put back after. */
	struct FPinnedConsoleValue
	{
		FPinnedConsoleValue(const TCHAR* Name, const TCHAR* Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Variable)
			{
				Previous = Variable->GetString();
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}
		~FPinnedConsoleValue()
		{
			if (Variable)
			{
				Variable->Set(*Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		FString Previous;
	};

	/** What the one dungeon rule on a stat of the player's is worth, in percent or points, or 0 when none is there. */
	float PlayersDungeonRuleOn(ACataclysmPlayerCharacter& Player, const TCHAR* Stat)
	{
		const UCataclysmAbilitySystemComponent* Abilities =
			Cast<UCataclysmAbilitySystemComponent>(Player.GetAbilitySystemComponent());
		const FCataclysmStatInputs* Inputs = Abilities ? Abilities->GetStatInputs(FName(Stat)) : nullptr;
		if (!Inputs)
		{
			return 0.0f;
		}
		for (const FCataclysmStatModifier& Modifier : Inputs->Modifiers)
		{
			if (Modifier.Source == ECataclysmModifierSource::DungeonRule)
			{
				return Modifier.Value;
			}
		}
		return 0.0f;
	}

	/** How many beats a stretch of seconds takes, rounded up. */
	int32 BeatsOver(float Seconds)
	{
		return FMath::CeilToInt(Seconds / ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	}

	/** The player stood at the floor's entrance. */
	void StandAtTheEntrance(ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player)
	{
		const FVector At = Mode.CurrentFloor->EntranceWorld();
		Player.SetActorLocation(FVector(At.X, At.Y, Player.GetActorLocation().Z));
	}

	/** The Labrynth's gates as a row of 1 for open and 0 for closed, by index. */
	FString LabrynthGatesText(const ACataclysmDungeonGameMode& Mode)
	{
		const FName Row(UCataclysmDungeonModifierEffects::LabrynthKey);
		FString Text;
		for (int32 Index = 0; Index < Mode.GatedShortcutsOf(Row).Num(); ++Index)
		{
			Text += Mode.GatedShortcutIsOpen(Row, Index) ? TEXT("1") : TEXT("0");
		}
		return Text;
	}

	/**
	 * THE NEXT `Count` SWAPS THE LABRYNTH MAKES, as the open gates after each, beating until they have happened or
	 * twice their time and two swaps more has passed. A swap the placement rule refuses changes nothing and is not one
	 * of them.
	 */
	TArray<FString> TheNextSwaps(ACataclysmDungeonGameMode& Mode, int32 Count)
	{
		TArray<FString> Swaps;
		FString Last = LabrynthGatesText(Mode);
		const int32 Most = (2 * Count + 2) * BeatsOver(UCataclysmDungeonModifierEffects::LabrynthSecondsBetweenSwaps);
		for (int32 Beat = 0; Beat < Most && Swaps.Num() < Count; ++Beat)
		{
			BeatOnce(Mode);
			const FString Now = LabrynthGatesText(Mode);
			if (Now != Last)
			{
				Swaps.Add(Now);
				Last = Now;
			}
		}
		return Swaps;
	}

	/** The test body of the Warzone hold's round trip; its seed pin is put back by its guard. */
	bool TheWarzoneRoundTrip(FAutomationTestBase& Test)
	{
		using Effects = UCataclysmDungeonModifierEffects;
		const FName Row(Effects::WarzoneControlPointsKey);

		FSaved Saved;
		ON_SCOPE_EXIT { Saved.Forget(); };
		FPinnedDungeonSeed PinnedSeed;
		if (!Test.TestNotNull(TEXT("set-up: the dungeon's seed can be pinned"), PinnedSeed.Variable))
		{
			return false;
		}
		int32 Seed = INDEX_NONE;
		float HeldSaved = -1.0f;
		int32 AlliesSaved = -1;
		{
			FDungeonScene Scene = MakeDungeonScene();
			if (!Test.TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
			{
				if (Scene.Base.World)
				{
					Scene.Base.World->DestroyWorld(false);
				}
				return false;
			}
			ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

			Saved.RunId = FGuid::NewGuid();
			Saved.CharacterId = FGuid::NewGuid();
			Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName,
										Scene.Mode->FloorNumber);
			const int32 DungeonId = PlaceADungeonCarrying(Test, Scene, {Row});
			if (!Test.TestTrue(TEXT("set-up: a dungeon carrying Warzone Control Points was placed"),
							   DungeonId != INDEX_NONE)
				|| !Test.TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
			{
				return false;
			}
			ACataclysmDungeonGameMode& Mode = *Scene.Mode;
			ACataclysmPlayerCharacter& Player = *Scene.Base.Character;

			// A FLOOR OF TWO POINTS OR MORE WHOSE FIRST HAS A SHORTCUT, AND NONE WITHIN REACH OF THE ENTRANCE, on the
			// first pinned seed that gives one.
			for (int32 Pinned = 1; Pinned <= 20 && Seed == INDEX_NONE; ++Pinned)
			{
				PinnedSeed.Pin(Pinned);
				if (!Mode.GoToFloor(2) || Mode.WarzonePointCellsNow().Num() < 2
					|| Mode.GatedShortcutLeadsTo(Row, 0).IsEmpty())
				{
					continue;
				}
				bool bClear = true;
				for (const FIntPoint& Cell : Mode.WarzonePointCellsNow())
				{
					bClear = bClear && FVector::Dist2D(Mode.CurrentFloor->WorldOfCell(Cell),
													   Mode.CurrentFloor->EntranceWorld())
						> 2.0f * Effects::WarzoneControlPointRadiusCm;
				}
				Seed = bClear ? Pinned : INDEX_NONE;
			}
			if (!Test.TestTrue(TEXT("set-up: a seed from 1 to 20 gives two points, the first with a shortcut"),
							   Seed != INDEX_NONE))
			{
				return false;
			}

			// POINT 0 CAPTURED, THEN TEN SECONDS IN POINT 1, THEN THE ENTRANCE.
			StandOnCell(Mode, Player, Mode.WarzonePointCellsNow()[0]);
			for (int32 Beat = 0; Beat < BeatsOver(Effects::WarzoneCaptureSeconds); ++Beat)
			{
				BeatOnce(Mode);
			}
			if (!Test.TestEqual(TEXT("set-up: point 0 held"), Mode.WarzonePointsHeld(), 1)
				|| !Test.TestTrue(TEXT("set-up: and its shortcut open"), Mode.GatedShortcutIsOpen(Row, 0)))
			{
				return false;
			}
			StandOnCell(Mode, Player, Mode.WarzonePointCellsNow()[1]);
			for (int32 Beat = 0; Beat < BeatsOver(10.0f); ++Beat)
			{
				BeatOnce(Mode);
			}
			StandAtTheEntrance(Mode, Player);
			BeatOnce(Mode);
			HeldSaved = Mode.WarzoneSecondsHeldNow().IsValidIndex(1) ? Mode.WarzoneSecondsHeldNow()[1] : -1.0f;
			AlliesSaved = Mode.WarzoneAlliesStanding().Num();
			if (!Test.TestEqual(TEXT("set-up: ten seconds in point 1"), HeldSaved, 10.0f, 0.001f))
			{
				return false;
			}
			Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
			WaitForSlot(Saved.RunSlot());
		}

		FDungeonScene Scene = MakeDungeonScene();
		if (!Test.TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World,
																				 *Scene.Base.Character,
																				 Saved.CharacterId);
		ACataclysmDungeonGameMode& Mode = *Scene.Mode;
		ACataclysmPlayerCharacter& Player = *Scene.Base.Character;
		StandAtTheEntrance(Mode, Player);
		BeatOnce(Mode);
		const int32 Held = Mode.WarzonePointsHeld();
		const bool bShortcutOpen = Mode.GatedShortcutIsOpen(Row, 0);
		const float HeldAfter =
			Mode.WarzoneSecondsHeldNow().IsValidIndex(1) ? Mode.WarzoneSecondsHeldNow()[1] : -1.0f;
		const float Damage = PlayersDungeonRuleOn(Player, TEXT("attack_damage"));
		const int32 Allies = Mode.WarzoneAlliesStanding().Num();

		Test.AddInfo(FString::Printf(TEXT("LOAD WARZONE on seed %d: %s (%s); after a beat %d point(s) held, point 0's "
										  "shortcut %s, point 1 at %.2f/%.2f s, attack damage rule %.2f, allies %d "
										  "(%d before the save)"),
									 Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, Held,
									 bShortcutOpen ? TEXT("open") : TEXT("CLOSED"), HeldAfter, HeldSaved, Damage, Allies,
									 AlliesSaved));

		Test.TestTrue(TEXT("CONTROL: the capture brought allied soldiers before the save"), AlliesSaved > 0);
		Test.TestEqual(TEXT("the load put the character and its run back"),
					   static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
		Test.TestEqual(TEXT("the captured point stays held"), Held, 1);
		Test.TestTrue(TEXT("and its shortcut open"), bShortcutOpen);
		Test.TestEqual(TEXT("the seconds stood at point 1 come back"), HeldAfter, HeldSaved, 0.001f);
		Test.TestEqual(TEXT("the point's strength is on the player once"), Damage,
					   Effects::WarzoneDamageMorePercentPerPoint, 0.001f);
		// RULED 2026-10-31: "and no allied soldiers came with the load" (Allies == 0) recorded the interim, while the allies
		// standing at a save were 3d's; since 3d-3b they are saved as records (Q5 of 3d) and stand again. That a load
		// brings none of its own, not calling `BringWarzoneAllies`, is "and no more".
		Test.TestEqual(TEXT("the allies standing at the save stand again, and no more"), Allies, AlliesSaved);
		return true;
	}

	/** The test body of the Labrynth's round trip; its seed pin is put back by its guard. */
	bool TheLabrynthRoundTrip(FAutomationTestBase& Test)
	{
		using Effects = UCataclysmDungeonModifierEffects;
		const FName Row(Effects::LabrynthKey);
		const int32 Swaps = 8;

		FSaved Saved;
		ON_SCOPE_EXIT { Saved.Forget(); };
		FPinnedDungeonSeed PinnedSeed;
		if (!Test.TestNotNull(TEXT("set-up: the dungeon's seed can be pinned"), PinnedSeed.Variable))
		{
			return false;
		}
		int32 Seed = INDEX_NONE;
		int32 Gates = 0;
		FString GatesSaved;
		TArray<FString> Unloaded;
		{
			FDungeonScene Scene = MakeDungeonScene();
			if (!Test.TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
			{
				if (Scene.Base.World)
				{
					Scene.Base.World->DestroyWorld(false);
				}
				return false;
			}
			ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

			Saved.RunId = FGuid::NewGuid();
			Saved.CharacterId = FGuid::NewGuid();
			Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName,
										Scene.Mode->FloorNumber);
			const int32 DungeonId = PlaceADungeonCarrying(Test, Scene, {Row});
			if (!Test.TestTrue(TEXT("set-up: a dungeon carrying The Labrynth was placed"), DungeonId != INDEX_NONE)
				|| !Test.TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
			{
				return false;
			}
			ACataclysmDungeonGameMode& Mode = *Scene.Mode;
			ACataclysmPlayerCharacter& Player = *Scene.Base.Character;

			// FOUR GATES, ON THE FIRST PINNED SEED THAT GIVES THEM; THREE IF NONE DOES (Q4), and the count is printed.
			for (const int32 Wanted : {4, 3})
			{
				for (int32 Pinned = 1; Pinned <= 20 && Seed == INDEX_NONE; ++Pinned)
				{
					PinnedSeed.Pin(Pinned);
					if (Mode.GoToFloor(2) && Mode.GatedShortcutsOf(Row).Num() == Wanted)
					{
						Seed = Pinned;
						Gates = Wanted;
					}
				}
			}
			if (!Test.TestTrue(TEXT("set-up: a seed from 1 to 20 gives four or three Labrynth gates"), Seed != INDEX_NONE))
			{
				return false;
			}

			// NO CREATURE HOLDS A CELL AND THE PLAYER STANDS AT THE ENTRANCE, so a close the placement rule refuses is
			// refused in both runs alike. One swap, then seven seconds into the next twenty.
			Mode.ClearFloorEnemies();
			StandAtTheEntrance(Mode, Player);
			if (!Test.TestEqual(TEXT("set-up: a first swap"), TheNextSwaps(Mode, 1).Num(), 1))
			{
				return false;
			}
			for (int32 Beat = 0; Beat < BeatsOver(7.0f); ++Beat)
			{
				BeatOnce(Mode);
			}
			GatesSaved = LabrynthGatesText(Mode);
			Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
			WaitForSlot(Saved.RunSlot());

			// THE SWAPS THE SAME GAME MAKES WITHOUT A LOAD.
			Unloaded = TheNextSwaps(Mode, Swaps);
		}
		if (!Test.TestEqual(TEXT("set-up: eight swaps without a load"), Unloaded.Num(), Swaps))
		{
			return false;
		}

		FDungeonScene Scene = MakeDungeonScene();
		if (!Test.TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World,
																				 *Scene.Base.Character,
																				 Saved.CharacterId);
		ACataclysmDungeonGameMode& Mode = *Scene.Mode;
		StandAtTheEntrance(Mode, *Scene.Base.Character);
		const FString GatesLoaded = LabrynthGatesText(Mode);
		const TArray<FString> Loaded = TheNextSwaps(Mode, Swaps);

		Test.AddInfo(FString::Printf(TEXT("LOAD LABRYNTH on seed %d, %d gates: %s (%s); gates %s/%s; swaps without a "
										  "load %s, after it %s"),
									 Seed, Gates, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
									 *GatesLoaded, *GatesSaved, *FString::Join(Unloaded, TEXT(" ")),
									 *FString::Join(Loaded, TEXT(" "))));

		Test.TestEqual(TEXT("the load put the character and its run back"),
					   static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
		Test.TestEqual(TEXT("the gates come back open and closed as saved"), GatesLoaded, GatesSaved);
		Test.TestEqual(TEXT("and the next eight swaps are the ones made without a load"),
					   FString::Join(Loaded, TEXT(" ")), FString::Join(Unloaded, TEXT(" ")));
		return true;
	}

	/** The test body of the Void Parasite's round trip; its seed pin and roll pin are put back by their guards. */
	bool TheVoidParasiteRoundTrip(FAutomationTestBase& Test)
	{
		using Effects = UCataclysmDungeonModifierEffects;
		const FName Row(Effects::VoidParasiteKey);

		FSaved Saved;
		ON_SCOPE_EXIT { Saved.Forget(); };
		FPinnedDungeonSeed PinnedSeed;
		FPinnedConsoleValue Rises(TEXT("Cataclysm.VoidParasiteRoll"), TEXT("0"));
		if (!Test.TestNotNull(TEXT("set-up: the dungeon's seed can be pinned"), PinnedSeed.Variable)
			|| !Test.TestNotNull(TEXT("set-up: the voidling roll can be pinned"), Rises.Variable))
		{
			return false;
		}
		PinnedSeed.Pin(1);
		int32 StacksSaved = -1;
		{
			FDungeonScene Scene = MakeDungeonScene();
			if (!Test.TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
			{
				if (Scene.Base.World)
				{
					Scene.Base.World->DestroyWorld(false);
				}
				return false;
			}
			ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

			Saved.RunId = FGuid::NewGuid();
			Saved.CharacterId = FGuid::NewGuid();
			Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName,
										Scene.Mode->FloorNumber);
			const int32 DungeonId = PlaceADungeonCarrying(Test, Scene, {Row});
			if (!Test.TestTrue(TEXT("set-up: a dungeon carrying Void Parasite was placed"), DungeonId != INDEX_NONE)
				|| !Test.TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
				|| !Test.TestTrue(TEXT("set-up: floor 2 was reached"), Scene.Mode->GoToFloor(2)))
			{
				return false;
			}
			ACataclysmDungeonGameMode& Mode = *Scene.Mode;
			ACataclysmPlayerCharacter& Player = *Scene.Base.Character;
			StandAtTheEntrance(Mode, Player);
			BeatOnce(Mode);
			const ACataclysmGroundZone* Light = Mode.VoidParasiteLightNow();
			if (!Test.TestNotNull(TEXT("set-up: the light is drawn"), Light))
			{
				return false;
			}

			// TWO OF THE FLOOR'S OWN CREATURES OUT OF THE LIGHT KILLED BY THE PLAYER, EACH LEAVING A VOIDLING, AND THE
			// PLAYER WALKED TO EACH, which attaches it.
			TArray<ACataclysmEnemyCharacter*> Victims;
			for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
			{
				if (Victims.Num() < 2 && IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy)
					&& Enemy->PaysForItsDeath() && !Light->Covers(Enemy->GetActorLocation()))
				{
					Victims.Add(Enemy.Get());
				}
			}
			if (!Test.TestEqual(TEXT("set-up: two creatures out of the light"), Victims.Num(), 2))
			{
				return false;
			}
			for (ACataclysmEnemyCharacter* Victim : Victims)
			{
				const FIntPoint Cell = Mode.CurrentFloor->CellOfWorld(Victim->GetActorLocation());
				if (!KillOnTheFloor(Test, Player, Victim, TEXT("a creature out of the light")))
				{
					return false;
				}
				StandOnCell(Mode, Player, Cell);
				BeatOnce(Mode);
			}
			StandAtTheEntrance(Mode, Player);
			BeatOnce(Mode);
			StacksSaved = Mode.VoidParasiteStacksHeld();
			if (!Test.TestEqual(TEXT("set-up: two voidlings attached"), StacksSaved, 2))
			{
				return false;
			}
			Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
			WaitForSlot(Saved.RunSlot());
		}

		FDungeonScene Scene = MakeDungeonScene();
		if (!Test.TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World,
																				 *Scene.Base.Character,
																				 Saved.CharacterId);
		ACataclysmDungeonGameMode& Mode = *Scene.Mode;
		ACataclysmPlayerCharacter& Player = *Scene.Base.Character;
		StandAtTheEntrance(Mode, Player);
		BeatOnce(Mode);
		const int32 Stacks = Mode.VoidParasiteStacksHeld();
		const float Loss = -PlayersDungeonRuleOn(Player, TEXT("attack_damage"));

		Test.AddInfo(FString::Printf(TEXT("LOAD VOID PARASITE: %s (%s); after a beat %d/%d attached, %.2f%% off attack "
										  "damage"),
									 FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, Stacks, StacksSaved,
									 Loss));

		Test.TestEqual(TEXT("the load put the character and its run back"),
					   static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
		Test.TestEqual(TEXT("the voidlings attached come back"), Stacks, 2);
		Test.TestEqual(TEXT("and their loss is on the player once"), Loss,
					   Effects::VoidParasiteLessPercent(2), 0.001f);
		return true;
	}
}

/**
 * Warzone Control Points' hold comes back after a load: a captured point stays held with its shortcut open and its
 * strength on the player once, and the seconds stood at a point not yet held come back. The load brings no allied
 * soldiers of its own; those standing at the save stand again, since 3d-3b. The wave clock is a timer and restarts (D1).
 * Issue #753, Group 3c-2, ruled 2026-10-30. Prints "LOAD WARZONE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadWarzoneTest,
	"Cataclysm.SaveLoad.AWarzoneHoldComesBackAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadWarzoneTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	IConsoleVariable* SeedVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.DungeonSeed"));
	const FString SeedBefore = SeedVariable ? SeedVariable->GetString() : FString();
	const bool bRan = TheWarzoneRoundTrip(*this);
	TestEqual(TEXT("the dungeon's seed is put back as it was"), SeedVariable ? SeedVariable->GetString() : FString(),
			  SeedBefore);
	return bRan;
}

/**
 * The Labrynth's gates come back open and closed as saved, and the swaps after the load are the swaps the same game
 * makes without it, in the same order: the stream they are drawn on is saved (Q2). The seconds to the next swap are a
 * timer and restart (D1), so the swaps are compared in order and not by when they come. Issue #753, Group 3c-2. Prints
 * "LOAD LABRYNTH".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadLabrynthTest,
	"Cataclysm.SaveLoad.TheLabrynthSwapsTheSameAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadLabrynthTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	IConsoleVariable* SeedVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.DungeonSeed"));
	const FString SeedBefore = SeedVariable ? SeedVariable->GetString() : FString();
	const bool bRan = TheLabrynthRoundTrip(*this);
	TestEqual(TEXT("the dungeon's seed is put back as it was"), SeedVariable ? SeedVariable->GetString() : FString(),
			  SeedBefore);
	return bRan;
}

/**
 * The voidlings the Void Parasite attached come back after a load, their loss on the player once. Those standing at the
 * save are put back too, since 3d-3b. Issue #753, Group 3c-2. Prints "LOAD VOID PARASITE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadVoidParasiteTest,
	"Cataclysm.SaveLoad.VoidParasiteStacksComeBackAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadVoidParasiteTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	IConsoleVariable* SeedVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.DungeonSeed"));
	IConsoleVariable* RollVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.VoidParasiteRoll"));
	const FString SeedBefore = SeedVariable ? SeedVariable->GetString() : FString();
	const FString RollBefore = RollVariable ? RollVariable->GetString() : FString();
	const bool bRan = TheVoidParasiteRoundTrip(*this);
	TestEqual(TEXT("the dungeon's seed is put back as it was"), SeedVariable ? SeedVariable->GetString() : FString(),
			  SeedBefore);
	TestEqual(TEXT("and the voidling roll"), RollVariable ? RollVariable->GetString() : FString(), RollBefore);
	return bRan;
}

namespace CataclysmSaveLoadTest
{
	/**
	 * ONE SAVE AND ONE LOAD OF A DUNGEON CARRYING `Rows`, the dungeon's seed pinned for the round trip and put back by its
	 * guard. `Before` is given the entered dungeon on its first floor and leaves it as it is to be saved, answering false
	 * when its set-up failed; the save is then written and the world destroyed. `After` is given the loaded world. Issue
	 * #753, Group 3d-1.
	 */
	template <typename FBefore, typename FAfter>
	bool ARoundTripOfRows(FAutomationTestBase& Test, const TArray<FName>& Rows, FBefore&& Before, FAfter&& After)
	{
		FSaved Saved;
		ON_SCOPE_EXIT { Saved.Forget(); };
		FPinnedDungeonSeed PinnedSeed;
		if (!Test.TestNotNull(TEXT("set-up: the dungeon's seed can be pinned"), PinnedSeed.Variable))
		{
			return false;
		}
		PinnedSeed.Pin(1);
		{
			FDungeonScene Scene = MakeDungeonScene();
			if (!Test.TestTrue(TEXT("set-up: the saving dungeon world was built"), Scene.IsUsable()))
			{
				if (Scene.Base.World)
				{
					Scene.Base.World->DestroyWorld(false);
				}
				return false;
			}
			ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

			Saved.RunId = FGuid::NewGuid();
			Saved.CharacterId = FGuid::NewGuid();
			Scene.Base.Writer->BeginRun(Saved.RunId, Saved.CharacterId, Scene.Mode->DungeonName,
										Scene.Mode->FloorNumber);
			const int32 DungeonId = PlaceADungeonCarrying(Test, Scene, Rows);
			if (!Test.TestTrue(TEXT("set-up: a dungeon carrying the rows was placed"), DungeonId != INDEX_NONE)
				|| !Test.TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
				|| !Before(*Scene.Mode, *Scene.Base.Character, *Scene.Base.World, PinnedSeed))
			{
				return false;
			}
			Scene.Base.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
			WaitForSlot(Saved.RunSlot());
		}

		FDungeonScene Scene = MakeDungeonScene();
		if (!Test.TestTrue(TEXT("set-up: the loading dungeon world was built"), Scene.IsUsable()))
		{
			if (Scene.Base.World)
			{
				Scene.Base.World->DestroyWorld(false);
			}
			return false;
		}
		ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.Base.World,
																				 *Scene.Base.Character,
																				 Saved.CharacterId);
		Test.TestEqual(TEXT("the load put the character and its run back"),
					   static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
		After(*Scene.Mode, *Scene.Base.Character, *Scene.Base.World, Report);
		return true;
	}

	/** The seed put back as it was around a round trip, and the test's answer. */
	template <typename FBody>
	bool WithTheSeedPutBack(FAutomationTestBase& Test, FBody&& Body)
	{
		IConsoleVariable* SeedVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.DungeonSeed"));
		const FString SeedBefore = SeedVariable ? SeedVariable->GetString() : FString();
		const bool bRan = Body();
		Test.TestEqual(TEXT("the dungeon's seed is put back as it was"),
					   SeedVariable ? SeedVariable->GetString() : FString(), SeedBefore);
		return bRan;
	}

	/**
	 * ECHOES OF THE PAST BROUGHT TO A STAGE: two of floor 1's creatures killed by the player, floor 2 reached, and the
	 * floor's beat run from the entrance for `Seconds`. Answers false when the set-up failed.
	 */
	bool EchoesToSeconds(FAutomationTestBase& Test, ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player,
						 float Seconds)
	{
		TArray<ACataclysmEnemyCharacter*> Victims;
		for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
		{
			if (Victims.Num() < 2 && IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy) && Enemy->PaysForItsDeath()
				&& !Enemy->bRaisedByARule && !Enemy->bCannotBeHurt)
			{
				Victims.Add(Enemy.Get());
			}
		}
		bool bAll = Test.TestEqual(TEXT("set-up: two of floor 1's creatures to kill"), Victims.Num(), 2);
		for (ACataclysmEnemyCharacter* Victim : Victims)
		{
			bAll = bAll && KillOnTheFloor(Test, Player, Victim, TEXT("a creature of floor 1"));
		}
		if (!bAll || !Test.TestTrue(TEXT("set-up: floor 2 was reached"), Mode.GoToFloor(2))
			|| !Test.TestEqual(TEXT("set-up: two of the last floor's dead are coming"), Mode.EchoesComingThisFloor(), 2))
		{
			return false;
		}
		StandAtTheEntrance(Mode, Player);
		for (int32 Beat = 0; Beat < BeatsOver(Seconds); ++Beat)
		{
			BeatOnce(Mode);
		}
		return true;
	}

	/** The most echoes standing at once over `Seconds` of the floor's beat, and the stage reached. */
	int32 MostEchoesOver(ACataclysmDungeonGameMode& Mode, float Seconds)
	{
		int32 Most = Mode.EchoesStandingNow().Num();
		for (int32 Beat = 0; Beat < BeatsOver(Seconds); ++Beat)
		{
			BeatOnce(Mode);
			Most = FMath::Max(Most, Mode.EchoesStandingNow().Num());
		}
		return Most;
	}

	/** The living creatures in the world the Plaguebearer rule has marked. */
	int32 MarkedPlaguebearersStanding(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<ACataclysmEnemyCharacter> It(&World); It; ++It)
		{
			Count += IsValid(*It) && It->bIsPlaguebearer && !UCataclysmSkillEffects::IsDead(*It) ? 1 : 0;
		}
		return Count;
	}

	/** The first pinned seed from 1 to 20 whose floor 2 has a Plaguebearer; INDEX_NONE when none does. */
	int32 AFloorWithAPlaguebearer(ACataclysmDungeonGameMode& Mode, FPinnedDungeonSeed& PinnedSeed)
	{
		for (int32 Pinned = 1; Pinned <= 20; ++Pinned)
		{
			PinnedSeed.Pin(Pinned);
			if (Mode.GoToFloor(2) && Mode.PlaguebearerOnTheFloor())
			{
				return Pinned;
			}
		}
		return INDEX_NONE;
	}

	/** A drop at `At`, of a piece of gear, as Famished Beasts' own tests place one. */
	ACataclysmDroppedItem* AGearDropAt(UWorld& World, const FVector& At)
	{
		ACataclysmDroppedItem* Drop = World.SpawnActor<ACataclysmDroppedItem>(At, FRotator::ZeroRotator);
		if (Drop)
		{
			Drop->Item.Base = FName(TEXT("Greataxe"));
		}
		return Drop;
	}

	/** A creature's maximum health. */
	float MaximumHealthOf(const ACataclysmEnemyCharacter& Creature)
	{
		const UAbilitySystemComponent* Abilities = Creature.GetAbilitySystemComponent();
		return Abilities ? Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) : 0.0f;
	}
}

/**
 * Echoes that struck before the save are not raised again by the load, so their strike is not taken twice. Until this
 * the load's floor change set their stage back to 0 and they rose 5 s after the load and struck again. Issue #753, Group
 * 3d-1, ruled 2026-10-30 (Q1). Prints "LOAD ECHOES STRUCK".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadEchoesStruckTest,
	"Cataclysm.SaveLoad.EchoesThatStruckBeforeASaveDoNotStrikeAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadEchoesStruckTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 StageSaved = -1;
		return ARoundTripOfRows(*this, {FName(Effects::EchoesOfThePastKey)},
			[this, &StageSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
								FPinnedDungeonSeed&)
			{
				if (!EchoesToSeconds(*this, Mode, Player, Effects::EchoesAppearAfterSeconds + Effects::EchoesStrikeAfterSeconds))
				{
					return false;
				}
				StageSaved = Mode.EchoesStageNow();
				return TestEqual(TEXT("set-up: the echoes have struck and stand"), StageSaved, 2);
			},
			[this, &StageSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
								const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				const int32 Coming = Mode.EchoesComingThisFloor();
				const int32 Most = MostEchoesOver(Mode, 10.0f);
				const int32 Stage = Mode.EchoesStageNow();
				AddInfo(FString::Printf(TEXT("LOAD ECHOES STRUCK: %s (%s); saved at stage %d; after the load %d of the "
											 "last floor's dead recorded, at most %d echo(es) standing over 10 s, stage %d"),
										FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, StageSaved, Coming, Most,
										Stage));
				TestEqual(TEXT("CONTROL: the last floor's dead are still recorded after the load"), Coming, 2);
				TestEqual(TEXT("no echo is raised again after the load"), Most, 0);
				TestEqual(TEXT("and the echoes stay gone"), Stage, 3);
			});
	});
}

/**
 * Echoes that had appeared and not yet struck at the save rise again after the load and strike: the pending strike is
 * kept, its clock restarted (D1). Issue #753, Group 3d-1, ruled 2026-10-30 (Q1). Prints "LOAD ECHOES PENDING".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadEchoesPendingTest,
	"Cataclysm.SaveLoad.EchoesThatHadNotStruckAtASaveStillStrike",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadEchoesPendingTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 StageSaved = -1;
		return ARoundTripOfRows(*this, {FName(Effects::EchoesOfThePastKey)},
			[this, &StageSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
								FPinnedDungeonSeed&)
			{
				if (!EchoesToSeconds(*this, Mode, Player, Effects::EchoesAppearAfterSeconds))
				{
					return false;
				}
				StageSaved = Mode.EchoesStageNow();
				return TestEqual(TEXT("set-up: the echoes have appeared and not struck"), StageSaved, 1);
			},
			[this, &StageSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
								const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				const int32 Most = MostEchoesOver(Mode, 10.0f);
				const int32 Stage = Mode.EchoesStageNow();
				AddInfo(FString::Printf(TEXT("LOAD ECHOES PENDING: %s (%s); saved at stage %d; after the load at most %d "
											 "echo(es) standing over 10 s, stage %d"),
										FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, StageSaved, Most, Stage));
				TestEqual(TEXT("both echoes rise again after the load"), Most, 2);
				TestEqual(TEXT("and strike and go"), Stage, 3);
			});
	});
}

/**
 * Lightforged Walls' barriers after a load: a barrier opened before the save is open, and one whose section still holds
 * creatures stays closed. Until this every barrier opened on the first beat, the restored creatures having no section.
 * Issue #753, Group 3d-1. Prints "LOAD SECTIONS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadSectionsTest,
	"Cataclysm.SaveLoad.ASectionBarrierStaysClosedAfterALoadWhileItsCreaturesStand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadSectionsTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 Seed = INDEX_NONE;
		int32 StandingSaved = -1;
		return ARoundTripOfRows(*this, {FName(Effects::LightforgedWallsKey)},
			[this, &Seed, &StandingSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
										  FPinnedDungeonSeed& PinnedSeed)
			{
				// TWO BARRIERS OR MORE, SECTIONS 0 AND 1 EACH HOLDING CREATURES, on the first pinned seed that gives them.
				for (int32 Pinned = 1; Pinned <= 20 && Seed == INDEX_NONE; ++Pinned)
				{
					PinnedSeed.Pin(Pinned);
					if (Mode.GoToFloor(2) && Mode.FloorSectionsNow().Boundaries.Num() >= 2 && Mode.StandingInSection(0) > 0
						&& Mode.StandingInSection(1) > 0)
					{
						Seed = Pinned;
					}
				}
				if (!TestTrue(TEXT("set-up: a seed from 1 to 20 gives two barriers and two held sections"),
							  Seed != INDEX_NONE))
				{
					return false;
				}
				// SECTION 0'S CREATURES SLAIN, which opens its barrier on the beat.
				TArray<ACataclysmEnemyCharacter*> InSectionZero;
				for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
				{
					if (IsValid(Enemy) && Enemy->FloorSection == 0 && !UCataclysmSkillEffects::IsDead(Enemy)
						&& !Enemy->bRaisedByARule && !Enemy->bCannotBeHurt)
					{
						InSectionZero.Add(Enemy.Get());
					}
				}
				bool bAll = true;
				for (ACataclysmEnemyCharacter* Victim : InSectionZero)
				{
					bAll = bAll && KillOnTheFloor(*this, Player, Victim, TEXT("a creature of section 0"));
				}
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				StandingSaved = Mode.StandingInSection(1);
				return bAll && TestEqual(TEXT("set-up: section 0 is empty"), Mode.StandingInSection(0), 0)
					&& TestFalse(TEXT("set-up: and its barrier open"), Mode.SectionBarrierIsClosed(0))
					&& TestTrue(TEXT("set-up: barrier 1 is closed"), Mode.SectionBarrierIsClosed(1));
			},
			[this, &Seed, &StandingSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
										  const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				const bool bZeroClosed = Mode.SectionBarrierIsClosed(0);
				const bool bOneClosed = Mode.SectionBarrierIsClosed(1);
				const int32 Standing = Mode.StandingInSection(1);
				AddInfo(FString::Printf(TEXT("LOAD SECTIONS on seed %d: %s (%s); after a beat barrier 0 %s, barrier 1 %s, "
											 "section 1 holds %d/%d"),
										Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
										bZeroClosed ? TEXT("CLOSED") : TEXT("open"), bOneClosed ? TEXT("closed") : TEXT("OPEN"),
										Standing, StandingSaved));
				TestFalse(TEXT("the barrier opened before the save is open"), bZeroClosed);
				TestTrue(TEXT("barrier 1 stays closed"), bOneClosed);
				TestEqual(TEXT("its section still holds its creatures"), Standing, StandingSaved);
			});
	});
}

/**
 * The Plaguebearer after a load: the saved bearer is the rule's bearer again and its stacks come back, every other
 * creature carrying them. Until this its stacks cleared on the first beat, as at its death, and it came back an ordinary
 * hostile creature. Issue #753, Group 3d-1, ruled 2026-10-30 (Q2). Prints "LOAD PLAGUEBEARER".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadPlaguebearerTest,
	"Cataclysm.SaveLoad.ThePlaguebearerAndItsStacksComeBackAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadPlaguebearerTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 Seed = INDEX_NONE;
		int32 StacksSaved = -1;
		return ARoundTripOfRows(*this, {FName(Effects::PlaguebearerKey)},
			[this, &Seed, &StacksSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
										FPinnedDungeonSeed& PinnedSeed)
			{
				Seed = AFloorWithAPlaguebearer(Mode, PinnedSeed);
				if (!TestTrue(TEXT("set-up: a seed from 1 to 20 gives a Plaguebearer"), Seed != INDEX_NONE))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				for (int32 Beat = 0; Beat < BeatsOver(3.0f * Effects::PlaguebearerSecondsBetweenStacks); ++Beat)
				{
					BeatOnce(Mode);
				}
				StacksSaved = Mode.PlaguebearerStacksNow();
				return TestEqual(TEXT("set-up: three stacks"), StacksSaved, 3);
			},
			[this, &Seed, &StacksSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld& World,
										const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				ACataclysmEnemyCharacter* Bearer = Mode.PlaguebearerOnTheFloor();
				const int32 Stacks = Mode.PlaguebearerStacksNow();
				float Carried = 0.0f;
				for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
				{
					if (IsValid(Enemy) && Enemy.Get() != Bearer && !UCataclysmSkillEffects::IsDead(Enemy))
					{
						Carried = Enemy->DamageMultiplierFrom(ACataclysmEnemyCharacter::PlaguebearerDamageSource);
						break;
					}
				}
				const int32 Marked = MarkedPlaguebearersStanding(World);
				AddInfo(FString::Printf(TEXT("LOAD PLAGUEBEARER on seed %d: %s (%s); after a beat the bearer %s, %d marked "
											 "standing, stacks %d/%d, %s, another creature's multiplier %.3f"),
										Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
										Bearer ? TEXT("is back") : TEXT("is NONE"), Marked, Stacks, StacksSaved,
										Mode.PlaguebearerHasFallen() ? TEXT("FALLEN") : TEXT("not fallen"), Carried));
				TestNotNull(TEXT("the saved bearer is the rule's bearer again"), Bearer);
				TestEqual(TEXT("one creature standing marked as the bearer"), Marked, 1);
				TestEqual(TEXT("its stacks come back"), Stacks, StacksSaved);
				TestFalse(TEXT("and it has not fallen"), Mode.PlaguebearerHasFallen());
				TestEqual(TEXT("another creature carries the stacks"), Carried,
						  Effects::PlaguebearerMultiplier(StacksSaved), 0.0001f);
			});
	});
}

/**
 * A Plaguebearer killed before the save stays fallen after the load, and the load's first beat pays or counts nothing of
 * its death again: no drop is added and no creature comes. Issue #753, Group 3d-1, ruled 2026-10-30 (Q2). Prints "LOAD
 * PLAGUEBEARER FALLEN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadPlaguebearerFallenTest,
	"Cataclysm.SaveLoad.APlaguebearerKilledBeforeASaveStaysFallenAndPaysNothingAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadPlaguebearerFallenTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 Seed = INDEX_NONE;
		int32 DropsSaved = -1;
		int32 CreaturesSaved = -1;
		return ARoundTripOfRows(*this, {FName(Effects::PlaguebearerKey)},
			[this, &Seed, &DropsSaved, &CreaturesSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player,
														UWorld& World, FPinnedDungeonSeed& PinnedSeed)
			{
				Seed = AFloorWithAPlaguebearer(Mode, PinnedSeed);
				if (!TestTrue(TEXT("set-up: a seed from 1 to 20 gives a Plaguebearer"), Seed != INDEX_NONE))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				for (int32 Beat = 0; Beat < BeatsOver(3.0f * Effects::PlaguebearerSecondsBetweenStacks); ++Beat)
				{
					BeatOnce(Mode);
				}
				if (!KillOnTheFloor(*this, Player, Mode.PlaguebearerOnTheFloor(), TEXT("the Plaguebearer")))
				{
					return false;
				}
				BeatOnce(Mode);
				DropsSaved = DropsLying(World);
				CreaturesSaved = OrdinaryCreaturesStanding(World);
				return TestTrue(TEXT("set-up: it has fallen"), Mode.PlaguebearerHasFallen())
					&& TestEqual(TEXT("set-up: and its stacks are cleared"), Mode.PlaguebearerStacksNow(), 0);
			},
			[this, &Seed, &DropsSaved, &CreaturesSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player,
														UWorld& World, const FCataclysmLoadReport& Report)
			{
				const bool bFallenAtTheLoad = Mode.PlaguebearerHasFallen();
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				const int32 Drops = DropsLying(World);
				const int32 Creatures = OrdinaryCreaturesStanding(World);
				AddInfo(FString::Printf(TEXT("LOAD PLAGUEBEARER FALLEN on seed %d: %s (%s); fallen at the load %s, after a "
											 "beat %s, stacks %d, %d marked standing; drops %d/%d, creatures %d/%d"),
										Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
										bFallenAtTheLoad ? TEXT("yes") : TEXT("NO"),
										Mode.PlaguebearerHasFallen() ? TEXT("yes") : TEXT("NO"), Mode.PlaguebearerStacksNow(),
										MarkedPlaguebearersStanding(World), DropsSaved, Drops, CreaturesSaved, Creatures));
				TestTrue(TEXT("it is fallen straight after the load, before any beat"), bFallenAtTheLoad);
				TestTrue(TEXT("and after a beat"), Mode.PlaguebearerHasFallen());
				TestEqual(TEXT("its stacks stay cleared"), Mode.PlaguebearerStacksNow(), 0);
				TestEqual(TEXT("no creature stands marked as the bearer"), MarkedPlaguebearersStanding(World), 0);
				TestEqual(TEXT("its death paid no drop again"), Drops, DropsSaved);
				TestEqual(TEXT("and brought no creature"), Creatures, CreaturesSaved);
			});
	});
}

/**
 * Quarantine Breach's released creatures are its released creatures again after a load, so a death spreads a patch.
 * Until this they came back as ordinary creatures and their deaths spread nothing. Issue #753, Group 3d-1. Prints "LOAD
 * QUARANTINE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadQuarantineReleasedTest,
	"Cataclysm.SaveLoad.AReleasedCreatureStillSpreadsTheInfectionAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadQuarantineReleasedTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 Seed = INDEX_NONE;
		int32 ReleasedSaved = -1;
		return ARoundTripOfRows(*this, {FName(Effects::QuarantineBreachKey)},
			[this, &Seed, &ReleasedSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
										  FPinnedDungeonSeed& PinnedSeed)
			{
				for (int32 Pinned = 1; Pinned <= 20 && Seed == INDEX_NONE; ++Pinned)
				{
					PinnedSeed.Pin(Pinned);
					if (Mode.GoToFloor(2) && Mode.QuarantineNow())
					{
						Seed = Pinned;
					}
				}
				if (!TestTrue(TEXT("set-up: a seed from 1 to 20 gives a containment"), Seed != INDEX_NONE)
					|| !KillOnTheFloor(*this, Player, Mode.QuarantineNow(), TEXT("the containment")))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				ReleasedSaved = Mode.QuarantineReleasedStanding().Num();
				return TestTrue(TEXT("set-up: creatures were released"), ReleasedSaved > 0)
					&& TestEqual(TEXT("set-up: and no patch burns"), Mode.QuarantinePatchesNow().Num(), 0);
			},
			[this, &Seed, &ReleasedSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
										  const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				const TArray<ACataclysmEnemyCharacter*> Released = Mode.QuarantineReleasedStanding();
				int32 Patches = -1;
				if (!Released.IsEmpty() && KillOnTheFloor(*this, Player, Released[0], TEXT("a released creature")))
				{
					Patches = Mode.QuarantinePatchesNow().Num();
				}
				AddInfo(FString::Printf(TEXT("LOAD QUARANTINE on seed %d: %s (%s); released standing %d/%d; patches after "
											 "one released death %d"),
										Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, Released.Num(),
										ReleasedSaved, Patches));
				TestEqual(TEXT("the released creatures come back as released"), Released.Num(), ReleasedSaved);
				if (!Released.IsEmpty())
				{
					TestEqual(TEXT("and a released creature's death spreads a patch"), Patches, 1);
				}
			});
	});
}

/**
 * Famished Beasts after a load: an eater keeps its drops eaten and its damage bonus, the floor its count, and a drop eaten
 * after the load replaces the health share already in the saved maximum instead of going on top of it (#2181). Issue #753,
 * Group 3d-1. Prints "LOAD FAMISHED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadFamishedTest,
	"Cataclysm.SaveLoad.AFamishedEaterKeepsItsDropsAndShareAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadFamishedTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		float MaximumSaved = -1.0f;
		return ARoundTripOfRows(*this, {FName(Effects::FamishedBeastsKey)},
			[this, &MaximumSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld& World,
								  FPinnedDungeonSeed&)
			{
				if (!TestTrue(TEXT("set-up: floor 2 was reached"), Mode.GoToFloor(2)))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				// AN EATER WITH NO OTHER CREATURE WITHIN TWICE THE EATING REACH, so the drops under it are its own.
				ACataclysmEnemyCharacter* Eater = nullptr;
				for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
				{
					if (Eater || !IsValid(Enemy) || UCataclysmSkillEffects::IsDead(Enemy) || !Enemy->bSeeksDropsForTheFloorRule)
					{
						continue;
					}
					bool bAlone = true;
					for (TActorIterator<ACataclysmEnemyCharacter> It(&World); It; ++It)
					{
						bAlone = bAlone && (*It == Enemy.Get()
											|| FVector::Dist2D(It->GetActorLocation(), Enemy->GetActorLocation())
												> 2.0f * Effects::FamishedBeastsEatWithinCm);
					}
					Eater = bAlone ? Enemy.Get() : nullptr;
				}
				if (!TestNotNull(TEXT("set-up: a creature that eats, alone"), Eater))
				{
					return false;
				}
				for (int32 Drop = 0; Drop < 2; ++Drop)
				{
					AGearDropAt(World, Eater->GetActorLocation());
					BeatOnce(Mode);
				}
				MaximumSaved = MaximumHealthOf(*Eater);
				return TestEqual(TEXT("set-up: it ate two"), Eater->DropsEaten, 2)
					&& TestEqual(TEXT("set-up: the floor counts two"), Mode.FamishedBeastsDropsEatenNow(), 2);
			},
			[this, &MaximumSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld& World,
								  const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				const int32 CountAfter = Mode.FamishedBeastsDropsEatenNow();
				// THE EATER IS THE CREATURE STANDING WHERE IT STOOD, its record having put it there.
				ACataclysmEnemyCharacter* Eater = nullptr;
				for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
				{
					if (IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy)
						&& FMath::IsNearlyEqual(MaximumHealthOf(*Enemy), MaximumSaved, 0.01f)
						&& (!Eater || Enemy->DropsEaten > Eater->DropsEaten))
					{
						Eater = Enemy.Get();
					}
				}
				if (!TestNotNull(TEXT("set-up: the eater stands after the load"), Eater))
				{
					return;
				}
				const int32 EatenAfterLoad = Eater->DropsEaten;
				const float DamageAfterLoad =
					Eater->DamageMultiplierFrom(ACataclysmEnemyCharacter::FamishedBeastsDamageSource);
				AGearDropAt(World, Eater->GetActorLocation());
				BeatOnce(Mode);
				const float MaximumAfterAnother = MaximumHealthOf(*Eater);
				const float Expected = MaximumSaved / Effects::FamishedBeastsMultiplier(2) * Effects::FamishedBeastsMultiplier(3);
				AddInfo(FString::Printf(TEXT("LOAD FAMISHED: %s (%s); after a beat the eater %d eaten, damage %.3f, the floor "
											 "%d; after another drop %d eaten, damage %.3f, maximum %.2f (saved %.2f, "
											 "expected %.2f)"),
										FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, EatenAfterLoad,
										DamageAfterLoad, CountAfter, Eater->DropsEaten,
										Eater->DamageMultiplierFrom(ACataclysmEnemyCharacter::FamishedBeastsDamageSource),
										MaximumAfterAnother, MaximumSaved, Expected));
				TestEqual(TEXT("it ate two before the save"), EatenAfterLoad, 2);
				TestEqual(TEXT("and keeps its damage bonus"), DamageAfterLoad, Effects::FamishedBeastsMultiplier(2), 0.0001f);
				TestEqual(TEXT("the floor still counts two"), CountAfter, 2);
				TestEqual(TEXT("three after another"), Eater->DropsEaten, 3);
				TestEqual(TEXT("its damage after another"),
						  Eater->DamageMultiplierFrom(ACataclysmEnemyCharacter::FamishedBeastsDamageSource),
						  Effects::FamishedBeastsMultiplier(3), 0.0001f);
				TestEqual(TEXT("its maximum after another replaces the share, not adds to it"), MaximumAfterAnother, Expected,
						  0.5f);
			});
	});
}

namespace CataclysmSaveLoadTest
{
	/** The player alive, which a save needs to write where it stands (a dead player's placement is not gathered). */
	bool ThePlayerIsAlive(FAutomationTestBase& Test, ACataclysmPlayerCharacter& Player)
	{
		return Test.TestFalse(TEXT("set-up: the player is alive at the save"), UCataclysmSkillEffects::IsDead(&Player));
	}

	/** The beat run `Seconds` long. */
	void RunTheBeatFor(ACataclysmDungeonGameMode& Mode, float Seconds)
	{
		for (int32 Beat = 0; Beat < BeatsOver(Seconds); ++Beat)
		{
			BeatOnce(Mode);
		}
	}

	/** The first pinned seed from 1 to 20 whose floor 2 answers `Wanted`; INDEX_NONE when none does. */
	template <typename FWanted>
	int32 AFloorTwoWhere(ACataclysmDungeonGameMode& Mode, FPinnedDungeonSeed& PinnedSeed, FWanted&& Wanted)
	{
		for (int32 Pinned = 1; Pinned <= 20; ++Pinned)
		{
			PinnedSeed.Pin(Pinned);
			if (Mode.GoToFloor(2) && Wanted())
			{
				return Pinned;
			}
		}
		return INDEX_NONE;
	}

	/** The living creatures of a Morale Break group. */
	TArray<ACataclysmEnemyCharacter*> TheGroupOf(const ACataclysmDungeonGameMode& Mode, int32 Group)
	{
		TArray<ACataclysmEnemyCharacter*> Members;
		for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
		{
			if (IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy) && Enemy->PackGroup == Group)
			{
				Members.Add(Enemy.Get());
			}
		}
		return Members;
	}

	/** The floor's living creatures Grim Totems' cleanse has weakened. */
	int32 WeakenedByACleanse(const ACataclysmDungeonGameMode& Mode)
	{
		int32 Count = 0;
		for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
		{
			Count += IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy)
				&& Enemy->DamageMultiplierFrom(ACataclysmEnemyCharacter::GrimTotemsDamageSource) < 1.0f - KINDA_SMALL_NUMBER
				? 1 : 0;
		}
		return Count;
	}
}

/**
 * Morale Break's leaders are their groups' leaders again after a load, and nobody panics until one falls. Until this every
 * leader read dead on the first beat and nobody panicked. Issue #753, Group 3d-2, ruled 2026-10-31. Prints "LOAD MORALE
 * LEADERS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadMoraleLeadersTest,
	"Cataclysm.SaveLoad.MoraleBreaksLeadersStillLeadAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadMoraleLeadersTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 Seed = INDEX_NONE;
		int32 LeadersSaved = -1;
		return ARoundTripOfRows(*this, {FName(Effects::MoraleBreakKey)},
			[this, &Seed, &LeadersSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
										 FPinnedDungeonSeed& PinnedSeed)
			{
				Seed = AFloorTwoWhere(Mode, PinnedSeed, [&Mode]() { return !Mode.MoraleLeadersNow().IsEmpty(); });
				if (!TestTrue(TEXT("set-up: a seed from 1 to 20 gives a leader"), Seed != INDEX_NONE))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				LeadersSaved = Mode.MoraleLeadersNow().Num();
				return ThePlayerIsAlive(*this, Player);
			},
			[this, &Seed, &LeadersSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
										 const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				const TArray<ACataclysmEnemyCharacter*> Leaders = Mode.MoraleLeadersNow();
				const int32 PanickedBefore = Mode.MoraleBreakPanickedNow();
				int32 PanickedAfter = -1;
				if (!Leaders.IsEmpty() && KillOnTheFloor(*this, Player, Leaders[0], TEXT("a leader after the load")))
				{
					BeatOnce(Mode);
					PanickedAfter = Mode.MoraleBreakPanickedNow();
				}
				AddInfo(FString::Printf(TEXT("LOAD MORALE LEADERS on seed %d: %s (%s); after a beat %d/%d leaders, %d "
											 "panicked; a leader killed, %d panicked"),
										Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, Leaders.Num(),
										LeadersSaved, PanickedBefore, PanickedAfter));
				TestEqual(TEXT("the leaders stand as leaders after the load"), Leaders.Num(), LeadersSaved);
				TestEqual(TEXT("and nobody panics"), PanickedBefore, 0);
				if (!Leaders.IsEmpty())
				{
					TestTrue(TEXT("a leader killed after the load panics its group"), PanickedAfter > 0);
				}
			});
	});
}

/**
 * Creatures that escaped Morale Break before a save are still away after the load, the floor not cleared while they are,
 * and they come back with their reinforcements; the time away restarts (Q3, D1). Until this they were forgotten. Issue
 * #753, Group 3d-2. Prints "LOAD MORALE ESCAPED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadMoraleEscapedTest,
	"Cataclysm.SaveLoad.CreaturesThatEscapedMoraleBreakStillComeBackAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadMoraleEscapedTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 Seed = INDEX_NONE;
		int32 EscapedSaved = -1;
		int32 CreaturesSaved = -1;
		return ARoundTripOfRows(*this, {FName(Effects::MoraleBreakKey)},
			[this, &Seed, &EscapedSaved, &CreaturesSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player,
														  UWorld& World, FPinnedDungeonSeed& PinnedSeed)
			{
				// A LEADER WHOSE WHOLE GROUP STANDS FARTHER FROM THE ENTRANCE THAN AN ESCAPE NEEDS, so they escape where
				// they stand, nothing in a test world moving them.
				ACataclysmEnemyCharacter* Leader = nullptr;
				Seed = AFloorTwoWhere(Mode, PinnedSeed, [&Mode, &Leader]()
				{
					const FVector Entrance = Mode.CurrentFloor->EntranceWorld();
					for (ACataclysmEnemyCharacter* Candidate : Mode.MoraleLeadersNow())
					{
						bool bFar = true;
						for (const ACataclysmEnemyCharacter* Member : TheGroupOf(Mode, Candidate->PackGroup))
						{
							bFar = bFar && FVector::Dist2D(Member->GetActorLocation(), Entrance)
								> Effects::MoraleBreakEscapeBeyondCm + 100.0f;
						}
						if (bFar && TheGroupOf(Mode, Candidate->PackGroup).Num() >= 2)
						{
							Leader = Candidate;
							return true;
						}
					}
					return false;
				});
				if (!TestTrue(TEXT("set-up: a seed from 1 to 20 gives a group far from the entrance"), Seed != INDEX_NONE)
					|| !KillOnTheFloor(*this, Player, Leader, TEXT("the group's leader")))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				RunTheBeatFor(Mode, Effects::MoraleBreakFleeSeconds + 1.0f);
				EscapedSaved = Mode.MoraleBreakEscapedNow();
				CreaturesSaved = OrdinaryCreaturesStanding(World);
				return TestTrue(TEXT("set-up: creatures escaped"), EscapedSaved > 0) && ThePlayerIsAlive(*this, Player);
			},
			[this, &Seed, &EscapedSaved, &CreaturesSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player,
														  UWorld& World, const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				const int32 AwayAfterTheLoad = Mode.MoraleBreakEscapedNow();
				RunTheBeatFor(Mode, Effects::MoraleBreakReturnSeconds + 1.0f);
				const int32 AwayAfterTheReturn = Mode.MoraleBreakEscapedNow();
				const int32 Creatures = OrdinaryCreaturesStanding(World);
				const int32 Expected = CreaturesSaved + EscapedSaved * (1 + Effects::MoraleBreakReinforcementsPerEscapee);
				AddInfo(FString::Printf(TEXT("LOAD MORALE ESCAPED on seed %d: %s (%s); away after the load %d/%d, after %.0f s "
											 "%d; creatures standing %d (saved %d, expected %d)"),
										Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, AwayAfterTheLoad,
										EscapedSaved, Effects::MoraleBreakReturnSeconds + 1.0f, AwayAfterTheReturn, Creatures,
										CreaturesSaved, Expected));
				TestEqual(TEXT("the escaped are still away after the load"), AwayAfterTheLoad, EscapedSaved);
				TestEqual(TEXT("and come back"), AwayAfterTheReturn, 0);
				TestEqual(TEXT("with their reinforcements"), Creatures, Expected);
			});
	});
}

/**
 * A Plague Harbinger and its trail after a load: the patches it laid burn again at the burn charged (QA), and its death
 * cleanses them. Until this its trail was gone and its death did nothing. Issue #753, Group 3d-2. Prints "LOAD
 * HARBINGERS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadHarbingersTest,
	"Cataclysm.SaveLoad.APlagueHarbingersTrailComesBackAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadHarbingersTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 Seed = INDEX_NONE;
		int32 AliveSaved = -1;
		int32 PatchesSaved = -1;
		int32 MovedPatchesSaved = -1;
		FVector MovedAt = FVector::ZeroVector;
		return ARoundTripOfRows(*this, {FName(Effects::PlagueHarbingersKey)},
			[this, &Seed, &AliveSaved, &PatchesSaved, &MovedPatchesSaved, &MovedAt](
				ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&, FPinnedDungeonSeed& PinnedSeed)
			{
				// TWO HARBINGERS OR MORE, so a death cleanses one trail and leaves the others' patches standing.
				Seed = AFloorTwoWhere(Mode, PinnedSeed, [&Mode]() { return Mode.PlagueHarbingersAlive().Num() >= 2; });
				if (!TestTrue(TEXT("set-up: a seed from 1 to 20 gives two Harbingers or more"), Seed != INDEX_NONE))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				// ONE HARBINGER MOVED BY HAND, A PATCH'S DISTANCE AND MORE AT A TIME, a beat after each move.
				ACataclysmEnemyCharacter* Moved = Mode.PlagueHarbingersAlive()[0];
				for (int32 Step = 0; Step < 2; ++Step)
				{
					Moved->SetActorLocation(Moved->GetActorLocation()
											+ FVector(Effects::PlagueHarbingersPatchEveryCm + 50.0f, 0.0f, 0.0f));
					BeatOnce(Mode);
				}
				MovedAt = Moved->GetActorLocation();
				AliveSaved = Mode.PlagueHarbingersAlive().Num();
				PatchesSaved = Mode.PlagueHarbingerTrailPatches();
				MovedPatchesSaved = Mode.PlagueHarbingerTrailPatches(Moved);
				return TestTrue(TEXT("set-up: the moved Harbinger laid two patches or more"), MovedPatchesSaved >= 2)
					&& TestTrue(TEXT("set-up: two Harbingers or more stand"), AliveSaved >= 2)
					&& TestTrue(TEXT("set-up: and the others laid patches of their own"), PatchesSaved > MovedPatchesSaved)
					&& ThePlayerIsAlive(*this, Player);
			},
			[this, &Seed, &AliveSaved, &PatchesSaved, &MovedPatchesSaved, &MovedAt](
				ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&, const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				const TArray<ACataclysmEnemyCharacter*> Alive = Mode.PlagueHarbingersAlive();
				const int32 Patches = Mode.PlagueHarbingerTrailPatches();
				// THE MOVED ONE IS THE HARBINGER NEAREST WHERE IT WAS SAVED.
				ACataclysmEnemyCharacter* Moved = nullptr;
				for (ACataclysmEnemyCharacter* One : Alive)
				{
					if (!Moved || FVector::Dist2D(One->GetActorLocation(), MovedAt)
									  < FVector::Dist2D(Moved->GetActorLocation(), MovedAt))
					{
						Moved = One;
					}
				}
				const int32 MovedPatches = Moved ? Mode.PlagueHarbingerTrailPatches(Moved) : -1;
				int32 PatchesAfterItsDeath = -1;
				if (Moved && KillOnTheFloor(*this, Player, Moved, TEXT("the moved Harbinger after the load")))
				{
					PatchesAfterItsDeath = Mode.PlagueHarbingerTrailPatches();
				}
				AddInfo(FString::Printf(TEXT("LOAD HARBINGERS on seed %d: %s (%s); after a beat %d/%d alive, %d/%d patches, "
											 "the moved one's %d/%d; after its death %d"),
										Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, Alive.Num(),
										AliveSaved, Patches, PatchesSaved, MovedPatches, MovedPatchesSaved,
										PatchesAfterItsDeath));
				TestEqual(TEXT("the Harbingers are Harbingers after the load"), Alive.Num(), AliveSaved);
				TestEqual(TEXT("their patches burn again"), Patches, PatchesSaved);
				TestEqual(TEXT("the moved one's trail is its own"), MovedPatches, MovedPatchesSaved);
				TestEqual(TEXT("and its death cleanses it"), PatchesAfterItsDeath, PatchesSaved - MovedPatchesSaved);
			});
	});
}

/**
 * Grim Totems' cleanse after a load: the creatures it weakened are weakened still, for as long as they live. Until this
 * they dealt full damage again while the totem stayed used. Issue #753, Group 3d-2. Prints "LOAD GRIM CLEANSE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadGrimCleanseTest,
	"Cataclysm.SaveLoad.CreaturesAGrimTotemWeakenedStayWeakenedAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadGrimCleanseTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 Seed = INDEX_NONE;
		int32 WeakenedSaved = -1;
		return ARoundTripOfRows(*this, {FName(Effects::GrimTotemsKey)},
			[this, &Seed, &WeakenedSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
										  FPinnedDungeonSeed& PinnedSeed)
			{
				Seed = AFloorTwoWhere(Mode, PinnedSeed, [&Mode]() { return !Mode.GrimTotemsNow().IsEmpty(); });
				if (!TestTrue(TEXT("set-up: a seed from 1 to 20 gives a totem"), Seed != INDEX_NONE)
					|| !TestTrue(TEXT("set-up: the totem was cleansed"),
								 Mode.ChooseAtFloorObject(Mode.GrimTotemsNow()[0], FName(Effects::GrimTotemsCleanse))))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				WeakenedSaved = WeakenedByACleanse(Mode);
				return TestTrue(TEXT("set-up: creatures were weakened"), WeakenedSaved > 0) && ThePlayerIsAlive(*this, Player);
			},
			[this, &Seed, &WeakenedSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
										  const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				const int32 Weakened = WeakenedByACleanse(Mode);
				float Multiplier = 1.0f;
				for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
				{
					if (IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy))
					{
						Multiplier = FMath::Min(Multiplier,
												Enemy->DamageMultiplierFrom(ACataclysmEnemyCharacter::GrimTotemsDamageSource));
					}
				}
				AddInfo(FString::Printf(TEXT("LOAD GRIM CLEANSE on seed %d: %s (%s); after a beat %d/%d weakened, at %.3f"),
										Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, Weakened, WeakenedSaved,
										Multiplier));
				TestEqual(TEXT("the creatures weakened stay weakened"), Weakened, WeakenedSaved);
				TestEqual(TEXT("at the cleanse's multiplier"), Multiplier,
						  1.0f - Effects::GrimTotemsCleanseDamageLessPercent / 100.0f, 0.0001f);
			});
	});
}

/**
 * A player saved standing on the Reality chamber's end, carried there and not yet stepped off, is not carried out on the
 * load's first beat, and the chamber pays nothing again. Until this the rift was armed by every load. Issue #753, Group
 * 3d-2, ruled 2026-10-30 (Q4). Prints "LOAD REALITY CHAMBER".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadRealityChamberTest,
	"Cataclysm.SaveLoad.APlayerSavedOnTheChambersEndIsNotCarriedOutByALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadRealityChamberTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 Seed = INDEX_NONE;
		return ARoundTripOfRows(*this, {FName(Effects::RealityRiftsKey)},
			[this, &Seed](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
						  FPinnedDungeonSeed& PinnedSeed)
			{
				Seed = AFloorTwoWhere(Mode, PinnedSeed, [&Mode]() { return Mode.RealityChamberStands(); });
				if (!TestTrue(TEXT("set-up: a seed from 1 to 20 gives a chamber"), Seed != INDEX_NONE))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				StandOnCell(Mode, Player, Mode.RealityChamberRiftCellNow());
				BeatOnce(Mode);
				return TestTrue(TEXT("set-up: carried into the chamber"), Mode.PlayerIsInTheRealityChamber())
					&& TestEqual(TEXT("set-up: and paid once"), Mode.RealityChamberRewardRollsMade(), 1)
					&& ThePlayerIsAlive(*this, Player);
			},
			[this, &Seed](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter&, UWorld&,
						  const FCataclysmLoadReport& Report)
			{
				const bool bPlaced = Report.Why.Contains(TEXT("placed where it was saved"));
				const bool bInAtTheLoad = Mode.PlayerIsInTheRealityChamber();
				BeatOnce(Mode);
				const bool bInAfterABeat = Mode.PlayerIsInTheRealityChamber();
				AddInfo(FString::Printf(TEXT("LOAD REALITY CHAMBER on seed %d: %s (%s); in the chamber at the load %s, after "
											 "a beat %s; rolls %d"),
										Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
										bInAtTheLoad ? TEXT("yes") : TEXT("NO"), bInAfterABeat ? TEXT("yes") : TEXT("NO"),
										Mode.RealityChamberRewardRollsMade()));
				if (!TestTrue(TEXT("set-up: the load placed the player where it was saved"), bPlaced)
					|| !TestTrue(TEXT("set-up: inside the chamber at the load"), bInAtTheLoad))
				{
					return;
				}
				TestTrue(TEXT("still in the chamber after a beat"), bInAfterABeat);
				TestEqual(TEXT("and the chamber paid nothing again"), Mode.RealityChamberRewardRollsMade(), 1);
			});
	});
}

/**
 * Carrion Feast's count of carcasses eaten on the floor comes back after a load, so the feeders after it are as strong as
 * they would have been. Its carcasses and feeders come back too, since 3d-3b. Issue #753, Group 3d-2, ruled 2026-10-31
 * (#2181). Prints
 * "LOAD CARRION".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadCarrionTest,
	"Cataclysm.SaveLoad.CarrionFeastsCountComesBackAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadCarrionTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 StacksSaved = -1;
		return ARoundTripOfRows(*this, {FName(Effects::CarrionFeastKey)},
			[this, &StacksSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
								 FPinnedDungeonSeed&)
			{
				if (!TestTrue(TEXT("set-up: floor 2 was reached"), Mode.GoToFloor(2)))
				{
					return false;
				}
				ACataclysmEnemyCharacter* Victim = nullptr;
				for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
				{
					if (!Victim && IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy) && !Enemy->bRaisedByARule
						&& !Enemy->bCannotBeHurt)
					{
						Victim = Enemy.Get();
					}
				}
				if (!KillOnTheFloor(*this, Player, Victim, TEXT("a creature to leave a carcass")))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				RunTheBeatFor(Mode, Effects::CarrionFeastEatenAfterSeconds + 1.0f);
				StacksSaved = Mode.CarrionFeastStacksNow();
				return TestEqual(TEXT("set-up: one carcass eaten"), StacksSaved, 1) && ThePlayerIsAlive(*this, Player);
			},
			[this, &StacksSaved](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&,
								 const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				const int32 Stacks = Mode.CarrionFeastStacksNow();
				AddInfo(FString::Printf(TEXT("LOAD CARRION: %s (%s); after a beat %d/%d carcass(es) eaten"),
										FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, Stacks, StacksSaved));
				TestEqual(TEXT("the carcasses eaten come back"), Stacks, StacksSaved);
			});
	});
}

/**
 * A test's kill does nothing to the player who makes it: a creature given Thorns of Glass's retaliation, by the path the
 * modifier takes at spawn, is killed by `KillOnTheFloor` and the player's health is as it was. The guard for the helper's
 * no-retaliation mark: without it the hit is dealt half back, which killed the test player in window w114. Issue #753,
 * Group 3d-2, ruled 2026-10-31. Prints "TEST KILL REFLECTS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadTestKillReflectsNothingTest,
	"Cataclysm.SaveLoad.ATestKillReflectsNothingOntoThePlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadTestKillReflectsNothingTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	FDungeonScene Scene = MakeDungeonScene();
	if (!TestTrue(TEXT("set-up: the dungeon world was built"), Scene.IsUsable()))
	{
		if (Scene.Base.World)
		{
			Scene.Base.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.Base.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 DungeonId = PlaceADungeonCarrying(*this, Scene, {});
	if (!TestTrue(TEXT("set-up: a dungeon was placed"), DungeonId != INDEX_NONE)
		|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
	{
		return false;
	}
	ACataclysmPlayerCharacter& Player = *Scene.Base.Character;

	// ONE OF THE FLOOR'S CREATURES GIVEN THORNS OF GLASS, ITS ATTRIBUTES WRITTEN AGAIN AS AT SPAWN.
	ACataclysmEnemyCharacter* Thorned = nullptr;
	for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Scene.Mode->FloorEnemies)
	{
		if (!Thorned && IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy) && !Enemy->bRaisedByARule
			&& !Enemy->bCannotBeHurt)
		{
			Thorned = Enemy.Get();
		}
	}
	if (!TestNotNull(TEXT("set-up: a creature to give the thorns"), Thorned))
	{
		return false;
	}
	Thorned->ModifierRows.AddUnique(FName(UCataclysmEnemyModifiers::ThornsOfGlassRow));
	Thorned->ApplyModifierAttributes();
	const float WouldReflect = UCataclysmRetaliation::AmountFor(Thorned->GetAbilitySystemComponent(), 100000.0f);
	if (!TestTrue(FString::Printf(TEXT("set-up: it would deal back a share of a hit (%.1f of 100000)"), WouldReflect),
				  WouldReflect > 0.0f))
	{
		return false;
	}

	const UAbilitySystemComponent* Abilities = Player.GetAbilitySystemComponent();
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const float Before = Abilities ? Abilities->GetNumericAttribute(Health) : -1.0f;
	if (!KillOnTheFloor(*this, Player, Thorned, TEXT("the creature with the thorns")))
	{
		return false;
	}
	const float After = Abilities ? Abilities->GetNumericAttribute(Health) : -1.0f;
	AddInfo(FString::Printf(TEXT("TEST KILL REFLECTS: it would deal back %.1f of a 100000 hit; the player's health %.1f "
								 "before the kill, %.1f after, %s"),
							WouldReflect, Before, After,
							UCataclysmSkillEffects::IsDead(&Player) ? TEXT("DEAD") : TEXT("alive")));
	TestEqual(TEXT("the player's health is as it was"), After, Before, 0.01f);
	TestFalse(TEXT("and the player is alive"), UCataclysmSkillEffects::IsDead(&Player));
	return true;
}

namespace CataclysmSaveLoadTest
{
	/** A creature's health as a share of its maximum. */
	float HealthShareOf(const ACataclysmEnemyCharacter& Creature)
	{
		const UAbilitySystemComponent* Abilities = Creature.GetAbilitySystemComponent();
		if (!Abilities)
		{
			return -1.0f;
		}
		const float Maximum = Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		return Maximum > 0.0f ? Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) / Maximum
							  : -1.0f;
	}

	/** Every creature here taken to half its health, as a fight would leave it. */
	void ToHalfHealth(const TArray<ACataclysmEnemyCharacter*>& Creatures)
	{
		for (ACataclysmEnemyCharacter* Creature : Creatures)
		{
			if (UAbilitySystemComponent* Abilities = Creature ? Creature->GetAbilitySystemComponent() : nullptr)
			{
				Abilities->SetNumericAttributeBase(
					UCataclysmVitalAttributeSet::GetHealthAttribute(),
					0.5f * Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()));
			}
		}
	}

	/**
	 * WHAT A RAISED CREATURE'S ROUND TRIP ASSERTS, the same for each kind: as many standing in its rule's list as saved, each
	 * at half its health and carrying the rule's mark, the floor's own standing count as saved, and not one of the floor's
	 * own, which is what the stairs' seal reads. Prints one line, `Label` first. `ExpectedShare` is the share of its health
	 * each was saved at: a half, or all of it for a creature the game holds at its maximum, the Reaper (window w120, 3d-4b).
	 */
	void TheRaisedCameBack(FAutomationTestBase& Test, const TCHAR* Label, int32 Seed, const FCataclysmLoadReport& Report,
						   ACataclysmDungeonGameMode& Mode, const TArray<ACataclysmEnemyCharacter*>& Standing,
						   int32 StandingSaved, int32 LivingSaved, TFunctionRef<bool(const ACataclysmEnemyCharacter&)> Marked,
						   float ExpectedShare = 0.5f)
	{
		const bool bHeldAtFull = FMath::IsNearlyEqual(ExpectedShare, 1.0f);
		int32 AtHalf = 0;
		int32 WithMark = 0;
		int32 FloorsOwn = 0;
		for (ACataclysmEnemyCharacter* One : Standing)
		{
			AtHalf += FMath::IsNearlyEqual(HealthShareOf(*One), ExpectedShare, 0.01f) ? 1 : 0;
			WithMark += Marked(*One) && One->bRaisedByARule ? 1 : 0;
			FloorsOwn += Mode.IsOneOfTheFloorsOwnStanding(One) ? 1 : 0;
		}
		const int32 Living = Mode.LivingFloorEnemies();
		Test.AddInfo(FString::Printf(TEXT("%s on seed %d: %s (%s); after a beat %d/%d standing, %d at %s health, %d marked, %d "
										  "counted as the floor's own; the floor's own standing %d/%d"),
									 Label, Seed, FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why, Standing.Num(),
									 StandingSaved, AtHalf, bHeldAtFull ? TEXT("full") : TEXT("half"), WithMark, FloorsOwn,
									 Living, LivingSaved));
		Test.TestEqual(TEXT("as many stand in the rule's list as were saved"), Standing.Num(), StandingSaved);
		Test.TestEqual(bHeldAtFull ? TEXT("each at its full health, at which it is held")
								   : TEXT("each at the health it was saved at, not full"),
					   AtHalf, Standing.Num());
		Test.TestEqual(TEXT("each carrying the rule's mark"), WithMark, Standing.Num());
		Test.TestEqual(TEXT("none counted as the floor's own, which the stairs' seal reads"), FloorsOwn, 0);
		Test.TestEqual(TEXT("and the floor's own standing as saved, which the clear reads"), Living, LivingSaved);
	}

	/**
	 * ONE ROUND TRIP OF A RAISED KIND: the first pinned seed whose floor 2 lets `Raise` bring some, which is run there; a
	 * beat, which brings any of the rule's creatures owed for the floor; THEN every one standing in the rule's list taken
	 * to half health, and the counts taken, with no beat before the save. Window w116 stopped on the other order: the
	 * beat after the halving brought a tithe's owed angels at full health. Saved, loaded, a beat. `Standing` reads the
	 * rule's list; `AlsoAfter`, when given, asserts more after the load, with the number standing at the save.
	 */
	bool ARaisedRoundTrip(FAutomationTestBase& Test, FName Row, const TCHAR* Label,
						  TFunctionRef<bool(ACataclysmDungeonGameMode&)> Raise,
						  TFunctionRef<TArray<ACataclysmEnemyCharacter*>(const ACataclysmDungeonGameMode&)> Standing,
						  TFunctionRef<bool(const ACataclysmEnemyCharacter&)> Marked,
						  TFunction<void(ACataclysmDungeonGameMode&, int32)> AlsoAfter = nullptr, float ExpectedShare = 0.5f)
	{
		int32 Seed = INDEX_NONE;
		int32 StandingSaved = -1;
		int32 LivingSaved = -1;
		return ARoundTripOfRows(Test, {Row},
			[&](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&, FPinnedDungeonSeed& PinnedSeed)
			{
				Seed = AFloorTwoWhere(Mode, PinnedSeed, [&]() { return Raise(Mode) && !Standing(Mode).IsEmpty(); });
				if (!Test.TestTrue(TEXT("set-up: a seed from 1 to 20 raises some"), Seed != INDEX_NONE))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				ToHalfHealth(Standing(Mode));
				StandingSaved = Standing(Mode).Num();
				LivingSaved = Mode.LivingFloorEnemies();
				return Test.TestTrue(TEXT("set-up: some stand"), StandingSaved > 0) && ThePlayerIsAlive(Test, Player);
			},
			[&](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&, const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				TheRaisedCameBack(Test, Label, Seed, Report, Mode, Standing(Mode), StandingSaved, LivingSaved, Marked,
								  ExpectedShare);
				if (AlsoAfter)
				{
					AlsoAfter(Mode, StandingSaved);
				}
			});
	}

	/** The floor's first object of a rule, chosen. */
	bool ChooseAtTheFirst(ACataclysmDungeonGameMode& Mode, const TArray<ACataclysmFloorObject*>& Objects, const TCHAR* Choice)
	{
		return !Objects.IsEmpty() && Mode.ChooseAtFloorObject(Objects[0], FName(Choice));
	}
}

/**
 * A refused tithe's angels standing at a save stand after the load, at the health they had, the tithe's again, and counted
 * by the stairs and the clear as before. Until this a load removed them. Issue #753, Group 3d-3a, ruled 2026-10-31. Prints
 * "LOAD TITHE ANGELS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadTitheAngelsTest,
	"Cataclysm.SaveLoad.ATithesAngelsStillStandAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadTitheAngelsTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		return ARaisedRoundTrip(*this, FName(Effects::ForcedTithesKey), TEXT("LOAD TITHE ANGELS"),
			[](ACataclysmDungeonGameMode& Mode)
			{
				ACataclysmFloorObject* Altar = Mode.TitheAltarNow();
				return Altar && Mode.ChooseAtFloorObject(Altar, FName(Effects::ForcedTithesRefuse));
			},
			[](const ACataclysmDungeonGameMode& Mode) { return Mode.TitheAngelsStanding(); },
			[](const ACataclysmEnemyCharacter& One) { return One.bIsATitheAngel; },
			// AND THE TITHE OWED IS NOT BROUGHT A SECOND TIME: 3b saved it as owed or not, and this is the first test to
			// read it across a load. Brought again, more would stand than were saved.
			[this](ACataclysmDungeonGameMode& Mode, int32 StandingSaved)
			{
				TestFalse(TEXT("no tithe is owed after the load"), Mode.TitheAngelsAreOwed());
				TestEqual(TEXT("and no angel more stands than at the save"), Mode.TitheAngelsStanding().Num(),
						  StandingSaved);
			});
	});
}

/**
 * An activated relic's spirits standing at a save stand after the load, the relic's again. Issue #753, Group 3d-3a.
 * Prints "LOAD RELIC SPIRITS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadRelicSpiritsTest,
	"Cataclysm.SaveLoad.ARelicsSpiritsStillStandAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadRelicSpiritsTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		return ARaisedRoundTrip(*this, FName(Effects::BattlefieldRelicsKey), TEXT("LOAD RELIC SPIRITS"),
			[](ACataclysmDungeonGameMode& Mode)
			{
				return ChooseAtTheFirst(Mode, Mode.BattlefieldRelicsNow(), Effects::BattlefieldRelicsActivate);
			},
			[](const ACataclysmDungeonGameMode& Mode) { return Mode.RelicSpiritsStanding(); },
			[](const ACataclysmEnemyCharacter& One) { return One.bIsARelicSpirit; });
	});
}

/**
 * An embraced totem's Elites standing at a save stand after the load, the totems' again. Issue #753, Group 3d-3a. Prints
 * "LOAD TOTEM ELITES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadTotemElitesTest,
	"Cataclysm.SaveLoad.AnEmbracedTotemsElitesStillStandAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadTotemElitesTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		return ARaisedRoundTrip(*this, FName(Effects::GrimTotemsKey), TEXT("LOAD TOTEM ELITES"),
			[](ACataclysmDungeonGameMode& Mode)
			{
				return ChooseAtTheFirst(Mode, Mode.GrimTotemsNow(), Effects::GrimTotemsEmbrace);
			},
			[](const ACataclysmDungeonGameMode& Mode) { return Mode.GrimTotemElitesStanding(); },
			// THE TOTEMS' ELITES CARRY NO MARK OF THEIR OWN: the rule's list is the mark, and the raised mark with it.
			[](const ACataclysmEnemyCharacter&) { return true; });
	});
}

/**
 * A planted banner's assailants standing at a save stand after the load, the banner's again. Issue #753, Group 3d-3a.
 * Prints "LOAD BANNER ASSAILANTS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadBannerAssailantsTest,
	"Cataclysm.SaveLoad.ABannersAssailantsStillStandAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadBannerAssailantsTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		return ARaisedRoundTrip(*this, FName(Effects::WarBannerKey), TEXT("LOAD BANNER ASSAILANTS"),
			[](ACataclysmDungeonGameMode& Mode)
			{
				ACataclysmFloorObject* Banner = Mode.WarBannerNow();
				return Banner && Mode.ChooseAtFloorObject(Banner, FName(Effects::WarBannerPlant));
			},
			[](const ACataclysmDungeonGameMode& Mode) { return Mode.BannerAssailantsStanding(); },
			[](const ACataclysmEnemyCharacter& One) { return One.bIsABannerAssailant; });
	});
}

/**
 * A Pandora's Box's standing wave at a save stands after the load, the box's again, and when it is killed the next wave
 * comes, the count of waves come kept (Q5 of 3d). Issue #753, Group 3d-3a. Prints "LOAD CHAOS SPAWN" and "LOAD PANDORA
 * WAVES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadChaosSpawnTest,
	"Cataclysm.SaveLoad.APandorasBoxsWavesGoOnAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadChaosSpawnTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 Seed = INDEX_NONE;
		int32 StandingSaved = -1;
		int32 LivingSaved = -1;
		int32 CameSaved = -1;
		return ARoundTripOfRows(*this, {FName(Effects::PandorasBoxKey)},
			[&](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&, FPinnedDungeonSeed& PinnedSeed)
			{
				// A BOX THAT LETS OUT ITS WAVES, its outcome being seeded: the first pinned seed whose floor 2's first box does.
				Seed = AFloorTwoWhere(Mode, PinnedSeed, [&Mode]()
				{
					return ChooseAtTheFirst(Mode, Mode.PandorasBoxesNow(), Effects::PandorasBoxOpen)
						&& !Mode.ChaosSpawnStanding().IsEmpty();
				});
				if (!TestTrue(TEXT("set-up: a seed from 1 to 20 gives a box that lets out its waves"), Seed != INDEX_NONE))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				ToHalfHealth(Mode.ChaosSpawnStanding());
				StandingSaved = Mode.ChaosSpawnStanding().Num();
				LivingSaved = Mode.LivingFloorEnemies();
				CameSaved = Mode.PandorasBoxWavesCameNow();
				return TestEqual(TEXT("set-up: the first wave has come"), CameSaved, 1)
					&& TestEqual(TEXT("set-up: and stands whole"), StandingSaved, Effects::PandorasBoxWaveSize)
					&& ThePlayerIsAlive(*this, Player);
			},
			[&](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&, const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				const TArray<ACataclysmEnemyCharacter*> Standing = Mode.ChaosSpawnStanding();
				TheRaisedCameBack(*this, TEXT("LOAD CHAOS SPAWN"), Seed, Report, Mode, Standing, StandingSaved, LivingSaved,
								  [](const ACataclysmEnemyCharacter& One) { return One.bIsAChaosSpawn; });
				const int32 CameAfterTheLoad = Mode.PandorasBoxWavesCameNow();
				bool bAll = true;
				for (ACataclysmEnemyCharacter* One : Standing)
				{
					bAll = bAll && KillOnTheFloor(*this, Player, One, TEXT("a chaos spawn after the load"));
				}
				BeatOnce(Mode);
				const int32 CameAfterTheKills = Mode.PandorasBoxWavesCameNow();
				const int32 Next = Mode.ChaosSpawnStanding().Num();
				AddInfo(FString::Printf(TEXT("LOAD PANDORA WAVES on seed %d: waves come %d at the save, %d after the load, %d "
											 "once the standing wave was killed, %d of the next standing"),
										Seed, CameSaved, CameAfterTheLoad, CameAfterTheKills, Next));
				TestEqual(TEXT("the waves come are as saved"), CameAfterTheLoad, CameSaved);
				if (bAll)
				{
					TestEqual(TEXT("and the next wave comes when the standing one is killed"), CameAfterTheKills, CameSaved + 1);
					TestTrue(TEXT("and stands"), Next > 0);
				}
			});
	});
}

namespace CataclysmSaveLoadTest
{
	/** The player of the game mode's world. */
	ACataclysmPlayerCharacter* ThePlayerOf(const ACataclysmDungeonGameMode& Mode)
	{
		const UWorld* World = Mode.GetWorld();
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		return Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	}

	/** One of the floor's own creatures that pays for its death, at least `AwayCm` from the entrance, or null. */
	ACataclysmEnemyCharacter* AFloorCreatureAwayFromTheEntrance(const ACataclysmDungeonGameMode& Mode, float AwayCm)
	{
		const FVector Entrance = Mode.CurrentFloor->EntranceWorld();
		for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
		{
			if (IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy) && Enemy->PaysForItsDeath()
				&& !Enemy->bRaisedByARule && !Enemy->bCannotBeHurt
				&& FVector::Dist2D(Enemy->GetActorLocation(), Entrance) >= AwayCm)
			{
				return Enemy.Get();
			}
		}
		return nullptr;
	}
}

/**
 * A woken statue's warden standing at a save stands after the load, its statue's again. Until this a load removed it.
 * Issue #753, Group 3d-3b, ruled 2026-10-31. Prints "LOAD WARDENS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadWardensTest,
	"Cataclysm.SaveLoad.AWokenStatuesWardenStillStandsAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadWardensTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		return ARaisedRoundTrip(*this, FName(Effects::AngelicWardensKey), TEXT("LOAD WARDENS"),
			[](ACataclysmDungeonGameMode& Mode)
			{
				const TArray<FIntPoint> Statues = Mode.AngelicStatueCellsStanding();
				return !Statues.IsEmpty() && Mode.NoteDivineLawViolated(Mode.CurrentFloor->WorldOfCell(Statues[0])) == 1;
			},
			[](const ACataclysmDungeonGameMode& Mode) { return Mode.AngelicWardensNow(); },
			// A WARDEN CARRIES NO MARK OF ITS OWN: its statue's link is its mark, and the raised mark with it.
			[](const ACataclysmEnemyCharacter&) { return true; });
	});
}

/**
 * Warzone's attackers standing at a save stand after the load, the point's again, paying nothing as they did. Issue #753,
 * Group 3d-3b. Prints "LOAD WARZONE ATTACKERS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadWarzoneAttackersTest,
	"Cataclysm.SaveLoad.WarzonesAttackersStillStandAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadWarzoneAttackersTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		return ARaisedRoundTrip(*this, FName(Effects::WarzoneControlPointsKey), TEXT("LOAD WARZONE ATTACKERS"),
			[](ACataclysmDungeonGameMode& Mode)
			{
				// A WAVE COMES ON THE FIRST BEAT IN A POINT NOT HELD, the wave clock starting full.
				ACataclysmPlayerCharacter* Player = ThePlayerOf(Mode);
				if (!Player || Mode.WarzonePointCellsNow().IsEmpty())
				{
					return false;
				}
				StandOnCell(Mode, *Player, Mode.WarzonePointCellsNow()[0]);
				for (int32 Beat = 0; Beat < 4 && Mode.WarzoneAttackersStanding().IsEmpty(); ++Beat)
				{
					BeatOnce(Mode);
				}
				return true;
			},
			[](const ACataclysmDungeonGameMode& Mode) { return Mode.WarzoneAttackersStanding(); },
			[](const ACataclysmEnemyCharacter& One) { return One.bDiesUnpaid; });
	});
}

/**
 * Warzone's allied soldiers standing at a save stand after the load, the player's and the point's again, and not on the
 * floor's list. Until this a load removed them: a reward lost. Issue #753, Group 3d-3b. Prints "LOAD WARZONE ALLIES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadWarzoneAlliesTest,
	"Cataclysm.SaveLoad.WarzonesAlliesStillStandAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadWarzoneAlliesTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		return ARaisedRoundTrip(*this, FName(Effects::WarzoneControlPointsKey), TEXT("LOAD WARZONE ALLIES"),
			[](ACataclysmDungeonGameMode& Mode)
			{
				// A POINT CAPTURED BY STANDING IN IT, which brings its allies.
				ACataclysmPlayerCharacter* Player = ThePlayerOf(Mode);
				if (!Player || Mode.WarzonePointCellsNow().IsEmpty())
				{
					return false;
				}
				StandOnCell(Mode, *Player, Mode.WarzonePointCellsNow()[0]);
				for (int32 Beat = 0; Beat < BeatsOver(Effects::WarzoneCaptureSeconds) + 1 && Mode.WarzonePointsHeld() == 0;
					 ++Beat)
				{
					BeatOnce(Mode);
				}
				return Mode.WarzonePointsHeld() == 1;
			},
			[](const ACataclysmDungeonGameMode& Mode) { return Mode.WarzoneAlliesStanding(); },
			[](const ACataclysmEnemyCharacter& One)
			{
				return One.bDiesUnpaid
					&& Cast<ACataclysmPlayerCharacter>(UCataclysmCommand::CommanderOf(&One)) != nullptr;
			},
			[this](ACataclysmDungeonGameMode& Mode, int32)
			{
				int32 OnTheList = 0;
				for (ACataclysmEnemyCharacter* Ally : Mode.WarzoneAlliesStanding())
				{
					OnTheList += Mode.FloorEnemies.Contains(Ally) ? 1 : 0;
				}
				TestEqual(TEXT("no ally is on the floor's list"), OnTheList, 0);
			});
	});
}

/**
 * A voidling standing at a save, not yet attached, stands after the load where it stood, the rule's again, paying nothing
 * and not on the floor's list. Until this a load removed it: a stack the player escaped. Issue #753, Group 3d-3b. Prints
 * "LOAD VOIDLINGS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadVoidlingsTest,
	"Cataclysm.SaveLoad.AVoidlingStillStandsAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadVoidlingsTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	IConsoleVariable* RollVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.VoidParasiteRoll"));
	const FString RollBefore = RollVariable ? RollVariable->GetString() : FString();
	const bool bRan = WithTheSeedPutBack(*this, [this]()
	{
		FPinnedConsoleValue Rises(TEXT("Cataclysm.VoidParasiteRoll"), TEXT("0"));
		return ARaisedRoundTrip(*this, FName(Effects::VoidParasiteKey), TEXT("LOAD VOIDLINGS"),
			[this](ACataclysmDungeonGameMode& Mode)
			{
				// A CREATURE FAR FROM THE ENTRANCE KILLED BY THE PLAYER, its voidling too far to attach once the player
				// stands there.
				ACataclysmPlayerCharacter* Player = ThePlayerOf(Mode);
				ACataclysmEnemyCharacter* Victim = AFloorCreatureAwayFromTheEntrance(Mode, 1000.0f);
				return Player && Victim && KillOnTheFloor(*this, *Player, Victim, TEXT("a creature to leave a voidling"));
			},
			[](const ACataclysmDungeonGameMode& Mode) { return Mode.VoidlingsNow(); },
			[](const ACataclysmEnemyCharacter& One) { return One.bIsAVoidling && One.bDiesUnpaid; },
			[this](ACataclysmDungeonGameMode& Mode, int32)
			{
				int32 OnTheList = 0;
				for (ACataclysmEnemyCharacter* Voidling : Mode.VoidlingsNow())
				{
					OnTheList += Mode.FloorEnemies.Contains(Voidling) ? 1 : 0;
				}
				TestEqual(TEXT("no voidling is on the floor's list"), OnTheList, 0);
				TestEqual(TEXT("and none attached"), Mode.VoidParasiteStacksHeld(), 0);
			});
	});
	TestEqual(TEXT("and the voidling roll is put back"), RollVariable ? RollVariable->GetString() : FString(), RollBefore);
	return bRan;
}

/**
 * Carrion Feast after a load: a feeder standing at the save stands again, its own maximum kept, strengthened by the
 * carcasses eaten; a carcass lying at the save lies again and, eaten, counts and brings a feeder. Until this both were
 * gone. Issue #753, Group 3d-3b, ruled 2026-10-31 (QA). Prints "LOAD FEEDERS" and "LOAD CARCASSES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadFeedersTest,
	"Cataclysm.SaveLoad.CarrionFeastsFeedersAndCarcassesComeBackAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadFeedersTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 CarcassesSaved = -1;
		int32 StacksSaved = -1;
		return ARaisedRoundTrip(*this, FName(Effects::CarrionFeastKey), TEXT("LOAD FEEDERS"),
			[this, &CarcassesSaved, &StacksSaved](ACataclysmDungeonGameMode& Mode)
			{
				// ONE CARCASS EATEN, WHICH BRINGS A FEEDER, AND A SECOND LYING.
				ACataclysmPlayerCharacter* Player = ThePlayerOf(Mode);
				ACataclysmEnemyCharacter* First = AFloorCreatureAwayFromTheEntrance(Mode, 0.0f);
				if (!Player || !First || !KillOnTheFloor(*this, *Player, First, TEXT("a creature to leave a carcass")))
				{
					return false;
				}
				for (int32 Beat = 0; Beat < BeatsOver(Effects::CarrionFeastEatenAfterSeconds + 1.0f); ++Beat)
				{
					BeatOnce(Mode);
				}
				ACataclysmEnemyCharacter* Second = AFloorCreatureAwayFromTheEntrance(Mode, 0.0f);
				if (!Second || !KillOnTheFloor(*this, *Player, Second, TEXT("a creature to leave a second carcass")))
				{
					return false;
				}
				CarcassesSaved = Mode.CarrionCarcassesNow().Num();
				StacksSaved = Mode.CarrionFeastStacksNow();
				return CarcassesSaved == 1 && StacksSaved == 1;
			},
			[](const ACataclysmDungeonGameMode& Mode) { return Mode.CarrionFeedersNow(); },
			[](const ACataclysmEnemyCharacter& One) { return One.bIsACarrionFeeder; },
			[this, &CarcassesSaved, &StacksSaved](ACataclysmDungeonGameMode& Mode, int32 FeedersSaved)
			{
				const int32 CarcassesAfterTheLoad = Mode.CarrionCarcassesNow().Num();
				float FeederDamage = 0.0f;
				if (!Mode.CarrionFeedersNow().IsEmpty())
				{
					FeederDamage = Mode.CarrionFeedersNow()[0]->DamageMultiplierFrom(ACataclysmEnemyCharacter::CarrionFeastDamageSource);
				}
				for (int32 Beat = 0; Beat < BeatsOver(Effects::CarrionFeastEatenAfterSeconds + 1.0f); ++Beat)
				{
					BeatOnce(Mode);
				}
				AddInfo(FString::Printf(TEXT("LOAD CARCASSES: %d/%d lying after the load, the feeders' damage %.3f; once it was "
											 "eaten, %d eaten (saved %d), %d feeder(s) (saved %d)"),
										CarcassesAfterTheLoad, CarcassesSaved, FeederDamage, Mode.CarrionFeastStacksNow(),
										StacksSaved, Mode.CarrionFeedersNow().Num(), FeedersSaved));
				TestEqual(TEXT("the carcass lying at the save lies again"), CarcassesAfterTheLoad, CarcassesSaved);
				TestEqual(TEXT("the feeders are strengthened by the carcasses eaten"), FeederDamage,
						  Effects::CarrionFeastMultiplier(StacksSaved), 0.0001f);
				TestEqual(TEXT("and, eaten, it counts"), Mode.CarrionFeastStacksNow(), StacksSaved + 1);
				TestEqual(TEXT("and brings a feeder"), Mode.CarrionFeedersNow().Num(), FeedersSaved + 1);
			});
	});
}

namespace CataclysmSaveLoadTest
{
	/** Plague Convergence's creatures standing, read through the rule's own question. */
	TArray<ACataclysmEnemyCharacter*> ConvergenceStanding(const ACataclysmDungeonGameMode& Mode)
	{
		TArray<ACataclysmEnemyCharacter*> Standing;
		for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
		{
			if (IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy) && Mode.IsAPlagueConvergenceCreature(Enemy.Get()))
			{
				Standing.Add(Enemy.Get());
			}
		}
		return Standing;
	}
}

/**
 * Plague Convergence after a load: its creatures standing stand again, the disease stacks the player carried are carried
 * still (a cost built up), and, begun, its waves come on their cadence and not after the floor's wait again (Q1). Until
 * this the creatures went, the stacks went to nought, and nothing came for 120 s. Issue #753, Group 3d-4a, ruled
 * 2026-10-31. Prints "LOAD CONVERGENCE" and "LOAD CONVERGENCE STACKS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadConvergenceTest,
	"Cataclysm.SaveLoad.APlagueConvergenceGoesOnAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadConvergenceTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 StacksSaved = -1;
		return ARaisedRoundTrip(*this, FName(Effects::PlagueConvergenceKey), TEXT("LOAD CONVERGENCE"),
			[&StacksSaved](ACataclysmDungeonGameMode& Mode)
			{
				// THE CONVERGENCE BEGUN AND ITS FIRST WAVE COME, then two of its blows landed on the player, which stack.
				ACataclysmPlayerCharacter* Player = ThePlayerOf(Mode);
				if (!Player)
				{
					return false;
				}
				StandAtTheEntrance(Mode, *Player);
				for (int32 Beat = 0; Beat < BeatsOver(Effects::PlagueConvergenceBeginsAfterSeconds + 1.0f); ++Beat)
				{
					BeatOnce(Mode);
				}
				const TArray<ACataclysmEnemyCharacter*> Come = ConvergenceStanding(Mode);
				if (Come.IsEmpty())
				{
					return false;
				}
				if (UAbilitySystemComponent* Abilities = Player->GetAbilitySystemComponent())
				{
					Abilities->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
				}
				UCataclysmSkillEffects::ApplyHit(Come[0], Player, 1.0f);
				UCataclysmSkillEffects::ApplyHit(Come[0], Player, 1.0f);
				StacksSaved = Mode.PlagueConvergenceDiseaseStacks();
				// AND THE FIRST WAVE CAME WHOLE, so a fresh wave after the load is the same size.
				return StacksSaved > 0 && Come.Num() == Effects::PlagueConvergenceCreaturesPerWave;
			},
			[](const ACataclysmDungeonGameMode& Mode) { return ConvergenceStanding(Mode); },
			[](const ACataclysmEnemyCharacter& One) { return One.bDiesUnpaid; },
			[this, &StacksSaved](ACataclysmDungeonGameMode& Mode, int32 StandingSaved)
			{
				const int32 Stacks = Mode.PlagueConvergenceDiseaseStacks();
				for (int32 Beat = 0; Beat < BeatsOver(Effects::PlagueConvergenceSecondsBetweenWaves + 1.0f); ++Beat)
				{
					BeatOnce(Mode);
				}
				const int32 StandingAfterAWave = ConvergenceStanding(Mode).Num();
				AddInfo(FString::Printf(TEXT("LOAD CONVERGENCE STACKS: %d/%d stacks after the load; %d standing at the save, "
											 "%d after %.0f s"),
										Stacks, StacksSaved, StandingSaved, StandingAfterAWave,
										Effects::PlagueConvergenceSecondsBetweenWaves + 1.0f));
				TestEqual(TEXT("the disease stacks are carried still"), Stacks, StacksSaved);
				TestTrue(TEXT("and, begun, a wave comes on its cadence"), StandingAfterAWave > StandingSaved);
			});
	});
}

/**
 * A standing Infection Bloom after a load: its waves standing stand again, not on the floor's list, and the bloom keeps its
 * spread and the health it had left (Q3 a). Until this the waves went, and the bloom stood at full health with one patch.
 * Issue #753, Group 3d-4a. Prints "LOAD BLOOM WAVES" and "LOAD BLOOM".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadBloomWavesTest,
	"Cataclysm.SaveLoad.AnInfectionBloomsWavesAndSpreadComeBackAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadBloomWavesTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		int32 PatchesSaved = -1;
		return ARaisedRoundTrip(*this, FName(Effects::InfectionBloomKey), TEXT("LOAD BLOOM WAVES"),
			[&PatchesSaved](ACataclysmDungeonGameMode& Mode)
			{
				// A WAVE SENT AND THE SPREAD GROWN, then the bloom taken to half its health.
				ACataclysmPlayerCharacter* Player = ThePlayerOf(Mode);
				ACataclysmEnemyCharacter* Bloom = Mode.InfectionBloomNow();
				if (!Player || !Bloom)
				{
					return false;
				}
				StandAtTheEntrance(Mode, *Player);
				for (int32 Beat = 0; Beat < BeatsOver(Effects::InfectionBloomSecondsBetweenWaves + 1.0f); ++Beat)
				{
					BeatOnce(Mode);
				}
				ToHalfHealth({Bloom});
				PatchesSaved = Mode.InfectionBloomPatchesNow().Num();
				return PatchesSaved > 1;
			},
			[](const ACataclysmDungeonGameMode& Mode) { return Mode.InfectionBloomWaveCreaturesStanding(); },
			[](const ACataclysmEnemyCharacter& One) { return One.bDiesUnpaid; },
			[this, &PatchesSaved](ACataclysmDungeonGameMode& Mode, int32)
			{
				int32 OnTheList = 0;
				for (ACataclysmEnemyCharacter* Wave : Mode.InfectionBloomWaveCreaturesStanding())
				{
					OnTheList += Mode.FloorEnemies.Contains(Wave) ? 1 : 0;
				}
				const ACataclysmEnemyCharacter* Bloom = Mode.InfectionBloomNow();
				const float Share = Bloom ? HealthShareOf(*Bloom) : -1.0f;
				const int32 Patches = Mode.InfectionBloomPatchesNow().Num();
				AddInfo(FString::Printf(TEXT("LOAD BLOOM: the bloom at %.2f of its health, %d/%d patches, %d wave creature(s) "
											 "on the floor's list"),
										Share, Patches, PatchesSaved, OnTheList));
				TestEqual(TEXT("no wave is on the floor's list"), OnTheList, 0);
				TestEqual(TEXT("the bloom keeps the health it had left"), Share, 0.5f, 0.01f);
				TestEqual(TEXT("and its spread"), Patches, PatchesSaved);
			});
	});
}

/**
 * A trick's pair standing at a save stands after the load, paying as it did. Until this a pickup, a quit and a reload kept
 * the treat and escaped the trick. Issue #753, Group 3d-4a. Prints "LOAD TRICK OR TREAT".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadTrickOrTreatTest,
	"Cataclysm.SaveLoad.ATricksPairStillStandsAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadTrickOrTreatTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	IConsoleVariable* RollVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.TrickOrTreatRoll"));
	const FString RollBefore = RollVariable ? RollVariable->GetString() : FString();
	const bool bRan = WithTheSeedPutBack(*this, [this]()
	{
		FPinnedConsoleValue ATrick(TEXT("Cataclysm.TrickOrTreatRoll"), TEXT("10"));
		return ARaisedRoundTrip(*this, FName(Effects::TrickOrTreatKey), TEXT("LOAD TRICK OR TREAT"),
			[](ACataclysmDungeonGameMode& Mode)
			{
				// A DROP TAKEN BY HAND ON A TRICK, which raises the pair.
				ACataclysmPlayerCharacter* Player = ThePlayerOf(Mode);
				ACataclysmDroppedItem* Drop = Player ? AGearDropAt(*Mode.GetWorld(), Player->GetActorLocation()) : nullptr;
				return Drop && UCataclysmDropPickup::TakeInto(Player->GetInventory(), Drop, /*bByHand=*/true);
			},
			[](const ACataclysmDungeonGameMode& Mode) { return Mode.TrickOrTreatPairStanding(); },
			[](const ACataclysmEnemyCharacter& One) { return One.PaysForItsDeath(); });
	});
	TestEqual(TEXT("and the trick's roll is put back"), RollVariable ? RollVariable->GetString() : FString(), RollBefore);
	return bRan;
}

/**
 * An Unstable Portal's Warden standing at a save stands after the load, at its mini-boss rung, paying as it did. Until this
 * a load removed it, its fight escaped and its reward lost. Issue #753, Group 3d-4a. Prints "LOAD PORTAL WARDEN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadPortalWardenTest,
	"Cataclysm.SaveLoad.AnUnstablePortalsWardenStillStandsAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadPortalWardenTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	IConsoleVariable* RollVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.UnstablePortalRoll"));
	const FString RollBefore = RollVariable ? RollVariable->GetString() : FString();
	const bool bRan = WithTheSeedPutBack(*this, [this]()
	{
		FPinnedConsoleValue AWarden(TEXT("Cataclysm.UnstablePortalRoll"), TEXT("90"));
		return ARaisedRoundTrip(*this, FName(Effects::UnstablePortalKey), TEXT("LOAD PORTAL WARDEN"),
			[](ACataclysmDungeonGameMode& Mode)
			{
				// THE STAIRS REACHED ON A ROLL THAT RAISES THE WARDEN, as the rule's own test reaches them.
				ACataclysmDungeonStairs* Stairs = Mode.Stairs.Get();
				return Stairs && Stairs->ArriveAt(Stairs->GetActorLocation());
			},
			[](const ACataclysmDungeonGameMode& Mode) { return Mode.UnstablePortalWardensStanding(); },
			[](const ACataclysmEnemyCharacter& One)
			{
				return One.PaysForItsDeath() && One.RarityStep == UCataclysmDungeonModifierEffects::UnstablePortalMiniBossRung;
			});
	});
	TestEqual(TEXT("and the portal's roll is put back"), RollVariable ? RollVariable->GetString() : FString(), RollBefore);
	return bRan;
}

/**
 * A drop a raised creature left keeps that mark across a load, so Trick or Treat rolls nothing for it after the load
 * either, and an ordinary drop stays unmarked (Q3 b). Until this a load cleared the mark. Issue #753, Group 3d-4a. Prints
 * "LOAD RAISED DROP".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadRaisedDropTest,
	"Cataclysm.SaveLoad.ARaisedCreaturesDropKeepsItsMarkAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadRaisedDropTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;

	return WithTheSeedPutBack(*this, [this]()
	{
		FVector MarkedAt = FVector::ZeroVector;
		FVector PlainAt = FVector::ZeroVector;
		return ARoundTripOfRows(*this, {},
			[this, &MarkedAt, &PlainAt](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld& World,
										FPinnedDungeonSeed&)
			{
				StandAtTheEntrance(Mode, Player);
				BeatOnce(Mode);
				MarkedAt = Player.GetActorLocation() + FVector(200.0f, 0.0f, 0.0f);
				PlainAt = Player.GetActorLocation() + FVector(-200.0f, 0.0f, 0.0f);
				ACataclysmDroppedItem* Marked = AGearDropAt(World, MarkedAt);
				ACataclysmDroppedItem* Plain = AGearDropAt(World, PlainAt);
				if (!TestNotNull(TEXT("set-up: a marked drop"), Marked) || !TestNotNull(TEXT("set-up: a plain drop"), Plain))
				{
					return false;
				}
				Marked->bDroppedByARaisedCreature = true;
				MarkedAt = Marked->GetActorLocation();
				PlainAt = Plain->GetActorLocation();
				return ThePlayerIsAlive(*this, Player);
			},
			[this, &MarkedAt, &PlainAt](ACataclysmDungeonGameMode&, ACataclysmPlayerCharacter&, UWorld& World,
										const FCataclysmLoadReport& Report)
			{
				const ACataclysmDroppedItem* Marked = nullptr;
				const ACataclysmDroppedItem* Plain = nullptr;
				for (TActorIterator<ACataclysmDroppedItem> It(&World); It; ++It)
				{
					if (FVector::Dist2D(It->GetActorLocation(), MarkedAt) < 1.0f)
					{
						Marked = *It;
					}
					if (FVector::Dist2D(It->GetActorLocation(), PlainAt) < 1.0f)
					{
						Plain = *It;
					}
				}
				AddInfo(FString::Printf(TEXT("LOAD RAISED DROP: %s (%s); the marked drop %s, the plain drop %s"),
										FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
										!Marked ? TEXT("MISSING") : Marked->bDroppedByARaisedCreature ? TEXT("marked")
																										: TEXT("UNMARKED"),
										!Plain ? TEXT("MISSING") : Plain->bDroppedByARaisedCreature ? TEXT("MARKED")
																									 : TEXT("unmarked")));
				if (!TestNotNull(TEXT("set-up: the marked drop came back"), Marked)
					|| !TestNotNull(TEXT("set-up: the plain drop came back"), Plain))
				{
					return;
				}
				TestTrue(TEXT("the raised creature's drop keeps its mark"), Marked->bDroppedByARaisedCreature);
				TestFalse(TEXT("and the plain drop is not marked"), Plain->bDroppedByARaisedCreature);
			});
	});
}

namespace CataclysmSaveLoadTest
{
	/** The Corrupted Stalker's build on one line: its entry, and each worn piece's base and rolled affixes. */
	FString StalkerBuildLine(const ACataclysmCorruptedStalkerCharacter& Stalker)
	{
		FString Out = Stalker.EntryName();
		for (const FCataclysmItem& Piece : Stalker.WornPieces())
		{
			Out += FString::Printf(TEXT(" | %s"), *Piece.Base.ToString());
			for (const FCataclysmRolledAffix& Affix : Piece.Affixes)
			{
				Out += FString::Printf(TEXT(" %s/%d/%.4f"), *Affix.Affix.ToString(), Affix.Tier, Affix.Roll);
			}
		}
		return Out;
	}

	/** Portal Unleashing's creatures standing, portal by portal, in the portals' order. */
	TArray<int32> PortalCreaturesByPortal(const ACataclysmDungeonGameMode& Mode)
	{
		TArray<int32> Counts;
		for (const ACataclysmEnemyCharacter* Portal : Mode.VoidPortalsNow())
		{
			Counts.Add(Mode.AbominationsOf(Portal).Num());
		}
		return Counts;
	}

	FString CountsText(const TArray<int32>& Counts)
	{
		FString Out;
		for (const int32 Count : Counts)
		{
			Out += FString::Printf(TEXT("%s%d"), Out.IsEmpty() ? TEXT("") : TEXT(","), Count);
		}
		return Out;
	}
}

/**
 * The Reaper standing at a save stands after the load, at its full health, at which the game holds it, still unable to be
 * hurt, and the floor's Reaper, so the beat does not raise a second. Until this a load removed it and the floor's clock raised a new one ten
 * seconds later at the entrance. Issue #753, Group 3d-4b, ruled 2026-10-31. Prints "LOAD REAPER" and "LOAD REAPER HIT".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadReaperTest,
	"Cataclysm.SaveLoad.TheReaperStillStandsAndCannotBeHurtAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadReaperTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		return ARaisedRoundTrip(*this, FName(Effects::TheReaperKey), TEXT("LOAD REAPER"),
			[](ACataclysmDungeonGameMode& Mode)
			{
				RunTheBeatFor(Mode, Effects::TheReaperDelaySeconds + 1.0f);
				return Mode.TheReaperOnTheFloor() != nullptr;
			},
			[](const ACataclysmDungeonGameMode& Mode)
			{
				TArray<ACataclysmEnemyCharacter*> Standing;
				ACataclysmEnemyCharacter* Reaper = Mode.TheReaperOnTheFloor();
				if (IsValid(Reaper) && !UCataclysmSkillEffects::IsDead(Reaper))
				{
					Standing.Add(Reaper);
				}
				return Standing;
			},
			// ITS RUNG, AND NOT ITS BEING UNABLE TO BE HURT, which the hit below reads.
			[](const ACataclysmEnemyCharacter& One) { return One.RarityStep == Effects::TheReaperRung; },
			[this](ACataclysmDungeonGameMode& Mode, int32)
			{
				// A HIT FROM THE PLAYER, with nothing to evade it or shield from it, and nothing answered back.
				ACataclysmEnemyCharacter* Reaper = Mode.TheReaperOnTheFloor();
				ACataclysmPlayerCharacter* Player = ThePlayerOf(Mode);
				UAbilitySystemComponent* Abilities = Reaper ? Reaper->GetAbilitySystemComponent() : nullptr;
				float Before = -1.0f;
				float After = -1.0f;
				if (Abilities && Player)
				{
					Abilities->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
					Abilities->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
					Before = Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
					FCataclysmHitDelivery NoRetaliation;
					NoRetaliation.bCannotBeRetaliatedAgainst = true;
					UCataclysmSkillEffects::ApplyHit(Player, Reaper, 100.0f, FGameplayTagContainer(), NoRetaliation);
					After = Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
				}

				// AND THE FLOOR'S CLOCK RUN PAST THE REAPER'S ARRIVAL AGAIN.
				RunTheBeatFor(Mode, Effects::TheReaperDelaySeconds + 1.0f);
				const bool bTheSame = Reaper && Mode.TheReaperOnTheFloor() == Reaper;
				AddInfo(FString::Printf(TEXT("LOAD REAPER HIT: its health %.1f before a hit of 100 and %.1f after; after %.0f s "
											 "more %s"),
										Before, After, Effects::TheReaperDelaySeconds + 1.0f,
										bTheSame ? TEXT("the same Reaper") : TEXT("ANOTHER REAPER OR NONE")));
				if (!TestTrue(TEXT("set-up: the Reaper and the player stand"), Abilities && Player))
				{
					return;
				}
				TestEqual(TEXT("a hit on the Reaper does no damage"), After, Before);
				TestTrue(TEXT("and no second Reaper comes"), bTheSame);
			},
			// AT ITS FULL HEALTH: the game holds a creature that cannot be hurt at its maximum, so the halving before the
			// save is refused and it is saved at full health (window w120). What proves it cannot be hurt is the hit above.
			1.0f);
	});
}

/**
 * The Corrupted Stalker standing at a save stands after the load at once, where it stood, with the build it wore and the
 * health it had (Q2). Until this a load removed it, and it came again at the entrance thirty seconds later with the
 * health the last beat kept. Its mana and cooldowns restart (D1). Issue #753, Group 3d-4b, ruled 2026-10-31. Prints
 * "LOAD STALKER" and "LOAD STALKER PLACE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadStalkerTest,
	"Cataclysm.SaveLoad.TheCorruptedStalkerStandsWhereItStoodAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadStalkerTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		FVector SavedAt = FVector::ZeroVector;
		FString BuildSaved;
		int32 PiecesSaved = 0;
		return ARaisedRoundTrip(*this, FName(Effects::CorruptedStalkerKey), TEXT("LOAD STALKER"),
			[&SavedAt, &BuildSaved, &PiecesSaved](ACataclysmDungeonGameMode& Mode)
			{
				RunTheBeatFor(Mode, Effects::CorruptedStalkerFirstFloorSeconds + 1.0f);
				const ACataclysmCorruptedStalkerCharacter* Stalker = Mode.CorruptedStalkerOnTheFloor();
				if (!Stalker)
				{
					return false;
				}
				SavedAt = Stalker->GetActorLocation();
				BuildSaved = StalkerBuildLine(*Stalker);
				PiecesSaved = Stalker->WornPieces().Num();
				return true;
			},
			[](const ACataclysmDungeonGameMode& Mode)
			{
				TArray<ACataclysmEnemyCharacter*> Standing;
				ACataclysmCorruptedStalkerCharacter* Stalker = Mode.CorruptedStalkerOnTheFloor();
				if (IsValid(Stalker) && !UCataclysmSkillEffects::IsDead(Stalker))
				{
					Standing.Add(Stalker);
				}
				return Standing;
			},
			[](const ACataclysmEnemyCharacter& One) { return One.IsA<ACataclysmCorruptedStalkerCharacter>(); },
			[this, &SavedAt, &BuildSaved, &PiecesSaved](ACataclysmDungeonGameMode& Mode, int32)
			{
				const ACataclysmCorruptedStalkerCharacter* Stalker = Mode.CorruptedStalkerOnTheFloor();
				const float FromSaved = Stalker ? FVector::Dist(Stalker->GetActorLocation(), SavedAt) : -1.0f;
				const float SavedFromEntrance = FVector::Dist2D(SavedAt, Mode.CurrentFloor->EntranceWorld());
				const FString Build = Stalker ? StalkerBuildLine(*Stalker) : FString(TEXT("NONE"));
				AddInfo(FString::Printf(TEXT("LOAD STALKER PLACE: %.1f cm from where it stood, which was %.0f cm from the "
											 "entrance; %d piece(s) worn at the save; the build %s"),
										FromSaved, SavedFromEntrance, PiecesSaved,
										Build == BuildSaved ? TEXT("the same") : TEXT("DIFFERENT")));
				AddInfo(FString::Printf(TEXT("LOAD STALKER BUILD: saved [%s]; after [%s]"), *BuildSaved, *Build));
				if (!TestNotNull(TEXT("set-up: the Stalker stands after the load"), Stalker))
				{
					return;
				}
				TestTrue(TEXT("CONTROL: it stood away from the entrance, where it would come again"),
						 SavedFromEntrance > 500.0f);
				TestTrue(TEXT("it stands where it stood"), FromSaved >= 0.0f && FromSaved < 1.0f);
				TestTrue(TEXT("CONTROL: it wore pieces at the save"), PiecesSaved > 0);
				TestEqual(TEXT("and wears the build it wore"), Build, BuildSaved);
			});
	});
}

/**
 * A Corrupted Stalker slain before a save stays slain after the load: no floor raises it again in that dungeon. Group 2
 * saves the mark, and until this no test read it across a load -- its round trip sets it false, and the rule's own test
 * `ASlainCorruptedStalkerIsNotRaisedAgainInThatDungeon` has no load. Issue #753, Group 3d-4b. Prints "LOAD STALKER SLAIN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadSlainStalkerTest,
	"Cataclysm.SaveLoad.ASlainCorruptedStalkerStaysSlainAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadSlainStalkerTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		return ARoundTripOfRows(*this, {FName(Effects::CorruptedStalkerKey)},
			[this](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&, FPinnedDungeonSeed& PinnedSeed)
			{
				const int32 Seed = AFloorTwoWhere(Mode, PinnedSeed, [&Mode]()
				{
					RunTheBeatFor(Mode, Effects::CorruptedStalkerFirstFloorSeconds + 1.0f);
					return Mode.CorruptedStalkerOnTheFloor() != nullptr;
				});
				if (!TestTrue(TEXT("set-up: a seed from 1 to 20 raises the Stalker"), Seed != INDEX_NONE))
				{
					return false;
				}
				StandAtTheEntrance(Mode, Player);
				ACataclysmCorruptedStalkerCharacter* Stalker = Mode.CorruptedStalkerOnTheFloor();
				UCataclysmSkillEffects::ReduceHealthDirectly(&Player, Stalker, 100000000.0f);
				BeatOnce(Mode);
				return TestTrue(TEXT("set-up: it died"), UCataclysmSkillEffects::IsDead(Stalker))
					&& TestTrue(TEXT("set-up: and it is slain in this dungeon"), Mode.CorruptedStalkerSlainInThisDungeon())
					&& ThePlayerIsAlive(*this, Player);
			},
			[this](ACataclysmDungeonGameMode& Mode, ACataclysmPlayerCharacter& Player, UWorld&, const FCataclysmLoadReport& Report)
			{
				StandAtTheEntrance(Mode, Player);
				const bool bSlain = Mode.CorruptedStalkerSlainInThisDungeon();
				RunTheBeatFor(Mode, Effects::CorruptedStalkerFollowSeconds + 1.0f);
				const bool bRaised = Mode.CorruptedStalkerOnTheFloor() != nullptr;
				AddInfo(FString::Printf(TEXT("LOAD STALKER SLAIN: %s (%s); %s, and after %.0f s %s"),
										FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
										bSlain ? TEXT("slain") : TEXT("NOT SLAIN"), Effects::CorruptedStalkerFollowSeconds + 1.0f,
										bRaised ? TEXT("A STALKER STANDS") : TEXT("none stands")));
				TestTrue(TEXT("the Stalker slain at the save stays slain"), bSlain);
				TestFalse(TEXT("and no floor raises it again"), bRaised);
			});
	});
}

/**
 * Portal Unleashing's creatures standing at a save stand after the load, each back with its own portal, which the rebuild
 * places where the floor's seed puts it: Abominations paying nothing and not on the floor's list. Until this a load removed
 * them and the portals sent new ones ten seconds later. Issue #753, Group 3d-4b, ruled 2026-10-31. Prints "LOAD PORTAL
 * CREATURES" and "LOAD PORTALS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveLoadPortalCreaturesTest,
	"Cataclysm.SaveLoad.APortalsCreaturesStillStandAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveLoadPortalCreaturesTest::RunTest(const FString&)
{
	using namespace CataclysmSaveLoadTest;
	using Effects = UCataclysmDungeonModifierEffects;

	return WithTheSeedPutBack(*this, [this]()
	{
		TArray<int32> ByPortalSaved;
		return ARaisedRoundTrip(*this, FName(Effects::PortalUnleashingKey), TEXT("LOAD PORTAL CREATURES"),
			[&ByPortalSaved](ACataclysmDungeonGameMode& Mode)
			{
				RunTheBeatFor(Mode, Effects::PortalUnleashingSecondsBetween + 1.0f);
				ByPortalSaved = PortalCreaturesByPortal(Mode);
				return true;
			},
			[](const ACataclysmDungeonGameMode& Mode)
			{
				TArray<ACataclysmEnemyCharacter*> Standing;
				for (const ACataclysmEnemyCharacter* Portal : Mode.VoidPortalsNow())
				{
					Standing.Append(Mode.AbominationsOf(Portal));
				}
				return Standing;
			},
			[](const ACataclysmEnemyCharacter& One) { return One.bIsAnAbomination && !One.PaysForItsDeath(); },
			[this, &ByPortalSaved](ACataclysmDungeonGameMode& Mode, int32)
			{
				int32 OnTheList = 0;
				for (const ACataclysmEnemyCharacter* Portal : Mode.VoidPortalsNow())
				{
					for (ACataclysmEnemyCharacter* Sent : Mode.AbominationsOf(Portal))
					{
						OnTheList += Mode.FloorEnemies.Contains(Sent) ? 1 : 0;
					}
				}
				const TArray<int32> ByPortal = PortalCreaturesByPortal(Mode);
				AddInfo(FString::Printf(TEXT("LOAD PORTALS: %d portal(s); their creatures by portal [%s], at the save [%s]; %d "
											 "on the floor's list"),
										Mode.VoidPortalsNow().Num(), *CountsText(ByPortal), *CountsText(ByPortalSaved),
										OnTheList));
				TestEqual(TEXT("each portal's creatures as at the save"), CountsText(ByPortal), CountsText(ByPortalSaved));
				TestEqual(TEXT("none is on the floor's list"), OnTheList, 0);
			});
	});
}

#endif // WITH_AUTOMATION_TESTS
