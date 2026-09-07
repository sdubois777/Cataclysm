// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "Empire/CataclysmDungeonModifier.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Engine/DataTable.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The real dungeon modifier table becoming a pool the empire layer can draw
 * from. Issue #41.
 *
 * WHY THIS IS SEPARATE FROM `Cataclysm.DungeonModifiers.*` IN THE EMPIRE MODULE.
 * Those tests build a pool by hand and check the RULES: how many a dungeon
 * carries, which Cataclysms it may draw from, that a draw does not repeat. This
 * file checks the JOIN: that `DT_DungeonModifiers` really has 117 rows, that
 * every one of them names a Cataclysm the roster knows or the `Generic` column,
 * that every danger score survives the crossing, and that the score model reads
 * the sum back off the game mode.
 *
 * THEY SHARE A PREFIX ON PURPOSE, so that
 * `python tools/unreal_build.py tests --prefix Cataclysm.DungeonModifiers` runs
 * the whole feature and not half of it.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierTableLoadsTest,
	"Cataclysm.DungeonModifiers.EveryRowOfTheTableBecomesAPoolEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierTableLoadsTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table =
		UCataclysmDungeonModifierTable::LoadDungeonModifierTable();
	if (Table == nullptr)
	{
		AddError(TEXT("DT_DungeonModifiers would not load, so nothing below "
					  "checked anything. It is generated from "
					  "game/Data/DungeonModifiers.csv; see game/README.md."));
		return false;
	}

	const TArray<FCataclysmDungeonModifier> Pool =
		UCataclysmDungeonModifierTable::PoolFromTable(Table);

	// 117, NOT 116. The Corrupted Stalker was added by issue #504 and the
	// simulation's copy of this table missed it for four months.
	// `tools/tests/test_dungeon_modifier_port.py` is what compares the two
	// copies; this is what says the asset the game actually reads holds them.
	TestEqual(TEXT("the table has 117 rows"), Table->GetRowMap().Num(), 117);
	TestEqual(TEXT("and every one becomes a pool entry"), Pool.Num(), 117);

	for (const FCataclysmDungeonModifier& Modifier : Pool)
	{
		TestFalse(TEXT("every entry has a row key"), Modifier.RowKey.IsNone());
		TestFalse(FString::Printf(TEXT("%s has a name"),
								  *Modifier.RowKey.ToString()),
				  Modifier.ModifierName.IsNone());

		// EVERY ROW IS DANGEROUS TO SOME DEGREE. A weight of zero would add
		// nothing to the Modifier Score, which would make the modifier free.
		TestTrue(FString::Printf(TEXT("%s carries a danger score above zero"),
								 *Modifier.RowKey.ToString()),
				 Modifier.Danger > 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierTableTypesTest,
	"Cataclysm.DungeonModifiers.EveryRowNamesACataclysmOrGeneric",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierTableTypesTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table =
		UCataclysmDungeonModifierTable::LoadDungeonModifierTable();
	if (Table == nullptr)
	{
		AddError(TEXT("DT_DungeonModifiers would not load."));
		return false;
	}

	// **THE CHECK THAT JOINS TWO SPELLINGS OF THE SAME EIGHT NAMES.** The CSV's
	// `CataclysmType` column is a string from the design workbook;
	// `UCataclysmRoster::NameFor` is the enum's. Both come from the same design
	// document, so they agree -- and if one is ever renamed without the other, a
	// whole Cataclysm's modifiers would silently become undrawable, because an
	// unrecognised type maps to `None` and `None` is never active.
	int32 Generic = 0;
	TSet<int32> Seen;
	bool bAnyUnrecognised = false;

	Table->ForeachRow<FCataclysmDungeonModifierRow>(
		TEXT("EveryRowNamesACataclysmOrGeneric"),
		[this, &Generic, &Seen, &bAnyUnrecognised](
			const FName& Key, const FCataclysmDungeonModifierRow& Row)
		{
			bool bRecognised = false;
			const ECataclysmType Cataclysm =
				UCataclysmDungeonModifierTable::CataclysmFor(
					Row.CataclysmType, bRecognised);

			if (!bRecognised)
			{
				bAnyUnrecognised = true;
				AddError(FString::Printf(
					TEXT("Dungeon modifier row '%s' names Cataclysm type '%s', "
						 "which is neither one of the eight nor 'Generic'. It "
						 "can never be drawn."),
					*Key.ToString(), *Row.CataclysmType));
				return;
			}

			if (Cataclysm == ECataclysmType::None)
			{
				++Generic;
			}
			else
			{
				Seen.Add(static_cast<int32>(Cataclysm));
			}
		});

	TestFalse(TEXT("no row names an unrecognised Cataclysm"), bAnyUnrecognised);

	// ALL EIGHT ARE PRESENT. Without this the check above would pass on a table
	// that had lost seven of its columns, because what is left would still be
	// recognised.
	TestEqual(TEXT("all eight Cataclysms have modifiers"), Seen.Num(),
			  UCataclysmRoster::Count);

	// AND EXACTLY ONE ROW IS GENERIC: the Corrupted Stalker. The project owner
	// ruled on 2026-09-05 that it is granted separately and does not take a
	// modifier slot, so it maps to `None` and is never in a pool. Nothing grants
	// it; that gap is issue #1308.
	TestEqual(TEXT("exactly one row is Generic"), Generic, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierTableNameTest,
	"Cataclysm.DungeonModifiers.ARowKeyCanBeTurnedBackIntoItsDesignedName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierTableNameTest::RunTest(const FString& Parameters)
{
	// FOR SHOWING A PLAYER. `FCataclysmDungeon::Modifiers` holds row keys, which
	// are machine names, and a screen has to print something a person can read.
	TestEqual(TEXT("a known key gives the design's name"),
		UCataclysmDungeonModifierTable::NameOf(
			FName(TEXT("Celestial_Edict_of_Silence"))),
		FString(TEXT("Edict of Silence")));

	// AND AN UNKNOWN KEY GIVES THE KEY, not an empty string. A player told
	// nothing is worse off than a player told a machine name.
	TestEqual(TEXT("an unknown key gives itself back"),
		UCataclysmDungeonModifierTable::NameOf(
			FName(TEXT("Nothing_Named_This"))),
		FString(TEXT("Nothing_Named_This")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierScoreReachesEnemiesTest,
	"Cataclysm.DungeonModifiers.TheModifierScoreReachesEveryCreaturesScore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierScoreReachesEnemiesTest::RunTest(const FString& Parameters)
{
	// **THE HALF OF THE FEATURE THAT IS NOT THE DRAW.** A dungeon can carry
	// perfect modifiers and change nothing at all if the score model still adds
	// zero, which is exactly what it did until this slice:
	// `UCataclysmEnemyScore::FloorIn` hard-zeroed `ModifierScore` with a comment
	// saying dungeon modifiers did not exist.
	//
	// THE MODEL IS EXERCISED DIRECTLY RATHER THAN THROUGH A WORLD, because
	// `FloorIn` needs a game mode and an automation run has none. What `FloorIn`
	// does with the number is checked by the source assertion below; what the
	// FORMULA does with it is checked here.
	FCataclysmScoredFloor Floor;
	Floor.DifficultyTier = 4;
	Floor.FloorNumber = 5;
	Floor.TotalFloors = 10;

	const int32 Without = UCataclysmEnemyScore::ScoreFor(Floor, /* Rarity */ 0);

	// FORTY IS A REAL SUM: four modifiers at a tier 4 dungeon, at 5, 10, 10 and
	// 15. It is not a round number picked for the test.
	Floor.ModifierScore = 40.0f;
	const int32 With = UCataclysmEnemyScore::ScoreFor(Floor, /* Rarity */ 0);

	TestEqual(TEXT("the modifier score is added to the enemy score, flat"),
			  With, Without + 40);

	// AND IT IS AN ADDEND RATHER THAN A MULTIPLIER, which is what makes zero
	// mean "no modifiers" exactly. Doubling the score must move the total by the
	// same 40 again rather than by a share of it.
	Floor.ModifierScore = 80.0f;
	TestEqual(TEXT("and twice the modifier score adds twice as much"),
			  UCataclysmEnemyScore::ScoreFor(Floor, /* Rarity */ 0),
			  Without + 80);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierEnteringCarriesItTest,
	"Cataclysm.DungeonModifiers.EnteringADungeonCarriesItsModifierScoreOver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierEnteringCarriesItTest::RunTest(const FString& Parameters)
{
	// **THE WHOLE ROUTE, END TO END, AND THE ONLY TEST THAT WALKS IT.** The
	// empire layer draws the modifiers; the game mode has to carry the sum
	// across the module line when the player enters; the score model has to read
	// it back. Each of those three has its own test and all three would pass
	// while the wiring between them did not exist -- which is how
	// `ModifierScore` stayed hard-zeroed for as long as it did.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode))
	{
		return false;
	}

	// THE REAL TABLE, so this is the pool the game would really have. Building
	// one by hand here would leave the loader untested from this direction.
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->ModifierPool = UCataclysmDungeonModifierTable::LoadPool();
	if (!TestTrue(TEXT("the modifier table loaded"), Run->ModifierPool.Num() > 0))
	{
		return false;
	}

	// TIER 4, SO A DUNGEON CARRIES FOUR MODIFIERS. At tier 1 a single modifier
	// could coincide with several wrong rules, and a score built from one row
	// could coincide with a stray constant.
	Run->Begin(/* Seed */ 1, ECataclysmSurgeMode::Static,
			   /* LethalityRung */ 0, /* DifficultyTier */ 4);
	Run->AdvanceDay();

	if (!TestTrue(TEXT("the first surge put a dungeon on the map"),
				  Run->Dungeons.Num() > 0))
	{
		return false;
	}

	Mode->SetEmpireRunForTests(Run);

	const FCataclysmDungeon Dungeon = Run->Dungeons[0];

	TestTrue(TEXT("the dungeon carries modifiers"), Dungeon.Modifiers.Num() > 0);
	TestTrue(TEXT("and a modifier score above zero"),
			 Dungeon.ModifierScore > 0.0f);

	// BEFORE ENTERING IT IS ZERO, which is what makes the check after entering
	// a comparison rather than a coincidence.
	TestEqual(TEXT("a game mode walking nothing adds no modifier score"),
			  Mode->RunModifierScore(), 0.0f);

	if (!TestTrue(TEXT("the dungeon is entered"),
				  Mode->EnterEmpireDungeon(Dungeon.DungeonId)))
	{
		return false;
	}

	TestEqual(TEXT("entering carries the dungeon's modifier score over"),
			  Mode->DungeonModifierScore, Dungeon.ModifierScore);

	// **AND THE NUMBER THE SCORE MODEL READS IS THE FLOOR'S, NOT THE DUNGEON'S.**
	// Since issue #41's sub-type slice a floor decides its own modifiers:
	// `FCataclysmDungeonFloorRules` gives a Volatile dungeon a different set on
	// every floor, and gives any floor carrying the Unstable Dimensions modifier
	// one extra. For every other dungeon the two numbers are the same, which is
	// why the check below is against the floor and the check above is against
	// the dungeon. `Cataclysm.FloorBrief.*` is where the per-floor rules are
	// covered.
	TestEqual(TEXT("and the number read back is the floor's own"),
			  Mode->RunModifierScore(), Mode->FloorBrief.ModifierScore);

	// AND THE SCORE MODEL READS IT OFF THE GAME MODE. This is the last link and
	// the one that was a hard-coded zero.
	//
	// **THROUGH `FloorFor` AND NOT `FloorIn`, AND THAT IS NOT A SHORTCUT.**
	// `FloorIn` finds its game mode with `UWorld::GetAuthGameMode`, which
	// answers null in every automation world: `UWorld::AuthorityGameMode` is
	// private and only a game instance sets it, and a world built by
	// `UWorld::CreateWorld` has none. Written against `FloorIn` this check read
	// 0 against the expected 40 -- not because the wiring was wrong but because
	// the game mode spawned two lines above could not be found. `FloorFor` is
	// the reading with the finding taken out of it, which is why it exists.
	const FCataclysmScoredFloor Floor =
		UCataclysmEnemyScore::FloorFor(Mode, /* DifficultyTier */ 4);
	TestEqual(TEXT("and the score model reads it back off the game mode"),
			  Floor.ModifierScore, Mode->FloorBrief.ModifierScore);

	// AND IT IS NOT ZERO, which is what the whole route exists to stop. Asserting
	// only that two numbers agree would pass if both were the hard-coded zero
	// this replaced.
	TestTrue(FString::Printf(
				 TEXT("the floor's modifiers are worth %.1f, which is above zero"),
				 Floor.ModifierScore),
			 Floor.ModifierScore > 0.0f);

	// AND THE REST OF THAT READING STILL HAPPENS. Four other fields come out of
	// the same block and none of them was reachable from a test before the
	// split, so a mistake in it would have shown up only in play.
	TestEqual(TEXT("the floor number comes over too"),
			  Floor.FloorNumber, Mode->RunFloorNumber());
	TestEqual(TEXT("and the dungeon's length"),
			  Floor.TotalFloors, Mode->RunTotalFloors());
	TestEqual(TEXT("and its sub-type"), static_cast<int32>(Floor.SubType),
			  static_cast<int32>(Mode->RunDungeonSubType()));
	TestEqual(TEXT("and the difficulty tier it was handed"),
			  Floor.DifficultyTier, 4);

	// A GAME MODE THAT IS NOT THERE GIVES THE DEFAULTS RATHER THAN NOTHING,
	// which is what `FloorIn` promises for a world without one.
	const FCataclysmScoredFloor Nowhere =
		UCataclysmEnemyScore::FloorFor(nullptr, /* DifficultyTier */ 4);
	TestEqual(TEXT("no game mode means no modifiers"),
			  Nowhere.ModifierScore, 0.0f);
	TestEqual(TEXT("and one floor of one"), Nowhere.TotalFloors, 1);

	// LEAVING PUTS IT BACK TO ZERO. Without this a player who left the empire
	// and walked a plain floor would still fight creatures carrying the last
	// dungeon's modifiers.
	Mode->LeaveEmpireDungeon();
	TestEqual(TEXT("leaving the dungeon leaves its modifiers behind"),
			  Mode->RunModifierScore(), 0.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
