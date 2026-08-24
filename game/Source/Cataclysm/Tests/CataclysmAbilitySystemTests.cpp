// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Misc/ScopeExit.h"
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


// ---------------------------------------------------------------------------
// Cooldown reduction, which nothing applied until issue #895
// ---------------------------------------------------------------------------

/**
 * A CHARACTER'S COOLDOWN REDUCTION SHORTENS ITS COOLDOWNS. Issue #895.
 *
 * WHAT WAS WRONG. UCataclysmCombatAttributeSet::FinalCooldown was written,
 * documented and tested, and no code in the project called it.
 * UCataclysmGameplayAbility::ApplyCooldown applied every cooldown at its stated
 * length, so `Stat_Increased_cooldown_reduction` was worth nothing however much
 * of it a player wore. The `cooldown_reduction` stat also had no entry in
 * UCataclysmPlayerClassStats::StatToAttribute, so the affix was dropped before
 * it reached the attribute at all.
 *
 * IT DIVIDES RATHER THAN SUBTRACTING, which is what stops it breaking. The
 * design: "Subtracting 1% per point would reach zero cooldowns at 100 points of
 * Efficacy. Dividing, all 100 points halve every cooldown, gear pushes further
 * with each point worth progressively less, and zero is unreachable."
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownReductionReachesASkillTest,
	"Cataclysm.Ability.CooldownReductionOnTheCharacterShortensItsCooldowns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCooldownReductionReachesASkillTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game,
									   /*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	AActor* Actor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("an actor"), Actor))
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		NewObject<UCataclysmAbilitySystemComponent>(Actor);
	AbilitySystem->RegisterComponent();

	// A raw pointer on purpose: AddAttributeSetSubobject is a template and a
	// TObjectPtr would deduce the wrapper rather than the set.
	UCataclysmCombatAttributeSet* Combat =
		NewObject<UCataclysmCombatAttributeSet>(Actor);
	AbilitySystem->AddAttributeSetSubobject(Combat);
	AbilitySystem->InitAbilityActorInfo(Actor, Actor);

	// NOTHING AT ALL LEAVES A COOLDOWN AT ITS STATED LENGTH, which is the
	// baseline every character sits on: no class line names cooldown reduction.
	TestEqual(TEXT("no ability system leaves a cooldown alone"),
		UCataclysmGameplayAbility::CooldownAfterReduction(nullptr, 4.0f),
		4.0f, 0.001f);
	TestEqual(TEXT("and so does a character with none of it"),
		UCataclysmGameplayAbility::CooldownAfterReduction(AbilitySystem, 4.0f),
		4.0f, 0.001f);

	// A QUARTER'S WORTH TURNS FOUR SECONDS INTO THREE AND TWO FIFTHS, because
	// it divides: 4 / 1.25. Subtracting would give three.
	Combat->SetCooldownReduction(25.0f);
	TestEqual(TEXT("25% divides a four second cooldown by 1.25"),
		UCataclysmGameplayAbility::CooldownAfterReduction(AbilitySystem, 4.0f),
		4.0f / 1.25f, 0.001f);

	// AND THE DESIGN'S OWN WORKED FIGURE: one hundred points of Efficacy at one
	// per cent each halves every cooldown.
	Combat->SetCooldownReduction(100.0f);
	TestEqual(TEXT("100% halves a four second cooldown"),
		UCataclysmGameplayAbility::CooldownAfterReduction(AbilitySystem, 4.0f),
		2.0f, 0.001f);

	// AND IT CAN NEVER REACH ZERO, which is why the design says the stat needs
	// no cap. Subtracting would have reached zero at 100 and gone negative
	// above it.
	for (const float Reduction : {200.0f, 1'000.0f, 100'000.0f})
	{
		Combat->SetCooldownReduction(Reduction);
		const float Left =
			UCataclysmGameplayAbility::CooldownAfterReduction(AbilitySystem, 4.0f);
		TestTrue(FString::Printf(
			TEXT("a cooldown stays above zero at %.0f%% reduction, at %.6f"),
			Reduction, Left),
			Left > 0.0f);
		TestTrue(TEXT("and keeps shrinking rather than turning negative"),
			Left < 4.0f);
	}

	// A NEGATIVE FIGURE LENGTHENS NOTHING. CooldownDivisor floors the increases
	// at zero, so bad data leaves a cooldown at its stated length rather than
	// making it longer or infinite.
	Combat->SetCooldownReduction(-50.0f);
	TestEqual(TEXT("a negative reduction leaves the cooldown alone"),
		UCataclysmGameplayAbility::CooldownAfterReduction(AbilitySystem, 4.0f),
		4.0f, 0.001f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
