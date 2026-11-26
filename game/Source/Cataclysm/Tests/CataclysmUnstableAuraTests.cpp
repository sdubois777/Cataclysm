// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmNova.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Unstable Aura, the Low Life limb's nova, as the project owner approved it on
 * 2026-11-24: "While you are at or below 50% health, you release a nova every 3
 * seconds dealing 10% of your weapon damage per point to enemies within 6
 * metres."
 *
 * WHAT CHANGED IN `UCataclysmNova`. It read only a share of MISSING health,
 * every 5 seconds, which the audit measured at about 182 damage at level 100.
 * It now also reads `nova_damage_of_weapon_damage`, a share of
 * `UCataclysmSkillEffects::WeaponDamageOf`, and a character holding that figure
 * releases a nova every 3 seconds. `Cataclysm.Nova.*` still covers the
 * missing-health figure and its 5 second interval.
 *
 * THE ROW IS BUILT THE WAY `ApplyTo` LEAVES IT, a flat figure under
 * `health_at_or_below` 50, for the reason `CataclysmNovaTests.cpp` gives.
 */
namespace CataclysmUnstableAuraTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	constexpr float AuraMetre = 100.0f;

	/** A combatant with collision, so the nova's sphere search finds it. */
	struct FUnstableAuraCombatant
	{
		explicit FUnstableAuraCombatant(UWorld* World, const FVector& Where = FVector::ZeroVector)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmAllResistanceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 1000.0f);
		}

		~FUnstableAuraCombatant()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		}

		void SetHealth(float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), Value);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * Unstable Aura at eight points: 80% of weapon damage, at or below half
	 * health, on a weapon damage of 200. And the old missing-health figure as
	 * well, when asked, to show the two make one nova.
	 */
	void GiveTheAura(FUnstableAuraCombatant& Who, bool bAndTheMissingHealthFigure = false)
	{
		Who.AbilitySystem->SetNumericAttributeBase(Combat::GetAttackDamageAttribute(), 200.0f);

		FCataclysmStatModifier Weapon;
		Weapon.Bucket = ECataclysmStatBucket::Flat;
		Weapon.Source = ECataclysmModifierSource::PassiveKeystone;
		Weapon.Value = 80.0f;
		Weapon.Condition = ECataclysmStatCondition::HealthAtOrBelowPercent;
		Weapon.ConditionValue = 50.0f;

		TMap<FName, FCataclysmStatInputs> Inputs;
		Inputs.FindOrAdd(FName(UCataclysmNova::WeaponDamageStat)).Modifiers = {Weapon};
		if (bAndTheMissingHealthFigure)
		{
			FCataclysmStatModifier Missing = Weapon;
			Missing.Value = 10.0f;
			Inputs.FindOrAdd(FName(UCataclysmNova::DamageStat)).Modifiers = {Missing};
		}
		Who.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnstableAuraArithmeticTest,
	"Cataclysm.UnstableAura.ANovaIsAShareOfWeaponDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmUnstableAuraArithmeticTest::RunTest(const FString&)
{
	TestEqual(TEXT("eighty per cent of 200 is 160"), UCataclysmNova::AmountFromWeaponDamage(80.0f, 200.0f), 160.0f,
			  0.001f);
	TestEqual(TEXT("no share is no nova"), UCataclysmNova::AmountFromWeaponDamage(0.0f, 200.0f), 0.0f, 0.001f);
	TestEqual(TEXT("no weapon damage is no nova"), UCataclysmNova::AmountFromWeaponDamage(80.0f, 0.0f), 0.0f, 0.001f);
	TestEqual(TEXT("and nothing negative"), UCataclysmNova::AmountFromWeaponDamage(-10.0f, 200.0f), 0.0f, 0.001f);
	TestEqual(TEXT("the interval for this figure is three seconds"), UCataclysmNova::WeaponDamageIntervalSeconds, 3.0f,
			  0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnstableAuraReleasesTest,
	"Cataclysm.UnstableAura.AtOrBelowHalfHealthANovaOfWeaponDamageComesEveryThreeSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Above half health nothing comes; at two fifths a nova worth 160 strikes an
 * enemy two metres off and spares one nine metres off, and the next is due three
 * seconds later.
 */
bool FCataclysmUnstableAuraReleasesTest::RunTest(const FString&)
{
	using namespace CataclysmUnstableAuraTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FUnstableAuraCombatant Masochist(World, FVector::ZeroVector);
	FUnstableAuraCombatant Near(World, FVector(2 * AuraMetre, 0.0f, 0.0f));
	FUnstableAuraCombatant Far(World, FVector(9 * AuraMetre, 0.0f, 0.0f));
	GiveTheAura(Masochist);

	Masochist.SetHealth(600.0f);
	TestEqual(TEXT("at three fifths of its health no nova comes"), UCataclysmNova::Step(Masochist.Actor), 0.0f, 0.001f);

	Masochist.SetHealth(400.0f);
	const float NearBefore = Near.Health();
	const float FarBefore = Far.Health();
	TestEqual(TEXT("at two fifths a nova of 80% of the 200 weapon damage comes: 160"),
			  UCataclysmNova::Step(Masochist.Actor), 160.0f, 0.01f);
	TestTrue(TEXT("the enemy two metres off is struck"), Near.Health() < NearBefore);
	TestEqual(TEXT("and the one nine metres off is not"), Far.Health(), FarBefore, 0.001f);

	const float SecondsUntilTheNext =
		Masochist.AbilitySystem->NovaAllowedAt() - static_cast<float>(World->GetTimeSeconds());
	TestEqual(TEXT("and the next is due three seconds after it"), SecondsUntilTheNext, 3.0f, 0.01f);
	TestEqual(TEXT("so a second on the same frame is refused"), UCataclysmNova::Step(Masochist.Actor), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnstableAuraBothFiguresTest,
	"Cataclysm.UnstableAura.AHolderOfBothFiguresReleasesOneNovaOfTheTwoTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The old missing-health figure is still read, and a character holding both
 * releases one nova worth both, on the weapon figure's three seconds: 160 of
 * weapon damage and 10% of the 600 missing, 220.
 */
bool FCataclysmUnstableAuraBothFiguresTest::RunTest(const FString&)
{
	using namespace CataclysmUnstableAuraTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FUnstableAuraCombatant Masochist(World, FVector::ZeroVector);
	GiveTheAura(Masochist, /*bAndTheMissingHealthFigure=*/true);
	Masochist.SetHealth(400.0f);
	TestEqual(TEXT("one nova of 160 and 60: 220"), UCataclysmNova::Step(Masochist.Actor), 220.0f, 0.01f);
	TestEqual(TEXT("on three seconds"),
			  Masochist.AbilitySystem->NovaAllowedAt() - static_cast<float>(World->GetTimeSeconds()), 3.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnstableAuraControlTest,
	"Cataclysm.UnstableAura.WithoutItAHurtCharacterReleasesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** THE CONTROL: the same character at two fifths with its weapon and no row. */
bool FCataclysmUnstableAuraControlTest::RunTest(const FString&)
{
	using namespace CataclysmUnstableAuraTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FUnstableAuraCombatant Masochist(World, FVector::ZeroVector);
	FUnstableAuraCombatant Near(World, FVector(2 * AuraMetre, 0.0f, 0.0f));
	Masochist.AbilitySystem->SetNumericAttributeBase(Combat::GetAttackDamageAttribute(), 200.0f);
	Masochist.SetHealth(400.0f);
	const float NearBefore = Near.Health();
	TestEqual(TEXT("no nova"), UCataclysmNova::Step(Masochist.Actor), 0.0f, 0.001f);
	TestEqual(TEXT("and the enemy is unhurt"), Near.Health(), NearBefore, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
