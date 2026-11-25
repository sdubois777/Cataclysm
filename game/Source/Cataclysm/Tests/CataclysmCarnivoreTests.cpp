// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Carnivore, The Final Vow's second option, as the project owner approved it on
 * 2026-11-24: "Every hit you take grants a stack of Carnage. Carnage has no
 * maximum, each stack grants 2% more damage, and each stack lasts 1 second
 * longer for every 10 Fervour you hold."
 *
 * WHAT IS TESTED HERE IS THE NEW CLAUSE. `carnage_stack_seconds` is a flat 1
 * scaled by the Fervour held in steps of 10, asked by
 * `UCataclysmStacks::CarnageExtraSeconds` when a Carnage stack is granted and
 * kept with the count by `UCataclysmAbilitySystemComponent::GrantStack`. The
 * other three clauses are `Cataclysm.MasochistStackRows.CarnivoreGivesARealMasochistUnlimitedCarnageFromHitsTaken`.
 *
 * ONE EXPIRY FOR THE WHOLE COUNT, as every stack in the game has: the extra is
 * the last grant's, so a stack granted at 0 Fervour after one granted at 100
 * brings the count back to Carnage's own 8 seconds.
 */
namespace CataclysmCarnivoreTest
{
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	struct FCarnivoreHolder
	{
		explicit FCarnivoreHolder(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Resource::GetMaxClassResourceAttribute(), 100.0f);
			// CARNIVORE'S FIRST CLAUSE, its flag on the attribute, as its row folds it.
			AbilitySystem->SetNumericAttributeBase(Resource::GetCarnageFromDamageTakenAttribute(), 1.0f);
		}

		~FCarnivoreHolder()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** The new clause's row: a flat 1 for every 10 Fervour held. */
		void HoldTheClause() const
		{
			FCataclysmStatModifier PerTen;
			PerTen.Bucket = ECataclysmStatBucket::Flat;
			PerTen.Source = ECataclysmModifierSource::PassiveKeystone;
			PerTen.Value = 1.0f;
			PerTen.Scale = ECataclysmStatScale::PerPointOfClassResourceHeld;
			PerTen.ScaleStep = 10.0f;
			TMap<FName, FCataclysmStatInputs> Inputs;
			Inputs.FindOrAdd(FName(UCataclysmStacks::CarnageStackSecondsStat)).Modifiers = {PerTen};
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		void SetFervour(float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Resource::GetClassResourceAttribute(), Value);
		}

		int32 Carnage() const
		{
			return UCataclysmStacks::Held(AbilitySystem, ECataclysmStackKind::Carnage);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarnivoreLongerStacksTest,
	"Cataclysm.Carnivore.ACarnageStackLastsASecondLongerForEveryTenFervourHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A hit taken at 100 Fervour grants a stack that lasts 18 seconds, 8 and 10:
 * held at 17.9, gone at 18.1. The extra is decided at the grant, so emptying the
 * pool afterwards does not shorten it.
 */
bool FCataclysmCarnivoreLongerStacksTest::RunTest(const FString&)
{
	using namespace CataclysmCarnivoreTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCarnivoreHolder Holder(World);
	Holder.HoldTheClause();
	Holder.SetFervour(100.0f);
	TestEqual(TEXT("100 Fervour held is 10 seconds more"), UCataclysmStacks::CarnageExtraSeconds(Holder.AbilitySystem),
			  10.0f, 0.001f);

	UCataclysmStacks::NoteDamageTaken(Holder.AbilitySystem);
	if (!TestEqual(TEXT("set-up: a hit taken grants one stack of Carnage"), Holder.Carnage(), 1))
	{
		return false;
	}

	Holder.SetFervour(0.0f);
	World->TimeSeconds += 17.9f;
	TestEqual(TEXT("it is still held 17.9 seconds on, though the pool was emptied after it came"), Holder.Carnage(),
			  1);
	World->TimeSeconds += 0.2f;
	TestEqual(TEXT("and gone at 18.1"), Holder.Carnage(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarnivoreLastGrantDecidesTest,
	"Cataclysm.Carnivore.TheLastStackGrantedDecidesHowLongTheWholeCountLasts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * One expiry for the whole count: a stack at 100 Fervour, then one at 30, and
 * the two last 8 and 3 seconds from the second, so they are held at 10.9 and
 * gone at 11.1.
 */
bool FCataclysmCarnivoreLastGrantDecidesTest::RunTest(const FString&)
{
	using namespace CataclysmCarnivoreTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCarnivoreHolder Holder(World);
	Holder.HoldTheClause();
	Holder.SetFervour(100.0f);
	UCataclysmStacks::NoteDamageTaken(Holder.AbilitySystem);
	World->TimeSeconds += 1.0f;
	Holder.SetFervour(30.0f);
	UCataclysmStacks::NoteDamageTaken(Holder.AbilitySystem);
	if (!TestEqual(TEXT("set-up: two stacks"), Holder.Carnage(), 2))
	{
		return false;
	}

	World->TimeSeconds += 10.9f;
	TestEqual(TEXT("both held 10.9 seconds after the second"), Holder.Carnage(), 2);
	World->TimeSeconds += 0.2f;
	TestEqual(TEXT("and gone at 11.1"), Holder.Carnage(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarnivoreKillTest,
	"Cataclysm.Carnivore.AKillsCarnageLastsLongerToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Each stack": a stack earned by a kill above 75 Fervour obeys it as one
 *  earned by a hit does. At 80 Fervour, 16 seconds. */
bool FCataclysmCarnivoreKillTest::RunTest(const FString&)
{
	using namespace CataclysmCarnivoreTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCarnivoreHolder Holder(World);
	Holder.HoldTheClause();
	Holder.SetFervour(80.0f);
	if (!TestTrue(TEXT("set-up: a kill above 75 Fervour grants a stack"), UCataclysmStacks::NoteEnemyKilled(Holder.Actor)))
	{
		return false;
	}
	World->TimeSeconds += 15.9f;
	TestEqual(TEXT("held at 15.9"), Holder.Carnage(), 1);
	World->TimeSeconds += 0.2f;
	TestEqual(TEXT("gone at 16.1"), Holder.Carnage(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarnivoreControlTest,
	"Cataclysm.Carnivore.WithoutTheClauseCarnageLastsItsEightSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** THE CONTROL: the same hit at 100 Fervour with no row: gone at 8.1. */
bool FCataclysmCarnivoreControlTest::RunTest(const FString&)
{
	using namespace CataclysmCarnivoreTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCarnivoreHolder Holder(World);
	Holder.SetFervour(100.0f);
	TestEqual(TEXT("no extra seconds"), UCataclysmStacks::CarnageExtraSeconds(Holder.AbilitySystem), 0.0f, 0.001f);
	UCataclysmStacks::NoteDamageTaken(Holder.AbilitySystem);
	if (!TestEqual(TEXT("set-up: one stack"), Holder.Carnage(), 1))
	{
		return false;
	}
	World->TimeSeconds += 7.9f;
	TestEqual(TEXT("held at 7.9"), Holder.Carnage(), 1);
	World->TimeSeconds += 0.2f;
	TestEqual(TEXT("gone at 8.1"), Holder.Carnage(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
