// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmImpCharacter.h"
#include "CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"

/**
 * Nowhere to Run, `Ravager_capstone_200` option 1: "Enemies within 8 metres of
 * you cannot move away from you. They may move toward you or around you, but
 * not further away." Issue #1515.
 *
 * THE STAT IS GIVEN BY HAND, as its row will give it: the option has no row
 * yet, so none of these can see a missing or wrong one. The rows change has to
 * add a test that wears the real row.
 *
 * A TEST WORLD IS NEVER TICKED, so a creature's own movement is stood in for by
 * moving it with `SetActorLocation`, as its movement component would, and the
 * hold's every-frame check is called directly.
 */
namespace CataclysmNowhereToRunTest
{
	constexpr float M = 100.0f;
	constexpr float Step = 0.25f;

	/** An actor with an ability system that may hold Nowhere to Run. */
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

		/** Nowhere to Run at its designed 8 metres, as its row will give it. */
		void HoldNowhereToRun() const
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = 8.0f;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line =
				Inputs.FindOrAdd(FName(UCataclysmDebuffs::NowhereToRunMetresStat));
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
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

	/** Distance from the holder in the plane, in centimetres. */
	float Apart(const AActor* A, const AActor* B)
	{
		return static_cast<float>(FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()));
	}

	/** Move the creature as its own movement would, then run the hold's check. */
	void WalkTo(ACataclysmImpCharacter* Imp, const FVector& Where)
	{
		Imp->SetActorLocation(FVector(Where.X, Where.Y, Imp->GetActorLocation().Z));
		Imp->HoldAgainstMovingAway();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNowhereToRunWalkTest,
	"Cataclysm.NowhereToRun.AHeldEnemyMayComeCloserOrGoAroundButNotWalkAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A creature within 8 metres of the holder is held; one beyond is not. Walking
 * outward is taken back to where it was; coming closer and going around at the
 * same distance are kept. The holder walking away drags nothing. Ruled
 * 2026-09-25.
 */
bool FCataclysmNowhereToRunWalkTest::RunTest(const FString&)
{
	using namespace CataclysmNowhereToRunTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedHolder Holder(World, FVector::ZeroVector);
	ACataclysmImpCharacter* Near = SpawnImp(World, FVector(5 * M, 0, 0));
	ACataclysmImpCharacter* Far = SpawnImp(World, FVector(9 * M, 0, 0));
	if (!TestNotNull(TEXT("a near Imp"), Near) || !TestNotNull(TEXT("a far Imp"), Far))
	{
		return false;
	}

	UCataclysmDebuffs::NowhereToRunStep(Holder.Actor, Step);
	TestFalse(TEXT("without the option nothing is held"), Near->IsHeld());

	Holder.HoldNowhereToRun();
	TestEqual(TEXT("with it, one creature is within 8 metres"),
			  UCataclysmDebuffs::NowhereToRunStep(Holder.Actor, Step), 1);
	TestTrue(TEXT("the one at 5 metres is held"), Near->IsHeld());
	TestFalse(TEXT("and the one at 9 metres is not"), Far->IsHeld());

	WalkTo(Near, FVector(6 * M, 0, 0));
	TestEqual(TEXT("walking out to 6 metres is taken back to 5"),
			  Apart(Near, Holder.Actor), 5 * M, 1.0f);

	WalkTo(Near, FVector(4 * M, 0, 0));
	TestEqual(TEXT("coming in to 4 metres is kept"), Apart(Near, Holder.Actor), 4 * M, 1.0f);

	WalkTo(Near, FVector(0, 4 * M, 0));
	TestEqual(TEXT("going around at 4 metres is kept"), Apart(Near, Holder.Actor), 4 * M, 1.0f);
	TestTrue(TEXT("and it really went around"), Near->GetActorLocation().Y > 3 * M);

	WalkTo(Near, FVector(0, 5 * M, 0));
	TestEqual(TEXT("and walking back out from there is taken back too"),
			  Apart(Near, Holder.Actor), 4 * M, 1.0f);

	const FVector Stood = Near->GetActorLocation();
	Holder.Actor->SetActorLocation(FVector(-3 * M, 0, 0));
	Near->HoldAgainstMovingAway();
	TestTrue(TEXT("the holder walking away drags nothing"),
			 FVector::Dist2D(Near->GetActorLocation(), Stood) < 1.0f);

	TestEqual(TEXT("a held creature is shown as held"),
			  UCataclysmCombatOverlay::HeldTextFor(Near), FString(TEXT("Held")));
	TestTrue(TEXT("on its status line"),
			 UCataclysmCombatOverlay::StatusLineFor(Near).Contains(TEXT("Held")));
	TestEqual(TEXT("and one that is not says nothing"),
			  UCataclysmCombatOverlay::HeldTextFor(Far), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNowhereToRunKnockbackTest,
	"Cataclysm.NowhereToRun.AKnockbackStillMovesAHeldEnemyAndPastEightMetresReleasesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Displacement done to the creature is not its own movement. Ruled 2026-09-25:
 * a knockback moves a held creature, the hold measures from where it landed, and
 * one pushed past 8 metres is let go.
 */
bool FCataclysmNowhereToRunKnockbackTest::RunTest(const FString&)
{
	using namespace CataclysmNowhereToRunTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedHolder Holder(World, FVector::ZeroVector);
	ACataclysmImpCharacter* Imp = SpawnImp(World, FVector(3 * M, 0, 0));
	if (!TestNotNull(TEXT("an Imp"), Imp))
	{
		return false;
	}
	Holder.HoldNowhereToRun();
	UCataclysmDebuffs::NowhereToRunStep(Holder.Actor, Step);
	if (!TestTrue(TEXT("the Imp is held"), Imp->IsHeld()))
	{
		return false;
	}

	UCataclysmSkillEffects::ApplyKnockback(Holder.Actor, Imp, 2 * M);
	const float Landed = Apart(Imp, Holder.Actor);
	if (!TestTrue(TEXT("the knockback moved it out, which every figure below "
					   "depends on"),
				  Landed > 3.5f * M))
	{
		return false;
	}
	Imp->HoldAgainstMovingAway();
	TestEqual(TEXT("the hold leaves it where it landed"), Apart(Imp, Holder.Actor), Landed, 1.0f);
	TestTrue(TEXT("and it is still held"), Imp->IsHeld());

	WalkTo(Imp, FVector(Landed + 1 * M, 0, 0));
	TestEqual(TEXT("from where it landed it still cannot walk away"),
			  Apart(Imp, Holder.Actor), Landed, 1.0f);

	Imp->SetActorLocation(FVector(10 * M, 0, Imp->GetActorLocation().Z));
	Imp->NoteDisplaced();
	Imp->HoldAgainstMovingAway();
	TestFalse(TEXT("displaced past 8 metres, it is let go at once"), Imp->IsHeld());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNowhereToRunChargeTest,
	"Cataclysm.NowhereToRun.AChargeMayPassThroughButEndsNoFartherThanItBegan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled 2026-09-25: a held creature's charge may run through the holder to the
 * far side, and ends no farther from the holder than where it began.
 */
bool FCataclysmNowhereToRunChargeTest::RunTest(const FString&)
{
	using namespace CataclysmNowhereToRunTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedHolder Holder(World, FVector(0, 1 * M, 0));
	ACataclysmImpCharacter* Imp = SpawnImp(World, FVector(3 * M, 0, 0));
	if (!TestNotNull(TEXT("an Imp"), Imp))
	{
		return false;
	}
	Holder.HoldNowhereToRun();
	UCataclysmDebuffs::NowhereToRunStep(Holder.Actor, Step);
	const float Began = Apart(Imp, Holder.Actor);

	// A LANE PAST THE HOLDER'S SIDE, so the charge passes it rather than
	// stopping on it, and ends 6 metres the other way.
	Imp->BeginCharge(FVector(-6 * M, 0, 0), 20 * M, 30.0f, 0.0f);
	for (int32 Frame = 0; Frame < 40 && Imp->IsCharging(); ++Frame)
	{
		Imp->AdvanceCharge(0.05f);
		Imp->HoldAgainstMovingAway();
	}
	TestTrue(TEXT("the charge carried it to the far side of the holder"),
			 Imp->GetActorLocation().X < 0.0f);
	TestTrue(TEXT("and it ends no farther from the holder than where it began"),
			 Apart(Imp, Holder.Actor) <= Began + 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNowhereToRunPhaseTest,
	"Cataclysm.NowhereToRun.AHeldEnemyMayNotMoveItselfToSomewhereFarther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The question a Phasewalker's step asks before it is made. Ruled 2026-09-25:
 * a step that would land farther from the holder is not made. The step itself
 * picks a seeded random direction, so this asks the question directly rather
 * than hoping for an outward draw.
 */
bool FCataclysmNowhereToRunPhaseTest::RunTest(const FString&)
{
	using namespace CataclysmNowhereToRunTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedHolder Holder(World, FVector::ZeroVector);
	ACataclysmImpCharacter* Imp = SpawnImp(World, FVector(4 * M, 0, 0));
	if (!TestNotNull(TEXT("an Imp"), Imp))
	{
		return false;
	}
	TestTrue(TEXT("a creature nobody holds may go anywhere"),
			 Imp->MayMoveItselfTo(FVector(10 * M, 0, 0)));

	Holder.HoldNowhereToRun();
	UCataclysmDebuffs::NowhereToRunStep(Holder.Actor, Step);
	TestFalse(TEXT("a held one may not land farther away"),
			  Imp->MayMoveItselfTo(FVector(6 * M, 0, 0)));
	TestTrue(TEXT("it may land closer"), Imp->MayMoveItselfTo(FVector(2 * M, 0, 0)));
	TestTrue(TEXT("or around at the same distance"),
			 Imp->MayMoveItselfTo(FVector(0, 4 * M, 0)));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
