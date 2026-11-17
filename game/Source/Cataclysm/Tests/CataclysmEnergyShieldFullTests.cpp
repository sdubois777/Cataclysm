// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A bonus that holds while the character's energy shield is at the top of its
 * bar. Issue #1515.
 *
 * WHAT THIS IS FOR. One node:
 *
 *   Ritualist_basic_c_a2   Cold Reading
 *       "+2% increased Spell Damage per point while your Energy Shield is full."
 *
 * Eight points, so +16% at full investment, which is the figure every test here
 * looks for.
 *
 * NOTHING HERE FILLS THE READING ITSELF. Every test writes the shield onto a
 * real ability system's attributes and then asks `CurrentConditions()`, so the
 * fill in `UCataclysmAbilitySystemComponent` runs. That is the lesson
 * `CataclysmStaggeredTargetTests.cpp` records in its own words: a set of tests
 * that each filled the fact in themselves failed nothing when the line filling
 * it was broken, three times running.
 *
 * THE PREDICATE HAS THREE CLAUSES AND EACH HAS ITS OWN TEST, because two of
 * them refuse states that look identical in a summary and are not:
 *
 *   the reading is unknown   no vital attribute set at all, so the fill is
 *                            SKIPPED and the readings stay at -1
 *   there is no bar          a maximum of zero, which is every class but the
 *                            Ritualist and every enemy
 *   the bar is not full      a real maximum with less than that in hand
 *
 * A summary that said "a character with no shield grants nothing" would cover
 * the first two with one sentence and one test, and either clause could then be
 * deleted with the suite still green.
 *
 * THE MAXIMUM IS WRITTEN BEFORE THE SHIELD, because the shield is clamped to it
 * and the other order silently leaves the shield at zero.
 * `CataclysmTargetHealthTests.cpp` records the same ordering for health.
 */
namespace CataclysmShieldFullTest
{
	/** The Ritualist's own maximum energy shield at level one. See ClassStats.csv. */
	constexpr float ShieldMax = 40.0f;

	/** Cold Reading at all eight points: eight times two per cent. */
	constexpr float ColdReadingAtEightPoints = 16.0f;

	/**
	 * An actor holding an ability system and the vital attributes, under this
	 * file's own names so a unity build does not see two of them.
	 */
	struct FShieldBearer
	{
		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * An ability system with a vital attribute set, or without one.
	 *
	 * @param bWithVitals false builds the ability system a character sheet has
	 *        before its attributes exist, and every enemy built without them.
	 *        That is what leaves both readings unknown, and it is a different
	 *        state from a shield of zero.
	 */
	FShieldBearer MakeShieldBearer(UWorld* World, bool bWithVitals = true)
	{
		FShieldBearer Made;
		Made.Actor = World->SpawnActor<AActor>();
		if (!Made.Actor)
		{
			return Made;
		}

		Made.AbilitySystem =
			NewObject<UCataclysmAbilitySystemComponent>(Made.Actor);
		Made.AbilitySystem->RegisterComponent();
		if (bWithVitals)
		{
			// OWNED BY THE ACTOR, NOT BARE. A component made with no owner
			// crashes the whole run the moment a test writes an attribute.
			Made.AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmVitalAttributeSet>(Made.Actor));
		}
		Made.AbilitySystem->InitAbilityActorInfo(Made.Actor, Made.Actor);
		return Made;
	}

	/** Write the top of the bar, then how much is in it. In that order. */
	void SetShield(const FShieldBearer& Bearer, float Maximum, float Held)
	{
		Bearer.AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), Maximum);
		Bearer.AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), Held);
	}

	/** What the ability system reports, rather than what the test asked for. */
	float ShieldHeld(const FShieldBearer& Bearer)
	{
		return Bearer.AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
	}

	/** The top of the bar, as the ability system reports it. */
	float ShieldMaximumOf(const FShieldBearer& Bearer)
	{
		return Bearer.AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute());
	}

	/**
	 * What Cold Reading's row is worth to this character right now.
	 *
	 * ONE ROW THROUGH THE REAL PIPELINE rather than a call to `ConditionHolds`.
	 * The condition being right is not the same claim as the condition reaching
	 * a modifier, and the second is what a node does.
	 */
	float ColdReadingIncrease(const FShieldBearer& Bearer)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Increased;
		Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
		Modifier.Value = ColdReadingAtEightPoints;
		Modifier.Condition = ECataclysmStatCondition::EnergyShieldAtMaximum;

		// NO CONDITION VALUE. "Full" names the top of the bar rather than a
		// number, and the generator refuses a value on a row carrying this.
		Modifier.ConditionValue = 0.0f;

		const TArray<FCataclysmStatModifier> Modifiers{ Modifier };
		return UCataclysmStatPipeline::Evaluate(
			/*Base=*/100.0f, Modifiers, FGameplayTagContainer(),
			Bearer.AbilitySystem->CurrentConditions()).SumOfIncreases;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// The bonus holds while the shield is full
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmShieldFullGrantsTest,
	"Cataclysm.EnergyShield.AFullEnergyShieldGrantsColdReading")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const CataclysmShieldFullTest::FShieldBearer Bearer =
		CataclysmShieldFullTest::MakeShieldBearer(World);
	CataclysmShieldFullTest::SetShield(Bearer,
		CataclysmShieldFullTest::ShieldMax,
		CataclysmShieldFullTest::ShieldMax);

	// THE STATE THE TEST MEANT TO BUILD, ASSERTED BEFORE THE BEHAVIOUR. A write
	// that the attribute set clamped somewhere unexpected would otherwise make
	// this test pass or fail for a reason that has nothing to do with the
	// predicate.
	TestEqual(TEXT("the shield is at the top of its bar"),
		CataclysmShieldFullTest::ShieldHeld(Bearer),
		CataclysmShieldFullTest::ShieldMax);

	TestEqual(TEXT("Cold Reading is worth its full sixteen per cent"),
		CataclysmShieldFullTest::ColdReadingIncrease(Bearer),
		CataclysmShieldFullTest::ColdReadingAtEightPoints);
	return true;
}

// --------------------------------------------------------------------------
// The third clause: a real bar with less than the top of it in hand
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmShieldPartialGrantsNothingTest,
	"Cataclysm.EnergyShield.APartialEnergyShieldGrantsNothing")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const CataclysmShieldFullTest::FShieldBearer Bearer =
		CataclysmShieldFullTest::MakeShieldBearer(World);
	CataclysmShieldFullTest::SetShield(Bearer,
		CataclysmShieldFullTest::ShieldMax,
		CataclysmShieldFullTest::ShieldMax - 1.0f);

	TestEqual(TEXT("the shield is one short of the top"),
		CataclysmShieldFullTest::ShieldHeld(Bearer),
		CataclysmShieldFullTest::ShieldMax - 1.0f);

	TestEqual(TEXT("Cold Reading grants nothing one point short"),
		CataclysmShieldFullTest::ColdReadingIncrease(Bearer), 0.0f);
	return true;
}

CATACLYSM_TEST(FCataclysmShieldBrokenGrantsNothingTest,
	"Cataclysm.EnergyShield.ABrokenEnergyShieldGrantsNothing")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// ZERO OF A REAL MAXIMUM, WHICH IS THE ORDINARY CASE THIS NODE ANSWERS "NO"
	// FOR. It is also the case a predicate written as `Held >= Maximum` with no
	// other clause would get RIGHT, which is why it does not stand in for the
	// test below.
	const CataclysmShieldFullTest::FShieldBearer Bearer =
		CataclysmShieldFullTest::MakeShieldBearer(World);
	CataclysmShieldFullTest::SetShield(Bearer,
		CataclysmShieldFullTest::ShieldMax, 0.0f);

	TestEqual(TEXT("the bar is real"),
		CataclysmShieldFullTest::ShieldMaximumOf(Bearer),
		CataclysmShieldFullTest::ShieldMax);
	TestEqual(TEXT("and it is empty"),
		CataclysmShieldFullTest::ShieldHeld(Bearer), 0.0f);

	TestEqual(TEXT("Cold Reading grants nothing on a broken shield"),
		CataclysmShieldFullTest::ColdReadingIncrease(Bearer), 0.0f);
	return true;
}

// --------------------------------------------------------------------------
// The middle clause: a bar that cannot hold anything
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmShieldNoBarGrantsNothingTest,
	"Cataclysm.EnergyShield.AClassWithNoEnergyShieldGrantsNothing")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// ZERO OF ZERO, WHICH IS EVERY CLASS BUT THE RITUALIST AND EVERY ENEMY.
	// `game/Data/ClassStats.csv` gives `max_energy_shield` to the Ritualist
	// alone, so this is the common state rather than a corner. `Held >= Maximum`
	// alone answers YES here, which is what the middle clause exists to refuse.
	const CataclysmShieldFullTest::FShieldBearer Bearer =
		CataclysmShieldFullTest::MakeShieldBearer(World);
	CataclysmShieldFullTest::SetShield(Bearer, 0.0f, 0.0f);

	TestEqual(TEXT("there is no bar"),
		CataclysmShieldFullTest::ShieldMaximumOf(Bearer), 0.0f);
	TestEqual(TEXT("and nothing in it"),
		CataclysmShieldFullTest::ShieldHeld(Bearer), 0.0f);

	TestEqual(TEXT("Cold Reading grants nothing to a character with no shield"),
		CataclysmShieldFullTest::ColdReadingIncrease(Bearer), 0.0f);
	return true;
}

// --------------------------------------------------------------------------
// The first clause: nothing to read at all
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmShieldUnknownGrantsNothingTest,
	"Cataclysm.EnergyShield.AnAbilitySystemWithNoVitalAttributesGrantsNothing")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// NO VITAL ATTRIBUTE SET, SO THE FILL NEVER RUNS AND BOTH READINGS STAY
	// NEGATIVE. This is a DIFFERENT refusal from the test above: that one is
	// refused by the maximum being zero, this one by the reading being unknown.
	// Deleting either clause leaves the other test passing.
	const CataclysmShieldFullTest::FShieldBearer Bearer =
		CataclysmShieldFullTest::MakeShieldBearer(World, /*bWithVitals=*/false);

	const FCataclysmStatConditions State =
		Bearer.AbilitySystem->CurrentConditions();
	TestTrue(TEXT("the shield reading is unknown rather than zero"),
		State.EnergyShieldHeld < 0.0f);
	TestTrue(TEXT("and so is the top of the bar"),
		State.EnergyShieldMaximum < 0.0f);

	TestEqual(TEXT("Cold Reading grants nothing when nothing can be read"),
		CataclysmShieldFullTest::ColdReadingIncrease(Bearer), 0.0f);
	return true;
}

// --------------------------------------------------------------------------
// Greater-or-equal rather than equal
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmShieldOverMaximumIsFullTest,
	"Cataclysm.EnergyShield.AShieldAboveItsMaximumStillCountsAsFull")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// REACHED THE WAY THE GAME REACHES IT, NOT BY WRITING A RAW NUMBER.
	// `UCataclysmVitalAttributeSet` clamps the shield whenever the SHIELD
	// changes and never when the MAXIMUM changes, and it carries no
	// proportional adjustment. So raising the maximum, filling the shield, and
	// lowering the maximum again leaves the character holding more shield than
	// its bar now has -- which is what any item or effect granting maximum
	// energy shield produces when it ends.
	const CataclysmShieldFullTest::FShieldBearer Bearer =
		CataclysmShieldFullTest::MakeShieldBearer(World);
	CataclysmShieldFullTest::SetShield(Bearer, 100.0f, 100.0f);
	Bearer.AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(),
		CataclysmShieldFullTest::ShieldMax);

	// THE STATE IS ASSERTED RATHER THAN ASSUMED. If a future clamp were added on
	// the maximum changing, this assertion fails and says so, instead of the
	// test below quietly proving nothing.
	TestTrue(TEXT("the shield really is above its maximum"),
		CataclysmShieldFullTest::ShieldHeld(Bearer)
			> CataclysmShieldFullTest::ShieldMaximumOf(Bearer));

	TestEqual(TEXT("a shield over the top of its bar is full"),
		CataclysmShieldFullTest::ColdReadingIncrease(Bearer),
		CataclysmShieldFullTest::ColdReadingAtEightPoints);
	return true;
}

#undef CATACLYSM_TEST

#endif  // WITH_AUTOMATION_TESTS
