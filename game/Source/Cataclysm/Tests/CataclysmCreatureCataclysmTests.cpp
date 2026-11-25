// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "Dungeon/CataclysmFloorPopulation.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Every creature belongs to the Cataclysm that sent its dungeon. Slice B3 of the Death and War plan, 2026-11-25.
 *
 * WHAT WAS WRONG. A creature draws its enemy modifiers from its own Cataclysm's column and the Generic one
 * (`UCataclysmEnemyModifiers::Draw`, called with `ACataclysmEnemyCharacter::DamageType`), and that field defaulted to
 * Demonic and nothing outside the tests set it. So a Death or War dungeon filled with Demonic creatures carrying Demonic
 * modifiers, dealing Demonic damage, and none of the 11 Death or 9 War enemy modifiers could ever be drawn in play. The
 * owner accepted on 2026-11-24 that the Demonic bodies standing in for Death's and War's belong to the Cataclysm that
 * sent the dungeon: they deal its damage type and draw its modifiers (the plan's Q16).
 *
 * WHAT THESE COVER. The rules seam (the brief carries the sender; the damage type each sender names; every roster is the
 * Demonic seven for now and the same seed builds the same floor whoever sent it), the floor's creatures in a dungeon
 * entered from the empire (a Death dungeon's, and a Demonic dungeon's and one nobody sent as the controls), a creature a
 * rule raises in a War dungeon, and the Gatekeeper's called Imps. A save and load keeping it is
 * `Cataclysm.SaveLoad.ACreatureSavedInADeathDungeonComesBackDealingDeath`, beside the other load tests it reuses.
 *
 * EVERY DUNGEON HERE IS PLACED BY HAND on an Outpost of a run begun at seed 1, the board emptied first, with no
 * modifiers unless a test names one and a resolve timer of 1,000 days, so no surge, resolution or fall can take it off
 * the board between two entries. `CataclysmSaveLoadTests.cpp`'s `PlaceByHand` is the same shape.
 *
 * ONE PREFIX, `Cataclysm.CreatureCataclysm.` WITH ITS DOT, which no other group shares.
 */
namespace CataclysmCreatureCataclysmTest
{
	/** A player character possessed by a controller, as the dungeon game mode finds one on its beat. */
	struct FCreatureCataclysmPlayer
	{
		explicit FCreatureCataclysmPlayer(UWorld* World)
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

	/** The game mode and the run every test here walks. */
	struct FCreatureCataclysmScene
	{
		ACataclysmDungeonGameMode* Mode = nullptr;
		UCataclysmEmpireRun* Run = nullptr;

		bool IsUsable() const { return Mode && Run && Run->Map && Run->Clock; }
	};

	/**
	 * A dungeon game mode and a run of seed 1 whose board has been emptied, the run handed to the game mode.
	 *
	 * @param bStartPlay  begin play on the game mode first, as a test that beats it needs
	 * @param Density     the game mode's `EnemyScale`, set before play begins
	 */
	FCreatureCataclysmScene MakeCreatureCataclysmScene(UWorld* World, bool bStartPlay, float Density)
	{
		FCreatureCataclysmScene Out;
		Out.Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
		Out.Run = NewObject<UCataclysmEmpireRun>();
		if (!Out.Mode || !Out.Run)
		{
			return Out;
		}
		Out.Mode->EnemyScale = Density;
		if (bStartPlay)
		{
			Out.Mode->StartPlay();
		}
		Out.Run->Begin(/*InSeed=*/1);
		Out.Run->AdvanceDay();
		if (Out.Run->Clock)
		{
			// THE OPENING WAVE TAKEN OFF, dungeons and timers together, so nothing bites the city the test's dungeon
			// stands on and the two lists stay in step.
			Out.Run->Dungeons.Empty();
			Out.Run->Clock->Timers.Empty();
		}
		Out.Mode->SetEmpireRunForTests(Out.Run);
		return Out;
	}

	/**
	 * Puts one ordinary dungeon, sent by `Sender`, on the run's first standing Outpost, with its timer. Its identifier,
	 * or `INDEX_NONE`.
	 */
	int32 PlaceADungeonSentBy(UCataclysmEmpireRun& Run, ECataclysmType Sender, int32 Floors,
							  const TArray<FName>& Rows = TArray<FName>())
	{
		if (!Run.Map || !Run.Clock)
		{
			return INDEX_NONE;
		}

		int32 HostId = INDEX_NONE;
		for (const FCataclysmCity& Candidate : Run.Map->Cities)
		{
			if (Candidate.Tier == ECataclysmCityTier::Outpost && !Candidate.bFallen)
			{
				HostId = Candidate.CityId;
				break;
			}
		}
		const FCataclysmCity* Host = Run.Map->Find(HostId);
		if (Host == nullptr)
		{
			return INDEX_NONE;
		}

		FCataclysmDungeon Placed;
		Placed.DungeonId = Run.NextDungeonId;
		Placed.Type = ECataclysmDungeonType::Basic;
		Placed.SubType = ECataclysmDungeonSubType::None;
		Placed.Cataclysm = Sender;
		Placed.CityId = HostId;
		Placed.CityTier = Host->Tier;
		Placed.Floors = Floors;
		Placed.ResolveDays = 1000.0f;
		Placed.SpawnedDay = Run.Day();
		Placed.Modifiers = Rows;

		Run.NextDungeonId = Placed.DungeonId + 1;
		Run.Dungeons.Add(Placed);
		if (!Run.Clock->AddDungeon(Placed.DungeonId, Placed.Floors)
			|| !Run.Clock->SetResolveDays(Placed.DungeonId, 1000.0f))
		{
			return INDEX_NONE;
		}
		return Placed.DungeonId;
	}

	/** Rewrites which Cataclysm sent a dungeon already on the board. Whether it was there to rewrite. */
	bool SendItFrom(UCataclysmEmpireRun& Run, int32 DungeonId, ECataclysmType Sender)
	{
		for (FCataclysmDungeon& Standing : Run.Dungeons)
		{
			if (Standing.DungeonId == DungeonId)
			{
				Standing.Cataclysm = Sender;
				return true;
			}
		}
		return false;
	}

	/**
	 * Every kind of creature the floor places at one fixed rung rather than a drawn one. A Herald (3) carries three
	 * modifiers, so a floor of them draws enough for a stray column to show up if one could.
	 */
	void EveryKindAtRung(ACataclysmDungeonGameMode& Mode, int32 Rung)
	{
		Mode.ImpRarityStep = Rung;
		Mode.HellhoundRarityStep = Rung;
		Mode.BruteRarityStep = Rung;
		Mode.AbyssalWardenRarityStep = Rung;
		Mode.CorruptedSentinelRarityStep = Rung;
		Mode.SuccubusRarityStep = Rung;
		Mode.GatekeeperRarityStep = Rung;
	}

	/** What the floor's creatures are, read against one damage type. */
	struct FCreatureCataclysmFloor
	{
		/** Creatures standing in the floor's list. */
		int32 Creatures = 0;

		/** Of those, the ones whose `DamageType` is the one asked about. */
		int32 OfTheType = 0;

		/** Of those, the ones the damage pipeline reads as that type (`UCataclysmSkillEffects::DamageTypeOf`). */
		int32 StrikeAsTheType = 0;

		/** Every modifier row they carry, counted by column. */
		int32 Rows = 0;
		int32 RowsOfTheType = 0;
		int32 RowsGeneric = 0;

		/** Each row of any other column, as "Row (Column)". */
		TArray<FString> Strays;

		/** Each creature's rows joined, and its class, in the order the floor placed them. */
		TArray<FString> ModifierLists;
		TArray<FString> Kinds;
	};

	FCreatureCataclysmFloor ReadTheFloor(const ACataclysmDungeonGameMode& Mode, FName Type)
	{
		FCreatureCataclysmFloor Out;
		const UDataTable* ModifierTable = UCataclysmEnemyModifiers::LoadEnemyModifierTable();
		for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Mode.FloorEnemies)
		{
			if (!IsValid(Enemy))
			{
				continue;
			}
			++Out.Creatures;
			Out.OfTheType += Enemy->DamageType == Type ? 1 : 0;
			Out.StrikeAsTheType += UCataclysmSkillEffects::DamageTypeOf(Enemy.Get()) == Type ? 1 : 0;
			Out.Kinds.Add(Enemy->GetClass()->GetName());

			TArray<FString> Names;
			for (const FName& Row : Enemy->ModifierRows)
			{
				Names.Add(Row.ToString());
				++Out.Rows;
				const FCataclysmEnemyModifierRow* Found = UCataclysmEnemyModifiers::FindRow(ModifierTable, Row);
				const FString Column = Found ? Found->CataclysmType : FString(TEXT("not in the table"));
				if (Column.Equals(Type.ToString(), ESearchCase::IgnoreCase))
				{
					++Out.RowsOfTheType;
				}
				else if (Column.Equals(UCataclysmEnemyModifiers::GenericCataclysm, ESearchCase::IgnoreCase))
				{
					++Out.RowsGeneric;
				}
				else
				{
					Out.Strays.Add(FString::Printf(TEXT("%s (%s)"), *Row.ToString(), *Column));
				}
			}
			Out.ModifierLists.Add(FString::Join(Names, TEXT("+")));
		}
		return Out;
	}

	/** One line of what a floor held, for the test's own log. */
	FString DescribeCreatureFloor(const TCHAR* Which, const FCreatureCataclysmFloor& Floor)
	{
		return FString::Printf(TEXT("%s: %d creatures, %d of the type and %d striking as it; %d rows, %d of the type, %d "
									"Generic, %d of another column%s%s"),
							   Which, Floor.Creatures, Floor.OfTheType, Floor.StrikeAsTheType, Floor.Rows,
							   Floor.RowsOfTheType, Floor.RowsGeneric, Floor.Strays.Num(),
							   Floor.Strays.IsEmpty() ? TEXT("") : TEXT(": "),
							   *FString::Join(Floor.Strays, TEXT(", ")));
	}
}

// ---------------------------------------------------------------------------
// The rules: the brief carries the sender, each sender names a damage type, and every roster is the Demonic seven
// ---------------------------------------------------------------------------

/**
 * THE SEAM, WITH NO WORLD. Four claims: the brief carries the dungeon's sender on every floor, and a dungeon nobody sent
 * carries none; `None` names no damage type to write, and each of the eight names the one its creatures draw by, spelled
 * as the enemy modifier table's column spells it (a misspelling would leave a creature drawing the Generic column alone);
 * every Cataclysm's roster is the very array the Demonic seven are, `None` included; and the same plan populated for a
 * dungeon Death sent and one nobody sent places the same creatures on the same cells, so no draw was added.
 *
 * THE CONTROL for the last: the floor holds creatures, so two empty lists cannot agree by being empty.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreatureCataclysmRulesTest,
	"Cataclysm.CreatureCataclysm.TheBriefCarriesTheSenderAndEveryRosterIsTheDemonicSevenForNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreatureCataclysmRulesTest::RunTest(const FString& Parameters)
{
	// 1. THE BRIEF CARRIES THE SENDER, ON EVERY FLOOR.
	FCataclysmDungeonIdentity SentByWar;
	SentByWar.DungeonSeed = 11;
	SentByWar.TotalFloors = 6;
	SentByWar.Cataclysm = ECataclysmType::War;
	for (int32 Floor = 1; Floor <= SentByWar.TotalFloors; ++Floor)
	{
		TestEqual(FString::Printf(TEXT("floor %d of a dungeon War sent carries War"), Floor),
				  static_cast<int32>(FCataclysmDungeonFloorRules::BriefFor(SentByWar, Floor).Cataclysm),
				  static_cast<int32>(ECataclysmType::War));
	}
	TestEqual(TEXT("a dungeon nobody sent carries nobody"),
			  static_cast<int32>(FCataclysmDungeonFloorRules::BriefFor(FCataclysmDungeonIdentity(), 1).Cataclysm),
			  static_cast<int32>(ECataclysmType::None));

	// 2. THE DAMAGE TYPE EACH SENDER NAMES.
	TestTrue(TEXT("nobody names no damage type to write"),
			 FCataclysmDungeonFloorRules::CreatureDamageTypeFor(ECataclysmType::None).IsNone());

	const UDataTable* ModifierTable = UCataclysmEnemyModifiers::LoadEnemyModifierTable();
	if (!TestNotNull(TEXT("the enemy modifier table loads"), ModifierTable))
	{
		return false;
	}
	for (const ECataclysmType One : UCataclysmRoster::All())
	{
		const FName Named = FCataclysmDungeonFloorRules::CreatureDamageTypeFor(One);
		TestEqual(FString::Printf(TEXT("%s's creatures deal %s"), *UCataclysmRoster::NameFor(One).ToString(),
								  *Named.ToString()),
				  Named, UCataclysmRoster::NameFor(One));

		int32 OwnRows = 0;
		for (const FName& Key : UCataclysmEnemyModifiers::PoolFor(ModifierTable, Named))
		{
			const FCataclysmEnemyModifierRow* Row = UCataclysmEnemyModifiers::FindRow(ModifierTable, Key);
			OwnRows += (Row && Row->CataclysmType.Equals(Named.ToString(), ESearchCase::IgnoreCase)) ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("and a %s creature's pool holds %d rows of its own column, so the name is the "
									  "table's spelling"), *Named.ToString(), OwnRows),
				 OwnRows > 0);
	}

	// 3. EVERY ROSTER IS THE DEMONIC SEVEN, THE SAME ARRAY.
	TArray<ECataclysmType> Senders = UCataclysmRoster::All();
	Senders.Add(ECataclysmType::None);
	for (const ECataclysmType One : Senders)
	{
		TestTrue(FString::Printf(TEXT("%s's roster is the Demonic seven, the same array"),
								 *UCataclysmRoster::NameFor(One).ToString()),
				 &FCataclysmFloorPopulator::PackKindsFor(One) == &FCataclysmFloorPopulator::PackKinds());
	}

	// 4. THE SAME SEED BUILDS THE SAME FLOOR WHOEVER SENT THE DUNGEON.
	FCataclysmFloorRequest Request;
	Request.DungeonSeed = 11;
	Request.FloorNumber = 2;
	Request.Layout = ECataclysmFloorLayout::Halls;
	const FCataclysmFloorPlan Plan = FCataclysmFloorGenerator::Generate(Request);
	if (!TestTrue(TEXT("set-up: the floor was carved"), Plan.IsBuilt()))
	{
		return false;
	}

	FCataclysmFloorBrief ByNobody;
	FCataclysmFloorBrief ByDeath;
	ByDeath.Cataclysm = ECataclysmType::Death;
	const FCataclysmFloorPopulation Nobodys = FCataclysmFloorPopulator::Populate(Plan, 1.0f, ByNobody);
	const FCataclysmFloorPopulation Deaths = FCataclysmFloorPopulator::Populate(Plan, 1.0f, ByDeath);

	TestTrue(TEXT("CONTROL: the floor holds creatures"), Nobodys.Enemies.Num() > 0);
	if (!TestEqual(TEXT("a floor of a dungeon Death sent holds as many creatures as one nobody sent"),
				   Deaths.Enemies.Num(), Nobodys.Enemies.Num()))
	{
		return false;
	}
	int32 Differ = 0;
	for (int32 Index = 0; Index < Nobodys.Enemies.Num(); ++Index)
	{
		const FCataclysmEnemyPlacement& A = Nobodys.Enemies[Index];
		const FCataclysmEnemyPlacement& B = Deaths.Enemies[Index];
		Differ += (A.Cell != B.Cell || A.Creature != B.Creature || A.Pack != B.Pack) ? 1 : 0;
	}
	TestEqual(TEXT("and the same creature on the same cell in the same group, every one"), Differ, 0);
	TestEqual(TEXT("in as many groups"), Deaths.PackCount, Nobodys.PackCount);
	return true;
}

// ---------------------------------------------------------------------------
// A dungeon's own creatures
// ---------------------------------------------------------------------------

/**
 * A DEATH DUNGEON'S CREATURES ARE DEATH'S: entered from the empire, its first floor's creatures all carry Death as their
 * damage type, the damage pipeline reads their blows as Death, and every modifier they drew is of Death's column or the
 * Generic one. Every kind is held at the Herald rung, three modifiers each, so the floor draws dozens.
 *
 * THE CONTROLS: the floor holds creatures; some of what they drew is Death's own (a pool of the Generic column alone, which
 * a sender spelled "None" would give, carries none); the bound dungeon's sender reaches the identity and the brief; and
 * leaving the dungeon forgets it, so the next floor walked with nothing behind it places Demonic creatures again.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreatureCataclysmDeathDungeonTest,
	"Cataclysm.CreatureCataclysm.ADeathDungeonsCreaturesDealDeathAndDrawOnlyDeathAndGenericModifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreatureCataclysmDeathDungeonTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCreatureCataclysmTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCreatureCataclysmScene Scene = MakeCreatureCataclysmScene(World, /*bStartPlay=*/false, /*Density=*/0.1f);
	if (!TestTrue(TEXT("set-up: a dungeon game mode and a run"), Scene.IsUsable()))
	{
		return false;
	}
	EveryKindAtRung(*Scene.Mode, /*Rung=*/3);

	const int32 DungeonId = PlaceADungeonSentBy(*Scene.Run, ECataclysmType::Death, /*Floors=*/3);
	if (!TestTrue(TEXT("set-up: a dungeon Death sent was placed"), DungeonId != INDEX_NONE)
		|| !TestTrue(TEXT("set-up: it was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
	{
		return false;
	}

	TestEqual(TEXT("the game mode knows Death sent it"), static_cast<int32>(Scene.Mode->DungeonCataclysm),
			  static_cast<int32>(ECataclysmType::Death));
	TestEqual(TEXT("and hands it to the floor rules"), static_cast<int32>(Scene.Mode->DungeonIdentity().Cataclysm),
			  static_cast<int32>(ECataclysmType::Death));
	TestEqual(TEXT("and the floor's brief carries it"), static_cast<int32>(Scene.Mode->FloorBrief.Cataclysm),
			  static_cast<int32>(ECataclysmType::Death));

	const FName DeathType(TEXT("Death"));
	const FCreatureCataclysmFloor DeathFloor = ReadTheFloor(*Scene.Mode, DeathType);
	AddInfo(DescribeCreatureFloor(TEXT("A DEATH DUNGEON'S FLOOR 1"), DeathFloor));

	if (!TestTrue(TEXT("CONTROL: the floor holds creatures"), DeathFloor.Creatures > 0))
	{
		return false;
	}
	TestEqual(TEXT("every creature deals Death"), DeathFloor.OfTheType, DeathFloor.Creatures);
	TestEqual(TEXT("and the damage pipeline reads every one's blows as Death"), DeathFloor.StrikeAsTheType,
			  DeathFloor.Creatures);
	TestEqual(TEXT("every modifier drawn is Death's or Generic"), DeathFloor.RowsOfTheType + DeathFloor.RowsGeneric,
			  DeathFloor.Rows);
	TestEqual(TEXT("so none is of another column"), DeathFloor.Strays.Num(), 0);
	TestTrue(TEXT("CONTROL: some of what they drew is Death's own"), DeathFloor.RowsOfTheType > 0);

	// AND LEAVING FORGETS THE SENDER.
	Scene.Mode->LeaveEmpireDungeon();
	TestEqual(TEXT("leaving forgets who sent the dungeon"), static_cast<int32>(Scene.Mode->DungeonCataclysm),
			  static_cast<int32>(ECataclysmType::None));
	TestEqual(TEXT("and the floor rules hear of nobody"), static_cast<int32>(Scene.Mode->DungeonIdentity().Cataclysm),
			  static_cast<int32>(ECataclysmType::None));
	return true;
}

/**
 * A DEMONIC DUNGEON'S CREATURES STAY DEMONIC, AND DRAW EXACTLY WHAT THEY DREW BEFORE. One dungeon, entered three times
 * with only its sender rewritten: by nobody (what every dungeon was before this slice), by Demonic, and by Death.
 *
 * Nobody's and Demonic's floors hold the same creatures, every one Demonic, carrying the same modifiers in the same
 * order, every row Demonic or Generic: a Demonic dungeon is untouched, and no draw was added to any stream.
 *
 * THE CONTROLS: the floor holds creatures and some drew a Demonic row; and the same dungeon sent by Death holds the same
 * kinds in the same order (the Demonic seven stand in) carrying DIFFERENT modifiers, so the equality above is the sender
 * at work and not a floor that ignores it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreatureCataclysmDemonicDungeonTest,
	"Cataclysm.CreatureCataclysm.ADemonicDungeonsCreaturesStayDemonicAndDrawWhatTheyDrewBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreatureCataclysmDemonicDungeonTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCreatureCataclysmTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCreatureCataclysmScene Scene = MakeCreatureCataclysmScene(World, /*bStartPlay=*/false, /*Density=*/0.1f);
	if (!TestTrue(TEXT("set-up: a dungeon game mode and a run"), Scene.IsUsable()))
	{
		return false;
	}
	EveryKindAtRung(*Scene.Mode, /*Rung=*/3);

	const int32 DungeonId = PlaceADungeonSentBy(*Scene.Run, ECataclysmType::None, /*Floors=*/3);
	if (!TestTrue(TEXT("set-up: a dungeon nobody sent was placed"), DungeonId != INDEX_NONE))
	{
		return false;
	}

	const FName DemonicType(TEXT("Demonic"));
	const FName DeathType(TEXT("Death"));
	const auto Walk = [this, &Scene, DungeonId](ECataclysmType Sender, FName ReadAs,
												FCreatureCataclysmFloor& OutFloor) -> bool
	{
		if (!TestTrue(TEXT("set-up: the sender was rewritten"), SendItFrom(*Scene.Run, DungeonId, Sender))
			|| !TestTrue(TEXT("set-up: the dungeon was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
		{
			return false;
		}
		OutFloor = ReadTheFloor(*Scene.Mode, ReadAs);
		Scene.Mode->LeaveEmpireDungeon();
		return true;
	};

	FCreatureCataclysmFloor ByNobody;
	FCreatureCataclysmFloor ByDemonic;
	FCreatureCataclysmFloor ByDeath;
	if (!Walk(ECataclysmType::None, DemonicType, ByNobody) || !Walk(ECataclysmType::Demonic, DemonicType, ByDemonic)
		|| !Walk(ECataclysmType::Death, DeathType, ByDeath))
	{
		return false;
	}
	AddInfo(DescribeCreatureFloor(TEXT("SENT BY NOBODY"), ByNobody));
	AddInfo(DescribeCreatureFloor(TEXT("SENT BY DEMONIC"), ByDemonic));
	AddInfo(DescribeCreatureFloor(TEXT("SENT BY DEATH"), ByDeath));

	if (!TestTrue(TEXT("CONTROL: the floor holds creatures"), ByNobody.Creatures > 0))
	{
		return false;
	}

	// NOBODY: EVERY CREATURE ITS CLASS'S OWN, DEMONIC.
	TestEqual(TEXT("a dungeon nobody sent places Demonic creatures"), ByNobody.OfTheType, ByNobody.Creatures);
	TestEqual(TEXT("which draw Demonic and Generic rows only"), ByNobody.Strays.Num(), 0);
	TestTrue(TEXT("CONTROL: some drew a Demonic row"), ByNobody.RowsOfTheType > 0);

	// DEMONIC: THE SAME FLOOR, DRAW FOR DRAW.
	TestEqual(TEXT("a Demonic dungeon places Demonic creatures"), ByDemonic.OfTheType, ByDemonic.Creatures);
	TestEqual(TEXT("which draw Demonic and Generic rows only"), ByDemonic.Strays.Num(), 0);
	TestTrue(TEXT("the same kinds as a dungeon nobody sent, in the same order"), ByDemonic.Kinds == ByNobody.Kinds);
	TestTrue(TEXT("carrying the same modifiers in the same order, so no draw was added"),
			 ByDemonic.ModifierLists == ByNobody.ModifierLists);

	// DEATH: THE SAME BODIES, OTHER MODIFIERS.
	TestTrue(TEXT("CONTROL: a dungeon Death sent places the same kinds in the same order"),
			 ByDeath.Kinds == ByNobody.Kinds);
	TestEqual(TEXT("CONTROL: every one of them Death's"), ByDeath.OfTheType, ByDeath.Creatures);
	TestTrue(TEXT("CONTROL: and carrying other modifiers, so the sender is what the equality above held"),
			 ByDeath.ModifierLists != ByNobody.ModifierLists);
	return true;
}

// ---------------------------------------------------------------------------
// A creature a rule raises, and a creature a creature calls
// ---------------------------------------------------------------------------

/**
 * A CREATURE A FLOOR RULE RAISES IN A WAR DUNGEON IS WAR'S: The Reaper, an Abyssal Warden `Death_The_Reaper` raises at
 * the entrance ten seconds into a floor, placed through `SpawnPlacedCreature`, deals War in a dungeon War sent. It is a
 * Death row, so this is also the judgement that a rule's creature belongs to the dungeon's sender and not to the
 * Cataclysm the row comes from (`docs/DECISIONS.md`, 2026-11-25).
 *
 * THE CONTROL: the same rule in a dungeon nobody sent raises a Reaper of its class's own type, Demonic.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreatureCataclysmRuleArrivalTest,
	"Cataclysm.CreatureCataclysm.ACreatureARuleRaisesInAWarDungeonDealsWar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreatureCataclysmRuleArrivalTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCreatureCataclysmTest;
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCreatureCataclysmPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player with an ability system"), Player.IsUsable()))
	{
		return false;
	}
	const FCreatureCataclysmScene Scene = MakeCreatureCataclysmScene(World, /*bStartPlay=*/true, /*Density=*/0.0f);
	if (!TestTrue(TEXT("set-up: a dungeon game mode and a run"), Scene.IsUsable()))
	{
		return false;
	}

	const TArray<FName> TheReaperAlone = {FName(Effects::TheReaperKey)};
	const auto TheReaperIn = [this, &Scene, &TheReaperAlone](ECataclysmType Sender) -> ACataclysmEnemyCharacter*
	{
		const int32 DungeonId = PlaceADungeonSentBy(*Scene.Run, Sender, /*Floors=*/3, TheReaperAlone);
		if (!TestTrue(TEXT("set-up: a dungeon carrying The Reaper was placed"), DungeonId != INDEX_NONE)
			|| !TestTrue(TEXT("set-up: it was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId))
			|| !TestTrue(TEXT("set-up: floor 2 was reached"), Scene.Mode->GoToFloor(2)))
		{
			return nullptr;
		}
		Scene.Mode->ClearFloorEnemies();
		const int32 BeatsToTheReaper = FMath::CeilToInt(
			Effects::TheReaperDelaySeconds / ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
		for (int32 BeatIndex = 0; BeatIndex < BeatsToTheReaper; ++BeatIndex)
		{
			Scene.Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
		}
		ACataclysmEnemyCharacter* Reaper = Scene.Mode->TheReaperOnTheFloor();
		return TestNotNull(TEXT("set-up: the Reaper came"), Reaper) ? Reaper : nullptr;
	};

	const ACataclysmEnemyCharacter* WarsReaper = TheReaperIn(ECataclysmType::War);
	if (!WarsReaper)
	{
		return false;
	}
	TestTrue(TEXT("set-up: it is a creature the rule raised"), WarsReaper->bRaisedByARule);
	TestEqual(TEXT("the Reaper raised in a dungeon War sent deals War"), WarsReaper->DamageType, FName(TEXT("War")));
	TestEqual(TEXT("and the damage pipeline reads its blows as War"),
			  UCataclysmSkillEffects::DamageTypeOf(WarsReaper), FName(TEXT("War")));
	Scene.Mode->LeaveEmpireDungeon();

	const ACataclysmEnemyCharacter* NobodysReaper = TheReaperIn(ECataclysmType::None);
	if (!NobodysReaper)
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: the Reaper raised in a dungeon nobody sent is Demonic, its class's own"),
			  NobodysReaper->DamageType, FName(TEXT("Demonic")));
	return true;
}

/**
 * THE GATEKEEPER'S CALLED IMPS ARE ITS CATACLYSM'S. The Gatekeeper the population stands at the exit of a dungeon War
 * sent is War's, and the Imps its Call the Damned brings deal War too: they are not placed by the floor, so only the
 * Gatekeeper can give it to them.
 *
 * THE CONTROL: a Gatekeeper spawned in the same world outside any floor is Demonic, and so are the Imps it calls.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreatureCataclysmCalledImpsTest,
	"Cataclysm.CreatureCataclysm.TheGatekeepersCalledImpsTakeItsCataclysm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreatureCataclysmCalledImpsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCreatureCataclysmTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE BOSS AND NOTHING ELSE: a one-floor dungeon's only floor is its last, and a density of nought places the boss
	// alone (`FCataclysmFloorPopulator::Populate` places it before it asks the density for anything).
	const FCreatureCataclysmScene Scene = MakeCreatureCataclysmScene(World, /*bStartPlay=*/false, /*Density=*/0.0f);
	if (!TestTrue(TEXT("set-up: a dungeon game mode and a run"), Scene.IsUsable()))
	{
		return false;
	}
	const int32 DungeonId = PlaceADungeonSentBy(*Scene.Run, ECataclysmType::War, /*Floors=*/1);
	if (!TestTrue(TEXT("set-up: a dungeon War sent was placed"), DungeonId != INDEX_NONE)
		|| !TestTrue(TEXT("set-up: it was entered"), Scene.Mode->EnterEmpireDungeon(DungeonId)))
	{
		return false;
	}

	ACataclysmGatekeeperCharacter* WarsGatekeeper = nullptr;
	for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : Scene.Mode->FloorEnemies)
	{
		if (IsValid(Enemy) && Enemy->IsA<ACataclysmGatekeeperCharacter>())
		{
			WarsGatekeeper = Cast<ACataclysmGatekeeperCharacter>(Enemy.Get());
		}
	}
	if (!TestNotNull(TEXT("set-up: the Gatekeeper stands at the exit"), WarsGatekeeper))
	{
		return false;
	}
	TestEqual(TEXT("the floor's Gatekeeper deals War"), WarsGatekeeper->DamageType, FName(TEXT("War")));

	// THE IMPS EACH ONE CALLS, by its own cast, read off the list it keeps of them.
	const auto CalledTypes = [](ACataclysmGatekeeperCharacter& Caller, FName Wanted, int32& OutCalled, int32& OutOfType)
	{
		Caller.UseEnemyAbility(ACataclysmGatekeeperCharacter::CallTheDamnedAbility, nullptr,
							   Caller.GetActorLocation());
		OutCalled = 0;
		OutOfType = 0;
		for (const TWeakObjectPtr<ACataclysmImpCharacter>& Weak : Caller.CalledImps)
		{
			if (const ACataclysmImpCharacter* Imp = Weak.Get())
			{
				++OutCalled;
				OutOfType += Imp->DamageType == Wanted ? 1 : 0;
			}
		}
	};

	int32 WarsCalled = 0;
	int32 WarsOfWar = 0;
	CalledTypes(*WarsGatekeeper, FName(TEXT("War")), WarsCalled, WarsOfWar);
	if (!TestTrue(TEXT("set-up: the floor's Gatekeeper called Imps"), WarsCalled > 0))
	{
		return false;
	}
	TestEqual(TEXT("and every Imp it called deals War"), WarsOfWar, WarsCalled);

	// THE CONTROL, FAR FROM THE FIRST so neither one's Imps land among the other's.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmGatekeeperCharacter* Unsent = World->SpawnActor<ACataclysmGatekeeperCharacter>(
		ACataclysmGatekeeperCharacter::StaticClass(), FVector(30000.0f, 30000.0f, 0.0f), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("set-up: a Gatekeeper outside any floor"), Unsent))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: a Gatekeeper outside any floor is Demonic"), Unsent->DamageType, FName(TEXT("Demonic")));
	int32 UnsentCalled = 0;
	int32 UnsentOfDemonic = 0;
	CalledTypes(*Unsent, FName(TEXT("Demonic")), UnsentCalled, UnsentOfDemonic);
	TestTrue(TEXT("CONTROL: it called Imps"), UnsentCalled > 0);
	TestEqual(TEXT("CONTROL: and every one is Demonic"), UnsentOfDemonic, UnsentCalled);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
