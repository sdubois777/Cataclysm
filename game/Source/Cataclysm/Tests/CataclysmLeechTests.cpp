// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmLeech.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Leech: what a hit gives back to whoever landed it. Issue #895.
 *
 * WHAT WAS WRONG. `LifeLeech`, `ManaLeech` and `EnergyShieldLeech` all existed,
 * were clamped and were replicated, and no code in the project read any of the
 * three. Two of the three affixes granting them were dropped before they reached
 * an attribute at all, and the third reached one nothing consulted. That is the
 * shape of issue #481, where an enemy's armour was computed, stored, tested and
 * then read by no arithmetic.
 *
 * THE DESIGN STATES EVERY RULE these check, at docs/Cataclysm_GDD_v2.md, Leech.
 * The 3% of 400 giving 12 below is its own worked example.
 */
namespace CataclysmLeechTest
{
	/**
	 * Two of these fight. Named apart from the harnesses in the neighbouring
	 * test files on purpose: the Unreal unity build concatenates these
	 * translation units, so two structs of one name in two files compile until
	 * both are clean and then collide.
	 */
	struct FLeechCombatant
	{
		explicit FLeechCombatant(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor));

			Vitals = NewVitals;
			Combat = NewCombat;

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			// Room to be hurt and room to recover, so neither the floor at zero
			// nor the ceiling at maximum interferes with what is being measured.
			Vitals->SetMaxHealth(10'000.0f);
			Vitals->SetHealth(10'000.0f);
			Vitals->SetMaxMana(10'000.0f);
			Vitals->SetMana(10'000.0f);
		}

		~FLeechCombatant()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
	};

	/** Run the payout timer forward, a quarter second at a time. */
	void PayOutFor(AActor* Character, float Seconds)
	{
		const float Step = UCataclysmRegeneration::StepSeconds;
		for (float Elapsed = 0.0f; Elapsed < Seconds - 0.0001f; Elapsed += Step)
		{
			UCataclysmLeech::PayOutStep(Character, Step);
		}
	}
}

#define CATACLYSM_LEECH_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The arithmetic, which needs no world
// ---------------------------------------------------------------------------

CATACLYSM_LEECH_TEST(FCataclysmLeechAmountTest,
	"Cataclysm.Leech.ThreePercentOfFourHundredDamageIsTwelve")
{
	// THE DESIGN'S OWN WORKED EXAMPLE: "A character with 3% life leech who lands
	// a hit for 400 damage leeches 12 health."
	TestEqual(TEXT("3% of 400 damage is 12"),
		UCataclysmLeech::AmountFrom(400.0f, 3.0f), 12.0f, 0.001f);

	TestEqual(TEXT("a character with no leech gets nothing"),
		UCataclysmLeech::AmountFrom(400.0f, 0.0f), 0.0f, 0.001f);
	TestEqual(TEXT("and a hit that took nothing gives nothing"),
		UCataclysmLeech::AmountFrom(0.0f, 3.0f), 0.0f, 0.001f);

	// A NEGATIVE IS NOT A DRAIN. Nothing in the design states one, so it is
	// treated as none rather than as leech running backwards.
	TestEqual(TEXT("a negative leech figure gives nothing"),
		UCataclysmLeech::AmountFrom(400.0f, -5.0f), 0.0f, 0.001f);

	return true;
}

CATACLYSM_LEECH_TEST(FCataclysmLeechPaidInStepTest,
	"Cataclysm.Leech.APaymentArrivesEvenlyAndTheLastStepPaysTheBalance")
{
	FCataclysmLeechPayment Payment;
	Payment.Remaining = 12.0f;
	Payment.SecondsLeft = 3.0f;

	// A QUARTER SECOND OF A THREE SECOND PAYOUT IS A TWELFTH OF IT.
	TestEqual(TEXT("a quarter second pays a twelfth of a three second payout"),
		UCataclysmLeech::PaidInStep(Payment, 0.25f), 1.0f, 0.001f);

	// THE LAST STEP PAYS WHATEVER IS LEFT, and that is not a rounding
	// convenience. Paying a fraction of the balance every step would leave a
	// remainder that halves for ever and never reaches zero, so a character
	// would carry a growing list of payments each owing a millionth of a point.
	Payment.Remaining = 0.4f;
	Payment.SecondsLeft = 0.2f;
	TestEqual(TEXT("a step longer than the time left pays the whole balance"),
		UCataclysmLeech::PaidInStep(Payment, 0.25f), 0.4f, 0.001f);

	Payment.Remaining = 0.0f;
	Payment.SecondsLeft = 3.0f;
	TestEqual(TEXT("a payment with nothing left pays nothing"),
		UCataclysmLeech::PaidInStep(Payment, 0.25f), 0.0f, 0.001f);

	// AND THE PAYOUT PERIOD IS THREE SECONDS, read off the constant rather than
	// written here twice. The design calls it a starting point expected to move.
	TestEqual(TEXT("a hit's leech takes three seconds to arrive"),
		UCataclysmLeech::PayoutSeconds, 3.0f, 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// A hit, and what it gives back
// ---------------------------------------------------------------------------

CATACLYSM_LEECH_TEST(FCataclysmHitLeechesForTheAttackerTest,
	"Cataclysm.Leech.AHitGivesItsAttackerHealthOverThreeSeconds")
{
	using namespace CataclysmLeechTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FLeechCombatant Attacker(World);
	FLeechCombatant Target(World);

	Attacker.Combat->SetAttackDamage(1'000.0f);
	Attacker.Vitals->SetLifeLeech(10.0f);

	// THE ATTACKER IS HURT FIRST, because a pool that is already full cannot be
	// topped up and the test would then pass on a leech of zero.
	Attacker.Vitals->SetHealth(5'000.0f);

	const float Before = Attacker.Vitals->GetHealth();
	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Target.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());

	if (!TestTrue(FString::Printf(TEXT("the hit dealt damage (%.1f)"), Dealt),
				  Dealt > 0.0f))
	{
		return false;
	}

	// NOTHING ARRIVES AT ONCE, which is the whole point of the rule. "Instant
	// leech makes a character that is winning unkillable and does nothing for
	// one that is losing, because the recovery arrives only as fast as the
	// damage does."
	TestEqual(TEXT("no health arrives on the instant of the hit"),
		Attacker.Vitals->GetHealth(), Before, 0.001f);
	TestEqual(TEXT("but one payment is outstanding"),
		Attacker.AbilitySystem->GetLeechPayments().Num(), 1);

	// A QUARTER OF THE WAY THROUGH, PART OF IT HAS ARRIVED AND NOT ALL.
	PayOutFor(Attacker.Actor, 0.75f);
	const float PartWay = Attacker.Vitals->GetHealth();
	const float Expected = UCataclysmLeech::AmountFrom(Dealt, 10.0f);

	TestTrue(FString::Printf(
		TEXT("some health has arrived after 0.75s: %.2f of %.2f"),
		PartWay - Before, Expected),
		PartWay > Before + 0.001f);
	TestTrue(TEXT("and not all of it"), PartWay < Before + Expected - 0.001f);

	// AND BY THREE SECONDS ALL OF IT HAS.
	PayOutFor(Attacker.Actor, 2.25f);
	TestEqual(TEXT("the whole amount has arrived by three seconds"),
		Attacker.Vitals->GetHealth(), Before + Expected, 0.05f);

	// AND NOTHING IS STILL OWED, so a character that stops fighting stops
	// carrying anything.
	TestEqual(TEXT("nothing is left outstanding"),
		Attacker.AbilitySystem->GetLeechPayments().Num(), 0);

	// A FURTHER STEP ADDS NOTHING, which is what makes the figure above a total
	// rather than a rate that happens to have been sampled there.
	const float Settled = Attacker.Vitals->GetHealth();
	PayOutFor(Attacker.Actor, 1.0f);
	TestEqual(TEXT("and no more arrives afterwards"),
		Attacker.Vitals->GetHealth(), Settled, 0.001f);

	return true;
}

CATACLYSM_LEECH_TEST(FCataclysmLeechFromSeveralHitsTest,
	"Cataclysm.Leech.LeechFromSeveralHitsRunsAtTheSameTime")
{
	using namespace CataclysmLeechTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FLeechCombatant Attacker(World);
	FLeechCombatant Target(World);

	Attacker.Combat->SetAttackDamage(1'000.0f);
	Attacker.Vitals->SetLifeLeech(10.0f);
	Attacker.Vitals->SetHealth(5'000.0f);

	// THREE HITS IN QUICK SUCCESSION, WITH NO PAYOUT BETWEEN THEM. The design:
	// "Each hit starts its own 3-second payout. A character hitting continuously
	// therefore reaches a steady state of roughly three hits' worth of leech in
	// flight." A running total could not express three deadlines.
	float Owed = 0.0f;
	for (int32 Hit = 0; Hit < 3; ++Hit)
	{
		Owed += UCataclysmLeech::AmountFrom(
			UCataclysmSkillEffects::ApplyHit(
				Attacker.Actor, Target.Actor, /*DamagePercent=*/100.0f,
				FGameplayTagContainer(), FCataclysmHitDelivery()),
			10.0f);
	}

	TestEqual(TEXT("three hits leave three payments in flight"),
		Attacker.AbilitySystem->GetLeechPayments().Num(), 3);

	const float Before = Attacker.Vitals->GetHealth();
	PayOutFor(Attacker.Actor, UCataclysmLeech::PayoutSeconds);

	TestEqual(TEXT("and all three are paid in full"),
		Attacker.Vitals->GetHealth(), Before + Owed, 0.1f);

	return true;
}

CATACLYSM_LEECH_TEST(FCataclysmLeechOverkillTest,
	"Cataclysm.Leech.OverkillDoesNotCount")
{
	using namespace CataclysmLeechTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FLeechCombatant Attacker(World);
	FLeechCombatant Target(World);

	Attacker.Combat->SetAttackDamage(1'000.0f);
	Attacker.Vitals->SetLifeLeech(10.0f);
	Attacker.Vitals->SetHealth(5'000.0f);

	// THE DESIGN'S OWN EXAMPLE, in its own numbers: "An enemy with 25 health
	// left, hit for 400, contributes 25 to the leech calculation and not 400.
	// Without this rule the last hit on every trash enemy is the largest heal in
	// the game, which rewards overkilling rather than fighting."
	Target.Vitals->SetHealth(25.0f);
	Target.Vitals->SetEnergyShield(0.0f);

	const float Before = Attacker.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Target.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());

	PayOutFor(Attacker.Actor, UCataclysmLeech::PayoutSeconds);

	// 10% OF THE 25 THE TARGET HAD, not 10% of the 1,000 the blow carried.
	TestEqual(TEXT("leech counts what the target had, not what the blow carried"),
		Attacker.Vitals->GetHealth() - Before, 2.5f, 0.05f);

	return true;
}

CATACLYSM_LEECH_TEST(FCataclysmMinionBlowLeechesNothingTest,
	"Cataclysm.Leech.AMinionsBlowLeechesNothingForItsSummoner")
{
	using namespace CataclysmLeechTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FLeechCombatant Summoner(World);
	FLeechCombatant Target(World);

	Summoner.Combat->SetAttackDamage(1'000.0f);
	Summoner.Vitals->SetLifeLeech(10.0f);
	Summoner.Vitals->SetHealth(5'000.0f);

	// A MINION'S BLOW IS DEALT IN ITS SUMMONER'S NAME, so the attacker every
	// part of the hit reads is the summoner. The design names leech among what
	// a minion does not take from its summoner, so the blow carries the fourth
	// of its four exclusions.
	FCataclysmHitDelivery MinionBlow;
	MinionBlow.bCannotCriticallyStrike = true;
	MinionBlow.bCannotPenetrate = true;
	MinionBlow.bCarriesNoWeaponSubType = true;
	MinionBlow.bCannotLeech = true;

	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Summoner.Actor, Target.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), MinionBlow);

	if (!TestTrue(FString::Printf(
			TEXT("the minion's blow dealt damage (%.1f)"), Dealt), Dealt > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("a minion's blow leaves its summoner no leech to collect"),
		Summoner.AbilitySystem->GetLeechPayments().Num(), 0);

	const float Before = Summoner.Vitals->GetHealth();
	PayOutFor(Summoner.Actor, UCataclysmLeech::PayoutSeconds);
	TestEqual(TEXT("and the summoner gains no health from it"),
		Summoner.Vitals->GetHealth(), Before, 0.001f);

	// AND THE SAME BLOW WITHOUT THAT ONE FLAG DOES LEECH, which is what makes
	// the check above evidence of the flag rather than of leech being broken.
	FCataclysmHitDelivery OwnBlow = MinionBlow;
	OwnBlow.bCannotLeech = false;

	UCataclysmSkillEffects::ApplyHit(
		Summoner.Actor, Target.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), OwnBlow);

	TestEqual(TEXT("the same blow struck in the character's own name does"),
		Summoner.AbilitySystem->GetLeechPayments().Num(), 1);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
