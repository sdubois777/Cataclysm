// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the body the player character wears. Issue #1124.
 *
 * WHAT THESE GUARD. The player used to be two engine primitives: a cylinder for
 * the body and a cone stuck on the front, because with a bare cylinder there was
 * no way to see which way the character faced. It had no skeleton, nothing to
 * hang a weapon on, and nothing to play a death animation on -- so a player
 * dying stopped moving and stood there until it came back.
 *
 * THE HALF THAT BREAKS SILENTLY IS COMING BACK. Dying puts the mesh component
 * into single-node mode so the body holds the last frame of its death clip
 * rather than blending to an idle stand for the two seconds before the respawn.
 * Nothing else takes it out of that mode, so a `Revive` that forgot would leave
 * a living character walking around frozen in the pose it died in -- and the
 * code that plays the death clip would look perfectly correct.
 *
 * THESE NEED THE MANNEQUIN ASSETS AND SAY SO WHEN THEY ARE ABSENT. A checkout
 * without `game/Content/Characters/Mannequins/` gets a character that is
 * invisible and still walks, fights and dies, which is deliberate. Under test
 * that means there is no mesh to make assertions about, so these report through
 * `CataclysmTestSkip::ReportSkippedHalf` rather than failing, and
 * `python tools/unreal_build.py tests` names them after the pass count.
 *
 * WHAT THESE DELIBERATELY DO NOT COVER. Whether it looks right. The automation
 * command passes `-nullrhi`, so nothing reaches a screen under test and no
 * assertion here can tell a good pose from a bad one. Somebody has to look.
 */

namespace CataclysmPlayerBodyTest
{
	/** Half-height of the player's collision capsule, in centimetres. A copy of
	 *  the constant in CataclysmPlayerCharacter.cpp, which is file-scoped. The
	 *  mesh has to drop by exactly this or the character's waist is at floor
	 *  level. */
	constexpr float CapsuleHalfHeight = 96.0f;

	/** The engine's convention for character meshes: they face -Y while the
	 *  actor faces +X, so the mesh carries a -90 degree yaw. Without it the
	 *  character walks sideways. */
	constexpr float MeshYaw = -90.0f;

	inline ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Actor =
			World->SpawnActor<ACataclysmPlayerCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
		if (State && Actor)
		{
			Actor->SetPlayerState(State);
			Actor->OnRep_PlayerState();
			return Actor;
		}
		return nullptr;
	}

	/** The mesh component, or null when the art is not on this machine. */
	inline USkeletalMeshComponent* DressedMeshOf(ACataclysmPlayerCharacter* Player)
	{
		USkeletalMeshComponent* Mesh = Player ? Player->GetMesh() : nullptr;
		return (Mesh && Mesh->GetSkeletalMeshAsset()) ? Mesh : nullptr;
	}

	inline FString NoArtReason()
	{
		return TEXT("game/Content/Characters/Mannequins/ is not on this "
					"machine, so there is no mesh to check. That the character "
					"still spawns and is driveable IS checked; nothing about "
					"its body, its pose or its death clip is. See "
					"game/docs/player-source-assets.md.");
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// The body
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmPlayerWearsASkeletalMeshTest,
	"Cataclysm.PlayerBody.ItWearsASkeletalMeshRatherThanACylinder")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmPlayerBodyTest::SpawnPlayer(World);
	if (TestNotNull(TEXT("a player"), Player))
	{
		// NO PLACEHOLDER LEFT ATTACHED. ACataclysmAbyssalWardenCharacter has to
		// call PlaceholderBody->SetVisibility(false) because assigning a
		// skeletal mesh does not remove a static mesh that is still attached,
		// and the project owner saw exactly that on the Warden: "the cylinder
		// base is appearing over him". The player has no placeholder to hide.
		//
		// NAMED RATHER THAN COUNTED, AND THAT IS DELIBERATE. An earlier version
		// asserted that the character had NO static mesh component at all. That
		// is the wrong shape twice over: it failed on a component this test
		// could not name, so the failure said nothing about what was there; and
		// issue #1125 will legitimately attach a weapon mesh to this character,
		// which would have made a correct change look like a regression.
		TArray<UStaticMeshComponent*> Primitives;
		Player->GetComponents<UStaticMeshComponent>(Primitives);

		FString Present;
		for (const UStaticMeshComponent* Component : Primitives)
		{
			if (!Component)
			{
				continue;
			}

			Present += FString::Printf(TEXT("%s [%s]  "), *Component->GetName(),
				*Component->GetClass()->GetName());

			TestFalse(FString::Printf(
					TEXT("%s is not a leftover placeholder"),
					*Component->GetName()),
				Component->GetName().Contains(TEXT("Placeholder")));
		}

		// RECORDED EVEN WHEN IT PASSES, so that the next person to read a run
		// can see what the player actually carries rather than inferring it.
		AddInfo(FString::Printf(
			TEXT("static mesh components on the player character: %s"),
			Present.IsEmpty() ? TEXT("none") : *Present));

		if (USkeletalMeshComponent* Mesh =
				CataclysmPlayerBodyTest::DressedMeshOf(Player))
		{
			TestNotNull(TEXT("and it is wearing a skeletal mesh"),
				Mesh->GetSkeletalMeshAsset());
		}
		else
		{
			CataclysmTestSkip::ReportSkippedHalf(
				*this, CataclysmPlayerBodyTest::NoArtReason());
		}
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPlayerMeshSitsOnTheCapsuleTest,
	"Cataclysm.PlayerBody.ItsFeetAreOnTheCapsuleBottomAndItFacesForward")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmPlayerBodyTest::SpawnPlayer(World);
	if (USkeletalMeshComponent* Mesh =
			CataclysmPlayerBodyTest::DressedMeshOf(Player))
	{
		// A skeletal mesh is authored with its origin at the feet and a
		// capsule's origin is its centre. Getting this wrong is not subtle --
		// the character walks with its waist at floor level -- but it is the
		// kind of thing a later edit moves without noticing.
		// CAST TO FLOAT BECAUSE FVector AND FRotator HOLD DOUBLES IN UE5. Passing
		// a double against a float expectation leaves TestEqual's float and
		// double overloads equally good and the compiler refuses it: "overloaded
		// functions have similar conversions", error C2666.
		TestEqual(TEXT("the mesh drops by the capsule half height"),
			static_cast<float>(Mesh->GetRelativeLocation().Z),
			-CataclysmPlayerBodyTest::CapsuleHalfHeight, 0.01f);

		TestEqual(TEXT("and carries the engine's -90 degree mesh yaw"),
			static_cast<float>(Mesh->GetRelativeRotation().Yaw),
			CataclysmPlayerBodyTest::MeshYaw, 0.01f);
	}
	else if (Player)
	{
		CataclysmTestSkip::ReportSkippedHalf(
			*this, CataclysmPlayerBodyTest::NoArtReason());
	}
	else
	{
		AddError(TEXT("no player character was spawned."));
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPlayerAnimationBlueprintTest,
	"Cataclysm.PlayerBody.AnAnimationBlueprintDrivesIt")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmPlayerBodyTest::SpawnPlayer(World);
	if (USkeletalMeshComponent* Mesh =
			CataclysmPlayerBodyTest::DressedMeshOf(Player))
	{
		// WITHOUT THIS THE CHARACTER SLIDES. Single-node mode plays one clip and
		// blends nothing, so walking does not blend into the idle. ABP_Unarmed
		// ships beside the Mannequin and is copied with it, unlike
		// ABP_AbyssalWarden which has never been authored, so reaching the
		// fallback here means a path is wrong rather than an asset being owed.
		TestEqual(TEXT("an animation Blueprint is driving the mesh"),
			static_cast<int32>(Mesh->GetAnimationMode()),
			static_cast<int32>(EAnimationMode::AnimationBlueprint));

		TestNotNull(TEXT("and an animation instance exists"),
			Mesh->GetAnimInstance());
	}
	else if (Player)
	{
		CataclysmTestSkip::ReportSkippedHalf(
			*this, CataclysmPlayerBodyTest::NoArtReason());
	}
	else
	{
		AddError(TEXT("no player character was spawned."));
	}

	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// Dying, and coming back
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmPlayerDeathClipTest,
	"Cataclysm.PlayerBody.DyingPlaysADeathClipAndHoldsIt")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmPlayerBodyTest::SpawnPlayer(World);
	if (USkeletalMeshComponent* Mesh =
			CataclysmPlayerBodyTest::DressedMeshOf(Player))
	{
		Player->HandleDeath();

		TestTrue(TEXT("the character is dead"),
			UCataclysmSkillEffects::IsDead(Player));

		// SINGLE-NODE MODE IS WHAT HOLDS THE POSE. Played as a montage through
		// the animation Blueprint's DefaultSlot the clip would blend back out to
		// the locomotion graph when it ended, and since the longest death clip
		// is 1.1333 seconds against a 3 second respawn delay, the corpse would
		// stand up and wait there for nearly two seconds.
		TestEqual(TEXT("the mesh is playing a single clip, not the graph"),
			static_cast<int32>(Mesh->GetAnimationMode()),
			static_cast<int32>(EAnimationMode::AnimationSingleNode));

		if (const UAnimSingleNodeInstance* Single = Mesh->GetSingleNodeInstance())
		{
			const UAnimationAsset* Playing = Single->GetAnimationAsset();
			if (TestNotNull(TEXT("and something is playing"), Playing))
			{
				TestTrue(TEXT("and it is one of the death clips"),
					Playing->GetName().StartsWith(TEXT("MM_Death_")));

				TestFalse(TEXT("and it does not loop, so it holds its last frame"),
					Single->IsLooping());
			}
		}
		else
		{
			AddError(TEXT("the mesh is in single-node mode but has no "
						  "single-node instance to read the clip from."));
		}
	}
	else if (Player)
	{
		CataclysmTestSkip::ReportSkippedHalf(
			*this, CataclysmPlayerBodyTest::NoArtReason());
	}
	else
	{
		AddError(TEXT("no player character was spawned."));
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPlayerRevivesIntoTheGraphTest,
	"Cataclysm.PlayerBody.ComingBackPutsTheAnimationBlueprintOnAgain")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmPlayerBodyTest::SpawnPlayer(World);
	if (USkeletalMeshComponent* Mesh =
			CataclysmPlayerBodyTest::DressedMeshOf(Player))
	{
		Player->HandleDeath();
		TestEqual(TEXT("dying left the mesh playing a single clip"),
			static_cast<int32>(Mesh->GetAnimationMode()),
			static_cast<int32>(EAnimationMode::AnimationSingleNode));

		// DRIVEN DIRECTLY RATHER THAN WAITED FOR, which is the same reason
		// Cataclysm.Death.APlayerStandsBackUpRatherThanBeingRemoved does it: a
		// world built by UWorld::CreateWorld is never ticked, so its timers
		// never fire. Revive is public for exactly this.
		Player->Revive();

		// THE ASSERTION THIS FILE EXISTS FOR. Nothing else takes the mesh out of
		// single-node mode, so without the call in Revive a character that came
		// back would walk around frozen in the pose it died in.
		TestEqual(TEXT("and coming back put the animation Blueprint on again"),
			static_cast<int32>(Mesh->GetAnimationMode()),
			static_cast<int32>(EAnimationMode::AnimationBlueprint));

		TestFalse(TEXT("and the character is no longer dead"),
			UCataclysmSkillEffects::IsDead(Player));
	}
	else if (Player)
	{
		CataclysmTestSkip::ReportSkippedHalf(
			*this, CataclysmPlayerBodyTest::NoArtReason());
	}
	else
	{
		AddError(TEXT("no player character was spawned."));
	}

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
