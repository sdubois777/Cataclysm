// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A minion's own health and damage, from its type row. Issue #340.
 *
 * WHAT THESE ARE FOR. The decision of 2026-08-06, issue #209, reversed the rule
 * this code carried: "a minion reaches its summoner through three channels and
 * nothing else: its side, its base health and damage raised by the summoner's
 * level, and increased damage from one primary attribute declared per minion
 * type." The engine went on dealing 30% of the summoner's weapon damage and
 * setting no health at all, while `game/Data/MinionTypes.csv` carried a base and
 * a per-level figure for both, read by nothing.
 *
 * WHY THIS FILE EXISTS RATHER THAN CHANGES TO THE TESTS THAT WERE THERE. Three
 * tests assert the old share, and all three summon a minion with NO TYPE NAME --
 * which still takes it, deliberately, because they predate the type table and
 * exist to check other things. **So the new behaviour had no coverage at all**,
 * and a build where a minion's damage was zero, its health unset and the level
 * read as one would have passed the whole suite.
 *
 * EVERY FIGURE BELOW IS COMPUTED HERE RATHER THAN ASKED OF THE CODE. The engine
 * has a helper for "a base raised by a level" and these tests deliberately do
 * not use it: a test that asks the thing under test to compute the expected
 * answer agrees with it however wrong it is.
 *
 * THE PRECONDITION IS ASSERTED FIRST IN EVERY CASE THAT NEEDS A TYPE. A minion
 * whose row could not be found spawns carrying the defaults and falls back to
 * the old share, so a stale imported asset would turn "it deals its own damage"
 * into a confusing failure rather than a clear one.
 * `Cataclysm.AI.AGadgetStaysWhereItIsPutAndACreatureFollows` reads the same
 * precondition the same way and says the same thing about it.
 */
namespace CataclysmMinionOwnStatsTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	/** Centimetres in a metre, so a case can place a character in metres. */
	constexpr float M = 100.0f;

	//~ The authored figures, from `game/Data/MinionTypes.csv`. Written out rather
	//~ than read back through the engine, for the reason the file comment gives.
	//~ `Cataclysm.DataTable` pins the table itself, so a row changed underneath
	//~ these is caught there and named, rather than making these fail obscurely.
	constexpr float ImpBaseDamage = 10.5f;
	constexpr float ImpDamagePerLevel = 10.5f;
	constexpr float ImpBaseHealth = 200.0f;
	constexpr float ImpHealthPerLevel = 90.0f;

	constexpr float BallistaBaseDamage = 63.0f;
	constexpr float BallistaDamagePerLevel = 63.0f;

	/**
	 * A base figure raised by a level, worked out here.
	 *
	 * THE ARITHMETIC IS DUPLICATED ON PURPOSE. The engine's own helper is in an
	 * unnamed namespace and could not be called from here even if it should be,
	 * which is the right shape: these tests have to state the answer
	 * independently or they cannot disagree with the code.
	 */
	float RaisedByLevel(float Base, float PerLevel, int32 Level)
	{
		return Base + PerLevel * static_cast<float>(Level);
	}

	/**
	 * The level these tests will see.
	 *
	 * NOT A GUESS AND NOT A CONSTANT. A summoner built here is an ordinary actor
	 * with no player state, so the minion falls back to the level the class stats
	 * are being previewed at -- which is the path EVERY non-player summoner takes
	 * in the game, not a test-only corner.
	 */
	int32 LevelTheseTestsSee()
	{
		return UCataclysmPlayerClassStats::ChosenLevel();
	}

	/**
	 * A bare actor carrying the attributes a blow needs at both ends.
	 *
	 * THE FOUR SETS ARE COPIED FROM A FIXTURE THAT IS KNOWN TO WORK.
	 * `CataclysmDamageTypeTests.cpp` builds `FScopedCombatant` with exactly these
	 * and drives `AttackTarget` at it, reading the whole figure that was swung, so
	 * nothing here mitigates a blow by default. Two earlier drafts of this file
	 * carried only the vital and combat sets; the resistance sets are here to keep
	 * the difference from that fixture at zero rather than because a reading
	 * needed them.
	 *
	 * NO CRITICAL STRIKE PIN, AND THAT IS DELIBERATE. `FScopedCombatant`'s tests
	 * need `FScopedNoCriticalStrikes` because they make the SUMMONER swing as
	 * well, and that blow can roll one. A minion's cannot: `MinionDelivery` sets
	 * `bCannotCriticallyStrike`, on both the flat and the percentage path, and
	 * `Cataclysm.AI.ASummonedImpNeverCriticallyStrikesEvenWhenItsSummonerWould`
	 * is what holds that. Nothing below makes the summoner swing, so there is no
	 * roll here to pin.
	 */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, float AttackDamage)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and
			// a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAllResist =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAllResist);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			// LARGE ENOUGH THAT NOTHING HERE APPROACHES DEATH, so a reading is
			// the blow rather than the health that was left.
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetMaxHealthAttribute(), 1'000'000.0f);
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetHealthAttribute(), 1'000'000.0f);
			AbilitySystem->SetNumericAttributeBase(
				Combat::GetAttackDamageAttribute(), AttackDamage);
		}

		~FScopedFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		float Health() const
		{
			const UAbilitySystemComponent* System =
				UCataclysmTargeting::AbilitySystemOf(Actor);
			return System
				? System->GetNumericAttribute(Vital::GetHealthAttribute())
				: 0.0f;
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A minion's current health, read the way anything else reads a character's. */
	float HealthOf(const ACataclysmMinion* Minion)
	{
		const UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Minion);
		return System
			? System->GetNumericAttribute(Vital::GetHealthAttribute())
			: -1.0f;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionOwnDamageTest,
	"Cataclysm.MinionStats.AMinionDealsItsOwnDamageAndTheSummonersIncreasesDoNotReachIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The blow is the minion's own figure, and it stays that figure when the
 * summoner is made much stronger.
 *
 * ONE TEST WITH TWO MEASUREMENTS RATHER THAN TWO TESTS, and the reason is the
 * whole point. "The summoner's increases do not reach it" passes on its own if
 * the minion deals NOTHING AT ALL -- it cannot tell a bonus correctly excluded
 * from a feature that never fired. Another session lost a cycle tonight to
 * exactly that: three "does not happen" tests all passed because the thing under
 * test was doing nothing whatever.
 *
 * So the first measurement is a specific number, not "more than zero", and the
 * second is the SAME blow with one change. A build where a minion deals nothing
 * fails the first half and never reaches the second.
 *
 * THE SUMMONER'S WEAPON IS 1000, WHICH IS CHOSEN BY WHAT IT CAN DISTINGUISH.
 * The old rule gave 30% of it, so a share would read 300. An imp's own damage at
 * the levels these tests run at is nowhere near that, so the two answers cannot
 * be confused by an arithmetic coincidence.
 */
bool FCataclysmMinionOwnDamageTest::RunTest(const FString&)
{
	using namespace CataclysmMinionOwnStatsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Summoner(World, /*AttackDamage=*/1000.0f);
	FScopedFighter Target(World, /*AttackDamage=*/0.0f);

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(1 * M, 0, 0), /*Lifetime=*/20.0f,
		/*bBurns=*/false, TEXT("Imp"));
	if (!TestNotNull(TEXT("an imp"), Imp))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	// THE PRECONDITION, FIRST, BECAUSE THE TEST SAYS NOTHING WITHOUT IT. A minion
	// whose row could not be found keeps the defaults AND falls back to the old
	// share, so a stale imported asset would make the readings below fail for a
	// reason that has nothing to do with this rule.
	if (!TestEqual(TEXT("the imp was made from the Imp row"),
				   Imp->TypeName, FString(TEXT("Imp"))))
	{
		AddError(TEXT("DT_MinionTypes could not supply the Imp row, so this test "
					  "cannot tell its own damage from the old share. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	const int32 Level = LevelTheseTestsSee();
	const float ItsOwn = RaisedByLevel(ImpBaseDamage, ImpDamagePerLevel, Level);

	const float Before = Target.Health();
	Imp->AttackTarget(Target.Actor);
	const float Plain = Before - Target.Health();

	TestEqual(TEXT("it deals its own damage from its type row"), Plain, ItsOwn,
			  0.01f);

	// AND NOT A SHARE OF THE SUMMONER'S WEAPON, which is what it dealt until the
	// design of 2026-08-06 was implemented. Stated as its own reading so the
	// failure names the rule rather than a number.
	TestNotEqual(TEXT("and not thirty per cent of the summoner's weapon"),
				 FMath::RoundToInt(Plain), FMath::RoundToInt(1000.0f * 0.3f));

	// NOW MAKE THE SUMMONER MUCH STRONGER AND MEASURE THE SAME BLOW. This is the
	// half that the flat damage path exists for: `ApplyHit` would run the
	// summoner's own stat modifiers over the figure, and this one takes it as
	// given. A doubling that reached the minion would be visible at once.
	{
		FCataclysmStatModifier Doubling;
		Doubling.Bucket = ECataclysmStatBucket::Increased;
		Doubling.Source = ECataclysmModifierSource::PassiveKeystone;
		Doubling.Value = 100.0f;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line =
			Inputs.FindOrAdd(FName(TEXT("attack_damage")));
		Line.Base = 1000.0f;
		Line.Modifiers = {Doubling};
		Summoner.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	}

	const float BeforeBoosted = Target.Health();
	Imp->AttackTarget(Target.Actor);
	const float Boosted = BeforeBoosted - Target.Health();

	TestEqual(TEXT("and doubling the summoner's attack damage changes nothing"),
			  Boosted, Plain, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionOwnHealthTest,
	"Cataclysm.MinionStats.AMinionHasItsOwnHealthFromItsTypeRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A minion has health, which nothing in this game has ever asserted.
 *
 * IT HAD NONE AT ALL UNTIL THIS CHANGE. `DeployedHealthPercent` was recorded at
 * spawn and read by nothing anywhere in the module, and the comment beside it
 * said health was "deliberately not set from the type" because applying it
 * needed the summoner's level, "which nothing in this module can read yet".
 * Something in the same module reads it: `UCataclysmEquipmentComponent` does,
 * with the fallback a minion needs for a summoner that is not a player.
 *
 * THE FIGURE IS SPECIFIC, NOT "MORE THAN ZERO". A minion left at whatever the
 * attribute set defaults to would pass a positive check and fail this one.
 */
bool FCataclysmMinionOwnHealthTest::RunTest(const FString&)
{
	using namespace CataclysmMinionOwnStatsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Summoner(World, /*AttackDamage=*/100.0f);

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(1 * M, 0, 0), /*Lifetime=*/20.0f,
		/*bBurns=*/false, TEXT("Imp"));
	if (!TestNotNull(TEXT("an imp"), Imp))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	if (!TestEqual(TEXT("the imp was made from the Imp row"),
				   Imp->TypeName, FString(TEXT("Imp"))))
	{
		AddError(TEXT("DT_MinionTypes could not supply the Imp row. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	const float Expected =
		RaisedByLevel(ImpBaseHealth, ImpHealthPerLevel, LevelTheseTestsSee());

	TestEqual(TEXT("an imp has its type's own health"), HealthOf(Imp), Expected,
			  0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionTypesDifferTest,
	"Cataclysm.MinionStats.TwoMinionTypesDoNotShareOneSetOfNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A ballista and an imp hit for different amounts, and each for its own row's.
 *
 * A SINGLE-TYPE TEST CANNOT SEE THIS. A build that read one row and gave it to
 * every minion would pass every case above, because every case above summons an
 * imp. The ballista is the control on the lookup itself.
 *
 * THE TWO ROWS DIFFER BY SIX TIMES, which is chosen by what it can distinguish:
 * a wrong row cannot be mistaken for rounding.
 */
bool FCataclysmMinionTypesDifferTest::RunTest(const FString&)
{
	using namespace CataclysmMinionOwnStatsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Summoner(World, /*AttackDamage=*/1000.0f);
	FScopedFighter First(World, /*AttackDamage=*/0.0f);
	FScopedFighter Second(World, /*AttackDamage=*/0.0f);

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(1 * M, 0, 0), /*Lifetime=*/20.0f,
		/*bBurns=*/false, TEXT("Imp"));
	ACataclysmMinion* Ballista = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(0, 1 * M, 0), /*Lifetime=*/20.0f,
		/*bBurns=*/false, TEXT("Ballista"));
	if (!TestNotNull(TEXT("an imp"), Imp)
		|| !TestNotNull(TEXT("a ballista"), Ballista))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };
	ON_SCOPE_EXIT { if (IsValid(Ballista)) { Ballista->Destroy(); } };

	if (!TestEqual(TEXT("the imp came from the Imp row"),
				   Imp->TypeName, FString(TEXT("Imp")))
		|| !TestEqual(TEXT("and the ballista from the Ballista row"),
					  Ballista->TypeName, FString(TEXT("Ballista"))))
	{
		AddError(TEXT("DT_MinionTypes could not supply both rows, so this test "
					  "cannot tell one type's numbers from another's. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	const int32 Level = LevelTheseTestsSee();

	const float BeforeImp = First.Health();
	Imp->AttackTarget(First.Actor);
	const float ImpDealt = BeforeImp - First.Health();

	const float BeforeBallista = Second.Health();
	Ballista->AttackTarget(Second.Actor);
	const float BallistaDealt = BeforeBallista - Second.Health();

	TestEqual(TEXT("the imp deals the Imp row's figure"), ImpDealt,
			  RaisedByLevel(ImpBaseDamage, ImpDamagePerLevel, Level), 0.01f);
	TestEqual(TEXT("and the ballista the Ballista row's"), BallistaDealt,
			  RaisedByLevel(BallistaBaseDamage, BallistaDamagePerLevel, Level),
			  0.01f);
	TestTrue(*FString::Printf(
				 TEXT("which are different figures: %.1f against %.1f"),
				 BallistaDealt, ImpDealt),
			 BallistaDealt > ImpDealt);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTypelessMinionTest,
	"Cataclysm.MinionStats.AMinionWithNoTypeStillTakesAShareOfItsSummoners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The fallback, pinned rather than left incidental.
 *
 * WHY IT EXISTS AT ALL. Both callers in the game name a type. A minion without
 * one is a test fixture: three tests that predate the type table summon one and
 * assert the old share, and they exist to check other things -- that an imp
 * never turns on its summoner, that it cannot take the summoner's critical
 * strike, that a burning minion sets what it hits alight. Forcing them into the
 * new model would have damaged tests that are about none of this.
 *
 * SO THE SEAM IS DELIBERATE AND IT IS ASSERTED HERE. Without this, a later
 * change could remove the fallback and those three would fail with no statement
 * anywhere of what the fallback was for.
 */
bool FCataclysmTypelessMinionTest::RunTest(const FString&)
{
	using namespace CataclysmMinionOwnStatsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedFighter Summoner(World, /*AttackDamage=*/100.0f);
	FScopedFighter Target(World, /*AttackDamage=*/0.0f);

	ACataclysmMinion* Nameless = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(1 * M, 0, 0), /*Lifetime=*/20.0f,
		/*bBurns=*/false);
	if (!TestNotNull(TEXT("a minion with no type"), Nameless))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Nameless)) { Nameless->Destroy(); } };

	TestEqual(TEXT("it carries no type name"), Nameless->TypeName, FString());

	const float Before = Target.Health();
	Nameless->AttackTarget(Target.Actor);
	const float Dealt = Before - Target.Health();

	TestEqual(TEXT("and deals the old share of its summoner's weapon"), Dealt,
			  100.0f * ACataclysmMinion::DamagePercentOfSummoner / 100.0f, 0.01f);

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
