// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmDebrisBurst.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for the hole the Brute tears its rock out of.
 *
 * WHAT THESE GUARD. Issue #432. `Ability_RipNToss_Rip` is an animation whose
 * whole content is reaching down, tearing a rock out of the ground and lifting
 * it. Since issue #421 the rock is visibly in the creature's hand while it does
 * that, and the ground it came out of was untouched.
 *
 * THE PART THAT IS EASY TO GET WRONG IS THE TIMING. A hole that appears when the
 * wind-up starts is there before the creature has reached for it, and a hole
 * spawned from Tick without a latch is a new hole every frame for the rest of
 * the wind-up. Both are covered below.
 *
 * NO PARAGON PACK IS NEEDED. Every test here puts an engine mesh and an engine
 * material on the creature itself rather than relying on
 * `SM_Rampage_Rock_Rip_Crater` being in the checkout. That also removes the trap
 * issue #422 found: the crater mesh's own material is the engine's grey
 * checkerboard, so a test that used the mesh's default would be asserting
 * against the very thing the code exists to replace.
 */

namespace CataclysmRipCraterTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	/** Metres, so these read the way the design document does. */
	constexpr float M = 100.0f;

	/** Something on the player's side for a Brute to aim at. */
	static ACataclysmEnemyCharacter* SpawnTarget(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Target =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Target)
		{
			Target->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			Target->SetHealth(1000.0f);
			Target->SetAttackDamage(0.0f);
		}
		return Target;
	}

	/**
	 * A Brute mid-throw with art it is certain to have.
	 *
	 * THE MESH AND MATERIAL ARE PUT ON AFTER ResolveBody, deliberately. Without
	 * the Paragon pack ResolveBody leaves both null and UpdateRipCrater does
	 * nothing, which is correct behaviour and is checked in its own test below.
	 * Every other test wants a creature that can actually dig, so it is given
	 * engine art.
	 */
	struct FRippingBrute
	{
		explicit FRippingBrute(UWorld* World, const FRotator& Facing)
		{
			Brute = World->SpawnActor<ACataclysmBruteCharacter>(
				FVector::ZeroVector, Facing);
			Target = SpawnTarget(World, FVector(7.0f * M, 0.0f, 0.0f));
			if (!Brute)
			{
				return;
			}

			// CALLED DIRECTLY, because whether BeginPlay fires depends on how
			// the world under test was built. CataclysmBruteTests says so on its
			// own mesh test and this file follows it.
			Brute->ResolveBody(/*bIncludeAnimation=*/false);
			Brain = Cast<ACataclysmEnemyController>(Brute->GetController());

			Brute->RockCraterMesh = Cast<UStaticMesh>(
				StaticLoadObject(UStaticMesh::StaticClass(), nullptr,
								 TEXT("/Engine/BasicShapes/Cube.Cube")));

			// NOT THE MESH'S OWN DEFAULT, which for the engine cube IS
			// WorldGridMaterial -- the same checkerboard the crater mesh
			// carries. A test dressed in it could not tell the two apart.
			Brute->RockMaterial = Cast<UMaterialInterface>(
				StaticLoadObject(UMaterialInterface::StaticClass(), nullptr,
								 TEXT("/Engine/EngineMaterials/DefaultMaterial."
									  "DefaultMaterial")));
		}

		/** Put the creature into the middle of a rock throw's wind-up. */
		void BeginWindingUpTheThrow(UWorld* World) const
		{
			if (Brain)
			{
				Brain->WindingUpAbility = ACataclysmBruteCharacter::RockThrowAbility;
			}
			if (Brute)
			{
				Brute->AbilityWindUpBeganAtSeconds = World->GetTimeSeconds();
				Brute->bCraterLeftForThisThrow = false;
			}
		}

		/**
		 * Move the wind-up's start backwards, which is the same thing as time
		 * passing and does not need the world to tick a real clock.
		 */
		void PretendSecondsHavePassed(float Seconds) const
		{
			if (Brute)
			{
				Brute->AbilityWindUpBeganAtSeconds -= Seconds;
			}
		}

		ACataclysmBruteCharacter* Brute = nullptr;
		ACataclysmEnemyCharacter* Target = nullptr;
		ACataclysmEnemyController* Brain = nullptr;
	};

	/** Every crater currently on the floor. */
	static TArray<ACataclysmDebrisBurst*> CratersIn(UWorld* World)
	{
		TArray<ACataclysmDebrisBurst*> Found;
		for (TActorIterator<ACataclysmDebrisBurst> It(World); It; ++It)
		{
			Found.Add(*It);
		}
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTheCraterWaitsForTheHandsToReachTheGround,
	"Cataclysm.Brute.TheRipCraterWaitsForTheHandsToReachTheGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheCraterWaitsForTheHandsToReachTheGround::RunTest(const FString&)
{
	using namespace CataclysmRipCraterTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FRippingBrute Fixture(World, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute"), Fixture.Brute)
		|| !TestNotNull(TEXT("brain"), Fixture.Brain)
		|| !TestNotNull(TEXT("crater mesh"), Fixture.Brute->RockCraterMesh.Get()))
	{
		return false;
	}

	const float Moment = Fixture.Brute->RipReachesGroundAtSeconds();
	if (!TestTrue(TEXT("the hands reach the ground some time into the wind-up"),
		Moment > 0.0f))
	{
		return false;
	}

	Fixture.BeginWindingUpTheThrow(World);

	// THE START OF THE WIND-UP DIGS NOTHING. The creature has not reached down
	// yet, so a hole here is a hole that appeared before anything made it.
	Fixture.Brute->UpdateRipCrater();
	TestEqual(TEXT("nothing is dug when the wind-up begins"),
		CratersIn(World).Num(), 0);

	// NOR JUST BEFORE THE MOMENT. A guard that only checked "some time has
	// passed" would already have fired by here.
	Fixture.PretendSecondsHavePassed(Moment * 0.9f);
	Fixture.Brute->UpdateRipCrater();
	TestEqual(TEXT("nor while the hands are still on their way down"),
		CratersIn(World).Num(), 0);

	// AND THEN IT IS DUG.
	Fixture.PretendSecondsHavePassed(Moment * 0.2f);
	Fixture.Brute->UpdateRipCrater();

	const TArray<ACataclysmDebrisBurst*> Craters = CratersIn(World);
	if (!TestEqual(TEXT("the hole appears once the hands reach the ground"),
		Craters.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("and it is a single piece rather than a scatter"),
		Craters[0]->PiecesPlaced, 1);

	// IT OUTLIVES NEITHER ITSELF NOR THE THROW. See the header: a lifetime
	// under the cooldown is what makes "one crater per Brute" an invariant
	// instead of something needing a manager.
	TestTrue(TEXT("and it goes away before the Brute can throw again"),
		ACataclysmBruteCharacter::CraterSecondsOnTheGround
			< ACataclysmBruteCharacter::RockThrowCooldownSeconds);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTheCraterMomentFollowsTheMontageCompression,
	"Cataclysm.Brute.TheRipCraterMomentFollowsTheMontageCompression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheCraterMomentFollowsTheMontageCompression::RunTest(const FString&)
{
	using ThisBrute = ACataclysmBruteCharacter;

	// WHY THIS TEST EXISTS AND WHY IT TAKES THE RATE AS AN ARGUMENT. The four
	// tests around it ask the creature when its hands reach the ground and then
	// wind the clock to just past that answer, so they move with whatever the
	// function returns. Deleting the division by the play rate left all four
	// passing. This one states the property instead of trusting the answer.
	const float AtAuthoredSpeed = ThisBrute::RipReachesGroundAtSeconds(0.0f, 1.0f);

	TestEqual(TEXT("at its authored speed the hands reach the ground when the "
				   "clip says they do"),
		AtAuthoredSpeed, ThisBrute::RipReachesGroundSeconds, 0.0001f);

	// THE LOAD-BEARING ONE. The rock throw's montage plays at about 1.67, so
	// waiting out the authored time would put the hole in the ground well after
	// the creature had lifted the rock out of it.
	const float AtDoubleSpeed = ThisBrute::RipReachesGroundAtSeconds(0.0f, 2.0f);
	TestEqual(TEXT("an animation played twice as fast reaches the ground in "
				   "half the time"),
		AtDoubleSpeed, AtAuthoredSpeed * 0.5f, 0.0001f);

	// THE DELAY IS ADDED, NOT SCALED. It is wall clock spent waiting before the
	// montage starts at all, so compression does not touch it.
	TestEqual(TEXT("and a montage that waits first reaches the ground that much "
				   "later"),
		ThisBrute::RipReachesGroundAtSeconds(0.5f, 2.0f),
		AtDoubleSpeed + 0.5f, 0.0001f);

	// A RATE OF ZERO DOES NOT DIVIDE BY ZERO. MontageRateFor cannot return one,
	// but this is a public static that anything may call.
	TestTrue(TEXT("a rate of zero gives a finite answer"),
		FMath::IsFinite(ThisBrute::RipReachesGroundAtSeconds(0.0f, 0.0f)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmOnlyOneCraterIsDugPerThrow,
	"Cataclysm.Brute.OnlyOneRipCraterIsDugPerThrow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOnlyOneCraterIsDugPerThrow::RunTest(const FString&)
{
	using namespace CataclysmRipCraterTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FRippingBrute Fixture(World, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute"), Fixture.Brute)
		|| !TestNotNull(TEXT("brain"), Fixture.Brain))
	{
		return false;
	}

	Fixture.BeginWindingUpTheThrow(World);
	Fixture.PretendSecondsHavePassed(
		Fixture.Brute->RipReachesGroundAtSeconds() + 0.1f);

	// TWENTY PASSES, WHICH IS WHAT TICK WOULD DO. This is the fault the latch
	// exists for: the spawn condition stays true for the rest of the wind-up,
	// so without it every frame digs another hole in the same spot.
	for (int32 Pass = 0; Pass < 20; ++Pass)
	{
		Fixture.Brute->UpdateRipCrater();
	}

	TestEqual(TEXT("twenty passes over the same wind-up dig one hole"),
		CratersIn(World).Num(), 1);

	TestTrue(TEXT("and the creature remembers that it has dug it"),
		Fixture.Brute->bCraterLeftForThisThrow);

	// THE WIND-UP ENDING CLEARS IT, so the next throw digs again. Every way a
	// wind-up can end clears WindingUpAbility on the controller, which is the
	// one question this asks.
	Fixture.Brain->WindingUpAbility = -1;
	Fixture.Brute->UpdateRipCrater();
	TestFalse(TEXT("the wind-up ending forgets it, so the next throw digs"),
		Fixture.Brute->bCraterLeftForThisThrow);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTheCraterIsAheadOfTheCreatureOnTheFloor,
	"Cataclysm.Brute.TheRipCraterIsAheadOfTheCreatureOnTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheCraterIsAheadOfTheCreatureOnTheFloor::RunTest(const FString&)
{
	using namespace CataclysmRipCraterTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// FACING NINETY DEGREES ROUND, so that "ahead of the creature" and "along
	// the world's X axis" are different answers. Spawned facing zero, a crater
	// placed with the actor's forward vector and one placed along world X land
	// in the same spot and the test cannot tell them apart.
	const FRippingBrute Fixture(World, FRotator(0.0f, 90.0f, 0.0f));
	if (!TestNotNull(TEXT("brute"), Fixture.Brute)
		|| !TestNotNull(TEXT("brain"), Fixture.Brain))
	{
		return false;
	}

	Fixture.BeginWindingUpTheThrow(World);
	Fixture.PretendSecondsHavePassed(
		Fixture.Brute->RipReachesGroundAtSeconds() + 0.1f);
	Fixture.Brute->UpdateRipCrater();

	const TArray<ACataclysmDebrisBurst*> Craters = CratersIn(World);
	if (!TestEqual(TEXT("one hole"), Craters.Num(), 1))
	{
		return false;
	}

	const FVector Where = Craters[0]->GetActorLocation();
	const FVector Ahead = Fixture.Brute->GetActorForwardVector();

	// CAST, BECAUSE FVector IS DOUBLE-PRECISION IN UE5 and TestEqual's float and
	// double overloads are otherwise ambiguous.
	TestEqual(TEXT("the hole is the measured distance ahead of the creature"),
		static_cast<float>(FVector::DotProduct(
			Where - Fixture.Brute->GetActorLocation(), Ahead)),
		ACataclysmBruteCharacter::CraterAheadCm, 0.5f);

	// AND NOT OFF TO ONE SIDE. The two hands bottom out symmetrically about the
	// centre line, half a centimetre apart; a crater with a sideways component
	// would mean the forward axis was taken from the wrong place.
	TestEqual(TEXT("and squarely in front rather than off to one side"),
		static_cast<float>(FVector::DotProduct(
			Where - Fixture.Brute->GetActorLocation(),
			Fixture.Brute->GetActorRightVector())),
		0.0f, 0.5f);

	// ON THE FLOOR. An actor's location is the middle of its capsule, so a
	// crater left at the actor's own height hangs at chest level.
	const float FeetZ = static_cast<float>(Fixture.Brute->GetActorLocation().Z)
		- Fixture.Brute->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	TestEqual(TEXT("and on the floor rather than at the creature's middle"),
		static_cast<float>(Where.Z), FeetZ, 0.5f);

	// WEARING THE ROCK'S MATERIAL. The crater mesh ships with the engine's grey
	// checkerboard, exactly as the five fragments do.
	if (TestEqual(TEXT("the hole is one piece"), Craters[0]->Pieces.Num(), 1)
		&& TestNotNull(TEXT("and the piece exists"), Craters[0]->Pieces[0].Get()))
	{
		UMaterialInterface* Worn = Craters[0]->Pieces[0]->GetMaterial(0);
		TestEqual(TEXT("the hole wears the rock's material"),
			Worn, Fixture.Brute->RockMaterial.Get());
		TestTrue(TEXT("and is not left in the engine's checkerboard"),
			Worn != nullptr
				&& !Worn->GetName().Contains(TEXT("WorldGridMaterial")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmACreatureWithNoCraterArtDigsNothing,
	"Cataclysm.Brute.ACreatureWithNoRipCraterArtDigsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmACreatureWithNoCraterArtDigsNothing::RunTest(const FString&)
{
	using namespace CataclysmRipCraterTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FRippingBrute Fixture(World, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute"), Fixture.Brute))
	{
		return false;
	}

	// THE STATE A CHECKOUT WITHOUT THE PARAGON PACK IS IN, which is every
	// continuous integration run and every fresh clone. The rip has to keep
	// working; an empty burst actor sitting on the floor for four seconds is a
	// thing to clean up rather than an effect.
	Fixture.Brute->RockCraterMesh = nullptr;

	Fixture.BeginWindingUpTheThrow(World);
	Fixture.PretendSecondsHavePassed(
		Fixture.Brute->RipReachesGroundAtSeconds() + 1.0f);

	for (int32 Pass = 0; Pass < 5; ++Pass)
	{
		Fixture.Brute->UpdateRipCrater();
	}

	TestEqual(TEXT("a creature with no crater mesh leaves nothing behind"),
		CratersIn(World).Num(), 0);

	// AND HAS NOT MARKED THE THROW AS DUG, which is the part of this that is
	// the Brute's own rather than ACataclysmDebrisBurst's.
	//
	// WITHOUT THIS ASSERTION THE TEST CANNOT FAIL. Scatter already counts its
	// usable pieces and returns null when there are none -- issue #422 built it
	// that way and tests it there -- so deleting the Brute's own mesh check
	// still leaves nothing on the floor and the count above still reads zero.
	// The latch is the observable difference: a creature that set it with no
	// art would have spent its one crater on nothing, and would then dig none
	// for the rest of the wind-up if the art arrived.
	TestFalse(TEXT("and has not spent this throw's one crater on nothing"),
		Fixture.Brute->bCraterLeftForThisThrow);

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
