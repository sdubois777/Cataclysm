// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "GameplayEffect.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

/**
 * Tests for the ability system wiring.
 *
 * These prove the loop the issue asks for: an attribute exists, a gameplay
 * effect modifies it, and the damage meta attribute routes through to health
 * rather than being written directly.
 *
 * They run without the editor's play mode, so they are fast and can gate a
 * merge. They do NOT cover replication; that needs a networked play-in-editor
 * session and is verified separately.
 */

namespace CataclysmTest
{
	/** A throwaway actor carrying an ability system component and attribute set. */
	struct FScopedAbilityActor
	{
		explicit FScopedAbilityActor(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointer, not the TObjectPtr member: AddAttributeSetSubobject is
			// a template and deduces T from the argument, so passing a
			// TObjectPtr deduces the wrapper rather than the attribute set.
			UCataclysmVitalAttributeSet* NewAttributes = NewObject<UCataclysmVitalAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewAttributes);
			Attributes = NewAttributes;

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FScopedAbilityActor()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Applies an instant effect setting one attribute to a magnitude. */
		void ApplyInstantModifier(const FGameplayAttribute& Attribute,
								  EGameplayModOp::Type Op,
								  float Magnitude) const
		{
			UGameplayEffect* Effect = NewObject<UGameplayEffect>(
				GetTransientPackage(), FName(TEXT("TestEffect")));
			Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

			const int32 Index = Effect->Modifiers.Num();
			Effect->Modifiers.SetNum(Index + 1);
			FGameplayModifierInfo& Info = Effect->Modifiers[Index];
			Info.Attribute = Attribute;
			Info.ModifierOp = Op;
			Info.ModifierMagnitude = FScalableFloat(Magnitude);

			AbilitySystem->ApplyGameplayEffectToSelf(
				Effect, 1.0f, AbilitySystem->MakeEffectContext());
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Attributes = nullptr;
	};

	static UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttributeDefaultsTest,
	"Cataclysm.AbilitySystem.AttributeDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttributeDefaultsTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmTest::MakeWorld();
	{
		const CataclysmTest::FScopedAbilityActor Fixture(World);

		TestEqual(TEXT("Health starts at 100"), Fixture.Attributes->GetHealth(), 100.0f);
		TestEqual(TEXT("MaxHealth starts at 100"), Fixture.Attributes->GetMaxHealth(), 100.0f);
		TestEqual(TEXT("Damage meta attribute starts at zero"), Fixture.Attributes->GetDamage(), 0.0f);
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageRoutingTest,
	"Cataclysm.AbilitySystem.DamageRoutesThroughMetaAttribute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDamageRoutingTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmTest::MakeWorld();
	{
		const CataclysmTest::FScopedAbilityActor Fixture(World);

		Fixture.ApplyInstantModifier(
			UCataclysmVitalAttributeSet::GetDamageAttribute(), EGameplayModOp::Additive, 30.0f);

		TestEqual(TEXT("Health reduced by the damage dealt"),
			Fixture.Attributes->GetHealth(), 70.0f);

		// The whole point of a meta attribute: it is consumed, not accumulated.
		// If this ever fails, damage is stacking across applications.
		TestEqual(TEXT("Damage meta attribute is zeroed after execution"),
			Fixture.Attributes->GetDamage(), 0.0f);
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHealthClampTest,
	"Cataclysm.AbilitySystem.HealthClampsToRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHealthClampTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmTest::MakeWorld();
	{
		const CataclysmTest::FScopedAbilityActor Fixture(World);

		// Overkill must floor at zero, not go negative. A negative health value
		// silently breaks every "is dead" check written as Health <= 0 elsewhere.
		Fixture.ApplyInstantModifier(
			UCataclysmVitalAttributeSet::GetDamageAttribute(), EGameplayModOp::Additive, 500.0f);
		TestEqual(TEXT("Health floors at zero on overkill"),
			Fixture.Attributes->GetHealth(), 0.0f);

		// Overhealing must cap at MaxHealth.
		Fixture.ApplyInstantModifier(
			UCataclysmVitalAttributeSet::GetHealthAttribute(), EGameplayModOp::Additive, 500.0f);
		TestEqual(TEXT("Health caps at MaxHealth on overheal"),
			Fixture.Attributes->GetHealth(), Fixture.Attributes->GetMaxHealth());
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaxHealthFloorTest,
	"Cataclysm.AbilitySystem.MaxHealthCannotReachZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMaxHealthFloorTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmTest::MakeWorld();
	{
		const CataclysmTest::FScopedAbilityActor Fixture(World);

		// MaxHealth of zero would make the Health clamp divide the character's
		// whole valid range down to a single point, and any percentage-of-max
		// calculation would divide by zero.
		Fixture.ApplyInstantModifier(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), EGameplayModOp::Additive, -1000.0f);

		TestTrue(TEXT("MaxHealth stays at or above 1"),
			Fixture.Attributes->GetMaxHealth() >= 1.0f);
	}
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
