// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmImpCharacter.h"
#include "CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"

/**
 * Ground Down, the Ravager's `Ravager_capstone_100` option 1. Issue #1515:
 * "Enemies within 4 metres of you have 15% reduced Movement Speed and 15%
 * reduced Attack Speed."
 *
 * THE STATS ARE GIVEN BY HAND, as the row will give them: the option has no row
 * yet, so these cannot see a missing or wrong one. The rows change has to add a
 * test that wears the real row.
 *
 * THE STEP IS CALLED DIRECTLY, after moving the world's clock, which is what the
 * holder's own regeneration step does each time it runs. A creature's speeds
 * are read the way the game reads them: its attack interval from
 * `SecondsBetweenAttacks`, and its walk from the movement component after
 * `RefreshWalkSpeed`, which a test world does not tick.
 */
namespace CataclysmGroundDownTest
{
	constexpr float M = 100.0f;
	constexpr float Step = 0.25f;

	/** A character with an ability system, a body to be found by, and none of
	 *  a creature's speeds, to hold Ground Down. */
	struct FScopedHolder
	{
		FScopedHolder(UWorld* World, const FVector& Where)
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
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
		}

		~FScopedHolder()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		/** Hold Ground Down at its designed 4 metres and 15%, as its row will. */
		void HoldGroundDown()
		{
			TMap<FName, FCataclysmStatInputs> Inputs;
			for (const TPair<const TCHAR*, float>& Stat :
				 {TPair<const TCHAR*, float>(UCataclysmDebuffs::GroundDownMetresStat, 4.0f),
				  TPair<const TCHAR*, float>(UCataclysmDebuffs::GroundDownPercentStat, 15.0f)})
			{
				FCataclysmStatModifier Flat;
				Flat.Bucket = ECataclysmStatBucket::Flat;
				Flat.Source = ECataclysmModifierSource::PassiveKeystone;
				Flat.Value = Stat.Value;
				FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat.Key));
				Line.Base = 0.0f;
				Line.Modifiers = {Flat};
			}
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		void StepOnce() const
		{
			UCataclysmDebuffs::GroundDownStep(Actor, Step);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	ACataclysmImpCharacter* SpawnImp(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmImpCharacter>(
			ACataclysmImpCharacter::StaticClass(), Where, FRotator::ZeroRotator, Params);
	}

	/** An Imp's attack interval and walk as the game reads them now. */
	struct FSpeeds
	{
		float Interval = 0.0f;
		float Walk = 0.0f;
	};

	FSpeeds SpeedsOf(ACataclysmImpCharacter* Imp)
	{
		Imp->RefreshWalkSpeed();
		return {Imp->SecondsBetweenAttacks(), Imp->GetCharacterMovement()->MaxWalkSpeed};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundDownSlowsTest,
	"Cataclysm.GroundDown.AnEnemyWithinFourMetresIsFifteenPercentSlowerAtBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * An Imp two metres from the holder attacks at its designed interval over 0.85
 * and walks at 0.85 of its designed speed; one six metres away is untouched.
 * The line under the near Imp's bar says so.
 */
bool FCataclysmGroundDownSlowsTest::RunTest(const FString&)
{
	using namespace CataclysmGroundDownTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedHolder Holder(World, FVector::ZeroVector);
	ACataclysmImpCharacter* Near = SpawnImp(World, FVector(2 * M, 0, 0));
	ACataclysmImpCharacter* Far = SpawnImp(World, FVector(6 * M, 0, 0));
	if (!TestNotNull(TEXT("a near Imp"), Near) || !TestNotNull(TEXT("a far Imp"), Far))
	{
		return false;
	}
	const float Interval = Near->DesignedSecondsBetweenAttacks();
	const float Walk = Near->DesignedWalkSpeedCmPerSecond;
	if (!TestTrue(TEXT("an Imp walks, which every walk figure below depends on"),
				  Walk > 0.0f))
	{
		return false;
	}

	Holder.HoldGroundDown();
	Holder.StepOnce();

	const FSpeeds NearNow = SpeedsOf(Near);
	const FSpeeds FarNow = SpeedsOf(Far);
	TestEqual(TEXT("the Imp two metres away attacks 15% less often"),
			  NearNow.Interval, Interval / 0.85f, 0.001f);
	TestEqual(TEXT("and walks 15% slower"), NearNow.Walk, Walk * 0.85f, 0.01f);
	TestEqual(TEXT("the Imp six metres away attacks at its designed interval"),
			  FarNow.Interval, Interval, 0.001f);
	TestEqual(TEXT("and walks at its designed speed"), FarNow.Walk, Walk, 0.01f);
	TestEqual(TEXT("the line under the near Imp's bar says it is slowed"),
			  UCataclysmCombatOverlay::StatusLineFor(Near), FString(TEXT("Slowed -15%")));
	TestEqual(TEXT("and the far Imp's says nothing"),
			  UCataclysmCombatOverlay::StatusLineFor(Far), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundDownEndsTest,
	"Cataclysm.GroundDown.ItEndsAfterTheEnemyLeavesAndDoesNotStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * With no further step, the slow holds for three steps and then ends. And two
 * holders stepping together slow an Imp by 15%, not 15% twice. Ruled
 * 2026-09-24: three steps, 0.75 seconds, and the larger percent holds.
 */
bool FCataclysmGroundDownEndsTest::RunTest(const FString&)
{
	using namespace CataclysmGroundDownTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedHolder Holder(World, FVector::ZeroVector);
	FScopedHolder Second(World, FVector(0, 1 * M, 0));
	ACataclysmImpCharacter* Imp = SpawnImp(World, FVector(2 * M, 0, 0));
	if (!TestNotNull(TEXT("an Imp"), Imp))
	{
		return false;
	}
	const float Interval = Imp->DesignedSecondsBetweenAttacks();
	Holder.HoldGroundDown();
	Holder.StepOnce();

	World->TimeSeconds += 0.5f;
	TestEqual(TEXT("half a second later, with no step, it is still slowed"),
			  SpeedsOf(Imp).Interval, Interval / 0.85f, 0.001f);

	World->TimeSeconds += 0.3f;
	const FSpeeds After = SpeedsOf(Imp);
	TestEqual(TEXT("eight tenths of a second after the last step it attacks at its "
				   "designed interval"),
			  After.Interval, Interval, 0.001f);
	TestEqual(TEXT("and walks at its designed speed"),
			  After.Walk, Imp->DesignedWalkSpeedCmPerSecond, 0.01f);
	TestEqual(TEXT("and the line under its bar says nothing"),
			  UCataclysmCombatOverlay::StatusLineFor(Imp), FString());

	Second.HoldGroundDown();
	Holder.StepOnce();
	Second.StepOnce();
	TestEqual(TEXT("two holders together slow it by 15%, not by 15% twice"),
			  SpeedsOf(Imp).Interval, Interval / 0.85f, 0.001f);
	TestEqual(TEXT("and the line still says 15%"),
			  UCataclysmCombatOverlay::StatusLineFor(Imp), FString(TEXT("Slowed -15%")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundDownWithCrippleTest,
	"Cataclysm.GroundDown.ItMultipliesWithCrippleAndNeedsTheStat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A crippled Imp near the holder is at Cripple's multiplier times 0.85, the two
 * multiplied rather than added (ruled 2026-09-24). And a character without the
 * option slows nothing.
 */
bool FCataclysmGroundDownWithCrippleTest::RunTest(const FString&)
{
	using namespace CataclysmGroundDownTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedHolder Holder(World, FVector::ZeroVector);
	ACataclysmImpCharacter* Imp = SpawnImp(World, FVector(2 * M, 0, 0));
	FScopedHolder Plain(World, FVector(0, 50 * M, 0));
	ACataclysmImpCharacter* PlainsImp = SpawnImp(World, FVector(2 * M, 50 * M, 0));
	if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("another Imp"), PlainsImp))
	{
		return false;
	}
	const float Interval = Imp->DesignedSecondsBetweenAttacks();

	const FGameplayTag Cripple = UCataclysmSkillShapes::StatusTagFor(TEXT("Cripple"));
	if (!TestTrue(TEXT("the Imp is crippled"),
				  UCataclysmSkillEffects::ApplyNamedEffect(
					  Holder.Actor, Imp, Cripple,
					  UCataclysmSkillEffects::NumbersForEffectTag(Cripple).DurationSeconds)))
	{
		return false;
	}
	const float Crippled = Imp->CrippleMultiplier();
	if (!TestTrue(TEXT("and Cripple slows it, which the figure below depends on"),
				  Crippled < 1.0f))
	{
		return false;
	}

	Holder.HoldGroundDown();
	Holder.StepOnce();
	TestEqual(TEXT("crippled and near the holder, it attacks at its interval over "
				   "the Cripple times 0.85"),
			  SpeedsOf(Imp).Interval, Interval / (Crippled * 0.85f), 0.001f);

	Plain.StepOnce();
	TestEqual(TEXT("an Imp near a character without the option attacks at its "
				   "designed interval"),
			  SpeedsOf(PlainsImp).Interval, PlainsImp->DesignedSecondsBetweenAttacks(),
			  0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
