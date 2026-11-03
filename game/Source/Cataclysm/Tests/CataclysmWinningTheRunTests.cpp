// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
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
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"

/**
 * Clearing the Cataclysm boss dungeon wins the run: the character is paid the first-kill passive points, its tier rises
 * by one before its record is written, and its next run begins at once, facing the won run's Cataclysms and one more,
 * with a line on its map saying the last run was won. Issue #1324 slice 6e, ruled by the coordinating session on
 * 2026-11-02 under the owner's delegation (R4, R6, R7, and the order of the comment of 2026-11-01).
 *
 * `Cataclysm.WinningTheRun.` IS A NEW GROUP and selects these and nothing else. The empire half -- that the clear is the
 * win and drops the walk's owed days -- is `Cataclysm.BossClearedWins.`, in the `CataclysmEmpire` module.
 *
 * THE WIN GOES THROUGH THE PATH EVERY CLEAR GOES THROUGH. A world with a game instance, a possessed character, a save
 * writer and a dungeon game mode that has begun play, as `Cataclysm.SaveLoad.`'s dungeon tests build it; the character's
 * run is begun through `UCataclysmGameInstance::BeginACharactersEmpireRun`, as play begins one; the boss is entered with
 * `EnterEmpireDungeon` and cleared with `ClearEmpireDungeon`, which is what the bottom floor's stairs call.
 *
 * THE BOSS IS PLACED BY HAND, on no city, three floors deep, with the timer that never runs out, as
 * `UCataclysmSurgeScheduler::MakeCataclysmDungeon` builds it but without drawing from the run's chance. How it OPENS is
 * `Cataclysm.EarnedBoss.`'s; what clearing it does is this group's, and the clear reads only its kind.
 *
 * THESE TESTS TOUCH THE DISK: the win writes both records. Every slot is named by an identifier made for the test and
 * deleted on every way out, after any write still in flight has finished, as the save/load tests do.
 *
 * NO NUMBER HERE IS A CATACLYSM'S NAME WRITTEN OUT. Which boss a win pays for is `UCataclysmRoster::OrderFor` of the
 * character's seed, asked of the game at run time; every seed is written into the tests, so every outcome is fixed.
 */
namespace CataclysmWinningTheRunTest
{
	static constexpr int32 SaveUserIndex = UCataclysmSaveWriter::UserIndex;

	/** The character's Cataclysm order seed every test gives it: slice 6a's tests' seed, which nothing else depends on. */
	static constexpr int32 TheCharactersSeed = 24681357;

	/** Waits, briefly, for a slot to exist with no write to it in flight. The save/load tests' `WaitForSlot`. */
	bool WaitForTheSlot(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FPlatformTime::Seconds() < GiveUpAt)
		{
			if (UGameplayStatics::DoesSaveGameExist(SlotName, SaveUserIndex)
				&& !FCataclysmSaveStorage::HasAWriteInFlight(SlotName))
			{
				return true;
			}
			FPlatformProcess::Sleep(0.01f);
		}
		return false;
	}

	/** Deletes a slot once nothing is writing it. */
	void DeleteTheSlot(const FString& SlotName)
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
		if (UGameplayStatics::DoesSaveGameExist(SlotName, SaveUserIndex))
		{
			UGameplayStatics::DeleteGameInSlot(SlotName, SaveUserIndex);
		}
	}

	/** Reads a record out of a slot, or null. */
	template <typename TRecord>
	TRecord* ReadTheSlot(const FString& SlotName)
	{
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
		FString Message;
		return Cast<TRecord>(FCataclysmSaveStorage::ReadFromSlot(SlotName, SaveUserIndex, TRecord::StaticClass(),
																	GetTransientPackage(), Result, Message));
	}

	/** A world with a game instance, a possessed character, a save writer and a dungeon game mode that has begun play. */
	struct FWinScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmSaveWriter* Writer = nullptr;
		ACataclysmDungeonGameMode* Mode = nullptr;

		/** The slots the game mode's own start began writing, before the test's, deleted too. */
		FString StartRunSlot;
		FString StartCharacterSlot;

		/** The test's own identifiers, which the writer writes to after the scene is made. */
		FGuid RunId;
		FGuid CharacterId;

		bool IsUsable() const
		{
			return World && Instance && State && Character && Character->GetAbilitySystemComponent() && Writer && Mode;
		}

		FString RunSlot() const { return UCataclysmSavePartition::RunSlotName(RunId); }
		FString CharacterSlot() const { return UCataclysmSavePartition::CharacterSlotName(CharacterId); }

		/** Every slot this scene could have written, deleted. */
		void ForgetTheSlots() const
		{
			DeleteTheSlot(RunSlot());
			DeleteTheSlot(CharacterSlot());
			DeleteTheSlot(StartRunSlot);
			DeleteTheSlot(StartCharacterSlot);
		}
	};

	/**
	 * The scene, as `Cataclysm.SaveLoad.`'s `MakeDungeonScene` builds it, and then the writer told the test's own two
	 * identifiers, so every write after this goes where the test deletes. The character is given its Cataclysm order
	 * seed, as a made character has one.
	 */
	FWinScene MakeWinScene()
	{
		FWinScene Out;
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
			Out.State->SetCataclysmOrderSeed(TheCharactersSeed);
		}

		Out.Writer = UCataclysmSaveWriter::In(Out.World);
		Out.Mode = Out.World->SpawnActor<ACataclysmDungeonGameMode>();
		if (Out.Mode)
		{
			Out.Mode->StartPlay();
		}
		if (Out.Writer && Out.Mode)
		{
			Out.StartRunSlot = Out.Writer->RunSlotName();
			Out.StartCharacterSlot = Out.Writer->CharacterSlotName();
			Out.RunId = FGuid::NewGuid();
			Out.CharacterId = FGuid::NewGuid();
			Out.Writer->BeginRun(Out.RunId, Out.CharacterId, Out.Mode->DungeonName, Out.Mode->FloorNumber);
		}
		return Out;
	}

	/**
	 * The Cataclysm boss dungeon, put on the board by hand as `MakeCataclysmDungeon` builds it: on no city, from the
	 * Pillar's row, sent by the newest active Cataclysm, a day a floor, with the timer that never runs out. Three floors,
	 * so a walk into it builds a first floor that is not its last. Answers its number, or `INDEX_NONE`.
	 */
	int32 PlaceTheBossByHand(UCataclysmEmpireRun& Run)
	{
		if (Run.Clock == nullptr)
		{
			return INDEX_NONE;
		}

		FCataclysmDungeon Boss;
		Boss.DungeonId = Run.NextDungeonId;
		Boss.Type = ECataclysmDungeonType::Cataclysm;
		Boss.SubType = ECataclysmDungeonSubType::None;
		Boss.Cataclysm = Run.ActiveCataclysms.Num() > 0 ? Run.ActiveCataclysms.Last() : ECataclysmType::None;
		Boss.CityId = INDEX_NONE;
		Boss.CityTier = ECataclysmCityTier::Pillar;
		Boss.Floors = 3;
		Boss.WalkDays = 3.0f;
		Boss.ResolveDays = UCataclysmSurgeScheduler::NeverResolveDays;
		Boss.SpawnedDay = Run.Day();

		Run.NextDungeonId = Boss.DungeonId + 1;
		Run.Dungeons.Add(Boss);
		if (!Run.Clock->AddDungeon(Boss.DungeonId, Boss.Floors)
			|| !Run.Clock->SetResolveDays(Boss.DungeonId, Boss.ResolveDays))
		{
			return INDEX_NONE;
		}
		return Boss.DungeonId;
	}

	/**
	 * An ordinary dungeon put on a standing Outpost by hand, three floors, a day a floor, with a hundred days on its
	 * timer and no damage, so the days a walk of it runs bite nothing. Answers its number, or `INDEX_NONE`.
	 */
	int32 PlaceAnOrdinaryOneByHand(UCataclysmEmpireRun& Run)
	{
		if (Run.Clock == nullptr || Run.Map == nullptr)
		{
			return INDEX_NONE;
		}
		const FCataclysmCity* Outpost = nullptr;
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.Tier == ECataclysmCityTier::Outpost && !City.bFallen)
			{
				Outpost = &City;
				break;
			}
		}
		if (Outpost == nullptr)
		{
			return INDEX_NONE;
		}

		FCataclysmDungeon Ordinary;
		Ordinary.DungeonId = Run.NextDungeonId;
		Ordinary.Type = ECataclysmDungeonType::Basic;
		Ordinary.SubType = ECataclysmDungeonSubType::None;
		Ordinary.CityId = Outpost->CityId;
		Ordinary.CityTier = Outpost->Tier;
		Ordinary.Floors = 3;
		Ordinary.WalkDays = 3.0f;
		Ordinary.ResolveDays = 100.0f;
		Ordinary.SpawnedDay = Run.Day();

		Run.NextDungeonId = Ordinary.DungeonId + 1;
		Run.Dungeons.Add(Ordinary);
		if (!Run.Clock->AddDungeon(Ordinary.DungeonId, Ordinary.Floors)
			|| !Run.Clock->SetResolveDays(Ordinary.DungeonId, Ordinary.ResolveDays))
		{
			return INDEX_NONE;
		}
		return Ordinary.DungeonId;
	}

	/**
	 * The character at this tier, its run begun as a character's run is begun -- the run's seed given, its Cataclysms
	 * from the character's seed -- on day 1 with the opening wave taken off the board, and the boss placed by hand.
	 * Answers the boss's number, or `INDEX_NONE`.
	 */
	int32 BeginAtTierWithTheBoss(FWinScene& Scene, int32 Tier, int32 RunSeed)
	{
		Scene.State->SetDifficultyTier(Tier);
		UCataclysmEmpireRun* Run = Scene.Instance->BeginACharactersEmpireRun(
			RunSeed, Scene.State->GetCataclysmOrderSeed(), ECataclysmSurgeMode::Static, /* LethalityRung */ 0, Tier);
		if (Run == nullptr || Run->Map == nullptr || Run->Clock == nullptr)
		{
			return INDEX_NONE;
		}
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return PlaceTheBossByHand(*Run);
	}

	/** Walks into the boss and clears it through the game mode, as the bottom floor's stairs do. */
	bool WalkInAndClear(FWinScene& Scene, int32 BossId)
	{
		return Scene.Mode->EnterEmpireDungeon(BossId) && Scene.Mode->ClearEmpireDungeon();
	}

	/** The Cataclysm the character's draw puts at this tier: `OrderFor(seed)[Tier - 1]`, the ruling R4's. */
	ECataclysmType DrawnAtTier(int32 OrderSeed, int32 Tier)
	{
		const TArray<ECataclysmType> Order = UCataclysmRoster::OrderFor(OrderSeed);
		return Order.IsValidIndex(Tier - 1) ? Order[Tier - 1] : ECataclysmType::None;
	}

	/** A list of Cataclysms as their names, comma separated, in order. */
	FString NamesOf(const TArray<ECataclysmType>& Cataclysms)
	{
		TArray<FString> Out;
		for (const ECataclysmType Cataclysm : Cataclysms)
		{
			Out.Add(UCataclysmRoster::NameFor(Cataclysm).ToString());
		}
		return FString::Join(Out, TEXT(", "));
	}
}

// ---------------------------------------------------------------------------
// The order: the tier is raised before the character record is written
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWinningTheRunWrittenTest,
	"Cataclysm.WinningTheRun.AWonRunRaisesTheTierBeforeTheCharacterRecordIsWritten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character at tier 3 is written at tier 3 (a death's write, as set-up), then wins: the character record the win
 * writes holds tier 4 and the boss defeated, and the run record it writes is the next run's -- tier 4, four Cataclysms,
 * carrying the win line. The coordinating session's comment of 2026-11-01 on #1324: the tier rises "before the
 * character record is written". The win's write is `ECataclysmSaveTrigger::RunWon`, raised once, after the raise.
 *
 * CONTROL: the record on disk held tier 3 before the win. Prints "WIN WRITTEN".
 */
bool FCataclysmWinningTheRunWrittenTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWinningTheRunTest;

	FWinScene Scene = MakeWinScene();
	if (!TestTrue(TEXT("set-up: the world was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ON_SCOPE_EXIT { Scene.ForgetTheSlots(); };

	const int32 BossId = BeginAtTierWithTheBoss(Scene, /* Tier */ 3, /* RunSeed */ 11);
	if (!TestTrue(TEXT("set-up: a tier 3 run was begun with its boss on the board"), BossId != INDEX_NONE))
	{
		return false;
	}
	UCataclysmEmpireRun* WonRun = Scene.Instance->EmpireRun;

	// THE RECORD BEFORE THE WIN, AT TIER 3: a death's write, which writes both records before it returns.
	Scene.Writer->NoteTrigger(ECataclysmSaveTrigger::CharacterDied);
	const UCataclysmCharacterSave* Before = ReadTheSlot<UCataclysmCharacterSave>(Scene.CharacterSlot());
	const int32 WrittenBefore = Before ? Before->DifficultyTier : -1;

	const bool bCleared = WalkInAndClear(Scene, BossId);
	const ECataclysmType Against = DrawnAtTier(TheCharactersSeed, 3);
	const FName AgainstName = UCataclysmRoster::NameFor(Against);
	const ECataclysmSaveTrigger LastTrigger = Scene.Writer->LastTrigger();
	const UCataclysmCharacterSave* Built = Scene.Writer->LastCharacterRecord();
	const int32 BuiltTier = Built ? Built->DifficultyTier : -1;

	WaitForTheSlot(Scene.CharacterSlot());
	WaitForTheSlot(Scene.RunSlot());
	const UCataclysmCharacterSave* Character = ReadTheSlot<UCataclysmCharacterSave>(Scene.CharacterSlot());
	const UCataclysmRunSave* Run = ReadTheSlot<UCataclysmRunSave>(Scene.RunSlot());
	const int32 WrittenAfter = Character ? Character->DifficultyTier : -1;
	const bool bBossWritten = Character && Character->DefeatedCataclysmBosses.Contains(AgainstName);

	AddInfo(FString::Printf(
		TEXT("WIN WRITTEN: cleared %s; the record before %d; the character now %d; the last trigger '%s'; the record "
			 "built %d, on disk %d, %s's boss %s; the run on disk at tier %d facing %d, its line day %d tier %d "
			 "Cataclysm %d points %d; the won run's day %d"),
		bCleared ? TEXT("yes") : TEXT("NO"), WrittenBefore, Scene.State->GetDifficultyTier(),
		UCataclysmSaveTriggers::NameOf(LastTrigger), BuiltTier, WrittenAfter, *AgainstName.ToString(),
		bBossWritten ? TEXT("written") : TEXT("NOT written"), Run ? Run->DifficultyTier : -1,
		Run ? Run->ActiveCataclysms.Num() : -1, Run ? Run->LastRunWonDay : -1, Run ? Run->LastRunWonTier : -1,
		Run ? static_cast<int32>(Run->LastRunWonCataclysm) : -1, Run ? Run->LastRunWonPassivePoints : -1,
		WonRun ? WonRun->WonDay : -1));

	TestEqual(TEXT("CONTROL: the character record held tier 3 before the win"), WrittenBefore, 3);
	TestTrue(TEXT("the boss dungeon was cleared"), bCleared);
	TestEqual(TEXT("the character is a tier higher"), Scene.State->GetDifficultyTier(), 4);
	TestEqual(TEXT("the win's write was the last trigger"), static_cast<int32>(LastTrigger),
			  static_cast<int32>(ECataclysmSaveTrigger::RunWon));
	TestEqual(TEXT("the character record the win built holds the raised tier"), BuiltTier, 4);
	TestEqual(TEXT("and the one on disk"), WrittenAfter, 4);
	TestTrue(TEXT("with the boss the win paid for among those defeated"), bBossWritten);
	if (!TestNotNull(TEXT("the run record was written"), Run))
	{
		return false;
	}
	TestEqual(TEXT("the run record on disk is the next run's, at tier 4"), Run->DifficultyTier, 4);
	TestEqual(TEXT("facing four Cataclysms"), Run->ActiveCataclysms.Num(), 4);
	TestEqual(TEXT("carrying the win's day"), Run->LastRunWonDay, WonRun ? WonRun->WonDay : -1);
	TestEqual(TEXT("its tier"), Run->LastRunWonTier, 3);
	TestEqual(TEXT("its Cataclysm"), static_cast<int32>(Run->LastRunWonCataclysm), static_cast<int32>(Against));
	TestEqual(TEXT("and its points"), Run->LastRunWonPassivePoints, 10);
	return true;
}

// ---------------------------------------------------------------------------
// The first-kill passive points
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWinningTheRunPointsTest,
	"Cataclysm.WinningTheRun.TheFirstWinOverABossPaysTenAndASecondWinOverItPaysNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The ruling R4: a win at tier T pays the first-kill 10 passive points for the boss of the T-th Cataclysm in the
 * character's draw. A first win at tier 3 pays 10 for the third; a second win at tier 3, the tier set back by hand, pays
 * nothing, since that boss is already defeated. CONTROL: a win at tier 4 then pays 10 for the fourth, a boss not yet
 * defeated. Prints "WIN POINTS".
 */
bool FCataclysmWinningTheRunPointsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWinningTheRunTest;

	FWinScene Scene = MakeWinScene();
	if (!TestTrue(TEXT("set-up: the world was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ON_SCOPE_EXIT { Scene.ForgetTheSlots(); };

	const int32 PointsAtFirst = Scene.State->PassivePointsAvailable();
	const int32 BossesAtFirst = Scene.State->GetDefeatedCataclysmBosses().Num();

	// THE FIRST WIN, AT TIER 3.
	const int32 FirstBoss = BeginAtTierWithTheBoss(Scene, /* Tier */ 3, /* RunSeed */ 11);
	const bool bFirst = FirstBoss != INDEX_NONE && WalkInAndClear(Scene, FirstBoss);
	const int32 PointsAfterFirst = Scene.State->PassivePointsAvailable();
	const TArray<FName> BossesAfterFirst = Scene.State->GetDefeatedCataclysmBosses();
	const int32 PaidFirst = Scene.Instance->EmpireRun ? Scene.Instance->EmpireRun->LastRunWonPassivePoints : -1;

	// THE SECOND, AT TIER 3 AGAIN: the tier set back by hand, as nothing in play does, to win over the same boss.
	const int32 SecondBoss = BeginAtTierWithTheBoss(Scene, /* Tier */ 3, /* RunSeed */ 12);
	const bool bSecond = SecondBoss != INDEX_NONE && WalkInAndClear(Scene, SecondBoss);
	const int32 PointsAfterSecond = Scene.State->PassivePointsAvailable();
	const int32 BossesAfterSecond = Scene.State->GetDefeatedCataclysmBosses().Num();
	const int32 PaidSecond = Scene.Instance->EmpireRun ? Scene.Instance->EmpireRun->LastRunWonPassivePoints : -1;
	const FString SecondLine = UCataclysmEmpireMapLayout::LastRunWonLine(Scene.Instance->EmpireRun);

	// CONTROL: A WIN AT TIER 4, over a boss not yet defeated.
	const int32 ThirdBoss = BeginAtTierWithTheBoss(Scene, /* Tier */ 4, /* RunSeed */ 13);
	const bool bThird = ThirdBoss != INDEX_NONE && WalkInAndClear(Scene, ThirdBoss);
	const int32 PointsAfterThird = Scene.State->PassivePointsAvailable();
	const TArray<FName> BossesAfterThird = Scene.State->GetDefeatedCataclysmBosses();

	const FName Third = UCataclysmRoster::NameFor(DrawnAtTier(TheCharactersSeed, 3));
	const FName Fourth = UCataclysmRoster::NameFor(DrawnAtTier(TheCharactersSeed, 4));

	AddInfo(FString::Printf(
		TEXT("WIN POINTS: wins %s %s %s; points %d, %d, %d, %d; bosses %d, then [%s], %d, then [%s]; paid %d then %d; "
			 "the draw's third %s and fourth %s; the second line '%s'"),
		bFirst ? TEXT("yes") : TEXT("NO"), bSecond ? TEXT("yes") : TEXT("NO"), bThird ? TEXT("yes") : TEXT("NO"),
		PointsAtFirst, PointsAfterFirst, PointsAfterSecond, PointsAfterThird, BossesAtFirst,
		*FString::JoinBy(BossesAfterFirst, TEXT(", "), [](const FName& Name) { return Name.ToString(); }),
		BossesAfterSecond,
		*FString::JoinBy(BossesAfterThird, TEXT(", "), [](const FName& Name) { return Name.ToString(); }),
		PaidFirst, PaidSecond, *Third.ToString(), *Fourth.ToString(), *SecondLine));

	TestTrue(TEXT("set-up: all three wins were cleared"), bFirst && bSecond && bThird);
	TestEqual(TEXT("set-up: the character had defeated no boss"), BossesAtFirst, 0);
	TestEqual(TEXT("the first win pays ten passive points"), PointsAfterFirst - PointsAtFirst, 10);
	TestTrue(TEXT("for the boss of the third Cataclysm of the character's draw, and only it"),
			 BossesAfterFirst.Num() == 1 && BossesAfterFirst[0] == Third);
	TestEqual(TEXT("and the next run carries ten"), PaidFirst, 10);
	TestEqual(TEXT("a second win over the same boss pays nothing"), PointsAfterSecond - PointsAfterFirst, 0);
	TestEqual(TEXT("and records no second defeat"), BossesAfterSecond, 1);
	TestEqual(TEXT("and the next run carries nought"), PaidSecond, 0);
	TestTrue(TEXT("and its line says why"), SecondLine.Contains(TEXT("was defeated before, so no passive points.")));
	TestEqual(TEXT("CONTROL: a win at tier 4 pays ten again"), PointsAfterThird - PointsAfterSecond, 10);
	TestTrue(TEXT("CONTROL: for the boss of the fourth, beside the third"),
			 BossesAfterThird.Num() == 2 && BossesAfterThird.Contains(Third) && BossesAfterThird.Contains(Fourth));
	return true;
}

// ---------------------------------------------------------------------------
// The next run
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWinningTheRunNextRunTest,
	"Cataclysm.WinningTheRun.TheNextRunBeginsAtOnceATierHigherFacingTheWonRunsCataclysmsAndOneMore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The ruling R7: the next run begins at once, the same character, one tier higher, facing the won run's list and one
 * more (slice 6a's `BeginForCharacter`). A tier 3 win: the game instance holds a new run, at tier 4, on day 0, not won,
 * whose first three Cataclysms are the won run's in order and whose fourth is none of them; the game mode walks
 * nothing; the run's kills start at nought. `docs/Cataclysm_GDD_v2.md`, Ending a Run: "Only defeating the boss dungeon
 * adds a Cataclysm to the next run." Prints "WIN NEXT RUN".
 */
bool FCataclysmWinningTheRunNextRunTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWinningTheRunTest;

	FWinScene Scene = MakeWinScene();
	if (!TestTrue(TEXT("set-up: the world was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ON_SCOPE_EXIT { Scene.ForgetTheSlots(); };

	const int32 BossId = BeginAtTierWithTheBoss(Scene, /* Tier */ 3, /* RunSeed */ 11);
	UCataclysmEmpireRun* WonRun = Scene.Instance->EmpireRun;
	if (!TestTrue(TEXT("set-up: a tier 3 run was begun with its boss on the board"), BossId != INDEX_NONE && WonRun))
	{
		return false;
	}
	const TArray<ECataclysmType> WonList = WonRun->ActiveCataclysms;
	Scene.State->NoteKill();
	const int32 KillsBefore = Scene.State->GetRunKills();

	const bool bCleared = WalkInAndClear(Scene, BossId);
	const UCataclysmEmpireRun* Next = Scene.Instance->EmpireRun;
	const TArray<ECataclysmType> NextList = Next ? Next->ActiveCataclysms : TArray<ECataclysmType>();

	bool bFirstThreeKept = WonList.Num() == 3 && NextList.Num() == 4;
	for (int32 Index = 0; bFirstThreeKept && Index < 3; ++Index)
	{
		bFirstThreeKept = NextList[Index] == WonList[Index];
	}

	AddInfo(FString::Printf(
		TEXT("WIN NEXT RUN: cleared %s; the won run [%s] at tier %d, won %s on day %d; the next run %s, tier %d, day "
			 "%d, won %s, [%s]; the game mode walks %d; kills %d before, %d after"),
		bCleared ? TEXT("yes") : TEXT("NO"), *NamesOf(WonList), WonRun->DifficultyTier,
		WonRun->bWon ? TEXT("yes") : TEXT("NO"), WonRun->WonDay, Next != WonRun ? TEXT("new") : TEXT("THE SAME"),
		Next ? Next->DifficultyTier : -1, Next ? Next->Day() : -1, Next && Next->bWon ? TEXT("YES") : TEXT("no"),
		*NamesOf(NextList), Scene.Mode->EmpireDungeonId, KillsBefore, Scene.State->GetRunKills()));

	TestTrue(TEXT("the boss dungeon was cleared"), bCleared);
	TestTrue(TEXT("the won run is marked won"), WonRun->bWon);
	if (!TestNotNull(TEXT("the game instance holds a run"), Next))
	{
		return false;
	}
	TestTrue(TEXT("and it is a new run, begun at once"), Next != WonRun);
	TestEqual(TEXT("one tier higher"), Next->DifficultyTier, 4);
	TestEqual(TEXT("on its first day"), Next->Day(), 0);
	TestFalse(TEXT("not won"), Next->bWon);
	TestEqual(TEXT("facing four Cataclysms"), NextList.Num(), 4);
	TestTrue(TEXT("the first three the won run's, by name and in order"), bFirstThreeKept);
	TestTrue(TEXT("and the fourth none of them"), NextList.Num() == 4 && !WonList.Contains(NextList[3]));
	TestTrue(TEXT("which is the character's draw at tier 4"),
			 NextList == UCataclysmRoster::ActiveFor(TheCharactersSeed, 4));
	TestEqual(TEXT("the game mode walks no dungeon"), Scene.Mode->EmpireDungeonId, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("CONTROL: the character had a kill this run"), KillsBefore, 1);
	TestEqual(TEXT("and the next run's kills start at nought"), Scene.State->GetRunKills(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWinningTheRunTierEightTest,
	"Cataclysm.WinningTheRun.AWinAtTierEightStaysAtTierEightAndFacesTheSameEight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The top of the ladder: "The whole climb is eight campaigns, one at each difficulty tier" (`docs/Cataclysm_GDD_v2.md`,
 * section XII), and "In every one of them the same character plays the next run" (section II, Ending a Run). A win at
 * tier 8 pays for the eighth Cataclysm's boss; the tier stays at 8 (`RaiseDifficultyTierForAWin` holds it there, issue
 * #1472); the next run faces the same eight, in the same order; and its line says so. Prints "WIN AT EIGHT".
 */
bool FCataclysmWinningTheRunTierEightTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWinningTheRunTest;

	FWinScene Scene = MakeWinScene();
	if (!TestTrue(TEXT("set-up: the world was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ON_SCOPE_EXIT { Scene.ForgetTheSlots(); };

	const int32 BossId = BeginAtTierWithTheBoss(Scene, /* Tier */ 8, /* RunSeed */ 11);
	const UCataclysmEmpireRun* WonRun = Scene.Instance->EmpireRun;
	if (!TestTrue(TEXT("set-up: a tier 8 run was begun with its boss on the board"), BossId != INDEX_NONE && WonRun))
	{
		return false;
	}
	const TArray<ECataclysmType> WonList = WonRun->ActiveCataclysms;

	const bool bCleared = WalkInAndClear(Scene, BossId);
	const UCataclysmEmpireRun* Next = Scene.Instance->EmpireRun;
	const FName Eighth = UCataclysmRoster::NameFor(DrawnAtTier(TheCharactersSeed, 8));
	const FString Line = UCataclysmEmpireMapLayout::LastRunWonLine(Next);

	AddInfo(FString::Printf(
		TEXT("WIN AT EIGHT: cleared %s; the won run [%s]; the character at tier %d; the next run %s at tier %d [%s]; "
			 "bosses [%s], the eighth %s; the line '%s'"),
		bCleared ? TEXT("yes") : TEXT("NO"), *NamesOf(WonList), Scene.State->GetDifficultyTier(),
		Next != WonRun ? TEXT("new") : TEXT("THE SAME"), Next ? Next->DifficultyTier : -1,
		Next ? *NamesOf(Next->ActiveCataclysms) : TEXT(""),
		*FString::JoinBy(Scene.State->GetDefeatedCataclysmBosses(), TEXT(", "),
						 [](const FName& Name) { return Name.ToString(); }),
		*Eighth.ToString(), *Line));

	TestTrue(TEXT("the boss dungeon was cleared"), bCleared);
	TestEqual(TEXT("set-up: the tier 8 run faced all eight"), WonList.Num(), 8);
	TestEqual(TEXT("the character stays at tier 8"), Scene.State->GetDifficultyTier(), 8);
	if (!TestNotNull(TEXT("a next run was begun"), Next))
	{
		return false;
	}
	TestTrue(TEXT("a new one"), Next != WonRun);
	TestEqual(TEXT("at tier 8"), Next->DifficultyTier, 8);
	TestTrue(TEXT("facing the same eight, in the same order"), Next->ActiveCataclysms == WonList);
	TestTrue(TEXT("the win paid for the eighth Cataclysm's boss"),
			 Scene.State->GetDefeatedCataclysmBosses().Contains(Eighth));
	TestEqual(TEXT("ten points"), Next->LastRunWonPassivePoints, 10);
	TestTrue(TEXT("and the line says the run faces the same eight"),
			 Line.EndsWith(TEXT("This run faces the same 8 Cataclysms: tier 8 is the highest.")));
	return true;
}

// ---------------------------------------------------------------------------
// The owed days, and a walk left uncleared
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWinningTheRunOwedDaysTest,
	"Cataclysm.WinningTheRun.AWinRunsNoneOfTheWalksOwedDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The ruling R6: "the walk's owed days are NOT run after a win". On a tier 3 run on day 1, an ordinary dungeon is walked
 * first, as the CONTROL: entering owes a day, three more floors' time owes three, and clearing it runs all four on the
 * way out, so the run is on day 5. Then the boss is walked the same way, owing four: clearing it wins on day 9, the day
 * the walk had reached, and the won run stays on day 5 and owes nothing. Prints "WIN OWED DAYS".
 */
bool FCataclysmWinningTheRunOwedDaysTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWinningTheRunTest;

	FWinScene Scene = MakeWinScene();
	if (!TestTrue(TEXT("set-up: the world was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ON_SCOPE_EXIT { Scene.ForgetTheSlots(); };

	const int32 BossId = BeginAtTierWithTheBoss(Scene, /* Tier */ 3, /* RunSeed */ 11);
	UCataclysmEmpireRun* WonRun = Scene.Instance->EmpireRun;
	const int32 OrdinaryId = WonRun ? PlaceAnOrdinaryOneByHand(*WonRun) : INDEX_NONE;
	if (!TestTrue(TEXT("set-up: the boss and an ordinary dungeon stand on a tier 3 run"),
				  BossId != INDEX_NONE && OrdinaryId != INDEX_NONE)
		|| !TestEqual(TEXT("set-up: on day 1"), WonRun->Day(), 1))
	{
		return false;
	}

	// THE CONTROL: AN ORDINARY DUNGEON, WALKED AND CLEARED.
	const bool bEnteredOrdinary = Scene.Mode->EnterEmpireDungeon(OrdinaryId);
	WonRun->SpendFloorTime(3.0f);
	const int32 OrdinaryOwed = WonRun->DaysOwed();
	const bool bClearedOrdinary = Scene.Mode->ClearEmpireDungeon();
	const int32 DayAfterTheOrdinary = WonRun->Day();
	const bool bTheOrdinaryWon = WonRun->bWon || Scene.Instance->EmpireRun != WonRun;

	// THE BOSS, WALKED THE SAME WAY.
	const bool bEnteredBoss = Scene.Mode->EnterEmpireDungeon(BossId);
	WonRun->SpendFloorTime(3.0f);
	const int32 BossOwed = WonRun->DaysOwed();
	const bool bClearedBoss = Scene.Mode->ClearEmpireDungeon();

	AddInfo(FString::Printf(
		TEXT("WIN OWED DAYS: the ordinary dungeon entered %s, owed %d, cleared %s, the run then on day %d; the boss "
			 "entered %s, owed %d, cleared %s; the won run won %s on day %d, on day %d owing %d; a new run %s"),
		bEnteredOrdinary ? TEXT("yes") : TEXT("NO"), OrdinaryOwed, bClearedOrdinary ? TEXT("yes") : TEXT("NO"),
		DayAfterTheOrdinary, bEnteredBoss ? TEXT("yes") : TEXT("NO"), BossOwed,
		bClearedBoss ? TEXT("yes") : TEXT("NO"), WonRun->bWon ? TEXT("yes") : TEXT("NO"), WonRun->WonDay,
		WonRun->Day(), WonRun->DaysOwed(), Scene.Instance->EmpireRun != WonRun ? TEXT("began") : TEXT("DID NOT BEGIN")));

	TestTrue(TEXT("set-up: the ordinary dungeon was walked and cleared"), bEnteredOrdinary && bClearedOrdinary);
	TestEqual(TEXT("CONTROL: its walk owed four days"), OrdinaryOwed, 4);
	TestEqual(TEXT("CONTROL: and clearing it ran all four"), DayAfterTheOrdinary, 5);
	TestFalse(TEXT("CONTROL: and won nothing: the run is not won and was not replaced"), bTheOrdinaryWon);
	TestTrue(TEXT("set-up: the boss was walked and cleared"), bEnteredBoss && bClearedBoss);
	TestEqual(TEXT("set-up: its walk owed four days"), BossOwed, 4);
	TestTrue(TEXT("clearing it won the run"), WonRun->bWon);
	TestEqual(TEXT("on day 9, the day the walk had reached"), WonRun->WonDay, 9);
	TestEqual(TEXT("and none of the walk's four days ran: the won run is still on day 5"), WonRun->Day(), 5);
	TestEqual(TEXT("and owes none"), WonRun->DaysOwed(), 0);
	TestTrue(TEXT("and the next run began"), Scene.Instance->EmpireRun != WonRun);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWinningTheRunUnclearedTest,
	"Cataclysm.WinningTheRun.LeavingTheBossDungeonUnclearedWinsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Walking into the boss and leaving without clearing it wins nothing: the run is the same run, not won, the boss still
 * stands, the walk's days run as any walk's do, the character's tier and bosses are as they were, and no win line
 * appears. CONTROL: the same boss, walked again and cleared, raises the tier, so the scene can win. Prints "WIN NOT
 * CLEARED".
 */
bool FCataclysmWinningTheRunUnclearedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWinningTheRunTest;

	FWinScene Scene = MakeWinScene();
	if (!TestTrue(TEXT("set-up: the world was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ON_SCOPE_EXIT { Scene.ForgetTheSlots(); };

	const int32 BossId = BeginAtTierWithTheBoss(Scene, /* Tier */ 3, /* RunSeed */ 11);
	UCataclysmEmpireRun* Run = Scene.Instance->EmpireRun;
	if (!TestTrue(TEXT("set-up: a tier 3 run was begun with its boss on the board"), BossId != INDEX_NONE && Run)
		|| !TestEqual(TEXT("set-up: on day 1"), Run->Day(), 1))
	{
		return false;
	}

	const bool bEntered = Scene.Mode->EnterEmpireDungeon(BossId);
	Run->SpendFloorTime(2.0f);
	const int32 Owed = Run->DaysOwed();
	Scene.Mode->LeaveEmpireDungeon();

	const bool bSameRun = Scene.Instance->EmpireRun == Run;
	const bool bWon = Run->bWon;
	const bool bStillStands = Run->FindDungeon(BossId) != nullptr;
	const int32 DayAfter = Run->Day();
	const int32 TierAfter = Scene.State->GetDifficultyTier();
	const int32 BossesAfter = Scene.State->GetDefeatedCataclysmBosses().Num();
	const FString Line = UCataclysmEmpireMapLayout::LastRunWonLine(Run);
	const bool bWinWritten = Scene.Writer->LastTrigger() == ECataclysmSaveTrigger::RunWon;

	// CONTROL: WALKED AGAIN AND CLEARED.
	const bool bClearedAfter = WalkInAndClear(Scene, BossId);

	AddInfo(FString::Printf(
		TEXT("WIN NOT CLEARED: entered %s, owed %d; after leaving the same run %s, won %s, the boss %s, day %d, tier %d, "
			 "bosses %d, line '%s', a win written %s; CONTROL cleared %s, tier %d"),
		bEntered ? TEXT("yes") : TEXT("NO"), Owed, bSameRun ? TEXT("yes") : TEXT("NO"), bWon ? TEXT("YES") : TEXT("no"),
		bStillStands ? TEXT("stands") : TEXT("IS GONE"), DayAfter, TierAfter, BossesAfter, *Line,
		bWinWritten ? TEXT("YES") : TEXT("no"), bClearedAfter ? TEXT("yes") : TEXT("NO"),
		Scene.State->GetDifficultyTier()));

	TestTrue(TEXT("set-up: the boss was entered"), bEntered);
	TestEqual(TEXT("set-up: the walk owed three days"), Owed, 3);
	TestTrue(TEXT("leaving uncleared keeps the same run"), bSameRun);
	TestFalse(TEXT("which is not won"), bWon);
	TestTrue(TEXT("with the boss still standing"), bStillStands);
	TestEqual(TEXT("and the walk's three days run as any walk's do"), DayAfter, 4);
	TestEqual(TEXT("the character's tier is as it was"), TierAfter, 3);
	TestEqual(TEXT("and it has defeated no boss"), BossesAfter, 0);
	TestTrue(TEXT("no win line appears"), Line.IsEmpty());
	TestFalse(TEXT("and no win was written"), bWinWritten);
	TestTrue(TEXT("CONTROL: the same boss walked again is cleared"), bClearedAfter);
	TestEqual(TEXT("CONTROL: and that raises the tier"), Scene.State->GetDifficultyTier(), 4);
	return true;
}

// ---------------------------------------------------------------------------
// The win line
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWinningTheRunLineTest,
	"Cataclysm.WinningTheRun.TheNewRunsMapCarriesTheWinLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The ruling R7's "win line", in the words `UCataclysmEmpireMapLayout::LastRunWonLine` gives it, written out here in
 * full: a first win, "+10 passive points"; a boss defeated before, which says so rather than "+0"; and tier 8, where the
 * run faces the same eight. A fresh run, a run not begun and no run say nothing. Then a real win at tier 3 carries its
 * line onto the next run's map. Prints "WIN LINE".
 */
bool FCataclysmWinningTheRunLineTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmWinningTheRunTest;

	UCataclysmEmpireRun* AtFour = NewObject<UCataclysmEmpireRun>();
	AtFour->Begin(/* Seed */ 5, ECataclysmSurgeMode::Static, /* LethalityRung */ 0, /* DifficultyTier */ 4);
	const FString Fresh = UCataclysmEmpireMapLayout::LastRunWonLine(AtFour);
	AtFour->NoteLastRunWon(380, 3, ECataclysmType::Death, 10);
	const FString Paid = UCataclysmEmpireMapLayout::LastRunWonLine(AtFour);
	AtFour->NoteLastRunWon(380, 3, ECataclysmType::Death, 0);
	const FString Unpaid = UCataclysmEmpireMapLayout::LastRunWonLine(AtFour);

	UCataclysmEmpireRun* AtEight = NewObject<UCataclysmEmpireRun>();
	AtEight->Begin(/* Seed */ 5, ECataclysmSurgeMode::Static, /* LethalityRung */ 0, /* DifficultyTier */ 8);
	AtEight->NoteLastRunWon(512, 8, ECataclysmType::Void, 10);
	const FString Top = UCataclysmEmpireMapLayout::LastRunWonLine(AtEight);

	UCataclysmEmpireRun* NotBegun = NewObject<UCataclysmEmpireRun>();
	NotBegun->NoteLastRunWon(380, 3, ECataclysmType::Death, 10);
	const FString OfNotBegun = UCataclysmEmpireMapLayout::LastRunWonLine(NotBegun);
	const FString OfNothing = UCataclysmEmpireMapLayout::LastRunWonLine(nullptr);

	AddInfo(FString::Printf(TEXT("WIN LINE: fresh '%s'; paid '%s'; unpaid '%s'; at eight '%s'; not begun '%s'"),
							*Fresh, *Paid, *Unpaid, *Top, *OfNotBegun));

	TestTrue(TEXT("a fresh run carries no line"), Fresh.IsEmpty());
	TestEqual(TEXT("a first win's line"), Paid,
			  FString(TEXT("Run won on day 380 at tier 3 against Death. +10 passive points. This run faces 4 "
						   "Cataclysms.")));
	TestEqual(TEXT("a boss defeated before says so, and no points"), Unpaid,
			  FString(TEXT("Run won on day 380 at tier 3 against Death. Death's boss was defeated before, so no "
						   "passive points. This run faces 4 Cataclysms.")));
	TestEqual(TEXT("at tier 8 the run faces the same eight, and The Void keeps its article"), Top,
			  FString(TEXT("Run won on day 512 at tier 8 against The Void. +10 passive points. This run faces the "
						   "same 8 Cataclysms: tier 8 is the highest.")));
	TestTrue(TEXT("a run not begun says nothing"), OfNotBegun.IsEmpty());
	TestTrue(TEXT("and no run says nothing"), OfNothing.IsEmpty());

	// AND A REAL WIN CARRIES ITS LINE ONTO THE NEXT RUN'S MAP.
	FWinScene Scene = MakeWinScene();
	if (!TestTrue(TEXT("set-up: the world was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ON_SCOPE_EXIT { Scene.ForgetTheSlots(); };

	const int32 BossId = BeginAtTierWithTheBoss(Scene, /* Tier */ 3, /* RunSeed */ 11);
	const UCataclysmEmpireRun* WonRun = Scene.Instance->EmpireRun;
	const FString LineBefore = UCataclysmEmpireMapLayout::LastRunWonLine(WonRun);
	const bool bCleared = BossId != INDEX_NONE && WalkInAndClear(Scene, BossId);
	const FString Line = UCataclysmEmpireMapLayout::LastRunWonLine(Scene.Instance->EmpireRun);
	const ECataclysmType Against = DrawnAtTier(TheCharactersSeed, 3);
	const FString AgainstName = Against == ECataclysmType::Void
		? FString(TEXT("The Void")) : UCataclysmRoster::NameFor(Against).ToString();
	const FString Expected = FString::Printf(
		TEXT("Run won on day %d at tier 3 against %s. +10 passive points. This run faces 4 Cataclysms."),
		WonRun ? WonRun->WonDay : -1, *AgainstName);

	AddInfo(FString::Printf(TEXT("WIN LINE: before the win '%s'; after it '%s'"), *LineBefore, *Line));

	TestTrue(TEXT("set-up: the boss was cleared"), bCleared);
	TestTrue(TEXT("CONTROL: the run before the win carried no line"), LineBefore.IsEmpty());
	TestEqual(TEXT("the next run's map carries the win's line"), Line, Expected);
	return true;
}

// ---------------------------------------------------------------------------
// The save: run record version 5
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWinningTheRunSavedTest,
	"Cataclysm.WinningTheRun.AWinLineSurvivesTheRecordItsJsonAndARestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A tier 4 run carrying a win line -- day 381, tier 3, War, 10 points -- is gathered as the writer gathers it, written
 * to JSON, read back and put into a new run with `FCataclysmSaveApply::RunInto`: the four figures come back, and so does
 * the line. CONTROL: a run carrying none comes back with none. Prints "WIN SAVED".
 */
bool FCataclysmWinningTheRunSavedTest::RunTest(const FString& Parameters)
{
	const auto SavedAndRestored = [this](const UCataclysmEmpireRun& Live) -> UCataclysmEmpireRun*
	{
		UCataclysmRunSave* Record = NewObject<UCataclysmRunSave>();
		Record->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;
		FString Why;
		FCataclysmSaveGather::RunClockFrom(Live, *Record);
		FCataclysmSaveGather::CitiesFrom(Live, *Record);
		FCataclysmSaveGather::SurgeScheduleFrom(Live, *Record);
		FCataclysmSaveGather::ProgressFrom(Live, *Record);
		FCataclysmSaveGather::IdentityFrom(Live, *Record);
		FString Json;
		if (!FCataclysmSaveGather::DungeonsFrom(Live, *Record, Why) || !FCataclysmSaveStorage::ToJson(Record, Json, Why))
		{
			AddError(FString::Printf(TEXT("set-up: the run could not be written: %s"), *Why));
			return nullptr;
		}
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		const UCataclysmRunSave* Back = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			Json, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Why));
		UCataclysmEmpireRun* Restored = NewObject<UCataclysmEmpireRun>();
		if (Back == nullptr || !FCataclysmSaveApply::RunInto(*Back, *Restored, Why))
		{
			AddError(FString::Printf(TEXT("set-up: the run could not be read back and restored: %s"), *Why));
			return nullptr;
		}
		return Restored;
	};

	const auto Begun = []()
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(/* Seed */ 11, ECataclysmSurgeMode::Static, /* LethalityRung */ 0, /* DifficultyTier */ 4);
		Run->AdvanceDays(2);
		return Run;
	};

	UCataclysmEmpireRun* Live = Begun();
	Live->NoteLastRunWon(381, 3, ECataclysmType::War, 10);
	const FString LiveLine = UCataclysmEmpireMapLayout::LastRunWonLine(Live);

	UCataclysmEmpireRun* Restored = SavedAndRestored(*Live);
	UCataclysmEmpireRun* RestoredNone = SavedAndRestored(*Begun());
	if (!TestNotNull(TEXT("set-up: the run with a win line was restored"), Restored)
		|| !TestNotNull(TEXT("set-up: the control was restored"), RestoredNone))
	{
		return false;
	}
	const FString RestoredLine = UCataclysmEmpireMapLayout::LastRunWonLine(Restored);

	AddInfo(FString::Printf(
		TEXT("WIN SAVED: day %d tier %d Cataclysm %d points %d; the line '%s'; the control's day %d, line '%s'"),
		Restored->LastRunWonDay, Restored->LastRunWonTier, static_cast<int32>(Restored->LastRunWonCataclysm),
		Restored->LastRunWonPassivePoints, *RestoredLine, RestoredNone->LastRunWonDay,
		*UCataclysmEmpireMapLayout::LastRunWonLine(RestoredNone)));

	TestEqual(TEXT("the day comes back"), Restored->LastRunWonDay, 381);
	TestEqual(TEXT("the tier"), Restored->LastRunWonTier, 3);
	TestEqual(TEXT("the Cataclysm"), static_cast<int32>(Restored->LastRunWonCataclysm),
			  static_cast<int32>(ECataclysmType::War));
	TestEqual(TEXT("the points"), Restored->LastRunWonPassivePoints, 10);
	TestFalse(TEXT("set-up: the live line is not empty"), LiveLine.IsEmpty());
	TestEqual(TEXT("and the restored run shows the same line"), RestoredLine, LiveLine);
	TestEqual(TEXT("CONTROL: a run carrying no line comes back with none"), RestoredNone->LastRunWonDay, 0);
	TestTrue(TEXT("CONTROL: and shows none"), UCataclysmEmpireMapLayout::LastRunWonLine(RestoredNone).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWinningTheRunMigrationTest,
	"Cataclysm.WinningTheRun.ARunFileFromBeforeTheWinLineMigratesWithNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Run_v4.json`, written before a run carried the win of the run before it, through the chain: `Migrate_4_to_5` writes
 * the four fields with their defaults, one assertion each; the record arrives at the current version with no win line;
 * and every other field is kept. Prints "WIN MIGRATED".
 */
bool FCataclysmWinningTheRunMigrationTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v4.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	const TSharedPtr<FJsonObject> Stepped = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Run_v4.json is a JSON object"), Stepped.IsValid())
		|| !TestFalse(TEXT("set-up: and holds no win line"), Stepped->HasField(TEXT("LastRunWonDay"))))
	{
		return false;
	}
	FString StepMessage;
	const ECataclysmSaveMigrationResult Stepping = FCataclysmSaveMigration::Migrate(
		GetDefault<UCataclysmRunSave>()->MigrationSteps(), UCataclysmRunSave::SchemaVersionNow, Stepped.ToSharedRef(),
		StepMessage);

	double Day = -1.0;
	double Tier = -1.0;
	FString Against;
	double Points = -1.0;
	const bool bWroteDay = Stepped->TryGetNumberField(TEXT("LastRunWonDay"), Day) && Day == 0.0;
	const bool bWroteTier = Stepped->TryGetNumberField(TEXT("LastRunWonTier"), Tier) && Tier == 0.0;
	const bool bWroteCataclysm = Stepped->TryGetStringField(TEXT("LastRunWonCataclysm"), Against)
		&& Against == TEXT("None");
	const bool bWrotePoints = Stepped->TryGetNumberField(TEXT("LastRunWonPassivePoints"), Points) && Points == 0.0;

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Run_v4.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	// AND NOTHING ELSE MOVED: the version 4 file with the version and the four keys, compared with what the record
	// writes back.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmRunSave::SchemaVersionNow);
	Expected->SetNumberField(TEXT("LastRunWonDay"), 0);
	Expected->SetNumberField(TEXT("LastRunWonTier"), 0);
	Expected->SetStringField(TEXT("LastRunWonCataclysm"), TEXT("None"));
	Expected->SetNumberField(TEXT("LastRunWonPassivePoints"), 0);
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	AddInfo(FString::Printf(TEXT("WIN MIGRATED: the step %d (%s); wrote day %s, tier %s, Cataclysm %s, points %s; the "
								 "record at version %d, line day %d; the rest %s"),
							static_cast<int32>(Stepping), *StepMessage, bWroteDay ? TEXT("yes") : TEXT("NO"),
							bWroteTier ? TEXT("yes") : TEXT("NO"), bWroteCataclysm ? TEXT("yes") : TEXT("NO"),
							bWrotePoints ? TEXT("yes") : TEXT("NO"), Read->SchemaVersion, Read->LastRunWonDay,
							bSame ? TEXT("kept") : *Where));

	TestEqual(TEXT("the chain migrates the version 4 file"), static_cast<int32>(Stepping),
		static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("the step writes day 0"), bWroteDay);
	TestTrue(TEXT("the step writes tier 0"), bWroteTier);
	TestTrue(TEXT("the step writes no Cataclysm"), bWroteCataclysm);
	TestTrue(TEXT("the step writes no points"), bWrotePoints);
	TestEqual(TEXT("it arrives at the current version"), Read->SchemaVersion, UCataclysmRunSave::SchemaVersionNow);
	TestEqual(TEXT("with no win line"), Read->LastRunWonDay, 0);
	TestTrue(TEXT("and every other field is kept"), bSame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWinningTheRunCommittedFileTest,
	"Cataclysm.WinningTheRun.TheCommittedRunFileReadsIntoTheWinLineItStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Run_v5.json` reads into the four values it states, none of them a default, so each is read and not re-defaulted.
 * Prints "WIN FILE".
 */
bool FCataclysmWinningTheRunCommittedFileTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v5.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Run_v5.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	AddInfo(FString::Printf(TEXT("WIN FILE: version %d; the run before won on day %d at tier %d against %d for %d"),
							Read->SchemaVersion, Read->LastRunWonDay, Read->LastRunWonTier,
							static_cast<int32>(Read->LastRunWonCataclysm), Read->LastRunWonPassivePoints));

	TestEqual(TEXT("it says it is version 5"), Read->SchemaVersion, 5);
	TestEqual(TEXT("the run before it was won on day 381"), Read->LastRunWonDay, 381);
	TestEqual(TEXT("at tier 3"), Read->LastRunWonTier, 3);
	TestEqual(TEXT("against War"), static_cast<int32>(Read->LastRunWonCataclysm),
			  static_cast<int32>(ECataclysmType::War));
	TestEqual(TEXT("for 10 passive points"), Read->LastRunWonPassivePoints, 10);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
