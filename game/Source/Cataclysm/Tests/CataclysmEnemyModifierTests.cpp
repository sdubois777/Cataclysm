// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmFloorPopulation.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

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

	/**
	 * A player pawn with the player state its ability system lives on.
	 *
	 * A PAWN ALONE HAS NO ATTRIBUTES. This project puts a character's ability
	 * system on the player state rather than on the pawn, because a pawn is
	 * destroyed on death and the state is not, so a pawn spawned without one
	 * carries nothing at all.
	 */
	ACataclysmPlayerCharacter* SpawnPlayerWithState(UWorld* World)
	{
		ACataclysmPlayerCharacter* Pawn =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector,
														 FRotator::ZeroRotator);
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		if (Pawn && State)
		{
			Pawn->SetPlayerState(State);
			Pawn->OnRep_PlayerState();
		}
		return Pawn;
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

// ---------------------------------------------------------------------------
// What the carried modifiers do
// ---------------------------------------------------------------------------

CATACLYSM_MODIFIER_TEST(FCataclysmModifierEffectsExistTest,
	"Cataclysm.EnemyModifiers.EveryModifierWithAnEffectIsStillInTheTable")
{
	using namespace CataclysmEnemyModifierTest;

	const UDataTable* ModifierTable = Table();
	if (!TestNotNull(TEXT("the enemy modifier table loads"), ModifierTable))
	{
		return false;
	}

	// A ROW RENAMED IN THE DESIGN WORKBOOK WOULD OTHERWISE MAKE AN EFFECT STOP
	// HAPPENING SILENTLY. The constants are the only place these keys are
	// written, and a creature that drew a renamed row would carry a name whose
	// effect never fires with nothing saying so.
	const TCHAR* Built[] = {
		UCataclysmEnemyModifiers::TitanicResolveRow,
		UCataclysmEnemyModifiers::OverpoweredRow,
		UCataclysmEnemyModifiers::BloodthirstyRow,
		UCataclysmEnemyModifiers::ThornsOfGlassRow,
		UCataclysmEnemyModifiers::HellfireAuraRow,
	};

	for (const TCHAR* Key : Built)
	{
		TestNotNull(*FString::Printf(
						TEXT("%s, which has an effect, is a row in the table"),
						Key),
					UCataclysmEnemyModifiers::FindRow(ModifierTable, FName(Key)));
	}

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmModifierStatEffectsTest,
	"Cataclysm.EnemyModifiers.TheStatChangingModifiersAnswerTheirOwnNumbers")
{
	const FName Titanic(UCataclysmEnemyModifiers::TitanicResolveRow);
	const FName Overpowered(UCataclysmEnemyModifiers::OverpoweredRow);
	const FName Bloodthirsty(UCataclysmEnemyModifiers::BloodthirstyRow);
	const FName Thorns(UCataclysmEnemyModifiers::ThornsOfGlassRow);
	const FName Hellfire(UCataclysmEnemyModifiers::HellfireAuraRow);

	// A CREATURE WITH NO MODIFIERS IS UNTOUCHED, which is 60% of what spawns.
	// Every answer here has to be the identity for the stat it moves.
	const TArray<FName> None;
	TestEqual(TEXT("no modifiers leave health alone"),
			  UCataclysmEnemyModifiers::MaxHealthMultiplier(None), 1.0f);
	TestTrue(TEXT("no modifiers force no critical strike chance"),
			 UCataclysmEnemyModifiers::ForcedCritChance(None) < 0.0f);
	TestEqual(TEXT("no modifiers grant no leech"),
			  UCataclysmEnemyModifiers::LifeLeechPercent(None), 0.0f);
	TestEqual(TEXT("no modifiers grant no retaliation"),
			  UCataclysmEnemyModifiers::RetaliationPercent(None), 0.0f);

	// A MODIFIER THAT HAS NO EFFECT BUILT CHANGES NOTHING, which is most of the
	// 79 and is the honest state of the work. Beguiling is one of them.
	const TArray<FName> Unbuilt = { FName(TEXT("Demonic_Beguiling")) };
	TestEqual(TEXT("a modifier with no effect built leaves health alone"),
			  UCataclysmEnemyModifiers::MaxHealthMultiplier(Unbuilt), 1.0f);

	// TITANIC RESOLVE: "50% more health".
	TestEqual(TEXT("Titanic Resolve multiplies health by one and a half"),
			  UCataclysmEnemyModifiers::MaxHealthMultiplier({Titanic}), 1.5f);

	// OVERPOWERED: "Always crits".
	TestEqual(TEXT("Overpowered forces a hundred percent critical chance"),
			  UCataclysmEnemyModifiers::ForcedCritChance({Overpowered}), 100.0f);

	// BLOODTHIRSTY: "Heal for 10% of the damage dealt to the player".
	TestEqual(TEXT("Bloodthirsty grants ten percent life leech"),
			  UCataclysmEnemyModifiers::LifeLeechPercent({Bloodthirsty}), 10.0f);

	// THORNS OF GLASS: "Reflects 50% of all damage taken back to the attacker."
	//
	// IT USED TO REFLECT THE WHOLE HIT AND LEAVE THE CREATURE ON ONE HEALTH.
	// The project owner changed it on 2026-09-05 in the design workbook, so this
	// checks the new figure and that it leaves health alone -- which is what it
	// stopped doing, and is the half a stale test would miss.
	TestEqual(TEXT("Thorns of Glass reflects half the hit"),
			  UCataclysmEnemyModifiers::RetaliationPercent({Thorns}), 50.0f);
	TestEqual(TEXT("and does not touch the creature's health"),
			  UCataclysmEnemyModifiers::MaxHealthMultiplier({Thorns}), 1.0f);

	// A CREATURE CARRYING BOTH GETS BOTH, which is what it means for Thorns of
	// Glass to no longer decide the health. It was the one modifier that
	// overruled another and it does not any more.
	const TArray<FName> Both = { Thorns, Titanic };
	TestEqual(TEXT("Titanic Resolve still multiplies health beside Thorns of Glass"),
			  UCataclysmEnemyModifiers::MaxHealthMultiplier(Both), 1.5f);
	TestEqual(TEXT("and Thorns of Glass still reflects half"),
			  UCataclysmEnemyModifiers::RetaliationPercent(Both), 50.0f);

	// AN AURA MODIFIER CHANGES NO STAT. Hellfire Aura reaches out on the
	// per-character step instead, and a stat answer here would mean two things
	// were happening.
	TestEqual(TEXT("Hellfire Aura changes no stat"),
			  UCataclysmEnemyModifiers::MaxHealthMultiplier({Hellfire}), 1.0f);
	TestEqual(TEXT("and grants no leech"),
			  UCataclysmEnemyModifiers::LifeLeechPercent({Hellfire}), 0.0f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmModifierAuraPulseTest,
	"Cataclysm.EnemyModifiers.AnAuraPulsesOnceASecondNotOnEveryStep")
{
	// THE PER-CHARACTER STEP RUNS FOUR TIMES A SECOND, so an aura that fired on
	// every step would refresh a four second burn four times a second. The gate
	// is what makes the interval a decision rather than an accident of how often
	// the step happens.
	TestFalse(TEXT("nothing is due at the start"),
			  UCataclysmEnemyModifiers::AuraPulseIsDue(0.0f));
	TestFalse(TEXT("nor after one step"),
			  UCataclysmEnemyModifiers::AuraPulseIsDue(0.25f));
	TestFalse(TEXT("nor after three"),
			  UCataclysmEnemyModifiers::AuraPulseIsDue(0.75f));

	// AT THE INTERVAL, NOT PAST IT. The step is a fixed 0.25 seconds and the
	// interval is a whole second, so the accumulated figure lands exactly on
	// 1.0. A strict comparison would make every pulse wait an extra step, which
	// would turn a one second aura into a one and a quarter second one.
	TestTrue(TEXT("but it is due on the fourth step, exactly"),
			 UCataclysmEnemyModifiers::AuraPulseIsDue(1.0f));
	TestTrue(TEXT("and after it"),
			 UCataclysmEnemyModifiers::AuraPulseIsDue(2.5f));

	// SIX METRES, AS THE PROJECT OWNER DECIDED, and the same distance the
	// Masochist's own aura reaches. Stated in centimetres because everything in
	// Unreal is.
	TestEqual(TEXT("an aura reaches six metres"),
			  UCataclysmEnemyModifiers::AuraRadiusCm, 600.0f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmModifierSetterDoesNotDrawTest,
	"Cataclysm.EnemyModifiers.SettingARarityDoesNotDrawAnything")
{
	// **THIS PINS A BUG THAT SHIPPED.** The draw was inside `SetRarityStep`
	// until 2026-09-05, which was tidy and wrong: a draw is random, so any test
	// that spawned two creatures, set the same rung on both and compared them
	// could fail depending on what each drew.
	// `Cataclysm.Enemy.RarityScalesWhicheverOrderTheSpawnerSetsItIn` did exactly
	// that -- one creature drew Titanic Resolve and came out with half again the
	// health of the other -- and it failed only sometimes, which is worse than
	// failing always.
	//
	// SO SETTING A RUNG SETS A RUNG AND NOTHING ELSE, and a spawner asks for the
	// draw separately. That is the same split the rarity itself has:
	// `ACataclysmGameMode::RarityStepFor` rolls, `SetRarityStep` sets.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a creature in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature"), Enemy))
	{
		return false;
	}

	// A HERALD, which carries three. If the setter drew, this is where they
	// would arrive.
	Enemy->SetRarityStep(3);

	TestEqual(TEXT("setting a rung draws no modifiers"),
			  Enemy->ModifierRows.Num(), 0);

	// AND ASKING FOR THEM GIVES EXACTLY THE RUNG'S COUNT. Pinned so the draw is
	// not merely absent from the setter but present where it belongs.
	Enemy->SetModifierSeedForTests(20260905);
	Enemy->DrawModifiersForRarity();

	TestEqual(TEXT("and asking draws the three a Herald carries"),
			  Enemy->ModifierRows.Num(), 3);

	// ASKING TWICE ADDS NOTHING, because a spawner may set a rung more than
	// once and the draw only ever makes up the shortfall.
	Enemy->DrawModifiersForRarity();
	TestEqual(TEXT("and asking again adds none"),
			  Enemy->ModifierRows.Num(), 3);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmModifierReachesACreatureTest,
	"Cataclysm.EnemyModifiers.ACreatureCarryingOneActuallyGetsTheStat")
{
	// **THIS IS THE TEST THAT MATTERS AND THE OTHERS ARE NOT SUBSTITUTES.**
	// Everything above checks what a list of modifier names ANSWERS. None of it
	// says the game ever asks. That failure has happened on this project before:
	// every passive tree test called the refresh by hand, so 988 tests proved
	// the pipeline and none proved the game ran it.
	//
	// So this spawns a real creature, gives it a modifier the way a draw would,
	// and reads the attribute back off its ability system.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn a creature in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature"), Enemy))
	{
		return false;
	}

	Enemy->SetHealth(500.0f);

	const UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the creature has an ability system"), ASC))
	{
		return false;
	}

	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const FGameplayAttribute Crit =
		UCataclysmCombatAttributeSet::GetCritChanceAttribute();

	// WITHOUT A MODIFIER, THE ARCHETYPE'S OWN FIGURE STANDS. Read first so the
	// comparison below is against what this creature actually had rather than
	// against a number typed here.
	const float PlainHealth = ASC->GetNumericAttribute(MaxHealth);
	TestTrue(TEXT("a creature with no modifiers has its own health"),
			 PlainHealth > 0.0f);

	// TITANIC RESOLVE: "50% more health". Given the way a draw gives it, then
	// the stat block re-applied the way every spawner re-applies it.
	Enemy->ModifierRows.Add(FName(UCataclysmEnemyModifiers::TitanicResolveRow));
	Enemy->ApplyStartingAttributes();

	TestEqual(TEXT("Titanic Resolve reaches the creature's health"),
			  ASC->GetNumericAttribute(MaxHealth), PlainHealth * 1.5f, 0.01f);

	// AND IT DOES NOT COMPOUND. `ApplyStartingAttributes` runs again every time
	// a spawner sets anything, and a multiplier applied to the attribute rather
	// than to the freshly computed base would make the creature 2.25 times as
	// tough on the second call and 3.4 on the third.
	Enemy->ApplyStartingAttributes();
	TestEqual(TEXT("and applying the stat block again does not compound it"),
			  ASC->GetNumericAttribute(MaxHealth), PlainHealth * 1.5f, 0.01f);

	// OVERPOWERED: "Always crits".
	Enemy->ModifierRows.Add(FName(UCataclysmEnemyModifiers::OverpoweredRow));
	Enemy->ApplyStartingAttributes();

	TestEqual(TEXT("Overpowered reaches the creature's critical strike chance"),
			  ASC->GetNumericAttribute(Crit), 100.0f, 0.01f);

	// THORNS OF GLASS REFLECTS HALF AND LEAVES THE HEALTH ALONE. The creature is
	// still carrying Titanic Resolve, so the health check is what proves this
	// modifier no longer overrules another one -- which it did until the project
	// owner changed it on 2026-09-05.
	Enemy->ModifierRows.Add(FName(UCataclysmEnemyModifiers::ThornsOfGlassRow));
	Enemy->ApplyStartingAttributes();

	TestEqual(TEXT("Thorns of Glass reflects half the hit"),
			  ASC->GetNumericAttribute(
				  UCataclysmCombatAttributeSet::GetRetaliationAttribute()),
			  50.0f, 0.01f);
	TestEqual(TEXT("and leaves Titanic Resolve's health untouched"),
			  ASC->GetNumericAttribute(MaxHealth), PlainHealth * 1.5f, 0.01f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmModifierAuraStepIsSafeTest,
	"Cataclysm.EnemyModifiers.TheAuraStepRefusesAnythingThatIsNotACreature")
{
	// THE PLAYER GOES THROUGH THIS EVERY STEP, because it hangs off the step on
	// the shared character base. So does a null, which is what a test passes
	// and what a torn-down world can produce.
	TestEqual(TEXT("a null actor touches nobody"),
			  UCataclysmEnemyModifiers::AuraStep(nullptr, 0.25f), 0);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmAbyssalAuraTest,
	"Cataclysm.EnemyModifiers.AbyssalAuraCutsTheResistanceOfWhoeverStandsNear")
{
	using namespace CataclysmEnemyModifierTest;

	// **THE MODIFIER THREE CHANGES EXISTED TO UNBLOCK.** Its row reads "Players
	// within the aura's radius will have their Demonic resistance reduced by
	// 25%", and until 2026-09-05 none of what it needed existed: nothing
	// connected the number 25 to a resistance stat, its own data row had every
	// numeric cell empty, and no creature pulsed an aura at all.
	//
	// IT CUTS TWO RESISTANCES, NOT ONE. `Debuff_Abyssal_Aura` names
	// `resistance_demonic, resistance_war` in the MovesStat column added for
	// issue #1144. That is why the pairing had to become data: no rule written
	// in C++ for a single stat could have said it.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);

	// **A PLAYER AND NOT A CREATURE, AND THAT IS NOT A TEST CONVENIENCE.** A
	// creature carries the single All Resistance attribute and NOT the eight
	// typed slots. `ACataclysmEnemyCharacter::ApplyStartingAttributes` says why:
	// player damage carries no type, so a creature resists everything equally
	// and eight copies of one number would mean nothing. A creature therefore
	// has no Demonic resistance to lose, and writing to that attribute on one
	// raises an engine ensure. **This test found that by doing it.**
	//
	// IT IS ALSO THE REAL CASE, which the row states: "Players within the aura's
	// radius".
	ACataclysmPlayerCharacter* Victim = SpawnPlayerWithState(World);
	if (!TestNotNull(TEXT("a creature with the aura"), Enemy)
		|| !TestNotNull(TEXT("a player to stand in it"), Victim))
	{
		return false;
	}

	Victim->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));

	// THE TWO HAVE TO BE ON OPPOSING SIDES, because the aura reaches whatever
	// the creature counts as an enemy. Two actors on one team stand in each
	// other's auras and take nothing, which is correct and would make this test
	// pass for the wrong reason.
	Enemy->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Enemy->SetHealth(500.0f);

	UAbilitySystemComponent* Standing = Victim->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the player has an ability system"), Standing))
	{
		return false;
	}

	// SOMETHING TO TAKE. The reduction is clamped to what the target actually
	// has, so a target at zero resistance loses nothing and this would pass
	// against a rule that did nothing at all.
	const FGameplayAttribute Demonic =
		UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute();
	const FGameplayAttribute War =
		UCataclysmResistanceAttributeSet::GetWarResistanceAttribute();
	Standing->SetNumericAttributeBase(Demonic, 60.0f);
	Standing->SetNumericAttributeBase(War, 60.0f);

	Enemy->ModifierRows.Add(FName(UCataclysmEnemyModifiers::AbyssalAuraRow));

	// ONE FULL SECOND OF STEPS, because the aura pulses once a second and the
	// per-character step runs four times a second. Stepping once would prove
	// only that nothing happens between pulses.
	for (int32 Step = 0; Step < 4; ++Step)
	{
		UCataclysmEnemyModifiers::AuraStep(Enemy, 0.25f);
	}

	TestEqual(TEXT("standing in the aura costs 25 Demonic resistance"),
			  Standing->GetNumericAttribute(Demonic), 35.0f, 0.01f);

	// AND WAR RESISTANCE TOO, which is the half no single-stat rule could have
	// expressed. The row names both and the shared path reads the row.
	TestEqual(TEXT("and 25 War resistance, because the row names both"),
			  Standing->GetNumericAttribute(War), 35.0f, 0.01f);

	// A CREATURE WITHOUT THE MODIFIER TAKES NOTHING OFF ANYBODY, which is what
	// says the reduction above came from the modifier rather than from standing
	// near any creature at all.
	ACataclysmEnemyCharacter* Plain =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(800.0f, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	ACataclysmPlayerCharacter* Bystander = SpawnPlayerWithState(World);
	if (!TestNotNull(TEXT("a creature with no modifiers"), Plain)
		|| !TestNotNull(TEXT("a player beside it"), Bystander))
	{
		return false;
	}

	Bystander->SetActorLocation(FVector(900.0f, 0.0f, 0.0f));
	Plain->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Plain->SetHealth(500.0f);

	UAbilitySystemComponent* Untouched = Bystander->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the bystander has an ability system"), Untouched))
	{
		return false;
	}
	Untouched->SetNumericAttributeBase(Demonic, 60.0f);

	for (int32 Step = 0; Step < 4; ++Step)
	{
		UCataclysmEnemyModifiers::AuraStep(Plain, 0.25f);
	}

	TestEqual(TEXT("standing near a creature without the aura costs nothing"),
			  Untouched->GetNumericAttribute(Demonic), 60.0f, 0.01f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmGenericModifierStatsTest,
	"Cataclysm.EnemyModifiers.TheRemainingGenericModifiersAnswerTheirOwnNumbers")
{
	const FName Relentless(UCataclysmEnemyModifiers::RelentlessRow);
	const FName Shielder(UCataclysmEnemyModifiers::ShielderRow);
	const FName PerfectAim(UCataclysmEnemyModifiers::PerfectAimRow);

	// NOTHING CARRIED MEANS NOTHING GRANTED, which is 60% of what spawns.
	const TArray<FName> None;
	TestEqual(TEXT("no modifiers regenerate nothing"),
			  UCataclysmEnemyModifiers::HealthRegenShareOfMaximum(None), 0.0f);
	TestEqual(TEXT("no modifiers add no shield"),
			  UCataclysmEnemyModifiers::EnergyShieldShareOfHealth(None), 0.0f);
	TestFalse(TEXT("and ordinary attacks can be dodged"),
			  UCataclysmEnemyModifiers::AttacksCannotBeDodged(None));

	// RELENTLESS: a share of its own maximum health each second, not a flat
	// figure. A creature's health grows about twentyfold across the difficulty
	// tiers, so a flat number would be a real heal at tier 1 and nothing at 8.
	TestEqual(TEXT("Relentless regenerates two per cent a second"),
			  UCataclysmEnemyModifiers::HealthRegenShareOfMaximum({Relentless}),
			  0.02f);

	// SHIELDER: "an additional shield equal to 50% of maximum health".
	TestEqual(TEXT("Shielder adds half its health as shield"),
			  UCataclysmEnemyModifiers::EnergyShieldShareOfHealth({Shielder}),
			  0.5f);

	// PERFECT AIM: "Attacks cannot be dodged".
	TestTrue(TEXT("Perfect Aim refuses evasion"),
			 UCataclysmEnemyModifiers::AttacksCannotBeDodged({PerfectAim}));

	// AND ONE MODIFIER DOES NOT ANSWER FOR ANOTHER, which a shared lookup could
	// quietly do.
	TestFalse(TEXT("Relentless does not refuse evasion"),
			  UCataclysmEnemyModifiers::AttacksCannotBeDodged({Relentless}));
	TestEqual(TEXT("and Shielder regenerates nothing"),
			  UCataclysmEnemyModifiers::HealthRegenShareOfMaximum({Shielder}),
			  0.0f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmGenericModifiersReachACreatureTest,
	"Cataclysm.EnemyModifiers.RelentlessAndShielderReachTheCreaturesStats")
{
	using namespace CataclysmEnemyModifierTest;

	// THE ANSWERS ABOVE DO NOT SAY THE GAME EVER ASKS, which is the failure this
	// project has shipped before. This spawns a real creature and reads the
	// attributes back off it.
	//
	// IT CALLS `ApplyStartingAttributes` ITSELF, SO IT PROVES THE RULE AND NOT
	// THAT A SPAWNER REACHES IT. That call is the step every spawner skipped
	// until issue #1552. `AFloorCreatureThatDrawsShielderSpawnsWithItsShield`
	// spawns through one and calls nothing afterwards.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature"), Enemy))
	{
		return false;
	}

	Enemy->SetHealth(500.0f);

	const UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the creature has an ability system"), ASC))
	{
		return false;
	}

	const FGameplayAttribute Regen =
		UCataclysmVitalAttributeSet::GetHealthRegenAttribute();
	const FGameplayAttribute MaxShield =
		UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute();
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();

	// **A CREATURE REGENERATES NOTHING BY DEFAULT, AND THAT IS A STATED
	// POSITION.** `ApplyStartingAttributes` writes health regeneration to zero
	// for every creature, with a comment saying a creature that should
	// regenerate "would be expressed as an archetype property or a modifier".
	// Without this control the check below would pass against a rule that simply
	// left the attribute set's own placeholder of 1.0 alone.
	TestEqual(TEXT("a creature regenerates nothing by default"),
			  ASC->GetNumericAttribute(Regen), 0.0f, 0.01f);

	const float Health = ASC->GetNumericAttribute(MaxHealth);
	TestTrue(TEXT("and has health for the share to be a share of"), Health > 0.0f);

	Enemy->ModifierRows.Add(FName(UCataclysmEnemyModifiers::RelentlessRow));
	Enemy->ModifierRows.Add(FName(UCataclysmEnemyModifiers::ShielderRow));
	Enemy->ApplyStartingAttributes();

	TestEqual(TEXT("Relentless reaches the creature's health regeneration"),
			  ASC->GetNumericAttribute(Regen), Health * 0.02f, 0.01f);

	TestEqual(TEXT("and Shielder gives it half its health as shield"),
			  ASC->GetNumericAttribute(MaxShield), Health * 0.5f, 0.01f);

	// APPLYING THE STAT BLOCK AGAIN DOES NOT COMPOUND EITHER, which it would if
	// they were applied to the attribute rather than to the freshly computed
	// base. A spawner sets these on the lines after SpawnActor in whatever order
	// suits it, so this really does run more than once.
	Enemy->ApplyStartingAttributes();
	TestEqual(TEXT("and applying it again does not compound the regeneration"),
			  ASC->GetNumericAttribute(Regen), Health * 0.02f, 0.01f);
	TestEqual(TEXT("nor the shield"),
			  ASC->GetNumericAttribute(MaxShield), Health * 0.5f, 0.01f);

	return true;
}

namespace CataclysmEnemyModifierTest
{
	/**
	 * A seed whose one-modifier draw for this Cataclysm type is exactly
	 * `Wanted`, or 0 when none of the first 20,000 is.
	 *
	 * SEARCHED FOR RATHER THAN WRITTEN DOWN. A row added to the table moves what
	 * every seed draws, and a seed typed in here would then quietly hand the
	 * creature something else. The draw asked for is the one
	 * `ACataclysmEnemyCharacter::DrawModifiersForRarity` makes for an Elite that
	 * carries nothing yet: one modifier, from a stream seeded with exactly this
	 * number.
	 */
	int32 SeedThatDraws(FName CataclysmType, FName Wanted)
	{
		for (int32 Seed = 1; Seed <= 20000; ++Seed)
		{
			FRandomStream Stream(Seed);
			const TArray<FName> Drawn = UCataclysmEnemyModifiers::Draw(
				Table(), CataclysmType, 1, Stream, TArray<FName>());
			if (Drawn.Num() == 1 && Drawn[0] == Wanted)
			{
				return Seed;
			}
		}
		return 0;
	}

	/**
	 * An Imp given its stats the way a dungeon floor gives them, as an Elite
	 * whose one modifier is `Wanted`.
	 *
	 * THE FLOOR'S OWN TWO STEPS, IN ITS OWN ORDER.
	 * `ACataclysmDungeonGameMode::SpawnPlacedCreature` spawns the class for the
	 * creature's kind and then calls `ApplyDesignedStats`, which sets the
	 * creature's figures, then its rarity, then draws its modifiers. Only the
	 * seed is set in between, and a seed changes what is drawn, not when.
	 *
	 * Null, with the reason added to the test, when it cannot be arranged.
	 */
	ACataclysmEnemyCharacter* FloorImpThatDraws(FAutomationTestBase& Test,
		UWorld* World, ACataclysmDungeonGameMode* GameMode, FName Wanted,
		const FVector& Where)
	{
		ACataclysmEnemyCharacter* Imp = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmDungeonGameMode::ClassFor(ECataclysmDungeonCreature::Imp),
			Where, FRotator::ZeroRotator);
		if (!Imp)
		{
			Test.AddError(TEXT("Could not spawn an Imp."));
			return nullptr;
		}

		const int32 Seed = SeedThatDraws(Imp->DamageType, Wanted);
		if (Seed == 0)
		{
			Test.AddError(FString::Printf(
				TEXT("No seed up to 20,000 draws %s for an Imp."),
				*Wanted.ToString()));
			return nullptr;
		}
		Imp->SetModifierSeedForTests(Seed);

		// AN ELITE, which draws exactly one modifier, so the seed decides which.
		GameMode->ImpRarityStep = 1;
		GameMode->ApplyDesignedStats(Imp, ECataclysmDungeonCreature::Imp);
		return Imp;
	}

	/** Whether this creature carries exactly one modifier, and it is `Wanted`. */
	bool DrewOnly(const ACataclysmEnemyCharacter* Enemy, FName Wanted)
	{
		return Enemy && Enemy->ModifierRows.Num() == 1
			&& Enemy->ModifierRows[0] == Wanted;
	}
}

CATACLYSM_MODIFIER_TEST(FCataclysmFloorCreatureDrawsShielderTest,
	"Cataclysm.EnemyModifiers.AFloorCreatureThatDrawsShielderSpawnsWithItsShield")
{
	using namespace CataclysmEnemyModifierTest;

	// ISSUE #1552, AND THE PROOF IT ASKS FOR. A spawner gives a creature its
	// figures, then its rarity, then draws its modifiers, and the draw came after
	// the last call that turns modifiers into stats. So a creature that drew
	// Shielder spawned with no shield at all. The test above passed anyway,
	// because it calls `ApplyStartingAttributes` itself, which is the step the
	// spawners never took.
	//
	// **THIS TEST MUST NEVER CALL A SETTER, OR `ApplyStartingAttributes`, AFTER
	// `ApplyDesignedStats`.** Either would supply the missing step, and the test
	// would then pass whether the spawner takes it or not.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* GameMode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("a dungeon game mode"), GameMode))
	{
		return false;
	}

	const FName Shielder(UCataclysmEnemyModifiers::ShielderRow);
	ACataclysmEnemyCharacter* Imp =
		FloorImpThatDraws(*this, World, GameMode, Shielder, FVector::ZeroVector);
	if (!Imp)
	{
		return false;
	}

	// IT REALLY IS AN ELITE CARRYING SHIELDER AND NOTHING ELSE. Read back off
	// the creature rather than trusted, so a seed that stopped drawing Shielder
	// says so here instead of as a shield of the wrong size below.
	if (!TestEqual(TEXT("the Imp is an Elite"), Imp->RarityStep, 1)
		|| !TestTrue(TEXT("and drew Shielder and nothing else"),
					 DrewOnly(Imp, Shielder)))
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = Imp->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the Imp has an ability system"), ASC))
	{
		return false;
	}

	const float MaxHealth = ASC->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	TestTrue(TEXT("it has health for the shield to be a share of"),
			 MaxHealth > 0.0f);

	// HALF ITS MAXIMUM HEALTH, which is Shielder's row, on top of whatever
	// shield its kind carries of its own. The Imp's own share is zero.
	const float Expected = MaxHealth * (Imp->EnergyShieldFraction + 0.5f);
	TestEqual(TEXT("it spawned with Shielder's maximum shield"),
			  ASC->GetNumericAttribute(
				  UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()),
			  Expected, 0.01f);
	TestEqual(TEXT("and with that shield full"),
			  ASC->GetNumericAttribute(
				  UCataclysmVitalAttributeSet::GetEnergyShieldAttribute()),
			  Expected, 0.01f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmFloorCreatureDrawsTitanicResolveTest,
	"Cataclysm.EnemyModifiers.AFloorCreatureThatDrawsTitanicResolveSpawnsWithMoreHealth")
{
	using namespace CataclysmEnemyModifierTest;

	// THE SAME FAULT, FOR THE MODIFIER THAT RAISES MAXIMUM HEALTH. Issue #1552.
	// Titanic Resolve is half again the health, and a creature that drew it
	// spawned with its ordinary health. It is compared with an Imp spawned the
	// same way that drew Shielder, which changes no health, so the comparison
	// needs no figure the game mode keeps to itself.
	//
	// **THIS TEST MUST NEVER CALL A SETTER, OR `ApplyStartingAttributes`, AFTER
	// `ApplyDesignedStats`**, for the reason the test above gives.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* GameMode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("a dungeon game mode"), GameMode))
	{
		return false;
	}

	const FName Titanic(UCataclysmEnemyModifiers::TitanicResolveRow);
	const FName Shielder(UCataclysmEnemyModifiers::ShielderRow);

	// THE CONTROL IS ONE ONLY IF SHIELDER CHANGES NO HEALTH, which is asked of
	// the rule rather than assumed.
	TestEqual(TEXT("Shielder changes no health"),
			  UCataclysmEnemyModifiers::MaxHealthMultiplier({Shielder}), 1.0f,
			  0.001f);
	TestEqual(TEXT("and Titanic Resolve is half again"),
			  UCataclysmEnemyModifiers::MaxHealthMultiplier({Titanic}), 1.5f,
			  0.001f);

	ACataclysmEnemyCharacter* Titan = FloorImpThatDraws(
		*this, World, GameMode, Titanic, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Control = FloorImpThatDraws(
		*this, World, GameMode, Shielder, FVector(500.0f, 0.0f, 0.0f));
	if (!Titan || !Control
		|| !TestTrue(TEXT("one Imp drew Titanic Resolve and nothing else"),
					 DrewOnly(Titan, Titanic))
		|| !TestTrue(TEXT("and the other drew Shielder and nothing else"),
					 DrewOnly(Control, Shielder)))
	{
		return false;
	}

	const UAbilitySystemComponent* TitanASC = Titan->GetAbilitySystemComponent();
	const UAbilitySystemComponent* ControlASC =
		Control->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the first Imp has an ability system"), TitanASC)
		|| !TestNotNull(TEXT("and so does the second"), ControlASC))
	{
		return false;
	}

	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float ControlHealth = ControlASC->GetNumericAttribute(MaxHealth);
	TestTrue(TEXT("the control Imp has health"), ControlHealth > 0.0f);

	TestEqual(TEXT("the Imp that drew Titanic Resolve spawned with half again the health"),
			  TitanASC->GetNumericAttribute(MaxHealth), ControlHealth * 1.5f,
			  0.01f);
	TestEqual(TEXT("and at full health"),
			  TitanASC->GetNumericAttribute(
				  UCataclysmVitalAttributeSet::GetHealthAttribute()),
			  ControlHealth * 1.5f, 0.01f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmPhasewalkerTest,
	"Cataclysm.EnemyModifiers.PhasewalkerMovesTheCreatureEveryFewSeconds")
{
	using namespace CataclysmEnemyModifierTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature"), Enemy))
	{
		return false;
	}
	Enemy->SetHealth(500.0f);

	// A CREATURE WITHOUT THE MODIFIER NEVER PHASES, however long it is stepped.
	// Without this the check below would pass against a rule that teleported
	// every creature in the game.
	for (int32 Step = 0; Step < 40; ++Step)
	{
		TestFalse(TEXT("a creature without Phasewalker does not teleport"),
				  UCataclysmEnemyModifiers::PhaseStep(Enemy, 0.25f));
	}

	Enemy->ModifierRows.Add(FName(UCataclysmEnemyModifiers::PhasewalkerRow));
	Enemy->SecondsSincePhase = 0.0f;

	// NOTHING BEFORE THE INTERVAL. Four seconds at a quarter second a step is
	// sixteen steps, and the first fifteen must do nothing, or the modifier is a
	// creature that never stands still.
	for (int32 Step = 0; Step < 15; ++Step)
	{
		TestFalse(TEXT("nothing happens before the interval"),
				  UCataclysmEnemyModifiers::PhaseStep(Enemy, 0.25f));
	}

	const FVector Before = Enemy->GetActorLocation();
	TestTrue(TEXT("and on the sixteenth step it phases"),
			 UCataclysmEnemyModifiers::PhaseStep(Enemy, 0.25f));

	// MEASURED AS A DISTANCE MOVED rather than as the return value, because the
	// return value would be true for a move of no length at all.
	TestTrue(TEXT("and it actually moved"),
			 (Enemy->GetActorLocation() - Before).Size() > 1.0f);

	// AND IT DOES NOT PHASE AGAIN ON THE NEXT STEP, which is what the interval
	// is for.
	TestFalse(TEXT("and does not phase again immediately"),
			  UCataclysmEnemyModifiers::PhaseStep(Enemy, 0.25f));

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmHordeLeaderTest,
	"Cataclysm.EnemyModifiers.HordeLeaderRalliesItsAlliesWhenItDies")
{
	using namespace CataclysmEnemyModifierTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Leader =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Ally =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(200.0f, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a leader"), Leader)
		|| !TestNotNull(TEXT("an ally"), Ally))
	{
		return false;
	}

	// THE TWO HAVE TO BE ON THE SAME SIDE, because the modifier buffs allies.
	Leader->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Ally->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Leader->SetHealth(500.0f);
	Ally->SetHealth(500.0f);

	// A CREATURE WITHOUT THE MODIFIER RALLIES NOBODY, which is what says the
	// result below came from the modifier rather than from any creature dying.
	TestEqual(TEXT("a creature without Horde Leader rallies nobody"),
			  UCataclysmEnemyModifiers::RallyAlliesOnDeath(Leader), 0);

	Leader->ModifierRows.Add(FName(UCataclysmEnemyModifiers::HordeLeaderRow));

	TestEqual(TEXT("and one carrying it rallies the ally beside it"),
			  UCataclysmEnemyModifiers::RallyAlliesOnDeath(Leader), 1);

	// AND THE ALLY REALLY CARRIES THE BUFF, not merely a count that says so.
	const FGameplayTag Commander =
		UCataclysmSkillShapes::StatusTagFor(TEXT("Commander"));
	TestTrue(TEXT("the Commander tag is a real tag"), Commander.IsValid());

	const UAbilitySystemComponent* Buffed = Ally->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the ally has an ability system"), Buffed))
	{
		return false;
	}
	TestTrue(TEXT("and the ally is carrying it"),
			 Buffed->HasMatchingGameplayTag(Commander));

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmDemonicModifierRowsTest,
	"Cataclysm.EnemyModifiers.EveryDemonicModifierNowHasItsEffectBuilt")
{
	using namespace CataclysmEnemyModifierTest;

	const UDataTable* ModifierTable = Table();
	if (!TestNotNull(TEXT("the enemy modifier table loads"), ModifierTable))
	{
		return false;
	}

	// ALL EIGHT DEMONIC MODIFIERS NOW HAVE AN EFFECT, and these are the row keys
	// the effects are named against. A row renamed in the design workbook breaks
	// the effect that reads it, and this is what says so before a play test does.
	const TCHAR* Built[] = {
		UCataclysmEnemyModifiers::HellfireAuraRow,
		UCataclysmEnemyModifiers::AbyssalAuraRow,
		UCataclysmEnemyModifiers::InfernalBrandRow,
		UCataclysmEnemyModifiers::BeguilingRow,
		UCataclysmEnemyModifiers::InfernalSacrificeRow,
		UCataclysmEnemyModifiers::UnholySigilsRow,
		UCataclysmEnemyModifiers::SacrificialBondRow,
		UCataclysmEnemyModifiers::InfernoChargeRow,
	};

	const TArray<FName> Pool = UCataclysmEnemyModifiers::PoolFor(
		ModifierTable, FName(TEXT("Demonic")));

	for (const TCHAR* Key : Built)
	{
		const FName RowKey(Key);
		TestNotNull(*FString::Printf(TEXT("%s is a row in the table"), Key),
					UCataclysmEnemyModifiers::FindRow(ModifierTable, RowKey));
		TestTrue(*FString::Printf(TEXT("%s is drawable by a Demonic creature"),
								  Key), Pool.Contains(RowKey));
	}

	// EIGHT, WHICH IS EVERY DEMONIC MODIFIER THERE IS. If the workbook gains a
	// ninth, this fails and whoever added it is told to build its effect or say
	// why not.
	int32 DemonicRows = 0;
	for (const FName& Key : Pool)
	{
		const FCataclysmEnemyModifierRow* Row =
			UCataclysmEnemyModifiers::FindRow(ModifierTable, Key);
		if (Row && Row->CataclysmType.Equals(TEXT("Demonic"),
											 ESearchCase::IgnoreCase))
		{
			++DemonicRows;
		}
	}

	TestEqual(TEXT("every Demonic modifier has its effect built"), DemonicRows,
			  static_cast<int32>(UE_ARRAY_COUNT(Built)));

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmSacrificialBondTest,
	"Cataclysm.EnemyModifiers.SacrificialBondDividesAHitAmongTheAlliesPresent")
{
	using namespace CataclysmEnemyModifierTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Bonded =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature"), Bonded))
	{
		return false;
	}
	Bonded->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Bonded->SetHealth(500.0f);

	// A CREATURE WITHOUT THE MODIFIER KEEPS ALL OF IT, whoever is standing by.
	// Without this the checks below would pass against a rule that halved every
	// hit in the game.
	TestEqual(TEXT("a creature without the modifier keeps the whole hit"),
			  UCataclysmEnemyModifiers::ShareOfDamageKept(Bonded), 1.0f, 0.001f);

	Bonded->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));

	// AND WITH NOBODY TO SHARE WITH IT STILL KEEPS ALL OF IT, which is what
	// makes the modifier answerable: pull it away from its pack.
	TestEqual(TEXT("with nobody nearby it still keeps the whole hit"),
			  UCataclysmEnemyModifiers::ShareOfDamageKept(Bonded), 1.0f, 0.001f);

	ACataclysmEnemyCharacter* First =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(200.0f, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an ally"), First))
	{
		return false;
	}
	First->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	First->SetHealth(500.0f);

	TestEqual(TEXT("one ally halves what it takes"),
			  UCataclysmEnemyModifiers::ShareOfDamageKept(Bonded), 0.5f, 0.001f);

	ACataclysmEnemyCharacter* Second =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(300.0f, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Third =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(400.0f, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a second ally"), Second)
		|| !TestNotNull(TEXT("a third ally"), Third))
	{
		return false;
	}
	Second->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Third->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Second->SetHealth(500.0f);
	Third->SetHealth(500.0f);

	TestEqual(TEXT("three allies leave it a quarter"),
			  UCataclysmEnemyModifiers::ShareOfDamageKept(Bonded), 0.25f, 0.001f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmUnholySigilTest,
	"Cataclysm.EnemyModifiers.AnUnholySigilProtectsAlliesStandingInIt")
{
	using namespace CataclysmEnemyModifierTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Caster =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Ally =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(200.0f, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a caster"), Caster)
		|| !TestNotNull(TEXT("an ally"), Ally))
	{
		return false;
	}

	Caster->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Ally->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Caster->SetHealth(500.0f);
	Ally->SetHealth(500.0f);

	// NOBODY IS PROTECTED BEFORE A SIGIL EXISTS, which is what says the check
	// below is about the sigil rather than about standing near anybody.
	TestFalse(TEXT("nobody is protected before a sigil is laid"),
			  UCataclysmEnemyModifiers::IsProtectedBySigil(Ally));

	Caster->ModifierRows.Add(FName(UCataclysmEnemyModifiers::UnholySigilsRow));

	// TWENTY SECONDS AT A QUARTER OF A SECOND A STEP IS EIGHTY STEPS. Stepping
	// fewer would prove only that nothing happens before the interval.
	for (int32 Step = 0; Step < 80; ++Step)
	{
		UCataclysmEnemyModifiers::TimedStep(Caster, 0.25f);
	}

	TestTrue(TEXT("an ally standing in the sigil is protected"),
			 UCataclysmEnemyModifiers::IsProtectedBySigil(Ally));

	// AND WALKING OUT OF IT ENDS THE PROTECTION IMMEDIATELY, with nothing to
	// clear and nothing to expire. That is why the question is asked of whoever
	// is about to die rather than kept as a flag on them.
	Ally->SetActorLocation(FVector(5000.0f, 0.0f, 0.0f));
	TestFalse(TEXT("and stepping out of it ends the protection at once"),
			  UCataclysmEnemyModifiers::IsProtectedBySigil(Ally));

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmInfernalSacrificeTest,
	"Cataclysm.EnemyModifiers.InfernalSacrificeEatsOneAllyAndHeals")
{
	using namespace CataclysmEnemyModifierTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Eater =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* First =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(200.0f, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Second =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(300.0f, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature"), Eater)
		|| !TestNotNull(TEXT("an ally"), First)
		|| !TestNotNull(TEXT("a second ally"), Second))
	{
		return false;
	}

	for (ACataclysmEnemyCharacter* Each : {Eater, First, Second})
	{
		Each->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Each->SetHealth(500.0f);
	}

	// SIX SECONDS AT A QUARTER OF A SECOND A STEP IS TWENTY-FOUR STEPS.
	for (int32 Step = 0; Step < 24; ++Step)
	{
		UCataclysmEnemyModifiers::TimedStep(Eater, 0.25f);
	}

	// WITHOUT THE MODIFIER NOBODY IS EATEN, however long it is stepped. This is
	// the control: the check below would otherwise pass against a rule that
	// killed an ally of every creature in the game.
	TestFalse(TEXT("a creature without the modifier eats nobody"),
			  UCataclysmSkillEffects::IsDead(First));
	TestFalse(TEXT("nor the second ally"),
			  UCataclysmSkillEffects::IsDead(Second));

	Eater->ModifierRows.Add(
		FName(UCataclysmEnemyModifiers::InfernalSacrificeRow));
	Eater->SecondsSinceSacrifice = 0.0f;

	for (int32 Step = 0; Step < 24; ++Step)
	{
		UCataclysmEnemyModifiers::TimedStep(Eater, 0.25f);
	}

	// ONE AT A TIME, NOT THE WHOLE PACK. A creature that ate everything at once
	// would replace the fight rather than change it.
	const int32 Eaten =
		(UCataclysmSkillEffects::IsDead(First) ? 1 : 0)
		+ (UCataclysmSkillEffects::IsDead(Second) ? 1 : 0);

	TestEqual(TEXT("exactly one ally is sacrificed in one interval"), Eaten, 1);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmInfernoChargeTest,
	"Cataclysm.EnemyModifiers.InfernoChargeSetsOffAtSomethingItCanReach")
{
	using namespace CataclysmEnemyModifierTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Charger =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature"), Charger))
	{
		return false;
	}
	Charger->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Charger->SetHealth(500.0f);

	ACataclysmPlayerCharacter* Quarry = SpawnPlayerWithState(World);
	if (!TestNotNull(TEXT("somebody to charge"), Quarry))
	{
		return false;
	}
	Quarry->SetActorLocation(FVector(900.0f, 0.0f, 0.0f));

	// TWELVE SECONDS AT A QUARTER OF A SECOND A STEP IS FORTY-EIGHT STEPS.
	// Without the modifier the creature never charges, which is the control.
	for (int32 Step = 0; Step < 48; ++Step)
	{
		UCataclysmEnemyModifiers::TimedStep(Charger, 0.25f);
	}

	TestFalse(TEXT("a creature without the modifier does not charge"),
			  Charger->IsCharging());

	Charger->ModifierRows.Add(FName(UCataclysmEnemyModifiers::InfernoChargeRow));
	Charger->SecondsSinceInfernoCharge = 0.0f;

	for (int32 Step = 0; Step < 48; ++Step)
	{
		UCataclysmEnemyModifiers::TimedStep(Charger, 0.25f);
	}

	TestTrue(TEXT("and one carrying it charges"), Charger->IsCharging());

	return true;
}

// ---------------------------------------------------------------------------
// Infernal Brand, through the hits that build it
// ---------------------------------------------------------------------------

namespace CataclysmEnemyModifierTest
{
	/**
	 * Something to be hit that none of its own defences protects: an ability
	 * system on a plain actor, with no armour, evasion, block or reduction, and
	 * no energy shield to take a blow before health does.
	 *
	 * A PLAIN ACTOR RATHER THAN A PLAYER, ON PURPOSE. The Infernal Brand test
	 * below was written to fail against the code of issue #1534, and against
	 * that code its target dies. A player's death writes a save file and stands
	 * the character back up on a timer; a plain actor's does nothing, because
	 * `UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero` acts only on a
	 * character. The brand reads nothing that differs between the two: it asks
	 * for an ability system and a blow that took health.
	 */
	struct FBareTarget
	{
		FBareTarget(UWorld* World, float Health)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			NewCombat->SetArmor(0.0f);
			NewCombat->SetEvasion(0.0f);
			NewCombat->SetBlockChance(0.0f);
			NewCombat->SetDamageReduction(0.0f);
			NewVitals->SetMaxEnergyShield(0.0f);
			NewVitals->SetEnergyShield(0.0f);
			NewVitals->SetMaxHealth(Health);
			NewVitals->SetHealth(Health);

			Vitals = NewVitals;
		}

		~FBareTarget()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
		UCataclysmVitalAttributeSet* Vitals = nullptr;
	};
}

CATACLYSM_MODIFIER_TEST(FCataclysmInfernalBrandThroughHitsTest,
	"Cataclysm.EnemyModifiers.InfernalBrandExplodesOnceForEveryFiveHitsThatLand")
{
	using namespace CataclysmEnemyModifierTest;

	// ISSUE #1534, THE WAY THE PROJECT OWNER MET IT: a Horde arena on
	// 2026-09-10 in which their character died ten times in 57 seconds. Their
	// log shows each death followed, within four milliseconds, by a burst of 4
	// to 28 explosions all naming ONE creature.
	//
	// ONE HIT CAN SET OFF A WHOLE BURST. The explosion is dealt as an ordinary
	// blow from the creature, so it reaches
	// `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` like any other
	// blow, and that calls `BrandOnHit` again. With the count stuck at five the
	// second call explodes too, and the third, until the target has no health
	// left for a blow to take. `BrandOnHit` writes each explosion's log line
	// after its damage returns, which is why the death is printed first.
	//
	// SO THE HITS HERE GO THROUGH THE DAMAGE PIPELINE, NOT STRAIGHT INTO
	// `BrandOnHit`. A loop calling the rule would count its own calls and never
	// see the explosions nested inside them.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Brander =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature to do the branding"), Brander))
	{
		return false;
	}

	UAbilitySystemComponent* Own = Brander->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the creature has an ability system"), Own))
	{
		return false;
	}

	Brander->ModifierRows.Add(FName(UCataclysmEnemyModifiers::InfernalBrandRow));

	// THE EXPLOSION IS FIVE OF THE CREATURE'S OWN HITS, so a hundred attack
	// damage makes an explosion of five hundred.
	constexpr float AttackDamage = 100.0f;
	Own->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), AttackDamage);
	const float Explosion =
		AttackDamage * UCataclysmEnemyModifiers::InfernalBrandExplosionHits;

	// TWO THOUSAND HEALTH, AND BOTH ENDS OF THAT NUMBER MATTER. Ten hits and the
	// two explosions they should cause take 1,010 of it, so the target lives
	// through a correct run. And a chain of explosions ends when there is no
	// health left for a blow to take, so against the broken code it is at most
	// five deep here -- where a target with a million health would make it two
	// thousand deep, and a run that crashes measures nothing.
	FBareTarget Target(World, 2'000.0f);

	TArray<FString> ExplosionsPerHit;
	TArray<FString> HeldAfter;
	float HealthLostToTheFifth = 0.0f;

	for (int32 Hit = 1; Hit <= 10; ++Hit)
	{
		const uint32 BlowsBefore = Target.AbilitySystem->GetResolvedHitStamp();
		const float HealthBefore = Target.Vitals->GetHealth();

		// ONE POINT OF DAMAGE, SO THE HITS THEMSELVES ARE NEGLIGIBLE and a large
		// loss of health can only be an explosion.
		UCataclysmSkillEffects::ApplyDirectDamage(Brander, Target.Actor,
												  /*Damage=*/1.0f,
												  FCataclysmHitDelivery());

		// EVERY BLOW THAT REACHES THE TARGET MOVES THIS STAMP BY ONE -- this
		// hit, and each explosion it set off inside itself -- so what is left
		// after taking away the hit is the number of explosions.
		const int32 Blows = static_cast<int32>(
			Target.AbilitySystem->GetResolvedHitStamp() - BlowsBefore);
		ExplosionsPerHit.Add(FString::FromInt(Blows - 1));
		HeldAfter.Add(FString::FromInt(UCataclysmStacks::Held(
			Target.AbilitySystem, ECataclysmStackKind::InfernalBrand)));

		if (Hit == 5)
		{
			HealthLostToTheFifth = HealthBefore - Target.Vitals->GetHealth();
		}
	}

	// ONE EXPLOSION FOR EVERY FIVE HITS, ON THE FIFTH AND THE TENTH.
	TestEqual(TEXT("explosions set off by each of ten hits"),
			  FString::Join(ExplosionsPerHit, TEXT(" ")),
			  FString(TEXT("0 0 0 0 1 0 0 0 0 1")));

	// AND AN EXPLOSION LEAVES NO BRAND BEHIND IT. The row says the explosion
	// consumes all stacks. A blow that brands as it explodes would leave one,
	// and the second explosion would then come on the ninth hit, not the tenth.
	TestEqual(TEXT("brands held after each of ten hits"),
			  FString::Join(HeldAfter, TEXT(" ")),
			  FString(TEXT("1 2 3 4 0 1 2 3 4 0")));

	// AND THE EXPLOSION IS REAL DAMAGE, AT ITS DESIGNED SIZE. Without this the
	// counts above would pass for an explosion that dealt nothing.
	TestEqual(TEXT("the fifth hit cost its own point and one explosion"),
			  HealthLostToTheFifth, 1.0f + Explosion, 0.01f);
	TestEqual(TEXT("and ten hits cost ten points and two explosions"),
			  Target.Vitals->GetHealth(),
			  2'000.0f - 10.0f - 2.0f * Explosion, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
