// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmDebuffs.h"
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
// For a creature that can be a boss, which is what a target-side
// condition asks about. Issue #1982.
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Items/CataclysmItem.h"
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

	/**
	 * A creature on the monsters' side, to be struck. Issue #1982.
	 *
	 * ITS OWN NAME, because this module is built as a unity blob and a second
	 * helper spelled the same as one in a neighbouring file would collide.
	 *
	 * AN ENEMY CHARACTER AND NOT A BARE ACTOR, because whether a target is a
	 * boss is read off `ACataclysmEnemyCharacter::IsBoss`, so a plain actor
	 * could never answer the question this test asks.
	 */
	ACataclysmEnemyCharacter* SpawnCritCreature(UWorld* World,
											   const FVector& Where,
											   float Health)
	{
		ACataclysmEnemyCharacter* Spawned =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where,
													   FRotator::ZeroRotator);
		if (Spawned)
		{
			Spawned->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Spawned->SetHealth(Health);

			// EVASION TAKEN TO NOTHING, SAID OUTRIGHT RATHER THAN INHERITED.
			// It already initialises to nought, so this changes no behaviour --
			// it states that the test does not depend on that default. An
			// evaded blow deals nothing, and a test comparing two blows would
			// then turn on a die roll rather than on the stat it is about.
			if (UAbilitySystemComponent* System =
					Spawned->GetAbilitySystemComponent())
			{
				System->SetNumericAttributeBase(
					UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			}
		}
		return Spawned;
	}

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

CATACLYSM_TEST(FCataclysmCritChanceFollowsAHealthConditionTest,
	"Cataclysm.Crit.ACriticalStrikeChanceThatDependsOnHealthReachesARealHit")
{
	using namespace CataclysmCritTest;

	// THE MASOCHIST'S LAST STAND NODE: "While at or below 20% health, +3%
	// increased Critical Strike Chance per point", held at its full eight
	// points. Issue #959.
	//
	// WHY IT HAS TO BE A REAL HIT. The chance is read off the attacker inside
	// `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`, and until this
	// issue it was read straight off the gameplay attribute -- which by design
	// carries no bonus that depends on the character's state. Every test in
	// CataclysmStatPipelineTests.cpp would go on passing with that read put
	// back, and a wounded Masochist would simply never gain the chance.
	UWorld* World = MakeWorldThatHasBegunPlay();
	{
		FScopedCombatant Attacker(World);
		FScopedCombatant Defender(World);

		Attacker.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1'000.0f);
		Attacker.SetCritical(/*Chance=*/0.0f, /*Multiplier=*/150.0f);

		// A HUNDRED HEALTH OUT OF A HUNDRED ON THE ATTACKER, so the share is the
		// figure itself. The defender keeps its own large pool.
		Attacker.Vitals->SetMaxHealth(100.0f);
		Attacker.Vitals->SetHealth(100.0f);

		// THE STAT'S INPUTS AS `UCataclysmPlayerClassStats::ApplyTo` WOULD LEAVE
		// THEM. A base of nothing and 24 percentage points that apply only under
		// a fifth of health, so the whole chance is the condition's doing and a
		// hit that critically strikes proves the condition held.
		FCataclysmStatInputs Inputs;
		Inputs.Base = 0.0f;

		FCataclysmStatModifier Conditional;
		Conditional.Bucket = ECataclysmStatBucket::Flat;
		Conditional.Source = ECataclysmModifierSource::PassiveKeystone;
		Conditional.Value = 100.0f;
		Conditional.Condition = ECataclysmStatCondition::HealthAtOrBelowPercent;
		Conditional.ConditionValue = 20.0f;
		Inputs.Modifiers.Add(Conditional);

		TMap<FName, FCataclysmStatInputs> Stats;
		Stats.Add(FName(TEXT("crit_chance")), Inputs);
		Attacker.AbilitySystem->SetStatInputs(MoveTemp(Stats));

		// THE ROLL IS PINNED AT ZERO THROUGHOUT, so the only thing that decides
		// whether a hit critically strikes is the chance: zero never beats a roll
		// of zero and a hundred always does.
		const FScopedCritRoll AlwaysCritsIfItCan(0.0f);

		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);
		const float AtFullHealth = Defender.TakeDamageReading();

		// A WOUNDED ATTACKER, AND NOTHING ELSE CHANGED.
		Attacker.Vitals->SetHealth(15.0f);

		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);
		const float AtLowHealth = Defender.TakeDamageReading();

		if (!TestTrue(FString::Printf(TEXT("both hits landed (%.0f, %.0f)"),
									  AtFullHealth, AtLowHealth),
					  AtFullHealth > 0.0f && AtLowHealth > 0.0f))
		{
			World->DestroyWorld(false);
			return false;
		}

		// THE RATIO IS THE WHOLE ASSERTION. 1.5 is the multiplier, so it says the
		// wounded hit critically struck and the healthy one did not. If both had,
		// or neither had, the ratio would be 1.
		TestEqual(TEXT("the wounded attacker critically strikes and the healthy "
					   "one does not, so the hit is 1.5 times as large"),
			AtLowHealth / AtFullHealth, 1.5f, 0.01f);
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
// abstract." Both are in docs/Cataclysm_GDD_v2.md: the table row, and the
// "Critical strike chance belongs to the skill, not the character" paragraph
// under it. A character holds six
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

CATACLYSM_TEST(FCataclysmCritMultiplierFollowsASkillTagTest,
	"Cataclysm.Crit.ACriticalStrikeMultiplierScopedToATagReachesOnlyThatSkill")
{
	using namespace CataclysmCritTest;

	// THE ROW THIS IS FOR: "Spell critical strikes deal 50%-100% increased
	// damage", one of the enchantments surveyed on #1642. It cannot be written
	// while the multiplier is read off the gameplay attribute, because that
	// attribute is worked out with no skill in hand and a modifier requiring a
	// tag is missing from it.
	//
	// WHY IT HAS TO BE A REAL HIT, which is the reason the health-conditioned
	// chance test above gives for itself. The multiplier is read inside
	// `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`. Every test in
	// CataclysmStatPipelineTests.cpp would go on passing with the attribute read
	// put back, and a spell build would simply never gain the bonus.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	{
		FScopedCombatant Attacker(World);
		FScopedCombatant Defender(World);

		Attacker.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1'000.0f);

		// ALWAYS CRITS, SO THE ONLY THING THAT VARIES IS THE MULTIPLIER. The
		// chance is out of the question entirely and any difference between the
		// two hits is the multiplier's doing.
		Attacker.SetCritical(/*Chance=*/100.0f, /*Multiplier=*/150.0f);
		const FScopedCritRoll AlwaysCrits(0.0f);

		// THE TAG IS READ OUT OF THE VOCABULARY RATHER THAN TYPED AS A STRING
		// HERE. A modifier requiring a tag nothing registers is a modifier
		// scoped to the empty set, and it would read as a change that does not
		// work rather than as a test that asked the wrong question.
		const FGameplayTag SpellTag = UCataclysmSkillEffects::SpellTag();
		if (!TestTrue(TEXT("Type.Spell is a registered tag"), SpellTag.IsValid()))
		{
			World->DestroyWorld(false);
			return false;
		}
		FGameplayTagContainer SpellTags;
		SpellTags.AddTag(SpellTag);

		// THE STAT'S INPUTS AS `ApplyTo` WOULD LEAVE THEM. A base of 150, which
		// is what the attribute holds, and 150 more that only a spell may have.
		// So a spell doubles what any other skill gets and the ratio is exactly
		// two.
		FCataclysmStatInputs Inputs;
		Inputs.Base = 150.0f;

		FCataclysmStatModifier SpellOnly;
		SpellOnly.Bucket = ECataclysmStatBucket::Flat;
		SpellOnly.Source = ECataclysmModifierSource::Enchantment;
		SpellOnly.Value = 150.0f;
		SpellOnly.RequiredTags.AddTag(SpellTag);
		Inputs.Modifiers.Add(SpellOnly);

		TMap<FName, FCataclysmStatInputs> Stats;
		Stats.Add(FName(TEXT("crit_multiplier")), Inputs);
		Attacker.AbilitySystem->SetStatInputs(MoveTemp(Stats));

		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer());
		const float Unscoped = Defender.TakeDamageReading();

		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 SpellTags);
		const float Spell = Defender.TakeDamageReading();

		if (!TestTrue(FString::Printf(TEXT("both hits landed (%.0f, %.0f)"),
									  Unscoped, Spell),
					  Unscoped > 0.0f && Spell > 0.0f))
		{
			World->DestroyWorld(false);
			return false;
		}

		// THE UNSCOPED HIT IS ASSERTED AS A NUMBER AND NOT ONLY AS THE SMALLER
		// OF THE TWO. 1,000 of attack damage at a multiplier of 150 is 1,500, so
		// this says the scoped modifier did not leak into a skill that does not
		// carry the tag -- a ratio alone would be satisfied by both hits being
		// wrong in the same proportion.
		TestEqual(TEXT("a skill without the tag takes the base multiplier only"),
				  Unscoped, 1'500.0f, 1.0f);

		TestEqual(TEXT("and a spell takes the scoped one as well, so its "
					   "critical strike is twice as large"),
				  Spell / Unscoped, 2.0f, 0.01f);
	}
	World->DestroyWorld(false);
	return true;
}


// --------------------------------------------------------------------------
// A critical strike row that asks about what is being struck
// --------------------------------------------------------------------------

/**
 * A critical strike row conditioned on the TARGET reaches the target. Issue #1982.
 *
 * WHAT WAS WRONG. Both critical strike stats are asked for through
 * `StatForSkill` on the attacker's own ability system, which is right, but with
 * THREE arguments. `Target` is the ninth and defaults to null, so
 * `WithTargetState` had nothing to read and every target-side condition on a
 * critical strike stat answered false. "Critical strike chance is increased by
 * 20%-40% against Boss enemies" is the enchantment sentence that wanted it; a
 * row written for it would have been accepted by every check, shipped, and
 * granted nothing.
 *
 * THE ROLL IS PINNED, so nothing here is probabilistic. At a pinned roll of
 * nought every chance above nought strikes critically, and a chance of exactly
 * nought cannot, because the calculation guards on `CritChance > 0`. So the two
 * halves are decided by the stat rather than by chance.
 *
 * TWO TARGETS, ONE ATTACKER, AND THE TIGHTEST PAIR THE LADDER ALLOWS. A boss and
 * the rung directly below it, so an implementation reading any rarity at all
 * would fail rather than pass.
 *
 * THE CONTROL IS THE DEFENDER'S HALF OF THE SAME QUESTION. `opponent_is_boss`
 * reads the blow record, which an attacker's own lookup never fills, so it must
 * grant NOTHING here. Without it this test would pass against a build that
 * answered every boss question from whichever field happened to be filled.
 */
CATACLYSM_TEST(FCataclysmCritAsksAboutTheTargetTest,
	"Cataclysm.Crit.ACriticalStrikeRowCanAskAboutTheCharacterBeingStruck")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// EVERY HIT THAT HAS ANY CHANCE AT ALL STRIKES CRITICALLY.
	const FScopedCritRoll Pinned(0.0f);

	FScopedCombatant Attacker(World);

	// ARMED, BECAUSE THIS HELPER DOES NOT ARM ITS OWN. A combatant built here
	// carries every attribute set and no weapon damage, so without this every
	// blow below would deal nothing and both halves would read the same.
	Attacker.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	ACataclysmEnemyCharacter* Boss =
		SpawnCritCreature(World, FVector(200.0f, 0.0f, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* Ordinary =
		SpawnCritCreature(World, FVector(0.0f, 200.0f, 0.0f), 1'000'000.0f);
	if (!TestNotNull(TEXT("a boss to strike"), Boss)
		|| !TestNotNull(TEXT("an ordinary creature to strike"), Ordinary))
	{
		return false;
	}

	Boss->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep);
	Ordinary->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep - 1);
	if (!TestTrue(TEXT("the boss is a boss"), Boss->IsBoss())
		|| !TestFalse(TEXT("and the other one is not"), Ordinary->IsBoss()))
	{
		return false;
	}

	const auto Strike = [&](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f,
										 FGameplayTagContainer(),
										 FCataclysmHitDelivery(), &Resolved);
		return Resolved;
	};

	/** A stat line carrying one conditioned modifier and nothing else. */
	const auto GiveLine = [&](const TCHAR* Stat, float Base,
							  ECataclysmStatBucket Bucket, float Value,
							  ECataclysmStatCondition Condition)
	{
		FCataclysmStatModifier Conditional;
		Conditional.Bucket = Bucket;
		Conditional.Source = ECataclysmModifierSource::Enchantment;
		Conditional.Value = Value;
		Conditional.Condition = Condition;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
		Line.Base = Base;
		Line.Modifiers = {Conditional};
		Attacker.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	};

	// THE CHANCE: nothing at all, plus fifty against a boss. A pinned roll of
	// nought beats fifty and cannot beat nothing.
	GiveLine(TEXT("crit_chance"), /*Base=*/0.0f, ECataclysmStatBucket::Flat,
			 50.0f, ECataclysmStatCondition::TargetIsBoss);

	const FCataclysmDamageResult AgainstBoss = Strike(Boss);
	const FCataclysmDamageResult AgainstOrdinary = Strike(Ordinary);

	TestTrue(TEXT("both blows landed"),
			 AgainstBoss.DealtToHealth > 0.0f
				 && AgainstOrdinary.DealtToHealth > 0.0f);
	TestTrue(TEXT("the blow against the boss critically struck"),
			 AgainstBoss.bWasCritical);
	TestFalse(TEXT("and the blow against the ordinary creature did not"),
			  AgainstOrdinary.bWasCritical);
	TestTrue(*FString::Printf(
				 TEXT("so the boss took more: %.2f against %.2f"),
				 AgainstBoss.DealtToHealth, AgainstOrdinary.DealtToHealth),
			 AgainstBoss.DealtToHealth > AgainstOrdinary.DealtToHealth + 0.01f);

	// THE CONTROL. The defender's half of the same question on an attacker's own
	// row reads a field this lookup never fills, so it must grant nothing and
	// neither blow may critically strike.
	GiveLine(TEXT("crit_chance"), /*Base=*/0.0f, ECataclysmStatBucket::Flat,
			 50.0f, ECataclysmStatCondition::OpponentIsBoss);

	TestFalse(TEXT("the opponent half grants no chance against a boss"),
			  Strike(Boss).bWasCritical);
	TestFalse(TEXT("nor against anything else"),
			  Strike(Ordinary).bWasCritical);

	// AND THE MULTIPLIER IS ASKED THE SAME WAY, which is the second lookup this
	// change touches. Both blows critically strike here, so the only difference
	// between them is the conditioned multiplier.
	{
		FCataclysmStatModifier Always;
		Always.Bucket = ECataclysmStatBucket::Flat;
		Always.Source = ECataclysmModifierSource::Enchantment;
		Always.Value = 50.0f;

		FCataclysmStatModifier AgainstABoss;
		AgainstABoss.Bucket = ECataclysmStatBucket::Increased;
		AgainstABoss.Source = ECataclysmModifierSource::Enchantment;
		AgainstABoss.Value = 100.0f;
		AgainstABoss.Condition = ECataclysmStatCondition::TargetIsBoss;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Chance =
			Inputs.FindOrAdd(FName(TEXT("crit_chance")));
		Chance.Base = 0.0f;
		Chance.Modifiers = {Always};

		FCataclysmStatInputs& Multiplier =
			Inputs.FindOrAdd(FName(TEXT("crit_multiplier")));
		Multiplier.Base = 150.0f;
		Multiplier.Modifiers = {AgainstABoss};

		Attacker.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	}

	const FCataclysmDamageResult BossCritical = Strike(Boss);
	const FCataclysmDamageResult OrdinaryCritical = Strike(Ordinary);
	TestTrue(TEXT("both blows critically struck this time"),
			 BossCritical.bWasCritical && OrdinaryCritical.bWasCritical);
	TestTrue(*FString::Printf(
				 TEXT("and the conditioned multiplier reached only the boss: "
					  "%.2f against %.2f"),
				 BossCritical.DealtToHealth, OrdinaryCritical.DealtToHealth),
			 BossCritical.DealtToHealth > OrdinaryCritical.DealtToHealth + 0.01f);

	return true;
}

// --------------------------------------------------------------------------
// Issue #1992: the attacker-side lookups hand over the whole blow
// --------------------------------------------------------------------------

namespace CataclysmCritTest
{
	/** A stat line per name, each carrying the modifiers given, on `System`. */
	static void GiveLines(UCataclysmAbilitySystemComponent* System,
						  TMap<FName, FCataclysmStatInputs>&& Lines)
	{
		System->SetStatInputs(MoveTemp(Lines));
	}

	static FCataclysmStatModifier Conditioned(ECataclysmStatBucket Bucket, float Value,
											  ECataclysmStatCondition Condition,
											  float ConditionValue = 0.0f)
	{
		FCataclysmStatModifier Made;
		Made.Bucket = Bucket;
		Made.Source = ECataclysmModifierSource::Enchantment;
		Made.Value = Value;
		Made.Condition = Condition;
		Made.ConditionValue = ConditionValue;
		return Made;
	}

	static FCataclysmDamageResult StrikeOnce(AActor* Attacker, AActor* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Attacker, Target, 100.0f,
										 FGameplayTagContainer(),
										 FCataclysmHitDelivery(), &Resolved);
		return Resolved;
	}
}

/**
 * A critical strike row can ask how far away the target is and whether it is
 * staggered. Issue #1992.
 *
 * WHAT WAS WRONG. Both critical strike lookups handed over the character being
 * struck (issue #1982) and left the distance at -1 and the stagger at false, so
 * `target_within_metres` and `target_is_staggered` on either stat granted
 * nothing, with every check passing.
 *
 * THE ROLL IS PINNED AT NOUGHT, so any chance above nought strikes critically
 * and a chance of nought cannot. The attacker is a bare actor, which stands at
 * the origin, so the two creatures are two and six metres from it.
 */
CATACLYSM_TEST(FCataclysmCritAsksDistanceAndStaggerTest,
	"Cataclysm.Crit.ACriticalStrikeRowCanAskHowFarAwayTheTargetIsAndWhetherItIsStaggered")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedCritRoll Pinned(0.0f);

	FScopedCombatant Attacker(World);
	Attacker.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	ACataclysmEnemyCharacter* Near =
		SpawnCritCreature(World, FVector(2.0f * M, 0.0f, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* Far =
		SpawnCritCreature(World, FVector(0.0f, 6.0f * M, 0.0f), 1'000'000.0f);
	if (!TestNotNull(TEXT("a creature two metres away"), Near)
		|| !TestNotNull(TEXT("and one six metres away"), Far))
	{
		return false;
	}

	// THE CHANCE: nothing, plus fifty within three metres.
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Chance = Lines.FindOrAdd(FName(TEXT("crit_chance")));
		Chance.Base = 0.0f;
		Chance.Modifiers = {Conditioned(ECataclysmStatBucket::Flat, 50.0f,
										ECataclysmStatCondition::TargetWithinMetres,
										3.0f)};
		GiveLines(Attacker.AbilitySystem, MoveTemp(Lines));
	}

	const FCataclysmDamageResult AtNear = StrikeOnce(Attacker.Actor, Near);
	const FCataclysmDamageResult AtFar = StrikeOnce(Attacker.Actor, Far);
	TestTrue(TEXT("both blows landed"),
			 AtNear.DealtToHealth > 0.0f && AtFar.DealtToHealth > 0.0f);
	TestTrue(TEXT("the blow two metres away critically struck"), AtNear.bWasCritical);
	TestFalse(TEXT("and the blow six metres away did not"), AtFar.bWasCritical);

	// THE MULTIPLIER: every blow strikes critically, and the multiplier is
	// doubled against a staggered target. Only the near creature is staggered.
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Chance = Lines.FindOrAdd(FName(TEXT("crit_chance")));
		Chance.Base = 0.0f;
		Chance.Modifiers = {Conditioned(ECataclysmStatBucket::Flat, 50.0f,
										ECataclysmStatCondition::Always)};
		FCataclysmStatInputs& Multiplier =
			Lines.FindOrAdd(FName(TEXT("crit_multiplier")));
		Multiplier.Base = 150.0f;
		Multiplier.Modifiers = {Conditioned(ECataclysmStatBucket::Increased, 100.0f,
											ECataclysmStatCondition::TargetIsStaggered)};
		GiveLines(Attacker.AbilitySystem, MoveTemp(Lines));
	}

	if (!TestTrue(TEXT("the near creature is staggered"),
				  UCataclysmSkillEffects::ApplyStagger(Attacker.Actor, Near)
					  && UCataclysmSkillEffects::IsStaggered(Near))
		|| !TestFalse(TEXT("and the far one is not"),
					  UCataclysmSkillEffects::IsStaggered(Far)))
	{
		return false;
	}

	const FCataclysmDamageResult Staggered = StrikeOnce(Attacker.Actor, Near);
	const FCataclysmDamageResult Steady = StrikeOnce(Attacker.Actor, Far);
	TestTrue(TEXT("both blows critically struck this time"),
			 Staggered.bWasCritical && Steady.bWasCritical);
	TestTrue(*FString::Printf(
				 TEXT("and the doubled multiplier reached only the staggered one: "
					  "%.2f against %.2f"),
				 Staggered.DealtToHealth, Steady.DealtToHealth),
			 Staggered.DealtToHealth > Steady.DealtToHealth + 0.01f);

	return true;
}

/**
 * An armour penetration row can ask how far away the character struck is. Issue
 * #1992.
 *
 * WHAT WAS WRONG. The armour penetration lookup handed over no target, no
 * distance and no stagger, so every row asking about the other end of the blow
 * granted nothing.
 *
 * THIS MEASURES THE DISTANCE HAND-OVER AND NOT THE TARGET. `target_within_metres`
 * reads the distance the lookup is given, so this test still passes with the
 * target broken: the first machine window's proof of the target hand-over was
 * "NOT A PROOF" against it. `...CanAskWhetherTheCharacterStruckIsABoss` below
 * measures the target. Renamed from `...CanAskAboutTheCharacterBeingStruck`,
 * which claimed both.
 *
 * TWO CREATURES WITH THE SAME HEAVY ARMOUR, two and six metres away. The row
 * ignores all of it within three metres, so the near creature takes more. The
 * roll is pinned so no blow strikes critically.
 */
CATACLYSM_TEST(FCataclysmArmourPenetrationAsksAboutTheTargetTest,
	"Cataclysm.ConditionalDamage.AnArmourPenetrationRowCanAskHowFarAwayTheCharacterStruckIs")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedCritRoll NeverCritical(100.0f);

	FScopedCombatant Attacker(World);
	Attacker.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	ACataclysmEnemyCharacter* Near =
		SpawnCritCreature(World, FVector(2.0f * M, 0.0f, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* Far =
		SpawnCritCreature(World, FVector(0.0f, 6.0f * M, 0.0f), 1'000'000.0f);
	if (!TestNotNull(TEXT("a creature two metres away"), Near)
		|| !TestNotNull(TEXT("and one six metres away"), Far))
	{
		return false;
	}
	for (ACataclysmEnemyCharacter* Creature : {Near, Far})
	{
		Creature->GetAbilitySystemComponent()->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetArmorAttribute(), 5000.0f);
	}

	TMap<FName, FCataclysmStatInputs> Lines;
	FCataclysmStatInputs& Penetration =
		Lines.FindOrAdd(FName(TEXT("armor_penetration")));
	Penetration.Base = 0.0f;
	Penetration.Modifiers = {Conditioned(ECataclysmStatBucket::Flat, 100.0f,
										 ECataclysmStatCondition::TargetWithinMetres,
										 3.0f)};
	GiveLines(Attacker.AbilitySystem, MoveTemp(Lines));

	const FCataclysmDamageResult AtNear = StrikeOnce(Attacker.Actor, Near);
	const FCataclysmDamageResult AtFar = StrikeOnce(Attacker.Actor, Far);
	TestTrue(TEXT("both blows landed"),
			 AtNear.DealtToHealth > 0.0f && AtFar.DealtToHealth > 0.0f);
	TestTrue(*FString::Printf(
				 TEXT("the near creature's armour was ignored, so it took more: "
					  "%.2f against %.2f"),
				 AtNear.DealtToHealth, AtFar.DealtToHealth),
			 AtNear.DealtToHealth > AtFar.DealtToHealth + 0.01f);

	return true;
}

/**
 * An armour penetration row can ask whether the character struck is a Boss.
 * Issue #1992, the target hand-over at the armour penetration site.
 *
 * `target_is_boss` READS ONLY THE TARGET, so this is the test that fails when
 * the lookup is handed no target -- unlike the distance test above.
 *
 * EACH CREATURE IS STRUCK BEFORE THE ROW AND AFTER IT, and compared with itself,
 * because a Boss's rarity can bring modifiers of its own: the Boss takes more
 * once the row ignores its armour, and the ordinary creature takes the same.
 */
CATACLYSM_TEST(FCataclysmArmourPenetrationAsksIfTheTargetIsABossTest,
	"Cataclysm.ConditionalDamage.AnArmourPenetrationRowCanAskWhetherTheCharacterStruckIsABoss")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedCritRoll NeverCritical(100.0f);

	FScopedCombatant Attacker(World);
	Attacker.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	ACataclysmEnemyCharacter* Boss =
		SpawnCritCreature(World, FVector(2.0f * M, 0.0f, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* Plain =
		SpawnCritCreature(World, FVector(0.0f, 2.0f * M, 0.0f), 1'000'000.0f);
	if (!TestNotNull(TEXT("a creature to make a Boss"), Boss)
		|| !TestNotNull(TEXT("and an ordinary one"), Plain))
	{
		return false;
	}
	Boss->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep);
	if (!TestTrue(TEXT("one creature is a Boss"), Boss->IsBoss())
		|| !TestFalse(TEXT("and the other is not"), Plain->IsBoss()))
	{
		return false;
	}

	// THE ARMOUR IS SET AFTER THE RARITY, which may write the starting
	// attributes again.
	for (ACataclysmEnemyCharacter* Creature : {Boss, Plain})
	{
		Creature->GetAbilitySystemComponent()->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetArmorAttribute(), 5000.0f);
	}

	const FCataclysmDamageResult BossBefore = StrikeOnce(Attacker.Actor, Boss);
	const FCataclysmDamageResult PlainBefore = StrikeOnce(Attacker.Actor, Plain);

	TMap<FName, FCataclysmStatInputs> Lines;
	FCataclysmStatInputs& Penetration =
		Lines.FindOrAdd(FName(TEXT("armor_penetration")));
	Penetration.Base = 0.0f;
	Penetration.Modifiers = {Conditioned(ECataclysmStatBucket::Flat, 100.0f,
										 ECataclysmStatCondition::TargetIsBoss)};
	GiveLines(Attacker.AbilitySystem, MoveTemp(Lines));

	const FCataclysmDamageResult BossAfter = StrikeOnce(Attacker.Actor, Boss);
	const FCataclysmDamageResult PlainAfter = StrikeOnce(Attacker.Actor, Plain);

	if (!TestTrue(TEXT("all four blows landed"),
				  BossBefore.DealtToHealth > 0.0f && PlainBefore.DealtToHealth > 0.0f
					  && BossAfter.DealtToHealth > 0.0f && PlainAfter.DealtToHealth > 0.0f))
	{
		return false;
	}
	TestTrue(*FString::Printf(
				 TEXT("the Boss's armour was ignored, so it took more than before: "
					  "%.2f against %.2f"),
				 BossAfter.DealtToHealth, BossBefore.DealtToHealth),
			 BossAfter.DealtToHealth > BossBefore.DealtToHealth + 0.01f);
	TestEqual(TEXT("and the ordinary creature took what it took before"),
			  PlainAfter.DealtToHealth, PlainBefore.DealtToHealth, 0.01f);

	return true;
}

// --------------------------------------------------------------------------
// Issue #1815: the first hit, and the first critical strike, against each enemy
// --------------------------------------------------------------------------

/**
 * A row under `target_not_yet_struck_by_you` reaches the first blow that gets
 * through to each enemy, and no later one. Issue #1815: "Your first hit against
 * each enemy deals 100%-300% bonus damage".
 *
 * THREE THINGS ARE CHECKED. The first blow on a creature is doubled and the
 * second is not. A second creature is still "not yet struck", so the record is
 * per enemy. And an EVADED blow does not use up the first hit, which is the
 * ruling that "a hit" is a blow that got through.
 */
CATACLYSM_TEST(FCataclysmFirstHitRowTest,
	"Cataclysm.ConditionalDamage.AFirstHitRowReachesOnlyTheFirstBlowThatGetsThroughToEachEnemy")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedCritRoll NeverCritical(100.0f);

	FScopedCombatant Attacker(World);
	Attacker.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	{
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Damage = Lines.FindOrAdd(FName(TEXT("attack_damage")));
		Damage.Base = 100.0f;
		Damage.Modifiers = {Conditioned(ECataclysmStatBucket::Increased, 100.0f,
										ECataclysmStatCondition::TargetNotYetStruckByYou)};
		GiveLines(Attacker.AbilitySystem, MoveTemp(Lines));
	}

	ACataclysmEnemyCharacter* First =
		SpawnCritCreature(World, FVector(2.0f * M, 0.0f, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* Second =
		SpawnCritCreature(World, FVector(0.0f, 2.0f * M, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* Evasive =
		SpawnCritCreature(World, FVector(-2.0f * M, 0.0f, 0.0f), 1'000'000.0f);
	if (!TestNotNull(TEXT("a first creature"), First)
		|| !TestNotNull(TEXT("a second"), Second)
		|| !TestNotNull(TEXT("and one that will evade"), Evasive))
	{
		return false;
	}

	// NO ARMOUR AND NO BLOCK. Armour takes a share that depends on the size of
	// the hit, and a block halves a blow on a die roll, so either would stop a
	// doubled blow coming out at exactly twice.
	for (ACataclysmEnemyCharacter* Creature : {First, Second, Evasive})
	{
		UAbilitySystemComponent* System = Creature->GetAbilitySystemComponent();
		System->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
		System->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
	}

	const FCataclysmDamageResult Opening = StrikeOnce(Attacker.Actor, First);
	const FCataclysmDamageResult Follow = StrikeOnce(Attacker.Actor, First);
	TestTrue(TEXT("both blows on the first creature landed"),
			 Opening.DealtToHealth > 0.0f && Follow.DealtToHealth > 0.0f);
	TestEqual(TEXT("the first blow is twice the second"),
			  Opening.DealtToHealth, Follow.DealtToHealth * 2.0f,
			  Follow.DealtToHealth * 0.01f);

	TestEqual(TEXT("the first blow on a second creature is doubled too"),
			  StrikeOnce(Attacker.Actor, Second).DealtToHealth,
			  Opening.DealtToHealth, Opening.DealtToHealth * 0.01f);

	// AN EVADED BLOW IS NOT A HIT. Evasion of a hundred evades, then none.
	UAbilitySystemComponent* EvasiveSystem = Evasive->GetAbilitySystemComponent();
	EvasiveSystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetEvasionAttribute(), 100.0f);
	const FCataclysmDamageResult Evaded = StrikeOnce(Attacker.Actor, Evasive);
	if (!TestTrue(TEXT("the blow on the evasive creature was evaded"),
				  Evaded.DealtToHealth <= 0.0f))
	{
		return false;
	}
	EvasiveSystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
	TestEqual(TEXT("so the first blow that gets through to it is still doubled"),
			  StrikeOnce(Attacker.Actor, Evasive).DealtToHealth,
			  Opening.DealtToHealth, Opening.DealtToHealth * 0.01f);

	return true;
}

/**
 * A row under `target_not_yet_crit_by_you` reaches the first critical strike
 * against each enemy and no later one. Issue #1815: "Your first critical strike
 * against each enemy deals an additional 50%-100% bonus damage".
 *
 * EVERY BLOW STRIKES CRITICALLY here, so the only difference between the first
 * and the second is the conditioned multiplier.
 */
CATACLYSM_TEST(FCataclysmFirstCritRowTest,
	"Cataclysm.ConditionalDamage.AFirstCriticalStrikeRowReachesOnlyTheFirstCriticalStrike")
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedCritRoll AlwaysCritical(0.0f);

	FScopedCombatant Attacker(World);
	Attacker.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	{
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Chance = Lines.FindOrAdd(FName(TEXT("crit_chance")));
		Chance.Base = 0.0f;
		Chance.Modifiers = {Conditioned(ECataclysmStatBucket::Flat, 50.0f,
										ECataclysmStatCondition::Always)};
		FCataclysmStatInputs& Multiplier =
			Lines.FindOrAdd(FName(TEXT("crit_multiplier")));
		Multiplier.Base = 150.0f;
		Multiplier.Modifiers = {Conditioned(ECataclysmStatBucket::Flat, 100.0f,
											ECataclysmStatCondition::TargetNotYetCritByYou)};
		GiveLines(Attacker.AbilitySystem, MoveTemp(Lines));
	}

	ACataclysmEnemyCharacter* Target =
		SpawnCritCreature(World, FVector(2.0f * M, 0.0f, 0.0f), 1'000'000.0f);
	if (!TestNotNull(TEXT("a creature"), Target))
	{
		return false;
	}

	const FCataclysmDamageResult Opening = StrikeOnce(Attacker.Actor, Target);
	const FCataclysmDamageResult Follow = StrikeOnce(Attacker.Actor, Target);
	TestTrue(TEXT("both blows critically struck"),
			 Opening.bWasCritical && Follow.bWasCritical);
	TestTrue(*FString::Printf(
				 TEXT("and only the first carried the extra multiplier: %.2f against %.2f"),
				 Opening.DealtToHealth, Follow.DealtToHealth),
			 Opening.DealtToHealth > Follow.DealtToHealth + 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCritPerTargetDebuffTest,
	"Cataclysm.Crit.ACriticalStrikeRowCanGrowWithTheDebuffsOnTheTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A critical strike chance row scaled by `target_debuffs` reaches the blow.
 * Issue #1815: "Each unique debuff on an enemy increases your crit chance
 * against them by 5%-10%".
 *
 * WHY THROUGH A REAL BLOW. The target's debuffs are read only when a row in the
 * lookup asks for them, and a scale asks separately from a condition. A test
 * that set the reading by hand would pass with that request deleted.
 *
 * THE ROLL IS PINNED AT NOUGHT and the chance is nothing but the row, so a
 * creature with no debuff cannot be struck critically and one bleeding is.
 */
bool FCataclysmCritPerTargetDebuffTest::RunTest(const FString&)
{
	using namespace CataclysmCritTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedCritRoll Pinned(0.0f);

	FScopedCombatant Attacker(World);
	Attacker.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	ACataclysmEnemyCharacter* Bleeding =
		SpawnCritCreature(World, FVector(2.0f * M, 0.0f, 0.0f), 1'000'000.0f);
	ACataclysmEnemyCharacter* Clean =
		SpawnCritCreature(World, FVector(0.0f, 2.0f * M, 0.0f), 1'000'000.0f);
	UAbilitySystemComponent* BleedingSystem =
		Bleeding ? Bleeding->GetAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("a creature to bleed"), BleedingSystem)
		|| !TestNotNull(TEXT("and one that does not"), Clean))
	{
		return false;
	}

	// A LOOSE TAG IS AN OWNED TAG, which is all `UCataclysmDebuffs` reads.
	BleedingSystem->AddLooseGameplayTag(UCataclysmDebuffs::BleedTag());
	if (!TestEqual(TEXT("the one creature carries one debuff"),
				   UCataclysmDebuffs::CountOnActor(Bleeding), 1)
		|| !TestEqual(TEXT("and the other none"), UCataclysmDebuffs::CountOnActor(Clean), 0))
	{
		return false;
	}

	{
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Chance = Lines.FindOrAdd(FName(TEXT("crit_chance")));
		Chance.Base = 0.0f;
		FCataclysmStatModifier PerDebuff = Conditioned(
			ECataclysmStatBucket::Flat, 50.0f, ECataclysmStatCondition::Always);
		PerDebuff.Scale = ECataclysmStatScale::PerTargetDebuff;
		PerDebuff.ScaleStep = 1.0f;
		Chance.Modifiers = {PerDebuff};
		GiveLines(Attacker.AbilitySystem, MoveTemp(Lines));
	}

	const FCataclysmDamageResult AtBleeding = StrikeOnce(Attacker.Actor, Bleeding);
	const FCataclysmDamageResult AtClean = StrikeOnce(Attacker.Actor, Clean);
	TestTrue(TEXT("both blows landed"),
			 AtBleeding.DealtToHealth > 0.0f && AtClean.DealtToHealth > 0.0f);
	TestTrue(TEXT("the blow on the bleeding creature critically struck"),
			 AtBleeding.bWasCritical);
	TestFalse(TEXT("and the blow on the clean one did not"), AtClean.bWasCritical);

	return true;
}

#undef CATACLYSM_TEST

#endif  // WITH_AUTOMATION_TESTS
