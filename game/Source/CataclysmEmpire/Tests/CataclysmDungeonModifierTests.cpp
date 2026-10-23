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
 * `Cataclysm` module, is the one that reads the real 123 rows.
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
	// The design has 123 dungeon modifiers and this one is among them.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModifierFamineQuestTest,
	"Cataclysm.DungeonModifiers.AFamineQuestDungeonCarriesDoubleAndADeathOneDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModifierFamineQuestTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;

	// **A FAMINE QUEST DUNGEON IS SACRIFICIAL, SO IT DRAWS DOUBLE.** Since
	// 2026-10-21 each of five Cataclysms' Quest dungeons is one sub-type, and
	// Famine's is the one sub-type `CountFor` doubles. This is that rule on the
	// path the day loop takes: `FireSurge` gives modifiers after the dungeon is
	// made, so the count reads the fixed sub-type and not the rolled one.
	//
	// THE CONTROL IS A DEATH QUEST DUNGEON, a Horde, which carries the ordinary
	// count. Both Cataclysms are the only two the run faces, set after `Begin`
	// and before the first day, so the pool is their eight modifiers -- enough
	// for six -- and every Quest dungeon is one or the other.
	//
	// SEARCHED FOR RATHER THAN ASSUMED, AND THE SEARCH IS ASSERTED. A Quest
	// dungeon is 12 in 100 of what a surge lands, so no single campaign is
	// guaranteed to land one from each. COPIES AND NOT POINTERS: `AdvanceDay`
	// adds to and removes from `Run->Dungeons`.
	FCataclysmDungeon Famine;
	FCataclysmDungeon Death;
	bool bFoundFamine = false;
	bool bFoundDeath = false;

	for (int32 Seed = 1; Seed <= 40 && !(bFoundFamine && bFoundDeath); ++Seed)
	{
		UCataclysmEmpireRun* Run = MakeRun(Seed, /* DifficultyTier */ 3);
		Run->ActiveCataclysms = { ECataclysmType::Famine, ECataclysmType::Death };

		for (int32 Day = 0; Day < 600 && !(bFoundFamine && bFoundDeath)
			 && Run->Map->ExposedCities().Num() >= 2; ++Day)
		{
			Run->AdvanceDay();

			for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
			{
				if (Dungeon.Type != ECataclysmDungeonType::Quest)
				{
					continue;
				}

				if (!bFoundFamine && Dungeon.Cataclysm == ECataclysmType::Famine)
				{
					Famine = Dungeon;
					bFoundFamine = true;
				}
				else if (!bFoundDeath
						 && Dungeon.Cataclysm == ECataclysmType::Death)
				{
					Death = Dungeon;
					bFoundDeath = true;
				}
			}
		}
	}

	if (!bFoundFamine || !bFoundDeath)
	{
		AddError(TEXT("40 campaigns landed no Famine Quest dungeon and a Death "
					  "one to compare it with, so this test checked nothing."));
		return false;
	}

	TestEqual(TEXT("the Famine Quest dungeon is Sacrificial"),
			  static_cast<int32>(Famine.SubType),
			  static_cast<int32>(ECataclysmDungeonSubType::Sacrificial));
	TestEqual(TEXT("and at tier 3 it carries six modifiers"),
			  Famine.Modifiers.Num(), 6);

	TestEqual(TEXT("CONTROL: the Death Quest dungeon is a Horde"),
			  static_cast<int32>(Death.SubType),
			  static_cast<int32>(ECataclysmDungeonSubType::Horde));
	TestEqual(TEXT("and carries the ordinary three"),
			  Death.Modifiers.Num(), 3);

	return true;
}

// ---------------------------------------------------------------------------
// The Divine Edicts and the Ascendant -- 2026-10-22
// ---------------------------------------------------------------------------
//
// `docs/Cataclysm_GDD_v2.md`, Heaven's Wrath, and the coordinating session's
// rulings of 2026-10-22: one more Edict a floor up to four, drawn evenly from
// those not already in force on a stream of the dungeon's own, each a tenth
// stronger a floor deeper and half as strong again on the Core's floor while
// the Core stands, and one of them left on the city by the hit.

namespace CataclysmDungeonModifierTest
{
	const FName EdictOfSilenceKey(TEXT("Celestial_Edict_of_Silence"));
	const FName EdictOfTemperanceKey(TEXT("Celestial_Edict_of_Temperance"));
	const FName EdictOfPatienceKey(TEXT("Celestial_Edict_of_Patience"));
	const FName EdictOfStillnessKey(TEXT("Celestial_Edict_of_Stillness"));
	const FName EdictOfAbstinenceKey(TEXT("Celestial_Edict_of_Abstinence"));
	const FName EdictOfVigilKey(TEXT("Celestial_Edict_of_Vigil"));
	const FName EdictOfHumilityKey(TEXT("Celestial_Edict_of_Humility"));
	const FName EdictOfTitheKey(TEXT("Celestial_Forced_Tithes"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEdictKeysTest,
	"Cataclysm.DungeonModifiers.TheEdictsAreTheCelestialRowsTheDesignNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEdictKeysTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;
	using Rules = UCataclysmDungeonModifierRules;

	// ALL EIGHT THE DESIGN NAMES, IN ITS ORDER: Abstinence, Vigil and Humility rows since 2026-10-23 (G7c).
	const TArray<FName> Expected = { EdictOfSilenceKey, EdictOfTemperanceKey, EdictOfPatienceKey, EdictOfAbstinenceKey,
									  EdictOfStillnessKey, EdictOfVigilKey, EdictOfHumilityKey, EdictOfTitheKey };
	TestTrue(TEXT("the Edicts are the Edicts of Silence, Temperance, Patience, Abstinence, Stillness, Vigil and Humility "
				  "and Forced Tithes, in the design's order"),
			 Rules::EdictKeys() == Expected);

	TestTrue(TEXT("the Edict of Silence is an Edict"), Rules::IsEdict(EdictOfSilenceKey));
	TestTrue(TEXT("and so is Forced Tithes"), Rules::IsEdict(EdictOfTitheKey));
	TestEqual(TEXT("which a player is shown as the Edict of Silence"),
			  Rules::EdictName(EdictOfSilenceKey), FString(TEXT("Edict of Silence")));
	TestEqual(TEXT("and the design's Edict of Tithe"),
			  Rules::EdictName(EdictOfTitheKey), FString(TEXT("Edict of Tithe")));

	// THE CONTROL: another Celestial row is not an Edict, and has no Edict's name.
	const FName Spires(TEXT("Celestial_Golden_Spires"));
	TestFalse(TEXT("CONTROL: Golden Spires, a Celestial row, is not an Edict"), Rules::IsEdict(Spires));
	TestTrue(TEXT("CONTROL: and has no Edict's name"), Rules::EdictName(Spires).IsEmpty());
	TestFalse(TEXT("CONTROL: and no row is none"), Rules::IsEdict(NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEdictsAFloorTest,
	"Cataclysm.DungeonModifiers.AnAscendantAddsOneEdictAFloorAndEachFloorHoldsTheLastFloorsUntilNoneIsLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEdictsAFloorTest::RunTest(const FString& Parameters)
{
	using Rules = UCataclysmDungeonModifierRules;

	TestEqual(TEXT("four at most, the owner's"), Rules::AscendantMostEdicts, 4);
	TestEqual(TEXT("and all eight Edicts are rows"), Rules::EdictKeys().Num(), 8);

	const TArray<FName> NoneActive;
	TestEqual(TEXT("a floor numbered below one adds none"), Rules::EdictsOnFloor(7, NoneActive, 0).Num(), 0);

	int32 Wrong = 0;
	TSet<FName> FirstFloors;
	for (int32 Seed = 1; Seed <= 200; ++Seed)
	{
		TArray<FName> Previous;
		for (int32 Floor = 1; Floor <= 6; ++Floor)
		{
			const TArray<FName> On = Rules::EdictsOnFloor(Seed, NoneActive, Floor);

			// ONE, TWO, THREE, FOUR, AND NO MORE THAN THERE ARE: "when none is left to draw, no further Edict is
			// added".
			const int32 Wanted = FMath::Min3(Floor, Rules::AscendantMostEdicts, Rules::EdictKeys().Num());
			if (On.Num() != Wanted)
			{
				++Wrong;
				AddError(FString::Printf(TEXT("seed %d floor %d holds %d Edicts, not %d"), Seed, Floor, On.Num(),
										 Wanted));
				continue;
			}

			// FLOOR N HOLDS FLOOR N-1'S, IN THE SAME ORDER.
			for (int32 Index = 0; Index < Previous.Num(); ++Index)
			{
				if (On[Index] != Previous[Index])
				{
					++Wrong;
					AddError(FString::Printf(TEXT("seed %d floor %d does not hold floor %d's Edicts"), Seed, Floor,
											 Floor - 1));
					break;
				}
			}

			// NO REPEATS, AND EVERY ONE AN EDICT.
			TSet<FName> Distinct;
			for (const FName& Edict : On)
			{
				Distinct.Add(Edict);
			}
			if (Distinct.Num() != On.Num())
			{
				++Wrong;
				AddError(FString::Printf(TEXT("seed %d floor %d repeats an Edict"), Seed, Floor));
			}
			for (const FName& Edict : On)
			{
				if (!Rules::IsEdict(Edict))
				{
					++Wrong;
					AddError(FString::Printf(TEXT("seed %d floor %d holds %s, which is no Edict"), Seed, Floor,
											 *Edict.ToString()));
				}
			}

			if (Floor == 1 && On.Num() == 1)
			{
				FirstFloors.Add(On[0]);
			}
			Previous = On;
		}

		// AND THE SAME ANSWER WHOEVER ASKS, AS OFTEN AS THEY ASK.
		if (Rules::EdictsOnFloor(Seed, NoneActive, 4) != Rules::EdictsOnFloor(Seed, NoneActive, 4))
		{
			++Wrong;
			AddError(FString::Printf(TEXT("seed %d answered two orders"), Seed));
		}
	}

	TestEqual(TEXT("every floor of every seed held what it should"), Wrong, 0);

	// THE CONTROL: the order really is drawn, so "holds the last floor's" is not one fixed list read twice.
	TestEqual(TEXT("CONTROL: every Edict comes first on some seed"), FirstFloors.Num(), Rules::EdictKeys().Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEdictsEvenTest,
	"Cataclysm.DungeonModifiers.AnAscendantsEdictsAreDrawnEvenlyAndNotByTheirDanger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEdictsEvenTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;
	using Rules = UCataclysmDungeonModifierRules;

	// THE OWNER'S RULING OF 2026-09-07, APPLIED TO THE EDICTS: an equal chance. The eight rows' dangers are 20, 20,
	// 15, 15, 10, 15, 15 and 15, 125 in all; drawn by them the Edict of Silence would come first 20 times in 125, 0.16.
	// Drawn evenly, one time in as many as there are Edicts: one in eight.
	constexpr int32 Seeds = 4000;
	int32 SilenceFirst = 0;
	const TArray<FName> NoneActive;
	for (int32 Seed = 1; Seed <= Seeds; ++Seed)
	{
		SilenceFirst += Rules::EdictsOnFloor(Seed, NoneActive, 1)[0] == EdictOfSilenceKey ? 1 : 0;
	}

	const float Share = static_cast<float>(SilenceFirst) / Seeds;
	AddInfo(FString::Printf(TEXT("the Edict of Silence came first on %d of %d seeds (%.3f)"), SilenceFirst, Seeds,
							Share));
	const float Even = 1.0f / Rules::EdictKeys().Num();
	TestTrue(TEXT("the Edict of Silence comes first about one time in as many as there are Edicts"),
			 Share > Even - 0.05f && Share < Even + 0.05f);
	TestTrue(TEXT("CONTROL: which is not the 20 in 125 a draw by danger would give"),
			 FMath::Abs(Share - 20.0f / 125.0f) > 0.02f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEdictsAlreadyActiveTest,
	"Cataclysm.DungeonModifiers.AnEdictAlreadyAmongADungeonsModifiersIsNotDrawnAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEdictsAlreadyActiveTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;
	using Rules = UCataclysmDungeonModifierRules;

	const TArray<FName> SilenceActive = { FName(TEXT("Famine_Starvation")), EdictOfSilenceKey };
	const TArray<FName> AllActive = Rules::EdictKeys();

	for (int32 Seed = 1; Seed <= 50; ++Seed)
	{
		// THE ORDER WITH SILENCE TAKEN OUT: what a floor adds when Silence is already in force.
		TArray<FName> WithoutSilence = Rules::EdictOrderFor(Seed);
		WithoutSilence.Remove(EdictOfSilenceKey);
		for (int32 Floor = 1; Floor <= 5; ++Floor)
		{
			TArray<FName> Expected = WithoutSilence;
			Expected.SetNum(FMath::Min3(Floor, Rules::AscendantMostEdicts, WithoutSilence.Num()));
			const TArray<FName> Added = Rules::EdictsOnFloor(Seed, SilenceActive, Floor);
			TestTrue(FString::Printf(TEXT("seed %d floor %d: with Silence in force the others are added in their turn"),
									 Seed, Floor),
					 Added == Expected);
			TestFalse(FString::Printf(TEXT("seed %d floor %d: and Silence is not added again"), Seed, Floor),
					  Added.Contains(EdictOfSilenceKey));
			TestEqual(FString::Printf(TEXT("seed %d floor %d: with every Edict in force none is"), Seed, Floor),
					  Rules::EdictsOnFloor(Seed, AllActive, Floor).Num(), 0);
		}

		// THE CONTROL: with nothing in force, floor 2 adds both.
		TestEqual(FString::Printf(TEXT("CONTROL: seed %d floor 2 with nothing in force adds both"), Seed),
				  Rules::EdictsOnFloor(Seed, TArray<FName>(), 2).Num(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEdictStrengthTest,
	"Cataclysm.DungeonModifiers.AnEdictIsATenthStrongerAFloorAndHalfAgainWhileTheCoreStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEdictStrengthTest::RunTest(const FString& Parameters)
{
	using Rules = UCataclysmDungeonModifierRules;

	TestEqual(TEXT("a tenth a floor, the owner's"), Rules::EdictStrongerPerFloor, 0.10f, 0.000001f);
	TestEqual(TEXT("half again while the Core stands"), Rules::EdictStrongerWhileTheCoreStands, 1.5f, 0.000001f);

	TestEqual(TEXT("floor 1 is the base"), Rules::EdictStrengthOn(1, false), 1.0f, 0.0001f);
	TestEqual(TEXT("floor 2 is a tenth stronger"), Rules::EdictStrengthOn(2, false), 1.1f, 0.0001f);
	TestEqual(TEXT("floor 6 half again"), Rules::EdictStrengthOn(6, false), 1.5f, 0.0001f);
	TestEqual(TEXT("floor 70, the deepest Quest dungeon's last, 7.9 times"), Rules::EdictStrengthOn(70, false), 7.9f,
			  0.0001f);
	TestEqual(TEXT("floor 70 with its Core standing, half again"), Rules::EdictStrengthOn(70, true), 11.85f, 0.001f);
	TestEqual(TEXT("floor 1 with a Core standing on it"), Rules::EdictStrengthOn(1, true), 1.5f, 0.0001f);
	TestEqual(TEXT("a floor below 1 is read as floor 1"), Rules::EdictStrengthOn(0, false), 1.0f, 0.0001f);

	// THE CONTROL: the Core's half is the only difference between standing and destroyed.
	TestEqual(TEXT("CONTROL: the Core destroyed takes away exactly its half"),
			  Rules::EdictStrengthOn(12, true) / Rules::EdictStrengthOn(12, false), 1.5f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEdictLeftTest,
	"Cataclysm.DungeonModifiers.TheEdictAHitLeavesIsOneTheAscendantWouldHoldFromFloorFour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEdictLeftTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModifierTest;
	using Rules = UCataclysmDungeonModifierRules;

	const TArray<FName> NoneActive;
	const TArray<FName> SilenceActive = { EdictOfSilenceKey };
	const TArray<FName> AllActive = Rules::EdictKeys();

	TSet<FName> Left;
	for (int32 Seed = 1; Seed <= 100; ++Seed)
	{
		for (int32 Day = 10; Day <= 40; Day += 10)
		{
			const FName One = Rules::EdictLeftBy(Seed, NoneActive, Day);
			TestTrue(FString::Printf(TEXT("seed %d day %d leaves one it would hold from floor 4"), Seed, Day),
					 Rules::EdictsOnFloor(Seed, NoneActive, Rules::AscendantMostEdicts).Contains(One));
			TestEqual(FString::Printf(TEXT("seed %d day %d leaves the same one asked twice"), Seed, Day),
					  Rules::EdictLeftBy(Seed, NoneActive, Day), One);
			const FName WithSilence = Rules::EdictLeftBy(Seed, SilenceActive, Day);
			TestTrue(FString::Printf(TEXT("seed %d day %d: with Silence among its own it leaves another it would hold"),
									 Seed, Day),
					 WithSilence != EdictOfSilenceKey
						 && Rules::EdictsOnFloor(Seed, SilenceActive, Rules::AscendantMostEdicts).Contains(WithSilence));
			Left.Add(One);
		}
		TestTrue(FString::Printf(TEXT("seed %d: with every Edict among its own it holds none and leaves none"), Seed),
				 Rules::EdictLeftBy(Seed, AllActive, 10).IsNone());
	}

	// THE CONTROL: "one at random" really is drawn, so every Edict is left by some.
	TestEqual(TEXT("CONTROL: every Edict is left by some"), Left.Num(), Rules::EdictKeys().Num());
	return true;
}

// ---------------------------------------------------------------------------
// Three more Edicts are rows: Temperance, Patience and Stillness -- 2026-10-22, G7b
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEdictsEachDrawnTest,
	"Cataclysm.DungeonModifiers.AnAscendantCanDrawEachOfItsEdictsAndNamesEachAsTheDesignDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEdictsEachDrawnTest::RunTest(const FString& Parameters)
{
	using Rules = UCataclysmDungeonModifierRules;

	const FName Temperance(TEXT("Celestial_Edict_of_Temperance"));
	const FName Patience(TEXT("Celestial_Edict_of_Patience"));
	const FName Stillness(TEXT("Celestial_Edict_of_Stillness"));

	// THE THREE ARE EDICTS, NAMED AS THE DESIGN NAMES THEM.
	TestTrue(TEXT("the Edict of Temperance is an Edict"), Rules::IsEdict(Temperance));
	TestTrue(TEXT("and the Edict of Patience"), Rules::IsEdict(Patience));
	TestTrue(TEXT("and the Edict of Stillness"), Rules::IsEdict(Stillness));
	TestEqual(TEXT("shown as the Edict of Temperance"), Rules::EdictName(Temperance), FString(TEXT("Edict of Temperance")));
	TestEqual(TEXT("shown as the Edict of Patience"), Rules::EdictName(Patience), FString(TEXT("Edict of Patience")));
	TestEqual(TEXT("shown as the Edict of Stillness"), Rules::EdictName(Stillness), FString(TEXT("Edict of Stillness")));

	// EVERY EDICT ON THE LIST COMES FIRST ON SOME SEED, AND IS HELD FROM FLOOR 4 ON SOME SEED.
	TSet<FName> First;
	TSet<FName> HeldByFour;
	int32 NotAnEdict = 0;
	for (int32 Seed = 1; Seed <= 400; ++Seed)
	{
		First.Add(Rules::EdictsOnFloor(Seed, TArray<FName>(), 1)[0]);
		for (const FName& Edict : Rules::EdictsOnFloor(Seed, TArray<FName>(), Rules::AscendantMostEdicts))
		{
			HeldByFour.Add(Edict);
			NotAnEdict += Rules::EdictName(Edict).IsEmpty() ? 1 : 0;
		}
	}
	for (const FName& Edict : Rules::EdictKeys())
	{
		TestTrue(FString::Printf(TEXT("%s comes first on some seed"), *Edict.ToString()), First.Contains(Edict));
		TestTrue(FString::Printf(TEXT("%s is held from floor 4 on some seed"), *Edict.ToString()),
				 HeldByFour.Contains(Edict));
	}
	TestTrue(TEXT("the three new ones among them"),
			 First.Contains(Temperance) && First.Contains(Patience) && First.Contains(Stillness));
	TestEqual(TEXT("and every one drawn has an Edict's name"), NotAnEdict, 0);

	// THE CONTROL: nothing drawn is outside the list, so "every one comes first" is not a list of anything.
	TestEqual(TEXT("CONTROL: no more Edicts come first than are listed"), First.Num(), Rules::EdictKeys().Num());
	TestFalse(TEXT("CONTROL: Golden Spires, a Celestial row, is never drawn as an Edict"),
			  HeldByFour.Contains(FName(TEXT("Celestial_Golden_Spires"))));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
