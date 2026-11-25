// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Warded Flesh, the Ritualist's First Pact's second option since the demonic tree rework of 2026-11-24: "Each time one
 * of your minions dies, you regain 10% of your Maximum Energy Shield." Its row is
 * `energy_shield_on_minion_death_percent`, a flat 10, read by `UCataclysmRegeneration::GiveShieldOnMinionDeath` from
 * the commanded-death hook in `UCataclysmVitalAttributeSet`, beside the Fervour the starting node grants.
 *
 * EVERY DEATH GOES BY THE ROUTE THE GAME TAKES: the minion's health set to zero. The row is granted by hand.
 */
namespace CataclysmWardedFleshTest
{
	constexpr float M = 100.0f;

	/** The commander's shield, large and round so a tenth of it reads plainly. */
	constexpr float ShieldMaximum = 1000.0f;

	/** Warded Flesh's row, a flat 10. */
	constexpr float WardedFleshPercent = 10.0f;

	struct FWardedFleshScopedCommander
	{
		explicit FWardedFleshScopedCommander(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper.
			UCataclysmVitalAttributeSet* Vitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* Combat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* Resource = NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(Vitals);
			AbilitySystem->AddAttributeSetSubobject(Combat);
			AbilitySystem->AddAttributeSetSubobject(Resource);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), ShieldMaximum);
			Set(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
		}

		~FWardedFleshScopedCommander()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value)
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Shield() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
		}

		void HoldWardedFlesh()
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Flat;
			Row.Source = ECataclysmModifierSource::PassiveKeystone;
			Row.Value = WardedFleshPercent;

			TMap<FName, FCataclysmStatInputs> Stats;
			FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(UCataclysmRegeneration::ShieldOnMinionDeathStat));
			Line.Base = 0.0f;
			Line.Modifiers = {Row};
			AbilitySystem->SetStatInputs(MoveTemp(Stats));
		}

		/** An imp of the Imp row, standing apart. */
		ACataclysmMinion* Summon(float MetresAway)
		{
			return ACataclysmMinion::Spawn(Actor, FVector(MetresAway * M, 0, 0), /*Lifetime=*/60.0f,
										   /*bBurns=*/false, TEXT("Imp"));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** Dies by the route the game takes: its health reaches zero. */
	void Kill(ACataclysmMinion* Minion)
	{
		if (UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Minion))
		{
			Theirs->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
		}
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a
// `using namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWardedFleshDeathTest,
	"Cataclysm.WardedFlesh.EachMinionDeathGivesItsCommanderATenthOfItsShield",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE CONTROL FIRST: an imp dying before the row is taken gives nothing. Then two deaths each give a tenth of the
 * 1000 shield, so a grant that fired once and latched is caught.
 */
bool FCataclysmWardedFleshDeathTest::RunTest(const FString&)
{
	using namespace CataclysmWardedFleshTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWardedFleshScopedCommander Ritualist(World);
	ACataclysmMinion* Before = Ritualist.Summon(3.0f);
	ACataclysmMinion* First = Ritualist.Summon(6.0f);
	ACataclysmMinion* Second = Ritualist.Summon(9.0f);
	if (!TestNotNull(TEXT("set-up: three imps"), Before) || !TestNotNull(TEXT("set-up: three imps"), First)
		|| !TestNotNull(TEXT("set-up: three imps"), Second))
	{
		return false;
	}

	Kill(Before);
	if (!TestTrue(TEXT("set-up: the first imp is recorded as dead"), UCataclysmSkillEffects::IsDead(Before)))
	{
		return false;
	}
	TestEqual(TEXT("control: without Warded Flesh a minion's death gives no shield"), Ritualist.Shield(), 0.0f,
			  0.01f);

	Ritualist.HoldWardedFlesh();
	Kill(First);
	TestEqual(TEXT("with Warded Flesh a minion's death gives a tenth of the maximum"), Ritualist.Shield(),
			  ShieldMaximum * WardedFleshPercent / 100.0f, 0.01f);
	Kill(Second);
	TestEqual(TEXT("and a second death another tenth"), Ritualist.Shield(),
			  2.0f * ShieldMaximum * WardedFleshPercent / 100.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWardedFleshExpiryTest,
	"Cataclysm.WardedFlesh.AnImpWhoseTimeRunsOutIsNoDeathAndGivesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Each time one of your minions DIES". A lifetime running out writes no health, so it is no death and reaches none of
 * the death hook: the shield is unchanged. And a full shield takes no more.
 */
bool FCataclysmWardedFleshExpiryTest::RunTest(const FString&)
{
	using namespace CataclysmWardedFleshTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWardedFleshScopedCommander Ritualist(World);
	Ritualist.HoldWardedFlesh();
	ACataclysmMinion* Expiring = Ritualist.Summon(3.0f);
	ACataclysmMinion* Dying = Ritualist.Summon(6.0f);
	if (!TestNotNull(TEXT("set-up: two imps"), Expiring) || !TestNotNull(TEXT("set-up: two imps"), Dying))
	{
		return false;
	}

	Expiring->LifeSpanExpired();
	TestEqual(TEXT("an imp whose time ran out gave no shield"), Ritualist.Shield(), 0.0f, 0.01f);

	Ritualist.Set(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), ShieldMaximum);
	Kill(Dying);
	TestEqual(TEXT("and a full shield takes no more"), Ritualist.Shield(), ShieldMaximum, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
