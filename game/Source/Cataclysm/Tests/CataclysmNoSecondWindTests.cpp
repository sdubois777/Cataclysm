// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"

/**
 * No Second Wind, the Ravager keystone `Ravager_keystone_c_kB`. Issue #1515:
 * "Cripple and Weaken you applied do not
 * expire while that enemy is within 4 metres of you."
 *
 * THE STAT IS GIVEN BY HAND, as the row will give it: the keystone has no row
 * yet. `applied_cripple_and_weaken_held_within_metres` is the 4.
 *
 * THE STEP IS CALLED DIRECTLY, with the world's clock moved by the same step
 * first, which is what the character's own regeneration step does each time it
 * runs. A held effect keeps the time it had left; one that is not held loses
 * the step.
 */
namespace CataclysmNoSecondWindTest
{
	using Vital = UCataclysmVitalAttributeSet;

	constexpr float M = 100.0f;
	constexpr float Lasts = 3.0f;

	/** A character with the attribute sets an effect needs, and a body to find. */
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

			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 1000.0f);
		}

		~FScopedFighter()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		/** Hold No Second Wind at `Metres`, as its row will. */
		void HoldNoSecondWind(float Metres)
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = Metres;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line =
				Inputs.FindOrAdd(FName(UCataclysmDebuffs::AppliedHeldWithinMetresStat));
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		/** The longest time left on any effect carrying `Tag`, or -1 for none. */
		float TimeLeftOn(const FGameplayTag& Tag) const
		{
			FGameplayTagContainer Tags;
			Tags.AddTag(Tag);
			const TArray<float> Left = AbilitySystem->GetActiveEffectsTimeRemaining(
				FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Tags));
			float Longest = -1.0f;
			for (const float Seconds : Left)
			{
				Longest = FMath::Max(Longest, Seconds);
			}
			return Longest;
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	void Cripple(const FScopedFighter& By, const FScopedFighter& Target)
	{
		UCataclysmSkillEffects::ApplyTagForDuration(By.Actor, Target.Actor,
			UCataclysmDebuffs::CrippleTag(), Lasts, 30.0f);
	}

	void Weaken(const FScopedFighter& By, const FScopedFighter& Target)
	{
		UCataclysmSkillEffects::ApplyNamedEffect(By.Actor, Target.Actor,
			UCataclysmDebuffs::WeakenTag(), Lasts, 20.0f);
	}

	/** One second of the world's clock, and the steps of the named characters. */
	void OneSecond(UWorld* World, const TArray<const FScopedFighter*>& Stepping)
	{
		World->TimeSeconds += 1.0f;
		for (const FScopedFighter* Who : Stepping)
		{
			UCataclysmDebuffs::HoldAppliedNearbyStep(Who->Actor, 1.0f);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoSecondWindHoldsTest,
	"Cataclysm.NoSecondWind.YourCrippleAndWeakenDoNotRunDownOnAnEnemyWithinFourMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The holder cripples and weakens an enemy two metres away and cripples one six
 * metres away. After a second, both of the near enemy's still have three
 * seconds left, and the far enemy's Cripple has two.
 */
bool FCataclysmNoSecondWindHoldsTest::RunTest(const FString&)
{
	using namespace CataclysmNoSecondWindTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Ravager(World, FVector::ZeroVector);
	FScopedFighter Near(World, FVector(2 * M, 0, 0));
	FScopedFighter Far(World, FVector(6 * M, 0, 0));
	Ravager.HoldNoSecondWind(4.0f);

	Cripple(Ravager, Near);
	Weaken(Ravager, Near);
	Cripple(Ravager, Far);
	if (!TestEqual(TEXT("the near enemy is crippled for three seconds"),
				   Near.TimeLeftOn(UCataclysmDebuffs::CrippleTag()), Lasts, 0.01f)
		|| !TestEqual(TEXT("and weakened for three"),
					  Near.TimeLeftOn(UCataclysmDebuffs::WeakenTag()), Lasts, 0.01f))
	{
		return false;
	}

	OneSecond(World, {&Ravager});

	TestEqual(TEXT("a second later the near enemy's Cripple has three seconds left"),
			  Near.TimeLeftOn(UCataclysmDebuffs::CrippleTag()), Lasts, 0.01f);
	TestEqual(TEXT("and its Weaken three"),
			  Near.TimeLeftOn(UCataclysmDebuffs::WeakenTag()), Lasts, 0.01f);
	TestEqual(TEXT("and the far enemy's Cripple two, since six metres is outside four"),
			  Far.TimeLeftOn(UCataclysmDebuffs::CrippleTag()), Lasts - 1.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoSecondWindOnlyYoursTest,
	"Cataclysm.NoSecondWind.OnlyWhatYouAppliedIsHeldAndOnlyWhileYouHoldTheKeystone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "You applied": a Cripple someone else put on an enemy two metres from the
 * holder runs down. And a character without the keystone holds nothing.
 */
bool FCataclysmNoSecondWindOnlyYoursTest::RunTest(const FString&)
{
	using namespace CataclysmNoSecondWindTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Ravager(World, FVector::ZeroVector);
	FScopedFighter Someone(World, FVector(0, 20 * M, 0));
	FScopedFighter Theirs(World, FVector(0, 2 * M, 0));
	FScopedFighter Plain(World, FVector(0, 50 * M, 0));
	FScopedFighter PlainsEnemy(World, FVector(2 * M, 50 * M, 0));
	Ravager.HoldNoSecondWind(4.0f);

	Cripple(Someone, Theirs);
	Cripple(Plain, PlainsEnemy);

	OneSecond(World, {&Ravager, &Someone, &Plain});

	TestEqual(TEXT("a Cripple someone else applied, two metres from the holder, runs down"),
			  Theirs.TimeLeftOn(UCataclysmDebuffs::CrippleTag()), Lasts - 1.0f, 0.01f);
	TestEqual(TEXT("and one applied by a character without the keystone runs down"),
			  PlainsEnemy.TimeLeftOn(UCataclysmDebuffs::CrippleTag()), Lasts - 1.0f, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
