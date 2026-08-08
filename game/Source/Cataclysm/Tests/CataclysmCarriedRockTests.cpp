// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for the rock the Brute carries before it throws it.
 *
 * WHAT THESE GUARD. Issue #421. The rock throw played `Ability_RipNToss_Rip` --
 * an animation whose whole content is tearing a rock out of the ground -- with
 * nothing in the creature's hands, and the rock then appeared in mid-air and
 * flew off.
 *
 * THE PART THAT IS EASY TO GET WRONG IS NOT SHOWING IT. It is hiding it again. A
 * wind-up ends three ways: the attack lands, a stun cancels it, or the pawn is
 * unpossessed. A rock left in the creature's hand after any of them is a worse
 * fault than the one this fixes, because it is permanent rather than momentary.
 * The visibility is therefore asked of the brain every frame rather than
 * remembered, and the tests below cover the endings that can be reached from a
 * synthetic world.
 */

namespace CataclysmCarriedRockTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (World)
		{
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}
		return World;
	}

	/** Metres, so these read the way the design document does. */
	constexpr float M = 100.0f;

	/** Something on the player's side for a Brute to attack. */
	static ACataclysmEnemyCharacter* SpawnTarget(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Target = World->SpawnActor<ACataclysmEnemyCharacter>(
			Where, FRotator::ZeroRotator);
		if (Target)
		{
			Target->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			Target->SetHealth(1000.0f);
			Target->SetAttackDamage(0.0f);
		}
		return Target;
	}

	/**
	 * A Brute out of reach of its stomp and inside its throw range, with the art
	 * resolved, plus the thing it is aiming at.
	 */
	struct FThrowingBrute
	{
		explicit FThrowingBrute(UWorld* World)
		{
			Brute = World->SpawnActor<ACataclysmBruteCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
			Target = SpawnTarget(World, FVector(7.0f * M, 0.0f, 0.0f));
			if (Brute)
			{
				// CALLED DIRECTLY. Whether BeginPlay fires depends on how the
				// world under test was built; CataclysmBruteTests says so on its
				// own mesh test, and this file learned it the same way.
				Brute->ResolveBody(/*bIncludeAnimation=*/false);
				Brain = Cast<ACataclysmEnemyController>(Brute->GetController());
			}
		}

		ACataclysmBruteCharacter* Brute = nullptr;
		ACataclysmEnemyCharacter* Target = nullptr;
		ACataclysmEnemyController* Brain = nullptr;
	};

	/** Whether the rock is currently being shown. */
	static bool RockIsVisible(const ACataclysmBruteCharacter* Brute)
	{
		return Brute && Brute->CarriedRock && !Brute->CarriedRock->bHiddenInGame;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTheRockHangsFromThePropBone,
	"Cataclysm.Brute.TheCarriedRockHangsFromThePropBone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheRockHangsFromThePropBone::RunTest(const FString&)
{
	using namespace CataclysmCarriedRockTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("brute"), Brute))
	{
		return false;
	}

	if (!TestNotNull(TEXT("it has somewhere to put a rock"),
		Brute->CarriedRock.Get()))
	{
		return false;
	}

	// ON THE SKELETAL MESH, NOT ON THE CAPSULE. A rock attached to the actor
	// root would hang in the air beside the creature and never move with the
	// hand that is supposed to be holding it.
	TestEqual(TEXT("the rock hangs from the animated mesh"),
		Brute->CarriedRock->GetAttachParent(),
		static_cast<USceneComponent*>(Brute->GetMesh()));

	// A BONE, NOT A SOCKET. The Rampage mesh has no sockets at all.
	//
	// THE LITERAL, NOT THE CONSTANT, AND THE DIFFERENCE IS THE WHOLE TEST.
	// Comparing the component's attachment against RockHoldBoneName asks
	// whether the class agrees with itself, which it always does: changing
	// the constant changes both sides and the test passes. Proved by doing
	// exactly that -- it was changed to hand_r and this test did not
	// notice. The measured answer is written here as its own copy.
	TestEqual(TEXT("the prop bone is the one the animation moves"),
		ACataclysmBruteCharacter::RockHoldBoneName, FName(TEXT("weapon_r")));
	TestEqual(TEXT("and the rock hangs from it"),
		Brute->CarriedRock->GetAttachSocketName(),
		ACataclysmBruteCharacter::RockHoldBoneName);

	TestTrue(TEXT("and it is hidden before anything asks for it"),
		Brute->CarriedRock->bHiddenInGame);

	// NO COLLISION. What gets thrown is a projectile with its own sweep; a
	// colliding rock in the hand would be a second way to hit somebody.
	TestEqual(TEXT("and it cannot hit anything while held"),
		static_cast<int32>(Brute->CarriedRock->GetCollisionEnabled()),
		static_cast<int32>(ECollisionEnabled::NoCollision));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmTheRockIsHeldOnlyWhileWindingUpTheThrow,
	"Cataclysm.Brute.TheRockIsHeldOnlyWhileTheThrowIsWoundUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheRockIsHeldOnlyWhileWindingUpTheThrow::RunTest(const FString&)
{
	using namespace CataclysmCarriedRockTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FThrowingBrute Fixture(World);
	if (!TestNotNull(TEXT("brute"), Fixture.Brute)
		|| !TestNotNull(TEXT("target"), Fixture.Target)
		|| !TestNotNull(TEXT("brain"), Fixture.Brain))
	{
		return false;
	}

	const bool bHasArt = Fixture.Brute->RockMesh != nullptr;
	if (!bHasArt)
	{
		AddInfo(TEXT("The Paragon Rampage pack is not installed, so there is no "
					 "rock to hold. Only the hidden-throughout path is checked."));
	}

	Fixture.Brute->UpdateCarriedRock();
	TestFalse(TEXT("nothing is held before the creature does anything"),
		RockIsVisible(Fixture.Brute));

	if (!TestEqual(TEXT("it begins a rock throw"),
		static_cast<int32>(Fixture.Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp)))
	{
		return false;
	}

	Fixture.Brute->UpdateCarriedRock();

	if (bHasArt)
	{
		TestTrue(TEXT("the rock is in its hand while it winds up"),
			RockIsVisible(Fixture.Brute));
	}
	else
	{
		TestFalse(TEXT("without the pack nothing is shown, which is not a fault"),
			RockIsVisible(Fixture.Brute));
	}

	// THE ENDING THAT MATTERS MOST. A rock left in the hand after the throw is
	// permanent, where the fault this replaces was momentary.
	World->TimeSeconds += ACataclysmBruteCharacter::RockThrowWindUpSeconds + 0.1;
	Fixture.Brain->Think();
	Fixture.Brute->UpdateCarriedRock();

	TestEqual(TEXT("the throw landed"), Fixture.Brain->AbilitiesUsed, 1);
	TestFalse(TEXT("and the rock left its hand with it"),
		RockIsVisible(Fixture.Brute));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAStunTakesTheRockOutOfItsHand,
	"Cataclysm.Brute.AStunTakesTheRockOutOfItsHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAStunTakesTheRockOutOfItsHand::RunTest(const FString&)
{
	using namespace CataclysmCarriedRockTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FThrowingBrute Fixture(World);
	if (!TestNotNull(TEXT("brute"), Fixture.Brute)
		|| !TestNotNull(TEXT("target"), Fixture.Target)
		|| !TestNotNull(TEXT("brain"), Fixture.Brain))
	{
		return false;
	}

	if (!Fixture.Brute->RockMesh)
	{
		AddInfo(TEXT("The Paragon Rampage pack is not installed, so the rock is "
					 "hidden throughout and this test can only check that."));
	}

	Fixture.Brain->Think();
	Fixture.Brute->UpdateCarriedRock();

	// A STUN ABANDONS A COMMITTED WIND-UP, which is the one thing that does. The
	// attack did not happen, so the rock must not stay in the hand.
	UCataclysmSkillEffects::ApplyStun(Fixture.Target, Fixture.Brute,
									  /*DurationSeconds=*/1.0f,
									  /*DamageDealt=*/0.0f,
									  /*bStunIsDesigned=*/true);

	Fixture.Brain->Think();
	Fixture.Brute->UpdateCarriedRock();

	TestEqual(TEXT("the wind-up was abandoned"),
		Fixture.Brain->WindingUpAbility, -1);
	TestEqual(TEXT("and no throw happened"), Fixture.Brain->AbilitiesUsed, 0);
	TestFalse(TEXT("so the rock is not still being held"),
		RockIsVisible(Fixture.Brute));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
