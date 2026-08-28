// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmHealthDebt.h"
#include "AbilitySystem/CataclysmLowHealthRelief.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * What a fall to very low health gives back, for the second sentence of the
 * Masochist's Rock Bottom:
 *
 *   Dropping below 20% health clears all outstanding debt and grants 50
 *   Fervour, no more than once every 30 seconds.
 *
 * Issue #1069.
 *
 * WHAT IS MEASURED HERE AND WHAT IS NOT. These drive
 * `UCataclysmLowHealthRelief::NoteHealthChanged` directly, because what the
 * sentence is made of is a crossing and a cooldown, and both are arithmetic on
 * a clock. That the health path really calls it is checked by
 * `tools/tests/test_hooks_no_headless_test_can_drive_still_call_their_jobs.py`,
 * which reads `UCataclysmVitalAttributeSet::NotifyHealthChanged` and fails if
 * the call is gone. That check exists because no headless test can drive that
 * function: reaching it means building and applying a real gameplay effect.
 *
 * WHAT EACH HALF DOES is checked next door.
 * `Cataclysm.HealthDebt.DroppingLowClearsTheDebtOnlyWithRockBottom` covers the
 * clearing and `Cataclysm.Fervour` covers the pool. This file is about WHEN.
 *
 * A PLAIN ACTOR AND NOT A CHARACTER, the choice
 * `CataclysmDamageConversionTests.cpp` makes and for the reason it gives.
 *
 * THE CLOCK IS MOVED BY HAND. A world built by `UWorld::CreateWorld` is never
 * ticked, so `World->TimeSeconds` stays where it is until a test moves it. That
 * is what makes a thirty second cooldown testable at all.
 */
namespace CataclysmLowHealthReliefTest
{
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;
	using Relief = UCataclysmLowHealthRelief;

	/** A character that can owe health and hold Fervour, on a plain actor. */
	struct FScopedFaller
	{
		explicit FScopedFaller(UWorld* World)
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
			Set(Resource::GetMaxClassResourceAttribute(), 100.0f);
		}

		~FScopedFaller()
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

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		float Owed() const { return Get(Resource::GetHealthOwedAttribute()); }
		float Fervour() const
		{
			return Get(Resource::GetClassResourceAttribute());
		}

		/** Spend the point: both of Rock Bottom's second-sentence rows. */
		void TakeRockBottom() const
		{
			Set(Resource::GetDebtClearedOnDroppingLowAttribute(), 1.0f);
			Set(Resource::GetFervourOnDroppingLowAttribute(), 50.0f);
		}

		/** Put health at a share of maximum and tell the rule it moved. */
		float MoveHealthTo(float Share) const
		{
			Set(Vital::GetHealthAttribute(), 1'000.0f * Share);
			return Relief::NoteHealthChanged(Actor);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

#define CATACLYSM_RELIEF_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The two things a crossing does
// ---------------------------------------------------------------------------

CATACLYSM_RELIEF_TEST(FCataclysmReliefClearsAndGrantsTest,
	"Cataclysm.LowHealthRelief.DroppingLowClearsTheDebtAndGrantsFervour")
{
	using namespace CataclysmLowHealthReliefTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedFaller Faller(World);
	UCataclysmHealthDebt::Defer(Faller.AbilitySystem, 300.0f);

	// WITHOUT THE OPTION A FALL TO LOW HEALTH DOES NOTHING AT ALL, and that
	// half comes first: a version that helped every character in the game would
	// pass every check below it.
	TestEqual(TEXT("a character without the option gains nothing"),
			  Faller.MoveHealthTo(0.15f), 0.0f, 0.001f);
	TestEqual(TEXT("and still owes three hundred"), Faller.Owed(), 300.0f,
			  0.001f);
	TestEqual(TEXT("and has no Fervour"), Faller.Fervour(), 0.0f, 0.001f);

	// AND WITH IT, ON THE NEXT CROSSING, BOTH THINGS HAPPEN. Back above the
	// line first, because the rule is a crossing rather than a state and the
	// character is already below it.
	Faller.TakeRockBottom();
	Faller.MoveHealthTo(1.0f);

	const float Gained = Faller.MoveHealthTo(0.15f);

	TestEqual(TEXT("the debt is cleared"), Faller.Owed(), 0.0f, 0.001f);
	TestEqual(TEXT("and fifty Fervour arrived"), Faller.Fervour(), 50.0f,
			  0.001f);
	TestEqual(TEXT("and the crossing reports both"), Gained, 350.0f, 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// A crossing, not a state
// ---------------------------------------------------------------------------

CATACLYSM_RELIEF_TEST(FCataclysmReliefIsACrossingTest,
	"Cataclysm.LowHealthRelief.BeingHitWhileAlreadyLowIsNotACrossing")
{
	using namespace CataclysmLowHealthReliefTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedFaller Faller(World);
	Faller.TakeRockBottom();

	// THE FIRST FALL IS A CROSSING.
	TestEqual(TEXT("falling to 15% health grants the Fervour"),
			  Faller.MoveHealthTo(0.15f), 50.0f, 0.001f);

	// AND BEING SCRATCHED AGAIN WHILE ALREADY LOW IS NOT ONE. The clock has not
	// moved, so the cooldown would refuse this too -- which is exactly why the
	// pool is checked rather than the return value alone. What this pins is
	// that the character sitting at 5% health is not crossing the line on every
	// blow, which is the failure a state check instead of a crossing check
	// would produce.
	Faller.Set(Resource::GetClassResourceAttribute(), 0.0f);
	World->TimeSeconds += Relief::CooldownSeconds + 1.0f;

	TestEqual(TEXT("a further hit while already low is not a crossing"),
			  Faller.MoveHealthTo(0.05f), 0.0f, 0.001f);
	TestEqual(TEXT("and no Fervour arrived, though the cooldown had passed"),
			  Faller.Fervour(), 0.0f, 0.001f);

	// AND HEALING BACK ABOVE THE LINE ARMS IT AGAIN.
	Faller.MoveHealthTo(0.9f);
	TestEqual(TEXT("healing above the line grants nothing by itself"),
			  Faller.Fervour(), 0.0f, 0.001f);

	TestEqual(TEXT("and falling below it again is a fresh crossing"),
			  Faller.MoveHealthTo(0.19f), 50.0f, 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// No more than once every thirty seconds
// ---------------------------------------------------------------------------

CATACLYSM_RELIEF_TEST(FCataclysmReliefCooldownTest,
	"Cataclysm.LowHealthRelief.ACrossingIsHonouredOnlyOnceEveryThirtySeconds")
{
	using namespace CataclysmLowHealthReliefTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedFaller Faller(World);
	Faller.TakeRockBottom();

	TestEqual(TEXT("the first crossing is honoured"),
			  Faller.MoveHealthTo(0.15f), 50.0f, 0.001f);

	// A SECOND CROSSING JUST INSIDE THE THIRTY SECONDS IS REFUSED. Health goes
	// back up and down again, which is a real crossing by every other rule
	// here, so what refuses it is the cooldown and nothing else.
	Faller.Set(Resource::GetClassResourceAttribute(), 0.0f);
	World->TimeSeconds += Relief::CooldownSeconds - 0.5f;

	Faller.MoveHealthTo(0.9f);
	TestEqual(TEXT("a crossing just inside the cooldown is refused"),
			  Faller.MoveHealthTo(0.15f), 0.0f, 0.001f);
	TestEqual(TEXT("and no Fervour arrived"), Faller.Fervour(), 0.0f, 0.001f);

	// AND ONE JUST PAST IT IS HONOURED.
	World->TimeSeconds += 1.0f;

	Faller.MoveHealthTo(0.9f);
	TestEqual(TEXT("and one just past the cooldown is honoured"),
			  Faller.MoveHealthTo(0.15f), 50.0f, 0.001f);

	// AND THE THRESHOLD AND THE COOLDOWN ARE THE OPTION'S OWN NUMBERS. A
	// mechanic tuned to different figures would pass everything above.
	TestEqual(TEXT("the threshold is a fifth of maximum health"),
			  Relief::HealthShare, 0.2f, 0.0001f);
	TestEqual(TEXT("and the cooldown is thirty seconds"),
			  Relief::CooldownSeconds, 30.0f, 0.0001f);

	return true;
}

// ---------------------------------------------------------------------------
// The boundary
// ---------------------------------------------------------------------------

CATACLYSM_RELIEF_TEST(FCataclysmReliefBoundaryTest,
	"Cataclysm.LowHealthRelief.SittingExactlyOnTheThresholdIsBelowIt")
{
	using namespace CataclysmLowHealthReliefTest;

	// "DROPPING BELOW 20% HEALTH", so a character that lands exactly on 20% has
	// not dropped below it and the option does nothing. The boundary is
	// reachable rather than theoretical: a cost that is a percentage of a round
	// number lands on it often enough.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedFaller Faller(World);
	Faller.TakeRockBottom();

	// EXACTLY ON THE LINE COUNTS AS BELOW IT, which is what
	// `Health > Maximum * HealthShare` says: 200 is not above 200.
	TestEqual(TEXT("landing exactly on a fifth of health is a crossing"),
			  Faller.MoveHealthTo(0.2f), 50.0f, 0.001f);

	// AND A SCRATCH THAT LEAVES THE CHARACTER ABOVE THE LINE IS NOT ONE.
	Faller.Set(Resource::GetClassResourceAttribute(), 0.0f);
	World->TimeSeconds += Relief::CooldownSeconds + 1.0f;
	Faller.MoveHealthTo(1.0f);

	TestEqual(TEXT("a fall to just above the line is not a crossing"),
			  Faller.MoveHealthTo(0.21f), 0.0f, 0.001f);
	TestEqual(TEXT("and no Fervour arrived"), Faller.Fervour(), 0.0f, 0.001f);

	return true;
}

#undef CATACLYSM_RELIEF_TEST

#endif // WITH_AUTOMATION_TESTS
