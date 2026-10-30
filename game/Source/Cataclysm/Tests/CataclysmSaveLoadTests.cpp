// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Dungeon/CataclysmDungeonStairs.h"
#include "Dungeon/CataclysmFloorBrief.h"
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
									 bool bChoiceAndCellRules)
	{
		FSaved Saved;
		ON_SCOPE_EXIT { Saved.Forget(); };
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
				|| !Test.TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
				|| !Test.TestTrue(TEXT("set-up: the walk reached floor 2"), Scene.Mode->GoToFloor(2)))
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
		Test.AddInfo(FString::Printf(TEXT("LOAD REBUILD %s: %s (%s); %spillar cells %d/%d%s"), Which,
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
		if (Scene.Mode->GoToFloor(2))
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

	RoundTripTheRulesOfOneFloor(*this, TEXT("choices and cells"), ChoiceAndCellRules(), /*bChoiceAndCellRules=*/true);
	RoundTripTheRulesOfOneFloor(*this, TEXT("creatures and zones"), CreatureAndZoneRules(),
								/*bChoiceAndCellRules=*/false);
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
	return true;
}

#endif // WITH_AUTOMATION_TESTS
