// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * `UCataclysmFervour::RaisePool`, the one way a gain reaches the pool (the Death trees' slice C4b1, 2026-11-25).
 * `tools/tests/test_every_gain_to_the_pool_goes_through_raise_pool.py` checks that every gain in the source calls it.
 * The Soul Collector's Any Fuel, Borrowed Time and Kindred Fires, which this file also read, were retired by the
 * keystone rework (issue #2951), and their tests with them.
 */

namespace CataclysmFervourGainsTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/** An actor holding health and an empty Fervour pool of 100, */
	struct FFervourGainsHolder
	{
		explicit FFervourGainsHolder(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers, because AddAttributeSetSubobject deduces its type from the argument.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource = NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												   100.0f);
			SetFervour(0.0f);
		}

		~FFervourGainsHolder()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void SetFervour(float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
												   Value);
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFervourGainsRaisePoolTest,
	"Cataclysm.FervourGains.RaisePoolClampsAtTheMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmFervour::RaisePool` returns what actually rose. At 95, a gain of 10 raises 5 and leaves 100 held. THE
 * CONTROL: at a full bar of 100, a gain of 5 raises nothing.
 */
bool FCataclysmFervourGainsRaisePoolTest::RunTest(const FString&)
{
	using namespace CataclysmFervourGainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FFervourGainsHolder Holder(World);
	UCataclysmAbilitySystemComponent* System = Holder.AbilitySystem;
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

	Holder.SetFervour(100.0f);
	TestEqual(TEXT("control: at a full bar a gain of 5 raises nothing"), UCataclysmFervour::RaisePool(System, 5.0f),
			  0.0f, 0.001f);

	Holder.SetFervour(95.0f);
	TestEqual(TEXT("at 95, a gain of 10 raises 5"), UCataclysmFervour::RaisePool(System, 10.0f), 5.0f, 0.001f);
	TestEqual(TEXT("so 100 is held"), System->GetNumericAttribute(Pool), 100.0f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
