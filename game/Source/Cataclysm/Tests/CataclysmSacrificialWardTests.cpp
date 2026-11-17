// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Sacrificial Ward, the Ritualist keystone `Ritualist_keystone_c_kC`. Issue
 * #1515: "Damage that would break your Energy Shield instead destroys the
 * minion with the least health remaining, no more than once every 3 seconds."
 *
 * RULED 2026-09-23 UNDER THE OWNER'S DELEGATION: the least health is counted in
 * points; the minion spent dies as any death does, so every death rule answers
 * it; with no minion alive the shield breaks normally.
 *
 * JUDGEMENT: "instead" cancels the whole blow. The shield keeps what it had and
 * health takes nothing.
 *
 * THE STAT IS GIVEN BY HAND, as the row will give it: the keystone has no row
 * yet. `shield_break_destroys_minion_every_seconds` is the 3.
 */
namespace CataclysmSacrificialWardTest
{
	using Vital = UCataclysmVitalAttributeSet;

	constexpr float M = 100.0f;
	constexpr float Pool = 1000.0f;
	constexpr float Shield = 100.0f;

	/** A character with the attribute sets a blow needs, and a body to find. */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, const FVector& Where)
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

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and
			// a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAllResist =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAllResist);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), Pool);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), Pool);
		}

		~FScopedFighter()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		/** Give this character a full energy shield of `Shield`. */
		void Shielded()
		{
			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxEnergyShieldAttribute(), Shield);
			AbilitySystem->SetNumericAttributeBase(Vital::GetEnergyShieldAttribute(), Shield);
		}

		/** Give this character flat figures, as passive rows would. */
		void Hold(const TMap<FName, float>& Stats)
		{
			TMap<FName, FCataclysmStatInputs> Inputs;
			for (const TPair<FName, float>& Stat : Stats)
			{
				FCataclysmStatModifier Flat;
				Flat.Bucket = ECataclysmStatBucket::Flat;
				Flat.Source = ECataclysmModifierSource::PassiveKeystone;
				Flat.Value = Stat.Value;
				FCataclysmStatInputs& Line = Inputs.FindOrAdd(Stat.Key);
				Line.Base = 0.0f;
				Line.Modifiers = {Flat};
			}
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		void HoldTheWard()
		{
			Hold({{FName(UCataclysmAbilitySystemComponent::ShieldBreakDestroysMinionEverySecondsStat),
				   3.0f}});
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** An imp of `Summoner`'s at `Where`, left with `ShareOfMaximum` of its health. */
	ACataclysmMinion* ImpAt(FAutomationTestBase& Test, AActor* Summoner, const FVector& Where,
							float ShareOfMaximum)
	{
		ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
			Summoner, Where, /*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Imp"));
		UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Imp);
		if (!Test.TestNotNull(TEXT("an imp with an ability system"), Its))
		{
			return nullptr;
		}
		const float Maximum = Its->GetNumericAttribute(Vital::GetMaxHealthAttribute());
		Its->SetNumericAttributeBase(Vital::GetHealthAttribute(), Maximum * ShareOfMaximum);
		return Imp;
	}

	bool IsDead(const ACataclysmMinion* Imp)
	{
		return !IsValid(Imp) || UCataclysmSkillEffects::IsDead(Imp);
	}

	void Strike(const FScopedFighter& From, const FScopedFighter& To, float Damage)
	{
		UCataclysmSkillEffects::ApplyDirectDamage(From.Actor, To.Actor, Damage);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSacrificialWardSpendsTest,
	"Cataclysm.SacrificialWard.AShieldBreakingBlowIsCancelledAndTheWeakestMinionDiesInstead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A blow of 300 against a full shield of 100: the shield keeps its 100, health
 * keeps its 1,000, and of two imps -- one at 80% health, one at 40% -- the one
 * at 40% dies. The next ward waits three seconds.
 */
bool FCataclysmSacrificialWardSpendsTest::RunTest(const FString&)
{
	using namespace CataclysmSacrificialWardTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World, FVector(0, 50 * M, 0));
	FScopedFighter Ritualist(World, FVector::ZeroVector);
	Ritualist.Shielded();
	Ritualist.HoldTheWard();
	ACataclysmMinion* Stronger = ImpAt(*this, Ritualist.Actor, FVector(2 * M, 0, 0), 0.8f);
	ACataclysmMinion* Weaker = ImpAt(*this, Ritualist.Actor, FVector(3 * M, 0, 0), 0.4f);
	if (!Stronger || !Weaker)
	{
		return false;
	}

	const float WardedAt = World->GetTimeSeconds();
	Strike(Attacker, Ritualist, 300.0f);

	TestEqual(TEXT("the shield keeps its 100"),
			  Ritualist.Get(Vital::GetEnergyShieldAttribute()), Shield, 0.001f);
	TestEqual(TEXT("and health keeps its 1,000"),
			  Ritualist.Get(Vital::GetHealthAttribute()), Pool, 0.001f);
	TestTrue(TEXT("the imp with the least health remaining dies"), IsDead(Weaker));
	TestFalse(TEXT("and the other does not"), IsDead(Stronger));
	TestEqual(TEXT("the next ward waits three seconds"),
			  Ritualist.AbilitySystem->ShieldWardAllowedAt() - WardedAt, 3.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSacrificialWardHoldsTest,
	"Cataclysm.SacrificialWard.ABlowTheShieldHoldsSpendsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** A blow of 60 against a shield of 100 leaves 40, and no imp is spent. */
bool FCataclysmSacrificialWardHoldsTest::RunTest(const FString&)
{
	using namespace CataclysmSacrificialWardTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World, FVector(0, 50 * M, 0));
	FScopedFighter Ritualist(World, FVector::ZeroVector);
	Ritualist.Shielded();
	Ritualist.HoldTheWard();
	ACataclysmMinion* Imp = ImpAt(*this, Ritualist.Actor, FVector(2 * M, 0, 0), 0.4f);
	if (!Imp)
	{
		return false;
	}

	Strike(Attacker, Ritualist, 60.0f);

	TestEqual(TEXT("the shield takes the 60 and keeps 40"),
			  Ritualist.Get(Vital::GetEnergyShieldAttribute()), Shield - 60.0f, 0.001f);
	TestFalse(TEXT("and no imp is spent"), IsDead(Imp));
	TestTrue(TEXT("and no ward is recorded"),
			 Ritualist.AbilitySystem->ShieldWardAllowedAt() < 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSacrificialWardIntervalTest,
	"Cataclysm.SacrificialWard.OnceInThreeSecondsAndWithNothingToSpendTheShieldBreaks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * After a ward, a second shield-breaking blow one second later breaks the
 * shield as normal and spends nothing; another after the three seconds spends
 * the remaining imp. A character with the keystone and nothing alive, and one
 * without the keystone, both have their shields broken.
 */
bool FCataclysmSacrificialWardIntervalTest::RunTest(const FString&)
{
	using namespace CataclysmSacrificialWardTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World, FVector(0, 50 * M, 0));
	FScopedFighter Ritualist(World, FVector::ZeroVector);
	Ritualist.Shielded();
	Ritualist.HoldTheWard();
	ACataclysmMinion* First = ImpAt(*this, Ritualist.Actor, FVector(2 * M, 0, 0), 0.4f);
	ACataclysmMinion* Second = ImpAt(*this, Ritualist.Actor, FVector(3 * M, 0, 0), 0.8f);
	if (!First || !Second)
	{
		return false;
	}

	Strike(Attacker, Ritualist, 300.0f);
	TestTrue(TEXT("the first ward spends the weaker imp"), IsDead(First));

	World->TimeSeconds += 1.0f;
	Strike(Attacker, Ritualist, 300.0f);
	TestEqual(TEXT("one second later the shield breaks as normal"),
			  Ritualist.Get(Vital::GetEnergyShieldAttribute()), 0.0f, 0.001f);
	TestEqual(TEXT("and health takes what the shield did not"),
			  Ritualist.Get(Vital::GetHealthAttribute()), Pool - 200.0f, 0.001f);
	TestFalse(TEXT("and the other imp is not spent"), IsDead(Second));

	World->TimeSeconds += 2.1f;
	Ritualist.Shielded();
	Strike(Attacker, Ritualist, 300.0f);
	TestTrue(TEXT("after the three seconds a ward spends the other"), IsDead(Second));
	TestEqual(TEXT("and the shield keeps its 100"),
			  Ritualist.Get(Vital::GetEnergyShieldAttribute()), Shield, 0.001f);

	FScopedFighter Alone(World, FVector(0, 20 * M, 0));
	Alone.Shielded();
	Alone.HoldTheWard();
	FScopedFighter Plain(World, FVector(0, 30 * M, 0));
	Plain.Shielded();
	Strike(Attacker, Alone, 300.0f);
	Strike(Attacker, Plain, 300.0f);
	TestEqual(TEXT("with nothing alive to spend, the shield breaks"),
			  Alone.Get(Vital::GetEnergyShieldAttribute()), 0.0f, 0.001f);
	TestEqual(TEXT("and without the keystone it breaks too"),
			  Plain.Get(Vital::GetEnergyShieldAttribute()), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSacrificialWardDeathTest,
	"Cataclysm.SacrificialWard.ASpentMinionsDeathIsADeathForSharedRuin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled 2026-09-23: the spent minion dies as any death does. A Ritualist holding
 * both the ward and Shared Ruin spends an imp, and an enemy three metres from
 * that imp loses a fifth of the imp's maximum health.
 */
bool FCataclysmSacrificialWardDeathTest::RunTest(const FString&)
{
	using namespace CataclysmSacrificialWardTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Attacker(World, FVector(0, 50 * M, 0));
	FScopedFighter Ritualist(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(13 * M, 0, 0));
	Ritualist.Shielded();
	Ritualist.Hold(
		{{FName(UCataclysmAbilitySystemComponent::ShieldBreakDestroysMinionEverySecondsStat), 3.0f},
		 {FName(ACataclysmMinion::DeathBlastPercentOfMaximumHealthStat), 20.0f},
		 {FName(ACataclysmMinion::DeathBlastRadiusMetresStat), 4.0f}});
	ACataclysmMinion* Imp = ImpAt(*this, Ritualist.Actor, FVector(10 * M, 0, 0), 0.4f);
	if (!Imp)
	{
		return false;
	}
	const float Maximum =
		UCataclysmTargeting::AbilitySystemOf(Imp)->GetNumericAttribute(Vital::GetMaxHealthAttribute());

	Strike(Attacker, Ritualist, 300.0f);

	TestTrue(TEXT("the ward spends the imp"), IsDead(Imp));
	TestEqual(TEXT("and its death is Shared Ruin's: a fifth of its maximum, three metres away"),
			  Pool - Enemy.Get(Vital::GetHealthAttribute()), Maximum * 0.2f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSacrificialWardKillerTest,
	"Cataclysm.SacrificialWard.ASpentMinionIsKilledByNobodyEvenIfAnEnemyStruckItBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A death written to health names as its killer whoever last struck the dying
 * creature. An enemy grazes an imp, and later strikes its Ritualist's shield;
 * the ward spends the imp, and its death notice names no killer: not the
 * player, so no kill rule of the player's pays for it, and not the enemy.
 */
bool FCataclysmSacrificialWardKillerTest::RunTest(const FString&)
{
	using namespace CataclysmSacrificialWardTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestNotNull(TEXT("combat events"), Events))
	{
		return false;
	}

	FScopedFighter Enemy(World, FVector(0, 50 * M, 0));
	FScopedFighter Ritualist(World, FVector::ZeroVector);
	Ritualist.Shielded();
	Ritualist.HoldTheWard();
	ACataclysmMinion* Imp = ImpAt(*this, Ritualist.Actor, FVector(2 * M, 0, 0), 0.8f);
	if (!Imp)
	{
		return false;
	}

	bool bHeard = false;
	const AActor* Killer = nullptr;
	const FDelegateHandle Handle = Events->OnDeath.AddLambda(
		[&bHeard, &Killer, Imp](const FCataclysmDeathNotice& Notice)
		{
			if (Notice.Victim == Imp)
			{
				bHeard = true;
				Killer = Notice.Killer;
			}
		});
	ON_SCOPE_EXIT { Events->OnDeath.Remove(Handle); };

	UCataclysmSkillEffects::ApplyDirectDamage(Enemy.Actor, Imp, 1.0f);
	Strike(Enemy, Ritualist, 300.0f);

	if (!TestTrue(TEXT("the ward spent the imp and its death was announced"), bHeard))
	{
		return false;
	}
	TestNull(TEXT("and it names no killer"), Killer);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
