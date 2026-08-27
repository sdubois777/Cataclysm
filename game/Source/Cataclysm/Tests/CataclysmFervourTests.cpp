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
// For building an instant effect by hand, which is the second of the two routes
// that write the pool. Issue #1031.
#include "GameplayEffect.h"
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
// The ceiling, by the two routes the attribute set itself clamps
// ---------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmFervourCeilingHoldsByEveryRouteTest,
	"Cataclysm.Fervour.TheCeilingHoldsWhicheverRouteWritesThePool")
{
	using namespace CataclysmFervourTest;
	using Resource = UCataclysmClassResourceAttributeSet;

	// TWO ROUTES WRITE THE POOL AND THIS ASSERTS THE CEILING HOLDS FOR BOTH: a
	// direct write to the attribute, and an instant gameplay effect adding to
	// it. Neither had a test at all before this. Until issue #1031 the only
	// thing asserting a direct write was held was one half of
	// `Cataclysm.Passives.ApotheosisUncapsARealCharactersFervourAndChargesForIt`,
	// which established the ordinary capped case before spending points on the
	// capstone option that removed the cap. That option is gone with the rewrite
	// of all twelve Masochist capstone options, and the assertion would have
	// gone with it.
	//
	// WHICH CODE ACTUALLY HOLDS THE CEILING, MEASURED RATHER THAN ASSUMED. Three
	// places clamp the pool and a guard proof of each, run on 2026-08-27, found
	// that only ONE of the three is load-bearing for any test:
	//
	//   `PreAttributeChange` in `UCataclysmClassResourceAttributeSet`
	//        breaking it fails this test.
	//   `PostGameplayEffectExecute` in the same file
	//        breaking it fails nothing. `PreAttributeChange` is reached first on
	//        both routes below.
	//   `UCataclysmFervour::Move`
	//        breaking it fails nothing either, including the test above, which
	//        goes through it. `Move` returns the change it can MEASURE rather
	//        than the change it asked for, so the attribute set's clamp makes
	//        its answer right even with its own clamp gone.
	//
	// SO THIS TEST COVERS ONE CLAMP SITE, NOT THREE, and saying otherwise here
	// would be the kind of claim that makes a reader stop looking. Issue #1036
	// carries what to do about the other two: their comments each argue they are
	// necessary, and neither argument is currently tested.

	UWorld* World = MakeWorld();
	{
		const FScopedCharacter Character(World);
		Character.SetHealth(500.0f, 500.0f);

		const FGameplayAttribute Pool = Resource::GetClassResourceAttribute();
		const float Maximum = Character.AbilitySystem->GetNumericAttribute(
			Resource::GetMaxClassResourceAttribute());
		if (!TestTrue(TEXT("the pool has a maximum above nothing"),
					  Maximum > 0.0f))
		{
			World->DestroyWorld(false);
			return false;
		}

		// A DIRECT WRITE, WELL PAST THE TOP.
		Character.AbilitySystem->SetNumericAttributeBase(Pool, Maximum * 3.0f);
		TestEqual(TEXT("a direct write past the maximum is held at it"),
				  Character.Fervour(), Maximum, 0.01f);

		// AND WELL BELOW THE BOTTOM. Nothing anywhere says a pool may go
		// negative, and a negative reading would make every "how much Fervour do
		// you hold" question answer a number no bar can draw.
		Character.AbilitySystem->SetNumericAttributeBase(Pool, -50.0f);
		TestEqual(TEXT("and a direct write below zero is held at zero"),
				  Character.Fervour(), 0.0f, 0.01f);

		// NOW THE OTHER ROUTE: an instant gameplay effect adding to the pool,
		// which is what reaches `PostGameplayEffectExecute`.
		{
			UGameplayEffect* Effect = NewObject<UGameplayEffect>(
				GetTransientPackage(), FName(TEXT("TestFervourGain")));
			Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

			const int32 Index = Effect->Modifiers.Num();
			Effect->Modifiers.SetNum(Index + 1);
			FGameplayModifierInfo& Info = Effect->Modifiers[Index];
			Info.Attribute = Pool;
			Info.ModifierOp = EGameplayModOp::Additive;
			Info.ModifierMagnitude = FScalableFloat(Maximum * 5.0f);

			Character.AbilitySystem->ApplyGameplayEffectToSelf(
				Effect, 1.0f, Character.AbilitySystem->MakeEffectContext());
		}

		TestEqual(TEXT("and a gameplay effect adding five times the maximum "
					   "leaves the bar full rather than over"),
				  Character.Fervour(), Maximum, 0.01f);
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

// ---------------------------------------------------------------------------
// Healing that does not remove Fervour
// ---------------------------------------------------------------------------

namespace CataclysmFervourTest
{
	/**
	 * Give a character the suppression flag, scoped to one kind of healing.
	 *
	 * WRITTEN AS A STAT INPUT AND NOT AS AN ATTRIBUTE, because that is how the
	 * game gets it: `UCataclysmPlayerClassStats::ApplyTo` records a passive
	 * node's modifiers here, and a modifier with a required tag is never folded
	 * into the attribute, since the attribute is worked out with no tags in
	 * hand. Setting the attribute instead would be testing something the game
	 * never does.
	 */
	void SuppressFervourLossFor(const FScopedCharacter& Character,
								const FGameplayTag& Healing)
	{
		FCataclysmStatModifier Flag;
		Flag.Bucket = ECataclysmStatBucket::Flat;
		Flag.Source = ECataclysmModifierSource::PassiveKeystone;
		Flag.Value = 1.0f;
		Flag.RequiredTags.AddTag(Healing);

		FCataclysmStatInputs Inputs;
		Inputs.Base = 0.0f;
		Inputs.Modifiers.Add(Flag);

		TMap<FName, FCataclysmStatInputs> Stats;
		Stats.Add(FName(UCataclysmFervour::LossSuppressedStat), Inputs);
		Character.AbilitySystem->SetStatInputs(MoveTemp(Stats));
	}

	/** A container holding one tag, for a call that takes healing's tags. */
	FGameplayTagContainer Only(const FGameplayTag& Tag)
	{
		FGameplayTagContainer Container;
		Container.AddTag(Tag);
		return Container;
	}
}

CATACLYSM_TEST(FCataclysmFervourLossSuppressedTest,
	"Cataclysm.Fervour.HealingCanBeStoppedFromRemovingFervour")
{
	using namespace CataclysmFervourTest;
	using Fervour = UCataclysmFervour;

	// THE MASOCHIST'S SANGUINE LEDGER AND WOUNDS THAT FEED KEYSTONES. Issues
	// #1006 and #1007. One says "Health regeneration no longer removes Fervour"
	// and the other "Healing from Life Leech does not remove Fervour", and they
	// set the same flag with a different tag on the row.
	//
	// A FLAG RATHER THAN A REDUCTION, and this is where that matters. The stat
	// pipeline clamps a Less multiplier at -99 so that no modifier can zero a
	// stat, which is a rule worth keeping; a node that says "does not remove"
	// therefore cannot be written as a reduction of the rate at all.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCharacter Character(World);
	Character.GiveTheMasochistGenerator();
	Character.SetHealth(500.0f, 1'000.0f);
	Character.SetFervour(50.0f);

	// WITH NO SUCH NODE, HEALING REMOVES FERVOUR. Asserted first, because every
	// check below would pass just as well if healing never removed any.
	TestEqual(TEXT("100 health restored removes 10 Fervour"),
			  Fervour::RemoveForHealing(Character.AbilitySystem, 100.0f,
										Only(Fervour::RegenerationTag())),
			  -10.0f, 0.001f);
	TestEqual(TEXT("leaving 40"), Character.Fervour(), 40.0f, 0.001f);

	// NOW GIVE IT SANGUINE LEDGER: the flag, scoped to regeneration.
	SuppressFervourLossFor(Character, Fervour::RegenerationTag());

	TestEqual(TEXT("regeneration now removes nothing"),
			  Fervour::RemoveForHealing(Character.AbilitySystem, 100.0f,
										Only(Fervour::RegenerationTag())),
			  0.0f, 0.001f);
	TestEqual(TEXT("and the bar is untouched"), Character.Fervour(), 40.0f,
			  0.001f);

	// AND LEECH STILL REMOVES IT, which is the half that makes the tag do work.
	// A flag that suppressed every kind of healing would pass every assertion
	// above and would be a different node.
	TestEqual(TEXT("but leech healing still removes its 10"),
			  Fervour::RemoveForHealing(Character.AbilitySystem, 100.0f,
										Only(Fervour::LeechTag())),
			  -10.0f, 0.001f);
	TestEqual(TEXT("leaving 30"), Character.Fervour(), 30.0f, 0.001f);

	// AND THE OTHER WAY ROUND FOR WOUNDS THAT FEED.
	SuppressFervourLossFor(Character, Fervour::LeechTag());

	TestEqual(TEXT("scoped to leech, leech removes nothing"),
			  Fervour::RemoveForHealing(Character.AbilitySystem, 100.0f,
										Only(Fervour::LeechTag())),
			  0.0f, 0.001f);
	TestEqual(TEXT("while regeneration removes its 10 again"),
			  Fervour::RemoveForHealing(Character.AbilitySystem, 100.0f,
										Only(Fervour::RegenerationTag())),
			  -10.0f, 0.001f);
	TestEqual(TEXT("leaving 20"), Character.Fervour(), 20.0f, 0.001f);

	// AND NOTHING STOPS FERVOUR ARRIVING. The flag is asked about only on the
	// way out; a node meant to protect the bar must not quietly stop it filling.
	TestEqual(TEXT("and a health cost still fills it"),
			  Fervour::GainFromHealthCost(Character.AbilitySystem, 100.0f),
			  10.0f, 0.001f);
	TestEqual(TEXT("back to 30"), Character.Fervour(), 30.0f, 0.001f);

	return true;
}

CATACLYSM_TEST(FCataclysmLeechHealingSaysItIsLeechTest,
	"Cataclysm.Fervour.LeechHealingSaysThatIsWhatItIs")
{
	using namespace CataclysmFervourTest;
	using Fervour = UCataclysmFervour;

	// LEECH USED TO CARRY NO TAG AT ALL. Issue #1006. That was enough while the
	// only node asking about the source of healing was Staunch, which requires
	// the regeneration tag: leech carried nothing, so it did not match. Wounds
	// That Feed asks the other way round, and carrying nothing cannot answer it.
	//
	// WHAT THIS PROVES THAT THE TEST ABOVE DOES NOT. That
	// `UCataclysmLeech::PayOutStep` really passes the tag, rather than the rule
	// working only when a test passes it by hand.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCharacter Character(World);
	Character.GiveTheMasochistGenerator();
	Character.SetHealth(500.0f, 1'000.0f);
	Character.SetFervour(50.0f);

	// A HIT WORTH LEECHING, and a life leech rate to leech it with.
	Character.AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetLifeLeechAttribute(), 10.0f);
	UCataclysmLeech::NoteHit(Character.AbilitySystem, 1'000.0f);

	// PAID OUT OVER ITS WHOLE WINDOW, so the whole instalment lands.
	for (float Elapsed = 0.0f;
		 Elapsed < UCataclysmLeech::PayoutSeconds + 0.001f;
		 Elapsed += UCataclysmRegeneration::StepSeconds)
	{
		UCataclysmLeech::PayOutStep(Character.Actor,
									UCataclysmRegeneration::StepSeconds);
	}

	const float AfterPlainLeech = Character.Fervour();
	TestTrue(FString::Printf(TEXT("leech healing removed Fervour (%.2f of 50)"),
							 AfterPlainLeech),
			 AfterPlainLeech < 50.0f);

	// NOW SUPPRESS IT FOR LEECH, and pay a second instalment of the same size.
	SuppressFervourLossFor(Character, Fervour::LeechTag());
	Character.SetFervour(50.0f);
	Character.SetHealth(500.0f, 1'000.0f);
	UCataclysmLeech::NoteHit(Character.AbilitySystem, 1'000.0f);

	for (float Elapsed = 0.0f;
		 Elapsed < UCataclysmLeech::PayoutSeconds + 0.001f;
		 Elapsed += UCataclysmRegeneration::StepSeconds)
	{
		UCataclysmLeech::PayOutStep(Character.Actor,
									UCataclysmRegeneration::StepSeconds);
	}

	TestEqual(TEXT("and with the node it removes nothing"),
			  Character.Fervour(), 50.0f, 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// Fervour that arrives from the passage of time
// ---------------------------------------------------------------------------

namespace CataclysmFervourTest
{
	/** Give a character Low Life: Fervour a second below a health threshold. */
	void GiveLowLife(const FScopedCharacter& Character, float PerSecond,
					 float AtOrBelowPercent)
	{
		FCataclysmStatModifier Rate;
		Rate.Bucket = ECataclysmStatBucket::Flat;
		Rate.Source = ECataclysmModifierSource::PassiveKeystone;
		Rate.Value = PerSecond;
		Rate.Condition = ECataclysmStatCondition::HealthAtOrBelowPercent;
		Rate.ConditionValue = AtOrBelowPercent;

		FCataclysmStatInputs Inputs;
		Inputs.Base = 0.0f;
		Inputs.Modifiers.Add(Rate);

		TMap<FName, FCataclysmStatInputs> Stats;
		Stats.Add(FName(UCataclysmFervour::PerSecondStat), Inputs);
		Character.AbilitySystem->SetStatInputs(MoveTemp(Stats));
	}
}

CATACLYSM_TEST(FCataclysmFervourPerSecondTest,
	"Cataclysm.Fervour.FervourCanArriveEverySecondWhileHurt")
{
	using namespace CataclysmFervourTest;
	using Fervour = UCataclysmFervour;

	// THE MASOCHIST'S LOW LIFE KEYSTONE. Issue #1008: "While at or below 35%
	// health you gain 10 Fervour per second."
	//
	// THE FIRST THING THAT FILLS THE POOL WITHOUT HEALTH HAVING MOVED. Every
	// other way in and out of Fervour is driven by a health change.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCharacter Character(World);
	Character.SetFervour(0.0f);

	// A CHARACTER WITHOUT THE NODE GAINS NOTHING, however hurt it is. Without
	// this the checks below would pass just as well if every character gained
	// Fervour on every step.
	Character.SetHealth(100.0f, 1'000.0f);
	TestEqual(TEXT("a character without the node gains nothing"),
			  Fervour::GainPerSecondStep(Character.AbilitySystem, 1.0f), 0.0f,
			  0.001f);

	GiveLowLife(Character, 10.0f, 35.0f);

	// AT 10% HEALTH, WELL INSIDE THE THRESHOLD.
	TestEqual(TEXT("a whole second is ten"),
			  Fervour::GainPerSecondStep(Character.AbilitySystem, 1.0f), 10.0f,
			  0.001f);
	TestEqual(TEXT("and the bar holds ten"), Character.Fervour(), 10.0f,
			  0.001f);

	// AND A QUARTER OF A SECOND IS A QUARTER OF IT, which is the step the
	// regeneration timer really runs at.
	TestEqual(TEXT("a quarter second is two and a half"),
			  Fervour::GainPerSecondStep(Character.AbilitySystem, 0.25f), 2.5f,
			  0.001f);

	// EXACTLY ON THE THRESHOLD STILL COUNTS, because the design writes "at or
	// below".
	Character.SetHealth(350.0f, 1'000.0f);
	TestEqual(TEXT("exactly 35% still gains"),
			  Fervour::GainPerSecondStep(Character.AbilitySystem, 1.0f), 10.0f,
			  0.001f);

	// AND ABOVE IT NOTHING ARRIVES. Nothing else changed: the health moved.
	const float BeforeHealing = Character.Fervour();
	Character.SetHealth(360.0f, 1'000.0f);
	TestEqual(TEXT("just above 35% gains nothing"),
			  Fervour::GainPerSecondStep(Character.AbilitySystem, 1.0f), 0.0f,
			  0.001f);
	TestEqual(TEXT("and the bar is unchanged"), Character.Fervour(),
			  BeforeHealing, 0.001f);

	// A FULL BAR TAKES NO MORE, which is the ordinary case for a character
	// standing at low health with this keystone: ten a second fills it in ten.
	Character.SetHealth(100.0f, 1'000.0f);
	Character.SetFervour(100.0f);
	TestEqual(TEXT("a full bar takes no more"),
			  Fervour::GainPerSecondStep(Character.AbilitySystem, 1.0f), 0.0f,
			  0.001f);
	TestEqual(TEXT("and stays full"), Character.Fervour(), 100.0f, 0.001f);

	// AND A STEP OF NO TIME GRANTS NOTHING.
	Character.SetFervour(0.0f);
	TestEqual(TEXT("a step of no time grants nothing"),
			  Fervour::GainPerSecondStep(Character.AbilitySystem, 0.0f), 0.0f,
			  0.001f);

	return true;
}

#undef CATACLYSM_TEST

#endif  // WITH_AUTOMATION_TESTS
