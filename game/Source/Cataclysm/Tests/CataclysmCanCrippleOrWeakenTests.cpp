// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A bonus that holds while this character's attacks can Cripple or Weaken at
 * all. Issue #1718.
 *
 * WHAT THIS IS FOR. One node:
 *
 *   Ravager_basic_c_c0   Spreading Hurt
 *       "+4% increased Area of Effect per point for attacks that Cripple or
 *        Weaken."
 *
 * Eight points, so +32% at full investment, which is the figure every test here
 * looks for.
 *
 * WHY IT READS THE ATTACKER AND NOT THE BLOW. An area of effect is used to
 * SHAPE an attack before it lands, so "an attack that applied a Cripple" is not
 * knowable when the bonus is worked out. What is knowable is whether this
 * character's attacks are ones that cripple or weaken, and the two chance stats
 * are what say so. `docs/DECISIONS.md` carries the ruling and the reading it was
 * chosen over.
 *
 * NOTHING HERE FILLS THE READING ITSELF, which is the lesson
 * `CataclysmEnergyShieldFullTests.cpp` and `CataclysmStaggeredTargetTests.cpp`
 * both record: a set of tests that each filled the fact in themselves failed
 * nothing when the line filling it was broken. Every test below writes a chance
 * onto a real ability system's attributes and then asks `CurrentConditions()`,
 * so the fill in `UCataclysmAbilitySystemComponent` runs.
 *
 * FOUR STATES, AND EACH HAS ITS OWN TEST, because the node says "or" and the
 * three ways of satisfying an "or" are not one case:
 *
 *   cripple alone     the left half
 *   weaken alone      the right half
 *   both              pays once, not twice
 *   neither           pays nothing, which is every class but an invested
 *                     Ravager and every enemy in the game
 *
 * AND A FIFTH FOR AN ABILITY SYSTEM WITH NO COMBAT ATTRIBUTES, where the fill is
 * skipped entirely. That is a different route to the same answer, and it is the
 * one a test that only ever builds complete characters never walks.
 */
namespace CataclysmCanCrippleOrWeakenTest
{
	using Combat = UCataclysmCombatAttributeSet;

	/** Four per cent a point over the node's eight. */
	constexpr float SpreadingHurtAtEightPoints = 32.0f;

	/** A chance large enough to be unmistakably above zero. */
	constexpr float SomeChance = 5.0f;

	struct FAttacker
	{
		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/**
	 * An actor with a combat attribute set and nothing else.
	 *
	 * @param bWithCombatAttributes  false builds the case where the fill is
	 *                               skipped rather than the case where it runs
	 *                               and finds zeroes
	 */
	FAttacker MakeAttacker(UWorld* World, bool bWithCombatAttributes = true)
	{
		FAttacker Made;
		Made.Actor = World->SpawnActor<AActor>();
		if (!Made.Actor)
		{
			return Made;
		}

		Made.AbilitySystem =
			NewObject<UCataclysmAbilitySystemComponent>(Made.Actor);
		Made.AbilitySystem->RegisterComponent();
		if (bWithCombatAttributes)
		{
			// A raw pointer on purpose: `AddAttributeSetSubobject` is a template
			// and a TObjectPtr would deduce the wrapper rather than the set.
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Made.Actor);
			Made.AbilitySystem->AddAttributeSetSubobject(NewCombat);
		}
		Made.AbilitySystem->InitAbilityActorInfo(Made.Actor, Made.Actor);
		return Made;
	}

	void SetChances(const FAttacker& Who, float Cripple, float Weaken)
	{
		Who.AbilitySystem->SetNumericAttributeBase(
			Combat::GetCrippleChanceAttribute(), Cripple);
		Who.AbilitySystem->SetNumericAttributeBase(
			Combat::GetWeakenChanceAttribute(), Weaken);
	}

	/** What the ability system reports, rather than what the test asked for. */
	float CrippleChanceOf(const FAttacker& Who)
	{
		return Who.AbilitySystem->GetNumericAttribute(
			Combat::GetCrippleChanceAttribute());
	}

	float WeakenChanceOf(const FAttacker& Who)
	{
		return Who.AbilitySystem->GetNumericAttribute(
			Combat::GetWeakenChanceAttribute());
	}

	/**
	 * What Spreading Hurt's row is worth to this character right now.
	 *
	 * ONE ROW THROUGH THE REAL PIPELINE rather than a call to `ConditionHolds`.
	 * The condition being right is not the same claim as the condition reaching
	 * a modifier, and the second is what a node does.
	 */
	float SpreadingHurtIncrease(const FAttacker& Who)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Increased;
		Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
		Modifier.Value = SpreadingHurtAtEightPoints;
		Modifier.Condition = ECataclysmStatCondition::CanCrippleOrWeaken;

		// NO CONDITION VALUE. The condition names its ailments rather than
		// comparing a number, and the generator refuses a value beside it.
		Modifier.ConditionValue = 0.0f;

		const TArray<FCataclysmStatModifier> Modifiers{ Modifier };
		return UCataclysmStatPipeline::Evaluate(
			/*Base=*/100.0f, Modifiers, FGameplayTagContainer(),
			Who.AbilitySystem->CurrentConditions()).SumOfIncreases;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// The left half of the "or"
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmCrippleChanceAloneGrantsTest,
	"Cataclysm.CanCrippleOrWeaken.AChanceToCrippleAloneGrantsSpreadingHurt")
{
	using namespace CataclysmCanCrippleOrWeakenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FAttacker Who = MakeAttacker(World);
	SetChances(Who, /*Cripple=*/SomeChance, /*Weaken=*/0.0f);

	// THE STATE THE TEST MEANT TO BUILD, ASSERTED BEFORE THE BEHAVIOUR. A write
	// the attribute set clamped somewhere unexpected would otherwise make this
	// pass or fail for a reason that has nothing to do with the predicate.
	TestEqual(TEXT("the chance to cripple is above zero"),
			  CrippleChanceOf(Who), SomeChance);
	TestEqual(TEXT("and the chance to weaken is not"), WeakenChanceOf(Who), 0.0f);

	TestEqual(TEXT("Spreading Hurt is worth its full thirty-two per cent"),
			  SpreadingHurtIncrease(Who), SpreadingHurtAtEightPoints);
	return true;
}

// --------------------------------------------------------------------------
// The right half, which a build reading only the first stat would fail
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmWeakenChanceAloneGrantsTest,
	"Cataclysm.CanCrippleOrWeaken.AChanceToWeakenAloneGrantsSpreadingHurt")
{
	using namespace CataclysmCanCrippleOrWeakenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FAttacker Who = MakeAttacker(World);
	SetChances(Who, /*Cripple=*/0.0f, /*Weaken=*/SomeChance);

	TestEqual(TEXT("the chance to cripple is zero"), CrippleChanceOf(Who), 0.0f);
	TestEqual(TEXT("and the chance to weaken is above it"),
			  WeakenChanceOf(Who), SomeChance);

	TestEqual(TEXT("Spreading Hurt is worth its full thirty-two per cent"),
			  SpreadingHurtIncrease(Who), SpreadingHurtAtEightPoints);
	return true;
}

// --------------------------------------------------------------------------
// Both, which pays once
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmBothChancesPayOnceTest,
	"Cataclysm.CanCrippleOrWeaken.BothChancesTogetherPayOnceAndNotTwice")
{
	using namespace CataclysmCanCrippleOrWeakenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FAttacker Who = MakeAttacker(World);
	SetChances(Who, /*Cripple=*/SomeChance, /*Weaken=*/SomeChance);

	TestTrue(TEXT("both chances are above zero"),
			 CrippleChanceOf(Who) > 0.0f && WeakenChanceOf(Who) > 0.0f);

	// THE CONDITION IS ONE FACT, NOT TWO. A build that read the two stats as
	// separate reasons to pay would give sixty-four here. The node says "Cripple
	// or Weaken", which is one bonus whichever of them is true.
	TestEqual(TEXT("Spreading Hurt is still worth thirty-two and not sixty-four"),
			  SpreadingHurtIncrease(Who), SpreadingHurtAtEightPoints);
	return true;
}

// --------------------------------------------------------------------------
// Neither, which is every class but an invested Ravager
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmNeitherChanceGrantsNothingTest,
	"Cataclysm.CanCrippleOrWeaken.ACharacterWithNeitherChanceGrantsNothing")
{
	using namespace CataclysmCanCrippleOrWeakenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FAttacker Who = MakeAttacker(World);
	SetChances(Who, /*Cripple=*/0.0f, /*Weaken=*/0.0f);

	TestEqual(TEXT("neither chance is above zero"),
			  CrippleChanceOf(Who) + WeakenChanceOf(Who), 0.0f);

	// THE HALF A BUILD IGNORING THE CONDITION WOULD FAIL. Without this the three
	// tests above would pass against a modifier that applied unconditionally.
	TestEqual(TEXT("Spreading Hurt is worth nothing at all"),
			  SpreadingHurtIncrease(Who), 0.0f);
	return true;
}

// --------------------------------------------------------------------------
// And the route where the reading is never taken
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmNoCombatAttributesGrantsNothingTest,
	"Cataclysm.CanCrippleOrWeaken.AnAbilitySystemWithNoCombatAttributesGrantsNothing")
{
	using namespace CataclysmCanCrippleOrWeakenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// NO COMBAT ATTRIBUTE SET, so `CurrentConditions` skips the fill entirely
	// rather than running it and finding zeroes. Both answer false, and this is
	// the one a test that only builds complete characters never reaches.
	const FAttacker Who = MakeAttacker(World, /*bWithCombatAttributes=*/false);

	TestEqual(TEXT("Spreading Hurt is worth nothing at all"),
			  SpreadingHurtIncrease(Who), 0.0f);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_AUTOMATION_TESTS
