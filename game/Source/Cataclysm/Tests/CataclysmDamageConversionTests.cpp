// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmDamageConversion.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"

/**
 * Damage arriving as Bleeding instead of all at once, for the Masochist's
 * The Breaking Point.
 *
 *   Dropping below 50% health converts all damage you take into Bleeding over
 *   5 seconds. The conversion lasts 3 seconds, increased by 5% per point, and
 *   cannot happen more than once every 10 seconds.
 *
 * WHAT IS MEASURED HERE AND WHAT IS NOT. These drive
 * `UCataclysmDamageConversion` directly, because what the node is made of is a
 * crossing, a window and a cooldown, and all three are arithmetic on a clock.
 * That the DAMAGE PATH really calls it -- that a blow which would have reached
 * health does not -- is proved separately, by breaking the call site and
 * watching a test fail.
 *
 * A PLAIN ACTOR AND NOT A CHARACTER, the same choice
 * `CataclysmHealthDebtTests.cpp` makes and for the same reason: a character
 * brings a movement component, a regeneration timer and a death path with it,
 * and none of that is what these are about.
 *
 * THE CLOCK IS MOVED BY HAND. A world built by `UWorld::CreateWorld` is never
 * ticked, so `World->TimeSeconds` stays where it is until a test moves it. That
 * is what makes a window and a cooldown testable at all, and it is the same
 * thing the health debt tests do.
 *
 * Issue #985.
 */
namespace CataclysmDamageConversionTest
{
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;
	using Conversion = UCataclysmDamageConversion;

	/** A character that can bleed itself: an ability system and two sets. */
	struct FScopedBleeder
	{
		explicit FScopedBleeder(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewResource);

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(Vital::GetMaxHealthAttribute(), 1'000.0f);
			Set(Vital::GetHealthAttribute(), 1'000.0f);
		}

		~FScopedBleeder()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		/** Spend points in The Breaking Point: the flag on, and a window. */
		void TakeTheNode(float WindowSeconds = Conversion::BaseWindowSeconds) const
		{
			Set(Resource::GetDamageToBleedingOnLowHealthAttribute(), 1.0f);
			Set(Resource::GetDamageToBleedingWindowAttribute(), WindowSeconds);
		}

		/** Put health at a share of maximum and tell the rule it moved. */
		void MoveHealthTo(float Share) const
		{
			Set(Vital::GetHealthAttribute(), 1'000.0f * Share);
			Conversion::NoteHealthChanged(Actor);
		}

		bool IsConverting() const
		{
			return AbilitySystem->IsConvertingDamageToBleeding();
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

#define CATACLYSM_CONVERSION_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The trigger
// ---------------------------------------------------------------------------

CATACLYSM_CONVERSION_TEST(FCataclysmCrossingBelowHalfOpensAWindowTest,
	"Cataclysm.DamageConversion.DroppingBelowHalfHealthOpensAWindow")
{
	using namespace CataclysmDamageConversionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		Masochist.TakeTheNode();

		TestFalse(TEXT("nothing is converting at full health"),
			Masochist.IsConverting());

		Masochist.MoveHealthTo(0.4f);

		TestTrue(TEXT("dropping below half opened a window"),
			Masochist.IsConverting());
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERSION_TEST(FCataclysmNodeIsNeededToOpenAWindowTest,
	"Cataclysm.DamageConversion.WithoutTheNodeNothingOpens")
{
	using namespace CataclysmDamageConversionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		// THE POINT OF THIS TEST IS THAT EVERY OTHER CHARACTER IS UNTOUCHED.
		// The crossing hook runs for everything with an ability system, on every
		// write to health, so a character without the node must come through it
		// having changed nothing at all.
		const FScopedBleeder Ordinary(World);

		Ordinary.MoveHealthTo(0.1f);

		TestFalse(TEXT("no node, no window"), Ordinary.IsConverting());
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERSION_TEST(FCataclysmStayingLowDoesNotReopenTest,
	"Cataclysm.DamageConversion.BeingHitAgainWhileAlreadyLowOpensNothingNew")
{
	using namespace CataclysmDamageConversionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		Masochist.TakeTheNode();

		Masochist.MoveHealthTo(0.4f);
		const float FirstEndsAt = Masochist.AbilitySystem->DamageConversionEndsAt();

		// PAST THE COOLDOWN BEFORE BEING HIT AGAIN, AND A GUARD PROOF IS WHY.
		// This test first waited one second, and it passed against a build whose
		// crossing check had been removed entirely -- because the COOLDOWN was
		// refusing the second window, not the logic this test is named for. It
		// proved nothing.
		//
		// Eleven seconds is past both the 3 second window and the 10 second
		// cooldown, so nothing else is standing in the way, and the only reason
		// left for no new window to open is that this was not a crossing.
		World->TimeSeconds += 11.0f;

		// STILL BELOW HALF THE WHOLE TIME. Health goes from 40% to 30% and never
		// back above, which is a character fighting on at low health being hit
		// again. If being low were enough, every one of those blows would start
		// the window again and the node would never stop converting.
		Masochist.MoveHealthTo(0.3f);

		TestEqual(TEXT("the window did not move"),
			Masochist.AbilitySystem->DamageConversionEndsAt(), FirstEndsAt,
			0.001f);
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// The window and the cooldown
// ---------------------------------------------------------------------------

CATACLYSM_CONVERSION_TEST(FCataclysmWindowEndsWhenItSaysTest,
	"Cataclysm.DamageConversion.TheWindowEndsWhenItSaysItDoes")
{
	using namespace CataclysmDamageConversionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		Masochist.TakeTheNode(/*WindowSeconds=*/3.0f);
		Masochist.MoveHealthTo(0.4f);

		World->TimeSeconds += 2.9f;
		TestTrue(TEXT("still converting just before it ends"),
			Masochist.IsConverting());

		World->TimeSeconds += 0.2f;
		TestFalse(TEXT("and not once it has"), Masochist.IsConverting());
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERSION_TEST(FCataclysmPointsMakeTheWindowLongerTest,
	"Cataclysm.DamageConversion.PointsSpentMakeTheWindowLonger")
{
	using namespace CataclysmDamageConversionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		// EIGHT POINTS, WHICH IS THE NODE'S WHOLE ALLOWANCE: 3 seconds increased
		// by 5% per point is 3 x 1.40, or 4.2. The stat pipeline is what works
		// that out; what is checked here is that this code uses the answer
		// rather than the base.
		const FScopedBleeder Masochist(World);
		Masochist.TakeTheNode(/*WindowSeconds=*/4.2f);
		Masochist.MoveHealthTo(0.4f);

		World->TimeSeconds += 3.5f;
		TestTrue(TEXT("a base window would already be over, and this is not"),
			Masochist.IsConverting());

		World->TimeSeconds += 0.8f;
		TestFalse(TEXT("and it ends at 4.2 seconds"), Masochist.IsConverting());
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERSION_TEST(FCataclysmCooldownHoldsOffTheNextWindowTest,
	"Cataclysm.DamageConversion.NoSecondWindowUntilTheCooldownIsUp")
{
	using namespace CataclysmDamageConversionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		Masochist.TakeTheNode(/*WindowSeconds=*/3.0f);
		Masochist.MoveHealthTo(0.4f);

		// PAST THE WINDOW BUT NOT PAST THE COOLDOWN, which is the stretch the
		// two numbers exist to create. The window is 3 seconds and the cooldown
		// is 10, so between them the conversion is over and another may not yet
		// begin. A single number could not describe that.
		World->TimeSeconds += 5.0f;
		TestFalse(TEXT("the first window is over"), Masochist.IsConverting());

		Masochist.MoveHealthTo(0.9f);
		Masochist.MoveHealthTo(0.4f);
		TestFalse(TEXT("and crossing again inside the cooldown opens nothing"),
			Masochist.IsConverting());
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERSION_TEST(FCataclysmAnotherWindowAfterTheCooldownTest,
	"Cataclysm.DamageConversion.AnotherCrossingOpensOneOnceTheCooldownIsUp")
{
	using namespace CataclysmDamageConversionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		Masochist.TakeTheNode(/*WindowSeconds=*/3.0f);
		Masochist.MoveHealthTo(0.4f);

		// THE OTHER HALF OF THE TEST ABOVE, and without it that one would pass
		// against code that never opened a second window at all.
		World->TimeSeconds += 10.1f;
		Masochist.MoveHealthTo(0.9f);
		Masochist.MoveHealthTo(0.4f);

		TestTrue(TEXT("the cooldown is up, so crossing opens another"),
			Masochist.IsConverting());
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// What is converted, and what is not
// ---------------------------------------------------------------------------

CATACLYSM_CONVERSION_TEST(FCataclysmConvertsTheWholeBlowTest,
	"Cataclysm.DamageConversion.AnOpenWindowConvertsTheWholeBlow")
{
	using namespace CataclysmDamageConversionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		Masochist.TakeTheNode();
		Masochist.MoveHealthTo(0.4f);

		// ALL OF IT, NOT A SHARE. "Converts ALL damage you take." A partial
		// conversion would read as a damage reduction, which this node is not.
		const float Converted = Conversion::ConvertIfActive(
			Masochist.Actor, 200.0f, /*bIsAlreadyDamageOverTime=*/false);

		TestEqual(TEXT("the whole blow was converted"), Converted, 200.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERSION_TEST(FCataclysmClosedWindowConvertsNothingTest,
	"Cataclysm.DamageConversion.WithNoWindowOpenNothingIsConverted")
{
	using namespace CataclysmDamageConversionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		Masochist.TakeTheNode();

		// NEVER CROSSED, so no window was ever opened. Zero here is what makes
		// the caller take the whole blow off health as it always did.
		const float Converted = Conversion::ConvertIfActive(
			Masochist.Actor, 200.0f, /*bIsAlreadyDamageOverTime=*/false);

		TestEqual(TEXT("nothing was converted"), Converted, 0.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERSION_TEST(FCataclysmDamageOverTimeIsNotConvertedTest,
	"Cataclysm.DamageConversion.DamageAlreadySpreadOverTimeIsNeverConverted")
{
	using namespace CataclysmDamageConversionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		Masochist.TakeTheNode();
		Masochist.MoveHealthTo(0.4f);

		// THE ONE THAT STOPS THE WHOLE THING LOOPING. The Bleeding this node
		// creates arrives as damage like anything else. Converting that again
		// would convert it for ever, and no damage would ever reach health --
		// the node would be immunity rather than a delay.
		const float Converted = Conversion::ConvertIfActive(
			Masochist.Actor, 200.0f, /*bIsAlreadyDamageOverTime=*/true);

		TestEqual(TEXT("a tick of damage over time is not converted"),
			Converted, 0.0f, 0.01f);

		// AND THE WINDOW IS STILL OPEN, so this is a refusal of that one blow
		// rather than the window being closed by it.
		TestTrue(TEXT("and the window is untouched"), Masochist.IsConverting());
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// That the damage path really uses any of this
// ---------------------------------------------------------------------------

/**
 * A blow that lands while a window is open takes no health.
 *
 * WHY THIS ONE MATTERS MORE THAN THE OTHERS. Everything above drives
 * `UCataclysmDamageConversion` directly, so all of it would still pass against a
 * build where the damage path never called it at all -- the node would do
 * nothing in the game and every test would be green. This is the only test here
 * that goes in through a real hit.
 *
 * A REAL ATTACKER, because `ApplyDirectDamage` needs an instigator with an
 * ability system to attribute the blow to.
 */
CATACLYSM_CONVERSION_TEST(FCataclysmDamagePathUsesTheConversionTest,
	"Cataclysm.DamageConversion.AHitDuringAnOpenWindowTakesNoHealth")
{
	using namespace CataclysmDamageConversionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		const FScopedBleeder Attacker(World);

		Masochist.TakeTheNode();
		Masochist.MoveHealthTo(0.4f);
		const float Before = Masochist.AbilitySystem->GetNumericAttribute(
			Vital::GetHealthAttribute());

		UCataclysmSkillEffects::ApplyDirectDamage(
			Attacker.Actor, Masochist.Actor, 100.0f);

		TestEqual(TEXT("the blow took no health at all"),
			Masochist.AbilitySystem->GetNumericAttribute(
				Vital::GetHealthAttribute()),
			Before, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

/**
 * A converted blow fills Fervour when the Bleeding lands, not when it is struck.
 *
 * WHAT THIS DOES AND DOES NOT GUARD, AND A GUARD PROOF IS WHAT SETTLED IT.
 * Breaking the conversion so that nothing is converted fails this test, which is
 * what it is for: a blow that reaches health fills the bar at once, and a
 * converted one must not.
 *
 * IT DOES NOT GUARD WHICH FIGURE THE FERVOUR CALL IS HANDED. Changing that call
 * from what reached health to what the whole hit was worth fails NOTHING, and
 * the reason is worth writing down rather than treating as a gap. The Breaking
 * Point converts a blow whole or not at all, so when anything is converted the
 * amount reaching health is zero and the block holding that call is skipped
 * entirely. The two figures cannot differ there. A test asserting otherwise
 * would be asserting something that cannot happen.
 *
 * A RATE HAS TO BE SET OR THIS PASSES AGAINST ANYTHING. Every class has a
 * Fervour rate of zero until a node grants one, so a character without one gains
 * nothing whatever this code does, and the test would prove nothing.
 */
CATACLYSM_CONVERSION_TEST(FCataclysmConvertedDamageDefersFervourTest,
	"Cataclysm.DamageConversion.AConvertedBlowDoesNotFillFervourTwice")
{
	using namespace CataclysmDamageConversionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		const FScopedBleeder Attacker(World);

		Masochist.Set(Resource::GetMaxClassResourceAttribute(), 100.0f);
		Masochist.Set(Resource::GetClassResourceAttribute(), 0.0f);
		Masochist.Set(Resource::GetFervourFromDamageAttribute(), 1.0f);

		Masochist.TakeTheNode();
		Masochist.MoveHealthTo(0.4f);

		UCataclysmSkillEffects::ApplyDirectDamage(
			Attacker.Actor, Masochist.Actor, 100.0f);

		TestEqual(TEXT("no Fervour arrived at the moment of the blow"),
			Masochist.AbilitySystem->GetNumericAttribute(
				Resource::GetClassResourceAttribute()),
			0.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// What the converted damage IS, which is what the next five nodes read back
// ---------------------------------------------------------------------------

CATACLYSM_CONVERSION_TEST(FCataclysmConvertedDamageIsBleedingTest,
	"Cataclysm.DamageConversion.TheConvertedDamageIsBleedingRatherThanAnyDamageOverTime")
{
	using namespace CataclysmDamageConversionTest;

	// THE NODE SAYS "BLEEDING" AND UNTIL ISSUE #962 THE TAG SAID "DAMAGE OVER
	// TIME". `ConvertIfActive` passed the bare `Keyword.DoT` parent, so a
	// character this rule had just hurt was carrying damage over time that was
	// not Bleeding -- and Thirst for Pain, the one node this whole mechanic was
	// built to unblock, reads the word Bleeding back. It would have been false
	// for ever and nothing at run time would have said so.
	//
	// BOTH DIRECTIONS ARE ASSERTED. That the character is Bleeding, which is what
	// that node needs; and that it still answers yes to the damage over time
	// question, which is what everything written before this needs. A tag
	// satisfying only the first would quietly break the second.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);

		Masochist.TakeTheNode();
		Masochist.MoveHealthTo(0.4f);
		if (!TestTrue(TEXT("the window is open"), Masochist.IsConverting()))
		{
			World->DestroyWorld(false);
			return false;
		}

		TestFalse(TEXT("nothing is bleeding before the blow"),
			UCataclysmDebuffs::IsBleeding(Masochist.AbilitySystem));
		TestEqual(TEXT("and no debuff is carried before the blow"),
			UCataclysmDebuffs::CountOn(Masochist.AbilitySystem), 0);

		const float Converted = Conversion::ConvertIfActive(
			Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false);
		TestEqual(TEXT("the whole blow was converted"), Converted, 100.0f, 0.01f);

		TestTrue(TEXT("the character is now Bleeding, which is what Thirst for "
					  "Pain asks"),
			UCataclysmDebuffs::IsBleeding(Masochist.AbilitySystem));

		// THE PARENT QUESTION STILL ANSWERS YES. Bleed is a child of
		// `Keyword.DoT` and the engine counts a tag against its parents, so
		// nothing written before this changes its answer.
		TestTrue(TEXT("and still carries damage over time"),
			Masochist.AbilitySystem->HasMatchingGameplayTag(
				UCataclysmDamageCalculation::DamageOverTimeTag()));

		// AND IT IS ONE DEBUFF, NOT TWO. This is the direction that would fail
		// against a build attaching both the parent and the child.
		TestEqual(TEXT("the Masochist carries exactly one debuff"),
			UCataclysmDebuffs::CountOn(Masochist.AbilitySystem), 1);
	}

	World->DestroyWorld(false);
	return true;
}

#undef CATACLYSM_CONVERSION_TEST

#endif  // WITH_AUTOMATION_TESTS
