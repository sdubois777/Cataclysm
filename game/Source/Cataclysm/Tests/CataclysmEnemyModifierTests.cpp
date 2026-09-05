// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Data/CataclysmDataRows.h"
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

#endif // WITH_AUTOMATION_TESTS
