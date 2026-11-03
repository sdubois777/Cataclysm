// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmFloorModifierPanelLayout.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveRecords.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The last floor of the Cataclysm boss dungeon, issue #1324 slice 6d. Since 2026-11-03.
 *
 * `docs/Cataclysm_GDD_v2.md` section VIII: "A Cataclysm Boss is placed, not drawn". So the Gatekeeper the population
 * places at the exit of that dungeon's last floor stands at the Cataclysm Boss rung, 5, where every other Gatekeeper
 * draws its rung. And ruling R5 of 2026-11-02 by the coordinating session: "the last floor's stairs stay sealed until
 * the Cataclysm Boss dies" -- any death, the player's kill or anything else.
 *
 * EVERY FIXTURE'S STARTING STATE, AND WHERE IT COMES FROM:
 * - the run is `UCataclysmEmpireRun::Begin(1)` and one `AdvanceDay`, as the Heavenly Core's tests build theirs, and a
 *   dungeon of it is taken and rewritten: its depth, its kind, its sub-type, and no modifiers, so its floors draw none.
 *   ONE DUNGEON IS ENOUGH: a test that walks two kinds rewrites the same one twice, so no test needs the opening wave
 *   to have put more than one dungeon that is not a Cow Level on the board;
 * - except the test of the earned boss dungeon, which clears the run's quests by hand until
 *   `UCataclysmEmpireRun::OpenCataclysmDungeonIfEarned` opens the real one (issue #1324 slice 6b) -- on no city, 100
 *   to 150 floors deep -- and then takes its modifiers off and makes it plain, for the same reason;
 * - `EnemyScale` is 0, so the population places the boss at the exit and nothing else
 *   (`FCataclysmFloorPopulator::Populate` places the boss before it asks the density for anything);
 * - the player is a possessed `ACataclysmPlayerCharacter` at the world's origin, with no gear.
 * NOTHING HERE ASSERTS A HEALTH OR A SCORE: the boss is priced by the existing score model through its rung, and the
 * figures are the score model's own tests'.
 */
namespace CataclysmBossFloorTest
{
	/** A player character possessed by a controller, as the dungeon game mode finds one. */
	struct FBossFloorPlayer
	{
		explicit FBossFloorPlayer(UWorld* World)
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
	 * Takes the first dungeon of the run that is not a Cow Level, makes it `Floors` deep, of this kind and sub-type and
	 * carrying no modifiers, and enters it. Answers its identifier, or `INDEX_NONE`. A second call takes the same
	 * dungeon again, so one dungeon on the board is all any test here needs.
	 */
	int32 EnterBossFloorDungeon(FAutomationTestBase& Test, ACataclysmDungeonGameMode* Mode, UCataclysmEmpireRun* Run,
								int32 Floors, ECataclysmDungeonType Type, ECataclysmDungeonSubType SubType)
	{
		int32 Chosen = INDEX_NONE;
		for (int32 Index = 0; Index < Run->Dungeons.Num() && Chosen == INDEX_NONE; ++Index)
		{
			if (Run->Dungeons[Index].SubType != ECataclysmDungeonSubType::CowLevel)
			{
				Chosen = Index;
			}
		}
		if (!Test.TestTrue(TEXT("set-up: the run has a dungeon to walk"), Chosen != INDEX_NONE))
		{
			return INDEX_NONE;
		}

		FCataclysmDungeon& Dungeon = Run->Dungeons[Chosen];
		Dungeon.Floors = Floors;
		Dungeon.Type = Type;
		Dungeon.SubType = SubType;
		Dungeon.Modifiers.Reset();
		Dungeon.ModifierScore = 0.0f;
		const int32 DungeonId = Dungeon.DungeonId;
		if (!Test.TestTrue(TEXT("set-up: the dungeon was entered"), Mode->EnterEmpireDungeon(DungeonId)))
		{
			return INDEX_NONE;
		}
		return DungeonId;
	}

	/** The floor's Gatekeeper, the one the population placed at the exit, or null. */
	ACataclysmEnemyCharacter* BossFloorGatekeeper(const ACataclysmDungeonGameMode* Mode)
	{
		for (ACataclysmEnemyCharacter* Enemy : Mode->FloorEnemies)
		{
			if (IsValid(Enemy) && Enemy->IsA<ACataclysmGatekeeperCharacter>())
			{
				return Enemy;
			}
		}
		return nullptr;
	}

	/** The panel's line for the Cataclysm Boss, as the panel would draw it, or empty when the panel lists none. */
	FString BossFloorPanelLine(const ACataclysmDungeonGameMode* Mode)
	{
		using Layout = UCataclysmFloorModifierPanelLayout;
		const TArray<FCataclysmFloorModifierLine> Lines = Layout::LinesFor(
			Mode->RowsTheFloorPanelLists(), UCataclysmDungeonModifierTable::LoadDungeonModifierTable(),
			Mode->LiveCountsForTheFloor());
		for (const FCataclysmFloorModifierLine& Line : Lines)
		{
			if (Line.bIsTheCataclysmBoss)
			{
				return Layout::NameLineFor(Line);
			}
		}
		return FString();
	}

	/**
	 * Makes one blow or one loss certain to kill the boss: its drawn modifiers taken off (a Sacrificial Bond keeps
	 * only a share of a blow), no evasion, block or retaliation, no energy shield, and one point of health.
	 */
	void ReadyTheBossToDie(ACataclysmEnemyCharacter* Boss)
	{
		Boss->ModifierRows.Reset();
		Boss->ApplyStartingAttributes();
		if (UAbilitySystemComponent* Abilities = Boss->GetAbilitySystemComponent())
		{
			Abilities->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Abilities->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Abilities->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetRetaliationAttribute(), 0.0f);
			Abilities->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
			Abilities->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
		}
	}

	/** The world, the game mode, the player and the run every test here starts from. */
	struct FBossFloorScene
	{
		UWorld* World = nullptr;
		ACataclysmDungeonGameMode* Mode = nullptr;
		UCataclysmEmpireRun* Run = nullptr;
	};

	FBossFloorScene MakeBossFloorScene(UWorld* World)
	{
		FBossFloorScene Out;
		Out.World = World;
		Out.Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
		Out.Run = NewObject<UCataclysmEmpireRun>();
		if (Out.Mode && Out.Run)
		{
			Out.Run->Begin(1);
			Out.Run->AdvanceDay();
			Out.Mode->SetEmpireRunForTests(Out.Run);
			// THE BOSS AND NOTHING ELSE ON A FLOOR. See the file's comment.
			Out.Mode->EnemyScale = 0.0f;
		}
		return Out;
	}

	/**
	 * Opens the run's real Cataclysm boss dungeon the way play does, through `UCataclysmEmpireRun::ClearDungeon`: as
	 * many Quest dungeons of the first active Cataclysm as it asks for, built by hand on the map's first city and
	 * cleared, as `Cataclysm.EarnedBoss.` does. A run at difficulty tier 1 faces one Cataclysm and needs it finished.
	 * Answers the boss dungeon's index in `Dungeons`, or `INDEX_NONE`.
	 */
	int32 OpenTheEarnedBossDungeon(UCataclysmEmpireRun& Run)
	{
		if (Run.ActiveCataclysms.IsEmpty() || Run.Map == nullptr || Run.Map->Cities.IsEmpty())
		{
			return INDEX_NONE;
		}
		const ECataclysmType Sender = Run.ActiveCataclysms[0];
		const int32 Needed = UCataclysmRoster::QuestObjectivesFor(Sender);
		for (int32 Cleared = 0; Cleared < Needed; ++Cleared)
		{
			FCataclysmDungeon Quest;
			Quest.DungeonId = Run.NextDungeonId++;
			Quest.Type = ECataclysmDungeonType::Quest;
			Quest.Cataclysm = Sender;
			Quest.CityId = Run.Map->Cities[0].CityId;
			Run.Dungeons.Add(Quest);
			Run.ClearDungeon(Quest.DungeonId);
		}
		return Run.Dungeons.IndexOfByPredicate([](const FCataclysmDungeon& Standing)
		{
			return Standing.Type == ECataclysmDungeonType::Cataclysm;
		});
	}
}

// ---------------------------------------------------------------------------
// The rung
// ---------------------------------------------------------------------------

/**
 * The last floor of a Cataclysm boss dungeon puts its Gatekeeper at the Cataclysm Boss rung, and nothing else does.
 *
 * AN ELITE CATACLYSM DUNGEON, because "every floor ends with a boss fight" there: floors 1 and 2 have a Gatekeeper too,
 * so the CONTROL on them has a boss to look at. And THE CONTROL OF ANOTHER KIND: the last floor of an Elite Quest
 * dungeon, whose Gatekeeper draws. A drawn rung is never 5, because the Cataclysm Boss's weight is 0.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossFloorRungTest,
	"Cataclysm.BossFloor.OnlyTheLastFloorOfACataclysmDungeonPlacesTheCataclysmBoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBossFloorRungTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmBossFloorTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FBossFloorPlayer Player(World);
	const FBossFloorScene Scene = MakeBossFloorScene(World);
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player"), Player.IsUsable()))
	{
		return false;
	}

	if (EnterBossFloorDungeon(*this, Mode, Scene.Run, 3, ECataclysmDungeonType::Cataclysm,
							  ECataclysmDungeonSubType::Elite) == INDEX_NONE)
	{
		return false;
	}

	// FLOORS 1 AND 2: A GATEKEEPER EACH, AT A DRAWN RUNG, AND NO CATACLYSM BOSS.
	for (int32 Floor = 1; Floor <= 2; ++Floor)
	{
		if (Floor > 1 && !TestTrue(FString::Printf(TEXT("set-up: floor %d was reached"), Floor), Mode->GoToFloor(Floor)))
		{
			return false;
		}
		const ACataclysmEnemyCharacter* Gatekeeper = BossFloorGatekeeper(Mode);
		if (!TestNotNull(FString::Printf(TEXT("set-up: floor %d of an Elite dungeon has a Gatekeeper"), Floor),
						 Gatekeeper))
		{
			return false;
		}
		TestFalse(FString::Printf(TEXT("CONTROL: floor %d is not the Cataclysm Boss's floor"), Floor),
				  Mode->IsTheCataclysmBossFloor());
		TestTrue(FString::Printf(TEXT("CONTROL: floor %d's Gatekeeper keeps a drawn rung, below the Cataclysm Boss's "
									  "(it stands at %d)"),
								 Floor, Gatekeeper->RarityStep),
				 Gatekeeper->RarityStep < ACataclysmDungeonGameMode::CataclysmBossRung);
		TestNull(FString::Printf(TEXT("CONTROL: and no Cataclysm Boss is recorded on floor %d"), Floor),
				 Mode->CataclysmBossNow());
		TestTrue(FString::Printf(TEXT("CONTROL: and nothing seals floor %d's stairs"), Floor),
				 Mode->StairsSealedBy().IsEmpty());
	}

	// FLOOR 3, THE LAST: THE CATACLYSM BOSS.
	if (!TestTrue(TEXT("set-up: floor 3 was reached"), Mode->GoToFloor(3))
		|| !TestTrue(TEXT("set-up: and is the last"), Mode->IsOnTheLastFloor()))
	{
		return false;
	}
	TestTrue(TEXT("the last floor of a Cataclysm dungeon is the Cataclysm Boss's floor"), Mode->IsTheCataclysmBossFloor());
	ACataclysmEnemyCharacter* Boss = BossFloorGatekeeper(Mode);
	if (!TestNotNull(TEXT("its exit holds a Gatekeeper"), Boss))
	{
		return false;
	}
	TestEqual(TEXT("at rung 5"), Boss->RarityStep, 5);
	TestEqual(TEXT("which the rarity table names the Cataclysm Boss"),
			  UCataclysmEnemyRarity::RarityNameForStep(UCataclysmEnemyRarity::LoadEnemyRarityTable(), Boss->RarityStep),
			  FString(TEXT("Cataclysm Boss")));
	TestTrue(TEXT("a boss, so it cannot be stunned or executed"), Boss->IsBoss());
	TestTrue(TEXT("and it is the Cataclysm Boss the floor records"), Mode->CataclysmBossNow() == Boss);

	// THE CONTROL OF ANOTHER KIND: THE LAST FLOOR OF AN ELITE QUEST DUNGEON.
	Mode->LeaveEmpireDungeon();
	if (EnterBossFloorDungeon(*this, Mode, Scene.Run, 2, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::Elite)
		== INDEX_NONE)
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the Quest dungeon's floor 2 was reached"), Mode->GoToFloor(2))
		|| !TestTrue(TEXT("set-up: and is its last"), Mode->IsOnTheLastFloor()))
	{
		return false;
	}
	const ACataclysmEnemyCharacter* QuestBoss = BossFloorGatekeeper(Mode);
	if (!TestNotNull(TEXT("set-up: the Quest dungeon's last floor has a Gatekeeper"), QuestBoss))
	{
		return false;
	}
	TestFalse(TEXT("CONTROL: another kind's last floor is not the Cataclysm Boss's floor"),
			  Mode->IsTheCataclysmBossFloor());
	TestTrue(FString::Printf(TEXT("CONTROL: its Gatekeeper keeps a drawn rung, below the Cataclysm Boss's (it stands at "
								  "%d)"),
							 QuestBoss->RarityStep),
			 QuestBoss->RarityStep < ACataclysmDungeonGameMode::CataclysmBossRung);
	TestNull(TEXT("CONTROL: and no Cataclysm Boss is recorded there"), Mode->CataclysmBossNow());
	TestTrue(TEXT("CONTROL: and nothing seals its stairs"), Mode->StairsSealedBy().IsEmpty());
	TestTrue(TEXT("CONTROL: and its floor panel names no Cataclysm Boss"), BossFloorPanelLine(Mode).IsEmpty());
	return true;
}

// ---------------------------------------------------------------------------
// The seal, and the panel
// ---------------------------------------------------------------------------

/**
 * The last floor's stairs stay sealed while the Cataclysm Boss stands, and the player's kill opens them. The floor panel
 * names the boss and says which. Ruling R5 of 2026-11-02.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossFloorSealByThePlayerTest,
	"Cataclysm.BossFloor.TheLastFloorsStairsStaySealedUntilThePlayerKillsTheCataclysmBoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBossFloorSealByThePlayerTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmBossFloorTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FBossFloorPlayer Player(World);
	const FBossFloorScene Scene = MakeBossFloorScene(World);
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player"), Player.IsUsable()))
	{
		return false;
	}

	const int32 DungeonId = EnterBossFloorDungeon(*this, Mode, Scene.Run, 2, ECataclysmDungeonType::Cataclysm,
												  ECataclysmDungeonSubType::None);
	if (DungeonId == INDEX_NONE)
	{
		return false;
	}
	const FName Seal(ACataclysmDungeonGameMode::CataclysmBossSealKey);

	// FLOOR 1 IS NOT THE LAST: NOTHING OF THE BOSS'S.
	TestFalse(TEXT("CONTROL: floor 1 is not the Cataclysm Boss's floor"), Mode->IsTheCataclysmBossFloor());
	TestFalse(TEXT("CONTROL: so the boss seals none of its stairs"), Mode->StairsSealedBy().Contains(Seal));
	TestTrue(TEXT("CONTROL: and floor 1's panel names no Cataclysm Boss"), BossFloorPanelLine(Mode).IsEmpty());

	if (!TestTrue(TEXT("set-up: floor 2 was reached"), Mode->GoToFloor(2))
		|| !TestTrue(TEXT("set-up: and is the last"), Mode->IsOnTheLastFloor()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Boss = Mode->CataclysmBossNow();
	if (!TestNotNull(TEXT("set-up: the Cataclysm Boss stands on the last floor"), Boss))
	{
		return false;
	}
	TestEqual(TEXT("at the Cataclysm Boss rung"), Boss->RarityStep, 5);

	// WHILE IT STANDS: THE PANEL NAMES IT, AND THE STAIRS ARE SEALED BY IT AND BY NOTHING ELSE.
	TestEqual(TEXT("the floor panel names it, and says the stairs are sealed"), BossFloorPanelLine(Mode),
			  FString(TEXT("The Cataclysm Boss (it stands; the stairs are sealed until it dies)")));
	if (!TestTrue(TEXT("while it stands the Cataclysm Boss seals the stairs"),
				  Mode->StairsSealedBy() == TArray<FName>({Seal})))
	{
		return false;
	}
	Mode->HandleStairsTaken();
	if (!TestNotNull(TEXT("so taking the stairs does not clear the dungeon"), Scene.Run->FindDungeon(DungeonId)))
	{
		return false;
	}

	// THE PLAYER KILLS IT.
	ReadyTheBossToDie(Boss);
	UCataclysmSkillEffects::ApplyHit(Player.Character, Boss, 100000.0f);
	if (!TestTrue(TEXT("set-up: the player's blow killed the Cataclysm Boss"), UCataclysmSkillEffects::IsDead(Boss)))
	{
		return false;
	}
	if (!TestTrue(TEXT("once it is dead nothing seals the stairs"), Mode->StairsSealedBy().IsEmpty()))
	{
		return false;
	}
	TestNull(TEXT("and no Cataclysm Boss stands"), Mode->CataclysmBossNow());
	TestEqual(TEXT("the floor panel says it is slain and the way out open"), BossFloorPanelLine(Mode),
			  FString(TEXT("The Cataclysm Boss (slain; the way out is open)")));

	Mode->HandleStairsTaken();
	TestNull(TEXT("now taking the stairs clears the dungeon"), Scene.Run->FindDungeon(DungeonId));
	return true;
}

/**
 * Any death of the Cataclysm Boss opens the stairs, not only the player's kill: here it dies of a loss of health it
 * takes from itself, with no player on record. Ruling R5 names no killer; the design says defeating the boss.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossFloorSealByAnotherCauseTest,
	"Cataclysm.BossFloor.TheLastFloorsStairsOpenWhenTheCataclysmBossDiesOfAnythingElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBossFloorSealByAnotherCauseTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmBossFloorTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FBossFloorPlayer Player(World);
	const FBossFloorScene Scene = MakeBossFloorScene(World);
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player"), Player.IsUsable()))
	{
		return false;
	}

	const int32 DungeonId = EnterBossFloorDungeon(*this, Mode, Scene.Run, 2, ECataclysmDungeonType::Cataclysm,
												  ECataclysmDungeonSubType::None);
	if (DungeonId == INDEX_NONE)
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: floor 2 was reached"), Mode->GoToFloor(2))
		|| !TestTrue(TEXT("set-up: and is the last"), Mode->IsOnTheLastFloor()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Boss = Mode->CataclysmBossNow();
	if (!TestNotNull(TEXT("set-up: the Cataclysm Boss stands on the last floor"), Boss))
	{
		return false;
	}
	if (!TestTrue(TEXT("while it stands the Cataclysm Boss seals the stairs, here too"),
				  Mode->StairsSealedBy() == TArray<FName>({FName(ACataclysmDungeonGameMode::CataclysmBossSealKey)})))
	{
		return false;
	}

	// IT DIES OF SOMETHING ELSE: a loss of health from itself, which is no hit and names no player.
	ReadyTheBossToDie(Boss);
	UCataclysmSkillEffects::ReduceHealthDirectly(Boss, Boss, 1000.0f);
	if (!TestTrue(TEXT("set-up: the Cataclysm Boss died with no blow of the player's"),
				  UCataclysmSkillEffects::IsDead(Boss)))
	{
		return false;
	}
	if (!TestTrue(TEXT("once it is dead, whoever killed it, nothing seals the stairs"), Mode->StairsSealedBy().IsEmpty()))
	{
		return false;
	}
	TestEqual(TEXT("the floor panel says it is slain"), BossFloorPanelLine(Mode),
			  FString(TEXT("The Cataclysm Boss (slain; the way out is open)")));

	Mode->HandleStairsTaken();
	TestNull(TEXT("and taking the stairs clears the dungeon"), Scene.Run->FindDungeon(DungeonId));
	return true;
}

// ---------------------------------------------------------------------------
// The earned boss dungeon itself
// ---------------------------------------------------------------------------

/**
 * The real Cataclysm boss dungeon, opened by the quest clears that earn it (issue #1324 slice 6b), on no city and 100
 * to 150 floors deep: its last floor places the Cataclysm Boss and is sealed until it dies, and its first is not. The
 * tests above rewrite an ordinary dungeon into the Cataclysm kind; this one walks the dungeon the run itself opened.
 * Its rolled sub-type and modifiers are taken off, as the others' are, so no floor rule and no Horde wave decides what
 * stands at the exit.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossFloorEarnedDungeonTest,
	"Cataclysm.BossFloor.TheEarnedBossDungeonsLastFloorHoldsTheCataclysmBossUntilItDies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBossFloorEarnedDungeonTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmBossFloorTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FBossFloorPlayer Player(World);
	const FBossFloorScene Scene = MakeBossFloorScene(World);
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player"), Player.IsUsable()))
	{
		return false;
	}

	const int32 Index = OpenTheEarnedBossDungeon(*Scene.Run);
	if (!TestTrue(TEXT("set-up: the quests cleared opened the Cataclysm boss dungeon"), Index != INDEX_NONE))
	{
		return false;
	}
	FCataclysmDungeon& Earned = Scene.Run->Dungeons[Index];
	TestEqual(TEXT("set-up: it is the earned one, standing on no city"), Earned.CityId, INDEX_NONE);
	const int32 Floors = Earned.Floors;
	if (!TestTrue(FString::Printf(TEXT("set-up: it is as deep as its roll, 100 or more (it is %d)"), Floors),
				  Floors >= 100))
	{
		return false;
	}
	Earned.SubType = ECataclysmDungeonSubType::None;
	Earned.Modifiers.Reset();
	Earned.ModifierScore = 0.0f;
	const int32 DungeonId = Earned.DungeonId;
	if (!TestTrue(TEXT("set-up: the boss dungeon was entered"), Mode->EnterEmpireDungeon(DungeonId)))
	{
		return false;
	}
	const FName Seal(ACataclysmDungeonGameMode::CataclysmBossSealKey);

	// ITS FIRST FLOOR: NOTHING OF THE BOSS'S.
	TestFalse(TEXT("CONTROL: the earned boss dungeon's first floor is not the Cataclysm Boss's floor"),
			  Mode->IsTheCataclysmBossFloor());
	TestNull(TEXT("CONTROL: and no Cataclysm Boss is recorded there"), Mode->CataclysmBossNow());
	TestFalse(TEXT("CONTROL: and the boss seals none of its stairs"), Mode->StairsSealedBy().Contains(Seal));

	// ITS LAST FLOOR: THE CATACLYSM BOSS, AND THE STAIRS SEALED BY IT.
	if (!TestTrue(TEXT("set-up: its last floor was reached"), Mode->GoToFloor(Floors))
		|| !TestTrue(TEXT("set-up: and is the last"), Mode->IsOnTheLastFloor()))
	{
		return false;
	}
	TestTrue(TEXT("the earned boss dungeon's last floor is the Cataclysm Boss's floor"), Mode->IsTheCataclysmBossFloor());
	ACataclysmEnemyCharacter* Boss = BossFloorGatekeeper(Mode);
	if (!TestNotNull(TEXT("its exit holds a Gatekeeper"), Boss))
	{
		return false;
	}
	TestEqual(TEXT("at the Cataclysm Boss rung, 5"), Boss->RarityStep, 5);
	TestTrue(TEXT("and it is the Cataclysm Boss the floor records"), Mode->CataclysmBossNow() == Boss);
	if (!TestTrue(TEXT("while it stands the earned boss dungeon's last stairs are sealed by it alone"),
				  Mode->StairsSealedBy() == TArray<FName>({Seal})))
	{
		return false;
	}
	Mode->HandleStairsTaken();
	if (!TestNotNull(TEXT("so taking them does not clear the boss dungeon"), Scene.Run->FindCataclysmDungeon()))
	{
		return false;
	}

	// IT DIES, AND THE WAY OUT IS OPEN.
	ReadyTheBossToDie(Boss);
	UCataclysmSkillEffects::ApplyHit(Player.Character, Boss, 100000.0f);
	if (!TestTrue(TEXT("set-up: the player's blow killed the Cataclysm Boss"), UCataclysmSkillEffects::IsDead(Boss)))
	{
		return false;
	}
	TestTrue(TEXT("once it is dead nothing seals the earned boss dungeon's last stairs"), Mode->StairsSealedBy().IsEmpty());
	Mode->HandleStairsTaken();
	TestNull(TEXT("and taking them clears the boss dungeon"), Scene.Run->FindCataclysmDungeon());
	return true;
}

// ---------------------------------------------------------------------------
// A load
// ---------------------------------------------------------------------------

/**
 * A load finds the Cataclysm Boss again, and does not take a risen one for it.
 *
 * A LOAD REBUILDS THE FLOOR AND THEN REPLACES ITS CREATURES WITH THE SAVED ONES: the rebuilt boss is destroyed, and
 * `RestoreCreatureRoles` is handed each saved creature with its record (`CataclysmSaveLoad::RelinkTheCreatures`).
 * This test does those two steps by hand, as `Cataclysm.SaveLoad.` does for the other roles the call restores. The
 * saved Gatekeeper at the Cataclysm Boss rung is the boss again and seals the stairs. A Gatekeeper at that rung whose
 * record says it rose from the dead -- Dead Rising and Divine Resurgence both raise a fallen creature at the rung it
 * died at and mark it so, and the save keeps the mark -- is NOT: the boss it rose from is dead, and its death opened the
 * stairs for good.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBossFloorLoadTest,
	"Cataclysm.BossFloor.ALoadFindsTheSavedCataclysmBossAndNotOneThatRoseFromTheDead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBossFloorLoadTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmBossFloorTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FBossFloorPlayer Player(World);
	const FBossFloorScene Scene = MakeBossFloorScene(World);
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player"), Player.IsUsable()))
	{
		return false;
	}

	if (EnterBossFloorDungeon(*this, Mode, Scene.Run, 2, ECataclysmDungeonType::Cataclysm,
							  ECataclysmDungeonSubType::None) == INDEX_NONE)
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: floor 2 was reached"), Mode->GoToFloor(2))
		|| !TestTrue(TEXT("set-up: and is the last"), Mode->IsOnTheLastFloor()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Rebuilt = Mode->CataclysmBossNow();
	if (!TestNotNull(TEXT("set-up: the rebuilt floor's Cataclysm Boss stands"), Rebuilt))
	{
		return false;
	}
	const FName Seal(ACataclysmDungeonGameMode::CataclysmBossSealKey);

	// THE LOAD DESTROYS THE REBUILT FLOOR'S CREATURES, which raises no death notice.
	Rebuilt->Destroy();
	if (!TestNull(TEXT("set-up: with the rebuilt boss destroyed no Cataclysm Boss stands"), Mode->CataclysmBossNow()))
	{
		return false;
	}

	const auto SpawnGatekeeperAtTheBossRung = [World](const FVector& Where) -> ACataclysmEnemyCharacter*
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ACataclysmEnemyCharacter* Gatekeeper = World->SpawnActor<ACataclysmGatekeeperCharacter>(
			ACataclysmGatekeeperCharacter::StaticClass(), Where, FRotator::ZeroRotator, Spawn);
		if (Gatekeeper)
		{
			Gatekeeper->SetRarityStep(ACataclysmDungeonGameMode::CataclysmBossRung);
		}
		return Gatekeeper;
	};

	// CONTROL: A GATEKEEPER AT THE BOSS RUNG THAT ROSE FROM THE DEAD.
	ACataclysmEnemyCharacter* Risen = SpawnGatekeeperAtTheBossRung(FVector(-800.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a risen Gatekeeper was put back"), Risen))
	{
		return false;
	}
	FCataclysmSavedCreature RisenRecord;
	RisenRecord.RarityStep = ACataclysmDungeonGameMode::CataclysmBossRung;
	RisenRecord.bRisenFromTheDead = true;
	Mode->RestoreCreatureRoles(*Risen, RisenRecord);
	TestNull(TEXT("CONTROL: a Gatekeeper at the Cataclysm Boss rung that rose from the dead is not taken for the boss"),
			 Mode->CataclysmBossNow());
	TestFalse(TEXT("CONTROL: so it seals nothing"), Mode->StairsSealedBy().Contains(Seal));

	// THE SAVED BOSS.
	ACataclysmEnemyCharacter* Saved = SpawnGatekeeperAtTheBossRung(FVector(800.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the saved Cataclysm Boss was put back"), Saved))
	{
		return false;
	}
	FCataclysmSavedCreature BossRecord;
	BossRecord.RarityStep = ACataclysmDungeonGameMode::CataclysmBossRung;
	Mode->RestoreCreatureRoles(*Saved, BossRecord);
	TestTrue(TEXT("the saved Gatekeeper at the Cataclysm Boss rung is the floor's Cataclysm Boss again"),
			 Mode->CataclysmBossNow() == Saved);
	TestTrue(TEXT("and it seals the stairs again, alone"), Mode->StairsSealedBy() == TArray<FName>({Seal}));
	TestEqual(TEXT("and the floor panel says it stands"), BossFloorPanelLine(Mode),
			  FString(TEXT("The Cataclysm Boss (it stands; the stairs are sealed until it dies)")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
