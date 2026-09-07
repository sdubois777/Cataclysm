// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Empire/CataclysmDungeonModifier.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"

/**
 * Which dungeon modifiers a dungeon carries, and how dangerous they make it.
 * Issue #41.
 *
 * WHAT THESE PIN AND WHAT `tools/tests/test_dungeon_modifier_port.py` PINS.
 * These check the behaviour: how many a dungeon gets, that a Sacrificial one
 * gets twice as many, that only the active Cataclysms' modifiers can be drawn,
 * that the same seed draws the same modifiers, that a run gives them out. That
 * Python test checks the two copies of the TABLE still agree, that the counts
 * still match `sim/cataclysm_sim/config.py`, and that both sides still describe
 * the `Weight` column as a danger score. Neither can notice what the other does.
 *
 * THEY BUILD THEIR POOL BY HAND, WITH NO DataTable. That is the whole reason the
 * rule lives in this module rather than beside the table: an automation run
 * passes `-nullrhi`, and a rule that needed an asset loaded could only be
 * checked where content is present.
 * `Cataclysm.DungeonModifiers.EveryRowOfTheTableBecomesAPoolEntry`, over in the
 * `Cataclysm` module, is the one that reads the real 117 rows.
 */

namespace CataclysmDungeonModifierTest
{
	/** A modifier of a named Cataclysm, so a pool can be written out by hand. */
	FCataclysmDungeonModifier Make(const TCHAR* Key, ECataclysmType Cataclysm,
								   float Danger)
	{
		FCataclysmDungeonModifier Modifier;
		Modifier.RowKey = FName(Key);
		Modifier.ModifierName = FName(Key);
		Modifier.Cataclysm = Cataclysm;
		Modifier.Danger = Danger;
		return Modifier;
	}

	/**
	 * Four modifiers for each of the eight Cataclysms, plus one Generic.
	 *
	 * FOUR RATHER THAN THE TABLE'S TWELVE TO FIFTEEN, so a test can ask for more
	 * than one Cataclysm's worth and watch the pool run out. The danger scores
	 * are the four the real table uses -- 5, 10, 15 and 20 -- so a sum computed
	 * here is a sum the game could really produce.
	 */
	TArray<FCataclysmDungeonModifier> WholeTable()
	{
		TArray<FCataclysmDungeonModifier> All;

		for (const ECataclysmType Cataclysm : UCataclysmRoster::All())
		{
			const FString Name = UCataclysmRoster::NameFor(Cataclysm).ToString();
			int32 Index = 0;
			for (const float Danger : { 5.0f, 10.0f, 15.0f, 20.0f })
			{
				All.Add(Make(*FString::Printf(TEXT("%s_%d"), *Name, Index++),
							 Cataclysm, Danger));
			}
		}

		// THE `Generic` COLUMN, WHICH IS ONE ROW IN THE REAL TABLE. It carries
		// `None`, which is never in an active set, so it must never be drawn.
		All.Add(Make(TEXT("Generic_Corrupted_Stalker"), ECataclysmType::None,
					 20.0f));

		return All;
	}

	/** A run at a chosen tier, with the whole table in its pool. */
	UCataclysmEmpireRun* MakeRun(int32 Seed, int32 DifficultyTier)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->ModifierPool = WholeTable();
		Run->Begin(Seed, ECataclysmSurgeMode::Static, /* LethalityRung */ 0,
				   DifficultyTier);
		return Run;
	}

	/**
	 * A dungeon's modifiers as one comparable string.
	 *
	 * BECAUSE `TestEqual` HAS NO OVERLOAD FOR A `TArray`. Joined rather than
	 * compared element by element so that a failure names both whole lists,
	 * which is what tells a reader whether the draw changed or only its order.
	 */
	FString Join(const TArray<FName>& Keys)
	{
		TArray<FString> Names;
		for (const FName Key : Keys)
		{
			Names.Add(Key.ToString());
		}
		return FString::Join(Names, TEXT(", "));
	}
}

// ---------------------------------------------------------------------------
// How many a dungeon carries
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierCountTest,
	"Cataclysm.DungeonModifiers.OneModifierPerDifficultyTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierCountTest::RunTest(const FString& Parameters)
{
	// `docs/Cataclysm_GDD_v2.md` SECTION VIII, in as many words: "A dungeon
	// carries one modifier per difficulty tier, so a tier 8 dungeon carries
	// eight."
	for (int32 Tier = 1; Tier <= 8; ++Tier)
	{
		TestEqual(FString::Printf(TEXT("tier %d carries %d"), Tier, Tier),
			UCataclysmDungeonModifierRules::CountFor(
				Tier, ECataclysmDungeonSubType::None), Tier);
	}

	// AND EVERY SUB-TYPE BUT ONE LEAVES IT ALONE. Named one at a time rather
	// than as "not Sacrificial", so a sub-type added later is not silently
	// covered by a rule nobody wrote for it.
	for (const ECataclysmDungeonSubType SubType : {
			ECataclysmDungeonSubType::None,
			ECataclysmDungeonSubType::Timed,
			ECataclysmDungeonSubType::Horde,
			ECataclysmDungeonSubType::Siege,
			ECataclysmDungeonSubType::CowLevel,
			ECataclysmDungeonSubType::Elite,
			ECataclysmDungeonSubType::Volatile })
	{
		TestEqual(TEXT("a tier 5 dungeon of this sub-type carries five"),
			UCataclysmDungeonModifierRules::CountFor(5, SubType), 5);
	}

	// A TIER OF ZERO OR LESS ASKS FOR NONE RATHER THAN FOR A NEGATIVE NUMBER.
	// Reachable: `UCataclysmEmpireRun::DifficultyTier` is an int32 and nothing
	// clamps it on the way in.
	TestEqual(TEXT("tier 0 carries none"),
		UCataclysmDungeonModifierRules::CountFor(
			0, ECataclysmDungeonSubType::None), 0);
	TestEqual(TEXT("a negative tier carries none, not a negative number"),
		UCataclysmDungeonModifierRules::CountFor(
			-3, ECataclysmDungeonSubType::None), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierSacrificialTest,
	"Cataclysm.DungeonModifiers.ASacrificialDungeonCarriesTwiceAsMany",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierSacrificialTest::RunTest(const FString& Parameters)
{
	// "A Sacrificial dungeon carries double that, which the player may either
	// shed by sacrificing materials or keep for bonus rewards."
	for (int32 Tier = 1; Tier <= 8; ++Tier)
	{
		const int32 Plain = UCataclysmDungeonModifierRules::CountFor(
			Tier, ECataclysmDungeonSubType::None);
		const int32 Sacrificial = UCataclysmDungeonModifierRules::CountFor(
			Tier, ECataclysmDungeonSubType::Sacrificial);

		TestEqual(FString::Printf(
			TEXT("tier %d Sacrificial carries %d"), Tier, Plain * 2),
			Sacrificial, Plain * 2);

		// AND IT IS A DOUBLING RATHER THAN AN INCREMENT, WHICH ONLY TIER 2
		// UPWARDS CAN SHOW. At tier 1 a doubling and an increment both give 2,
		// so asserting they differ there fails on the correct implementation --
		// which is what this check did on its first run.
		if (Tier >= 2)
		{
			TestNotEqual(FString::Printf(
				TEXT("and %d is not the %d that adding one would give"),
				Sacrificial, Plain + 1),
				Sacrificial, Plain + 1);
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Which ones it may draw
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierPoolTest,
	"Cataclysm.DungeonModifiers.OnlyTheActiveCataclysmsModifiersAreDrawable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierPoolTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;

	const TArray<FCataclysmDungeonModifier> All = WholeTable();

	// `config.py`: "dungeons then draw from the combined modifier pool of every
	// ACTIVE Cataclysm. Two active types means ~26 modifiers to roll from."
	const TArray<ECataclysmType> Active = {
		ECataclysmType::Death, ECataclysmType::Void };

	const TArray<FCataclysmDungeonModifier> Pool =
		UCataclysmDungeonModifierRules::PoolFor(All, Active);

	TestEqual(TEXT("two active Cataclysms pool both their columns"),
			  Pool.Num(), 8);

	for (const FCataclysmDungeonModifier& Modifier : Pool)
	{
		TestTrue(FString::Printf(TEXT("%s belongs to an active Cataclysm"),
								 *Modifier.RowKey.ToString()),
				 Active.Contains(Modifier.Cataclysm));
	}

	// AND THE POOL IS SORTED, WHICH IS NOT TIDINESS. The caller builds the list
	// by walking a `UDataTable`, which is a map with no guaranteed order, so an
	// unsorted pool would hand the same seed a different modifier on a different
	// run.
	for (int32 Index = 1; Index < Pool.Num(); ++Index)
	{
		TestTrue(TEXT("the pool is in row-key order"),
				 Pool[Index - 1].RowKey.LexicalLess(Pool[Index].RowKey));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierGenericTest,
	"Cataclysm.DungeonModifiers.TheGenericColumnIsNeverDrawable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierGenericTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;

	// **THE OWNER'S RULING OF 2026-09-05.** The Corrupted Stalker, the one row
	// of the `Generic` column, is "granted separately" and does not compete for
	// one of a dungeon's modifier slots. A dungeon draws from this pool, so
	// anything in the pool competes for a slot by construction.
	// `test_a_generic_modifier_is_never_drawable` in
	// `tools/tests/test_dungeon_modifier_port.py` is the same check on the
	// simulation's copy of the table.
	const TArray<FCataclysmDungeonModifier> All = WholeTable();

	for (int32 Count = 1; Count <= UCataclysmRoster::Count; ++Count)
	{
		TArray<ECataclysmType> Active = UCataclysmRoster::All();
		Active.SetNum(Count);

		const TArray<FCataclysmDungeonModifier> Pool =
			UCataclysmDungeonModifierRules::PoolFor(All, Active);

		for (const FCataclysmDungeonModifier& Modifier : Pool)
		{
			TestNotEqual(FString::Printf(
				TEXT("with %d Cataclysm(s) active, %s is drawable and should "
					 "not be: it belongs to the Generic column"),
				Count, *Modifier.RowKey.ToString()),
				static_cast<int32>(Modifier.Cataclysm),
				static_cast<int32>(ECataclysmType::None));
		}
	}

	// AND THE ROW IS STILL IN THE TABLE. It is not drawn; it is not deleted.
	// The design has 117 dungeon modifiers and this one is the 117th.
	const bool bStillThere = All.ContainsByPredicate(
		[](const FCataclysmDungeonModifier& Modifier)
		{
			return Modifier.Cataclysm == ECataclysmType::None;
		});
	TestTrue(TEXT("the Generic row is still in the table, it is only not drawn"),
			 bStillThere);

	return true;
}

// ---------------------------------------------------------------------------
// The draw itself
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierDrawTest,
	"Cataclysm.DungeonModifiers.ADrawIsDistinctAndComesOutOfThePool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierDrawTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;

	const TArray<FCataclysmDungeonModifier> Pool =
		UCataclysmDungeonModifierRules::PoolFor(
			WholeTable(), UCataclysmRoster::All());

	FRandomStream Stream(1234);
	const TArray<FCataclysmDungeonModifier> Drawn =
		UCataclysmDungeonModifierRules::Draw(Pool, 8, Stream);

	TestEqual(TEXT("eight were asked for and eight came back"), Drawn.Num(), 8);

	TSet<FName> Seen;
	for (const FCataclysmDungeonModifier& Modifier : Drawn)
	{
		TestFalse(FString::Printf(TEXT("%s was drawn twice"),
								  *Modifier.RowKey.ToString()),
				  Seen.Contains(Modifier.RowKey));
		Seen.Add(Modifier.RowKey);

		TestTrue(FString::Printf(TEXT("%s came out of the pool"),
								 *Modifier.RowKey.ToString()),
				 Pool.ContainsByPredicate(
					[&Modifier](const FCataclysmDungeonModifier& Candidate)
					{
						return Candidate.RowKey == Modifier.RowKey;
					}));
	}

	// A DRAW OF NONE MUST NOT TOUCH THE STREAM. `CountFor` answers zero at tier
	// 0, and a stream advanced anyway would shift every later dungeon in the
	// wave.
	FRandomStream Untouched(99);
	const int32 Before = Untouched.GetCurrentSeed();
	TestEqual(TEXT("asking for none draws none"),
		UCataclysmDungeonModifierRules::Draw(Pool, 0, Untouched).Num(), 0);
	TestEqual(TEXT("and does not advance the stream"),
			  Untouched.GetCurrentSeed(), Before);

	// AND THE POOL IT WAS HANDED IS NOT EMPTIED. A run holds one pool for its
	// whole length and draws from it once per dungeon; a draw that consumed the
	// caller's list would leave the second dungeon with nothing.
	TestEqual(TEXT("the caller's pool still holds every row"),
			  Pool.Num(), 32);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierEmptyPoolTest,
	"Cataclysm.DungeonModifiers.AnEmptyOrShortPoolGivesFewerRatherThanRepeats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierEmptyPoolTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;

	FRandomStream Stream(7);

	// AN EMPTY POOL IS THE STATE EVERY HEADLESS RUN IS IN, because filling it
	// needs the DataTable. It must give nothing rather than refuse or crash.
	const TArray<FCataclysmDungeonModifier> Nothing;
	TestEqual(TEXT("an empty pool draws nothing"),
		UCataclysmDungeonModifierRules::Draw(Nothing, 5, Stream).Num(), 0);

	// AND A POOL SMALLER THAN THE ASK GIVES WHAT IT HAS. `config.py` names the
	// risk -- "~26 modifiers to roll from, which is what stops deep tiers
	// running dry" -- and repeating a modifier to fill the gap would put two
	// copies of one environmental effect on one dungeon.
	const TArray<FCataclysmDungeonModifier> Small =
		UCataclysmDungeonModifierRules::PoolFor(
			WholeTable(), { ECataclysmType::Famine });

	TestEqual(TEXT("one Cataclysm is four modifiers in this table"),
			  Small.Num(), 4);

	const TArray<FCataclysmDungeonModifier> Drawn =
		UCataclysmDungeonModifierRules::Draw(Small, 16, Stream);

	TestEqual(TEXT("asking for sixteen from four gives four"),
			  Drawn.Num(), 4);

	TSet<FName> Seen;
	for (const FCataclysmDungeonModifier& Modifier : Drawn)
	{
		Seen.Add(Modifier.RowKey);
	}
	TestEqual(TEXT("and all four are different"), Seen.Num(), 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierDangerTest,
	"Cataclysm.DungeonModifiers.TheModifierScoreIsTheSumOfTheDangerScores",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierDangerTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;

	// `docs/Cataclysm_GDD_v2.md` SECTION VIII: "Each dungeon modifier carries a
	// weight, and the sum of the weights on a dungeon is the Modifier Score in
	// the Enemy Score formula in section X."
	const TArray<FCataclysmDungeonModifier> Three = {
		Make(TEXT("A"), ECataclysmType::War, 5.0f),
		Make(TEXT("B"), ECataclysmType::War, 15.0f),
		Make(TEXT("C"), ECataclysmType::War, 20.0f) };

	TestEqual(TEXT("5 + 15 + 20 is 40"),
			  UCataclysmDungeonModifierRules::DangerOf(Three), 40.0f);

	// NO MODIFIERS IS EXACTLY ZERO. The Enemy Score model adds this as a flat
	// term, so zero means "nothing added" rather than standing in for something
	// unknown.
	const TArray<FCataclysmDungeonModifier> None;
	TestEqual(TEXT("no modifiers add nothing"),
			  UCataclysmDungeonModifierRules::DangerOf(None), 0.0f);

	return true;
}

// ---------------------------------------------------------------------------
// A run actually handing them out
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierRunTest,
	"Cataclysm.DungeonModifiers.EveryDungeonASurgeLandsCarriesModifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierRunTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;

	// TIER 4, SO THE COUNT IS NOT 1. At tier 1 a count of one agrees with
	// several wrong rules, including "every dungeon gets exactly one".
	UCataclysmEmpireRun* Run = MakeRun(/* Seed */ 42, /* DifficultyTier */ 4);

	Run->AdvanceDay();

	TestTrue(TEXT("a surge landed on the first day"), Run->Dungeons.Num() > 0);

	for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
	{
		const int32 Wanted = UCataclysmDungeonModifierRules::CountFor(
			4, Dungeon.SubType);

		TestEqual(FString::Printf(
			TEXT("dungeon %d carries %d modifiers"), Dungeon.DungeonId, Wanted),
			Dungeon.Modifiers.Num(), Wanted);

		TestTrue(FString::Printf(
			TEXT("dungeon %d has a modifier score above zero"),
			Dungeon.DungeonId), Dungeon.ModifierScore > 0.0f);

		// AND EVERY ONE OF THEM BELONGS TO A CATACLYSM THE RUN IS FACING. This
		// is what makes the pool rule reach the game rather than only the
		// function: a run at tier 4 faces four of the eight, so half the table
		// must be unreachable.
		for (const FName Key : Dungeon.Modifiers)
		{
			const FCataclysmDungeonModifier* Row =
				Run->ModifierPool.FindByPredicate(
					[Key](const FCataclysmDungeonModifier& Candidate)
					{
						return Candidate.RowKey == Key;
					});

			TestNotNull(TEXT("the modifier is a row of the pool"), Row);
			if (Row != nullptr)
			{
				TestTrue(FString::Printf(
					TEXT("%s belongs to a Cataclysm this run faces"),
					*Key.ToString()),
					Run->ActiveCataclysms.Contains(Row->Cataclysm));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierNoPoolTest,
	"Cataclysm.DungeonModifiers.ARunWithNoPoolGivesNoModifiersAndDoesNotRefuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierNoPoolTest::RunTest(const FString& Parameters)
{
	// THE STATE EVERY OTHER EMPIRE TEST IS IN, and the state the game is in
	// when the DataTable cannot be read. It has to behave exactly as the game
	// did before dungeon modifiers existed rather than refusing to run.
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(/* Seed */ 42, ECataclysmSurgeMode::Static,
			   /* LethalityRung */ 0, /* DifficultyTier */ 4);

	Run->AdvanceDay();

	TestTrue(TEXT("a surge still landed"), Run->Dungeons.Num() > 0);

	for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
	{
		TestEqual(TEXT("it carries no modifiers"), Dungeon.Modifiers.Num(), 0);
		TestEqual(TEXT("and its modifier score is zero"),
				  Dungeon.ModifierScore, 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierSeedTest,
	"Cataclysm.DungeonModifiers.TheSameSeedDrawsTheSameModifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierSeedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;

	UCataclysmEmpireRun* First = MakeRun(/* Seed */ 9, /* DifficultyTier */ 5);
	UCataclysmEmpireRun* Same = MakeRun(/* Seed */ 9, /* DifficultyTier */ 5);
	UCataclysmEmpireRun* Other = MakeRun(/* Seed */ 10, /* DifficultyTier */ 5);

	for (UCataclysmEmpireRun* Run : { First, Same, Other })
	{
		for (int32 Day = 0; Day < 40; ++Day)
		{
			Run->AdvanceDay();
		}
	}

	TestTrue(TEXT("the run landed dungeons at all"), First->Dungeons.Num() > 0);

	TestEqual(TEXT("the same seed lands the same number of dungeons"),
			  Same->Dungeons.Num(), First->Dungeons.Num());

	const int32 Shared = FMath::Min(First->Dungeons.Num(), Same->Dungeons.Num());
	for (int32 Index = 0; Index < Shared; ++Index)
	{
		TestEqual(FString::Printf(
			TEXT("and gives dungeon %d the same modifiers"), Index),
			Join(Same->Dungeons[Index].Modifiers),
			Join(First->Dungeons[Index].Modifiers));
	}

	// AND A DIFFERENT SEED DOES NOT. Without this the check above would pass on
	// an implementation that gave every dungeon in every run the same modifier.
	bool bAnyDifferent = false;
	const int32 Both = FMath::Min(First->Dungeons.Num(), Other->Dungeons.Num());
	for (int32 Index = 0; Index < Both; ++Index)
	{
		if (Join(Other->Dungeons[Index].Modifiers)
			!= Join(First->Dungeons[Index].Modifiers))
		{
			bAnyDifferent = true;
			break;
		}
	}
	TestTrue(TEXT("a different seed draws different modifiers somewhere"),
			 bAnyDifferent);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierStreamTest,
	"Cataclysm.DungeonModifiers.TheDrawDoesNotShiftAnythingElseTheRunRolls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierStreamTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;

	// **THE PROPERTY THAT KEPT EVERY FIXED-SEED TEST IN THIS PROJECT PASSING.**
	// The modifier draw consumes one number per modifier. Taken from the run's
	// main stream it would shift every later draw, so the same seed would land
	// different waves on different cities at different depths than it did before
	// dungeon modifiers existed -- and a test measuring something else entirely
	// would start failing for a reason that has nothing to do with what it
	// measures. `UCataclysmRoster::ModifierSalt` is what keeps the streams
	// apart; this is the check that it worked.
	//
	// TIER 8, SO THE DRAW CONSUMES AS MANY NUMBERS AS IT EVER WILL: eight per
	// dungeon and sixteen for a Sacrificial one. At tier 1 it would move a
	// shared stream by one per dungeon, which could coincide.
	UCataclysmEmpireRun* WithPool = MakeRun(/* Seed */ 5, /* DifficultyTier */ 8);

	UCataclysmEmpireRun* WithoutPool = NewObject<UCataclysmEmpireRun>();
	WithoutPool->Begin(/* Seed */ 5, ECataclysmSurgeMode::Static,
					   /* LethalityRung */ 0, /* DifficultyTier */ 8);

	for (int32 Day = 0; Day < 60; ++Day)
	{
		WithPool->AdvanceDay();
		WithoutPool->AdvanceDay();
	}

	TestEqual(TEXT("both runs landed the same number of dungeons"),
			  WithPool->Dungeons.Num(), WithoutPool->Dungeons.Num());

	const int32 Shared = FMath::Min(WithPool->Dungeons.Num(),
									WithoutPool->Dungeons.Num());
	for (int32 Index = 0; Index < Shared; ++Index)
	{
		const FCataclysmDungeon& A = WithPool->Dungeons[Index];
		const FCataclysmDungeon& B = WithoutPool->Dungeons[Index];

		TestEqual(TEXT("on the same city"), A.CityId, B.CityId);
		TestEqual(TEXT("at the same depth"), A.Floors, B.Floors);
		TestEqual(TEXT("with the same sub-type"),
				  static_cast<int32>(A.SubType), static_cast<int32>(B.SubType));
		TestEqual(TEXT("sent by the same Cataclysm"),
				  static_cast<int32>(A.Cataclysm),
				  static_cast<int32>(B.Cataclysm));
		TestEqual(TEXT("and on the same day"), A.SpawnedDay, B.SpawnedDay);
	}

	// AND THE ONE THAT HAD A POOL REALLY DID DRAW. Without this the comparison
	// above would be satisfied by a run that drew nothing at all, which is
	// exactly the failure it exists to rule out.
	TestTrue(TEXT("the run with a pool gave out modifiers"),
			 WithPool->Dungeons.Num() > 0
				&& WithPool->Dungeons[0].Modifiers.Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierSacrificialRunTest,
	"Cataclysm.DungeonModifiers.ASacrificialDungeonInARunReallyGetsDouble",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierSacrificialRunTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;

	// SEARCHED FOR RATHER THAN ASSUMED, AND THE SEARCH IS ASSERTED. Sacrificial
	// carries 8 of the 100 spawn weight, so no fixed number of days is
	// guaranteed to produce one. A test that quietly found none would pass
	// having checked nothing, which is the failure this shape avoids.
	//
	// COPIES AND NOT POINTERS. `AdvanceDay` adds to and removes from
	// `Run->Dungeons`, so a pointer into it does not survive the next day.
	UCataclysmEmpireRun* Run = MakeRun(/* Seed */ 3, /* DifficultyTier */ 3);

	FCataclysmDungeon Sacrificial;
	FCataclysmDungeon Plain;
	bool bFoundSacrificial = false;
	bool bFoundPlain = false;

	for (int32 Day = 0; Day < 400 && !(bFoundSacrificial && bFoundPlain); ++Day)
	{
		Run->AdvanceDay();

		for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
		{
			if (!bFoundSacrificial
				&& Dungeon.SubType == ECataclysmDungeonSubType::Sacrificial)
			{
				Sacrificial = Dungeon;
				bFoundSacrificial = true;
			}
			else if (!bFoundPlain
					 && Dungeon.SubType == ECataclysmDungeonSubType::None)
			{
				Plain = Dungeon;
				bFoundPlain = true;
			}
		}
	}

	if (!bFoundSacrificial || !bFoundPlain)
	{
		AddError(TEXT("400 days produced no Sacrificial dungeon and a plain one "
					  "to compare it with, so this test checked nothing. Pick "
					  "another seed."));
		return false;
	}

	TestEqual(TEXT("a tier 3 plain dungeon carries three"),
			  Plain.Modifiers.Num(), 3);
	TestEqual(TEXT("and a tier 3 Sacrificial one carries six"),
			  Sacrificial.Modifiers.Num(), 6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierDescribedTest,
	"Cataclysm.DungeonModifiers.TheRunSaysWhatEachDungeonsModifiersAreWorth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierDescribedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;

	// **THE ONLY PLACE THE FEATURE IS VISIBLE OUTSIDE ITS OWN TESTS.** No screen
	// shows a dungeon's modifiers, so `Cataclysm.EmpireStatus` is how a person
	// sees that a dungeon has any at all. A feature nothing can be seen doing is
	// a feature nobody can report a bug about.
	UCataclysmEmpireRun* Run = MakeRun(/* Seed */ 42, /* DifficultyTier */ 4);
	Run->AdvanceDay();

	if (!TestTrue(TEXT("a surge landed"), Run->Dungeons.Num() > 0))
	{
		return false;
	}

	const FCataclysmDungeon& Dungeon = Run->Dungeons[0];
	const FString Description = Run->Describe();

	TestTrue(TEXT("the description says how many modifiers a dungeon carries"),
		Description.Contains(FString::Printf(TEXT("%d modifiers worth"),
											 Dungeon.Modifiers.Num()),
							 ESearchCase::CaseSensitive));

	TestTrue(TEXT("and how much danger they add"),
		Description.Contains(FString::Printf(TEXT("worth %.0f danger"),
											 Dungeon.ModifierScore),
							 ESearchCase::CaseSensitive));

	// AND A RUN WHOSE DUNGEONS HAVE NONE SAYS NOTHING RATHER THAN "0 modifiers".
	// That is every headless test and the game itself when the DataTable cannot
	// be read, so it is the common case rather than an edge one.
	UCataclysmEmpireRun* Bare = NewObject<UCataclysmEmpireRun>();
	Bare->Begin(/* Seed */ 42, ECataclysmSurgeMode::Static,
				/* LethalityRung */ 0, /* DifficultyTier */ 4);
	Bare->AdvanceDay();

	TestFalse(TEXT("a dungeon with no modifiers is not described as having any"),
			  Bare->Describe().Contains(TEXT("modifiers worth"),
										ESearchCase::CaseSensitive));

	// AND THE TWO RUNS REALLY DID DIFFER, so the check above is a comparison and
	// not a sentence that never appears.
	TestTrue(TEXT("the run with a pool gave its first dungeon modifiers"),
			 Dungeon.Modifiers.Num() > 0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
