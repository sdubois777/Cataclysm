// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmHealthDebt.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

/**
 * The rules a health debt follows once it exists, for the three Blood Tithe
 * nodes that are about the debt rather than about a skill.
 *
 * WHY THESE ARE NOT IN `CataclysmSkillTemplateTests.cpp` WITH THE OTHERS. The
 * tests there are about a cost being WORKED OUT: a skill is granted, cast, and
 * the health it charged is measured. Nothing here needs a skill at all. Rolling
 * Debt is about what a payment does to a debt that already exists, and The
 * Reckoning is about a debt that is never taken on a timer, is cleared by a
 * kill, and kills the character it passes. The wiring -- that
 * `UCataclysmSkillTemplate::PayHealthCost` really calls these -- is proved in
 * that file, beside the cost it is part of.
 *
 * Issues #995 and #997.
 */
namespace CataclysmHealthDebtTest
{
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	/**
	 * A character that can owe health: an ability system with the two sets a
	 * debt needs, on a plain actor.
	 *
	 * A PLAIN ACTOR AND NOT A CHARACTER, deliberately, for every test but the
	 * lethal one. What is being measured is arithmetic on two attributes and a
	 * timestamp, and a character brings a movement component, a regeneration
	 * timer and a death path with it. The one test that needs a real death
	 * spawns a real player character instead and says so.
	 */
	struct FScopedDebtor
	{
		FScopedDebtor(UWorld* World)
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

		~FScopedDebtor()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value)
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		float Health() const { return Get(Vital::GetHealthAttribute()); }
		float Owed() const { return Get(Resource::GetHealthOwedAttribute()); }

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

// ---------------------------------------------------------------------------
// Rolling Debt
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRollingDebtPushesADebtOutTest,
	"Cataclysm.HealthDebt.PayingAgainWhileOwingPushesTheDebtFurtherOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRollingDebtPushesADebtOutTest::RunTest(const FString&)
{
	using namespace CataclysmHealthDebtTest;
	using Debt = UCataclysmHealthDebt;

	// THE MASOCHIST'S ROLLING DEBT NODE at its full six points: "Paying a health
	// cost while one is still owed extends the delay on what is owed by 0.5
	// seconds per point, to a maximum of 3 seconds." Issue #995.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedDebtor Debtor(World);

	// A DEBT OF A HUNDRED, FALLING DUE IN THREE SECONDS. `Defer` is what a
	// deferred cost does, so this is the state a character is in an instant
	// after casting with Deferred Payment.
	Debt::Defer(Debtor.AbilitySystem, 100.0f);
	TestEqual(TEXT("a hundred is owed"), Debtor.Owed(), 100.0f, 0.001f);

	// A CHARACTER WITHOUT THE NODE PUSHES NOTHING OUT, which is every character
	// in the game until a point is spent there. Without this the assertions
	// below would pass just as well if every payment extended every debt.
	TestEqual(TEXT("a character without the node extends nothing"),
			  Debt::ExtendForPaymentWhileOwing(Debtor.AbilitySystem), 0.0f,
			  0.001f);

	// AT ITS FULL SIX POINTS: half a second per point.
	Debtor.Set(Resource::GetHealthDebtDelayExtensionAttribute(), 3.0f);

	// THE WHOLE DRAIN SPAN IS PASSED AS ONE STEP THROUGHOUT THIS TEST, AND
	// THAT IS DELIBERATE. Since issue #1120 a debt comes out of health over
	// `DrainSeconds` rather than in one write, and `DrainedInStep` takes the
	// whole balance when the step is at least as long as the time left. What
	// this test is about is WHEN a debt falls due, not how it lands, so asking
	// for the lot in one call keeps every assertion on the due date.
	// `Cataclysm.HealthDebt.ADebtComesOutOfHealthOverFiveSeconds` is where the
	// drain itself is measured.

	// TWO AND A HALF SECONDS IN, THE DEBT IS NOT DUE. It falls due at three.
	World->TimeSeconds += 2.5f;
	TestEqual(TEXT("before three seconds nothing is taken"),
			  Debt::DrainIfDue(Debtor.Actor, Debt::DrainSeconds), 0.0f, 0.001f);

	// AND A PAYMENT NOW PUSHES IT OUT BY THREE SECONDS.
	TestEqual(TEXT("paying while owing pushes the debt out by the node's value"),
			  Debt::ExtendForPaymentWhileOwing(Debtor.AbilitySystem), 3.0f,
			  0.001f);

	// SO THE MOMENT IT WOULD HAVE FALLEN DUE PASSES AND NOTHING IS TAKEN. This
	// is the assertion the whole node comes down to: without the extension the
	// debt would be gone from the character's health by now.
	World->TimeSeconds += 1.0f;
	TestEqual(TEXT("three and a half seconds in, the debt is still not due"),
			  Debt::DrainIfDue(Debtor.Actor, Debt::DrainSeconds), 0.0f, 0.001f);
	TestEqual(TEXT("and the health is untouched"), Debtor.Health(), 1'000.0f,
			  0.001f);

	// AND IT FALLS DUE AT SIX SECONDS INSTEAD OF THREE. Nothing else changed:
	// no point spent, no attribute rewritten, only the clock moved.
	World->TimeSeconds += 2.6f;
	TestEqual(TEXT("once the extended delay passes the whole debt is taken"),
			  Debt::DrainIfDue(Debtor.Actor, Debt::DrainSeconds), 100.0f, 0.001f);
	TestEqual(TEXT("and the health is gone"), Debtor.Health(), 900.0f, 0.001f);
	TestEqual(TEXT("and nothing is owed"), Debtor.Owed(), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRollingDebtNeedsSomethingOwedTest,
	"Cataclysm.HealthDebt.APaymentWithNothingOwedExtendsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRollingDebtNeedsSomethingOwedTest::RunTest(const FString&)
{
	using namespace CataclysmHealthDebtTest;
	using Debt = UCataclysmHealthDebt;

	// "WHILE ONE IS STILL OWED" IS HALF THE SENTENCE AND IT IS THE HALF THAT IS
	// EASIEST TO DROP. Issue #995. A version that extended on every payment
	// would pass every assertion in the test above, and would give a character
	// with this node a longer delay on the FIRST debt of every fight -- an
	// entirely different node.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedDebtor Debtor(World);
	Debtor.Set(Resource::GetHealthDebtDelayExtensionAttribute(), 3.0f);

	// NOTHING OWED AT ALL.
	TestEqual(TEXT("a payment with nothing owed extends nothing"),
			  Debt::ExtendForPaymentWhileOwing(Debtor.AbilitySystem), 0.0f,
			  0.001f);
	TestFalse(TEXT("and no due time was invented"),
			  Debtor.AbilitySystem->IsHealthDebtDue());

	// AND THE CLOCK RUNNING ON DOES NOT MAKE ONE APPEAR. A version that set a
	// due time here would leave a debt of nothing falling due for ever, which
	// `DrainIfDue` would then have to clear on every step.
	World->TimeSeconds += 10.0f;
	TestFalse(TEXT("and none appears later either"),
			  Debtor.AbilitySystem->IsHealthDebtDue());
	TestEqual(TEXT("and nothing is taken"),
			  Debt::DrainIfDue(Debtor.Actor, Debt::DrainSeconds), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRollingDebtCapTest,
	"Cataclysm.HealthDebt.HowFarOneDebtCanBePushedOutIsCapped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRollingDebtCapTest::RunTest(const FString&)
{
	using namespace CataclysmHealthDebtTest;
	using Debt = UCataclysmHealthDebt;

	// "TO A MAXIMUM OF 3 SECONDS", READ AS A CAP ON THE WHOLE DEBT RATHER THAN
	// ON ONE PAYMENT. Issue #996 carries the reading and why it was chosen: at
	// the node's full six points one payment already reaches three seconds, so
	// the two readings agree there and disagree only on repeated payments, and
	// capping one payment would let a character that keeps paying push a debt
	// out for ever.
	//
	// MEASURED AT ONE POINT, WHERE THE TWO READINGS DIFFER. Half a second a
	// payment: six payments reach the cap and the seventh adds nothing. At six
	// points this test could not tell the two apart at all.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedDebtor Debtor(World);
	Debtor.Set(Resource::GetHealthDebtDelayExtensionAttribute(), 0.5f);

	Debt::Defer(Debtor.AbilitySystem, 100.0f);

	// SIX PAYMENTS OF HALF A SECOND REACH THE CAP.
	float Pushed = 0.0f;
	for (int32 Payment = 0; Payment < 6; ++Payment)
	{
		Pushed += Debt::ExtendForPaymentWhileOwing(Debtor.AbilitySystem);
	}
	TestEqual(TEXT("six payments of half a second push it out by three"),
			  Pushed, Debt::MaxDelayExtensionSeconds, 0.001f);
	TestEqual(TEXT("and the component agrees that is what it has used"),
			  Debtor.AbilitySystem->HealthDebtExtensionApplied(),
			  Debt::MaxDelayExtensionSeconds, 0.001f);

	// AND THE SEVENTH PUSHES NOTHING.
	TestEqual(TEXT("a seventh payment pushes nothing"),
			  Debt::ExtendForPaymentWhileOwing(Debtor.AbilitySystem), 0.0f,
			  0.001f);
	TestEqual(TEXT("and a hundredth does not either"),
			  Debt::ExtendForPaymentWhileOwing(Debtor.AbilitySystem), 0.0f,
			  0.001f);

	// SO THE DEBT FALLS DUE AT SIX SECONDS AND NOT LATER, however many times the
	// character paid. Three seconds of delay plus three of extension.
	World->TimeSeconds += 5.9f;
	TestEqual(TEXT("just under six seconds it is still not due"),
			  Debt::DrainIfDue(Debtor.Actor, Debt::DrainSeconds), 0.0f, 0.001f);

	World->TimeSeconds += 0.2f;
	TestEqual(TEXT("and just past six seconds it is taken"),
			  Debt::DrainIfDue(Debtor.Actor, Debt::DrainSeconds), 100.0f, 0.001f);

	// AND THE NEXT DEBT GETS THE WHOLE ALLOWANCE AGAIN. The cap belongs to one
	// debt rather than to a character's life; a settled debt is finished.
	TestEqual(TEXT("a settled debt returns the allowance"),
			  Debtor.AbilitySystem->HealthDebtExtensionApplied(), 0.0f, 0.001f);

	Debt::Defer(Debtor.AbilitySystem, 50.0f);
	TestEqual(TEXT("so a fresh debt can be pushed out again"),
			  Debt::ExtendForPaymentWhileOwing(Debtor.AbilitySystem), 0.5f,
			  0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// A debt comes out of health as a drain and not as one hit. Issue #1120.
// ---------------------------------------------------------------------------

/**
 * The arithmetic of one step, with no ability system and no world.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDrainStepArithmeticTest,
	"Cataclysm.HealthDebt.OneStepTakesItsShareAndTheLastStepTakesTheRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDrainStepArithmeticTest::RunTest(const FString&)
{
	using Debt = UCataclysmHealthDebt;

	// FIVE SECONDS, WHICH THE PROJECT OWNER ASKED FOR ON 2026-08-31.
	TestEqual(TEXT("a debt drains over five seconds"), Debt::DrainSeconds, 5.0f);

	// A QUARTER SECOND OUT OF FIVE IS A TWENTIETH OF WHAT IS LEFT.
	TestEqual(TEXT("a quarter second of a five second drain takes a twentieth"),
			  Debt::DrainedInStep(100.0f, 5.0f, 0.25f), 5.0f, 0.001f);

	// AND THE SHARE IS OF WHAT IS LEFT OVER THE TIME LEFT, so the rate stays
	// constant as both shrink together. 95 over 4.75 seconds is the same 5 a
	// step as 100 over 5 was.
	TestEqual(TEXT("the rate does not drift as the debt shrinks"),
			  Debt::DrainedInStep(95.0f, 4.75f, 0.25f), 5.0f, 0.001f);
	TestEqual(TEXT("nor near the end of it"),
			  Debt::DrainedInStep(10.0f, 0.5f, 0.25f), 5.0f, 0.001f);

	// THE LAST STEP TAKES WHATEVER IS LEFT. Without this a fixed share of the
	// remainder would leave a shrinking amount that never reaches zero, and the
	// character would owe a fraction of a point for ever.
	TestEqual(TEXT("a step as long as the time left takes the balance"),
			  Debt::DrainedInStep(5.0f, 0.25f, 0.25f), 5.0f, 0.001f);
	TestEqual(TEXT("and so does a longer one"),
			  Debt::DrainedInStep(5.0f, 0.1f, 0.25f), 5.0f, 0.001f);

	// AND A STEP THAT ARRIVED LATE TAKES THE LOT, which is reachable rather
	// than theoretical: the timer is a quarter second and a frame can be
	// longer than that.
	TestEqual(TEXT("a step past the end takes the balance"),
			  Debt::DrainedInStep(40.0f, -2.0f, 0.25f), 40.0f, 0.001f);

	// NOTHING OWED TAKES NOTHING, and neither does a step of no time.
	TestEqual(TEXT("nothing owed takes nothing"),
			  Debt::DrainedInStep(0.0f, 5.0f, 0.25f), 0.0f, 0.001f);
	TestEqual(TEXT("a negative amount takes nothing"),
			  Debt::DrainedInStep(-10.0f, 5.0f, 0.25f), 0.0f, 0.001f);
	TestEqual(TEXT("a step of no time takes nothing"),
			  Debt::DrainedInStep(100.0f, 5.0f, 0.0f), 0.0f, 0.001f);

	return true;
}

/**
 * A whole debt really does come out over five seconds, and not before.
 *
 * WHY THIS IS SEPARATE FROM THE ARITHMETIC ABOVE. That checks one step in
 * isolation; this drives the real function on a real ability system with a real
 * clock, which is what says the two are wired to each other. A version that
 * computed the right share and then wrote the whole debt anyway would pass every
 * assertion above.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDebtDrainsOverFiveSecondsTest,
	"Cataclysm.HealthDebt.ADebtComesOutOfHealthOverFiveSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDebtDrainsOverFiveSecondsTest::RunTest(const FString&)
{
	using namespace CataclysmHealthDebtTest;
	using Debt = UCataclysmHealthDebt;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedDebtor Debtor(World);

	constexpr float Step = UCataclysmRegeneration::StepSeconds;

	// A HUNDRED OWED, FALLING DUE IN THREE SECONDS, against a thousand health.
	Debt::Defer(Debtor.AbilitySystem, 100.0f);

	// NOTHING COMES OUT BEFORE IT IS DUE. The drain replaced the single hit; it
	// did not replace the delay, so Deferred Payment still says what it said.
	World->TimeSeconds += 2.9f;
	TestEqual(TEXT("before three seconds nothing is taken"),
			  Debt::DrainIfDue(Debtor.Actor, Step), 0.0f, 0.001f);
	TestEqual(TEXT("and health has not moved"), Debtor.Health(), 1'000.0f,
			  0.001f);

	// AND WHEN IT IS DUE, A STEP TAKES A STEP'S WORTH AND NOT THE LOT. This is
	// the whole change: before issue #1120 this one call took all hundred.
	World->TimeSeconds += 0.1f;
	TestEqual(TEXT("the first step takes a twentieth and not the whole debt"),
			  Debt::DrainIfDue(Debtor.Actor, Step), 5.0f, 0.001f);
	TestEqual(TEXT("health moved by exactly that"), Debtor.Health(), 995.0f,
			  0.001f);
	TestEqual(TEXT("and the rest is still owed"), Debtor.Owed(), 95.0f, 0.001f);

	// THE DEBT SHRINKS AS IT DRAINS, which is what separates this from a
	// Reckoning debt: that one bleeds health while the amount owed stands.
	TestTrue(TEXT("the debt is smaller than it was"), Debtor.Owed() < 100.0f);

	// STEPPING ON TO THE END OF THE SPAN. Nineteen more quarter seconds is the
	// remaining 4.75, and the last of them takes whatever rounding left behind.
	for (int32 Taken = 0; Taken < 19; ++Taken)
	{
		World->TimeSeconds += Step;
		Debt::DrainIfDue(Debtor.Actor, Step);
	}

	TestEqual(TEXT("after five seconds the whole hundred has come out"),
			  Debtor.Health(), 900.0f, 0.01f);
	TestEqual(TEXT("and nothing is owed"), Debtor.Owed(), 0.0f, 0.001f);

	// AND THE DUE TIME WENT WITH IT, so this is not asked again on every step
	// for the rest of the character's life.
	TestFalse(TEXT("and no debt is outstanding"),
			  Debtor.AbilitySystem->IsHealthDebtDue());

	// A FURTHER STEP TAKES NOTHING AT ALL.
	World->TimeSeconds += Step;
	TestEqual(TEXT("a step with nothing owed takes nothing"),
			  Debt::DrainIfDue(Debtor.Actor, Step), 0.0f, 0.001f);
	TestEqual(TEXT("and health is where it was"), Debtor.Health(), 900.0f,
			  0.01f);

	return true;
}

// ---------------------------------------------------------------------------
// The Reckoning
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReckoningDebtNeverFallsDueTest,
	"Cataclysm.HealthDebt.TheReckoningsDebtIsNeverTakenOnATimer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReckoningDebtNeverFallsDueTest::RunTest(const FString&)
{
	using namespace CataclysmHealthDebtTest;
	using Debt = UCataclysmHealthDebt;

	// THE MASOCHIST'S THE RECKONING KEYSTONE: "Health costs are never taken.
	// They accumulate as a debt... and the debt is cleared only by killing an
	// enemy." Issue #997.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedDebtor Debtor(World);

	// THE KEYSTONE'S TWO FLAT ROWS: the whole cost is deferred, and the debt is
	// cleared only by a kill.
	Debtor.Set(Resource::GetDeferredHealthCostShareAttribute(), 100.0f);
	Debtor.Set(Resource::GetHealthDebtClearedOnlyByAKillAttribute(), 1.0f);

	TestTrue(TEXT("the flag reads as on"),
			 Debt::IsClearedOnlyByAKill(Debtor.AbilitySystem));
	TestEqual(TEXT("and the whole of every cost is deferred"),
			  Debt::DeferredSharePercent(Debtor.AbilitySystem), 100.0f, 0.001f);

	Debt::Defer(Debtor.AbilitySystem, 200.0f);

	// A MINUTE LATER, TWENTY TIMES THE ORDINARY DELAY, AND NOTHING IS TAKEN.
	World->TimeSeconds += 60.0f;
	TestEqual(TEXT("the debt is not taken when the delay passes"),
			  Debt::DrainIfDue(Debtor.Actor, Debt::DrainSeconds), 0.0f, 0.001f);
	TestEqual(TEXT("the health is untouched"), Debtor.Health(), 1'000.0f,
			  0.001f);
	TestEqual(TEXT("and it is still owed"), Debtor.Owed(), 200.0f, 0.001f);

	// AND A SECOND COST JOINS THE FIRST rather than replacing it, so the debt
	// really does accumulate the way the keystone says.
	Debt::Defer(Debtor.AbilitySystem, 150.0f);
	World->TimeSeconds += 60.0f;
	TestEqual(TEXT("a second cost is still not taken"),
			  Debt::DrainIfDue(Debtor.Actor, Debt::DrainSeconds), 0.0f, 0.001f);
	TestEqual(TEXT("and the debt accumulated"), Debtor.Owed(), 350.0f, 0.001f);
	TestEqual(TEXT("and the health is still untouched"), Debtor.Health(),
			  1'000.0f, 0.001f);

	// A CHARACTER WITHOUT THE KEYSTONE THAT DEFERS THE WHOLE COST STILL PAYS.
	// This is the distinction the flag exists for: Deferred Payment at its full
	// ten points also defers 100% of a cost, and its own sentence says the cost
	// "is taken 3 seconds later". A build that read the share instead of the
	// flag would pass every assertion above and quietly change that node.
	FScopedDebtor Ordinary(World);
	Ordinary.Set(Resource::GetDeferredHealthCostShareAttribute(), 100.0f);
	TestFalse(TEXT("a full deferral is not the keystone"),
			  Debt::IsClearedOnlyByAKill(Ordinary.AbilitySystem));

	Debt::Defer(Ordinary.AbilitySystem, 200.0f);
	World->TimeSeconds += Debt::DelaySeconds + 0.1f;
	TestEqual(TEXT("and its debt is taken when the delay passes"),
			  Debt::DrainIfDue(Ordinary.Actor, Debt::DrainSeconds), 200.0f, 0.001f);
	TestEqual(TEXT("and its health went with it"), Ordinary.Health(), 800.0f,
			  0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReckoningKillClearsTheDebtTest,
	"Cataclysm.HealthDebt.KillingAnEnemyClearsTheReckoningsDebt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReckoningKillClearsTheDebtTest::RunTest(const FString&)
{
	using namespace CataclysmHealthDebtTest;
	using Debt = UCataclysmHealthDebt;

	// "THE DEBT IS CLEARED ONLY BY KILLING AN ENEMY." Issue #997. This is the
	// half of the keystone the test above cannot reach: it proves the debt is
	// never taken, and without this one there would be no way to end it at all.
	//
	// THROUGH A REAL DEATH RATHER THAN BY CALLING `ClearOnKill`. What is being
	// checked is that `ACataclysmEnemyCharacter::HandleDeath` reaches the
	// player's pawn, which is the same route the experience award takes and the
	// same shape `Cataclysm.EnemyScore.KillingAnEnemyIsWhatGrantsTheExperience`
	// uses.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player state"), PlayerState)
		|| !TestNotNull(TEXT("player controller"), Controller)
		|| !TestNotNull(TEXT("player character"), Player))
	{
		return false;
	}

	Controller->SetPlayerState(PlayerState);
	Controller->Possess(Player);

	UAbilitySystemComponent* AbilitySystem = Player->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the player's ability system"), AbilitySystem))
	{
		return false;
	}

	AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(),
										   1'000.0f);
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   1'000.0f);
	AbilitySystem->SetNumericAttributeBase(
		Resource::GetHealthDebtClearedOnlyByAKillAttribute(), 1.0f);

	Debt::Defer(AbilitySystem, 300.0f);
	TestEqual(TEXT("three hundred is owed"),
			  AbilitySystem->GetNumericAttribute(
				  Resource::GetHealthOwedAttribute()), 300.0f, 0.001f);

	ACataclysmEnemyCharacter* Victim =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a creature to kill"), Victim))
	{
		return false;
	}

	Victim->HandleDeath();

	TestEqual(TEXT("killing it cleared the whole debt"),
			  AbilitySystem->GetNumericAttribute(
				  Resource::GetHealthOwedAttribute()), 0.0f, 0.001f);

	// AND THE HEALTH WAS NOT TAKEN ON THE WAY OUT. A debt cleared by a kill is
	// forgiven, not collected; charging it here would make the keystone's own
	// escape route the thing that kills the player.
	TestEqual(TEXT("and the health was not charged for it"),
			  AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute()),
			  1'000.0f, 0.001f);

	// AND A KILL DOES NOTHING FOR A CHARACTER WITHOUT THE KEYSTONE, whose
	// deferred cost is meant to be paid. Without this the hook would hand every
	// character in the game a free clearance on every kill.
	AbilitySystem->SetNumericAttributeBase(
		Resource::GetHealthDebtClearedOnlyByAKillAttribute(), 0.0f);
	Debt::Defer(AbilitySystem, 200.0f);

	ACataclysmEnemyCharacter* Second =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(600.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a second creature"), Second))
	{
		return false;
	}

	Second->HandleDeath();

	TestEqual(TEXT("an ordinary deferred cost survives a kill"),
			  AbilitySystem->GetNumericAttribute(
				  Resource::GetHealthOwedAttribute()), 200.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReckoningDebtCanKillTest,
	"Cataclysm.HealthDebt.ADebtPastCurrentHealthKillsUnderTheReckoning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReckoningDebtCanKillTest::RunTest(const FString&)
{
	using namespace CataclysmHealthDebtTest;
	using Debt = UCataclysmHealthDebt;

	// "IF YOUR DEBT EVER EXCEEDS YOUR CURRENT HEALTH, YOU DIE." Issue #997.
	//
	// A REAL CHARACTER, BECAUSE A REAL DEATH IS WHAT IS BEING MEASURED. The
	// plain actor the other tests use has no `HandleDeath` to call, so a test
	// against it could only watch health reach zero -- and health at zero
	// without the character being marked dead is exactly the defect issue #570
	// recorded, where fifty-six attacks landed on a player who was already out
	// of health.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player state"), PlayerState)
		|| !TestNotNull(TEXT("player controller"), Controller)
		|| !TestNotNull(TEXT("player character"), Player))
	{
		return false;
	}

	Controller->SetPlayerState(PlayerState);
	Controller->Possess(Player);

	UAbilitySystemComponent* AbilitySystem = Player->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the player's ability system"), AbilitySystem))
	{
		return false;
	}

	AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(),
										   1'000.0f);
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   400.0f);
	AbilitySystem->SetNumericAttributeBase(
		Resource::GetHealthDebtClearedOnlyByAKillAttribute(), 1.0f);

	// THE STEP THE RUNNING GAME USES, so the arithmetic here is the arithmetic
	// that happens in play. `ACataclysmCharacterBase` calls this from the
	// regeneration timer and passes exactly this.
	constexpr float Step = UCataclysmRegeneration::StepSeconds;

	// A DEBT SMALLER THAN THE HEALTH LEFT TAKES NOTHING.
	Debt::Defer(AbilitySystem, 399.0f);
	TestEqual(TEXT("a debt below current health drains nothing"),
			  Debt::DrainWhileDebtExceedsHealth(Player, Step), 0.0f, 0.001f);
	TestFalse(TEXT("and the character is alive"),
			  UCataclysmSkillEffects::IsDead(Player));

	// AND SO DOES ONE EXACTLY EQUAL TO IT, because the design writes "exceeds".
	AbilitySystem->SetNumericAttributeBase(
		Resource::GetHealthOwedAttribute(), 400.0f);
	TestEqual(TEXT("a debt exactly equal to current health drains nothing"),
			  Debt::DrainWhileDebtExceedsHealth(Player, Step), 0.0f, 0.001f);
	TestFalse(TEXT("and the character is still alive"),
			  UCataclysmSkillEffects::IsDead(Player));
	TestEqual(TEXT("and still has its health"),
			  AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute()),
			  400.0f, 0.001f);

	// ONE POINT MORE AND THE BLEEDING STARTS. Nothing else changed: the debt
	// grew by one. Issue #1120 turned this from an instant death into a drain,
	// so the first step past the line takes a step's worth and not a life.
	//
	// 401 SPREAD ACROSS FIVE SECONDS IS 80.2 A SECOND, and a quarter second of
	// that is 20.05.
	AbilitySystem->SetNumericAttributeBase(
		Resource::GetHealthOwedAttribute(), 401.0f);
	TestEqual(TEXT("a debt one point past current health starts a drain"),
			  Debt::DrainWhileDebtExceedsHealth(Player, Step), 20.05f, 0.01f);
	TestFalse(TEXT("and the character is NOT dead yet, which is the whole "
				   "point of the change"),
			  UCataclysmSkillEffects::IsDead(Player));
	TestEqual(TEXT("and health has moved by exactly that much"),
			  AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute()),
			  379.95f, 0.01f);

	// AND THE DEBT DID NOT SHRINK, which is the whole difference between this
	// and an ordinary debt draining. The Reckoning says its debt "is cleared
	// only by killing an enemy", so bleeding does not pay it off, and the
	// keystone's damage bonus -- which is read off the amount owed -- still
	// stands while the character bleeds.
	TestEqual(TEXT("the debt is untouched by the bleeding"),
			  AbilitySystem->GetNumericAttribute(
				  Resource::GetHealthOwedAttribute()), 401.0f, 0.001f);

	// STEPPING ON UNTIL IT KILLS, AND COUNTING THE STEPS. "It dies eventually"
	// would pass for a drain a hundred times too fast or too slow, so the count
	// is asserted.
	int32 Steps = 0;
	while (!UCataclysmSkillEffects::IsDead(Player) && Steps < 200)
	{
		Debt::DrainWhileDebtExceedsHealth(Player, Step);
		++Steps;
	}

	TestTrue(TEXT("the character died of it in the end"),
			 UCataclysmSkillEffects::IsDead(Player));
	TestEqual(TEXT("after 379.95 health at 20.05 a step, which is 19 steps"),
			  Steps, 19);

	TestEqual(TEXT("and its health is at zero"),
			  AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute()),
			  0.0f, 0.001f);
	TestEqual(TEXT("and the debt went with it"),
			  AbilitySystem->GetNumericAttribute(
				  Resource::GetHealthOwedAttribute()), 0.0f, 0.001f);

	// THE WHOLE DEATH TOOK LESS THAN THE FIVE SECOND SPAN, WHICH IS THE RULE
	// AND NOT AN ACCIDENT. Health is already smaller than the debt when the
	// bleeding starts, so it runs out before the span does. Twenty steps
	// altogether, counting the first one above, is five seconds exactly; the
	// character was dead on the twentieth.
	TestTrue(TEXT("and it took no longer than the whole drain span"),
			 static_cast<float>(Steps + 1) * Step <= Debt::DrainSeconds);

	// AND A CORPSE IS NOT BLED AGAIN, which matters because this runs on the
	// regeneration step several times a second and a body stays in the level.
	TestEqual(TEXT("a dead character is not bled a second time"),
			  Debt::DrainWhileDebtExceedsHealth(Player, Step), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOrdinaryDebtDoesNotKillTest,
	"Cataclysm.HealthDebt.AnOrdinaryDebtPastCurrentHealthDoesNotKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOrdinaryDebtDoesNotKillTest::RunTest(const FString&)
{
	using namespace CataclysmHealthDebtTest;
	using Debt = UCataclysmHealthDebt;

	// ONLY THE RECKONING'S DEBT IS LETHAL. Issue #997. Deferred Payment at its
	// full ten points defers the whole of a cost, and a character on low health
	// can easily owe more than it has left; that debt takes health to nothing
	// when it settles and does not kill outright. Without this test the lethal
	// rule could be applied to every debt in the game and the test above would
	// still pass.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player state"), PlayerState)
		|| !TestNotNull(TEXT("player controller"), Controller)
		|| !TestNotNull(TEXT("player character"), Player))
	{
		return false;
	}

	Controller->SetPlayerState(PlayerState);
	Controller->Possess(Player);

	UAbilitySystemComponent* AbilitySystem = Player->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the player's ability system"), AbilitySystem))
	{
		return false;
	}

	AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(),
										   1'000.0f);
	AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 100.0f);

	// TEN TIMES THE HEALTH LEFT, AND NO KEYSTONE.
	Debt::Defer(AbilitySystem, 1'000.0f);

	TestEqual(TEXT("an ordinary debt past current health bleeds nothing"),
			  Debt::DrainWhileDebtExceedsHealth(
				  Player, UCataclysmRegeneration::StepSeconds), 0.0f, 0.001f);
	TestFalse(TEXT("and the character is alive"),
			  UCataclysmSkillEffects::IsDead(Player));
	TestEqual(TEXT("and keeps its health until the debt falls due"),
			  AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute()),
			  100.0f, 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// Rock Bottom: what cannot be paid, and what a fall to low health clears
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnpayableArithmeticTest,
	"Cataclysm.HealthDebt.WhatCannotBePaidIsTheChargeAboveTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmUnpayableArithmeticTest::RunTest(const FString&)
{
	using Debt = UCataclysmHealthDebt;

	// PURE ARITHMETIC, SO EVERY CASE IS A FEW NUMBERS. Issue #1069. Rock
	// Bottom: "A health cost can never reduce you below 1 health; anything you
	// cannot pay becomes health debt instead."
	constexpr float Floor = 1.0f;

	// A CHARGE THE CHARACTER CAN AFFORD OWES NOTHING, which is the ordinary
	// case even for a character holding the option.
	TestEqual(TEXT("a small charge on full health owes nothing"),
			  Debt::AmountUnpayable(50.0f, 1'000.0f, Floor), 0.0f, 0.001f);

	// AND ONE THAT LANDS EXACTLY ON THE FLOOR OWES NOTHING EITHER. A character
	// on 100 health charged 99 is left on 1, which the sentence allows.
	TestEqual(TEXT("a charge that lands exactly on the floor owes nothing"),
			  Debt::AmountUnpayable(99.0f, 100.0f, Floor), 0.0f, 0.001f);

	// AND ONE PAST IT OWES THE DIFFERENCE. Charged 150 with 100 health, 99 can
	// be paid and 51 is owed.
	TestEqual(TEXT("a charge past the floor owes the difference"),
			  Debt::AmountUnpayable(150.0f, 100.0f, Floor), 51.0f, 0.001f);

	// AND A CHARACTER ALREADY ON THE FLOOR OWES ALL OF IT. Reachable: one that
	// paid down to 1 health and cast again.
	TestEqual(TEXT("a character already on the floor owes the whole charge"),
			  Debt::AmountUnpayable(40.0f, 1.0f, Floor), 40.0f, 0.001f);

	// AND ONE BELOW THE FLOOR IS NOT CREDITED FOR BEING THERE. Without the
	// inner floor this would answer 40 minus a negative number, which is MORE
	// than the charge: a character would owe more than it was ever charged.
	TestEqual(TEXT("a character below the floor still owes only the charge"),
			  Debt::AmountUnpayable(40.0f, 0.0f, Floor), 40.0f, 0.001f);

	// AND A CHARGE OF NOTHING OWES NOTHING, however little health there is.
	TestEqual(TEXT("no charge owes nothing"),
			  Debt::AmountUnpayable(0.0f, 0.0f, Floor), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRockBottomDebtClearingTest,
	"Cataclysm.HealthDebt.DroppingLowClearsTheDebtOnlyWithRockBottom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRockBottomDebtClearingTest::RunTest(const FString&)
{
	using namespace CataclysmHealthDebtTest;
	using Debt = UCataclysmHealthDebt;

	// THE SECOND SENTENCE OF ROCK BOTTOM, on its own. Issue #1069. Whether a
	// real health crossing calls this is `Cataclysm.LowHealthRelief`'s job.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedDebtor Debtor(World);
	Debt::Defer(Debtor.AbilitySystem, 300.0f);
	TestEqual(TEXT("the character owes three hundred"), Debtor.Owed(), 300.0f,
			  0.001f);

	// WITHOUT THE OPTION NOTHING IS CLEARED, and this half comes first: a
	// version that cleared every character's debt would pass the half below.
	TestEqual(TEXT("a character without the option clears nothing"),
			  Debt::ClearOnDroppingLow(Debtor.Actor), 0.0f, 0.001f);
	TestEqual(TEXT("and still owes three hundred"), Debtor.Owed(), 300.0f,
			  0.001f);

	// AND WITH IT THE WHOLE DEBT GOES, because the option says "all outstanding
	// debt" rather than a share of it.
	Debtor.Set(Resource::GetDebtClearedOnDroppingLowAttribute(), 1.0f);

	TestEqual(TEXT("with the option the whole debt is cleared"),
			  Debt::ClearOnDroppingLow(Debtor.Actor), 300.0f, 0.001f);
	TestEqual(TEXT("and nothing is owed"), Debtor.Owed(), 0.0f, 0.001f);

	// AND THE DUE TIME GOES WITH IT. A cleared debt still marked due would have
	// the regeneration step ask about it on every step for the rest of the
	// character's life.
	TestFalse(TEXT("and no debt is marked due"),
			  Debtor.AbilitySystem->IsHealthDebtDue());

	// AND CLEARING AGAIN WITH NOTHING OWED CLEARS NOTHING, which is what the
	// crossing hits every time a character falls low without a debt.
	TestEqual(TEXT("clearing nothing clears nothing"),
			  Debt::ClearOnDroppingLow(Debtor.Actor), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnpayableFlagTest,
	"Cataclysm.HealthDebt.OnlyRockBottomTurnsAnUnpayableCostIntoDebt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmUnpayableFlagTest::RunTest(const FString&)
{
	using namespace CataclysmHealthDebtTest;
	using Debt = UCataclysmHealthDebt;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedDebtor Debtor(World);

	TestFalse(TEXT("a character without the option pays what it can and dies "
				   "of the rest"),
			  Debt::UnpayableBecomesDebt(Debtor.AbilitySystem));

	Debtor.Set(Resource::GetUnpayableHealthCostBecomesDebtAttribute(), 1.0f);
	TestTrue(TEXT("and with it the rest becomes debt"),
			 Debt::UnpayableBecomesDebt(Debtor.AbilitySystem));

	// AND NOTHING AT ALL IS NOT A CHARACTER WITH THE OPTION.
	TestFalse(TEXT("no ability system does not turn a cost into debt"),
			  Debt::UnpayableBecomesDebt(nullptr));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
