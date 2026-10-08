// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
// For the bleed whose tick a bonded creature shares. Issue #2289.
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
// For the overkill explosion row a test wears. Issue #2289.
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Character/CataclysmSpireCharacter.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
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

namespace CataclysmBondAndSigilTest
{
	/**
	 * A Monsters-team creature at a place, with this much health, that cannot
	 * evade or block, so every blow here lands whole and the same way twice.
	 */
	ACataclysmEnemyCharacter* Creature(UWorld* World, const FVector& Where, float Health)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (!Made)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetHealth(Health);
		if (UAbilitySystemComponent* Own = Made->GetAbilitySystemComponent())
		{
			Own->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Own->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		}
		return Made;
	}

	/** A player well away from every creature, whose blows never land critically. */
	ACataclysmPlayerCharacter* Striker(UWorld* World)
	{
		ACataclysmPlayerCharacter* Player =
			CataclysmEnemyModifierTest::SpawnPlayerWithState(World);
		if (Player)
		{
			Player->SetActorLocation(FVector(0.0f, 5000.0f, 0.0f));
			if (UAbilitySystemComponent* Own = Player->GetAbilitySystemComponent())
			{
				Own->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 0.0f);
			}
		}
		return Player;
	}

	float HealthOf(const ACataclysmEnemyCharacter* Creature)
	{
		const UAbilitySystemComponent* Own =
			Creature ? Creature->GetAbilitySystemComponent() : nullptr;
		return Own ? Own->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	float BaseHealthOf(const ACataclysmEnemyCharacter* Creature)
	{
		const UAbilitySystemComponent* Own =
			Creature ? Creature->GetAbilitySystemComponent() : nullptr;
		return Own ? Own->GetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	/** Lay the caster's sigil: twenty seconds of quarter-second steps. */
	void LaySigil(ACataclysmEnemyCharacter* Caster)
	{
		Caster->ModifierRows.Add(FName(UCataclysmEnemyModifiers::UnholySigilsRow));
		for (int32 Step = 0; Step < 80; ++Step)
		{
			UCataclysmEnemyModifiers::TimedStep(Caster, 0.25f);
		}
	}
}

CATACLYSM_MODIFIER_TEST(FCataclysmBondSharesABlowTest,
	"Cataclysm.EnemyModifiers.ABondedCreatureSharesALandedBlowEvenlyWithItsAlly")
{
	// ISSUE #1559. Until it, `ShareOfDamageKept` answered and nothing asked, so
	// the test above passed while a bonded creature took every hit in full. So
	// this one lands a real blow through the damage pipeline.
	using namespace CataclysmBondAndSigilTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = Striker(World);
	ACataclysmEnemyCharacter* Bonded = Creature(World, FVector::ZeroVector, 100000.0f);
	ACataclysmEnemyCharacter* Ally = Creature(World, FVector(200.0f, 0.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* Control = Creature(World, FVector(-3000.0f, 0.0f, 0.0f), 100000.0f);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a bonded creature"), Bonded)
		|| !TestNotNull(TEXT("an ally"), Ally) || !TestNotNull(TEXT("a control"), Control))
	{
		return false;
	}
	Bonded->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));

	// THE WHOLE BLOW, MEASURED ON A CREATURE WITH NO BOND, so the shares below are
	// compared with what the same blow does rather than with a number typed here.
	UCataclysmSkillEffects::ApplyDirectDamage(Player, Control, 1000.0f, FCataclysmHitDelivery());
	const float Whole = 100000.0f - HealthOf(Control);
	if (!TestTrue(TEXT("the blow takes health from a creature with no bond"), Whole > 0.0f))
	{
		return false;
	}

	UCataclysmSkillEffects::ApplyDirectDamage(Player, Bonded, 1000.0f, FCataclysmHitDelivery());
	TestEqual(TEXT("the bonded creature loses half the blow"),
			  100000.0f - HealthOf(Bonded), Whole * 0.5f, 0.01f);
	TestEqual(TEXT("and its ally loses the other half"),
			  100000.0f - HealthOf(Ally), Whole * 0.5f, 0.01f);

	// AND AN ALLY THE SHARE KILLS IS THE PLAYER'S KILL. Ruled 2026-09-30: a death
	// notice names its killer from the victim's last blow, so on-kill effects
	// such as Wrung Out fire for a death the player's blow caused.
	Ally->GetAbilitySystemComponent()->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestNotNull(TEXT("the world's combat announcer"), Events))
	{
		return false;
	}
	int32 AllyDeaths = 0;
	bool bNamedThePlayer = false;
	const FDelegateHandle Heard = Events->OnDeath.AddLambda(
		[&AllyDeaths, &bNamedThePlayer, Ally, Player](const FCataclysmDeathNotice& Notice)
		{
			if (Notice.Victim == Ally)
			{
				++AllyDeaths;
				bNamedThePlayer |= Notice.Killer == Player;
			}
		});
	UCataclysmSkillEffects::ApplyDirectDamage(Player, Bonded, 1000.0f, FCataclysmHitDelivery());
	Events->OnDeath.Remove(Heard);

	TestEqual(TEXT("the ally's share killed it, once"), AllyDeaths, 1);
	TestTrue(TEXT("and its death names the player as the killer"), bNamedThePlayer);
	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmBondKeepsAllWithNoAllyTest,
	"Cataclysm.EnemyModifiers.ABondedCreatureWithNoCreatureToShareWithKeepsTheWholeBlow")
{
	// NOBODY IN REACH, AND NOTHING THAT IS NOT AN ALLY IN THE ROW'S SENSE. Issue
	// #1559, ruled 2026-09-30: a floor-rule object, a creature that cannot be
	// hurt and a shrouded creature are all beside it, and it still keeps the
	// whole blow.
	using namespace CataclysmBondAndSigilTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = Striker(World);
	ACataclysmEnemyCharacter* Bonded = Creature(World, FVector::ZeroVector, 100000.0f);
	ACataclysmEnemyCharacter* Control = Creature(World, FVector(-3000.0f, 0.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* Unhurt = Creature(World, FVector(200.0f, 0.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* Shrouded = Creature(World, FVector(0.0f, 200.0f, 0.0f), 100000.0f);
	ACataclysmSpireCharacter* Spire = World->SpawnActor<ACataclysmSpireCharacter>(
		FVector(-200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a bonded creature"), Bonded)
		|| !TestNotNull(TEXT("a control"), Control) || !TestNotNull(TEXT("an unhurt creature"), Unhurt)
		|| !TestNotNull(TEXT("a shrouded creature"), Shrouded) || !TestNotNull(TEXT("a spire"), Spire))
	{
		return false;
	}
	Bonded->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));
	Unhurt->bCannotBeHurt = true;
	Shrouded->bShrouded = true;
	Spire->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Spire->SetHealth(100000.0f);

	// THE THREE ARE FOUND BY THE ALLY SEARCH, which is what makes the rest of
	// this test about the bond refusing them rather than about the search
	// missing them.
	const TArray<AActor*> Found = UCataclysmTargeting::FindAlliesInSphere(
		World, Bonded, Bonded->GetActorLocation(), 600.0f);
	TestTrue(TEXT("the ally search finds the spire"), Found.Contains(Spire));
	TestTrue(TEXT("and the creature that cannot be hurt"), Found.Contains(Unhurt));
	TestTrue(TEXT("and the shrouded creature"), Found.Contains(Shrouded));

	UCataclysmSkillEffects::ApplyDirectDamage(Player, Control, 1000.0f, FCataclysmHitDelivery());
	const float Whole = 100000.0f - HealthOf(Control);
	if (!TestTrue(TEXT("the blow takes health from a creature with no bond"), Whole > 0.0f))
	{
		return false;
	}

	UCataclysmSkillEffects::ApplyDirectDamage(Player, Bonded, 1000.0f, FCataclysmHitDelivery());
	TestEqual(TEXT("the bonded creature keeps the whole blow"),
			  100000.0f - HealthOf(Bonded), Whole, 0.01f);
	TestEqual(TEXT("and the spire loses nothing"), HealthOf(Spire), 100000.0f, 0.01f);
	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmSigilHoldsALethalBlowTest,
	"Cataclysm.EnemyModifiers.ALethalBlowLeavesACreatureInAnUnholySigilAtOneHealth")
{
	// "ALLIES IN THIS SIGIL CANNOT BE KILLED." Issue #1559. Until it, a creature
	// in a sigil died like any other while the log said a sigil was down.
	using namespace CataclysmBondAndSigilTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = Striker(World);
	ACataclysmEnemyCharacter* Caster = Creature(World, FVector::ZeroVector, 100000.0f);
	ACataclysmEnemyCharacter* Inside = Creature(World, FVector(200.0f, 0.0f, 0.0f), 100.0f);
	ACataclysmEnemyCharacter* Outside = Creature(World, FVector(3000.0f, 0.0f, 0.0f), 100.0f);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a caster"), Caster)
		|| !TestNotNull(TEXT("a creature inside"), Inside)
		|| !TestNotNull(TEXT("a creature outside"), Outside))
	{
		return false;
	}
	LaySigil(Caster);

	UCataclysmSkillEffects::ApplyDirectDamage(Player, Inside, 100000.0f, FCataclysmHitDelivery());
	TestFalse(TEXT("a lethal blow does not kill a creature inside the sigil"),
			  UCataclysmSkillEffects::IsDead(Inside));
	TestEqual(TEXT("it is left at one health"), HealthOf(Inside), 1.0f, 0.001f);
	TestEqual(TEXT("and its stored base agrees, so a later gain starts from one"),
			  BaseHealthOf(Inside), 1.0f, 0.001f);

	// THE CONTROL: THE SAME BLOW OUTSIDE THE SIGIL KILLS.
	UCataclysmSkillEffects::ApplyDirectDamage(Player, Outside, 100000.0f, FCataclysmHitDelivery());
	TestTrue(TEXT("the same blow outside the sigil kills"),
			 !IsValid(Outside) || UCataclysmSkillEffects::IsDead(Outside));
	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmSigilHoldsABondShareTest,
	"Cataclysm.EnemyModifiers.ABondsShareDoesNotKillAnAllyInAnUnholySigil")
{
	// THE TWO MODIFIERS TOGETHER. Issue #1559, ruled 2026-09-30: the bond's
	// shares arrive as direct reductions, and the sigil holds every way health is
	// lowered, so an ally in a sigil survives a share that would kill it.
	using namespace CataclysmBondAndSigilTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE CASTER STANDS OUT OF THE BOND'S SIX METRES, SO THE SHARE IS A HALF. The
	// sigil reaches twelve metres from the caster, so the ally ten metres from the
	// caster is inside it.
	ACataclysmPlayerCharacter* Player = Striker(World);
	ACataclysmEnemyCharacter* Caster = Creature(World, FVector(1200.0f, 0.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* Bonded = Creature(World, FVector::ZeroVector, 100000.0f);
	ACataclysmEnemyCharacter* Ally = Creature(World, FVector(200.0f, 0.0f, 0.0f), 100.0f);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a caster"), Caster)
		|| !TestNotNull(TEXT("a bonded creature"), Bonded) || !TestNotNull(TEXT("an ally"), Ally))
	{
		return false;
	}
	LaySigil(Caster);
	Bonded->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));
	if (!TestTrue(TEXT("the ally stands in the sigil"),
				  UCataclysmEnemyModifiers::IsProtectedBySigil(Ally))
		|| !TestEqual(TEXT("and the bond shares with the ally alone"),
					  UCataclysmEnemyModifiers::ShareOfDamageKept(Bonded), 0.5f, 0.001f))
	{
		return false;
	}

	UCataclysmSkillEffects::ApplyDirectDamage(Player, Bonded, 100000.0f, FCataclysmHitDelivery());
	TestTrue(TEXT("the bonded creature took its half"), HealthOf(Bonded) < 100000.0f);
	TestFalse(TEXT("a share that would kill the ally in the sigil does not"),
			  UCataclysmSkillEffects::IsDead(Ally));
	TestEqual(TEXT("it is left at one health"), HealthOf(Ally), 1.0f, 0.001f);
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

CATACLYSM_MODIFIER_TEST(FCataclysmInfernoChargeChannelTest,
	"Cataclysm.EnemyModifiers.InfernoChargeChannelsTwoSecondsThenLeavesItsPathBurning")
{
	using namespace CataclysmEnemyModifierTest;
	using Modifiers_t = UCataclysmEnemyModifiers;

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

	// PRICED OFF THE CREATURE'S OWN ATTACK DAMAGE WHEN THE PATH IS LAID, so
	// it has some, set here because the figure is asserted below.
	constexpr float AttackDamage = 100.0f;
	Charger->SetAttackDamage(AttackDamage);
	Charger->ModifierRows.Add(FName(Modifiers_t::InfernoChargeRow));
	Charger->SecondsSinceInfernoCharge = 0.0f;

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Charger->GetController());
	if (!TestNotNull(TEXT("the creature's brain"), Brain))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Quarry = SpawnPlayerWithState(World);
	if (!TestNotNull(TEXT("somebody to charge"), Quarry))
	{
		return false;
	}
	Quarry->SetActorLocation(FVector(900.0f, 0.0f, 0.0f));
	const FVector StoodAt = Charger->GetActorLocation();

	// THE CHANNEL IS THE LAST TWO SECONDS OF THE TWELVE: thirty-nine quarter
	// second steps leave it doing nothing, and the fortieth, at ten seconds,
	// begins the channel. Issue #1560.
	for (int32 Step = 0; Step < 39; ++Step)
	{
		Modifiers_t::TimedStep(Charger, 0.25f);
	}
	TestFalse(TEXT("nothing has begun at 9.75 seconds"),
			  Charger->IsChannellingInfernoCharge() || Charger->IsCharging());

	Modifiers_t::TimedStep(Charger, 0.25f);
	if (!TestTrue(TEXT("the channel begins at ten seconds"),
				  Charger->IsChannellingInfernoCharge()))
	{
		return false;
	}
	TestFalse(TEXT("and it has not charged yet"), Charger->IsCharging());

	// THE LANE IS DRAWN, AND IT IS THE CHARGE'S OWN WIDTH. A lane narrower
	// than a metre draws nothing, which is why the width was raised to one.
	const ACataclysmTelegraphMarker* Marker = Charger->InfernoChargeMarker.Get();
	if (TestNotNull(TEXT("the lane it will run is drawn on the floor"), Marker))
	{
		TestTrue(TEXT("as a lane"), Marker->IsLane());
		TestEqual(TEXT("as wide as the charge"), Marker->RadiusCm,
				  Modifiers_t::InfernoChargeHalfWidthCm);
		TestEqual(TEXT("and as long as the way to the player"), Marker->LengthCm,
				  static_cast<float>(FVector::Dist2D(
					  StoodAt, FVector(900.0f, 0.0f, 0.0f))),
				  0.5f);
	}

	// IT STANDS. The brain answers a channelling creature by standing it still,
	// rather than walking it at the player nine metres away.
	TestEqual(TEXT("the brain stands it for the channel"),
			  static_cast<int32>(Brain->Think()),
			  static_cast<int32>(ECataclysmBrainAction::WindingUp));

	// THE LANE DOES NOT FOLLOW THE PLAYER. The player steps sideways out of it.
	const FVector Aimed = Charger->InfernoChargeTo;
	Quarry->SetActorLocation(FVector(0.0f, 900.0f, 0.0f));

	// SEVEN MORE STEPS IS 1.75 SECONDS OF CHANNEL: still standing, no charge
	// and no path.
	for (int32 Step = 0; Step < 7; ++Step)
	{
		Modifiers_t::TimedStep(Charger, 0.25f);
	}
	TestTrue(TEXT("at 1.75 seconds it is still channelling"),
			 Charger->IsChannellingInfernoCharge());
	TestFalse(TEXT("and has not charged"), Charger->IsCharging());
	TestFalse(TEXT("and nothing burns yet"),
			  Charger->LastInfernoPathLeftBurning.IsValid());

	// THE EIGHTH IS TWO SECONDS: THE CHARGE SETS OFF.
	Modifiers_t::TimedStep(Charger, 0.25f);
	TestFalse(TEXT("the channel is over at two seconds"),
			  Charger->IsChannellingInfernoCharge());
	TestTrue(TEXT("and the charge has set off"), Charger->IsCharging());
	TestEqual(TEXT("down the lane drawn when the channel began"),
			  Charger->InfernoChargeTo, Aimed);
	TestEqual(TEXT("which ran to where the player stood then"),
			  FVector2D(Aimed), FVector2D(900.0f, 0.0f));

	// AND ITS PATH BURNS: the Hellhound's lane, copied.
	const ACataclysmGroundZone* Path =
		Charger->LastInfernoPathLeftBurning.Get();
	if (!TestNotNull(TEXT("the path it runs is left burning"), Path))
	{
		return false;
	}
	TestTrue(TEXT("along a path rather than at a point"), Path->IsLong());
	TestEqual(TEXT("from where it stood"), FVector2D(Path->GetActorLocation()),
			  FVector2D(StoodAt));
	TestEqual(TEXT("to where it was aimed"), FVector2D(Path->FarEnd),
			  FVector2D(900.0f, 0.0f));
	TestEqual(TEXT("as wide as the charge"), Path->RadiusCm,
			  Modifiers_t::InfernoChargeHalfWidthCm);
	TestEqual(TEXT("a quarter of a hit a second"), Path->DamagePerTick,
			  AttackDamage * Modifiers_t::InfernoPathPercent / 100.0f);
	TestFalse(TEXT("burning the player and not the creature's own side"),
			  Path->bBurnsEveryone);

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

// ---------------------------------------------------------------------------
// A medic heals its allies
// ---------------------------------------------------------------------------

namespace CataclysmMedicTest
{
	/**
	 * One full second of the per-character step, which runs four times a second.
	 *
	 * THROUGH `AuraStep` AND NOT THROUGH THE HEALING DIRECTLY, because that is
	 * what `CataclysmCharacterBase` calls. Driving the healing on its own would
	 * pass whether or not anything had wired it to a creature's step.
	 */
	static int32 OneSecondOfSteps(AActor* Medic)
	{
		int32 Healed = 0;
		for (int32 Step = 0; Step < 4; ++Step)
		{
			Healed += UCataclysmEnemyModifiers::AuraStep(Medic, 0.25f);
		}
		return Healed;
	}

	/** A creature with a stated maximum, wounded to a stated current. */
	static ACataclysmEnemyCharacter* WoundedCreature(UWorld* World,
													const FVector& Where,
													float Maximum, float Current)
	{
		ACataclysmEnemyCharacter* Creature =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where,
														FRotator::ZeroRotator);
		if (Creature == nullptr)
		{
			return nullptr;
		}

		// SetHealth SETS BOTH, which is why the wound is a second step: its own
		// header says setting the current alone is clamped to the old maximum.
		Creature->SetHealth(Maximum);
		Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));

		if (UAbilitySystemComponent* ASC = Creature->GetAbilitySystemComponent())
		{
			ASC->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), Current);
		}
		return Creature;
	}

	static ACataclysmEnemyCharacter* TheFloorsMedic(UWorld* World,
												   const FVector& Where,
												   float Maximum, float Current)
	{
		ACataclysmEnemyCharacter* Creature =
			WoundedCreature(World, Where, Maximum, Current);
		if (Creature != nullptr)
		{
			// WHAT THE DUNGEON DOES AT SPAWN, done by hand here.
			// `ACataclysmDungeonGameMode::ChooseTheFloorsMedic` sets this on one
			// creature when the floor carries `War_Field_Medic`.
			Creature->bHealsAlliesForTheFloorRule = true;
		}
		return Creature;
	}

	static float HealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* ASC =
			UCataclysmTargeting::AbilitySystemOf(Who);
		return ASC ? ASC->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}
}

CATACLYSM_MODIFIER_TEST(FCataclysmMedicHealsAWoundedAllyTest,
	"Cataclysm.EnemyModifiers.AMedicHealsAWoundedAllyStandingNearIt")
{
	using namespace CataclysmMedicTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Medic =
		TheFloorsMedic(World, FVector::ZeroVector, 500.0f, 500.0f);
	ACataclysmEnemyCharacter* Ally =
		WoundedCreature(World, FVector(200.0f, 0.0f, 0.0f), 500.0f, 200.0f);
	if (!TestNotNull(TEXT("a medic"), Medic)
		|| !TestNotNull(TEXT("a wounded ally"), Ally))
	{
		return false;
	}

	// ROOM TO HEAL INTO, asserted rather than assumed. TopUp is clamped to the
	// maximum, so an ally already at full would gain nothing and this would
	// pass against a rule that healed nobody.
	if (!TestEqual(TEXT("the ally starts wounded"), HealthOf(Ally), 200.0f, 0.01f))
	{
		return false;
	}

	const int32 Healed = OneSecondOfSteps(Medic);

	TestEqual(TEXT("one pulse healed one ally"), Healed, 1);

	// FIVE PERCENT OF THE ALLY'S OWN MAXIMUM, which is 25 of its 500.
	TestEqual(TEXT("and it gained five percent of its own maximum"),
			  HealthOf(Ally), 225.0f, 0.01f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmMedicDoesNotHealItselfTest,
	"Cataclysm.EnemyModifiers.AMedicDoesNotHealItself")
{
	using namespace CataclysmMedicTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE ROW SAYS "ALL OTHER ENEMIES", and FindAlliesInSphere excludes the
	// instigator, so this asserts a property of the search rather than of a
	// rule written here. It is worth a test because a medic that healed itself
	// would be far harder to kill than the row intends.
	ACataclysmEnemyCharacter* Medic =
		TheFloorsMedic(World, FVector::ZeroVector, 500.0f, 200.0f);
	ACataclysmEnemyCharacter* Ally =
		WoundedCreature(World, FVector(200.0f, 0.0f, 0.0f), 500.0f, 200.0f);
	if (!TestNotNull(TEXT("a wounded medic"), Medic)
		|| !TestNotNull(TEXT("a wounded ally"), Ally))
	{
		return false;
	}

	OneSecondOfSteps(Medic);

	TestEqual(TEXT("the medic healed nobody into itself"),
			  HealthOf(Medic), 200.0f, 0.01f);

	// AND THE ALLY DID GAIN, which is what says the pulse happened at all.
	// Without this, a medic that healed nothing whatsoever would pass.
	TestEqual(TEXT("while the ally beside it was healed"),
			  HealthOf(Ally), 225.0f, 0.01f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmMedicHealsNeitherFarNorHostileTest,
	"Cataclysm.EnemyModifiers.AMedicHealsNeitherTheDistantNorTheOtherSide")
{
	using namespace CataclysmMedicTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Medic =
		TheFloorsMedic(World, FVector::ZeroVector, 500.0f, 500.0f);

	// JUST INSIDE AND JUST OUTSIDE THE SIX METRES, so this measures the radius
	// rather than some much larger difference.
	ACataclysmEnemyCharacter* Near =
		WoundedCreature(World, FVector(500.0f, 0.0f, 0.0f), 500.0f, 200.0f);
	ACataclysmEnemyCharacter* Far =
		WoundedCreature(World, FVector(900.0f, 0.0f, 0.0f), 500.0f, 200.0f);

	// THE OTHER SIDE, STANDING AS CLOSE AS THE ONE THAT IS HEALED. Distance is
	// therefore not what separates them, which is the whole point of the case.
	ACataclysmEnemyCharacter* Hostile =
		WoundedCreature(World, FVector(0.0f, 500.0f, 0.0f), 500.0f, 200.0f);
	if (!TestNotNull(TEXT("a medic"), Medic) || !TestNotNull(TEXT("a near ally"), Near)
		|| !TestNotNull(TEXT("a far ally"), Far)
		|| !TestNotNull(TEXT("someone on the other side"), Hostile))
	{
		return false;
	}
	Hostile->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));

	const int32 Healed = OneSecondOfSteps(Medic);

	TestEqual(TEXT("exactly one of the three was healed"), Healed, 1);
	TestEqual(TEXT("the one inside six metres gained"),
			  HealthOf(Near), 225.0f, 0.01f);
	TestEqual(TEXT("the one outside it did not"),
			  HealthOf(Far), 200.0f, 0.01f);
	TestEqual(TEXT("and the other side did not, though it stood as close"),
			  HealthOf(Hostile), 200.0f, 0.01f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmDeadMedicHealsNobodyTest,
	"Cataclysm.EnemyModifiers.ADeadMedicHealsNobody")
{
	using namespace CataclysmMedicTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THIS IS WHAT MAKES KILLING THE MEDIC THE ANSWER, which is the row's
	// stated purpose: it "forces the player to prioritize a non-threatening
	// enemy". A dead medic that kept healing would defeat that entirely.
	//
	// AND IT IS WHY NO DEATH HOOK WAS WRITTEN. The aura ends with the creature
	// because nothing pulses for a dead one -- the opposite of a floor hazard,
	// which must outlive the death that created it. Issue #1605.
	ACataclysmEnemyCharacter* Medic =
		TheFloorsMedic(World, FVector::ZeroVector, 500.0f, 500.0f);
	ACataclysmEnemyCharacter* Ally =
		WoundedCreature(World, FVector(200.0f, 0.0f, 0.0f), 500.0f, 200.0f);
	if (!TestNotNull(TEXT("a medic"), Medic)
		|| !TestNotNull(TEXT("a wounded ally"), Ally))
	{
		return false;
	}

	// ASSERTED ALIVE FIRST, so "healed nobody" below cannot be true because the
	// medic was never able to heal anybody in this world at all.
	if (!TestEqual(TEXT("it heals while it lives"), OneSecondOfSteps(Medic), 1))
	{
		return false;
	}

	const float AfterOnePulse = HealthOf(Ally);

	Medic->GetAbilitySystemComponent()->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
	if (!TestTrue(TEXT("the medic is dead"),
				  UCataclysmSkillEffects::IsDead(Medic)))
	{
		return false;
	}

	TestEqual(TEXT("and now it heals nobody"), OneSecondOfSteps(Medic), 0);
	TestEqual(TEXT("the ally gained nothing further"),
			  HealthOf(Ally), AfterOnePulse, 0.01f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmMedicHealsOncePerPulseTest,
	"Cataclysm.EnemyModifiers.AMedicHealsOncePerPulseAndNotOnEveryStep")
{
	using namespace CataclysmMedicTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Medic =
		TheFloorsMedic(World, FVector::ZeroVector, 500.0f, 500.0f);
	ACataclysmEnemyCharacter* Ally =
		WoundedCreature(World, FVector(200.0f, 0.0f, 0.0f), 500.0f, 100.0f);
	if (!TestNotNull(TEXT("a medic"), Medic)
		|| !TestNotNull(TEXT("a wounded ally"), Ally))
	{
		return false;
	}

	// THREE OF THE FOUR STEPS IN A SECOND, so the accumulated time is 0.75 and
	// the pulse is not yet due. Without this the aura could be healing four
	// times a second and every other test here would still pass.
	for (int32 Step = 0; Step < 3; ++Step)
	{
		UCataclysmEnemyModifiers::AuraStep(Medic, 0.25f);
	}

	TestEqual(TEXT("nothing is healed before the second is up"),
			  HealthOf(Ally), 100.0f, 0.01f);

	// THE FOURTH STEP COMPLETES THE SECOND.
	TestEqual(TEXT("the fourth step pulses"),
			  UCataclysmEnemyModifiers::AuraStep(Medic, 0.25f), 1);
	TestEqual(TEXT("and it healed once, not four times"),
			  HealthOf(Ally), 125.0f, 0.01f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmMedicAndAuraShareOneClockTest,
	"Cataclysm.EnemyModifiers.AMedicCarryingAnAuraStillPulsesOncePerSecond")
{
	using namespace CataclysmMedicTest;

	// THIS IS THE CASE THE EARLIER SHAPE COULD NOT HAVE HAD. The healing used
	// to keep its own copy of the pulse clock, so a creature that was both the
	// floor's medic and carried an aura advanced ONE field twice every step and
	// everything it did fired twice as often as the design states.
	//
	// NO OTHER TEST HERE CAN SEE IT, because every other one gives the medic no
	// modifier rows, and with none the second clock was never advanced.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Medic =
		TheFloorsMedic(World, FVector::ZeroVector, 500.0f, 500.0f);
	ACataclysmEnemyCharacter* Ally =
		TheFloorsMedic(World, FVector(200.0f, 0.0f, 0.0f), 500.0f, 100.0f);
	if (!TestNotNull(TEXT("a medic"), Medic)
		|| !TestNotNull(TEXT("a wounded ally"), Ally))
	{
		return false;
	}

	// THE ALLY IS NOT A SECOND MEDIC. TheFloorsMedic marks whatever it makes,
	// and two medics healing each other would make the arithmetic below mean
	// nothing.
	Ally->bHealsAlliesForTheFloorRule = false;

	// AND THE MEDIC ALSO CARRIES AN AURA, which is the whole point: both
	// behaviours now read and clear the same field.
	Medic->ModifierRows.Add(FName(UCataclysmEnemyModifiers::HellfireAuraRow));

	// THREE QUARTERS OF A SECOND. A clock advanced twice a step would already
	// have passed one second here and healed.
	for (int32 Step = 0; Step < 3; ++Step)
	{
		UCataclysmEnemyModifiers::AuraStep(Medic, 0.25f);
	}
	TestEqual(TEXT("nothing is healed before the second is up"),
			  HealthOf(Ally), 100.0f, 0.01f);

	// THE FOURTH STEP COMPLETES THE SECOND, and heals once rather than twice.
	UCataclysmEnemyModifiers::AuraStep(Medic, 0.25f);
	TestEqual(TEXT("and then it heals five percent once, not twice"),
			  HealthOf(Ally), 125.0f, 0.01f);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmMedicStepIsSafeTest,
	"Cataclysm.EnemyModifiers.TheMedicStepRefusesAnythingThatIsNotACreature")
{
	// THE SAME REFUSAL THE AURA STEP MAKES, for the same reason: whatever calls
	// this may hand it a player, or a null from a torn-down world.
	//
	// THE HEALING DIRECTLY RATHER THAN THROUGH `AuraStep`, because the step's
	// own refusal already has a test of its own and this one is about the
	// healing keeping its guard even though its only caller checks first.
	TestEqual(TEXT("a null actor heals nobody"),
			  UCataclysmEnemyModifiers::HealAlliesPulse(nullptr), 0);

	return true;
}

// ---------------------------------------------------------------------------
// A medic starts nothing hostile, whatever it drew. Issue #1680
// ---------------------------------------------------------------------------

CATACLYSM_MODIFIER_TEST(FCataclysmMedicBurnsNobodyTest,
	"Cataclysm.EnemyModifiers.AMedicWithABurningAuraBurnsNobody")
{
	using namespace CataclysmEnemyModifierTest;

	// WHY THIS CASE EXISTS AT ALL. The floor's medic is chosen as the rarest
	// creature there, and rarity is how many modifiers a creature draws -- so
	// the medic is the creature MOST likely to have drawn a burning aura.
	// Between 17% at Elite and 55% at Boss carry one of the three traits that
	// harm without the creature deciding to.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Burner =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature"), Burner))
	{
		return false;
	}
	Burner->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Burner->SetHealth(500.0f);
	Burner->ModifierRows.Add(FName(UCataclysmEnemyModifiers::HellfireAuraRow));

	ACataclysmPlayerCharacter* Victim = SpawnPlayerWithState(World);
	if (!TestNotNull(TEXT("somebody to burn"), Victim))
	{
		return false;
	}
	Victim->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));

	// THE CONTROL, AND IT IS THE HALF THAT MATTERS. Without it, "caught
	// nobody" passes for a creature that was never in range, never pulsed, or
	// never carried the aura at all.
	int32 TouchedOrdinary = 0;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		TouchedOrdinary += UCataclysmEnemyModifiers::AuraStep(Burner, 0.25f);
	}
	TestEqual(TEXT("an ordinary creature's burning aura catches the player"),
			  TouchedOrdinary, 1);

	// ONE THING CHANGES.
	Burner->bHealsAlliesForTheFloorRule = true;
	Burner->SecondsSinceAuraPulse = 0.0f;

	int32 TouchedAsMedic = 0;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		TouchedAsMedic += UCataclysmEnemyModifiers::AuraStep(Burner, 0.25f);
	}
	TestEqual(TEXT("and the same creature as the floor's medic catches nobody"),
			  TouchedAsMedic, 0);

	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmMedicNeitherChargesNorEatsTest,
	"Cataclysm.EnemyModifiers.AMedicNeitherChargesNorEatsAnAlly")
{
	using namespace CataclysmEnemyModifierTest;

	// TWO TRAITS, TWO SEPARATE REASONS, AND THEY ARE TESTED TOGETHER ONLY
	// BECAUSE ONE FUNCTION DRIVES BOTH.
	//
	// The charge goes because a creature that dashes at the player and hurts
	// it is threatening, and the row exists to put a non-threatening enemy on
	// the floor.
	//
	// EATING AN ALLY IS NOT AN ATTACK AND GOES FOR A DIFFERENT REASON: the row
	// says the medic "constantly heals all other enemies in a large radius",
	// and a trait that consumes one contradicts what the creature DOES rather
	// than what it is for.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Carrier =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature"), Carrier))
	{
		return false;
	}
	Carrier->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Carrier->SetHealth(500.0f);
	// BOTH TRAITS, THE SAME TWO THE MEDIC BELOW CARRIES. The control has to
	// differ from the medic in the MARK and in nothing else. The first draft
	// gave this creature only the charge, so it could never have eaten
	// anybody -- and that went unnoticed because the assertion was reading a
	// destroyed actor and reporting whatever it found.
	Carrier->ModifierRows.Add(FName(UCataclysmEnemyModifiers::InfernoChargeRow));
	Carrier->ModifierRows.Add(
		FName(UCataclysmEnemyModifiers::InfernalSacrificeRow));

	ACataclysmPlayerCharacter* Quarry = SpawnPlayerWithState(World);
	if (!TestNotNull(TEXT("somebody to charge"), Quarry))
	{
		return false;
	}
	Quarry->SetActorLocation(FVector(900.0f, 0.0f, 0.0f));

	// SOMEBODY FOR THE ORDINARY CREATURE TO EAT, 100cm away and well inside
	// the six metres the sacrifice searches.
	ACataclysmEnemyCharacter* Meal =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(100.0f, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an ally for the ordinary creature"), Meal))
	{
		return false;
	}
	Meal->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Meal->SetHealth(500.0f);

	// HELD WEAKLY, BECAUSE BEING EATEN DESTROYS IT. The sacrifice calls the
	// creature's death handler, and a dead creature destroys itself once its
	// death animation has played -- well inside the twelve seconds stepped
	// below. Reading a raw pointer afterwards is undefined, and the first
	// draft of this test did exactly that: the same assertion passed in one
	// run and failed in the next, which is how the break proof found it.
	TWeakObjectPtr<ACataclysmEnemyCharacter> EatenAlly = Meal;

	// TWELVE SECONDS AT A QUARTER OF A SECOND A STEP. The charge fires every
	// twelve and the sacrifice every six, so both have a chance.
	//
	// BOTH CONTROLS MATTER AND THE SECOND ONE MATTERS MORE. Without showing
	// that an ordinary creature DOES eat its ally here, "the medic's ally
	// survived" would pass if the sacrifice never reached that far, never came
	// due, or found nothing it was willing to eat.
	for (int32 Step = 0; Step < 48; ++Step)
	{
		UCataclysmEnemyModifiers::TimedStep(Carrier, 0.25f);
	}
	if (!TestTrue(TEXT("an ordinary creature carrying the charge charges"),
				  Carrier->IsCharging()))
	{
		return false;
	}
	// GONE OR DEAD, BECAUSE BOTH MEAN EATEN. A creature that has been
	// sacrificed may still be lying there or may already have destroyed
	// itself, and which one it is depends on timing this test does not
	// control.
	const bool bMealIsGone = !EatenAlly.IsValid()
		|| UCataclysmSkillEffects::IsDead(EatenAlly.Get());
	if (!TestTrue(TEXT("and an ordinary creature eats the ally beside it"),
				  bMealIsGone))
	{
		return false;
	}

	// NOW THE SAME THING AT THE SAME PLACES, WITH ONE DIFFERENCE.
	//
	// The first pair is destroyed and the medic takes the identical position,
	// so the geometry is not what separates the two halves -- the mark is. A
	// medic parked somewhere else could have failed to charge because the
	// player was out of range and failed to eat because nothing was near it.
	Carrier->Destroy();
	if (EatenAlly.IsValid())
	{
		EatenAlly->Destroy();
	}

	ACataclysmEnemyCharacter* Medic =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a medic"), Medic))
	{
		return false;
	}
	Medic->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Medic->SetHealth(500.0f);
	Medic->ModifierRows.Add(FName(UCataclysmEnemyModifiers::InfernoChargeRow));
	Medic->ModifierRows.Add(
		FName(UCataclysmEnemyModifiers::InfernalSacrificeRow));
	Medic->bHealsAlliesForTheFloorRule = true;

	ACataclysmEnemyCharacter* Patient =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(100.0f, 0.0f, 0.0f),
													FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an ally for the medic"), Patient))
	{
		return false;
	}
	Patient->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Patient->SetHealth(500.0f);

	// HELD WEAKLY FOR THE REASON THE FIRST ONE IS, even though this one is
	// expected to survive: if the rule ever breaks, this ally is eaten and
	// destroyed, and the assertion below would then be reading freed memory
	// rather than reporting a failure.
	TWeakObjectPtr<ACataclysmEnemyCharacter> MedicsAlly = Patient;

	for (int32 Step = 0; Step < 48; ++Step)
	{
		UCataclysmEnemyModifiers::TimedStep(Medic, 0.25f);
	}

	TestFalse(TEXT("a medic carrying the charge never charges"),
			  Medic->IsCharging());
	TestTrue(TEXT("and the ally beside it is untouched"),
			 MedicsAlly.IsValid()
				 && !UCataclysmSkillEffects::IsDead(MedicsAlly.Get()));

	return true;
}

// ---------------------------------------------------------------------------
// SACRIFICIAL BOND: A LARGE ENOUGH BLOW KILLS A BONDED CREATURE. Issue #2289.
//
// The project owner, 2026-10-08, asked "should a large enough blow kill a creature under Sacrificial Bond?":
// "yes". Ruled by the coordinating session under the owner's delegation the same day, resting on that answer:
// the whole blow is divided before it is cut to the health left; the creature dies when its share of the whole
// blow is at least its remaining health; each ally takes the whole blow less the kept share, divided among the
// allies.
//
// EVERY AMOUNT IS READ AGAINST A CONTROL: the same blow, or the same tick, on a creature that carries no bond and
// has health to spare. No figure of what a blow takes is typed here. A bonded creature's health is then written
// as a part of that measured figure, well away from the line on both sides: three quarters of it where the
// creature must live, two fifths of it where it must die. The half it keeps is a half of the measured figure.
//
// WHERE THE ACTORS STAND is said at the top of each test. No two are within two metres of each other. Nothing
// here waits: every blow and every tick is made by hand.
// ---------------------------------------------------------------------------

namespace CataclysmBondKillsTest
{
	using namespace CataclysmBondAndSigilTest;

	/** Write a creature's health now. Every figure written here is under the 100,000 it was made with. */
	void PutHealthAt(ACataclysmEnemyCharacter* Creature, float Health)
	{
		if (UAbilitySystemComponent* Own = Creature ? Creature->GetAbilitySystemComponent() : nullptr)
		{
			Own->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		}
	}

	/** Whether a creature has died: marked dead, or already gone from the world. */
	bool HasDied(const ACataclysmEnemyCharacter* Creature)
	{
		return !IsValid(Creature) || UCataclysmSkillEffects::IsDead(Creature);
	}

	/** What the player's blow of 1,000 takes from a creature that carries no bond and has health to spare. */
	float WholeBlowOn(ACataclysmPlayerCharacter* Player, ACataclysmEnemyCharacter* Control)
	{
		const float Before = HealthOf(Control);
		UCataclysmSkillEffects::ApplyDirectDamage(Player, Control, 1000.0f, FCataclysmHitDelivery());
		return Before - HealthOf(Control);
	}
}

CATACLYSM_MODIFIER_TEST(FCataclysmBondLargeBlowKillsTest,
	"Cataclysm.EnemyModifiers.ABlowWhoseShareReachesABondedCreaturesHealthKillsIt")
{
	// STANDING: the player 50 m along Y. The bonded creature at the origin and its ally 2 m along X. The control,
	// which carries no bond, 30 m the other way along X.
	using namespace CataclysmBondKillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = Striker(World);
	ACataclysmEnemyCharacter* Bonded = Creature(World, FVector::ZeroVector, 100000.0f);
	ACataclysmEnemyCharacter* Ally = Creature(World, FVector(200.0f, 0.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* Control = Creature(World, FVector(-3000.0f, 0.0f, 0.0f), 100000.0f);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a bonded creature"), Bonded)
		|| !TestNotNull(TEXT("an ally"), Ally) || !TestNotNull(TEXT("a control"), Control))
	{
		return false;
	}
	Bonded->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));

	// THE WHOLE BLOW, MEASURED ON A CREATURE WITH NO BOND.
	const float Whole = WholeBlowOn(Player, Control);
	if (!TestTrue(FString::Printf(TEXT("set-up: the blow takes health from a creature with no bond, and under half "
									   "of what it holds (%.2f)"), Whole),
				  Whole > 10.0f && Whole < 50000.0f)
		|| !TestEqual(TEXT("set-up: the bond shares with the one ally"),
					  UCataclysmEnemyModifiers::ShareOfDamageKept(Bonded), 0.5f, 0.001f))
	{
		return false;
	}

	// THE CONTROL: ITS HALF OF THE BLOW IS UNDER ITS HEALTH, SO IT LIVES AND LOSES HALF OF THE WHOLE BLOW. The blow
	// is larger than the three quarters of it the creature holds. Before issue #2289 the blow was cut to that
	// health first and the creature lost half of the cut figure, which is three eighths of the blow.
	PutHealthAt(Bonded, Whole * 0.75f);
	UCataclysmSkillEffects::ApplyDirectDamage(Player, Bonded, 1000.0f, FCataclysmHitDelivery());
	TestFalse(TEXT("control: a blow whose half is under the bonded creature's health does not kill it"),
			  HasDied(Bonded));
	TestEqual(TEXT("control: and it loses half of the whole blow"),
			  Whole * 0.75f - HealthOf(Bonded), Whole * 0.5f, 0.01f);

	// THE CASE: THE SAME BLOW ON THE SAME PAIR, AND ITS HALF IS NOW MORE THAN THE HEALTH HELD.
	PutHealthAt(Bonded, Whole * 0.4f);
	if (!TestFalse(TEXT("set-up: the bonded creature stands before the killing blow"), HasDied(Bonded))
		|| !TestEqual(TEXT("set-up: and still shares with its one ally"),
					  UCataclysmEnemyModifiers::ShareOfDamageKept(Bonded), 0.5f, 0.001f))
	{
		return false;
	}
	UCataclysmSkillEffects::ApplyDirectDamage(Player, Bonded, 1000.0f, FCataclysmHitDelivery());
	TestTrue(TEXT("a blow whose half is at least the bonded creature's health kills it"), HasDied(Bonded));
	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmBondKillPaysAllyTest,
	"Cataclysm.EnemyModifiers.AKillingBlowOnABondedCreatureTakesHalfTheWholeBlowFromItsAlly")
{
	// STANDING: the player 50 m along Y. The bonded creature at the origin and its ally 2 m along X. The control,
	// which carries no bond, 30 m the other way along X.
	using namespace CataclysmBondKillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = Striker(World);
	ACataclysmEnemyCharacter* Bonded = Creature(World, FVector::ZeroVector, 100000.0f);
	ACataclysmEnemyCharacter* Ally = Creature(World, FVector(200.0f, 0.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* Control = Creature(World, FVector(-3000.0f, 0.0f, 0.0f), 100000.0f);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a bonded creature"), Bonded)
		|| !TestNotNull(TEXT("an ally"), Ally) || !TestNotNull(TEXT("a control"), Control))
	{
		return false;
	}
	Bonded->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));

	// THE WHOLE BLOW, MEASURED ON A CREATURE WITH NO BOND, as the first test of the bond's blows measures it.
	const float Whole = WholeBlowOn(Player, Control);
	if (!TestTrue(FString::Printf(TEXT("set-up: the blow takes health from a creature with no bond, and under half "
									   "of what it holds (%.2f)"), Whole),
				  Whole > 10.0f && Whole < 50000.0f)
		|| !TestEqual(TEXT("set-up: the bond shares with the one ally"),
					  UCataclysmEnemyModifiers::ShareOfDamageKept(Bonded), 0.5f, 0.001f))
	{
		return false;
	}

	// THE BONDED CREATURE HOLDS TWO FIFTHS OF THE BLOW, SO ITS HALF KILLS IT. The ally is paid the whole blow less
	// that half, and not the health the bonded creature held less its half. Before issue #2289 the ally was paid
	// half of the two fifths, one fifth of the blow.
	PutHealthAt(Bonded, Whole * 0.4f);
	UCataclysmSkillEffects::ApplyDirectDamage(Player, Bonded, 1000.0f, FCataclysmHitDelivery());
	TestTrue(TEXT("the blow kills the bonded creature"), HasDied(Bonded));

	const float AllyLost = 100000.0f - HealthOf(Ally);
	TestEqual(TEXT("its ally loses half of the whole blow"), AllyLost, Whole * 0.5f, 0.01f);
	TestTrue(TEXT("which is more than the bonded creature had left"), AllyLost > Whole * 0.4f + 0.01f);
	TestFalse(TEXT("and the ally, with health to spare, still stands"), HasDied(Ally));
	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmBondTwoBondedCountTest,
	"Cataclysm.EnemyModifiers.TwoBondedCreaturesOfEqualHealthFallToTheFourthEqualBlowOnOne")
{
	// STANDING: the player 50 m along Y. The two bonded creatures at the origin and 2 m along X. The control, which
	// carries no bond, 30 m the other way along X.
	//
	// THE COUNT, BY ARITHMETIC. Call what one blow takes W. Each of the two holds 1.75 W and each carries the bond.
	// The one struck keeps half of every blow, W / 2, and the other is paid the other W / 2 as a direct reduction,
	// which is not shared again. So both lose W / 2 a blow:
	//     after blow 1 each holds 1.25 W; after blow 2, 0.75 W; after blow 3, 0.25 W.
	//     Blow 3 is larger than the 0.75 W held and its half, 0.5 W, is under it, so the struck one lives.
	//     Blow 4: its half, 0.5 W, is at least the 0.25 W held, so the struck one dies. The other is paid 0.5 W
	//     against the 0.25 W it holds and dies of the same blow.
	// The struck one dies on the first blow n with n x W / 2 at least 1.75 W, which is n = 4.
	//
	// BEFORE ISSUE #2289 NEITHER DIED. From blow 3 every blow was cut to the health held before it was halved, so
	// each blow took half of what was left and the cap of ten blows below was reached.
	using namespace CataclysmBondKillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = Striker(World);
	ACataclysmEnemyCharacter* Struck = Creature(World, FVector::ZeroVector, 100000.0f);
	ACataclysmEnemyCharacter* Other = Creature(World, FVector(200.0f, 0.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* Control = Creature(World, FVector(-3000.0f, 0.0f, 0.0f), 100000.0f);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a bonded creature to strike"), Struck)
		|| !TestNotNull(TEXT("a second bonded creature"), Other) || !TestNotNull(TEXT("a control"), Control))
	{
		return false;
	}
	Struck->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));
	Other->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));

	const float Whole = WholeBlowOn(Player, Control);
	if (!TestTrue(FString::Printf(TEXT("set-up: the blow takes health from a creature with no bond, and under half "
									   "of what it holds (%.2f)"), Whole),
				  Whole > 10.0f && Whole < 50000.0f)
		|| !TestEqual(TEXT("set-up: the struck creature shares with the other"),
					  UCataclysmEnemyModifiers::ShareOfDamageKept(Struck), 0.5f, 0.001f)
		|| !TestEqual(TEXT("set-up: and the other with it"),
					  UCataclysmEnemyModifiers::ShareOfDamageKept(Other), 0.5f, 0.001f))
	{
		return false;
	}
	PutHealthAt(Struck, Whole * 1.75f);
	PutHealthAt(Other, Whole * 1.75f);

	int32 Blows = 0;
	float StruckAfterThree = -1.0f;
	float OtherAfterThree = -1.0f;
	while (Blows < 10 && !HasDied(Struck))
	{
		UCataclysmSkillEffects::ApplyDirectDamage(Player, Struck, 1000.0f, FCataclysmHitDelivery());
		++Blows;
		if (Blows == 3)
		{
			StruckAfterThree = HealthOf(Struck);
			OtherAfterThree = HealthOf(Other);
		}
	}

	TestEqual(TEXT("the struck creature dies on the fourth equal blow"), Blows, 4);
	TestTrue(TEXT("and it did die, rather than the count of blows running out"), HasDied(Struck));
	TestEqual(TEXT("after three blows it held a quarter of one blow"), StruckAfterThree, Whole * 0.25f, 0.01f);
	TestEqual(TEXT("and the other, paid the same shares, held the same"), OtherAfterThree, Whole * 0.25f, 0.01f);
	TestTrue(TEXT("the other dies of the fourth blow as well"), HasDied(Other));
	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmBondKillHeldBySigilTest,
	"Cataclysm.EnemyModifiers.ABondedCreatureInAnUnholySigilIsLeftAtOneHealthByAKillingShare")
{
	// STANDING: the player 50 m along Y. The sigil's caster 11 m along X: outside the bond's 6 m, with the bonded
	// creature at the origin inside its sigil's 12 m. The bonded creature's ally 2 m the other way along X, 13 m
	// from the caster and outside the sigil. The control pair 40 m along -Y, outside the sigil: a bonded creature
	// and its ally 2 m along X from it. The creature with no bond, which measures the blow, 30 m along -X.
	using namespace CataclysmBondKillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = Striker(World);
	ACataclysmEnemyCharacter* Caster = Creature(World, FVector(1100.0f, 0.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* Bonded = Creature(World, FVector::ZeroVector, 100000.0f);
	ACataclysmEnemyCharacter* Ally = Creature(World, FVector(-200.0f, 0.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* Outside = Creature(World, FVector(0.0f, -4000.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* OutsideAlly = Creature(World, FVector(200.0f, -4000.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* Control = Creature(World, FVector(-3000.0f, 0.0f, 0.0f), 100000.0f);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a caster"), Caster)
		|| !TestNotNull(TEXT("a bonded creature"), Bonded) || !TestNotNull(TEXT("an ally"), Ally)
		|| !TestNotNull(TEXT("a bonded creature outside"), Outside)
		|| !TestNotNull(TEXT("an ally outside"), OutsideAlly) || !TestNotNull(TEXT("a control"), Control))
	{
		return false;
	}
	LaySigil(Caster);
	Bonded->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));
	Outside->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));
	if (!TestTrue(TEXT("set-up: the bonded creature stands in the sigil"),
				  UCataclysmEnemyModifiers::IsProtectedBySigil(Bonded))
		|| !TestFalse(TEXT("set-up: its ally stands outside the sigil"),
					  UCataclysmEnemyModifiers::IsProtectedBySigil(Ally))
		|| !TestFalse(TEXT("set-up: the control pair's bonded creature stands outside the sigil"),
					  UCataclysmEnemyModifiers::IsProtectedBySigil(Outside))
		|| !TestEqual(TEXT("set-up: the bond shares with the one ally, and not with the caster"),
					  UCataclysmEnemyModifiers::ShareOfDamageKept(Bonded), 0.5f, 0.001f)
		|| !TestEqual(TEXT("set-up: and the control pair's bond shares with its one ally"),
					  UCataclysmEnemyModifiers::ShareOfDamageKept(Outside), 0.5f, 0.001f))
	{
		return false;
	}

	const float Whole = WholeBlowOn(Player, Control);
	if (!TestTrue(FString::Printf(TEXT("set-up: the blow takes health from a creature with no bond, and under half "
									   "of what it holds (%.2f)"), Whole),
				  Whole > 10.0f && Whole < 50000.0f))
	{
		return false;
	}

	// BOTH BONDED CREATURES HOLD TWO FIFTHS OF THE BLOW, SO THE HALF EACH KEEPS IS MORE THAN IT HOLDS.
	PutHealthAt(Bonded, Whole * 0.4f);
	PutHealthAt(Outside, Whole * 0.4f);

	UCataclysmSkillEffects::ApplyDirectDamage(Player, Bonded, 1000.0f, FCataclysmHitDelivery());
	TestFalse(TEXT("a blow whose half is at least its health does not kill a bonded creature in the sigil"),
			  HasDied(Bonded));
	TestEqual(TEXT("it is left at one health"), HealthOf(Bonded), 1.0f, 0.001f);
	TestEqual(TEXT("and its stored base agrees"), BaseHealthOf(Bonded), 1.0f, 0.001f);
	TestEqual(TEXT("its ally outside the sigil still loses half of the whole blow"),
			  100000.0f - HealthOf(Ally), Whole * 0.5f, 0.01f);

	// THE CONTROL: THE SAME BLOW ON THE SAME BONDED CREATURE OUTSIDE THE SIGIL KILLS.
	UCataclysmSkillEffects::ApplyDirectDamage(Player, Outside, 1000.0f, FCataclysmHitDelivery());
	TestTrue(TEXT("control: the same blow kills a bonded creature outside the sigil"), HasDied(Outside));
	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmBondTickKillsTest,
	"Cataclysm.EnemyModifiers.ATickWhoseShareReachesABondedCreaturesHealthKillsIt")
{
	// STANDING: the player 50 m along Y. The bonded creature at the origin and its ally 2 m along X. The control,
	// which carries no bond, 30 m the other way along X.
	//
	// THE TICKS ARE RUN BY HAND, one at a time, with `ExecutePeriodicEffectsGrantingForTests`: a test world runs no
	// timers. No time passes, so the bleed of four seconds is running for every tick here.
	using namespace CataclysmBondKillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = Striker(World);
	ACataclysmEnemyCharacter* Bonded = Creature(World, FVector::ZeroVector, 100000.0f);
	ACataclysmEnemyCharacter* Ally = Creature(World, FVector(200.0f, 0.0f, 0.0f), 100000.0f);
	ACataclysmEnemyCharacter* Control = Creature(World, FVector(-3000.0f, 0.0f, 0.0f), 100000.0f);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a bonded creature"), Bonded)
		|| !TestNotNull(TEXT("an ally"), Ally) || !TestNotNull(TEXT("a control"), Control))
	{
		return false;
	}
	Bonded->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));

	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	UCataclysmAbilitySystemComponent* ControlSystem =
		Cast<UCataclysmAbilitySystemComponent>(Control->GetAbilitySystemComponent());
	UCataclysmAbilitySystemComponent* BondedSystem =
		Cast<UCataclysmAbilitySystemComponent>(Bonded->GetAbilitySystemComponent());
	if (!TestTrue(TEXT("set-up: the bleed's tag"), Bleed.IsValid())
		|| !TestNotNull(TEXT("set-up: the control's ability system"), ControlSystem)
		|| !TestNotNull(TEXT("set-up: the bonded creature's ability system"), BondedSystem))
	{
		return false;
	}

	// THE WHOLE TICK, MEASURED ON A CREATURE WITH NO BOND. A bleed of 1,000 a tick, not scaled by the player.
	if (!TestTrue(TEXT("set-up: the bleed is applied to the control"),
				  UCataclysmSkillEffects::ApplyDamageOverTime(Player, Control, /*DamagePerTick=*/1000.0f,
															  /*DurationSeconds=*/4.0f, Bleed,
															  /*bScalesWithInstigator=*/false)))
	{
		return false;
	}
	const float ControlBefore = HealthOf(Control);
	const int32 ControlTicks = ControlSystem->ExecutePeriodicEffectsGrantingForTests(Bleed);
	const float WholeTick = ControlBefore - HealthOf(Control);
	if (!TestEqual(TEXT("set-up: one bleed ticked on the control"), ControlTicks, 1)
		|| !TestTrue(FString::Printf(TEXT("set-up: the tick takes health from a creature with no bond, and under "
										  "half of what it holds (%.2f)"), WholeTick),
					 WholeTick > 10.0f && WholeTick < 50000.0f)
		|| !TestEqual(TEXT("set-up: the bond shares with the one ally"),
					  UCataclysmEnemyModifiers::ShareOfDamageKept(Bonded), 0.5f, 0.001f)
		|| !TestTrue(TEXT("set-up: the same bleed is applied to the bonded creature"),
					 UCataclysmSkillEffects::ApplyDamageOverTime(Player, Bonded, /*DamagePerTick=*/1000.0f,
																 /*DurationSeconds=*/4.0f, Bleed,
																 /*bScalesWithInstigator=*/false)))
	{
		return false;
	}

	// THE CONTROL: THE TICK'S HALF IS UNDER THE HEALTH HELD, SO THE BONDED CREATURE LIVES AND LOSES HALF OF THE
	// WHOLE TICK. Its health is written after the bleed is applied and read straight after the tick.
	PutHealthAt(Bonded, WholeTick * 0.75f);
	const float AllyBefore = HealthOf(Ally);
	TestEqual(TEXT("set-up: one bleed ticked on the bonded creature"),
			  BondedSystem->ExecutePeriodicEffectsGrantingForTests(Bleed), 1);
	TestFalse(TEXT("control: a tick whose half is under the bonded creature's health does not kill it"),
			  HasDied(Bonded));
	TestEqual(TEXT("control: and it loses half of the whole tick"),
			  WholeTick * 0.75f - HealthOf(Bonded), WholeTick * 0.5f, 0.01f);
	TestEqual(TEXT("control: and its ally loses the other half"),
			  AllyBefore - HealthOf(Ally), WholeTick * 0.5f, 0.01f);

	// THE CASE: THE NEXT TICK OF THE SAME BLEED, AND ITS HALF IS NOW MORE THAN THE HEALTH HELD.
	PutHealthAt(Bonded, WholeTick * 0.4f);
	if (!TestFalse(TEXT("set-up: the bonded creature stands before the killing tick"), HasDied(Bonded)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: the bleed ticked on the bonded creature again"),
			  BondedSystem->ExecutePeriodicEffectsGrantingForTests(Bleed), 1);
	TestTrue(TEXT("a tick whose half is at least the bonded creature's health kills it"), HasDied(Bonded));
	TestEqual(TEXT("and its ally loses half of the whole tick again"),
			  AllyBefore - HealthOf(Ally), WholeTick, 0.02f);
	return true;
}

CATACLYSM_MODIFIER_TEST(FCataclysmBondAllyShareOverkillTest,
	"Cataclysm.EnemyModifiers.AnAllyKilledByItsShareOfABondedKillAddsNoOverkillExplosion")
{
	// RULED 2026-10-08, a labelled judgement by the coordinating session under the owner's delegation: an ally killed
	// by its share of a blow on the bonded creature records an overkill of nought, because the figure is another
	// creature's. So "Enemies killed by you explode for the overkill amount" explodes the bonded creature's body
	// and not the ally's.
	//
	// STANDING, in each of two worlds: the player 50 m along Y, wearing the overkill explosion row at 100%. The bonded
	// creature at the origin. Its ally 2 m along X. A bystander 3 m along Y from the bonded creature, 3.6 m from the
	// ally: inside the explosion's 5 m of both bodies. The bystander is a creature of the same team, so it is the
	// bond's second ally and each of the three keeps or is paid a third. A creature with no bond 30 m along -X,
	// out of every explosion, on which the blow and the explosion's blow are measured.
	//
	// WHAT HAPPENS, AS READ. W is what the blow takes from a creature with health to spare. The bonded creature
	// holds 0.2 W, keeps a third of the blow and dies; its death records an overkill of the blow less 0.2 W. Its
	// death is handled inside its health write, so its body explodes before its allies are paid: the ally and the
	// bystander each take E, what a blow of that overkill delivered as the explosion's takes. Then each is paid a
	// third of the blow.
	//
	// THE CONTROL: the ally has health to spare. One death, one explosion, and the bystander loses W / 3 + E.
	// THE CASE: the ally holds E + W / 6, so the explosion leaves it W / 6 and its share of W / 3 kills it. That
	// death is the player's kill and the row hears it. The bystander must lose what it lost in the control.
	//
	// THE TWO OBSERVABLES, and why both. The overkill on each death notice is the figure the ruled line writes,
	// and the only thing the explosion is sized from. What the bystander loses is what a player would see.
	// WITHOUT THE RULED LINE the ally's notice carries the bonded creature's overkill, about 0.8 W, and the
	// bystander loses a second E.
	using namespace CataclysmBondKillsTest;

	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	struct FRead
	{
		bool bSetUp = false;
		int32 Deaths = 0;
		bool bAllyDied = false;
		bool bAllyKilledByThePlayer = false;
		float BondedOverkill = -1.0f;
		float AllyOverkill = -1.0f;
		float BystanderLost = 0.0f;
		float Whole = 0.0f;
		float ExplosionTakes = 0.0f;
	};

	const auto Run = [this](const TCHAR* Who, bool bAllyDies) -> FRead
	{
		FRead Read;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(*FString::Printf(TEXT("%s: set-up: a world"), Who), World))
		{
			return Read;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		ACataclysmPlayerCharacter* Player = Striker(World);
		ACataclysmEnemyCharacter* Bonded = Creature(World, FVector::ZeroVector, 100000.0f);
		ACataclysmEnemyCharacter* Ally = Creature(World, FVector(200.0f, 0.0f, 0.0f), 100000.0f);
		ACataclysmEnemyCharacter* Bystander = Creature(World, FVector(0.0f, 300.0f, 0.0f), 100000.0f);
		ACataclysmEnemyCharacter* Control = Creature(World, FVector(-3000.0f, 0.0f, 0.0f), 100000.0f);
		UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
		UCataclysmAbilitySystemComponent* PlayerSystem =
			Player ? Cast<UCataclysmAbilitySystemComponent>(Player->GetAbilitySystemComponent()) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s: set-up: a player"), Who), Player)
			|| !TestNotNull(*FString::Printf(TEXT("%s: set-up: the player's ability system"), Who), PlayerSystem)
			|| !TestNotNull(*FString::Printf(TEXT("%s: set-up: the announcements"), Who), Events)
			|| !TestNotNull(*FString::Printf(TEXT("%s: set-up: a bonded creature"), Who), Bonded)
			|| !TestNotNull(*FString::Printf(TEXT("%s: set-up: an ally"), Who), Ally)
			|| !TestNotNull(*FString::Printf(TEXT("%s: set-up: a bystander"), Who), Bystander)
			|| !TestNotNull(*FString::Printf(TEXT("%s: set-up: a creature with no bond"), Who), Control))
		{
			return Read;
		}
		Bonded->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));

		// THE BLOW, AND THEN THE EXPLOSION'S BLOW FOR THE OVERKILL IT WILL HAVE, both measured on the creature with
		// no bond before the row is worn. The delivery is the one `ExplodeForOverkill` builds.
		Read.Whole = WholeBlowOn(Player, Control);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: the blow takes health from a creature with no bond, and "
										   "under half of what it holds (%.2f)"), Who, Read.Whole),
					  Read.Whole > 10.0f && Read.Whole < 50000.0f)
			|| !TestEqual(*FString::Printf(TEXT("%s: set-up: the bond shares with its two allies"), Who),
						  UCataclysmEnemyModifiers::ShareOfDamageKept(Bonded), 1.0f / 3.0f, 0.001f))
		{
			return Read;
		}
		FCataclysmHitDelivery AsTheExplosion;
		AsTheExplosion.bIsArea = true;
		AsTheExplosion.bCannotBeRetaliatedAgainst = true;
		AsTheExplosion.bCannotCriticallyStrike = true;
		AsTheExplosion.bCarriesNoWeaponSubType = true;
		AsTheExplosion.bCannotLeech = true;
		AsTheExplosion.bIsConsequenceOfADeath = true;
		const float ControlBefore = HealthOf(Control);
		UCataclysmSkillEffects::ApplyDirectDamage(Player, Control, Read.Whole * 0.8f, AsTheExplosion);
		Read.ExplosionTakes = ControlBefore - HealthOf(Control);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: a blow of the overkill, delivered as the explosion's, takes "
										   "health (%.2f)"), Who, Read.ExplosionTakes),
					  Read.ExplosionTakes > 1.0f))
		{
			return Read;
		}

		// THE ROW, as the loader builds it: on `kill`, the value as the share.
		FCataclysmPoolAction Row;
		Row.Event = FName(TEXT("kill"));
		Row.Pool = FName(UCataclysmAbilitySystemComponent::ExplodeVictimForOverkillAction);
		Row.Percent = 100.0f;
		Row.bExplodeVictimForOverkill = true;
		Row.TriggerKey = FName(TEXT("Test:bond-overkill"));
		TArray<FCataclysmPoolAction> Rows;
		Rows.Add(Row);
		PlayerSystem->SetPoolActions(MoveTemp(Rows));

		PutHealthAt(Bonded, Read.Whole * 0.2f);
		if (bAllyDies)
		{
			PutHealthAt(Ally, Read.ExplosionTakes + Read.Whole / 6.0f);
		}
		const float BystanderBefore = HealthOf(Bystander);

		const FDelegateHandle Heard = Events->OnDeath.AddLambda(
			[&Read, Bonded, Ally, Player](const FCataclysmDeathNotice& Notice)
			{
				++Read.Deaths;
				if (Notice.Victim == Bonded)
				{
					Read.BondedOverkill = Notice.Overkill;
				}
				if (Notice.Victim == Ally)
				{
					Read.AllyOverkill = Notice.Overkill;
					Read.bAllyKilledByThePlayer = Notice.Killer == Player;
				}
			});

		// THE ONE BLOW.
		UCataclysmSkillEffects::ApplyDirectDamage(Player, Bonded, 1000.0f, FCataclysmHitDelivery());
		Events->OnDeath.Remove(Heard);

		Read.bAllyDied = HasDied(Ally);
		Read.BystanderLost = BystanderBefore - HealthOf(Bystander);
		Read.bSetUp = TestTrue(*FString::Printf(TEXT("%s: set-up: the blow killed the bonded creature"), Who),
							   HasDied(Bonded))
			&& TestFalse(*FString::Printf(TEXT("%s: set-up: the bystander, with health to spare, still stands"), Who),
						 HasDied(Bystander));
		return Read;
	};

	const FRead Spared = Run(TEXT("the ally lives"), false);
	const FRead Slain = Run(TEXT("the ally dies of its share"), true);
	if (!Spared.bSetUp || !Slain.bSetUp)
	{
		return false;
	}

	// THE CONTROL: ONE DEATH AND ONE EXPLOSION.
	TestEqual(TEXT("control: one creature dies"), Spared.Deaths, 1);
	TestFalse(TEXT("control: the ally lives"), Spared.bAllyDied);
	TestEqual(TEXT("control: the bonded creature's death records the whole blow less the health it held"),
			  Spared.BondedOverkill, Spared.Whole * 0.8f, 0.05f);
	TestEqual(TEXT("control: the bystander loses its third of the blow and what one explosion takes"),
			  Spared.BystanderLost, Spared.Whole / 3.0f + Spared.ExplosionTakes, 0.05f);

	// THE CASE: TWO DEATHS, AND STILL ONE EXPLOSION.
	TestEqual(TEXT("two creatures die: the bonded creature and its ally"), Slain.Deaths, 2);
	TestTrue(TEXT("the ally died"), Slain.bAllyDied);
	TestTrue(TEXT("and its death names the player as the killer, so the row hears it"), Slain.bAllyKilledByThePlayer);
	TestEqual(TEXT("the bonded creature's death records the same overkill as in the control"),
			  Slain.BondedOverkill, Spared.BondedOverkill, 0.05f);
	TestEqual(TEXT("the ally's death records an overkill of nought"), Slain.AllyOverkill, 0.0f, 0.001f);
	TestEqual(TEXT("the bystander loses what it lost in the control: the ally's death adds no explosion"),
			  Slain.BystanderLost, Spared.BystanderLost, 0.05f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
