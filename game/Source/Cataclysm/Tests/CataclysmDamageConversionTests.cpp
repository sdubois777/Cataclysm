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
 * WHAT IS MEASURED HERE AND WHAT IS NOT. What the node is made of is a
 * crossing, a window and a cooldown, and all three are arithmetic on a clock,
 * so that is what these check.
 *
 * THESE USED TO TELL THE RULE HEALTH HAD MOVED AND NO LONGER DO. Issue #1072.
 * Writing health is enough on its own now, because
 * `UCataclysmVitalAttributeSet::PostAttributeBaseChange` runs the notification
 * for every direct write, so each test below also says that a fall in health
 * reaches the rule at all. Before the fix it did not, on any route but a blow,
 * and this whole file passed anyway.
 *
 * WHAT IS STILL NOT MEASURED HERE is that a BLOW reaches it, which goes through
 * `PostGameplayEffectExecute` instead. That is proved by breaking the call site
 * and watching a test fail, and it is checked on every pull request by
 * `tools/tests/test_hooks_no_headless_test_can_drive_still_call_their_jobs.py`.
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

		/**
		 * Put health at a share of maximum.
		 *
		 * THE WRITE IS THE WHOLE OF IT, AND IT USED NOT TO BE. Issue #1072 made
		 * `UCataclysmVitalAttributeSet::PostAttributeBaseChange` run the
		 * notification on every direct write, so the
		 * `Conversion::NoteHealthChanged` call that stood on the next line is
		 * now reached by the write itself. Leaving it would have had every test
		 * below drive the rule by hand and say nothing about whether anything
		 * else does.
		 */
		void MoveHealthTo(float Share) const
		{
			Set(Vital::GetHealthAttribute(), 1'000.0f * Share);
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

// ---------------------------------------------------------------------------

CATACLYSM_CONVERSION_TEST(FCataclysmConvertedDamageSurvivesACleanseTest,
	"Cataclysm.DamageConversion.TheConvertedBleedIsKeptByACleanseBecauseItIsMarked")
{
	using namespace CataclysmDamageConversionTest;

	// THE CONVERSION MARKS ITS BLEED AS CONVERTED DAMAGE, AND THE MARK IS WHAT A CLEANSE KEEPS. Ruled 2026-10-06.
	// Until then a cleanse kept everything a character had laid on themselves, so this bleed was kept without
	// being told apart. THIS IS THE CONTROL ON THE MARK: with the mark dropped from the conversion, the bleed is an
	// ordinary self-laid one and the cleanse below removes it.

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

		const float Converted = Conversion::ConvertIfActive(
			Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false);
		TestEqual(TEXT("set-up: the whole blow was converted"), Converted, 100.0f, 0.01f);
		TestTrue(TEXT("set-up: the character is bleeding with the converted damage"),
			UCataclysmDebuffs::IsBleeding(Masochist.AbilitySystem));

		TestEqual(TEXT("a cleanse removes nothing from the character"), UCataclysmDebuffs::Cleanse(Masochist.Actor), 0);
		TestTrue(TEXT("and the converted bleed is still there"),
			UCataclysmDebuffs::IsBleeding(Masochist.AbilitySystem));
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------

CATACLYSM_CONVERSION_TEST(FCataclysmConvertedBleedIgnoresTheMovementGateTest,
	"Cataclysm.DamageConversion.AConvertedBleedOnAStillCharacterDealsItsFullTotal")
{
	using namespace CataclysmDamageConversionTest;

	// A BLEED TICKS ONLY WHILE ITS TARGET MOVES, AND STOPS AFTER FOUR SECONDS OF MOVEMENT, BUT NOT THIS ONE. Issue
	// #918, ruled 2026-10-31. The conversion defers damage already taken; it does not cancel it, so its bleed deals its
	// whole total whether its character moves or not. THIS IS THE CONTROL ON THE EXEMPTION: with it removed, the still
	// character below takes nothing and the moving one takes four ticks of its five.
	//
	// TWO MASOCHISTS, EACH CONVERTING ONE BLOW OF 100 INTO A BLEED OF 20 A TICK FOR 5 SECONDS. One stands, which a
	// plain actor always does: no movement sample ever reads one, and `IsMoving` starts false. The other is told once
	// that it moved, which nothing overturns for the same reason. The clock runs 5.5 seconds in the engine's own
	// ticks; the fifth tick lands at the window's end, as the tenth of a ten-second effect does in
	// `Cataclysm.RemainingDamage.ATenSecondEffectOfTenATickDealsAHundredOverItsWholeDuration`.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Still(World);
		const FScopedBleeder Moving(World);
		Moving.AbilitySystem->NoteMovedMetres(1.0f);

		float Before[2] = {0.0f, 0.0f};
		const FScopedBleeder* Both[2] = {&Still, &Moving};
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FScopedBleeder& One = *Both[Index];
			One.TakeTheNode();
			One.MoveHealthTo(0.4f);
			if (!TestTrue(TEXT("set-up: the window is open"), One.IsConverting())
				|| !TestEqual(TEXT("set-up: the whole blow was converted"),
					Conversion::ConvertIfActive(One.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f)
				|| !TestTrue(TEXT("set-up: the character is bleeding with it"),
					UCataclysmDebuffs::IsBleeding(One.AbilitySystem)))
			{
				World->DestroyWorld(false);
				return false;
			}
			Before[Index] = One.AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		}

		CataclysmTestWorld::RunClock(World, 5.5f);

		TestFalse(TEXT("set-up: the still character reads as standing"), Still.AbilitySystem->IsMoving());
		TestTrue(TEXT("set-up: the moving one reads as moving"), Moving.AbilitySystem->IsMoving());
		const float StillTook = Before[0] - Still.AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		const float MovingTook = Before[1] - Moving.AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		TestEqual(FString::Printf(TEXT("the still character took the whole 100 it converted, %.2f"), StillTook),
				  StillTook, 100.0f, 0.01f);
		TestEqual(FString::Printf(TEXT("and the moving one the same, %.2f, so the four seconds did not stop it"),
								  MovingTook),
				  MovingTook, 100.0f, 0.01f);
		TestFalse(TEXT("the converted bleed has run out"), UCataclysmDebuffs::IsBleeding(Still.AbilitySystem));
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// Converted damage holds no bleed slot. Issue #2275.
// ---------------------------------------------------------------------------

/**
 * EACH CONVERTED BLOW IS ITS OWN EFFECT, BESIDE EVERY OTHER BLEED. Issue #2275, ruled by the coordinating session
 * 2026-11-01. "Converts all damage you take into Bleeding over 5 seconds" is said of each blow, and the design's
 * health-debt paragraph is "the same rule with the sign reversed" as leech, where "Each hit starts its own 3-second
 * payout". Until then the converted bleed shared the one bleed slot every bleed on a target shares: a converted blow
 * no stronger than a running bleed only refreshed it and was lost, a stronger enemy bleed took the converted one off,
 * and an enemy's weaker bleed lived on as extra converted time, which a cleanse kept and the movement gate exempted.
 *
 * EVERY TEST BELOW KEEPS ITS MASOCHIST STANDING STILL. A plain actor is never sampled, so `IsMoving` stays false and
 * an enemy's bleed on it ticks nothing (issue #918). What a Masochist loses is then exactly the converted damage, and
 * any enemy bleed's time sheltering inside a converted one shows as converted ticks that should not be there.
 *
 * EACH ENEMY BLEED IS LAID WITH `bScalesWithInstigator` FALSE, so its figure is the one written here and not one the
 * enemy's damage over time stats made of it.
 */
namespace CataclysmConvertedSlotTest
{
	using CataclysmDamageConversionTest::FScopedBleeder;

	/** How many effects granting the bleed tag run on this character, converted ones included. */
	int32 CountBleedEffectsOn(const UAbilitySystemComponent* System)
	{
		FGameplayTagContainer Bleeding;
		Bleeding.AddTag(UCataclysmDebuffs::BleedTag());
		return System
			? System->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Bleeding)).Num()
			: 0;
	}

	/** How many of those carry the converted-damage mark, `UCataclysmSkillEffects::ConvertedDamageDataName`. */
	int32 CountConvertedBleedsOn(const UAbilitySystemComponent* System)
	{
		if (!System)
		{
			return 0;
		}
		FGameplayTagContainer Bleeding;
		Bleeding.AddTag(UCataclysmDebuffs::BleedTag());
		int32 Converted = 0;
		for (const FActiveGameplayEffectHandle& Handle :
				 System->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Bleeding)))
		{
			const FActiveGameplayEffect* Active = System->GetActiveGameplayEffect(Handle);
			if (Active && Active->Spec.GetSetByCallerMagnitude(
					FName(UCataclysmSkillEffects::ConvertedDamageDataName), /*WarnIfNotFound=*/false, 0.0f) > 0.0f)
			{
				++Converted;
			}
		}
		return Converted;
	}

	/** Whether a bleed this applier laid runs on the character. */
	bool CarriesABleedLaidBy(const UAbilitySystemComponent* System, const AActor* Applier)
	{
		if (!System)
		{
			return false;
		}
		FGameplayTagContainer Bleeding;
		Bleeding.AddTag(UCataclysmDebuffs::BleedTag());
		for (const FActiveGameplayEffectHandle& Handle :
				 System->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Bleeding)))
		{
			const FActiveGameplayEffect* Active = System->GetActiveGameplayEffect(Handle);
			if (Active && Active->Spec.GetContext().GetInstigator() == Applier)
			{
				return true;
			}
		}
		return false;
	}

	/** The health a test character holds now. */
	float HealthHeldBy(const FScopedBleeder& Character)
	{
		return Character.AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	}

	/** An enemy's ordinary bleed of this much a tick for 5 seconds, laid on the Masochist. */
	bool LayAnEnemysBleed(const FScopedBleeder& Enemy, const FScopedBleeder& Masochist, float PerTick)
	{
		return UCataclysmSkillEffects::ApplyDamageOverTime(Enemy.Actor, Masochist.Actor, PerTick, 5.0f,
			UCataclysmDebuffs::BleedTag(), /*bScalesWithInstigator=*/false);
	}
}

CATACLYSM_CONVERSION_TEST(FCataclysmSmallerSecondBlowIsStoredTest,
	"Cataclysm.DamageConversion.ASmallerSecondBlowIsStoredBesideTheFirst")
{
	using namespace CataclysmDamageConversionTest;
	using namespace CataclysmConvertedSlotTest;

	// A BLOW OF 100, THEN ONE OF 50 HALF A SECOND LATER. Until issue #2275 the second, at 10 a second against the
	// first's 20, only refreshed the first: its end moved from 5.0 to 5.5 s, it still ticked five times for 100, and
	// the 50 was reported converted and taken off nothing. Now each runs its own five ticks: 100 and 50.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		Masochist.TakeTheNode();
		Masochist.MoveHealthTo(0.4f);
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.IsConverting()))
		{
			World->DestroyWorld(false);
			return false;
		}
		const float Before = HealthHeldBy(Masochist);

		TestEqual(TEXT("set-up: the first blow, 100, is converted whole"),
			Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f);
		CataclysmTestWorld::RunClock(World, 0.5f);
		TestTrue(TEXT("set-up: the window is still open half a second on"), Masochist.IsConverting());
		TestEqual(TEXT("set-up: and the second, 50, is converted whole"),
			Conversion::ConvertIfActive(Masochist.Actor, 50.0f, /*bIsAlreadyDamageOverTime=*/false), 50.0f, 0.01f);
		TestEqual(TEXT("each converted blow runs as its own bleed: two"),
			CountConvertedBleedsOn(Masochist.AbilitySystem), 2);

		CataclysmTestWorld::RunClock(World, 5.6f);

		const float Took = Before - HealthHeldBy(Masochist);
		TestEqual(FString::Printf(TEXT("the Masochist took both blows, 150, five ticks each: %.2f"), Took),
			Took, 150.0f, 0.01f);
		TestEqual(TEXT("and both have run out"), CountConvertedBleedsOn(Masochist.AbilitySystem), 0);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERSION_TEST(FCataclysmLargerSecondBlowKeepsTheFirstTest,
	"Cataclysm.DamageConversion.ALargerSecondBlowDoesNotEraseTheFirst")
{
	using namespace CataclysmDamageConversionTest;
	using namespace CataclysmConvertedSlotTest;

	// A BLOW OF 50, THEN ONE OF 100 A SECOND AND A HALF LATER. Until issue #2275 the second, at 20 a second against
	// the first's 10, replaced the first: the first had ticked once, for 10, and its other 40 were lost, so the
	// Masochist took 110 of 150. Now the first runs on beside the second.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		Masochist.TakeTheNode();
		Masochist.MoveHealthTo(0.4f);
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.IsConverting()))
		{
			World->DestroyWorld(false);
			return false;
		}
		const float Before = HealthHeldBy(Masochist);

		TestEqual(TEXT("set-up: the first blow, 50, is converted whole"),
			Conversion::ConvertIfActive(Masochist.Actor, 50.0f, /*bIsAlreadyDamageOverTime=*/false), 50.0f, 0.01f);
		CataclysmTestWorld::RunClock(World, 1.5f);
		TestTrue(TEXT("set-up: the window is still open a second and a half on"), Masochist.IsConverting());
		TestEqual(TEXT("set-up: and the second, 100, is converted whole"),
			Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f);
		TestEqual(TEXT("the larger blow took nothing off: two converted bleeds run"),
			CountConvertedBleedsOn(Masochist.AbilitySystem), 2);

		CataclysmTestWorld::RunClock(World, 5.5f);

		const float Took = Before - HealthHeldBy(Masochist);
		TestEqual(FString::Printf(TEXT("the Masochist took both blows, 150: %.2f"), Took), Took, 150.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERSION_TEST(FCataclysmStrongerEnemyBleedLeavesConvertedDamageTest,
	"Cataclysm.DamageConversion.AnEnemysStrongerBleedLeavesTheConvertedDamageRunning")
{
	using namespace CataclysmDamageConversionTest;
	using namespace CataclysmConvertedSlotTest;

	// A BLOW OF 100 CONVERTED, 20 A TICK; THEN AN ENEMY'S BLEED OF 40 A TICK. Until issue #2275 the enemy's, the
	// stronger, took the converted bleed off and what it had left was lost; on a Masochist standing still the enemy's
	// bleed ticks nothing, so the Masochist took nothing at all. Now the two run side by side: the converted 100 is
	// taken whole, and the enemy's bleed is gated as any bleed on a still target is.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		const FScopedBleeder Enemy(World);
		Masochist.TakeTheNode();
		Masochist.MoveHealthTo(0.4f);
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.IsConverting()))
		{
			World->DestroyWorld(false);
			return false;
		}
		const float Before = HealthHeldBy(Masochist);

		TestEqual(TEXT("set-up: the blow, 100, is converted whole"),
			Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f);
		TestTrue(TEXT("set-up: the enemy's stronger bleed lands"), LayAnEnemysBleed(Enemy, Masochist, 40.0f));

		TestEqual(TEXT("the converted bleed still runs beside it"), CountConvertedBleedsOn(Masochist.AbilitySystem), 1);
		TestTrue(TEXT("and the enemy's bleed runs too"), CarriesABleedLaidBy(Masochist.AbilitySystem, Enemy.Actor));

		CataclysmTestWorld::RunClock(World, 5.5f);

		TestFalse(TEXT("set-up: the Masochist reads as standing, so the enemy's bleed ticks nothing"),
			Masochist.AbilitySystem->IsMoving());
		const float Took = Before - HealthHeldBy(Masochist);
		TestEqual(FString::Printf(TEXT("the still Masochist took the 100 it converted: %.2f"), Took), Took, 100.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERSION_TEST(FCataclysmBlowConvertedDuringAnEnemysBleedTest,
	"Cataclysm.DamageConversion.ABlowConvertedDuringAnEnemysStrongerBleedIsStored")
{
	using namespace CataclysmDamageConversionTest;
	using namespace CataclysmConvertedSlotTest;

	// AN ENEMY'S BLEED OF 40 A TICK FIRST, WITH TWO OF ITS FOUR SECONDS OF MOVEMENT SPENT; THEN A BLOW OF 100
	// CONVERTED, 20 A TICK. Until issue #2275 the converted blow, the weaker, only refreshed the enemy's bleed: the 100
	// was reported converted and taken off nothing, and the enemy's bleed was given its four seconds back. Now the
	// blow is stored in a bleed of its own, the enemy's bleed runs on beside it, and its count is left alone.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		const FScopedBleeder Enemy(World);
		Masochist.TakeTheNode();
		Masochist.MoveHealthTo(0.4f);
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.IsConverting())
			|| !TestTrue(TEXT("set-up: the enemy's bleed lands"), LayAnEnemysBleed(Enemy, Masochist, 40.0f)))
		{
			World->DestroyWorld(false);
			return false;
		}
		Masochist.AbilitySystem->BleedMovingSeconds = 2.0f;
		const float Before = HealthHeldBy(Masochist);

		TestEqual(TEXT("set-up: the blow, 100, is converted whole"),
			Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f);

		TestEqual(TEXT("the converted blow runs as a bleed of its own"),
			CountConvertedBleedsOn(Masochist.AbilitySystem), 1);
		TestTrue(TEXT("and the enemy's bleed still runs beside it"),
			CarriesABleedLaidBy(Masochist.AbilitySystem, Enemy.Actor));
		TestEqual(TEXT("and the enemy's bleed kept its count of movement, 2.0"),
			Masochist.AbilitySystem->BleedMovingSeconds, 2.0f, 0.001f);

		CataclysmTestWorld::RunClock(World, 5.5f);

		TestFalse(TEXT("set-up: the Masochist reads as standing, so the enemy's bleed ticks nothing"),
			Masochist.AbilitySystem->IsMoving());
		const float Took = Before - HealthHeldBy(Masochist);
		TestEqual(FString::Printf(TEXT("the still Masochist took the 100 it converted: %.2f"), Took), Took, 100.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERSION_TEST(FCataclysmWeakerEnemyBleedDoesNotLengthenTest,
	"Cataclysm.DamageConversion.AnEnemysWeakerBleedDoesNotLengthenTheConvertedDamage")
{
	using namespace CataclysmDamageConversionTest;
	using namespace CataclysmConvertedSlotTest;

	// A BLOW OF 100 CONVERTED, 20 A TICK FOR 5 SECONDS; AT 3.2 SECONDS AN ENEMY'S BLEED OF 1 A TICK FOR 5. Until issue
	// #2275 the enemy's, the weaker, only refreshed the converted bleed, moving its end from 5.0 to 8.2 s: it ticked
	// eight times at the converted 20, exempt from the movement gate, and the Masochist took 160 for a blow of 100.
	// Now the enemy's bleed is its own effect, gated on a still Masochist, and the converted one ends at 5.0 s.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		const FScopedBleeder Enemy(World);
		Masochist.TakeTheNode();
		Masochist.MoveHealthTo(0.4f);
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.IsConverting()))
		{
			World->DestroyWorld(false);
			return false;
		}
		const float Before = HealthHeldBy(Masochist);

		TestEqual(TEXT("set-up: the blow, 100, is converted whole"),
			Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f);
		CataclysmTestWorld::RunClock(World, 3.2f);
		TestEqual(TEXT("set-up: three of its ticks have landed, 60"), Before - HealthHeldBy(Masochist), 60.0f, 0.01f);
		TestTrue(TEXT("set-up: the enemy's weaker bleed lands"), LayAnEnemysBleed(Enemy, Masochist, 1.0f));

		TestEqual(TEXT("the enemy's bleed is an effect of its own: two bleeds run"),
			CountBleedEffectsOn(Masochist.AbilitySystem), 2);

		CataclysmTestWorld::RunClock(World, 6.3f);

		TestFalse(TEXT("set-up: the Masochist reads as standing, so the enemy's bleed ticks nothing"),
			Masochist.AbilitySystem->IsMoving());
		const float Took = Before - HealthHeldBy(Masochist);
		TestEqual(FString::Printf(TEXT("the still Masochist took the 100 it converted and no more: %.2f"), Took),
			Took, 100.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_CONVERSION_TEST(FCataclysmCleanseRemovesTheEnemysBleedTest,
	"Cataclysm.Cleanse.ItRemovesAnEnemysWeakerBleedAndKeepsTheConvertedDamage")
{
	using namespace CataclysmDamageConversionTest;
	using namespace CataclysmConvertedSlotTest;

	// AS THE TEST ABOVE, WITH A CLEANSE STRAIGHT AFTER THE ENEMY'S BLEED LANDS. Until issue #2275 the enemy's bleed
	// had no effect of its own, so the cleanse removed nothing: the converted bleed it lived in is kept by its mark
	// (ruled 2026-10-06), and the Masochist took 160 for a blow of 100. Now the cleanse removes the enemy's bleed and
	// keeps the converted damage, which deals its 100 and no more.

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		const FScopedBleeder Masochist(World);
		const FScopedBleeder Enemy(World);
		Masochist.TakeTheNode();
		Masochist.MoveHealthTo(0.4f);
		if (!TestTrue(TEXT("set-up: the window is open"), Masochist.IsConverting()))
		{
			World->DestroyWorld(false);
			return false;
		}
		const float Before = HealthHeldBy(Masochist);

		TestEqual(TEXT("set-up: the blow, 100, is converted whole"),
			Conversion::ConvertIfActive(Masochist.Actor, 100.0f, /*bIsAlreadyDamageOverTime=*/false), 100.0f, 0.01f);
		CataclysmTestWorld::RunClock(World, 3.2f);
		TestTrue(TEXT("set-up: the enemy's weaker bleed lands"), LayAnEnemysBleed(Enemy, Masochist, 1.0f));

		TestEqual(TEXT("the cleanse removes one effect, the enemy's bleed"), UCataclysmDebuffs::Cleanse(Masochist.Actor), 1);
		TestFalse(TEXT("the enemy's bleed is gone"), CarriesABleedLaidBy(Masochist.AbilitySystem, Enemy.Actor));
		TestEqual(TEXT("and the converted damage is still carried"), CountConvertedBleedsOn(Masochist.AbilitySystem), 1);
		TestTrue(TEXT("so the Masochist still reads as bleeding"), UCataclysmDebuffs::IsBleeding(Masochist.AbilitySystem));

		CataclysmTestWorld::RunClock(World, 6.3f);

		const float Took = Before - HealthHeldBy(Masochist);
		TestEqual(FString::Printf(TEXT("the Masochist took the 100 it converted and no more: %.2f"), Took),
			Took, 100.0f, 0.01f);
	}

	World->DestroyWorld(false);
	return true;
}

#undef CATACLYSM_CONVERSION_TEST

#endif  // WITH_AUTOMATION_TESTS
