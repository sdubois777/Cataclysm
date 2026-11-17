// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmShoulderThrough.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Shoulder Through, the Ravager's `Ravager_capstone_100` option 3. Issue #1515.
 *
 * "Moving into an enemy pushes it aside and deals your melee damage to it."
 *
 * THE STAT IS GIVEN BY HAND, as the row will give it: the option has no row
 * yet. The player controller's per-frame call is not reached from here, since
 * automation tests run with no player controller; these drive `Step`, which is
 * what that call makes, with the direction the controller would pass.
 */
namespace CataclysmShoulderThroughTest
{
	using Vital = UCataclysmVitalAttributeSet;

	constexpr float Pool = 100000.0f;

	/** 34 cm, a body the size `FScopedSwinger` uses elsewhere. So two bodies
	 *  touch within 34 + 34 + 10 = 78 cm between centres. */
	constexpr float BodyCm = 34.0f;

	/** A body a sphere search can find, with the sets a blow needs. */
	struct FScopedBody
	{
		FScopedBody(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(BodyCm);
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
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
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
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FScopedBody()
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

		FVector Where() const
		{
			return Actor->GetActorLocation();
		}

		/** Hold Shoulder Through, as its row will. */
		void HoldShoulderThrough()
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = 1.0f;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(UCataclysmShoulderThrough::Stat));
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShoulderThroughDeadAheadTest,
	"Cataclysm.ShoulderThrough.AnEnemyDeadAheadIsPushedToTheRightStaggeredAndStruck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Walking along +X into an enemy 60 cm ahead: it moves 150 cm to the right,
 * which is +Y, and not along the walk; it is staggered; it takes damage.
 */
bool FCataclysmShoulderThroughDeadAheadTest::RunTest(const FString&)
{
	using namespace CataclysmShoulderThroughTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedBody Walker(World, FVector::ZeroVector);
	FScopedBody Enemy(World, FVector(60.0f, 0.0f, 0.0f));
	Walker.HoldShoulderThrough();

	TestTrue(TEXT("the enemy 60 cm ahead is the one moved into"),
			 UCataclysmShoulderThrough::EnemyMovedInto(Walker.Actor, FVector::ForwardVector)
				 == Enemy.Actor);
	TestTrue(TEXT("and Step pushes it"),
			 UCataclysmShoulderThrough::Step(Walker.Actor, FVector::ForwardVector)
				 == Enemy.Actor);
	TestEqual(TEXT("it stands where it stood along the walk"), Enemy.Where().X, 60.0,
			  1.0);
	TestEqual(TEXT("and 150 cm to the right of it, since dead ahead goes right"),
			  Enemy.Where().Y, static_cast<double>(UCataclysmShoulderThrough::PushCm), 1.0);
	TestTrue(TEXT("it is staggered, as every displacement leaves it"),
			 UCataclysmSkillEffects::IsStaggered(Enemy.Actor));
	TestTrue(TEXT("and it took the walker's melee damage"), Enemy.Health() < Pool);
	TestEqual(TEXT("the walker took nothing"), Walker.Health(), Pool);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShoulderThroughSideTest,
	"Cataclysm.ShoulderThrough.AnEnemyToTheLeftIsPushedLeftAndOneBehindOrApartIsLeftAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The push goes to the side the enemy already stands on. An enemy behind the
 * walk, one outside the 45 degree cone, and one not touching are all left
 * where they are and unharmed.
 */
bool FCataclysmShoulderThroughSideTest::RunTest(const FString&)
{
	using namespace CataclysmShoulderThroughTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedBody Walker(World, FVector::ZeroVector);
	Walker.HoldShoulderThrough();

	{
		// 58 cm away, 31 degrees to the left of +X: touching and inside the cone.
		FScopedBody Left(World, FVector(50.0f, -30.0f, 0.0f));
		TestTrue(TEXT("an enemy ahead and to the left is pushed"),
				 UCataclysmShoulderThrough::Step(Walker.Actor, FVector::ForwardVector)
					 == Left.Actor);
		TestEqual(TEXT("to the left, 150 cm on from where it stood"), Left.Where().Y,
				  -30.0 - UCataclysmShoulderThrough::PushCm, 1.0);
		TestTrue(TEXT("and struck"), Left.Health() < Pool);
		Left.Actor->Destroy();
		Left.Actor = nullptr;
	}

	// Each alone, so the one asked about is the only one there.
	const TPair<const TCHAR*, FVector> Untouched[] = {
		{TEXT("an enemy 60 cm behind the walk"), FVector(-60.0f, 0.0f, 0.0f)},
		// 64 cm away and 51 degrees off the walk: touching, outside the cone.
		{TEXT("an enemy 51 degrees off the walk"), FVector(40.0f, 50.0f, 0.0f)},
		// 90 cm between centres is 12 cm more than the 78 that touches.
		{TEXT("an enemy 90 cm ahead"), FVector(90.0f, 0.0f, 0.0f)},
	};
	for (const TPair<const TCHAR*, FVector>& Case : Untouched)
	{
		FScopedBody Enemy(World, Case.Value);
		TestNull(FString::Printf(TEXT("%s is not moved into"), Case.Key),
				 UCataclysmShoulderThrough::Step(Walker.Actor, FVector::ForwardVector));
		TestEqual(FString::Printf(TEXT("%s stays where it stood"), Case.Key),
				  Enemy.Where(), Case.Value);
		TestEqual(FString::Printf(TEXT("%s is unharmed"), Case.Key), Enemy.Health(), Pool);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShoulderThroughOncePerSecondTest,
	"Cataclysm.ShoulderThrough.EachEnemyIsPushedAtMostOnceASecondAndOthersAreNotKeptWaiting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Put back in front of the walker half a second later, the same enemy is not
 * pushed or struck; a second enemy is. A full second after the first push the
 * first is pushed again.
 */
bool FCataclysmShoulderThroughOncePerSecondTest::RunTest(const FString&)
{
	using namespace CataclysmShoulderThroughTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FVector Ahead(60.0f, 0.0f, 0.0f);
	FScopedBody Walker(World, FVector::ZeroVector);
	FScopedBody First(World, Ahead);
	Walker.HoldShoulderThrough();

	TestTrue(TEXT("the first enemy is pushed"),
			 UCataclysmShoulderThrough::Step(Walker.Actor, FVector::ForwardVector)
				 == First.Actor);

	World->TimeSeconds += 0.5;
	First.Actor->SetActorLocation(Ahead);
	const float HealthBefore = First.Health();
	TestNull(TEXT("half a second later, back in front, it is not pushed again"),
			 UCataclysmShoulderThrough::Step(Walker.Actor, FVector::ForwardVector));
	TestEqual(TEXT("so it stays in front"), First.Where(), Ahead);
	TestEqual(TEXT("and is not struck again"), First.Health(), HealthBefore);

	{
		// Out of the way, so the second enemy is the only one touching.
		First.Actor->SetActorLocation(FVector(0.0f, 1000.0f, 0.0f));
		FScopedBody Second(World, Ahead);
		TestTrue(TEXT("but a second enemy in the same half second is"),
				 UCataclysmShoulderThrough::Step(Walker.Actor, FVector::ForwardVector)
					 == Second.Actor);
		Second.Actor->Destroy();
		Second.Actor = nullptr;
	}

	World->TimeSeconds += 0.5;
	First.Actor->SetActorLocation(Ahead);
	TestTrue(TEXT("and one second after its first push the first enemy is pushed again"),
			 UCataclysmShoulderThrough::Step(Walker.Actor, FVector::ForwardVector)
				 == First.Actor);
	TestTrue(TEXT("and struck again"), First.Health() < HealthBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShoulderThroughNeedsTheOptionTest,
	"Cataclysm.ShoulderThrough.StandingStillOrNotHoldingTheOptionPushesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** No direction is not walking, and a walker without the option pushes nothing. */
bool FCataclysmShoulderThroughNeedsTheOptionTest::RunTest(const FString&)
{
	using namespace CataclysmShoulderThroughTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FVector Ahead(60.0f, 0.0f, 0.0f);
	FScopedBody Holder(World, FVector::ZeroVector);
	FScopedBody Enemy(World, Ahead);
	Holder.HoldShoulderThrough();

	TestNull(TEXT("a holder asking to move nowhere pushes nothing"),
			 UCataclysmShoulderThrough::Step(Holder.Actor, FVector::ZeroVector));
	TestNull(TEXT("nor does one asking to move straight up"),
			 UCataclysmShoulderThrough::Step(Holder.Actor, FVector::UpVector));

	FScopedBody Plain(World, FVector(0.0f, 1000.0f, 0.0f));
	FScopedBody PlainsEnemy(World, FVector(60.0f, 1000.0f, 0.0f));
	TestTrue(TEXT("a walker without the option still moves into the enemy ahead"),
			 UCataclysmShoulderThrough::EnemyMovedInto(Plain.Actor, FVector::ForwardVector)
				 == PlainsEnemy.Actor);
	TestNull(TEXT("but pushes nothing"),
			 UCataclysmShoulderThrough::Step(Plain.Actor, FVector::ForwardVector));
	TestEqual(TEXT("so that enemy stays where it stood"), PlainsEnemy.Where(),
			  FVector(60.0f, 1000.0f, 0.0f));
	TestEqual(TEXT("and is unharmed"), PlainsEnemy.Health(), Pool);
	TestEqual(TEXT("and the holder's enemy was never touched either"), Enemy.Where(), Ahead);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
