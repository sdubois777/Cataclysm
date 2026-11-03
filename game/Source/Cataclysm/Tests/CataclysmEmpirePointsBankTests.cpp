// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dom/JsonObject.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Banking what a defeated dungeon earns into the account, saving the bank, and showing it. Issue #52, the first slice.
 *
 * WHERE THE BANK LIVES. In the account record of the playing character's partition (`UCataclysmAccountSave`), held by
 * the game instance: `docs/Cataclysm_GDD_v2.md`, Empire-Wide Upgrades, says the points "persist through all runs --
 * including failed ones" and are "earned into that character's lethality mode". What one defeat is worth is the run's
 * to measure, and its tests are the five `Cataclysm.EmpirePoints.` tests at the end of
 * `CataclysmEmpire/Tests/CataclysmEmpireRunTests.cpp`.
 *
 * THE SLOT IS THE TEST'S OWN. While an automation test runs, `UCataclysmGameInstance::AccountSlotFor` ends an account
 * slot in `_AutomationTest`, so nothing here reads or rewrites the player's own bank; each test that banks deletes that
 * slot first, and `FCataclysmTestSaves` deletes it again when the test ends.
 */

namespace CataclysmEmpirePointsBankTest
{
	/** A world with this project's game instance, a possessed player of one partition, a dungeon game mode and a run. */
	struct FBankScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmDungeonGameMode* Mode = nullptr;
		UCataclysmEmpireRun* Run = nullptr;
		FCataclysmCharacterPartition Partition;

		bool IsUsable() const
		{
			return World && Instance && State && Character && Mode && Run && Run->Map && Run->Clock;
		}
	};

	/** Waits for any write to the slot to finish, then deletes it when it exists. */
	void DeleteSlot(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		if (UGameplayStatics::DoesSaveGameExist(SlotName, UCataclysmSaveWriter::UserIndex))
		{
			UGameplayStatics::DeleteGameInSlot(SlotName, UCataclysmSaveWriter::UserIndex);
		}
	}

	/**
	 * The scene: Standard and offline unless told otherwise, the test's account slot emptied first, and a run begun with
	 * nothing standing on it. `bWriting` begins the save writer, as `ACataclysmGameMode::BeginSavingThisRun` does at
	 * Play, so `ECataclysmSaveTrigger::AccountChanged` writes the bank.
	 */
	FBankScene MakeScene(bool bSoloSelfFound = false, bool bWriting = false)
	{
		FBankScene Out;
		Out.Partition.Lethality = ECataclysmLethality::Standard;
		Out.Partition.Population = ECataclysmPopulation::Offline;
		Out.Partition.bSoloSelfFound = bSoloSelfFound;

		const FString Slot = UCataclysmGameInstance::AccountSlotFor(Out.Partition);
		if (!Slot.IsEmpty())
		{
			DeleteSlot(Slot);
		}

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
			Out.State->SetIdentity(TEXT("Banker"), Out.Partition);
		}

		if (bWriting)
		{
			if (UCataclysmSaveWriter* Writer = UCataclysmSaveWriter::In(Out.World))
			{
				Writer->BeginRun(FGuid::NewGuid(), FGuid::NewGuid(), FName(TEXT("Sandbox")), 1);
			}
		}

		Out.Mode = Out.World->SpawnActor<ACataclysmDungeonGameMode>();

		// THE GAME INSTANCE'S RUN, which `ACataclysmDungeonGameMode::EmpireRun` reads before its test seam.
		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		Out.Run->Begin(1);
		Out.Instance->EmpireRun = Out.Run;
		return Out;
	}

	/** The first Outpost that has not fallen, or `INDEX_NONE`. */
	int32 AnOutpost(const UCataclysmEmpireRun& Run)
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

	/** One dungeon of this kind on an Outpost, by hand, with a long timer. Answers its number, or `INDEX_NONE`. */
	int32 Place(UCataclysmEmpireRun& Run, ECataclysmDungeonType Type)
	{
		const int32 CityId = AnOutpost(Run);
		const FCataclysmCity* City = Run.Map->Find(CityId);
		if (City == nullptr || Run.Clock == nullptr)
		{
			return INDEX_NONE;
		}

		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = Run.NextDungeonId;
		Dungeon.Type = Type;
		Dungeon.SubType = ECataclysmDungeonSubType::None;
		Dungeon.CityId = CityId;
		Dungeon.CityTier = City->Tier;
		Dungeon.Floors = 5;
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

	/** Puts a dungeon on the map, binds the game mode to it and clears it, as the last floor's stairs do. */
	bool PlaceAndClear(FAutomationTestBase& Test, const FBankScene& Scene, ECataclysmDungeonType Type)
	{
		const int32 DungeonId = Place(*Scene.Run, Type);
		if (!Test.TestTrue(TEXT("set-up: a dungeon was placed"), DungeonId != INDEX_NONE))
		{
			return false;
		}
		Scene.Mode->EmpireDungeonId = DungeonId;
		return Test.TestTrue(TEXT("set-up: the game mode cleared it"), Scene.Mode->ClearEmpireDungeon());
	}

	/** The bank of the scene's partition, or -1 when there is none. */
	double Bank(const FBankScene& Scene)
	{
		const UCataclysmAccountSave* Account = Scene.Instance->AccountFor(Scene.Partition);
		return Account != nullptr ? Account->EmpireUpgradePoints : -1.0;
	}

	/** Every city at half its maximum population: all exact in binary. */
	void HalveEveryCity(UCataclysmEmpireRun& Run)
	{
		for (FCataclysmCity& City : Run.Map->Cities)
		{
			City.Population = City.MaxPopulation * 0.5f;
		}
	}
}

// ---------------------------------------------------------------------------
// A defeat banks what it earned
// ---------------------------------------------------------------------------

/**
 * Through the game mode, as the last floor's stairs reach it: an intact empire's ordinary dungeon banks 1 and a Quest
 * dungeon 3 more, into a bank that held nothing. Prints "FULL".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpirePointsBankFullTest,
	"Cataclysm.EmpirePoints.ADefeatAtFullPopulationBanksTheBase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpirePointsBankFullTest::RunTest(const FString&)
{
	using namespace CataclysmEmpirePointsBankTest;

	FBankScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	TestTrue(TEXT("CONTROL: the test's bank starts empty"), Bank(Scene) == 0.0);

	if (!PlaceAndClear(*this, Scene, ECataclysmDungeonType::Basic))
	{
		return false;
	}
	const double AfterOrdinary = Bank(Scene);

	if (!PlaceAndClear(*this, Scene, ECataclysmDungeonType::Quest))
	{
		return false;
	}
	const double AfterQuest = Bank(Scene);

	AddInfo(FString::Printf(TEXT("FULL: after an ordinary dungeon %g, after a Quest dungeon %g"), AfterOrdinary,
							AfterQuest));
	TestTrue(TEXT("an ordinary dungeon at full population banks 1"), AfterOrdinary == 1.0);
	TestTrue(TEXT("and a Quest dungeon 3 more"), AfterQuest == 4.0);
	TestEqual(TEXT("CONTROL: the bank is held in the account record of the character's partition"),
			  Scene.Instance->HeldAccount != nullptr
				  ? static_cast<int32>(Scene.Instance->HeldAccount->Lethality) : -1,
			  static_cast<int32>(ECataclysmLethality::Standard));
	return true;
}

/**
 * With every city at half its people, an ordinary dungeon banks 0.5: the fraction is kept. Prints "HALF".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpirePointsBankHalfTest,
	"Cataclysm.EmpirePoints.ADefeatAtHalfTheLivingPopulationBanksHalf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpirePointsBankHalfTest::RunTest(const FString&)
{
	using namespace CataclysmEmpirePointsBankTest;

	FBankScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	HalveEveryCity(*Scene.Run);
	TestTrue(TEXT("CONTROL: the living population is exactly half the maximum"),
			 2.0 * Scene.Run->Map->TotalPopulation() == Scene.Run->Map->TotalMaxPopulation());

	if (!PlaceAndClear(*this, Scene, ECataclysmDungeonType::Basic))
	{
		return false;
	}
	const double Banked = Bank(Scene);

	AddInfo(FString::Printf(TEXT("HALF: banked %g"), Banked));
	TestTrue(TEXT("an ordinary dungeon at half the population banks 0.5"), Banked == 0.5);
	return true;
}

/**
 * Three ordinary dungeons defeated with a third of the population living bank exactly 1.0: each awards a third, and
 * the thirds are kept rather than rounded away (ruled 2026-11-03).
 *
 * EVERY CITY IS GIVEN 1,000 PEOPLE OF A MAXIMUM OF 3,000, so the share is 25,000 over 75,000: both exact in binary, and
 * their quotient is the double nearest a third, three of which add up to exactly 1.0. The map's own maxima would give a
 * third of 610,000, which is not a whole number of people. Prints "THIRDS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpirePointsBankThirdsTest,
	"Cataclysm.EmpirePoints.ThreeOrdinaryDefeatsAtAThirdOfThePopulationBankOnePoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpirePointsBankThirdsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpirePointsBankTest;

	FBankScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	for (FCataclysmCity& City : Scene.Run->Map->Cities)
	{
		City.MaxPopulation = 3000.0f;
		City.Population = 1000.0f;
	}
	TestTrue(TEXT("CONTROL: a third of the population is living"),
			 3.0 * Scene.Run->Map->TotalPopulation() == Scene.Run->Map->TotalMaxPopulation());

	TArray<double> Banks;
	for (int32 Defeat = 0; Defeat < 3; ++Defeat)
	{
		if (!PlaceAndClear(*this, Scene, ECataclysmDungeonType::Basic))
		{
			return false;
		}
		Banks.Add(Bank(Scene));
	}

	AddInfo(FString::Printf(TEXT("THIRDS: %.17g, %.17g, %.17g; shown as %s"), Banks[0], Banks[1], Banks[2],
							*UCataclysmEmpireMapLayout::EmpirePointsText(Banks[2])));
	TestTrue(TEXT("the first defeat banks a third"), Banks[0] == 1.0 / 3.0);
	TestTrue(TEXT("three defeats at a third bank exactly 1.0"), Banks[2] == 1.0);
	TestEqual(TEXT("and the screen shows 1.0"), UCataclysmEmpireMapLayout::EmpirePointsText(Banks[2]),
			  FString(TEXT("1.0")));
	return true;
}

// ---------------------------------------------------------------------------
// What banks nothing
// ---------------------------------------------------------------------------

/**
 * A death in a dungeon resolves it at once, and a resolve is not a defeat: the bank does not move. The run's last
 * award is still the clear before it. Prints "DEATH".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpirePointsBankDeathTest,
	"Cataclysm.EmpirePoints.ADeathsResolveBanksNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpirePointsBankDeathTest::RunTest(const FString&)
{
	using namespace CataclysmEmpirePointsBankTest;

	FBankScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	if (!PlaceAndClear(*this, Scene, ECataclysmDungeonType::Basic))
	{
		return false;
	}
	const double BeforeTheDeath = Bank(Scene);

	const int32 Detonated = Scene.Run->DungeonsDetonated;
	const int32 DiedIn = Place(*Scene.Run, ECataclysmDungeonType::Basic);
	if (!TestTrue(TEXT("set-up: the dungeon to die in was placed"), DiedIn != INDEX_NONE))
	{
		return false;
	}
	Scene.Mode->EmpireDungeonId = DiedIn;
	const bool bEnded = Scene.Mode->EndTheDungeonForADeath();
	const double AfterTheDeath = Bank(Scene);

	AddInfo(FString::Printf(TEXT("DEATH: banked %g before the death and %g after it; the last award %g"), BeforeTheDeath,
							AfterTheDeath, Scene.Run->LastEmpirePointsAwarded));
	TestTrue(TEXT("CONTROL: the death ended the walk"), bEnded);
	TestEqual(TEXT("CONTROL: and the dungeon resolved on its city"), Scene.Run->DungeonsDetonated, Detonated + 1);
	TestTrue(TEXT("CONTROL: the clear before it banked 1"), BeforeTheDeath == 1.0);
	TestTrue(TEXT("a death's resolve banks nothing"), AfterTheDeath == BeforeTheDeath);
	return true;
}

/**
 * A Solo Self-Found character has no account record, and this slice does not bank into its own character record: the
 * defeat banks nothing anywhere, and the screen says so. Prints "SOLO".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpirePointsBankSoloTest,
	"Cataclysm.EmpirePoints.ASoloSelfFoundDefeatBanksNothingInAnyAccount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpirePointsBankSoloTest::RunTest(const FString&)
{
	using namespace CataclysmEmpirePointsBankTest;

	FBankScene Scene = MakeScene(/*bSoloSelfFound=*/true);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 DungeonId = Place(*Scene.Run, ECataclysmDungeonType::Basic);
	if (!TestTrue(TEXT("set-up: a dungeon was placed"), DungeonId != INDEX_NONE))
	{
		return false;
	}
	Scene.Mode->EmpireDungeonId = DungeonId;
	const bool bCleared = Scene.Mode->ClearEmpireDungeon();
	const FString Line = UCataclysmEmpireMapLayout::EmpirePointsLineIn(Scene.World, Scene.Run);

	AddInfo(FString::Printf(TEXT("SOLO: cleared %s, awarded %g, account held %s; the line '%s'"),
							bCleared ? TEXT("yes") : TEXT("no"), Scene.Run->LastEmpirePointsAwarded,
							Scene.Instance->HeldAccount != nullptr ? TEXT("yes") : TEXT("no"), *Line));
	TestTrue(TEXT("CONTROL: the dungeon was cleared"), bCleared);
	TestTrue(TEXT("CONTROL: and the run measured its award"), Scene.Run->LastEmpirePointsAwarded == 1.0);
	TestTrue(TEXT("no account record is held for a Solo Self-Found character"),
			 Scene.Instance->HeldAccount == nullptr);
	TestEqual(TEXT("and the screen says its points are not banked"), Line,
			  FString(TEXT("Solo Self-Found: empire points are not banked yet")));
	return true;
}

// ---------------------------------------------------------------------------
// The bank is saved
// ---------------------------------------------------------------------------

/**
 * The defeat's bank is written to the account slot by `ECataclysmSaveTrigger::AccountChanged`, and reads back from it,
 * both through the storage and through a new game instance, as the next session would. Prints "ROUND TRIP".
 *
 * THE WRITES ARE COUNTED AS AT LEAST ONE AND NOT AS EXACTLY ONE: leaving the dungeon after the clear does a great deal,
 * and whether any of it raises another trigger is not what this test is about. The slot holding the bank is.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpirePointsBankRoundTripTest,
	"Cataclysm.EmpirePoints.TheBankSurvivesASaveRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpirePointsBankRoundTripTest::RunTest(const FString&)
{
	using namespace CataclysmEmpirePointsBankTest;

	FBankScene Scene = MakeScene(/*bSoloSelfFound=*/false, /*bWriting=*/true);
	UCataclysmSaveWriter* Writer = Scene.World ? UCataclysmSaveWriter::In(Scene.World) : nullptr;
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestTrue(TEXT("set-up: and its save writer is writing"), Writer != nullptr && Writer->IsWriting()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FString Slot = UCataclysmGameInstance::AccountSlotFor(Scene.Partition);
	HalveEveryCity(*Scene.Run);
	const int32 WritesBefore = Writer->WritesStarted();

	if (!PlaceAndClear(*this, Scene, ECataclysmDungeonType::Basic))
	{
		return false;
	}
	const int32 Writes = Writer->WritesStarted() - WritesBefore;

	// THE WRITE IS ASYNCHRONOUS: wait for it to land before reading the slot.
	const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
	while (FCataclysmSaveStorage::HasAWriteInFlight(Slot) && FPlatformTime::Seconds() < GiveUpAt)
	{
		FPlatformProcess::Sleep(0.01f);
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
	FString Message;
	const UCataclysmAccountSave* Read = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::ReadFromSlot(
		Slot, UCataclysmSaveWriter::UserIndex, UCataclysmAccountSave::StaticClass(), GetTransientPackage(), Result,
		Message));

	// AND A NEW SESSION'S GAME INSTANCE, which holds nothing until it reads the slot.
	UCataclysmGameInstance* NextSession = NewObject<UCataclysmGameInstance>();
	const UCataclysmAccountSave* Reread = NextSession->AccountFor(Scene.Partition);

	AddInfo(FString::Printf(TEXT("ROUND TRIP: %d write(s) started; '%s' read back %s (%s) holding %g; a new game "
								 "instance reads %g"),
							Writes, *Slot,
							Read != nullptr ? TEXT("a record") : TEXT("nothing"), FCataclysmSaveStorage::Describe(Result),
							Read != nullptr ? Read->EmpireUpgradePoints : -1.0,
							Reread != nullptr ? Reread->EmpireUpgradePoints : -1.0));

	TestEqual(TEXT("CONTROL: the slot is the test's own, not the player's"),
			  Slot, UCataclysmSavePartition::AccountSlotName(Scene.Partition) + TEXT("_AutomationTest"));
	TestTrue(TEXT("CONTROL: the clear started a write"), Writes >= 1);
	if (!TestNotNull(TEXT("the account record reads back from its slot"), Read))
	{
		return false;
	}
	TestEqual(TEXT("at the current version"), Read->SchemaVersion, UCataclysmAccountSave::SchemaVersionNow);
	TestTrue(TEXT("holding the half point banked"), Read->EmpireUpgradePoints == 0.5);
	TestEqual(TEXT("naming the partition's lethality mode"), static_cast<int32>(Read->Lethality),
			  static_cast<int32>(ECataclysmLethality::Standard));
	TestEqual(TEXT("and its population"), static_cast<int32>(Read->Population),
			  static_cast<int32>(ECataclysmPopulation::Offline));
	TestTrue(TEXT("and a new game instance reads the same bank"),
			 Reread != nullptr && Reread->EmpireUpgradePoints == 0.5);
	return true;
}

/**
 * `Account_v1.json`, written when the bank was a whole number, migrates to the current version holding the same 37
 * points, and every other field is kept. Prints "ACCOUNT MIGRATED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpirePointsAccountV1MigratesTest,
	"Cataclysm.EmpirePoints.AnAccountFileFromBeforeFractionsMigratesKeepingItsPoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpirePointsAccountV1MigratesTest::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Account_v1.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	// THE STEP, ON THE FILE.
	const TSharedPtr<FJsonObject> Stepped = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Account_v1.json is a JSON object"), Stepped.IsValid()))
	{
		return false;
	}
	FString StepMessage;
	const ECataclysmSaveMigrationResult Stepping = FCataclysmSaveMigration::Migrate(
		GetDefault<UCataclysmAccountSave>()->MigrationSteps(), UCataclysmAccountSave::SchemaVersionNow,
		Stepped.ToSharedRef(), StepMessage);
	double SteppedPoints = -1.0;
	const bool bWritten = Stepped->TryGetNumberField(TEXT("EmpireUpgradePoints"), SteppedPoints);

	// THE RECORD.
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmAccountSave* Read = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmAccountSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Account_v1.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	// AND NOTHING ELSE MOVED: the file as it was, at the current version, is what the record writes back.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmAccountSave::SchemaVersionNow);
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	AddInfo(FString::Printf(TEXT("ACCOUNT MIGRATED: the step %d (%s), wrote the points %s at %g; the record at version "
								 "%d holding %g; the rest %s"),
							static_cast<int32>(Stepping), *StepMessage, bWritten ? TEXT("yes") : TEXT("NO"),
							SteppedPoints, Read->SchemaVersion, Read->EmpireUpgradePoints, bSame ? TEXT("kept") : *Where));

	TestEqual(TEXT("the chain migrates the version 1 file"), static_cast<int32>(Stepping),
		static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("the step writes the same 37 points into the file"), bWritten && SteppedPoints == 37.0);
	TestEqual(TEXT("it arrives at the current version"), Read->SchemaVersion, UCataclysmAccountSave::SchemaVersionNow);
	TestTrue(TEXT("an account from before fractions keeps its 37 points"), Read->EmpireUpgradePoints == 37.0);
	TestTrue(TEXT("and every other field is kept"), bSame);
	return true;
}

/**
 * `Account_v2.json`, the committed account file, reads a fraction of a point, 37.625, into the record: the double is
 * read and not an integer field. Prints "ACCOUNT FILE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpirePointsAccountV2ReadsTest,
	"Cataclysm.EmpirePoints.TheCommittedAccountFileKeepsAFractionOfAPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpirePointsAccountV2ReadsTest::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Account_v2.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmAccountSave* Read = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmAccountSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Account_v2.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	AddInfo(FString::Printf(TEXT("ACCOUNT FILE: %s, version %d, %g points, %d character(s)"),
							FCataclysmSaveStorage::Describe(Result), Read->SchemaVersion, Read->EmpireUpgradePoints,
							Read->Characters.Num()));
	TestEqual(TEXT("it is read rather than migrated"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestTrue(TEXT("the bank keeps its fraction, 37.625"), Read->EmpireUpgradePoints == 37.625);
	TestEqual(TEXT("the lethality mode is the file's"), static_cast<int32>(Read->Lethality),
			  static_cast<int32>(ECataclysmLethality::Hardcore));
	TestEqual(TEXT("and the population"), static_cast<int32>(Read->Population),
			  static_cast<int32>(ECataclysmPopulation::Online));
	TestEqual(TEXT("and its two characters"), Read->Characters.Num(), 2);
	return true;
}

// ---------------------------------------------------------------------------
// The player sees it
// ---------------------------------------------------------------------------

/**
 * The empire screen's words for the bank: rounded down to a tenth, and the last award beside it; and, for the
 * character playing, its partition's bank after a defeat. Prints "MAP LINE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpirePointsMapLineTest,
	"Cataclysm.EmpirePoints.TheMapLineShowsTheBankRoundedDownToATenth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpirePointsMapLineTest::RunTest(const FString&)
{
	using namespace CataclysmEmpirePointsBankTest;
	using Layout = UCataclysmEmpireMapLayout;

	TestEqual(TEXT("nothing banked reads 0.0"), Layout::EmpirePointsText(0.0), FString(TEXT("0.0")));
	TestEqual(TEXT("12.96 is rounded down, to 12.9"), Layout::EmpirePointsText(12.96), FString(TEXT("12.9")));
	TestEqual(TEXT("37.625 reads 37.6"), Layout::EmpirePointsText(37.625), FString(TEXT("37.6")));
	TestEqual(TEXT("a twentieth reads 0.0"), Layout::EmpirePointsText(0.05), FString(TEXT("0.0")));
	TestEqual(TEXT("a bank with no award beside it"), Layout::EmpirePointsLine(37.625, 0.0),
			  FString(TEXT("37.6 empire points banked")));
	TestEqual(TEXT("a bank with the last award beside it"), Layout::EmpirePointsLine(12.5, 0.5),
			  FString(TEXT("12.5 empire points banked (+0.5 from the last dungeon defeated)")));

	FBankScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FString Before = Layout::EmpirePointsLineIn(Scene.World, Scene.Run);
	HalveEveryCity(*Scene.Run);
	if (!PlaceAndClear(*this, Scene, ECataclysmDungeonType::Basic))
	{
		return false;
	}
	const FString After = Layout::EmpirePointsLineIn(Scene.World, Scene.Run);
	const FString NoWorld = Layout::EmpirePointsLineIn(nullptr, Scene.Run);

	AddInfo(FString::Printf(TEXT("MAP LINE: before '%s'; after '%s'; with no world '%s'"), *Before, *After, *NoWorld));
	TestEqual(TEXT("before any defeat the screen shows an empty bank"), Before,
			  FString(TEXT("0.0 empire points banked")));
	TestEqual(TEXT("after a defeat at half the population it shows the half point and where it came from"), After,
			  FString(TEXT("0.5 empire points banked (+0.5 from the last dungeon defeated)")));
	TestEqual(TEXT("and with no world to find a character in it shows nothing"), NoWorld, FString());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
