// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"

/**
 * A death in a dungeon of the empire says what it cost. Issue #41, ruled by the coordinating session on 2026-11-02.
 *
 * `ACataclysmDungeonGameMode::EndTheDungeonForADeath` worked out every figure of a death and only logged them. The run
 * now keeps them (`UCataclysmEmpireRun::NoteDeath`), `UCataclysmEmpireMapLayout::LastDeathLine` turns them into words,
 * the empire screen shows that line while the death is the latest thing it has a line for (`LatestEventLine`), the
 * player reads it as a notice for `ACataclysmPlayerCharacter::DeathCostNoticeSeconds` after standing back up, and the
 * run record keeps it from version 3.
 *
 * THE DRAWING IS NOT COVERED. The automation command passes `-nullrhi`, so `ACataclysmHUD::DrawHUD` never runs and the
 * empire screen's labels are null; every judgement it draws is a function tested here.
 */
namespace CataclysmDeathCostTest
{
	/** A world, a possessed player, a begun dungeon game mode, and an empire run with nothing standing on it. */
	struct FDeathCostScene
	{
		UWorld* World = nullptr;
		ACataclysmDungeonGameMode* Mode = nullptr;
		UCataclysmEmpireRun* Run = nullptr;
		ACataclysmPlayerCharacter* Player = nullptr;

		bool IsUsable() const
		{
			return World && Mode && Run && Run->Map && Run->Clock && Player;
		}
	};

	/**
	 * The scene `CataclysmDungeonCostsDaysTests.cpp` builds for its deaths, in a copy of this file's own: the run is
	 * begun at Standard, a day is spent to land the opening wave, and both lists are emptied, so nothing is on the map
	 * but what a test places and the next surge is far off.
	 */
	FDeathCostScene MakeDeathCostScene()
	{
		FDeathCostScene Out;

		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}

		ACataclysmPlayerState* PlayerState = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Player = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState || !Controller || !Out.Player)
		{
			return Out;
		}
		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Out.Player);

		Out.Mode = Out.World->SpawnActor<ACataclysmDungeonGameMode>();
		if (!Out.Mode)
		{
			return Out;
		}
		Out.Mode->StartPlay();

		Out.Run = NewObject<UCataclysmEmpireRun>();
		Out.Run->Begin(1, ECataclysmSurgeMode::Static, 0);
		Out.Run->AdvanceDay();
		Out.Run->Dungeons.Empty();
		if (Out.Run->Clock)
		{
			Out.Run->Clock->Timers.Empty();
		}
		Out.Mode->SetEmpireRunForTests(Out.Run);

		return Out;
	}

	/** The first Outpost of the map that has not fallen, or `INDEX_NONE`. */
	int32 AStandingOutpost(const UCataclysmEmpireRun& Run)
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

	/**
	 * One ordinary dungeon, no sub-type, twenty floors, forty days on its timer, 100 defence and 50 population of
	 * damage, put on a city by hand with its timer, as `PlaceByHand` in `CataclysmDungeonCostsDaysTests.cpp` does.
	 */
	int32 PlaceAnOrdinaryDungeon(UCataclysmEmpireRun& Run, int32 CityId)
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
		Dungeon.ResolveDays = 40.0f;
		Dungeon.SpawnedDay = Run.Day();
		Dungeon.DefenceDamage = 100.0f;
		Dungeon.PopulationDamage = 50.0f;

		Run.NextDungeonId = Dungeon.DungeonId + 1;
		Run.Dungeons.Add(Dungeon);

		if (!Run.Clock->AddDungeon(Dungeon.DungeonId, Dungeon.Floors)
			|| !Run.Clock->SetResolveDays(Dungeon.DungeonId, Dungeon.ResolveDays))
		{
			return INDEX_NONE;
		}
		return Dungeon.DungeonId;
	}

	/**
	 * Enters the dungeon and walks down two floors, so the walk owes three days, then clears the floor and stands the
	 * player at its entrance. Each step is asserted as set-up.
	 */
	bool WalkThreeFloorsIn(FAutomationTestBase& Test, const FDeathCostScene& Scene, int32 DungeonId)
	{
		if (!Test.TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
			|| !Test.TestTrue(TEXT("set-up: the player went down to floor 2"), Scene.Mode->GoDownOneFloor())
			|| !Test.TestTrue(TEXT("set-up: and to floor 3"), Scene.Mode->GoDownOneFloor()))
		{
			return false;
		}
		Scene.Mode->ClearFloorEnemies();
		return Test.TestTrue(TEXT("set-up: the player stands at the floor's entrance"),
							 Scene.Mode->PlaceAtEntrance(Scene.Player))
			&& Test.TestEqual(TEXT("set-up: the walk owes three days"), Scene.Run->DaysOwed(), 3)
			&& Test.TestEqual(TEXT("set-up: the game mode is walking that dungeon"), Scene.Mode->EmpireDungeonId,
							  DungeonId);
	}

	/** Kills the player and stands it back up by hand, which is what the respawn timer calls. Asserted as set-up. */
	bool DieAndStandUp(FAutomationTestBase& Test, const FDeathCostScene& Scene)
	{
		UCataclysmSkillEffects::ReduceHealthDirectly(Scene.Player, Scene.Player, 1000000.0f);
		if (!Test.TestTrue(TEXT("set-up: the player died"), UCataclysmSkillEffects::IsDead(Scene.Player)))
		{
			return false;
		}
		Scene.Player->Revive();
		return Test.TestFalse(TEXT("set-up: the player stood back up"), UCataclysmSkillEffects::IsDead(Scene.Player));
	}

	/** A city's name as the map has it, or empty. */
	FString NameOfCity(const UCataclysmEmpireRun& Run, int32 CityId)
	{
		const FCataclysmCity* City = Run.Map->Find(CityId);
		return City != nullptr ? City->Name : FString();
	}
}

// ---------------------------------------------------------------------------
// The words
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathCostLineTest,
	"Cataclysm.DeathCost.TheLineSaysTheDayTheDungeonTheDaysAndTheCitiesThatFell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `LastDeathLine` for representative records: no city fell, one, two, three, one the map cannot find; a walk of one
 * day, of several, and of none; a name and no name. CONTROL: no run, a run not begun and a run with no death say
 * nothing. The sentences are written out here; only the cities' names are read from the map.
 */
bool FCataclysmDeathCostLineTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathCostTest;

	UCataclysmEmpireRun* NotBegun = NewObject<UCataclysmEmpireRun>();
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(1);
	const FString A = NameOfCity(*Run, 0);
	const FString B = NameOfCity(*Run, 1);
	const FString C = NameOfCity(*Run, 2);
	if (!TestFalse(TEXT("set-up: the map names its first three cities"), A.IsEmpty() || B.IsEmpty() || C.IsEmpty()))
	{
		return false;
	}

	TestEqual(TEXT("CONTROL: no run says nothing"), UCataclysmEmpireMapLayout::LastDeathLine(nullptr), FString());
	TestEqual(TEXT("CONTROL: a run not begun says nothing"), UCataclysmEmpireMapLayout::LastDeathLine(NotBegun),
			  FString());
	TestEqual(TEXT("CONTROL: a run with no death says nothing"), UCataclysmEmpireMapLayout::LastDeathLine(Run),
			  FString());

	Run->NoteDeath(42, TEXT("the Hollow dungeon on ") + A, 3, 5, TArray<int32>());
	TestEqual(TEXT("no city fell, a walk of three days"), UCataclysmEmpireMapLayout::LastDeathLine(Run),
		FString::Printf(TEXT("Day 42: you died in the Hollow dungeon on %s. The walk's 3 days ran, the death cost 5 "
							 "more days, and no city fell."), *A));

	Run->NoteDeath(7, TEXT("the dungeon on ") + A, 1, 1, TArray<int32>{0});
	TestEqual(TEXT("one city fell, a walk of one day, a death of one day"), UCataclysmEmpireMapLayout::LastDeathLine(Run),
		FString::Printf(TEXT("Day 7: you died in the dungeon on %s. The walk's 1 day ran, the death cost 1 more day, "
							 "and %s fell."), *A, *A));

	Run->NoteDeath(60, TEXT("the dungeon on ") + B, 12, 10, TArray<int32>{0, 1});
	TestEqual(TEXT("two cities fell"), UCataclysmEmpireMapLayout::LastDeathLine(Run),
		FString::Printf(TEXT("Day 60: you died in the dungeon on %s. The walk's 12 days ran, the death cost 10 more "
							 "days, and %s and %s fell."), *B, *A, *B));

	Run->NoteDeath(61, TEXT("the dungeon on ") + C, 2, 15, TArray<int32>{0, 1, 2});
	TestEqual(TEXT("three cities fell"), UCataclysmEmpireMapLayout::LastDeathLine(Run),
		FString::Printf(TEXT("Day 61: you died in the dungeon on %s. The walk's 2 days ran, the death cost 15 more "
							 "days, and %s, %s and %s fell."), *C, *A, *B, *C));

	Run->NoteDeath(9, TEXT("the dungeon on ") + A, 0, 5, TArray<int32>());
	TestEqual(TEXT("a walk that owed no whole day"), UCataclysmEmpireMapLayout::LastDeathLine(Run),
		FString::Printf(TEXT("Day 9: you died in the dungeon on %s. The walk owed no whole day, the death cost 5 days, "
							 "and no city fell."), *A));

	Run->NoteDeath(9, FString(), 2, 5, TArray<int32>());
	TestEqual(TEXT("a death with no name kept"), UCataclysmEmpireMapLayout::LastDeathLine(Run),
		FString(TEXT("Day 9: you died in a dungeon. The walk's 2 days ran, the death cost 5 more days, and no city "
					 "fell.")));

	Run->NoteDeath(9, TEXT("the dungeon on ") + A, 2, 5, TArray<int32>{999});
	TestEqual(TEXT("a city the map cannot find"), UCataclysmEmpireMapLayout::LastDeathLine(Run),
		FString::Printf(TEXT("Day 9: you died in the dungeon on %s. The walk's 2 days ran, the death cost 5 more days, "
							 "and a city fell."), *A));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathCostNameTest,
	"Cataclysm.DeathCost.ADungeonIsNamedByItsSubTypeItsKindAndItsCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmEmpireRun::NameOfDungeon`, which the death keeps because an ordinary dungeon is off the map by the time
 * the line is read. CONTROL: a number no dungeon has is named nothing.
 */
bool FCataclysmDeathCostNameTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathCostTest;

	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(1);
	Run->Dungeons.Empty();
	const FString A = NameOfCity(*Run, 0);

	const auto Put = [Run](int32 Id, ECataclysmDungeonType Type, ECataclysmDungeonSubType SubType, int32 CityId)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = Id;
		Dungeon.Type = Type;
		Dungeon.SubType = SubType;
		Dungeon.CityId = CityId;
		Run->Dungeons.Add(Dungeon);
	};
	Put(101, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, 0);
	Put(102, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Hollow, 0);
	Put(103, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::Horde, 0);
	Put(104, ECataclysmDungeonType::FallenCity, ECataclysmDungeonSubType::None, 0);
	Put(105, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::CowLevel, 999);

	TestEqual(TEXT("an ordinary dungeon"), Run->NameOfDungeon(101), FString::Printf(TEXT("the dungeon on %s"), *A));
	TestEqual(TEXT("a sub-type"), Run->NameOfDungeon(102), FString::Printf(TEXT("the Hollow dungeon on %s"), *A));
	TestEqual(TEXT("a sub-type and a kind"), Run->NameOfDungeon(103),
			  FString::Printf(TEXT("the Horde Quest dungeon on %s"), *A));
	TestEqual(TEXT("a Dungeon City"), Run->NameOfDungeon(104), FString::Printf(TEXT("the Fallen City dungeon on %s"), *A));
	TestEqual(TEXT("on a city the map cannot find"), Run->NameOfDungeon(105), FString(TEXT("the Cow Level dungeon")));
	TestEqual(TEXT("CONTROL: a number no dungeon has is named nothing"), Run->NameOfDungeon(106), FString());
	return true;
}

// ---------------------------------------------------------------------------
// A death in a real run keeps what ending the walk computed
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathCostFellsTest,
	"Cataclysm.DeathCost.ADeathThatFellsItsCityKeepsWhatEndingTheWalkComputed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * An ordinary dungeon on an Outpost with ten defence left, walked three floors in, and the player dies: the run keeps
 * the day the walk had reached, the dungeon's name, three walk days, Standard's five, and the Outpost as the one city
 * that fell. CONTROL: before the death it keeps none, and afterwards the day is the death's day and five, which is
 * what was charged. Prints "DEATH COST FELLS".
 */
bool FCataclysmDeathCostFellsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathCostTest;

	FDeathCostScene Scene = MakeDeathCostScene();
	if (!TestNotNull(TEXT("a test world was created"), Scene.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		return false;
	}

	const int32 CityId = AStandingOutpost(*Scene.Run);
	FCataclysmCity* Weakened = Scene.Run->Map->FindMutable(CityId);
	if (!TestNotNull(TEXT("set-up: an Outpost is on the map"), Weakened))
	{
		return false;
	}
	Weakened->Defence = 10.0f;
	const FString CityName = Weakened->Name;

	const int32 DungeonId = PlaceAnOrdinaryDungeon(*Scene.Run, CityId);
	if (!TestTrue(TEXT("set-up: a dungeon was placed on the Outpost"), DungeonId != INDEX_NONE)
		|| !WalkThreeFloorsIn(*this, Scene, DungeonId))
	{
		return false;
	}

	const int32 DayOfTheDeath = Scene.Run->Day() + Scene.Run->DaysOwed();
	if (!TestEqual(TEXT("CONTROL: before the death the run keeps no death"), Scene.Run->LastDeathDay, 0)
		|| !DieAndStandUp(*this, Scene))
	{
		return false;
	}

	const UCataclysmEmpireRun& Run = *Scene.Run;
	AddInfo(FString::Printf(TEXT("DEATH COST FELLS: day %d (expected %d), \"%s\", walk %d, cost %d, %d fell (first %d, "
								 "the Outpost %d); now day %d; line \"%s\""),
							Run.LastDeathDay, DayOfTheDeath, *Run.LastDeathDungeonName, Run.LastDeathWalkDays,
							Run.LastDeathDaysCost, Run.LastDeathFallenCityIds.Num(),
							Run.LastDeathFallenCityIds.Num() > 0 ? Run.LastDeathFallenCityIds[0] : -1, CityId, Run.Day(),
							*UCataclysmEmpireMapLayout::LastDeathLine(&Run)));

	TestEqual(TEXT("the death is kept on the day the walk had reached"), Run.LastDeathDay, DayOfTheDeath);
	TestEqual(TEXT("the dungeon is kept by its name"), Run.LastDeathDungeonName,
			  FString::Printf(TEXT("the dungeon on %s"), *CityName));
	TestEqual(TEXT("the walk's three days are kept"), Run.LastDeathWalkDays, 3);
	TestEqual(TEXT("Standard's five days are kept as the death's cost"), Run.LastDeathDaysCost, 5);
	TestTrue(TEXT("the Outpost its resolve felled is kept, and nothing else"),
			 Run.LastDeathFallenCityIds.Num() == 1 && Run.LastDeathFallenCityIds[0] == CityId);
	TestEqual(TEXT("CONTROL: the day is now the death's day and the five days charged"), Run.Day(), DayOfTheDeath + 5);
	TestEqual(TEXT("the line says so"), UCataclysmEmpireMapLayout::LastDeathLine(&Run),
		FString::Printf(TEXT("Day %d: you died in the dungeon on %s. The walk's 3 days ran, the death cost 5 more days, "
							 "and %s fell."), DayOfTheDeath, *CityName, *CityName));
	TestEqual(TEXT("and the empire screen shows it"), UCataclysmEmpireMapLayout::LatestEventLine(&Run),
			  UCataclysmEmpireMapLayout::LastDeathLine(&Run));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathCostFellsNothingTest,
	"Cataclysm.DeathCost.ADeathThatFellsNothingKeepsWhatEndingTheWalkComputedAndTheScreenShowsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The same walk on an Outpost at full defence: its resolve hits the city and the dungeon leaves, and no city falls.
 * The death's own hit sets the last hit on the day the run's clock stood at, before the walk's days ran, so it is older
 * than the death and the empire screen shows the death. Prints "DEATH COST NOTHING FELL".
 */
bool FCataclysmDeathCostFellsNothingTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathCostTest;

	FDeathCostScene Scene = MakeDeathCostScene();
	if (!TestNotNull(TEXT("a test world was created"), Scene.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		return false;
	}

	const int32 CityId = AStandingOutpost(*Scene.Run);
	const FString CityName = NameOfCity(*Scene.Run, CityId);
	const int32 DungeonId = PlaceAnOrdinaryDungeon(*Scene.Run, CityId);
	if (!TestTrue(TEXT("set-up: a dungeon was placed on an Outpost"), DungeonId != INDEX_NONE)
		|| !WalkThreeFloorsIn(*this, Scene, DungeonId))
	{
		return false;
	}

	const int32 DayOfTheDeath = Scene.Run->Day() + Scene.Run->DaysOwed();
	if (!DieAndStandUp(*this, Scene))
	{
		return false;
	}

	const UCataclysmEmpireRun& Run = *Scene.Run;
	AddInfo(FString::Printf(TEXT("DEATH COST NOTHING FELL: day %d (expected %d), \"%s\", walk %d, cost %d, %d fell; "
								 "last hit on day %d; line \"%s\""),
							Run.LastDeathDay, DayOfTheDeath, *Run.LastDeathDungeonName, Run.LastDeathWalkDays,
							Run.LastDeathDaysCost, Run.LastDeathFallenCityIds.Num(), Run.LastHitDay,
							*UCataclysmEmpireMapLayout::LastDeathLine(&Run)));

	TestEqual(TEXT("the death is kept on the day the walk had reached"), Run.LastDeathDay, DayOfTheDeath);
	TestEqual(TEXT("the dungeon is kept by its name"), Run.LastDeathDungeonName,
			  FString::Printf(TEXT("the dungeon on %s"), *CityName));
	TestEqual(TEXT("the walk's three days are kept"), Run.LastDeathWalkDays, 3);
	TestEqual(TEXT("Standard's five days are kept as the death's cost"), Run.LastDeathDaysCost, 5);
	TestEqual(TEXT("no city is kept as fallen"), Run.LastDeathFallenCityIds.Num(), 0);
	TestEqual(TEXT("the line says so"), UCataclysmEmpireMapLayout::LastDeathLine(&Run),
		FString::Printf(TEXT("Day %d: you died in the dungeon on %s. The walk's 3 days ran, the death cost 5 more days, "
							 "and no city fell."), DayOfTheDeath, *CityName));
	TestTrue(TEXT("CONTROL: the death's own hit set the last hit, on a day before the death's"),
			 Run.LastHitDay > 0 && Run.LastHitDay < Run.LastDeathDay);
	TestEqual(TEXT("so the empire screen shows the death and not the hit"),
			  UCataclysmEmpireMapLayout::LatestEventLine(&Run), UCataclysmEmpireMapLayout::LastDeathLine(&Run));
	return true;
}

// ---------------------------------------------------------------------------
// Which line the empire screen shows
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathCostScreenTest,
	"Cataclysm.DeathCost.TheEmpireScreenShowsTheDeathWhileItIsTheLatestAndTheHitOnceOneIsLater",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `LatestEventLine`: the hit line before any death (CONTROL); the death line when the death is later than the last
 * hit, and on the same day as it; the hit line once a hit is later; the move line once a fall's move is later; and the
 * death again on a move's own day.
 */
bool FCataclysmDeathCostScreenTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathCostTest;

	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(1);
	Run->LastHitDay = 10;
	Run->LastHitCityId = 0;
	Run->bLastHitWasAQuestTimer = false;

	TestEqual(TEXT("CONTROL: with no death the screen shows the hit"), UCataclysmEmpireMapLayout::LatestEventLine(Run),
			  UCataclysmEmpireMapLayout::LastHitLine(Run));

	Run->NoteDeath(12, TEXT("the dungeon on ") + NameOfCity(*Run, 1), 2, 5, TArray<int32>());
	const FString Death = UCataclysmEmpireMapLayout::LastDeathLine(Run);
	if (!TestFalse(TEXT("set-up: the death has a line"), Death.IsEmpty()))
	{
		return false;
	}
	TestEqual(TEXT("a death later than the last hit is shown"), UCataclysmEmpireMapLayout::LatestEventLine(Run), Death);

	Run->LastHitDay = 12;
	TestEqual(TEXT("on the same day as the last hit the death is shown"),
			  UCataclysmEmpireMapLayout::LatestEventLine(Run), Death);

	Run->LastHitDay = 13;
	const FString Hit = UCataclysmEmpireMapLayout::LastHitLine(Run);
	TestTrue(TEXT("set-up: the hit's line is the hit of day 13"), Hit.StartsWith(TEXT("Day 13: a dungeon hit ")));
	TestEqual(TEXT("a later hit replaces the death"), UCataclysmEmpireMapLayout::LatestEventLine(Run), Hit);

	Run->LastHitDay = 10;
	Run->LastMoveDay = 14;
	Run->LastMoveFromCityId = 0;
	Run->LastMoveToCityId = 1;
	const FString Move = UCataclysmEmpireMapLayout::LastHitLine(Run);
	TestTrue(TEXT("set-up: the hit line is now the move of day 14"), Move.StartsWith(TEXT("Day 14: a Quest dungeon moved")));
	TestEqual(TEXT("a later fall's move replaces the death"), UCataclysmEmpireMapLayout::LatestEventLine(Run), Move);

	Run->LastMoveDay = 12;
	TestEqual(TEXT("on a fall's move's own day the death is shown"), UCataclysmEmpireMapLayout::LatestEventLine(Run),
			  Death);
	return true;
}

// ---------------------------------------------------------------------------
// The notice on standing up
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathCostNoticeTest,
	"Cataclysm.DeathCost.TheNoticeIsUpWhenThePlayerStandsBackUpAndGoneAfterItsSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * After a death that ended a walk, the character holds a notice posted at the world's time it stood up, saying the
 * empire screen's death line, up from that moment until `DeathCostNoticeSeconds` after it and gone a quarter second
 * later. CONTROL: before the death there is none, and a death with no dungeon of the empire bound posts none. Prints
 * "DEATH NOTICE".
 */
bool FCataclysmDeathCostNoticeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathCostTest;

	const float Lasts = ACataclysmPlayerCharacter::DeathCostNoticeSeconds;

	// THE CONTROL: the player dies with no dungeon of the empire bound, which ends no walk.
	{
		FDeathCostScene Scene = MakeDeathCostScene();
		if (!TestNotNull(TEXT("a test world was created"), Scene.World))
		{
			return false;
		}
		ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		if (!TestTrue(TEXT("set-up: the control's scene was built"), Scene.IsUsable())
			|| !TestTrue(TEXT("set-up: floor 1 of the level was built"), Scene.Mode->GoToFloor(1)))
		{
			return false;
		}
		Scene.Mode->ClearFloorEnemies();
		if (!DieAndStandUp(*this, Scene))
		{
			return false;
		}
		TestEqual(TEXT("CONTROL: a death that ended no walk posts no notice"),
				  Scene.Player->DeathCostNoticePostedAtSeconds(), -1.0f);
		TestEqual(TEXT("CONTROL: and none is up"), Scene.Player->DeathCostNoticeAt(static_cast<float>(Scene.World->GetTimeSeconds())),
				  FString());
	}

	FDeathCostScene Scene = MakeDeathCostScene();
	if (!TestNotNull(TEXT("a second test world was created"), Scene.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		return false;
	}

	const int32 DungeonId = PlaceAnOrdinaryDungeon(*Scene.Run, AStandingOutpost(*Scene.Run));
	if (!TestTrue(TEXT("set-up: a dungeon was placed on an Outpost"), DungeonId != INDEX_NONE)
		|| !WalkThreeFloorsIn(*this, Scene, DungeonId))
	{
		return false;
	}

	const float Now = static_cast<float>(Scene.World->GetTimeSeconds());
	TestEqual(TEXT("CONTROL: before the death no notice is up"), Scene.Player->DeathCostNoticeAt(Now), FString());

	if (!DieAndStandUp(*this, Scene))
	{
		return false;
	}

	const float Posted = Scene.Player->DeathCostNoticePostedAtSeconds();
	const FString Line = UCataclysmEmpireMapLayout::LastDeathLine(Scene.Run);
	AddInfo(FString::Printf(TEXT("DEATH NOTICE: posted at %.2f (world %.2f), lasts %.2f; at the posting \"%s\"; at "
								 "%.2f \"%s\"; at %.2f \"%s\""),
							Posted, Scene.World->GetTimeSeconds(), Lasts, *Scene.Player->DeathCostNoticeAt(Posted),
							Posted + Lasts - 0.25f, *Scene.Player->DeathCostNoticeAt(Posted + Lasts - 0.25f),
							Posted + Lasts + 0.25f, *Scene.Player->DeathCostNoticeAt(Posted + Lasts + 0.25f)));

	if (!TestFalse(TEXT("set-up: the run has a death line"), Line.IsEmpty()))
	{
		return false;
	}
	TestEqual(TEXT("the notice is posted at the world's time the player stood up"), Posted,
			  static_cast<float>(Scene.World->GetTimeSeconds()));
	TestEqual(TEXT("it is up the moment the player stands, and says the death line"),
			  Scene.Player->DeathCostNoticeAt(Posted), Line);
	TestEqual(TEXT("it is still up a quarter second before its seconds are over"),
			  Scene.Player->DeathCostNoticeAt(Posted + Lasts - 0.25f), Line);
	TestEqual(TEXT("it is gone a quarter second after them"),
			  Scene.Player->DeathCostNoticeAt(Posted + Lasts + 0.25f), FString());
	return true;
}

// ---------------------------------------------------------------------------
// The save
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathCostSaveTest,
	"Cataclysm.DeathCost.ARunsLastDeathSurvivesTheRecordItsJsonAndARestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A run that has kept a death is gathered as the writer gathers it, written to JSON, read back and put into a new run
 * with `FCataclysmSaveApply::RunInto`: all five fields come back and the restored run says the same line. CONTROL: a
 * run with no death comes back with none and says nothing. Prints "DEATH SAVED".
 */
bool FCataclysmDeathCostSaveTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathCostTest;

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

	const auto Played = []()
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->ModifierPool = UCataclysmDungeonModifierTable::LoadPool();
		Run->Begin(4242, ECataclysmSurgeMode::Both, 2, 4);
		for (int32 Day = 0; Day < 10; ++Day)
		{
			Run->AdvanceDay();
		}
		return Run;
	};

	UCataclysmEmpireRun* Live = Played();
	const FString Name = TEXT("the Siege dungeon on ") + NameOfCity(*Live, 3);
	Live->NoteDeath(9, Name, 4, 15, TArray<int32>{3, 5});

	UCataclysmEmpireRun* Restored = SavedAndRestored(*Live);
	UCataclysmEmpireRun* Unharmed = Played();
	UCataclysmEmpireRun* RestoredUnharmed = SavedAndRestored(*Unharmed);
	if (!TestNotNull(TEXT("set-up: the run with a death was restored"), Restored)
		|| !TestNotNull(TEXT("set-up: the control was restored"), RestoredUnharmed))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("DEATH SAVED: day %d, \"%s\", walk %d, cost %d, %d fell; line \"%s\"; the control "
								 "day %d, line \"%s\""),
							Restored->LastDeathDay, *Restored->LastDeathDungeonName, Restored->LastDeathWalkDays,
							Restored->LastDeathDaysCost, Restored->LastDeathFallenCityIds.Num(),
							*UCataclysmEmpireMapLayout::LastDeathLine(Restored), RestoredUnharmed->LastDeathDay,
							*UCataclysmEmpireMapLayout::LastDeathLine(RestoredUnharmed)));

	TestEqual(TEXT("the day comes back"), Restored->LastDeathDay, 9);
	TestEqual(TEXT("the name comes back"), Restored->LastDeathDungeonName, Name);
	TestEqual(TEXT("the walk's days come back"), Restored->LastDeathWalkDays, 4);
	TestEqual(TEXT("the death's days come back"), Restored->LastDeathDaysCost, 15);
	TestTrue(TEXT("both fallen cities come back, in order"), Restored->LastDeathFallenCityIds.Num() == 2
		&& Restored->LastDeathFallenCityIds[0] == 3 && Restored->LastDeathFallenCityIds[1] == 5);
	TestEqual(TEXT("and the restored run says the same line"), UCataclysmEmpireMapLayout::LastDeathLine(Restored),
			  UCataclysmEmpireMapLayout::LastDeathLine(Live));
	TestEqual(TEXT("CONTROL: a run with no death comes back with none"), RestoredUnharmed->LastDeathDay, 0);
	TestEqual(TEXT("CONTROL: and says nothing"), UCataclysmEmpireMapLayout::LastDeathLine(RestoredUnharmed), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathCostMigrationTest,
	"Cataclysm.DeathCost.ARunFileFromBeforeTheDeathLineMigratesWithNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Run_v2.json`, written before the run kept a death, through the chain: `Migrate_2_to_3` writes the five fields with
 * their defaults, one assertion each; the record arrives at the current version with no death; and every other field
 * is kept. Prints "DEATH MIGRATED".
 */
bool FCataclysmDeathCostMigrationTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v2.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	const TSharedPtr<FJsonObject> Stepped = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Run_v2.json is a JSON object"), Stepped.IsValid())
		|| !TestFalse(TEXT("set-up: and holds no death"), Stepped->HasField(TEXT("LastDeathDay"))))
	{
		return false;
	}
	FString StepMessage;
	const ECataclysmSaveMigrationResult Stepping = FCataclysmSaveMigration::Migrate(
		GetDefault<UCataclysmRunSave>()->MigrationSteps(), UCataclysmRunSave::SchemaVersionNow, Stepped.ToSharedRef(),
		StepMessage);

	double Day = -1.0;
	FString Name = TEXT("unset");
	double Walk = -1.0;
	double Cost = -1.0;
	const TArray<TSharedPtr<FJsonValue>>* Fallen = nullptr;
	const bool bWroteDay = Stepped->TryGetNumberField(TEXT("LastDeathDay"), Day) && Day == 0.0;
	const bool bWroteName = Stepped->TryGetStringField(TEXT("LastDeathDungeonName"), Name) && Name.IsEmpty();
	const bool bWroteWalk = Stepped->TryGetNumberField(TEXT("LastDeathWalkDays"), Walk) && Walk == 0.0;
	const bool bWroteCost = Stepped->TryGetNumberField(TEXT("LastDeathDaysCost"), Cost) && Cost == 0.0;
	const bool bWroteFallen = Stepped->TryGetArrayField(TEXT("LastDeathFallenCityIds"), Fallen) && Fallen
		&& Fallen->IsEmpty();

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Run_v2.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	// AND NOTHING ELSE MOVED: the version 2 file with the version and the five keys, compared with what the record
	// writes back.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmRunSave::SchemaVersionNow);
	Expected->SetNumberField(TEXT("LastDeathDay"), 0);
	Expected->SetStringField(TEXT("LastDeathDungeonName"), FString());
	Expected->SetNumberField(TEXT("LastDeathWalkDays"), 0);
	Expected->SetNumberField(TEXT("LastDeathDaysCost"), 0);
	Expected->SetArrayField(TEXT("LastDeathFallenCityIds"), TArray<TSharedPtr<FJsonValue>>());
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	AddInfo(FString::Printf(TEXT("DEATH MIGRATED: the step %d (%s); wrote day %s, name %s, walk %s, cost %s, fallen %s; "
								 "the record at version %d, day %d; the rest %s"),
							static_cast<int32>(Stepping), *StepMessage, bWroteDay ? TEXT("yes") : TEXT("NO"),
							bWroteName ? TEXT("yes") : TEXT("NO"), bWroteWalk ? TEXT("yes") : TEXT("NO"),
							bWroteCost ? TEXT("yes") : TEXT("NO"), bWroteFallen ? TEXT("yes") : TEXT("NO"),
							Read->SchemaVersion, Read->LastDeathDay, bSame ? TEXT("kept") : *Where));

	TestEqual(TEXT("the chain migrates the version 2 file"), static_cast<int32>(Stepping),
		static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("the step writes day 0, no death"), bWroteDay);
	TestTrue(TEXT("the step writes no dungeon's name"), bWroteName);
	TestTrue(TEXT("the step writes no walk days"), bWroteWalk);
	TestTrue(TEXT("the step writes no death days"), bWroteCost);
	TestTrue(TEXT("the step writes no fallen city"), bWroteFallen);
	TestEqual(TEXT("it arrives at the current version"), Read->SchemaVersion, UCataclysmRunSave::SchemaVersionNow);
	TestEqual(TEXT("with no death"), Read->LastDeathDay, 0);
	TestTrue(TEXT("and every other field is kept"), bSame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathCostCommittedFileTest,
	"Cataclysm.DeathCost.TheCommittedRunFileReadsIntoTheDeathItStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Run_v3.json` reads into the five values it states, none of them a default, so each is read and not re-defaulted.
 * Prints "DEATH FILE".
 */
bool FCataclysmDeathCostCommittedFileTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v3.json"), Text, Reason))
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
		AddError(FString::Printf(TEXT("Run_v3.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	AddInfo(FString::Printf(TEXT("DEATH FILE: version %d; day %d, \"%s\", walk %d, cost %d, %d fell"),
							Read->SchemaVersion, Read->LastDeathDay, *Read->LastDeathDungeonName, Read->LastDeathWalkDays,
							Read->LastDeathDaysCost, Read->LastDeathFallenCityIds.Num()));

	TestEqual(TEXT("it says it is version 3"), Read->SchemaVersion, 3);
	TestEqual(TEXT("the death's day"), Read->LastDeathDay, 103);
	TestEqual(TEXT("the dungeon's name"), Read->LastDeathDungeonName, FString(TEXT("the Siege dungeon on Outpost (-3,0)")));
	TestEqual(TEXT("the walk's days"), Read->LastDeathWalkDays, 19);
	TestEqual(TEXT("the death's days"), Read->LastDeathDaysCost, 15);
	TestTrue(TEXT("the two fallen cities, in order"), Read->LastDeathFallenCityIds.Num() == 2
		&& Read->LastDeathFallenCityIds[0] == 17 && Read->LastDeathFallenCityIds[1] == 23);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
