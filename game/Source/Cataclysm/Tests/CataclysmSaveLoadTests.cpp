// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
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

	const FCataclysmLoadReport Report = FCataclysmSaveLoad::CharacterAndRun(*Scene.World, *Scene.Character,
																			 Saved.CharacterId);
	const bool bDeadAfterTheLoad = UCataclysmSkillEffects::IsDead(Scene.Character);
	const ECataclysmSaveTrigger AfterTheLoad = Scene.Writer->LastTrigger();
	const int32 Synchronous = Scene.Writer->SynchronousWrites();
	const bool bSameSlots = Scene.Writer->RunSlotName() == Saved.RunSlot();

	Scene.Character->Revive();
	const bool bStandingAfterRevive = !UCataclysmSkillEffects::IsDead(Scene.Character);
	const ECataclysmSaveTrigger AfterRevive = Scene.Writer->LastTrigger();
	WaitForSlot(Saved.RunSlot());

	AddInfo(FString::Printf(TEXT("LOAD DEAD: %s (%s); %s after the load, last trigger '%s', %d synchronous writes; "
								 "%s after standing up, last trigger '%s'"),
							FCataclysmSaveLoad::Describe(Report.Outcome), *Report.Why,
							bDeadAfterTheLoad ? TEXT("dead") : TEXT("ALIVE"),
							UCataclysmSaveTriggers::NameOf(AfterTheLoad), Synchronous,
							bStandingAfterRevive ? TEXT("standing") : TEXT("STILL DEAD"),
							UCataclysmSaveTriggers::NameOf(AfterRevive)));

	TestEqual(TEXT("the load put the character and its run back"),
			  static_cast<int32>(Report.Outcome), static_cast<int32>(ECataclysmLoadOutcome::Loaded));
	TestTrue(TEXT("the character is dead after the load"), bDeadAfterTheLoad);
	TestEqual(TEXT("its death raised the death's own write"),
			  static_cast<int32>(AfterTheLoad), static_cast<int32>(ECataclysmSaveTrigger::CharacterDied));
	TestTrue(TEXT("which was written before the load returned"), Synchronous > 0);
	TestTrue(TEXT("into the slots the load came from"), bSameSlots);
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

#endif // WITH_AUTOMATION_TESTS
