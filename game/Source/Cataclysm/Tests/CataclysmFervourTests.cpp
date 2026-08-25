// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "Character/CataclysmPassiveTree.h"
// For FCataclysmPassiveEffectRow and FCataclysmPassiveNodeRow.
#include "Data/CataclysmDataRows.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "Misc/ScopeExit.h"

/**
 * What fills Fervour and what empties it. Issue #954.
 *
 * WHY THESE MATTER MORE THAN THE USUAL. Before this change nothing in the game
 * moved the resource at all: the pool existed, its maximum was written from the
 * class stat line, and no other line of code touched either number. So 23 of the
 * Masochist's 74 nodes named a resource that was permanently zero, and nothing
 * anywhere reported that.
 *
 * THE ARITHMETIC IS SEPARATE FROM THE PLUMBING AND IS TESTED SEPARATELY.
 * `FervourFor` takes three floats, so every edge of the rule can be checked
 * without a world. The rest needs an ability system, and those tests build the
 * smallest one that carries a Fervour pool and a health pool.
 */

namespace CataclysmFervourTest
{
	/**
	 * An actor able to take a real hit and hold Fervour.
	 *
	 * THE FOUR DEFENCE SETS AS WELL AS THE TWO FERVOUR NEEDS, so the same
	 * fixture serves the tests that call the Fervour functions directly and the
	 * one that lands a blow through `UCataclysmSkillEffects::ApplyHit`. A
	 * defender missing a resistance set is a different case from one that has a
	 * resistance of zero, and this is not the place to find out which.
	 */
	struct FScopedCharacter
	{
		explicit FScopedCharacter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers rather than TObjectPtr, because
			// AddAttributeSetSubobject is a template that deduces its type from
			// the argument and would deduce the wrapper.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor));

			Vitals = NewVitals;
			Resource = NewResource;
			Combat = NewCombat;

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FScopedCharacter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Give this character the Masochist's generator: 1 to all three rates. */
		void GiveTheMasochistGenerator() const
		{
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmClassResourceAttributeSet::GetFervourFromDamageAttribute(),
				1.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmClassResourceAttributeSet::GetFervourFromCostAttribute(),
				1.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmClassResourceAttributeSet::GetFervourLostToHealingAttribute(),
				1.0f);
		}

		void SetHealth(float Current, float Maximum) const
		{
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Maximum);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), Current);
		}

		void SetFervour(float Current) const
		{
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
				Current);
		}

		float Fervour() const { return Resource->GetClassResource(); }

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
		TObjectPtr<UCataclysmClassResourceAttributeSet> Resource = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
	};

	UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game,
								   /*bInformEngineOfWorld=*/false);
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The arithmetic, with no world and no ability system
// ---------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmFervourArithmeticTest,
	"Cataclysm.Fervour.OnePerPercentOfMaximumHealth")
{
	// THE RULE, IN THE DESIGN'S OWN TERMS: "1 per 1% of maximum health lost to
	// damage". A character with 500 health losing 50 has lost 10% of itself, so
	// at a rate of 1 that is 10 Fervour.
	TestEqual(TEXT("ten percent of maximum health at a rate of one is ten"),
			  UCataclysmFervour::FervourFor(50.0f, 500.0f, 1.0f), 10.0f, 0.001f);

	// THE SAME SHARE OF A DIFFERENT CHARACTER IS THE SAME FERVOUR, which is the
	// whole reason the rate is per PERCENT rather than per point of health. A
	// rate per point would make a character with more health fill more slowly
	// for the same fraction of itself spent.
	TestEqual(TEXT("ten percent of a much larger character is also ten"),
			  UCataclysmFervour::FervourFor(500.0f, 5000.0f, 1.0f), 10.0f,
			  0.001f);

	// AND THE RATE MULTIPLIES IT. Eight points in the Masochist's Open Wounds
	// node is +16% increased, so a rate of 1.16.
	TestEqual(TEXT("the same loss at a rate of 1.16 is 16% more"),
			  UCataclysmFervour::FervourFor(50.0f, 500.0f, 1.16f), 11.6f,
			  0.001f);

	// NOTHING FROM NOTHING, THREE WAYS, and each is a real case: a hit that was
	// wholly mitigated, a character with no generator, and an ability system
	// whose maximum health has not been written yet.
	TestEqual(TEXT("no health lost is no Fervour"),
			  UCataclysmFervour::FervourFor(0.0f, 500.0f, 1.0f), 0.0f);
	TestEqual(TEXT("no generator is no Fervour"),
			  UCataclysmFervour::FervourFor(50.0f, 500.0f, 0.0f), 0.0f);
	TestEqual(TEXT("no maximum health is no Fervour, and no division by zero"),
			  UCataclysmFervour::FervourFor(50.0f, 0.0f, 1.0f), 0.0f);

	return true;
}

// ---------------------------------------------------------------------------
// A character with no generator, which is every character until a point is spent
// ---------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmFervourNeedsAGeneratorTest,
	"Cataclysm.Fervour.ACharacterWithNoGeneratorGainsNone")
{
	using namespace CataclysmFervourTest;

	UWorld* World = MakeWorld();
	{
		const FScopedCharacter Character(World);
		Character.SetHealth(500.0f, 500.0f);

		// THE POOL IS THERE AND THE RATES ARE NOT. Every class has a maximum of
		// 100 -- game/Data/ClassStats.csv gives it to the shared Default line --
		// and all three rates start at zero. That combination is the ordinary
		// state of every character in the game, and it is what makes a tree's
		// generator node worth a point.
		TestEqual(TEXT("the pool exists"),
				  Character.Resource->GetMaxClassResource(), 100.0f);
		TestFalse(TEXT("and nothing can move it"),
				  UCataclysmFervour::HasAGenerator(Character.AbilitySystem));

		TestEqual(TEXT("a hundred health lost grants nothing"),
				  UCataclysmFervour::GainFromDamage(
					  Character.AbilitySystem, 100.0f, FGameplayTagContainer()),
				  0.0f);
		TestEqual(TEXT("and the bar is still empty"), Character.Fervour(), 0.0f);

		// ONE RATE IS ENOUGH TO COUNT AS A GENERATOR, because a character can
		// buy a node that only fills from damage over time and nothing else.
		Character.AbilitySystem->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetFervourFromDamageAttribute(),
			1.0f);
		TestTrue(TEXT("one rate above zero is a generator"),
				 UCataclysmFervour::HasAGenerator(Character.AbilitySystem));
	}
	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// The two ways in and the one way out
// ---------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmFervourFillsAndEmptiesTest,
	"Cataclysm.Fervour.DamageAndCostsFillItAndHealingEmptiesIt")
{
	using namespace CataclysmFervourTest;

	UWorld* World = MakeWorld();
	{
		const FScopedCharacter Character(World);
		Character.SetHealth(500.0f, 500.0f);
		Character.GiveTheMasochistGenerator();

		// FIFTY OF FIVE HUNDRED IS TEN PER CENT, SO TEN FERVOUR.
		TestEqual(TEXT("health lost to damage fills the bar"),
				  UCataclysmFervour::GainFromDamage(
					  Character.AbilitySystem, 50.0f, FGameplayTagContainer()),
				  10.0f, 0.001f);
		TestEqual(TEXT("and the bar holds it"), Character.Fervour(), 10.0f,
				  0.001f);

		// A HEALTH COST IS THE SECOND WAY IN AND IS COUNTED SEPARATELY. The tree
		// has different nodes increasing each of the two and two keystones that
		// trade one against the other, so they cannot be one number.
		TestEqual(TEXT("health spent as a cost fills it too"),
				  UCataclysmFervour::GainFromHealthCost(Character.AbilitySystem,
														25.0f),
				  5.0f, 0.001f);
		TestEqual(TEXT("so the bar now holds fifteen"), Character.Fervour(),
				  15.0f, 0.001f);

		// AND HEALING TAKES IT BACK OUT AT THE SAME RATE, which is the tension
		// the whole class is built on: a Masochist that heals loses its resource.
		TestEqual(TEXT("healing removes it, as a negative change"),
				  UCataclysmFervour::RemoveForHealing(
					  Character.AbilitySystem, 50.0f, FGameplayTagContainer()),
				  -10.0f, 0.001f);
		TestEqual(TEXT("leaving five"), Character.Fervour(), 5.0f, 0.001f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmFervourClampsTest,
	"Cataclysm.Fervour.ItStopsAtTheMaximumAndAtZero")
{
	using namespace CataclysmFervourTest;

	UWorld* World = MakeWorld();
	{
		const FScopedCharacter Character(World);
		Character.SetHealth(500.0f, 500.0f);
		Character.GiveTheMasochistGenerator();

		// WHAT REALLY LANDED, NOT WHAT WAS ASKED FOR. A character at 95 Fervour
		// losing half its health has earned 50 and can only take 5. A caller
		// reporting the amount it asked for would be reporting a lie, and two of
		// the tree's keystones are about what happens at maximum Fervour.
		Character.SetFervour(95.0f);
		TestEqual(TEXT("a gain that overflows reports only what fitted"),
				  UCataclysmFervour::GainFromDamage(
					  Character.AbilitySystem, 250.0f, FGameplayTagContainer()),
				  5.0f, 0.001f);
		TestEqual(TEXT("and the bar is full rather than over"),
				  Character.Fervour(), 100.0f, 0.001f);

		TestEqual(TEXT("a further gain at maximum moves nothing"),
				  UCataclysmFervour::GainFromDamage(
					  Character.AbilitySystem, 50.0f, FGameplayTagContainer()),
				  0.0f);

		// THE SAME AT THE BOTTOM.
		Character.SetFervour(5.0f);
		TestEqual(TEXT("healing past empty removes only what was there"),
				  UCataclysmFervour::RemoveForHealing(
					  Character.AbilitySystem, 250.0f, FGameplayTagContainer()),
				  -5.0f, 0.001f);
		TestEqual(TEXT("and the bar is empty rather than negative"),
				  Character.Fervour(), 0.0f, 0.001f);
	}
	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// Damage arrives through a real blow
// ---------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmFervourFromARealHitTest,
	"Cataclysm.Fervour.ARealBlowLandingFillsTheBar")
{
	using namespace CataclysmFervourTest;

	// WHY THIS EXISTS ALONGSIDE THE TEST ABOVE, which calls `GainFromDamage`
	// directly. Deleting the one line in
	// `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` that connects a
	// landed hit to Fervour would leave every other test in this file passing,
	// and Fervour would never move in a real fight. Project law records that
	// exact failure: a system can be fully tested and still uncovered at its one
	// real entry point.
	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	{
		const FScopedCharacter Attacker(World);
		const FScopedCharacter Defender(World);

		Attacker.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 500.0f);
		Defender.SetHealth(10'000.0f, 10'000.0f);

		// A DEFENDER WITH NO GENERATOR GAINS NOTHING, asserted first so the
		// figure below is evidence of the generator rather than of anything else
		// a hit happens to do.
		const float Ignored = UCataclysmSkillEffects::ApplyHit(
			Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
			FGameplayTagContainer(), FCataclysmHitDelivery());
		if (!TestTrue(FString::Printf(TEXT("the first hit landed (%.1f)"),
									  Ignored),
					  Ignored > 0.0f))
		{
			return false;
		}
		TestEqual(TEXT("a defender with no generator gains no Fervour"),
				  Defender.Fervour(), 0.0f);

		// AND ONE WITH A GENERATOR GAINS EXACTLY THE SHARE OF ITSELF IT LOST.
		Defender.GiveTheMasochistGenerator();
		Defender.SetHealth(10'000.0f, 10'000.0f);
		Defender.SetFervour(0.0f);

		const float Dealt = UCataclysmSkillEffects::ApplyHit(
			Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
			FGameplayTagContainer(), FCataclysmHitDelivery());
		if (!TestTrue(FString::Printf(TEXT("the second hit landed (%.1f)"),
									  Dealt),
					  Dealt > 0.0f))
		{
			return false;
		}

		// READ OFF WHAT THE HIT REALLY DID rather than assumed, because the blow
		// goes through the whole mitigation order and this test is about Fervour
		// rather than about what a hit is worth.
		const float Expected = Dealt / 10'000.0f * 100.0f;
		TestEqual(FString::Printf(
					  TEXT("a blow of %.1f on 10000 health granted %.2f Fervour"),
					  Dealt, Expected),
				  Defender.Fervour(), Expected, 0.01f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Healing arrives through the one place that refills a pool
// ---------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmFervourThroughRegenerationTest,
	"Cataclysm.Fervour.RegeneratingHealthEmptiesTheBarAndOtherPoolsDoNot")
{
	using namespace CataclysmFervourTest;

	UWorld* World = MakeWorld();
	{
		const FScopedCharacter Character(World);
		Character.GiveTheMasochistGenerator();

		// THE REAL ENTRY POINT AND NOT THE FUNCTION UNDER IT. `TopUp` is where
		// both health regeneration and life leech put health back, so a test
		// that called `RemoveForHealing` directly would go on passing with the
		// call inside `TopUp` deleted. That failure has happened in this project
		// before, on the scoped-stat work, and it is why this test exists
		// alongside the one above rather than instead of it.
		Character.SetHealth(400.0f, 500.0f);
		Character.SetFervour(50.0f);

		FGameplayTagContainer Regeneration;
		Regeneration.AddTag(UCataclysmFervour::RegenerationTag());

		UCataclysmRegeneration::TopUp(
			*Character.AbilitySystem,
			UCataclysmVitalAttributeSet::GetHealthAttribute(),
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 50.0f,
			Regeneration);

		TestEqual(TEXT("the health went in"), Character.Vitals->GetHealth(),
				  450.0f, 0.001f);
		TestEqual(TEXT("and ten per cent of maximum health came out of Fervour"),
				  Character.Fervour(), 40.0f, 0.001f);

		// MANA IS NOT HEALTH. Refilling any other pool must leave Fervour where
		// it is, and `TopUp` is shared by all three, so this is the one thing
		// that says the branch inside it is real.
		Character.AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 200.0f);
		Character.AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetManaAttribute(), 0.0f);

		UCataclysmRegeneration::TopUp(
			*Character.AbilitySystem,
			UCataclysmVitalAttributeSet::GetManaAttribute(),
			UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 100.0f,
			Regeneration);

		TestEqual(TEXT("the mana went in"), Character.Vitals->GetMana(), 100.0f,
				  0.001f);
		TestEqual(TEXT("and Fervour did not move"), Character.Fervour(), 40.0f,
				  0.001f);

		// HEALING THAT OVERFLOWED RESTORED NOTHING, SO IT REMOVES NOTHING.
		// Without this a character standing at full health would have its bar
		// drained every step by a regeneration rate that was doing nothing.
		Character.SetHealth(500.0f, 500.0f);
		UCataclysmRegeneration::TopUp(
			*Character.AbilitySystem,
			UCataclysmVitalAttributeSet::GetHealthAttribute(),
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100.0f,
			Regeneration);
		TestEqual(TEXT("a full character loses no Fervour to its regeneration"),
				  Character.Fervour(), 40.0f, 0.001f);
	}
	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// The tree's own numbers, read out of the real table
// ---------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmFervourStartingNodeTest,
	"Cataclysm.Fervour.TheMasochistStartingNodeGrantsAllThreeRates")
{
	// THE REAL TABLE. This is the join between the design and the code: the node
	// says what it does in a sentence and `game/Data/PassiveEffects.csv` says it
	// in numbers, and nothing at run time reports the two disagreeing.
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const FName Start(TEXT("Masochist_basic_spine_000"));

	// THREE ROWS ON ONE NODE, which is what issue #953 made possible and what
	// this node needed it for. Before that a node could grant exactly one stat
	// and this generator could not be written down at all.
	const TArray<const FCataclysmPassiveEffectRow*> Rows =
		UCataclysmPassiveTree::EffectsFor(EffectTable, Start);
	if (!TestEqual(TEXT("the starting node grants three things"), Rows.Num(), 3))
	{
		return false;
	}

	TMap<FString, const FCataclysmPassiveEffectRow*> ByStat;
	for (const FCataclysmPassiveEffectRow* Row : Rows)
	{
		ByStat.Add(Row->Stat, Row);
	}

	const TCHAR* Expected[] = {
		UCataclysmFervour::FromDamageStat,
		UCataclysmFervour::FromCostStat,
		UCataclysmFervour::LostToHealingStat,
	};

	for (const TCHAR* Stat : Expected)
	{
		const FCataclysmPassiveEffectRow** Found = ByStat.Find(FString(Stat));
		if (!TestNotNull(*FString::Printf(TEXT("the node grants %s"), Stat),
						 Found ? *Found : nullptr))
		{
			continue;
		}

		// FLAT AND EXACTLY ONE, because the rate has no base to increase: every
		// class starts at zero and the generator node is what supplies it. An
		// `increased` row here would multiply zero and grant nothing, silently.
		TestEqual(*FString::Printf(TEXT("%s is flat"), Stat),
				  (*Found)->ValueKind, FString(TEXT("flat")));
		TestEqual(*FString::Printf(TEXT("%s is worth one per point"), Stat),
				  (*Found)->ValuePerPoint, 1.0f);
	}

	// AND THE NODE HOLDS ONE POINT, so "1 per point" is a rate of exactly 1
	// rather than something a player can stack.
	const FCataclysmPassiveNodeRow* Node =
		UCataclysmPassiveTree::FindNode(NodeTable, Start);
	if (TestNotNull(TEXT("the starting node exists"), Node))
	{
		TestEqual(TEXT("and holds a single point"), Node->MaxPoints, 1);
	}

	return true;
}

CATACLYSM_TEST(FCataclysmFervourScopedByDamageOverTimeTest,
	"Cataclysm.Fervour.ANodeScopedToDamageOverTimeAppliesOnlyToThoseTicks")
{
	using namespace CataclysmFervourTest;

	UWorld* World = MakeWorld();
	{
		const FScopedCharacter Character(World);
		Character.SetHealth(1000.0f, 1000.0f);
		Character.GiveTheMasochistGenerator();

		// THE MASOCHIST'S SHARED AGONY NODE, held at its full eight points:
		// "+2% increased Fervour gained from health lost to damage over time per
		// point", so +16% and only on a tick.
		//
		// WHY IT IS WORTH A TEST OF ITS OWN. A scoped modifier is dropped
		// entirely unless the tags of the thing that caused the change are
		// handed to the pipeline, which is issue #943's whole subject. Reading
		// the gameplay attribute instead would give the unscoped answer and
		// nothing would report the difference.
		FCataclysmStatInputs Inputs;
		Inputs.Base = 1.0f;

		FCataclysmStatModifier Scoped;
		Scoped.Bucket = ECataclysmStatBucket::Increased;
		Scoped.Source = ECataclysmModifierSource::PassiveKeystone;
		Scoped.Value = 16.0f;
		Scoped.RequiredTags.AddTag(
			UCataclysmDamageCalculation::DamageOverTimeTag());
		Inputs.Modifiers.Add(Scoped);

		TMap<FName, FCataclysmStatInputs> Stats;
		Stats.Add(FName(UCataclysmFervour::FromDamageStat), Inputs);
		Character.AbilitySystem->SetStatInputs(MoveTemp(Stats));

		FGameplayTagContainer OverTime;
		OverTime.AddTag(UCataclysmDamageCalculation::DamageOverTimeTag());

		// A HUNDRED OF A THOUSAND IS TEN PER CENT. At a rate of 1 that is 10, and
		// at 1.16 it is 11.6.
		TestEqual(TEXT("an ordinary blow gets the unscoped rate"),
				  UCataclysmFervour::GainFromDamage(
					  Character.AbilitySystem, 100.0f, FGameplayTagContainer()),
				  10.0f, 0.001f);

		Character.SetFervour(0.0f);
		TestEqual(TEXT("a damage over time tick gets sixteen per cent more"),
				  UCataclysmFervour::GainFromDamage(Character.AbilitySystem,
													100.0f, OverTime),
				  11.6f, 0.001f);
	}
	World->DestroyWorld(false);
	return true;
}

#undef CATACLYSM_TEST

#endif  // WITH_AUTOMATION_TESTS
