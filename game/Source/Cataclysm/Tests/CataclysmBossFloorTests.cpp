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
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmFloorModifierPanelLayout.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
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
 *   dungeon of it is taken and rewritten: its depth, its kind, its sub-type, and no modifiers, so its floors draw none;
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
	 * Takes the `Skip`-th dungeon of the run that is not a Cow Level, makes it `Floors` deep, of this kind and
	 * sub-type and carrying no modifiers, and enters it. Answers its identifier, or `INDEX_NONE`.
	 */
	int32 EnterBossFloorDungeon(FAutomationTestBase& Test, ACataclysmDungeonGameMode* Mode, UCataclysmEmpireRun* Run,
								int32 Floors, ECataclysmDungeonType Type, ECataclysmDungeonSubType SubType,
								int32 Skip = 0)
	{
		int32 Chosen = INDEX_NONE;
		int32 Passed = 0;
		for (int32 Index = 0; Index < Run->Dungeons.Num() && Chosen == INDEX_NONE; ++Index)
		{
			if (Run->Dungeons[Index].SubType != ECataclysmDungeonSubType::CowLevel && Passed++ >= Skip)
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
	if (EnterBossFloorDungeon(*this, Mode, Scene.Run, 2, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::Elite,
							  /*Skip=*/1) == INDEX_NONE)
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

#endif // WITH_AUTOMATION_TESTS
