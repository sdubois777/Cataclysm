// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
// For the row-to-skill half of a skill's own critical strike chance. Issue #657.
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
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
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
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

// --------------------------------------------------------------------------
// A skill's own base chance, which beats the character's. Issue #657.
//
// WHY IT IS NOT THE CHARACTER'S NUMBER. The design's stat source table names
// "the skill being used" as the source of critical strike chance, and the
// sentence after it is "A character has no critical strike chance in the
// abstract." docs/Cataclysm_GDD_v2.md lines 858 and 866. A character holds six
// skills at once and the ability system has one CritChance attribute to put them
// in, so a skill that states its own sends it with the hit instead.
//
// THE MULTIPLIER IS NOT MOVED, and that is deliberate rather than an omission.
// The design puts only the CHANCE on the skill; the multiplier stays a character
// stat and is still read off the attacker's attribute on every hit.
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmSkillCritChanceKeyExistsTest,
	"Cataclysm.Crit.TheKeyASkillsOwnChanceTravelsUnderIsInTheVocabulary")
{
	// A NUMBER CANNOT RIDE ON A TAG, so this one rides as a set-by-caller
	// magnitude, which is Unreal's map from tag to float on an effect spec. This
	// tag is that map's key and appears in no tag container, so nothing can match
	// on it by accident.
	//
	// AN INVALID KEY FAILS SILENTLY, which is why this test exists at all. The
	// tag is requested by name with ErrorIfNotFound false, so a Tags sheet that
	// lost the row returns an invalid tag, the number is never attached, every
	// skill quietly falls back to the character's attribute, and nothing reports
	// it. The same reasoning gives Keyword.NoCrit its own test above.
	const FGameplayTag Key = UCataclysmDamageCalculation::SkillCritChanceDataTag();

	TestTrue(TEXT("Data.SkillCritChance is a tag the vocabulary knows"),
		Key.IsValid());
	TestEqual(TEXT("and it is spelled the way the generator writes it"),
		Key.GetTagName().ToString(), FString(TEXT("Data.SkillCritChance")));

	return true;
}

CATACLYSM_TEST(FCataclysmSkillChanceBeatsTheCharactersTest,
	"Cataclysm.Crit.ASkillsOwnChanceIsUsedInsteadOfTheCharactersAttribute")
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

		// BOTH DIRECTIONS ARE CHECKED, because only one of them proves anything
		// on its own. A skill can raise the chance above what the character
		// carries and it can lower it below, and a test that only raised it would
		// pass against an implementation that took the larger of the two.
		{
			// The character never critically strikes. The skill always does.
			Attacker.SetCritical(/*Chance=*/0.0f, /*Multiplier=*/200.0f);
			const FScopedCritRoll RollsZero(0.0f);

			FCataclysmHitDelivery Delivery;
			Delivery.CritChancePercent = 100.0f;
			UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
											 FGameplayTagContainer(), Delivery);

			TestEqual(TEXT("a skill stating 100% critically strikes where its "
						   "character's 0% would not"),
				Defender.TakeDamageReading(), 2'000.0f, 1.0f);
		}
		{
			// The character always critically strikes. The skill never does.
			Attacker.SetCritical(/*Chance=*/100.0f, /*Multiplier=*/200.0f);
			const FScopedCritRoll RollsZero(0.0f);

			FCataclysmHitDelivery Delivery;
			Delivery.CritChancePercent = 0.0f;
			UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
											 FGameplayTagContainer(), Delivery);

			// ZERO IS A REAL ANSWER AND NOT A BLANK. This is the case the whole
			// sentinel exists for: the decision of 2026-08-04 says the 5% is a
			// default and not a floor, so a skill designed never to critically
			// strike has to be able to say so.
			TestEqual(TEXT("and a skill stating 0% does not, where its "
						   "character's 100% would"),
				Defender.TakeDamageReading(), 1'000.0f, 1.0f);
		}
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmHitStatingNoChanceUsesTheCharactersTest,
	"Cataclysm.Crit.AHitThatStatesNoChanceTakesTheCharactersAttribute")
{
	using namespace CataclysmCritTest;

	// THE CASE EVERY HIT IN THE GAME IS IN TODAY. All 398 rows of the weapon
	// skill matrix leave the Crit Chance column blank, and an enemy's attack, a
	// minion's blow and a burning patch of ground have no skill row at all. If
	// this broke, every critical strike in the game would stop.
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

		const FScopedCritRoll RollsZero(0.0f);

		// -1 IS WHAT A DEFAULT-CONSTRUCTED DELIVERY CARRIES, so this is also the
		// behaviour of every caller that says nothing about critical strikes.
		FCataclysmHitDelivery SaysNothing;
		TestEqual(TEXT("a delivery states no chance by default"),
			SaysNothing.CritChancePercent, -1.0f);

		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), SaysNothing);
		TestEqual(TEXT("so the hit takes the character's own chance"),
			Defender.TakeDamageReading(), 2'000.0f, 1.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPersonalCapBindsTheAttributeTest,
	"Cataclysm.Crit.ACharactersOwnCeilingBoundsItsCriticalStrikeChance")
{
	using namespace CataclysmCritTest;

	// ONE ENCHANTMENT LOWERS A CHARACTER'S CEILING BELOW 100%. The Enchantments
	// sheet of docs/All_Things_Cataclysm.xlsx carries "Your critical hit chance
	// cannot exceed 30%-50%" as a downside, and the cap was a single constant
	// shared by everyone, so it had nowhere to live. Issue #680.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		const FScopedCombatant Character(World);

		TestEqual(TEXT("a character starts at the shared ceiling"),
			Character.Combat->GetMaxCritChance(),
			UCataclysmCombatAttributeSet::CritChanceCap);

		// Below its ceiling, the chance is what it says.
		Character.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 40.0f);
		TestEqual(TEXT("under the ceiling it is worth what it says"),
			Character.Combat->GetCritChance(), 40.0f);

		// Lower the ceiling, then try to exceed it.
		Character.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetMaxCritChanceAttribute(), 30.0f);
		Character.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 80.0f);
		TestEqual(TEXT("a lowered ceiling holds the chance down"),
			Character.Combat->GetCritChance(), 30.0f);

		// AND THE CEILING ITSELF CANNOT BE RAISED. The project owner ruled on
		// 2026-08-17 that nothing raises the cap, which is the opposite of
		// maximum resistance, where one enchantment raises it to 90%.
		Character.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetMaxCritChanceAttribute(), 150.0f);
		TestEqual(TEXT("and the ceiling itself cannot be raised past the cap"),
			Character.Combat->GetMaxCritChance(),
			UCataclysmCombatAttributeSet::CritChanceCap);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPersonalCapBindsASkillsStatedChanceTest,
	"Cataclysm.Crit.ACharactersOwnCeilingBoundsASkillsStatedChance")
{
	using namespace CataclysmCritTest;

	// THE SECOND ROUTE, AND THE ONE THAT WOULD HAVE LEAKED. Since issue #657 a
	// skill can state its own base chance, and that figure travels with the hit
	// rather than being written onto the character, so it never passes through
	// the clamp on the attribute. Without a second bound, a skill stating 100% on
	// a character an enchantment has capped at 30% would critically strike every
	// time. Issue #680.
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
		Attacker.SetCritical(/*Chance=*/0.0f, /*Multiplier=*/200.0f);

		// An enchantment has capped this character at 30%.
		Attacker.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetMaxCritChanceAttribute(), 30.0f);

		// A ROLL BETWEEN THE TWO FIGURES IS WHAT MAKES THIS A TEST. At 50, a
		// chance of 100 would critically strike and a chance of 30 would not, so
		// the reading says which figure was used rather than only that something
		// changed.
		const FScopedCritRoll RollsFifty(50.0f);

		FCataclysmHitDelivery Delivery;
		Delivery.CritChancePercent = 100.0f;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), Delivery);

		TestEqual(TEXT("a skill stating 100% is held to its character's 30%"),
			Defender.TakeDamageReading(), 1'000.0f, 1.0f);

		// AND WITHOUT THE ENCHANTMENT THE SAME SKILL DOES CRITICALLY STRIKE,
		// which is what makes the reading above the ceiling doing its job rather
		// than the stated chance having stopped working.
		Attacker.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetMaxCritChanceAttribute(),
			UCataclysmCombatAttributeSet::CritChanceCap);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), Delivery);
		TestEqual(TEXT("where the same skill on an uncapped character does"),
			Defender.TakeDamageReading(), 2'000.0f, 1.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmSkillRowCarriesItsChanceTest,
	"Cataclysm.Crit.ASkillRowsOwnCriticalStrikeChanceReachesTheSkill")
{
	// THE DATA HALF OF THE CHAIN, from a row of the weapon skill matrix to the
	// skill a character is granted. Built from a CSV string through the real row
	// struct, the same way Cataclysm.Data.EveryGeneratedTableImports checks the
	// shipped tables, so the column name and its type are both exercised rather
	// than assumed.
	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FCataclysmWeaponSkillRow::StaticStruct();

	// Two rows differing in one cell: one states a chance, one states nothing.
	const FString Csv = TEXT(
		// THE THREE FIGURES AT THE END ARE THE SKILL'S OWN DAMAGE, COOLDOWN
		// AND MANA COST, added by issue #836 when a slot became a key. Each is
		// -1 here, meaning the row says nothing, because this test is about the
		// critical strike chance beside them. A column missing from this string
		// is reported by CreateTableFromCSVString rather than ignored, which is
		// what makes this notice a row struct that grew.
		"Name,WeaponType,DamageType,Slot,SkillName,SkillDescription,Tags,Shape,ShapeParams,CritChancePercent,DamagePercent,Cooldown,ManaCost\r\n"
		"War_Sword_Heavy,Sword,War,Heavy,Precise Cut,Cuts.,,,,20,-1,-1,-1\r\n"
		"War_Sword_Special,Sword,War,Special,Wild Swing,Swings.,,,,-1,-1,-1,-1\r\n");

	const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
	if (!TestEqual(TEXT("the CSV imports with no problems"), Problems.Num(), 0))
	{
		for (const FString& Problem : Problems)
		{
			AddError(Problem);
		}
		return false;
	}

	const TArray<FCataclysmWeaponSkill> Skills =
		UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Sword"), TEXT("War"));

	if (!TestEqual(TEXT("both rows became skills"), Skills.Num(), 2))
	{
		return false;
	}

	const FCataclysmWeaponSkill* Stated = Skills.FindByPredicate(
		[](const FCataclysmWeaponSkill& S) { return S.Name == TEXT("Precise Cut"); });
	const FCataclysmWeaponSkill* Silent = Skills.FindByPredicate(
		[](const FCataclysmWeaponSkill& S) { return S.Name == TEXT("Wild Swing"); });

	if (!TestNotNull(TEXT("the row that states a chance"), Stated)
		|| !TestNotNull(TEXT("the row that states none"), Silent))
	{
		return false;
	}

	TestEqual(TEXT("a row stating 20% produces a skill of 20%"),
		Stated->CritChancePercent, 20.0f);

	// CARRIED, NOT RESOLVED. Turning -1 into 5 here would put the default in a
	// second place, and the two could then disagree without anything saying so.
	// It stays -1 until the character's attribute is written.
	TestEqual(TEXT("and a row stating none stays at -1 rather than becoming 5"),
		Silent->CritChancePercent, -1.0f);

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
