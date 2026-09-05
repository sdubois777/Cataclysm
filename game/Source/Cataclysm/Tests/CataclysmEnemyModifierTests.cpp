// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

/**
 * Tests for which modifiers a creature is given, issue #742.
 *
 * WHAT WAS WRONG. Nothing gave any creature a modifier. The 79 rows have been in
 * `game/Data/EnemyModifiers.csv` since the workbook was imported, the list on
 * the creature has existed since issue #740 and the hover panel has printed it,
 * and the list was empty on every creature the game ever spawned unless somebody
 * typed one in by hand. The design's one-per-rung-above-Common rule had never
 * run once.
 *
 * WHAT THESE COVER AND WHAT THEY DO NOT. The draw is arithmetic over a data
 * table and a random stream, so all of it is covered here. **Whether a modifier
 * DOES anything is a different question** and mostly still answered no: the
 * effects are being built one at a time, and a creature carrying a name whose
 * effect does not exist behaves exactly as it did before.
 */

namespace CataclysmEnemyModifierTest
{
	const UDataTable* Table()
	{
		return UCataclysmEnemyModifiers::LoadEnemyModifierTable();
	}

	/** How many rows of the table belong to one Cataclysm. */
	int32 CountOfType(const UDataTable* ModifierTable, const TCHAR* Type)
	{
		int32 Found = 0;
		if (!ModifierTable)
		{
			return Found;
		}

		ModifierTable->ForeachRow<FCataclysmEnemyModifierRow>(
			TEXT("CataclysmEnemyModifierTest::CountOfType"),
			[&Found, Type](const FName&, const FCataclysmEnemyModifierRow& Row)
			{
				if (Row.CataclysmType.Equals(Type, ESearchCase::IgnoreCase))
				{
					++Found;
				}
			});

		return Found;
	}
}

#define CATACLYSM_MODIFIER_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// How many
// ---------------------------------------------------------------------------

CATACLYSM_MODIFIER_TEST(FCataclysmModifierCountPerRungTest,
	"Cataclysm.EnemyModifiers.EachRungCarriesTheCountTheDesignStates")
{
	// EVERY RUNG WRITTEN OUT, AGAINST THE DESIGN'S OWN TABLE, and not derived
	// from the step the implementation derives it from. `docs/Cataclysm_GDD_v2.md`
	// states these six figures; that they happen to equal the rarity step is
	// what makes the implementation short, and a test that also used the step
	// would agree with the code for the same wrong reason if the ladder moved.
	TestEqual(TEXT("a Common carries none"),
			  UCataclysmEnemyModifiers::CountForRarityStep(0), 0);
	TestEqual(TEXT("an Elite carries one"),
			  UCataclysmEnemyModifiers::CountForRarityStep(1), 1);
	TestEqual(TEXT("a Legendary carries two"),
			  UCataclysmEnemyModifiers::CountForRarityStep(2), 2);
	TestEqual(TEXT("a Herald carries three"),
			  UCataclysmEnemyModifiers::CountForRarityStep(3), 3);
	TestEqual(TEXT("a Boss carries four"),
			  UCataclysmEnemyModifiers::CountForRarityStep(4), 4);
	TestEqual(TEXT("a Cataclysm Boss carries five"),
			  UCataclysmEnemyModifiers::CountForRarityStep(5), 5);

	// A NEGATIVE STEP IS A CALLER ERROR ANSWERED WITH NONE, the same way
	// `SetRarityStep` answers one with Common. Carrying a negative number of
	// modifiers is not a state, and Common is the rung that gives least, so a
	// fault cannot quietly make a creature harder.
	TestEqual(TEXT("a negative step carries none"),
			  UCataclysmEnemyModifiers::CountForRarityStep(-3), 0);

	return true;
}

// ---------------------------------------------------------------------------
// Which ones
// ---------------------------------------------------------------------------

CATACLYSM_MODIFIER_TEST(FCataclysmModifierPoolTest,
	"Cataclysm.EnemyModifiers.ACreatureDrawsFromItsOwnCataclysmAndGeneric")
{
	using namespace CataclysmEnemyModifierTest;

	const UDataTable* ModifierTable = Table();
	if (!TestNotNull(TEXT("the enemy modifier table loads"), ModifierTable))
	{
		return false;
	}

	// COUNTED FROM THE TABLE RATHER THAN WRITTEN IN, so this keeps working when
	// the workbook gains a modifier. Measured on 2026-09-05 the two are 8 and
	// 10, and the test says so without depending on it.
	const int32 Demonic = CountOfType(ModifierTable, TEXT("Demonic"));
	const int32 Generic = CountOfType(ModifierTable, TEXT("Generic"));

	TestTrue(TEXT("the table holds some Demonic modifiers"), Demonic > 0);
	TestTrue(TEXT("and some Generic ones"), Generic > 0);

	const TArray<FName> Pool = UCataclysmEnemyModifiers::PoolFor(
		ModifierTable, FName(TEXT("Demonic")));

	// THE DESIGN'S RULE, IN ONE NUMBER: "drawn from its own Cataclysm's column
	// and the Generic one". Anything else in the pool is a modifier belonging to
	// a Cataclysm this creature is not.
	TestEqual(TEXT("a Demonic creature's pool is its own column plus Generic"),
			  Pool.Num(), Demonic + Generic);

	// AND NOTHING FROM ANOTHER CATACLYSM IS IN IT. The count above would also
	// pass if the pool held the right NUMBER of the wrong rows.
	for (const FName& Key : Pool)
	{
		const FCataclysmEnemyModifierRow* Row =
			UCataclysmEnemyModifiers::FindRow(ModifierTable, Key);
		if (!TestNotNull(*FString::Printf(TEXT("%s is a real row"),
										  *Key.ToString()), Row))
		{
			continue;
		}

		const bool bAllowed =
			Row->CataclysmType.Equals(TEXT("Demonic"), ESearchCase::IgnoreCase)
			|| Row->CataclysmType.Equals(TEXT("Generic"),
										 ESearchCase::IgnoreCase);

		TestTrue(*FString::Printf(
					 TEXT("%s is Demonic or Generic, not %s"),
					 *Key.ToString(), *Row->CataclysmType), bAllowed);
	}

	// A DIFFERENT CATACLYSM DRAWS A DIFFERENT POOL, which is the half of the
	// rule the check above cannot see: a pool that ignored the type entirely
	// and answered "Demonic plus Generic" for everybody would pass everything
	// so far.
	const TArray<FName> WarPool = UCataclysmEnemyModifiers::PoolFor(
		ModifierTable, FName(TEXT("War")));

	TestTrue(TEXT("a War creature's pool is not a Demonic one"),
			 WarPool != Pool);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmModifierPoolIsSortedTest,
	"Cataclysm.EnemyModifiers.ThePoolIsSortedSoTheSameSeedDrawsTheSame")
{
	using namespace CataclysmEnemyModifierTest;

	const UDataTable* ModifierTable = Table();
	if (!TestNotNull(TEXT("the enemy modifier table loads"), ModifierTable))
	{
		return false;
	}

	// A `UDataTable` IS A MAP AND WALKING ONE HAS NO GUARANTEED ORDER, so an
	// unsorted pool would hand the same seed a different modifier on a different
	// run. That is not a hypothetical failure mode in this project:
	// `UCataclysmEnemyRarity::SpawnableSteps` carries the same note and sorts
	// for the same reason.
	const TArray<FName> Pool = UCataclysmEnemyModifiers::PoolFor(
		ModifierTable, FName(TEXT("Demonic")));

	for (int32 Index = 1; Index < Pool.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("%s sorts before %s"),
								  *Pool[Index - 1].ToString(),
								  *Pool[Index].ToString()),
				 Pool[Index - 1].LexicalLess(Pool[Index]));
	}

	// AND THE SAME SEED DRAWS THE SAME THING TWICE, which is what the sort is
	// for. Two streams from one seed against one pool.
	FRandomStream First(4242);
	FRandomStream Second(4242);

	const TArray<FName> A = UCataclysmEnemyModifiers::Draw(
		ModifierTable, FName(TEXT("Demonic")), 3, First, {});
	const TArray<FName> B = UCataclysmEnemyModifiers::Draw(
		ModifierTable, FName(TEXT("Demonic")), 3, Second, {});

	TestTrue(TEXT("the same seed draws the same three"), A == B);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmModifierDrawTest,
	"Cataclysm.EnemyModifiers.ADrawIsDistinctAndAvoidsWhatIsAlreadyCarried")
{
	using namespace CataclysmEnemyModifierTest;

	const UDataTable* ModifierTable = Table();
	if (!TestNotNull(TEXT("the enemy modifier table loads"), ModifierTable))
	{
		return false;
	}

	FRandomStream Stream(7);

	// NO DUPLICATES. The project owner decided this on 2026-09-05, and neither
	// Diablo II's nor Path of Exile's public documentation states a rule either
	// way, so it is a judgement rather than something read off another game.
	// Three copies of one aura read to a player as one aura that hurts more.
	const TArray<FName> Drawn = UCataclysmEnemyModifiers::Draw(
		ModifierTable, FName(TEXT("Demonic")), 5, Stream, {});

	TestEqual(TEXT("five were asked for and five came back"), Drawn.Num(), 5);

	TSet<FName> Seen;
	for (const FName& Key : Drawn)
	{
		TestFalse(*FString::Printf(TEXT("%s was drawn once"), *Key.ToString()),
				  Seen.Contains(Key));
		Seen.Add(Key);
	}

	// WHAT THE CREATURE ALREADY CARRIES IS NEVER DRAWN AGAIN. This is what makes
	// a creature typed with a named modifier keep it and get the rest around it,
	// rather than getting the same one twice.
	const TArray<FName> Already = { Drawn[0], Drawn[1] };
	FRandomStream Again(7);

	const TArray<FName> Second = UCataclysmEnemyModifiers::Draw(
		ModifierTable, FName(TEXT("Demonic")), 3, Again, Already);

	TestEqual(TEXT("three more came back"), Second.Num(), 3);
	for (const FName& Key : Second)
	{
		TestFalse(*FString::Printf(TEXT("%s was not already carried"),
								   *Key.ToString()), Already.Contains(Key));
	}

	// ASKING FOR NONE DRAWS NONE, which is the Common case and the one that runs
	// most often.
	FRandomStream Unused(1);
	TestEqual(TEXT("a Common draws nothing"),
			  UCataclysmEnemyModifiers::Draw(
				  ModifierTable, FName(TEXT("Demonic")), 0, Unused, {}).Num(),
			  0);

	// ASKING FOR MORE THAN THE POOL HOLDS ANSWERS THE POOL, rather than looping
	// for ever. It cannot happen with the shipped data -- the smallest pool is
	// Famine's 7 plus 10 Generic and the most anything draws is 5 -- so this is
	// the guard rather than a case in play.
	FRandomStream Greedy(11);
	const TArray<FName> TooMany = UCataclysmEnemyModifiers::Draw(
		ModifierTable, FName(TEXT("Demonic")), 500, Greedy, {});

	TestEqual(TEXT("asking for 500 answers the whole pool"), TooMany.Num(),
			  UCataclysmEnemyModifiers::PoolFor(
				  ModifierTable, FName(TEXT("Demonic"))).Num());

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmModifierMissingTableTest,
	"Cataclysm.EnemyModifiers.NoTableDrawsNothingRatherThanCrashing")
{
	// A MISSING TABLE IS A BROKEN INSTALL, NOT A CRASH. Every loader in this
	// project answers null and logs, and a creature that draws nothing is the
	// same creature the game spawned before any of this existed.
	FRandomStream Stream(3);

	TestEqual(TEXT("no table means an empty pool"),
			  UCataclysmEnemyModifiers::PoolFor(nullptr,
												FName(TEXT("Demonic"))).Num(),
			  0);
	TestEqual(TEXT("and an empty draw"),
			  UCataclysmEnemyModifiers::Draw(nullptr, FName(TEXT("Demonic")), 3,
											 Stream, {}).Num(),
			  0);
	TestNull(TEXT("and no row for any key"),
			 UCataclysmEnemyModifiers::FindRow(nullptr,
											   FName(TEXT("Demonic_Beguiling"))));

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmModifierRowsExistTest,
	"Cataclysm.EnemyModifiers.EveryDemonicModifierNamedInTheDesignIsInTheTable")
{
	using namespace CataclysmEnemyModifierTest;

	const UDataTable* ModifierTable = Table();
	if (!TestNotNull(TEXT("the enemy modifier table loads"), ModifierTable))
	{
		return false;
	}

	// THE EIGHT DEMONIC MODIFIERS BY KEY. Written out rather than counted,
	// because the point is that these exact rows are what a Demonic creature can
	// draw, and the effects being built one at a time are named against these
	// keys. A row renamed in the workbook breaks the effect that reads it, and
	// this is what says so.
	const TCHAR* Keys[] = {
		TEXT("Demonic_Hellfire_Aura"),
		TEXT("Demonic_Beguiling"),
		TEXT("Demonic_Infernal_Sacrifice"),
		TEXT("Demonic_Unholy_Sigils"),
		TEXT("Demonic_Abyssal_Aura"),
		TEXT("Demonic_Sacrificial_Bond"),
		TEXT("Demonic_Inferno_Charge"),
		TEXT("Demonic_Infernal_Brand"),
	};

	const TArray<FName> Pool = UCataclysmEnemyModifiers::PoolFor(
		ModifierTable, FName(TEXT("Demonic")));

	for (const TCHAR* Key : Keys)
	{
		const FName RowKey(Key);
		const FCataclysmEnemyModifierRow* Row =
			UCataclysmEnemyModifiers::FindRow(ModifierTable, RowKey);

		if (!TestNotNull(*FString::Printf(TEXT("%s is a row in the table"), Key),
						 Row))
		{
			continue;
		}

		TestEqual(*FString::Printf(TEXT("%s is Demonic"), Key),
				  Row->CataclysmType, FString(TEXT("Demonic")));
		TestFalse(*FString::Printf(TEXT("%s says what it does"), Key),
				  Row->Description.IsEmpty());
		TestTrue(*FString::Printf(TEXT("%s is drawable by a Demonic creature"),
								  Key), Pool.Contains(RowKey));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
