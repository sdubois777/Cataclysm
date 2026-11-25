// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dom/JsonObject.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmEmpireTree.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"

/**
 * The empire upgrade tree's slice 5b: Relentless Pursuit and the four drop rules. Issue #2741, each reading a judgement
 * of the coordinating session's D5, D6 and E1 to E4 of 2026-11-19.
 *
 *   Relentless Pursuit     +5% loot quantity a point for each dungeon cleared since the last death, to +100% (D5); an
 *                          Explorer figure, doubled by One-Day Specialist after its cap
 *   Opportunistic Looting  +3% a point: an Elite's kill drops one more gear item
 *   Plunderer's Eye        +2% a point: any kill drops one more crafting material
 *   Scavenger              +5% a point: each tier 1 to 3 crafting material a kill drops is a stack of two (E2)
 *   The High Roller        5% for each whole ten floors added: a Boss's kill drops one more Legendary or rarer item
 *
 * The run counts the clears since the last death (`UCataclysmEmpireRun::ClearsSinceLastDeath`), saved at run record
 * version 11. `Cataclysm.EmpireTreeDrops.` IS A NEW GROUP and selects these and nothing else.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1 of the slice: every figure below is reasoned from the code, and the run that proves
 * them is registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmEmpireTreeDropsTest
{
	// The tree file's identifiers, by the node names the table gives them.
	const FName RelentlessPursuit(TEXT("basic_expl_t3c"));		// +5% loot quantity a point per clean clear, 10 points
	const FName OpportunisticLooting(TEXT("basic_expl_t1c"));	// +3% a point, Elite extra item, 8 points
	const FName PlunderersEye(TEXT("basic_expl_t4g"));			// +2% a point, extra material, 10 points
	const FName ScavengerNode(TEXT("basic_0000004z"));			// Artisan, +5% a point, tier 1-3 stack of two, 10 points
	const FName HighRoller(TEXT("keystone_0000002p"));			// 5% per whole 10 floors added, a keystone

	FCataclysmEmpireTreePlayerBonuses DropBonusesOf(const FCataclysmPassiveAllocation& Tree)
	{
		return UCataclysmEmpireTree::PlayerBonusesFor(UCataclysmEmpireTree::LoadNodeTable(),
													  UCataclysmEmpireTree::LoadEffectTable(), Tree);
	}

	/** A run begun on this seed, its first day spent and its board emptied, as the earlier slices' tests leave one. */
	UCataclysmEmpireRun* QuietDropsRun(int32 Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	/** A Basic dungeon on the run's first city with a timer, as a surge would put one, for clearing. */
	void PlaceDropsDungeon(UCataclysmEmpireRun& Run, int32 DungeonId)
	{
		const FCataclysmCity& City = Run.Map->Cities[0];
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.Type = ECataclysmDungeonType::Basic;
		Dungeon.CityId = City.CityId;
		Dungeon.CityTier = City.Tier;
		Dungeon.Floors = 20;
		Dungeon.ResolveDays = 50.0f;
		Dungeon.SpawnedDay = Run.Day();
		Dungeon.DefenceDamage = 100.0f;
		Dungeon.PopulationDamage = 400.0f;
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, Dungeon.ResolveDays);
	}

	/** A walk of these days from a default of these, with these floors added, every other count nought. */
	FCataclysmEmpireTreeDungeonState DropsWalkOf(float DefaultWalkDays, float WalkDays, int32 FloorsAdded = 0)
	{
		FCataclysmEmpireTreeDungeonState State;
		State.DefaultWalkDays = DefaultWalkDays;
		State.WalkDays = WalkDays;
		State.FloorsAdded = FloorsAdded;
		return State;
	}

	/** A run file read from the committed fixtures, or null after saying why. */
	UCataclysmRunSave* ReadDropsRunFixture(FAutomationTestBase& Test, const FString& Text, const TCHAR* Name)
	{
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		FString Message;
		UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
		if (Read == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s would not load: %s -- %s"), Name,
										  FCataclysmSaveStorage::Describe(Result), *Message));
		}
		return Read;
	}

	/** The drops lying in a world: gear items, crafting material drops, and the material drops' quantities by tier. */
	struct FDropsOnTheFloor
	{
		int32 Gear = 0;
		int32 Materials = 0;
		TArray<const ACataclysmDroppedItem*> GearDrops;
		TArray<const ACataclysmDroppedItem*> MaterialDrops;
	};

	FDropsOnTheFloor DropsIn(UWorld* World)
	{
		FDropsOnTheFloor Found;
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			if (!IsValid(*It))
			{
				continue;
			}
			if (It->IsMaterial())
			{
				++Found.Materials;
				Found.MaterialDrops.Add(*It);
			}
			else if (!It->Item.Base.IsNone())
			{
				++Found.Gear;
				Found.GearDrops.Add(*It);
			}
		}
		return Found;
	}

	/** One of each chance, every other nought. */
	FCataclysmEmpireTreeDropRules OnlyRule(float Elite, float Material, float LowTier, float Boss)
	{
		FCataclysmEmpireTreeDropRules Rules;
		Rules.EliteExtraItemChance = Elite;
		Rules.ExtraMaterialChance = Material;
		Rules.LowTierMaterialExtraChance = LowTier;
		Rules.BossLegendaryChance = Boss;
		return Rules;
	}
}

// ---------------------------------------------------------------------------
// The table, Relentless Pursuit and the chances
// ---------------------------------------------------------------------------

/**
 * The five nodes add up through the generated tables at full points: Relentless Pursuit 0.50 a clean clear to its 1.00
 * cap, Opportunistic Looting 0.24, Plunderer's Eye 0.20, Scavenger 0.50, The High Roller 0.05 for each ten floors.
 * CONTROL: an allocation without them gives none of the six figures.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsTableTest,
	"Cataclysm.EmpireTreeDrops.TheFiveNodesAddUpFromTheirRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsTableTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	if (!TestNotNull(TEXT("set-up: the node table"), UCataclysmEmpireTree::LoadNodeTable())
		|| !TestNotNull(TEXT("set-up: the effect table"), UCataclysmEmpireTree::LoadEffectTable()))
	{
		return false;
	}
	FCataclysmPassiveAllocation Tree;
	Tree.Add(RelentlessPursuit, 10);
	Tree.Add(OpportunisticLooting, 8);
	Tree.Add(PlunderersEye, 10);
	Tree.Add(ScavengerNode, 10);
	Tree.Add(HighRoller, 1);
	const FCataclysmEmpireTreePlayerBonuses Held = DropBonusesOf(Tree);
	const FCataclysmEmpireTreePlayerBonuses None = DropBonusesOf(FCataclysmPassiveAllocation());

	AddInfo(FString::Printf(TEXT("DROPS TABLE: per clean clear %.2f to %.2f, Elite %.2f, material %.2f, low tier %.2f, "
								 "boss per ten floors %.2f"),
							Held.LootQuantityPerCleanClear, Held.CleanClearCap, Held.EliteExtraItemChance,
							Held.ExtraMaterialChance, Held.LowTierMaterialExtraChance,
							Held.BossLegendaryChancePerTenFloorsAdded));
	TestEqual(TEXT("Relentless Pursuit: 10 points x 5%"), Held.LootQuantityPerCleanClear, 0.50f, 0.0001f);
	TestEqual(TEXT("to a cap of +100% (D5)"), Held.CleanClearCap, 1.00f, 0.0001f);
	TestEqual(TEXT("Opportunistic Looting: 8 points x 3%"), Held.EliteExtraItemChance, 0.24f, 0.0001f);
	TestEqual(TEXT("Plunderer's Eye: 10 points x 2%"), Held.ExtraMaterialChance, 0.20f, 0.0001f);
	TestEqual(TEXT("Scavenger: 10 points x 5%"), Held.LowTierMaterialExtraChance, 0.50f, 0.0001f);
	TestEqual(TEXT("The High Roller: 5% for each ten floors"), Held.BossLegendaryChancePerTenFloorsAdded, 0.05f,
			  0.0001f);
	TestEqual(TEXT("CONTROL: none bought, no clean clear figure"), None.LootQuantityPerCleanClear, 0.0f);
	TestEqual(TEXT("CONTROL: and no chance"),
			  None.EliteExtraItemChance + None.ExtraMaterialChance + None.LowTierMaterialExtraChance
				  + None.BossLegendaryChancePerTenFloorsAdded,
			  0.0f);
	return true;
}

/**
 * RELENTLESS PURSUIT COUNTS THE CLEARS SINCE THE LAST DEATH, TO ITS CAP. At 4 points (+20% a clear): no clear +0%, three
 * +60%, five +100%, seven still +100%; and at a one-day walk from a longer default, seven are doubled after the cap,
 * +200%, as One-Day Specialist doubles Speed Runner.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsPursuitTest,
	"Cataclysm.EmpireTreeDrops.RelentlessPursuitCountsTheClearsSinceTheLastDeathToItsCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsPursuitTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.LootQuantityPerCleanClear = 0.20f;
	Bonuses.CleanClearCap = 1.00f;
	const auto At = [&Bonuses](int32 Clears, float DefaultWalk, float Walk)
	{
		FCataclysmEmpireTreeDungeonState State = DropsWalkOf(DefaultWalk, Walk);
		State.ClearsSinceLastDeath = Clears;
		return UCataclysmEmpireTree::LootQuantityFor(Bonuses, State);
	};

	TestEqual(TEXT("no clear since the last death: +0%"), At(0, 10.0f, 10.0f), 0.0f, 0.01f);
	TestEqual(TEXT("three: +60%"), At(3, 10.0f, 10.0f), 60.0f, 0.01f);
	TestEqual(TEXT("five: +100%"), At(5, 10.0f, 10.0f), 100.0f, 0.01f);
	TestEqual(TEXT("seven: still +100%, the cap"), At(7, 10.0f, 10.0f), 100.0f, 0.01f);
	Bonuses.ExplorerLootDoubledAtWalkDays = 1;
	TestEqual(TEXT("seven at a one-day walk from ten: +200%, doubled after the cap"), At(7, 10.0f, 1.0f), 200.0f, 0.01f);
	return true;
}

/**
 * THE FOUR CHANCES ARE THE NODES' FIGURES, DOUBLED AT ONE DAY WHERE THE BRANCH IS EXPLORER, AND HELD AT ONE. With 25
 * floors added: Elite 0.24, material 0.20, low tier 0.50, boss 0.10 (two whole steps of ten); with 9 added the boss's is
 * 0. At a one-day walk the three Explorer chances double -- 0.48, 0.40, 0.20 -- and Scavenger's, an Artisan node's,
 * stays 0.50. An Elite chance of 0.80 doubled is held at 1.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsChancesTest,
	"Cataclysm.EmpireTreeDrops.TheFourChancesAreDoubledAtOneDayWhereExplorerAndHeldAtOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsChancesTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.EliteExtraItemChance = 0.24f;
	Bonuses.ExtraMaterialChance = 0.20f;
	Bonuses.LowTierMaterialExtraChance = 0.50f;
	Bonuses.BossLegendaryChancePerTenFloorsAdded = 0.05f;

	const FCataclysmEmpireTreeDropRules Walked = UCataclysmEmpireTree::DropRulesFor(Bonuses, DropsWalkOf(10.0f, 10.0f, 25));
	TestEqual(TEXT("Elite 0.24"), Walked.EliteExtraItemChance, 0.24f, 0.0001f);
	TestEqual(TEXT("material 0.20"), Walked.ExtraMaterialChance, 0.20f, 0.0001f);
	TestEqual(TEXT("low tier 0.50"), Walked.LowTierMaterialExtraChance, 0.50f, 0.0001f);
	TestEqual(TEXT("25 floors added: two whole steps, 0.10"), Walked.BossLegendaryChance, 0.10f, 0.0001f);
	TestEqual(TEXT("9 floors added: no whole step, 0"),
			  UCataclysmEmpireTree::DropRulesFor(Bonuses, DropsWalkOf(10.0f, 10.0f, 9)).BossLegendaryChance, 0.0f);

	Bonuses.ExplorerLootDoubledAtWalkDays = 1;
	const FCataclysmEmpireTreeDropRules OneDay = UCataclysmEmpireTree::DropRulesFor(Bonuses, DropsWalkOf(10.0f, 1.0f, 25));
	TestEqual(TEXT("one day: Elite doubled, 0.48"), OneDay.EliteExtraItemChance, 0.48f, 0.0001f);
	TestEqual(TEXT("material doubled, 0.40"), OneDay.ExtraMaterialChance, 0.40f, 0.0001f);
	TestEqual(TEXT("Scavenger is Artisan and stays 0.50"), OneDay.LowTierMaterialExtraChance, 0.50f, 0.0001f);
	TestEqual(TEXT("boss doubled, 0.20"), OneDay.BossLegendaryChance, 0.20f, 0.0001f);

	Bonuses.EliteExtraItemChance = 0.80f;
	TestEqual(TEXT("0.80 doubled is held at 1"),
			  UCataclysmEmpireTree::DropRulesFor(Bonuses, DropsWalkOf(10.0f, 1.0f)).EliteExtraItemChance, 1.0f);
	return true;
}

/**
 * THE GAME MODE ANSWERS THE DROP RULES FOR THE BOUND DUNGEON, AND NONE WITHOUT ONE. A game mode with the tree's bonuses
 * set: before a dungeon is entered, none; walking a dungeon of 30 floors whose default is 10 (20 added), Elite 0.24 and
 * the boss's 0.10; the same through the world, as a kill asks; and none again after leaving.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsBoundTest,
	"Cataclysm.EmpireTreeDrops.TheGameModeAnswersTheDropRulesForTheBoundDungeonOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsBoundTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("set-up: the dungeon game mode"), Mode))
	{
		return false;
	}
	Mode->StartPlay();
	UCataclysmEmpireRun* Run = QuietDropsRun(2741);
	Mode->SetEmpireRunForTests(Run);
	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.EliteExtraItemChance = 0.24f;
	Bonuses.BossLegendaryChancePerTenFloorsAdded = 0.05f;
	Mode->SetEmpireTreePlayerBonusesForTests(Bonuses);

	TestFalse(TEXT("no dungeon bound: no chance"), Mode->EmpireTreeDropRulesHere().RollsAnything());

	const FCataclysmCity& City = Run->Map->Cities[0];
	FCataclysmDungeon Dungeon;
	Dungeon.DungeonId = Run->NextDungeonId;
	Dungeon.Type = ECataclysmDungeonType::Basic;
	Dungeon.CityId = City.CityId;
	Dungeon.CityTier = City.Tier;
	Dungeon.Floors = 30;
	Dungeon.UpgradeFloors = 20;
	Dungeon.Bosses = 1;
	Dungeon.ResolveDays = 40.0f;
	Dungeon.SpawnedDay = Run->Day();
	Run->NextDungeonId = Dungeon.DungeonId + 1;
	Run->Dungeons.Add(Dungeon);
	if (!TestTrue(TEXT("set-up: its timer"), Run->Clock->AddDungeon(Dungeon.DungeonId, Dungeon.Floors)
				  && Run->Clock->SetResolveDays(Dungeon.DungeonId, Dungeon.ResolveDays))
		|| !TestTrue(TEXT("set-up: the dungeon was entered"), Mode->EnterEmpireDungeon(Dungeon.DungeonId)))
	{
		return false;
	}

	const FCataclysmEmpireTreeDropRules Here = Mode->EmpireTreeDropRulesHere();
	const FCataclysmEmpireTreeDropRules Through = ACataclysmDungeonGameMode::EmpireTreeDropRulesIn(World);
	AddInfo(FString::Printf(TEXT("DROPS BOUND: here Elite %.2f boss %.2f; through the world Elite %.2f boss %.2f"),
							Here.EliteExtraItemChance, Here.BossLegendaryChance, Through.EliteExtraItemChance,
							Through.BossLegendaryChance));
	TestEqual(TEXT("walking it: Elite 0.24"), Here.EliteExtraItemChance, 0.24f, 0.0001f);
	TestEqual(TEXT("and 20 floors added, two steps: 0.10"), Here.BossLegendaryChance, 0.10f, 0.0001f);
	TestEqual(TEXT("through the world, as a kill asks: Elite 0.24"), Through.EliteExtraItemChance, 0.24f, 0.0001f);
	TestEqual(TEXT("and 0.10"), Through.BossLegendaryChance, 0.10f, 0.0001f);

	Mode->LeaveEmpireDungeon();
	TestFalse(TEXT("left: no chance"), Mode->EmpireTreeDropRulesHere().RollsAnything());
	TestFalse(TEXT("nor through the world"), ACataclysmDungeonGameMode::EmpireTreeDropRulesIn(World).RollsAnything());
	return true;
}

// ---------------------------------------------------------------------------
// The run's clears since the last death, and run record version 11
// ---------------------------------------------------------------------------

/**
 * THE CLEAN CLEAR COUNT RISES WITH EVERY CLEAR AND ENDS AT A NOTED DEATH AND A NEW RUN. Two clears: 2. A death noted: 0,
 * while the run's clears stay 2. Another clear: 1. A new run: 0.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsStreakTest,
	"Cataclysm.EmpireTreeDrops.TheCleanClearCountRisesWithClearsAndEndsAtADeathAndANewRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsStreakTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	UCataclysmEmpireRun* Run = QuietDropsRun(2741);
	PlaceDropsDungeon(*Run, 9300);
	PlaceDropsDungeon(*Run, 9301);
	PlaceDropsDungeon(*Run, 9302);
	if (!TestTrue(TEXT("set-up: two clears"), Run->ClearDungeon(9300) && Run->ClearDungeon(9301)))
	{
		return false;
	}
	const int32 AfterTwo = Run->ClearsSinceLastDeath;
	Run->NoteDeath(Run->Day(), TEXT("a test dungeon"), 3, 5, TArray<int32>());
	const int32 AfterDeath = Run->ClearsSinceLastDeath;
	const int32 ClearsAfterDeath = Run->DungeonsCleared;
	if (!TestTrue(TEXT("set-up: a third clear"), Run->ClearDungeon(9302)))
	{
		return false;
	}
	const int32 AfterThird = Run->ClearsSinceLastDeath;
	Run->Begin(2742);

	AddInfo(FString::Printf(TEXT("CLEAN CLEARS: %d after two clears, %d after a death (run clears %d), %d after another, "
								 "%d in a new run"),
							AfterTwo, AfterDeath, ClearsAfterDeath, AfterThird, Run->ClearsSinceLastDeath));
	TestEqual(TEXT("two clears: 2"), AfterTwo, 2);
	TestEqual(TEXT("a death noted: 0"), AfterDeath, 0);
	TestEqual(TEXT("while the run's clears stay 2"), ClearsAfterDeath, 2);
	TestEqual(TEXT("another clear: 1"), AfterThird, 1);
	TestEqual(TEXT("a new run: 0"), Run->ClearsSinceLastDeath, 0);
	return true;
}

/**
 * A SAVED RUN KEEPS ITS CLEAN CLEARS: 4, written out by the save and read back into a fresh run.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsSavedTest,
	"Cataclysm.EmpireTreeDrops.ASavedRunKeepsItsClearsSinceTheLastDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsSavedTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	UCataclysmEmpireRun* Run = QuietDropsRun(2741);
	Run->ClearsSinceLastDeath = 4;

	UCataclysmRunSave* Record = NewObject<UCataclysmRunSave>();
	Record->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;
	FString Why;
	FCataclysmSaveGather::RunClockFrom(*Run, *Record);
	FCataclysmSaveGather::CitiesFrom(*Run, *Record);
	FCataclysmSaveGather::SurgeScheduleFrom(*Run, *Record);
	FCataclysmSaveGather::ProgressFrom(*Run, *Record);
	FCataclysmSaveGather::IdentityFrom(*Run, *Record);
	FString Json;
	const bool bWritten = FCataclysmSaveGather::DungeonsFrom(*Run, *Record, Why)
		&& FCataclysmSaveStorage::ToJson(Record, Json, Why);
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	UCataclysmRunSave* Read = bWritten ? Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Json, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Why)) : nullptr;
	if (!TestNotNull(TEXT("set-up: the record was written and read back"), Read))
	{
		AddError(Why);
		return false;
	}
	UCataclysmEmpireRun* Loaded = NewObject<UCataclysmEmpireRun>();
	const bool bLoaded = FCataclysmSaveApply::RunInto(*Read, *Loaded, Why);

	AddInfo(FString::Printf(TEXT("CLEAN CLEARS SAVED: the file holds 4 %s; loaded %s with %d"),
							Json.Contains(TEXT("\"ClearsSinceLastDeath\": 4")) ? TEXT("yes") : TEXT("NO"),
							bLoaded ? TEXT("yes") : *Why, Loaded->ClearsSinceLastDeath));
	TestTrue(TEXT("set-up: it loaded"), bLoaded);
	TestTrue(TEXT("the file holds the count"), Json.Contains(TEXT("\"ClearsSinceLastDeath\": 4")));
	TestEqual(TEXT("4 clears since the last death"), Loaded->ClearsSinceLastDeath, 4);
	return true;
}

/**
 * `Run_v10.json`, written before the count, migrates to the clears it can prove: it records a death, on day 103, so 0;
 * the same file with no death recorded gets its 14 clears, every one made without dying. Every other field is kept.
 * CONTROL: the committed run file, `Run_v19.json` since issue #51 (`Run_v18.json` from issue #2848, `Run_v17.json` from issue #2840, `Run_v16.json` from issue #2838, `Run_v15.json` from issue #2741 slice 9, `Run_v14.json` from issue #46, `Run_v13.json` from issue #2825, `Run_v12.json` from slice 6b), reads 5. Prints "CLEAN CLEARS MIGRATED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsMigrationTest,
	"Cataclysm.EmpireTreeDrops.ARunFileFromVersion10MigratesWithTheClearsItCanProve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsMigrationTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	FString Text;
	FString CurrentText;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v10.json"), Text, Reason)
		|| !CataclysmSaveFixtures::Read(TEXT("Run_v19.json"), CurrentText, Reason))
	{
		AddError(Reason);
		return false;
	}
	const FString DeathLine = TEXT("\"LastDeathDungeonName\": \"the Siege dungeon on Outpost (-3,0)\"");
	if (!TestTrue(TEXT("set-up: Run_v10.json records the death on day 103"), Text.Contains(DeathLine)))
	{
		return false;
	}
	const FString NoDeathText = Text.Replace(*DeathLine, TEXT("\"LastDeathDungeonName\": \"\""));

	const TSharedPtr<FJsonObject> Stepped = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Run_v10.json is a JSON object with no clean clear count"),
				  Stepped.IsValid() && !Stepped->HasField(TEXT("ClearsSinceLastDeath"))))
	{
		return false;
	}
	FString StepMessage;
	const ECataclysmSaveMigrationResult Stepping = FCataclysmSaveMigration::Migrate(
		GetDefault<UCataclysmRunSave>()->MigrationSteps(), UCataclysmRunSave::SchemaVersionNow, Stepped.ToSharedRef(),
		StepMessage);
	double Written = -1.0;
	const bool bWritten = Stepped->TryGetNumberField(TEXT("ClearsSinceLastDeath"), Written);

	const UCataclysmRunSave* Died = ReadDropsRunFixture(*this, Text, TEXT("Run_v10.json"));
	const UCataclysmRunSave* NoDeath = ReadDropsRunFixture(*this, NoDeathText, TEXT("Run_v10.json with no death"));
	const UCataclysmRunSave* Current = ReadDropsRunFixture(*this, CurrentText, TEXT("Run_v19.json"));
	if (!Died || !NoDeath || !Current)
	{
		return false;
	}

	// EVERY OTHER FIELD KEPT: the version 10 file at the current version with what the step writes, compared with what
	// the record writes back.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmRunSave::SchemaVersionNow);
	CataclysmSaveFixtures::AddTheRunsVersion11Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion12Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion13Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion14Defaults(*Expected); // And 14, #46 G3 and G5: socket lists, no gem on the floor.
	CataclysmSaveFixtures::AddTheRunsVersion15Defaults(*Expected); // And issue #2741 slice 9's step.
	CataclysmSaveFixtures::AddTheRunsVersion16Defaults(*Expected); // And issue #2838's step.
	CataclysmSaveFixtures::AddTheRunsVersion17Defaults(*Expected); // And issue #2840's step.
	CataclysmSaveFixtures::AddTheRunsVersion18Defaults(*Expected); // And issue #2848's step: where each item arrived.
	CataclysmSaveFixtures::AddTheRunsVersion19Defaults(*Expected); // And issue #51's step: no Rift altar fallen.
	FString Rewritten;
	FString Error;
	FString Where;
	const bool bSame = FCataclysmSaveStorage::ToJson(Died, Rewritten, Error)
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	AddInfo(FString::Printf(TEXT("CLEAN CLEARS MIGRATED: the step %d (%s); written %s %.0f; with the death %d at version "
								 "%d; with none %d of %d clears; Run_v19.json %d; the rest %s"),
							static_cast<int32>(Stepping), *StepMessage, bWritten ? TEXT("yes") : TEXT("NO"), Written,
							Died->ClearsSinceLastDeath, Died->SchemaVersion, NoDeath->ClearsSinceLastDeath,
							NoDeath->DungeonsCleared, Current->ClearsSinceLastDeath, bSame ? TEXT("kept") : *Where));
	TestEqual(TEXT("the chain migrates the version 10 file"), static_cast<int32>(Stepping),
			  static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("the step writes a count of 0 for a run that records a death"), bWritten && Written == 0.0);
	TestEqual(TEXT("it arrives at the current version"), Died->SchemaVersion, UCataclysmRunSave::SchemaVersionNow);
	TestEqual(TEXT("with the death: 0"), Died->ClearsSinceLastDeath, 0);
	TestEqual(TEXT("with no death: every one of the run's 14 clears"), NoDeath->ClearsSinceLastDeath, 14);
	TestTrue(TEXT("and every other field is kept"), bSame);
	TestEqual(TEXT("CONTROL: Run_v19.json reads 5"), Current->ClearsSinceLastDeath, 5);
	return true;
}

// ---------------------------------------------------------------------------
// The extra drops
// ---------------------------------------------------------------------------

/**
 * AN ELITE AT CHANCE ONE DROPS ONE MORE GEAR ITEM, AND A COMMON NONE: Opportunistic Looting names the rung named Elite
 * and no other (D6). The Common's roll draws nothing from the stream.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsEliteTest,
	"Cataclysm.EmpireTreeDrops.AnEliteAtChanceOneDropsOneMoreGearItemAndACommonNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsEliteTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmEmpireTreeDropRules Rules = OnlyRule(1.0f, 0.0f, 0.0f, 0.0f);
	FRandomStream Stream(2741);
	const int32 FromElite = UCataclysmDropSpawner::SpawnTheTreesExtraDropsFor(
		World, UCataclysmDropSpawner::EliteRarityStep, 0.0f, UCataclysmDropRoll::BaselineLootQuantity,
		FVector(100.0f, 0.0f, 0.0f), Stream, Rules);
	const FDropsOnTheFloor AfterElite = DropsIn(World);
	const int32 SeedBefore = Stream.GetCurrentSeed();
	const int32 FromCommon = UCataclysmDropSpawner::SpawnTheTreesExtraDropsFor(
		World, 0, 0.0f, UCataclysmDropRoll::BaselineLootQuantity, FVector(-100.0f, 0.0f, 0.0f), Stream, Rules);
	const FDropsOnTheFloor AfterCommon = DropsIn(World);

	AddInfo(FString::Printf(TEXT("ELITE EXTRA: the Elite %d spawned, %d gear and %d materials; the Common %d, the stream "
								 "%s"),
							FromElite, AfterElite.Gear, AfterElite.Materials, FromCommon,
							Stream.GetCurrentSeed() == SeedBefore ? TEXT("untouched") : TEXT("DRAWN FROM")));
	TestEqual(TEXT("the Elite drops one more"), FromElite, 1);
	TestEqual(TEXT("a gear item"), AfterElite.Gear, 1);
	TestEqual(TEXT("and no material"), AfterElite.Materials, 0);
	TestEqual(TEXT("the Common drops none"), FromCommon, 0);
	TestEqual(TEXT("so the floor is as it was"), AfterCommon.Gear + AfterCommon.Materials, 1);
	TestEqual(TEXT("and the Common's roll drew nothing"), Stream.GetCurrentSeed(), SeedBefore);
	return true;
}

/**
 * PLUNDERER'S EYE AT CHANCE ONE DROPS ONE MORE CRAFTING MATERIAL FROM ANY KILL: a Common's, the rung Opportunistic
 * Looting does not reach.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsMaterialTest,
	"Cataclysm.EmpireTreeDrops.PlunderersEyeDropsOneMoreMaterialFromAnyKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsMaterialTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRandomStream Stream(2741);
	const int32 Spawned = UCataclysmDropSpawner::SpawnTheTreesExtraDropsFor(
		World, 0, 0.0f, UCataclysmDropRoll::BaselineLootQuantity, FVector::ZeroVector, Stream,
		OnlyRule(0.0f, 1.0f, 0.0f, 0.0f));
	const FDropsOnTheFloor Found = DropsIn(World);
	AddInfo(FString::Printf(TEXT("MATERIAL EXTRA: %d spawned, %d gear and %d materials"), Spawned, Found.Gear,
							Found.Materials));
	TestEqual(TEXT("a Common's kill drops one more"), Spawned, 1);
	TestEqual(TEXT("a crafting material"), Found.Materials, 1);
	TestEqual(TEXT("and no gear"), Found.Gear, 0);
	return true;
}

/**
 * A CHANCE OF NOUGHT, OR A RUNG THE NODE DOES NOT NAME, DRAWS NOTHING FROM THE KILL'S STREAM: no rule on a Cataclysm
 * Boss, Opportunistic Looting on a Herald, The High Roller on an Elite. So a kill with none of the nodes bought rolls
 * exactly what it rolled before slice 5b.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsNoDrawTest,
	"Cataclysm.EmpireTreeDrops.AChanceOfNoughtOrARungNotNamedDrawsNothingFromTheStream",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsNoDrawTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	struct FCase { const TCHAR* What; int32 Step; FCataclysmEmpireTreeDropRules Rules; };
	const FCase Cases[] = {
		{ TEXT("no rule on a Cataclysm Boss"), 5, FCataclysmEmpireTreeDropRules() },
		{ TEXT("Opportunistic Looting on a Herald"), 3, OnlyRule(1.0f, 0.0f, 0.0f, 0.0f) },
		{ TEXT("The High Roller on an Elite"), UCataclysmDropSpawner::EliteRarityStep, OnlyRule(0.0f, 0.0f, 0.0f, 1.0f) },
	};
	for (const FCase& One : Cases)
	{
		FRandomStream Stream(2741);
		const int32 SeedBefore = Stream.GetCurrentSeed();
		const int32 Spawned = UCataclysmDropSpawner::SpawnTheTreesExtraDropsFor(
			World, One.Step, 0.0f, UCataclysmDropRoll::BaselineLootQuantity, FVector::ZeroVector, Stream, One.Rules);
		TestEqual(FString::Printf(TEXT("%s: nothing spawned"), One.What), Spawned, 0);
		TestEqual(FString::Printf(TEXT("%s: nothing drawn"), One.What), Stream.GetCurrentSeed(), SeedBefore);
	}
	TestEqual(TEXT("and nothing lies on the floor"), DropsIn(World).Gear + DropsIn(World).Materials, 0);
	return true;
}

/**
 * SCAVENGER AT CHANCE ONE MAKES EVERY CRAFTING MATERIAL OF TIER 3 OR BELOW A STACK OF TWO, AND LEAVES TIERS 4 AND 5 AT
 * ONE (E2: the same drop holding two). 300 materials rolled at a high magic find, so both sides of the line appear.
 * CONTROL: the same roll at chance nought, every drop one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsScavengerTest,
	"Cataclysm.EmpireTreeDrops.ScavengerMakesEachLowTierMaterialAStackOfTwo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsScavengerTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	const UDataTable* Drops = UCataclysmDropRoll::LoadEnemyDropTable();
	if (!TestNotNull(TEXT("set-up: the enemy drop table"), Drops))
	{
		return false;
	}
	const FName Common = UCataclysmDropSpawner::EnemyRarityForStep(Drops, 0);

	// THE QUANTITIES OF ONE ROLL OF 300, by tier: how many drops of tier 3 or below hold two, how many hold one, and the
	// same above tier 3.
	struct FByTier { int32 LowTwo = 0; int32 LowOne = 0; int32 HighTwo = 0; int32 HighOne = 0; };
	const auto RollOf = [this, Common](float Chance, FByTier& Out) -> bool
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("set-up: a test world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FRandomStream Stream(2741);
		UCataclysmDropSpawner::SpawnMaterialsFor(World, Common, /*MagicFind=*/3000.0f, FVector::ZeroVector,
												 /*Count=*/300, /*AlreadyOnTheFloor=*/0, /*TotalDrops=*/300,
												 /*DifficultyTier=*/8, Stream, /*bMarked=*/false,
												 /*bInfested=*/false, Chance);
		for (const ACataclysmDroppedItem* Drop : DropsIn(World).MaterialDrops)
		{
			const bool bLow = Drop->MaterialTier <= UCataclysmDropSpawner::ScavengerHighestTier;
			const bool bTwo = Drop->MaterialQuantity == 2;
			(bLow ? (bTwo ? Out.LowTwo : Out.LowOne) : (bTwo ? Out.HighTwo : Out.HighOne)) += 1;
		}
		return true;
	};

	FByTier Scavenged;
	FByTier Plain;
	if (!RollOf(1.0f, Scavenged) || !RollOf(0.0f, Plain))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("SCAVENGER: at 1, tier 3 or below %d of two and %d of one, above %d of two and %d of "
								 "one; at 0, %d, %d, %d, %d"),
							Scavenged.LowTwo, Scavenged.LowOne, Scavenged.HighTwo, Scavenged.HighOne, Plain.LowTwo,
							Plain.LowOne, Plain.HighTwo, Plain.HighOne));
	if (!TestTrue(TEXT("set-up: both sides of tier 3 dropped"),
				  Scavenged.LowTwo + Scavenged.LowOne > 0 && Scavenged.HighTwo + Scavenged.HighOne > 0))
	{
		return false;
	}
	TestEqual(TEXT("every material of tier 3 or below is two"), Scavenged.LowOne, 0);
	TestEqual(TEXT("and none of tier 4 or 5 is"), Scavenged.HighTwo, 0);
	TestEqual(TEXT("CONTROL: at nought, none is two"), Plain.LowTwo + Plain.HighTwo, 0);
	return true;
}

/**
 * THE HIGH ROLLER'S RARITY IS LEGENDARY OR RARER, IN THE ORDINARY ROLL'S SHARES. 4000 draws at tier 8 with no magic
 * find from Legendary up: none lower, and each rung's share within 0.03 of the ordinary roll's own share for it as a
 * share of the rungs from Legendary up (`RarityDistribution`, renormalized). And from Everyday it is the ordinary roll,
 * draw for draw.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsRarityTest,
	"Cataclysm.EmpireTreeDrops.TheHighRollersRarityIsLegendaryOrRarerInTheOrdinaryRollsShares",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsRarityTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable();
	if (!TestNotNull(TEXT("set-up: the gear rarity table"), Rarities))
	{
		return false;
	}
	constexpr int32 Tier = 8;
	constexpr int32 Draws = 4000;
	const int32 From = static_cast<int32>(ECataclysmRarity::Legendary);

	TArray<float> Shares;
	UCataclysmDropRoll::RarityDistribution(Rarities, Tier, 0.0f, Shares);
	float Reachable = 0.0f;
	for (int32 Rung = From; Rung < Shares.Num(); ++Rung)
	{
		Reachable += Shares[Rung];
	}
	if (!TestTrue(TEXT("set-up: Legendary or rarer can drop at tier 8"), Reachable > 0.0f))
	{
		return false;
	}

	TArray<int32> Counts;
	Counts.Init(0, Shares.Num());
	FRandomStream Stream(2741);
	for (int32 Draw = 0; Draw < Draws; ++Draw)
	{
		const int32 Rung = static_cast<int32>(
			UCataclysmDropRoll::RollRarityAtLeast(Rarities, Tier, 0.0f, ECataclysmRarity::Legendary, Stream));
		if (Counts.IsValidIndex(Rung))
		{
			++Counts[Rung];
		}
	}
	int32 Below = 0;
	for (int32 Rung = 0; Rung < From; ++Rung)
	{
		Below += Counts[Rung];
	}
	TestEqual(TEXT("none below Legendary"), Below, 0);
	for (int32 Rung = From; Rung < Shares.Num(); ++Rung)
	{
		const float Expected = Shares[Rung] / Reachable;
		const float Observed = static_cast<float>(Counts[Rung]) / static_cast<float>(Draws);
		AddInfo(FString::Printf(TEXT("HIGH ROLLER: rung %d drawn %.3f, the ordinary roll's share from Legendary up %.3f"),
								Rung, Observed, Expected));
		TestEqual(FString::Printf(TEXT("rung %d in its share"), Rung), Observed, Expected, 0.03f);
	}

	// FROM EVERYDAY IT IS THE ORDINARY ROLL, draw for draw.
	FRandomStream Ordinary(4321);
	FRandomStream FromEveryday(4321);
	int32 Differ = 0;
	for (int32 Draw = 0; Draw < 200; ++Draw)
	{
		Differ += UCataclysmDropRoll::RollRarity(Rarities, Tier, 0.0f, Ordinary)
			!= UCataclysmDropRoll::RollRarityAtLeast(Rarities, Tier, 0.0f, ECataclysmRarity::Everyday, FromEveryday)
			? 1 : 0;
	}
	TestEqual(TEXT("from Everyday, the ordinary roll"), Differ, 0);
	return true;
}

/**
 * A BOSS AT CHANCE ONE DROPS ONE MORE GEAR ITEM, LEGENDARY OR RARER: The High Roller at tier 8 on the first boss rung.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeDropsBossTest,
	"Cataclysm.EmpireTreeDrops.ABossAtChanceOneDropsOneMoreItemOfLegendaryOrRarer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeDropsBossTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeDropsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedDifficultyTier Tier(8);

	FRandomStream Stream(2741);
	const int32 Spawned = UCataclysmDropSpawner::SpawnTheTreesExtraDropsFor(
		World, UCataclysmDropSpawner::FirstBossRarityStep, 0.0f, UCataclysmDropRoll::BaselineLootQuantity,
		FVector::ZeroVector, Stream, OnlyRule(0.0f, 0.0f, 0.0f, 1.0f));
	const FDropsOnTheFloor Found = DropsIn(World);
	const ECataclysmRarity Rarity = Found.GearDrops.Num() == 1 ? Found.GearDrops[0]->Rarity : ECataclysmRarity::Everyday;
	AddInfo(FString::Printf(TEXT("BOSS EXTRA: %d spawned, %d gear, rarity %d"), Spawned, Found.Gear,
							static_cast<int32>(Rarity)));
	TestEqual(TEXT("the Boss drops one more"), Spawned, 1);
	TestEqual(TEXT("a gear item"), Found.Gear, 1);
	TestTrue(TEXT("of Legendary or rarer"),
			 static_cast<int32>(Rarity) >= static_cast<int32>(ECataclysmRarity::Legendary));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
