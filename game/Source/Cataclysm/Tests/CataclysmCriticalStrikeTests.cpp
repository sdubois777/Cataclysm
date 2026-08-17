// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"

/**
 * Tests for critical strikes.
 *
 * NOTHING IN THE PROJECT ROLLED ONE UNTIL ISSUE #649. `CritChance` and
 * `CritMultiplier` existed as replicated attributes, were clamped, and were set
 * on every enemy from its archetype row, and no code read either. Every enemy
 * carrying them hit for less than the model said, and a floating damage number
 * could not mark a critical strike because there was nothing to read.
 *
 * WHERE THE MULTIPLIER SITS IS NOT A JUDGEMENT MADE HERE. The model applies it
 * to the finished per-hit damage and hands the result to mitigation as the raw
 * hit -- `average_damage_per_hit` in `sim/cataclysm_sim/enemy_stats.py` and
 * `sim/cataclysm_sim/reference_build.py`. So a critical strike multiplies the
 * whole hit before block, armour, resistance, flat reduction and the shield.
 *
 * THE ONE PLACE THE GAME DIFFERS FROM THE MODEL IS DELIBERATE. The model never
 * rolls: it multiplies every hit by the long-run average
 * `(1 - chance + chance x multiplier)`, because it has no use for a single blow.
 * The game rolls, because a hit that is 15.8% larger than usual is not a
 * critical strike and cannot be drawn as one. Over many hits the two agree.
 */

namespace CataclysmCritTest
{
	/** A metre in Unreal's centimetres. */
	constexpr float M = 100.0f;

	static UWorld* MakeWorldThatHasBegunPlay()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (World)
		{
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}
		return World;
	}

	/**
	 * Pins the critical strike roll for as long as it is in scope.
	 *
	 * 0 always critically strikes, because every chance above zero beats it.
	 * 100 never does, because the comparison is strictly less than.
	 *
	 * SET AT THE CONSOLE'S OWN PRIORITY. A console variable in Unreal remembers
	 * who set it and silently drops a write from code when the command line has
	 * already set one. Without this the pin is present in the source and absent
	 * from the run, which is how it first failed.
	 */
	struct FScopedCritRoll
	{
		explicit FScopedCritRoll(float Roll)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(
				TEXT("Cataclysm.CritRoll"));
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}

		~FScopedCritRoll()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	/** A bare actor holding every attribute set, usable as either side. */
	struct FScopedCombatant
	{
		explicit FScopedCombatant(UWorld* World)
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
			UCataclysmAllResistanceAttributeSet* NewAll =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAll);

			Vitals = NewVitals;
			Combat = NewCombat;

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			// Large enough that the floor at zero health never interferes.
			Vitals->SetMaxHealth(1'000'000.0f);
			Vitals->SetHealth(1'000'000.0f);
			LastHealth = Vitals->GetHealth();
		}

		~FScopedCombatant()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Health lost since the last time this was asked. */
		float TakeDamageReading()
		{
			const float Now = Vitals->GetHealth();
			const float Lost = LastHealth - Now;
			LastHealth = Now;
			return Lost;
		}

		/** Give this combatant a critical strike chance and multiplier. */
		void SetCritical(float ChancePercent, float MultiplierPercent) const
		{
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetCritChanceAttribute(),
				ChancePercent);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(),
				MultiplierPercent);
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
		float LastHealth = 0.0f;
	};

	/** A hit of the given size that can critically strike at the given chance. */
	static FCataclysmIncomingHit HitThatCanCrit(float Damage, float Chance,
											   float Multiplier)
	{
		FCataclysmIncomingHit Hit;
		Hit.Damage = Damage;
		Hit.CritChance = Chance;
		Hit.CritMultiplier = Multiplier;
		return Hit;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// The roll and the multiplication, against the calculation directly
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmCritMultipliesTheHitTest,
	"Cataclysm.Crit.ARollUnderTheChanceMultipliesTheWholeHit")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FScopedCombatant Defender(World);

		// 200% is double. A round multiplier makes the reading arithmetic rather
		// than approximate, and 200 is a real figure: the Brute, the Succubus and
		// the Abyssal Warden all carry it in game/Data/EnemyArchetypes.csv.
		const FCataclysmIncomingHit Hit = HitThatCanCrit(1'000.0f, 25.0f, 200.0f);

		const FCataclysmDamageResult Crit =
			UCataclysmDamageCalculation::Resolve(
				Hit, Defender.AbilitySystem, /*Tier=*/1,
				/*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f, /*CritRoll=*/0.0f);

		TestTrue(TEXT("a roll of 0 against a 25% chance critically strikes"),
			Crit.bWasCritical);
		TestEqual(TEXT("and the whole hit is doubled"),
			Crit.DealtToHealth, 2'000.0f, 0.01f);

		// AT THE CHANCE RATHER THAN UNDER IT IS A MISS, which is what makes a
		// chance of 0 mean never rather than sometimes.
		const FCataclysmDamageResult Ordinary =
			UCataclysmDamageCalculation::Resolve(
				Hit, Defender.AbilitySystem, /*Tier=*/1,
				/*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f, /*CritRoll=*/25.0f);

		TestFalse(TEXT("a roll of 25 against a 25% chance does not"),
			Ordinary.bWasCritical);
		TestEqual(TEXT("and the hit is left alone"),
			Ordinary.DealtToHealth, 1'000.0f, 0.01f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmNoChanceNeverCritsTest,
	"Cataclysm.Crit.AChanceOfZeroNeverCriticallyStrikes")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FScopedCombatant Defender(World);

		// ZERO IS THE DEFAULT ON THE STRUCT and it has to mean never, because it
		// is what a damage over time tick and a minion's blow are given. A roll of
		// 0 is the most favourable roll there is; if any chance could beat it,
		// this is where it would show.
		const FCataclysmIncomingHit Hit = HitThatCanCrit(1'000.0f, 0.0f, 200.0f);

		const FCataclysmDamageResult Outcome =
			UCataclysmDamageCalculation::Resolve(
				Hit, Defender.AbilitySystem, /*Tier=*/1,
				/*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f, /*CritRoll=*/0.0f);

		TestFalse(TEXT("no chance means no critical strike"), Outcome.bWasCritical);
		TestEqual(TEXT("and the hit is untouched"),
			Outcome.DealtToHealth, 1'000.0f, 0.01f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmEvadedHitIsNeverCriticalTest,
	"Cataclysm.Crit.AnEvadedHitIsNeverReportedAsACriticalStrike")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FScopedCombatant Defender(World);
		Defender.Combat->SetEvasion(100.0f);

		// THE ROLL SITS AFTER THE EVASION STEP FOR THIS REASON AND NO OTHER. Every
		// step after it is a multiplication or a minimum, so moving the multiplier
		// among them does not change a single number -- but a hit that never
		// landed being called a critical strike would put an exclamation mark on
		// the word "Evaded".
		const FCataclysmIncomingHit Hit = HitThatCanCrit(1'000.0f, 100.0f, 200.0f);

		const FCataclysmDamageResult Outcome =
			UCataclysmDamageCalculation::Resolve(
				Hit, Defender.AbilitySystem, /*Tier=*/1,
				/*EvasionRoll=*/0.0f, /*BlockRoll=*/100.0f, /*CritRoll=*/0.0f);

		TestTrue(TEXT("the hit was evaded"), Outcome.bEvaded);
		TestFalse(TEXT("so it is not a critical strike"), Outcome.bWasCritical);
		TestEqual(TEXT("and it dealt nothing"), Outcome.DealtToHealth, 0.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmCritIsMultipliedBeforeMitigationTest,
	"Cataclysm.Crit.TheMultiplierIsAppliedToTheHitAndNotToWhatSurvives")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		FScopedCombatant Defender(World);

		// 800 ARMOUR AT TIER 1 REMOVES EXACTLY HALF, because armour removes
		// `armor / (armor + 800 x tier)`. A round figure makes both readings
		// arithmetic.
		Defender.Combat->SetArmor(800.0f);
		Defender.Combat->SetDamageReduction(20.0f);

		const FCataclysmIncomingHit Hit = HitThatCanCrit(1'000.0f, 100.0f, 200.0f);

		const FCataclysmDamageResult Ordinary =
			UCataclysmDamageCalculation::Resolve(
				Hit, Defender.AbilitySystem, /*Tier=*/1,
				/*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f, /*CritRoll=*/100.0f);
		const FCataclysmDamageResult Crit =
			UCataclysmDamageCalculation::Resolve(
				Hit, Defender.AbilitySystem, /*Tier=*/1,
				/*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f, /*CritRoll=*/0.0f);

		// 1000 halved by armour is 500, less 20% is 400. Doubled first it is
		// 2000, halved is 1000, less 20% is 800 -- exactly twice, because every
		// step between is a multiplication and multiplication does not care about
		// order. THAT IS THE POINT: the model scales the raw hit and hands the
		// scaled figure to mitigation, and this shows the engine agrees.
		TestEqual(TEXT("an ordinary hit loses armour then flat reduction"),
			Ordinary.DealtToHealth, 400.0f, 0.01f);
		TestEqual(TEXT("a critical strike is worth exactly the multiplier more"),
			Crit.DealtToHealth, 800.0f, 0.01f);
		TestEqual(TEXT("which is the ordinary hit times the multiplier"),
			Crit.DealtToHealth, Ordinary.DealtToHealth * 2.0f, 0.01f);

		// AND IT IS STILL MITIGATED. A critical strike is not a bypass: 800 is
		// well short of the 2,000 the multiplied hit started as.
		TestTrue(TEXT("a critical strike is still cut down by the defences"),
			Crit.DealtToHealth < 2'000.0f);
	}
	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// Through a real gameplay effect, which is the wiring rather than the sums
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmCritArrivesThroughARealHitTest,
	"Cataclysm.Crit.AnAttackersChanceReachesAHitThatTravelsAsAnEffect")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FScopedCombatant Attacker(World);
		FScopedCombatant Defender(World);

		Attacker.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1'000.0f);

		// 150% IS THE DESIGN'S OWN DEFAULT, from the shared class stat line in
		// the Class Stats sheet of docs/All_Things_Cataclysm.xlsx, where no class
		// overrides it. It is also what UCataclysmCombatAttributeSet initialises
		// the attribute to.
		Attacker.SetCritical(/*Chance=*/40.0f, /*Multiplier=*/150.0f);

		// CALLING Resolve DIRECTLY WOULD NOT COVER ANY OF THIS. The chance and the
		// multiplier are read off the attacker inside
		// UCataclysmVitalAttributeSet::PostGameplayEffectExecute, from the
		// instigator on the effect's context, and a test that built the hit struct
		// itself would prove only that arithmetic works.
		{
			const FScopedCritRoll AlwaysCrits(0.0f);
			UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);
			TestEqual(TEXT("a critical strike through a real effect is worth 1.5x"),
				Defender.TakeDamageReading(), 1'500.0f, 1.0f);
		}
		{
			const FScopedCritRoll NeverCrits(100.0f);
			UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);
			TestEqual(TEXT("and an ordinary one through the same path is not"),
				Defender.TakeDamageReading(), 1'000.0f, 1.0f);
		}
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmDamageOverTimeNeverCritsTest,
	"Cataclysm.Crit.ADamageOverTimeTickNeverCriticallyStrikes")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FScopedCombatant Attacker(World);
		FScopedCombatant Defender(World);

		Attacker.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1'000.0f);
		Attacker.SetCritical(/*Chance=*/100.0f, /*Multiplier=*/200.0f);

		// THE GENRE DECIDES THIS AND THE DESIGN DOCUMENT DOES NOT. Last Epoch's
		// own manual says a damage over time effect is not a hit and so "cannot be
		// Dodged, are not affected by on hit effects, the damage is not scaled
		// randomly, nor do they deal critical strikes". Path of Exile says damage
		// over time cannot critically hit. The design agrees in shape: it gives
		// damage over time three scaling levers of its own and calls the critical
		// strike attribute the direct-hit one.
		const FScopedCritRoll AlwaysCrits(0.0f);

		FCataclysmHitDelivery Tick;
		Tick.bIsDamageOverTime = true;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), Tick);

		TestEqual(TEXT("a tick is worth its own damage and no more"),
			Defender.TakeDamageReading(), 1'000.0f, 1.0f);

		// AND THE SAME ATTACKER'S ORDINARY BLOW DOES CRITICALLY STRIKE, which is
		// what makes the reading above a rule rather than a hit that failed to
		// land for some other reason.
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);
		TestEqual(TEXT("while its ordinary blow is doubled"),
			Defender.TakeDamageReading(), 2'000.0f, 1.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmCallerCanForbidACritTest,
	"Cataclysm.Crit.ACallerCanSayAHitMayNotCriticallyStrike")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FScopedCombatant Attacker(World);
		FScopedCombatant Defender(World);

		Attacker.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1'000.0f);
		Attacker.SetCritical(/*Chance=*/100.0f, /*Multiplier=*/200.0f);

		const FScopedCritRoll AlwaysCrits(0.0f);

		// THIS IS THE MECHANISM A SUMMONED MINION USES. Its damage is dealt in its
		// summoner's name, so the attacker the engine reads a chance off is the
		// player, and the design says a minion takes neither the summoner's
		// critical strike chance nor its multiplier.
		FCataclysmHitDelivery NoCrit;
		NoCrit.bCannotCriticallyStrike = true;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), NoCrit);

		TestEqual(TEXT("a hit forbidden to critically strike does not"),
			Defender.TakeDamageReading(), 1'000.0f, 1.0f);

		// The same attacker, the same roll, without the flag.
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);
		TestEqual(TEXT("where the same blow without the flag is doubled"),
			Defender.TakeDamageReading(), 2'000.0f, 1.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmNoCritTagExistsTest,
	"Cataclysm.Crit.TheTagThatForbidsACriticalStrikeIsInTheVocabulary")
{
	// AN INVALID TAG WOULD STOP THE FLAG TRAVELLING AND FAIL NOTHING. The tag is
	// requested by name rather than declared natively, so that a name the Tags
	// sheet of docs/All_Things_Cataclysm.xlsx has lost answers with an invalid
	// tag instead of being created out of thin air. That is the right behaviour
	// and it is also silent: every test that did not look would still pass while
	// every minion quietly started critically striking.
	const FGameplayTag NoCrit = UCataclysmDamageCalculation::NoCriticalStrikeTag();

	TestTrue(TEXT("Keyword.NoCrit is a tag the vocabulary knows"),
		NoCrit.IsValid());
	TestEqual(TEXT("and it is spelled the way the generator writes it"),
		NoCrit.GetTagName().ToString(), FString(TEXT("Keyword.NoCrit")));

	return true;
}

#undef CATACLYSM_TEST

#endif  // WITH_AUTOMATION_TESTS
