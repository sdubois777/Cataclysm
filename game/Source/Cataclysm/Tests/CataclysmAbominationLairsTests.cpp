// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dom/JsonObject.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"

/**
 * Death's quest: Abomination lairs. Issue #51 slice B5.
 *
 * THE DESIGN, Dead Rising: "Cursed Abominations spawn as cities fall, growing stronger with each subsequent loss.
 * Players must defeat Abominations in their quest dungeon lairs to collect 5 Seeds of Undeath ... Each fallen city adds
 * a more powerful Abomination." Accepted by the owner on 2026-11-24; L1 to L9 ruled by the coordinating session on
 * 2026-11-25.
 *
 * THE STEP IS `UCataclysmSurgeScheduler::AbominationStep`, sized by the simulation slice A2. Every strength check here is
 * written as the step to a power, so it holds at any step; at a step of 1 the strength checks cannot tell a scaled boss
 * from an unscaled one, which the decisions entry says.
 *
 * EVERY RULE HAS A CONTROL. The model's half is `sim/tests/test_an_abomination_lair.py`, and
 * `tools/tests/test_abomination_lairs_port.py` holds the two to the same rules.
 */
namespace CataclysmAbominationLairsTest
{
	/** A run begun on `Seed` facing `Facing`, a day passed, its board and timers cleared. */
	UCataclysmEmpireRun* MakeEmptyRun(ECataclysmType Facing, int32 Seed = 1)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed, ECataclysmSurgeMode::Static);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		Run->ActiveCataclysms = { Facing };
		return Run;
	}

	/** A dungeon put down by hand on `City`, with its timer. */
	void Place(UCataclysmEmpireRun& Run, int32 DungeonId, ECataclysmDungeonType Type, ECataclysmType Sender,
			   const FCataclysmCity& City, float ResolveDays = 10000.0f)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.Type = Type;
		Dungeon.SubType = ECataclysmDungeonSubType::None;
		Dungeon.Cataclysm = Sender;
		Dungeon.CityId = City.CityId;
		Dungeon.CityTier = City.Tier;
		Dungeon.Floors = 4;
		Dungeon.ResolveDays = ResolveDays;
		Dungeon.SpawnedDay = Run.Day();
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
	}

	/** Fells `CityId` tomorrow with an ordinary dungeon `Sender` sent; the day's report. */
	FCataclysmDayReport FellTomorrow(UCataclysmEmpireRun& Run, int32 CityId, ECataclysmType Sender, int32 DungeonId)
	{
		FCataclysmCity* City = Run.Map->FindMutable(CityId);
		City->Defence = 1.0f;
		Place(Run, DungeonId, ECataclysmDungeonType::Basic, Sender, *City, 1.0f);
		Run.Dungeons.Last().DefenceDamage = 1000000.0f;
		return Run.AdvanceDay();
	}

	/** An ordinary dungeon that never runs out, on the Pillar, so no empty-board wave lands. */
	void HoldTheBoard(UCataclysmEmpireRun& Run)
	{
		Place(Run, 9999, ECataclysmDungeonType::Basic, ECataclysmType::None, *Run.Map->Find(Run.Map->PillarId));
	}

	/** An exposed city that is not the Pillar and has an exposed neighbour, or INDEX_NONE. */
	int32 AFrontCity(const UCataclysmEmpireRun& Run)
	{
		for (const int32 CityId : Run.Map->ExposedCities())
		{
			const FCataclysmCity* City = Run.Map->Find(CityId);
			for (const int32 Next : UCataclysmSurgeScheduler::AdjacentCities(*City))
			{
				if (Next != Run.Map->PillarId && Run.Map->IsExposed(Next))
				{
					return CityId;
				}
			}
		}
		return INDEX_NONE;
	}

	/** The lairs on the map. */
	TArray<const FCataclysmDungeon*> Lairs(const UCataclysmEmpireRun& Run)
	{
		TArray<const FCataclysmDungeon*> Found;
		for (const FCataclysmDungeon& Dungeon : Run.Dungeons)
		{
			if (Dungeon.IsALair())
			{
				Found.Add(&Dungeon);
			}
		}
		return Found;
	}

	/** A player possessed by a controller, as the dungeon game mode finds one. */
	struct FLairPlayer
	{
		explicit FLairPlayer(UWorld* World)
		{
			State = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (State && Controller && Character)
			{
				Controller->SetPlayerState(State);
				Controller->Possess(Character);
			}
		}
		bool IsUsable() const { return State && Controller && Character && Character->GetAbilitySystemComponent(); }
		ACataclysmPlayerState* State = nullptr;
		APlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
	};

	/**
	 * Enters a two-floor dungeon of this kind and sender, Horde, carrying no modifiers, counting `Fallen` cities, with
	 * only its boss on a floor, and walks to its last floor, a wave. Answers its last-floor Gatekeeper, or null.
	 */
	ACataclysmEnemyCharacter* TheLastWavesGatekeeper(FAutomationTestBase& Test, UWorld* World, ECataclysmType Sender,
													  int32 Fallen, ACataclysmDungeonGameMode*& OutMode)
	{
		OutMode = World->SpawnActor<ACataclysmDungeonGameMode>();
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		if (!OutMode || !Run)
		{
			return nullptr;
		}
		Run->Begin(1);
		Run->AdvanceDay();
		OutMode->SetEmpireRunForTests(Run);
		OutMode->EnemyScale = 0.0f; // The boss and nothing else on a floor.
		int32 Chosen = INDEX_NONE;
		for (int32 Index = 0; Index < Run->Dungeons.Num() && Chosen == INDEX_NONE; ++Index)
		{
			Chosen = Run->Dungeons[Index].SubType != ECataclysmDungeonSubType::CowLevel ? Index : INDEX_NONE;
		}
		if (!Test.TestTrue(TEXT("set-up: the run has a dungeon to walk"), Chosen != INDEX_NONE))
		{
			return nullptr;
		}
		FCataclysmDungeon& Dungeon = Run->Dungeons[Chosen];
		Dungeon.Floors = 2;
		Dungeon.Type = ECataclysmDungeonType::Quest;
		Dungeon.Cataclysm = Sender;
		Dungeon.SubType = ECataclysmDungeonSubType::Horde;
		Dungeon.CitiesFallenBefore = Fallen;
		Dungeon.Modifiers.Reset();
		Dungeon.ModifierScore = 0.0f;
		if (!Test.TestTrue(TEXT("set-up: the dungeon was entered"), OutMode->EnterEmpireDungeon(Dungeon.DungeonId))
			|| !Test.TestTrue(TEXT("set-up: the last floor was reached"), OutMode->GoToFloor(2)))
		{
			return nullptr;
		}
		for (ACataclysmEnemyCharacter* Enemy : OutMode->FloorEnemies)
		{
			if (IsValid(Enemy) && Enemy->IsA<ACataclysmGatekeeperCharacter>() && !UCataclysmSkillEffects::IsDead(Enemy))
			{
				return Enemy;
			}
		}
		return nullptr;
	}
}

// ---------------------------------------------------------------------------
// What a lair is, and its count
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAbominationLairsWhatTest,
	"Cataclysm.AbominationLairs.ALairIsAQuestDungeonDeathSentAndItsBossGrowsByTheStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAbominationLairsWhatTest::RunTest(const FString& Parameters)
{
	FCataclysmDungeon Dungeon;
	Dungeon.Type = ECataclysmDungeonType::Quest;
	Dungeon.Cataclysm = ECataclysmType::Death;
	Dungeon.CitiesFallenBefore = 3;
	TestTrue(TEXT("a Quest dungeon Death sent is a lair"), Dungeon.IsALair());
	TestEqual(TEXT("its boss grows by the step to the power of three"), Dungeon.AbominationMultiplier(),
			  FMath::Pow(UCataclysmSurgeScheduler::AbominationStep, 3.0f), 1e-5f);
	Dungeon.Cataclysm = ECataclysmType::War;
	TestFalse(TEXT("CONTROL: War's is not"), Dungeon.IsALair());
	TestEqual(TEXT("CONTROL: and its multiplier is one"), Dungeon.AbominationMultiplier(), 1.0f);
	Dungeon.Cataclysm = ECataclysmType::Death;
	Dungeon.Type = ECataclysmDungeonType::Basic;
	TestFalse(TEXT("CONTROL: nor an ordinary dungeon Death sent"), Dungeon.IsALair());
	TestEqual(TEXT("Death's Quest dungeons are Hordes"),
			  static_cast<int32>(UCataclysmSurgeScheduler::QuestSubTypeFor(ECataclysmType::Death)),
			  static_cast<int32>(ECataclysmDungeonSubType::Horde));
	return true;
}

/**
 * A surge's lair counts the cities fallen as it lands. Two cities felled by hand; a wave Death sends counts two on
 * every lair. CONTROL: a wave War sends counts nought on its Quest dungeons. Over 200 seeds, since Quest dungeons are
 * 12 in 100. Prints "SURGE LAIRS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAbominationLairsSurgeCountTest,
	"Cataclysm.AbominationLairs.ASurgesLairCountsTheCitiesFallenAsItLands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAbominationLairsSurgeCountTest::RunTest(const FString& Parameters)
{
	UCataclysmSurgeScheduler* Scheduler = NewObject<UCataclysmSurgeScheduler>();
	UCataclysmEmpireMap* Map = NewObject<UCataclysmEmpireMap>();
	Map->Build();
	const TArray<int32> Exposed = Map->ExposedCities();
	if (!TestTrue(TEXT("set-up: three exposed cities"), Exposed.Num() >= 3))
	{
		return false;
	}
	Map->Fall(Exposed[0]);
	Map->Fall(Exposed[1]);
	if (!TestEqual(TEXT("set-up: two fallen"), Map->FallenCityCount(), 2))
	{
		return false;
	}
	int32 Lairs = 0;
	int32 WrongCount = 0;
	int32 WarQuests = 0;
	int32 WarCounted = 0;
	for (int32 Seed = 1; Seed <= 200; ++Seed)
	{
		FRandomStream Death(Seed);
		FRandomStream War(Seed);
		for (const FCataclysmDungeon& Made : Scheduler->RollWave(*Map, 1, 0, Death, {}, {},
																 []() { return ECataclysmType::Death; }))
		{
			if (Made.IsALair())
			{
				++Lairs;
				WrongCount += Made.CitiesFallenBefore == 2 ? 0 : 1;
			}
		}
		for (const FCataclysmDungeon& Made : Scheduler->RollWave(*Map, 1, 0, War, {}, {},
																 []() { return ECataclysmType::War; }))
		{
			if (Made.Type == ECataclysmDungeonType::Quest)
			{
				++WarQuests;
				WarCounted += Made.CitiesFallenBefore;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("SURGE LAIRS: %d lairs, %d with a count other than 2; %d War Quest dungeons counting %d"),
							Lairs, WrongCount, WarQuests, WarCounted));
	TestTrue(TEXT("set-up: lairs were made"), Lairs > 0 && WarQuests > 0);
	TestEqual(TEXT("every lair counts the two fallen"), WrongCount, 0);
	TestEqual(TEXT("CONTROL: War's Quest dungeons count nothing"), WarCounted, 0);
	return true;
}

// ---------------------------------------------------------------------------
// A fall while Death is active adds a lair
// ---------------------------------------------------------------------------

/**
 * A city's fall while Death is active makes one lair on a neighbour, counting the fall (L1). CONTROL: the same fall
 * with Death not active makes none. Prints "FALL LAIR".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAbominationLairsFallTest,
	"Cataclysm.AbominationLairs.AFallWhileDeathIsActiveMakesALairThatCountsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAbominationLairsFallTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmAbominationLairsTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun(ECataclysmType::Death);
	UCataclysmEmpireRun* Control = MakeEmptyRun(ECataclysmType::War);
	HoldTheBoard(*Run);
	HoldTheBoard(*Control);
	const int32 Front = AFrontCity(*Run);
	if (!TestTrue(TEXT("set-up: a front city"), Front != INDEX_NONE))
	{
		return false;
	}
	const FCataclysmDayReport Report = FellTomorrow(*Run, Front, ECataclysmType::Demonic, 9001);
	const FCataclysmDayReport ControlReport = FellTomorrow(*Control, Front, ECataclysmType::Demonic, 9001);
	const TArray<const FCataclysmDungeon*> Made = Lairs(*Run);
	AddInfo(FString::Printf(TEXT("FALL LAIR: fell %s, %d lairs made on the fall, %d on the map; CONTROL %d"),
							Report.Fallen.Contains(Front) ? TEXT("yes") : TEXT("NO"), Report.LairOnAFall.Num(),
							Made.Num(), ControlReport.LairOnAFall.Num()));
	if (!TestTrue(TEXT("set-up: both fell"), Report.Fallen.Contains(Front) && ControlReport.Fallen.Contains(Front)))
	{
		return false;
	}
	TestEqual(TEXT("one lair made on the fall"), Report.LairOnAFall.Num(), 1);
	if (TestEqual(TEXT("and it is the one lair on the map"), Made.Num(), 1))
	{
		TestTrue(TEXT("on a neighbour of the fallen city"),
				 UCataclysmSurgeScheduler::AdjacentCities(*Run->Map->Find(Front)).Contains(Made[0]->CityId));
		TestEqual(TEXT("counting the fall that made it"), Made[0]->CitiesFallenBefore, Run->Map->FallenCityCount());
		TestTrue(TEXT("at least one"), Made[0]->CitiesFallenBefore >= 1);
		TestEqual(TEXT("a Horde"), static_cast<int32>(Made[0]->SubType), static_cast<int32>(ECataclysmDungeonSubType::Horde));
		TestTrue(TEXT("listed among the day's new dungeons"), Report.Spawned.Contains(Made[0]->DungeonId));
	}
	TestEqual(TEXT("CONTROL: with Death not active, none"), ControlReport.LairOnAFall.Num(), 0);
	TestEqual(TEXT("CONTROL: and no lair on its map"), Lairs(*Control).Num(), 0);
	return true;
}

/** Every fall makes one, with no cap (L2): two falls on two days, two lairs, counting one and two. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAbominationLairsNoCapTest,
	"Cataclysm.AbominationLairs.EveryFallMakesOneWithNoCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAbominationLairsNoCapTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmAbominationLairsTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun(ECataclysmType::Death);
	HoldTheBoard(*Run);
	TArray<int32> Counts;
	TArray<int32> FallLairs;
	TArray<int32> SurgeLairs;
	for (int32 Fall = 0; Fall < 2; ++Fall)
	{
		const int32 Front = AFrontCity(*Run);
		if (!TestTrue(TEXT("set-up: a front city"), Front != INDEX_NONE))
		{
			return false;
		}
		const FCataclysmDayReport Report = FellTomorrow(*Run, Front, ECataclysmType::Demonic, 9001 + Fall);
		if (!TestEqual(TEXT("one lair made on each fall"), Report.LairOnAFall.Num(), 1))
		{
			return false;
		}
		FallLairs.Add(Report.LairOnAFall[0]);
		Counts.Add(Run->FindDungeon(Report.LairOnAFall[0])->CitiesFallenBefore);
		// A SURGE THAT DAY LANDS A LAIR OF ITS OWN while Death is active: Death's Quest dungeon is a lair. Window
		// b4-w1 counted three lairs after two falls; this says where any lair beyond the falls' came from.
		for (const int32 Spawned : Report.Spawned)
		{
			const FCataclysmDungeon* Dungeon = Run->FindDungeon(Spawned);
			if (Dungeon != nullptr && Dungeon->IsALair() && !Report.LairOnAFall.Contains(Spawned))
			{
				SurgeLairs.Add(Spawned);
			}
		}
	}
	AddInfo(FString::Printf(TEXT("LAIRS: %d on the map, %d from the falls, %d a surge landed"), Lairs(*Run).Num(),
							FallLairs.Num(), SurgeLairs.Num()));
	int32 FallLairsStanding = 0;
	for (const int32 Id : FallLairs)
	{
		const FCataclysmDungeon* Dungeon = Run->FindDungeon(Id);
		FallLairsStanding += Dungeon != nullptr && Dungeon->IsALair() ? 1 : 0;
	}
	TestEqual(TEXT("two lairs, one from each fall, both on the map"), FallLairsStanding, 2);
	TestNotEqual(TEXT("and they are two lairs, not one counted twice"), FallLairs[0], FallLairs[1]);
	TestEqual(TEXT("every other lair on the map is one a surge landed"), Lairs(*Run).Num(), 2 + SurgeLairs.Num());
	TestEqual(TEXT("the second counts one more than the first"), Counts[1], Counts[0] + 1);
	return true;
}

// ---------------------------------------------------------------------------
// The Cursed Abomination
// ---------------------------------------------------------------------------

/**
 * A lair's last-wave Gatekeeper is its Cursed Abomination: flagged, at the Boss rung (L3), named "Cursed Abomination"
 * under its bar (L5), its gear's step the lair's multiplier. CONTROL: the same Horde Quest dungeon War sent has a
 * Gatekeeper that is none of those. Prints "CURSED ABOMINATION".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAbominationLairsBossTest,
	"Cataclysm.AbominationLairs.ALairsLastWaveHoldsTheCursedAbominationAtTheBossRung",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAbominationLairsBossTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmAbominationLairsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FLairPlayer Player(World);
	if (!TestTrue(TEXT("a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	ACataclysmDungeonGameMode* Mode = nullptr;
	ACataclysmEnemyCharacter* Boss = TheLastWavesGatekeeper(*this, World, ECataclysmType::Death, 3, Mode);
	ACataclysmDungeonGameMode* ControlMode = nullptr;
	ACataclysmEnemyCharacter* ControlBoss = TheLastWavesGatekeeper(*this, World, ECataclysmType::War, 3, ControlMode);
	if (!TestNotNull(TEXT("the lair's last wave holds a Gatekeeper"), Boss)
		|| !TestNotNull(TEXT("and so does the control's"), ControlBoss))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("CURSED ABOMINATION: flag %s, rung %d, text '%s', gear %.3f; CONTROL flag %s"),
							Boss->bIsTheCursedAbomination ? TEXT("yes") : TEXT("no"), Boss->RarityStep,
							*UCataclysmCombatOverlay::CursedAbominationTextFor(Boss), Boss->AbominationGearMore,
							ControlBoss->bIsTheCursedAbomination ? TEXT("YES") : TEXT("no")));
	TestTrue(TEXT("the walk is a lair"), Mode->IsALair());
	TestTrue(TEXT("its Gatekeeper is the Cursed Abomination"), Boss->bIsTheCursedAbomination);
	TestEqual(TEXT("at the Boss rung"), Boss->RarityStep, ACataclysmEnemyCharacter::FirstBossRarityStep);
	TestEqual(TEXT("named under its bar"), UCataclysmCombatOverlay::CursedAbominationTextFor(Boss),
			  FString(TEXT("Cursed Abomination")));
	TestEqual(TEXT("its gear's step is the lair's"), Boss->AbominationGearMore,
			  FMath::Pow(UCataclysmSurgeScheduler::AbominationStep, 3.0f), 1e-5f);
	TestFalse(TEXT("CONTROL: War's Horde is no lair"), ControlMode->IsALair());
	TestFalse(TEXT("CONTROL: its Gatekeeper is no Abomination"), ControlBoss->bIsTheCursedAbomination);
	TestEqual(TEXT("CONTROL: nor named one"), UCataclysmCombatOverlay::CursedAbominationTextFor(ControlBoss), FString());
	TestEqual(TEXT("CONTROL: and its gear is not stepped"), ControlBoss->AbominationGearMore, 1.0f);
	return true;
}

/**
 * The Cursed Abomination's health and damage are the step to the power of the lair's count times those of the same
 * boss counting nought. Prints "ABOMINATION STRENGTH". AT A STEP OF 1 THE TWO ARE EQUAL and this cannot tell a scaled
 * boss from an unscaled one; the decisions entry says so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAbominationLairsStrengthTest,
	"Cataclysm.AbominationLairs.TheCursedAbominationGrowsByTheStepForEachFallenCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAbominationLairsStrengthTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmAbominationLairsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FLairPlayer Player(World);
	ACataclysmDungeonGameMode* Mode = nullptr;
	ACataclysmDungeonGameMode* BaseMode = nullptr;
	ACataclysmEnemyCharacter* Grown = TheLastWavesGatekeeper(*this, World, ECataclysmType::Death, 4, Mode);
	if (!TestNotNull(TEXT("the grown lair's boss"), Grown))
	{
		return false;
	}
	const float Step4 = FMath::Pow(UCataclysmSurgeScheduler::AbominationStep, 4.0f);
	// READ AS THE FLOOR LEAVES IT, BEFORE A SECOND GAME MODE IS MADE IN THIS WORLD: `GoToFloor` populates the floor and
	// then applies its rules, and until window b4-w1 that reset the Abomination's damage (it was on the placed key).
	const float GrownDamage = Grown->GetAbominationDamageMultiplier();
	ACataclysmEnemyCharacter* Base = TheLastWavesGatekeeper(*this, World, ECataclysmType::Death, 0, BaseMode);
	if (!TestNotNull(TEXT("and the base lair's"), Base))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("ABOMINATION STRENGTH: step %.3f, x%.3f; health %.1f against %.1f; damage x%.3f"),
							UCataclysmSurgeScheduler::AbominationStep, Step4, Grown->GetStartingMaxHealth(),
							Base->GetStartingMaxHealth(), GrownDamage));
	TestEqual(TEXT("its health is the step to the fourth times the base boss's"), Grown->GetStartingMaxHealth(),
			  Base->GetStartingMaxHealth() * Step4, Base->GetStartingMaxHealth() * 1e-4f);
	TestEqual(TEXT("its damage multiplier is the same, once its floor's rules have been applied"), GrownDamage, Step4,
			  1e-4f);
	TestEqual(TEXT("and still is after another floor in the world applied its rules"),
			  Grown->GetAbominationDamageMultiplier(), Step4, 1e-4f);
	TestEqual(TEXT("CONTROL: the base boss's damage multiplier is one"), Base->GetAbominationDamageMultiplier(), 1.0f);
	return true;
}

// ---------------------------------------------------------------------------
// The map and the save
// ---------------------------------------------------------------------------

/** The capital map names a lair and how strong its Abomination is. CONTROL: War's Quest dungeon reads as before. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAbominationLairsMapTest,
	"Cataclysm.AbominationLairs.TheMapNamesALairAndItsAbominationsStrength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAbominationLairsMapTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmAbominationLairsTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun(ECataclysmType::Death);
	const FCataclysmCity& City = *Run->Map->Find(Run->Map->ExposedCities()[0]);
	Place(*Run, 801, ECataclysmDungeonType::Quest, ECataclysmType::Death, City);
	Run->Dungeons.Last().SubType = ECataclysmDungeonSubType::Horde;
	Run->Dungeons.Last().CitiesFallenBefore = 2;
	Place(*Run, 802, ECataclysmDungeonType::Quest, ECataclysmType::War, City);
	Run->Dungeons.Last().SubType = ECataclysmDungeonSubType::Siege;
	const FString Lair = UCataclysmEmpireMapLayout::CityDetailDungeonLine(Run, 801);
	const FString War = UCataclysmEmpireMapLayout::CityDetailDungeonLine(Run, 802);
	AddInfo(FString::Printf(TEXT("MAP: '%s' | '%s'"), *Lair, *War));
	TestTrue(TEXT("a lair is named as one"), Lair.StartsWith(TEXT("Abomination lair of 4 floors")));
	TestTrue(TEXT("with its Abomination's strength"), Lair.Contains(TEXT("its Abomination ")));
	TestFalse(TEXT("CONTROL: War's Quest dungeon is no lair"), War.Contains(TEXT("Abomination")));
	return true;
}

/** A gathered run record carries a lair's count, and a load reads it. CONTROL: a dungeon that is no lair, nought. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAbominationLairsSaveTest,
	"Cataclysm.AbominationLairs.AGatheredRunRecordCarriesTheCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAbominationLairsSaveTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmAbominationLairsTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun(ECataclysmType::Death);
	const FCataclysmCity& City = *Run->Map->Find(Run->Map->ExposedCities()[0]);
	Place(*Run, 811, ECataclysmDungeonType::Quest, ECataclysmType::Death, City);
	Run->Dungeons.Last().CitiesFallenBefore = 5;
	Place(*Run, 812, ECataclysmDungeonType::Basic, ECataclysmType::Death, City);

	UCataclysmRunSave* Record = NewObject<UCataclysmRunSave>();
	FString Why;
	if (!TestTrue(TEXT("the dungeons are gathered"), FCataclysmSaveGather::DungeonsFrom(*Run, *Record, Why)))
	{
		AddError(Why);
		return false;
	}
	Record->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;
	FString Text;
	FString Error;
	if (!TestTrue(TEXT("it is written"), FCataclysmSaveStorage::ToJson(Record, Text, Error)))
	{
		AddError(Error);
		return false;
	}
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (!TestNotNull(TEXT("and read back"), Read))
	{
		AddError(Message);
		return false;
	}
	const FCataclysmDungeon* Lair = Read->Dungeons.FindByPredicate(
		[](const FCataclysmDungeon& Dungeon) { return Dungeon.DungeonId == 811; });
	const FCataclysmDungeon* Other = Read->Dungeons.FindByPredicate(
		[](const FCataclysmDungeon& Dungeon) { return Dungeon.DungeonId == 812; });
	if (!TestNotNull(TEXT("the lair is read"), Lair) || !TestNotNull(TEXT("and the other"), Other))
	{
		return false;
	}
	TestEqual(TEXT("its count is carried"), Lair->CitiesFallenBefore, 5);
	TestEqual(TEXT("CONTROL: a dungeon that is no lair, nought"), Other->CitiesFallenBefore, 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
