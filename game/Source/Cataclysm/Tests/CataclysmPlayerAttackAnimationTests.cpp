// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
// For granting a charge and a strike to the same character, so that what a
// Movement skill plays can be told from what a Strike plays. Issue #1180.
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the animation the player plays when it uses a skill. Issue #1126.
 *
 * WHAT THESE GUARD. The player used every skill it had without moving at all:
 * the cast effect flashed, damage landed, and the body stood still through it.
 *
 * THE HALF THAT BREAKS SILENTLY IS THE ROOT MOTION. All three attack clips carry
 * it, and `UCharacterMovementComponent` takes root motion from montages by
 * default. Left alone, every basic attack shoves the character a step forward --
 * and the basic attack fires by itself, several times a second, at whatever is
 * in reach. Nothing about the code that plays the clip would look wrong; the
 * character would drift across the floor while fighting.
 * `ASwingDoesNotWalkTheCharacterForward` below is the assertion this file exists
 * for.
 *
 * THESE NEED THE MANNEQUIN ASSETS AND SAY SO WHEN THEY ARE ABSENT. A checkout
 * without `game/Content/Characters/Mannequins/` has no clips to play and no
 * animation Blueprint to play them into, which is correct behaviour -- the
 * character fights identically without moving -- so these report through
 * `CataclysmTestSkip::ReportSkippedHalf` rather than failing.
 *
 * WHAT THESE DELIBERATELY DO NOT COVER. Whether the swing looks like a swing,
 * whether it suits the weapon being held, or whether it lands where the damage
 * does. The automation command passes `-nullrhi`, so nothing reaches a screen
 * under test. Somebody has to look.
 */

namespace CataclysmPlayerAttackTest
{
	/** How many clips the character cycles through. */
	constexpr int32 CycledClips = 3;

	ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World)
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

	/** The animation instance, or null when the Mannequin art is absent. */
	UAnimInstance* DressedAnimInstanceOf(ACataclysmPlayerCharacter* Player)
	{
		USkeletalMeshComponent* Mesh = Player ? Player->GetMesh() : nullptr;
		return Mesh ? Mesh->GetAnimInstance() : nullptr;
	}

	/** The clip a dynamic montage was built around, or null. */
	const UAnimSequenceBase* ClipInside(const UAnimMontage* Montage)
	{
		if (!Montage || Montage->SlotAnimTracks.Num() == 0)
		{
			return nullptr;
		}

		const FAnimTrack& Track = Montage->SlotAnimTracks[0].AnimTrack;
		if (Track.AnimSegments.Num() == 0)
		{
			return nullptr;
		}

		return Track.AnimSegments[0].GetAnimReference();
	}

	FString NoArtReason()
	{
		return TEXT("game/Content/Characters/Mannequins/ is not on this "
					"machine, so there is no animation Blueprint to play a "
					"swing into and no clip to play. That using a skill calls "
					"through to the character IS checked; what it plays is "
					"not. See game/docs/player-source-assets.md.");
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// The swing happens
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmPlayerSwingsTest,
	"Cataclysm.PlayerAttack.UsingASkillPlaysAClipThroughTheGraphSlot")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmPlayerAttackTest::SpawnPlayer(World);
	if (TestNotNull(TEXT("a player"), Player))
	{
		UAnimInstance* AnimInstance =
			CataclysmPlayerAttackTest::DressedAnimInstanceOf(Player);

		if (AnimInstance)
		{
			TestNull(TEXT("nothing is playing to begin with"),
				AnimInstance->GetCurrentActiveMontage());

			Player->PlayAttackAnimation();

			UAnimMontage* Montage = AnimInstance->GetCurrentActiveMontage();
			if (TestNotNull(TEXT("a swing is playing"), Montage))
			{
				// THROUGH THE SLOT, WHICH IS WHAT KEEPS LOCOMOTION RUNNING.
				// Played onto the component instead -- what a death does -- the
				// swing would replace the graph and the character would hold
				// its last frame.
				if (Montage->SlotAnimTracks.Num() > 0)
				{
					TestEqual(TEXT("into the animation Blueprint's slot"),
						Montage->SlotAnimTracks[0].SlotName,
						FName(TEXT("DefaultSlot")));
				}
				else
				{
					AddError(TEXT("the montage has no slot track at all."));
				}
			}
		}
		else
		{
			CataclysmTestSkip::ReportSkippedHalf(
				*this, CataclysmPlayerAttackTest::NoArtReason());
		}
	}

	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// The swing does not move the character
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmSwingRootMotionTest,
	"Cataclysm.PlayerAttack.ASwingDoesNotWalkTheCharacterForward")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmPlayerAttackTest::SpawnPlayer(World);
	if (TestNotNull(TEXT("a player"), Player))
	{
		UAnimInstance* AnimInstance =
			CataclysmPlayerAttackTest::DressedAnimInstanceOf(Player);

		if (AnimInstance)
		{
			// WHAT THIS CAN AND CANNOT SEE, SAID FIRST. A world built by
			// UWorld::CreateWorld is never ticked, so no animation ever
			// advances and no test in this project can watch a character move.
			// This checks the setting the engine actually consults. Whether the
			// character stays still on screen is something a person has to look
			// at, and the first version of this feature shipped because a check
			// of this shape was mistaken for one.
			//
			// TRUE IS THE WANTED ANSWER HERE, WHICH READS BACKWARDS.
			// ShouldExtractRootMotion is true for RootMotionFromEverything and
			// for IgnoreRootMotion, and false for the RootMotionFromMontagesOnly
			// an animation instance starts at. IgnoreRootMotion means "extract
			// it but do not apply it": the motion is taken out of the pose, so
			// the mesh stays on the capsule, and then discarded, so nothing
			// moves. So the character is correctly set exactly when this is
			// true, and the default it has to be moved away from makes it false.
			TestTrue(TEXT("the character is set to take root motion out of the "
						  "pose and then discard it"),
				AnimInstance->ShouldExtractRootMotion());

			Player->PlayAttackAnimation();

			UAnimMontage* Montage = AnimInstance->GetCurrentActiveMontage();
			if (TestNotNull(TEXT("a swing is playing"), Montage))
			{
				// AND THE CLIP DOES CARRY ROOT MOTION, which is what makes the
				// assertion above load-bearing rather than a check against a
				// default. If the clips are ever replaced with ones carrying
				// none, this says so rather than letting the guard quietly
				// become worthless.
				const UAnimSequenceBase* Clip =
					CataclysmPlayerAttackTest::ClipInside(Montage);
				if (const UAnimSequence* Sequence = Cast<UAnimSequence>(Clip))
				{
					TestTrue(TEXT("and the clip it came from does carry root "
								  "motion, so discarding it matters"),
						Sequence->bEnableRootMotion);
				}
			}
		}
		else
		{
			CataclysmTestSkip::ReportSkippedHalf(
				*this, CataclysmPlayerAttackTest::NoArtReason());
		}
	}

	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// The clips cycle
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmSwingCyclesTest,
	"Cataclysm.PlayerAttack.ThreeSwingsUseThreeDifferentClips")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmPlayerAttackTest::SpawnPlayer(World);
	if (TestNotNull(TEXT("a player"), Player))
	{
		UAnimInstance* AnimInstance =
			CataclysmPlayerAttackTest::DressedAnimInstanceOf(Player);

		if (AnimInstance)
		{
			// IN TURN RATHER THAN AT RANDOM, which is the opposite of what the
			// death clips do. A basic attack fires every two thirds of a second
			// at a fast weapon, so a random draw over three clips repeats one
			// about a third of the time and reads as the animation sticking.
			TSet<FString> Seen;
			for (int32 Swing = 0; Swing < CataclysmPlayerAttackTest::CycledClips;
				 ++Swing)
			{
				Player->PlayAttackAnimation();

				const UAnimSequenceBase* Clip =
					CataclysmPlayerAttackTest::ClipInside(
						AnimInstance->GetCurrentActiveMontage());

				if (Clip)
				{
					Seen.Add(Clip->GetName());
				}
			}

			TestEqual(TEXT("three swings used three different clips"),
				Seen.Num(), CataclysmPlayerAttackTest::CycledClips);

			// AND THE FOURTH COMES BACK ROUND TO THE FIRST, which is what makes
			// it a cycle rather than a list that runs out.
			Player->PlayAttackAnimation();
			const UAnimSequenceBase* Fourth =
				CataclysmPlayerAttackTest::ClipInside(
					AnimInstance->GetCurrentActiveMontage());

			if (TestNotNull(TEXT("a fourth swing plays something"), Fourth))
			{
				TestTrue(TEXT("and it is one already seen, so the cycle wraps"),
					Seen.Contains(Fourth->GetName()));
			}
		}
		else
		{
			CataclysmTestSkip::ReportSkippedHalf(
				*this, CataclysmPlayerAttackTest::NoArtReason());
		}
	}

	World->DestroyWorld(false);
	return true;
}


CATACLYSM_TEST(FCataclysmMovementSkillDoesNotPunchTest,
	"Cataclysm.PlayerAttack.AMovementSkillPlaysNoSwingAndAStrikeStillDoes")
{
	// WHAT THE PROJECT OWNER SAW. Pressing the Greatsword's charge on 2026-09-02:
	// "he does this weird punch when he starts moving and then switches to
	// running". Every skill shape calls `UCataclysmSkillTemplate::CommitAndBegin`
	// first, and that played an attack clip for all eight of them -- and the
	// three clips the player cycles are the Mannequin's UNARMED attacks, so a
	// charge threw a punch and then ran. Issue #1180.
	//
	// THE CONTROL IS THE HALF THAT MAKES THIS WORTH ANYTHING. A build where
	// nothing ever played a clip would satisfy the first assertion on its own, so
	// a Strike is cast on the same character straight afterwards and has to
	// still swing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmPlayerAttackTest::SpawnPlayer(World);
	if (TestNotNull(TEXT("a player"), Player))
	{
		UAnimInstance* AnimInstance =
			CataclysmPlayerAttackTest::DressedAnimInstanceOf(Player);

		UCataclysmAbilitySystemComponent* AbilitySystem =
			Cast<UCataclysmAbilitySystemComponent>(
				Player->GetAbilitySystemComponent());

		if (AnimInstance && AbilitySystem)
		{
			// ENOUGH MANA THAT NEITHER CAST CAN BE REFUSED FOR THE WRONG REASON.
			// A skill turned down for its cost animates nothing either, and
			// would read here as the shape test working.
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);

			TestNull(TEXT("nothing is playing to begin with"),
				AnimInstance->GetCurrentActiveMontage());

			// --- the charge, which must not swing ------------------------
			const FGameplayAbilitySpecHandle MoveHandle =
				AbilitySystem->GiveAbilityInSlot(
					UCataclysmMovementSkill::StaticClass(),
					ECataclysmAbilitySlot::Movement, /*Level=*/100, Player);
			FGameplayAbilitySpec* MoveSpec =
				AbilitySystem->FindAbilitySpecFromHandle(MoveHandle);
			UCataclysmMovementSkill* Charge =
				MoveSpec ? Cast<UCataclysmMovementSkill>(
							   MoveSpec->GetPrimaryInstance())
						 : nullptr;
			if (!Charge)
			{
				AddError(TEXT("Could not grant the charge."));
				return false;
			}

			Charge->SkillName = TEXT("Inexorable");
			Charge->Params = UCataclysmSkillShapes::ParseParams(
				TEXT("Mode=Charge; Range=14; Radius=2.5; Duration=1.5"));

			TestTrue(TEXT("the charge activates"),
				AbilitySystem->TryActivateAbility(
					MoveHandle, /*bAllowRemoteActivation=*/false));

			TestNull(TEXT("and it played no swing, because a charge is not one"),
				AnimInstance->GetCurrentActiveMontage());

			// --- the control: a Strike on the same character still swings --
			const FGameplayAbilitySpecHandle StrikeHandle =
				AbilitySystem->GiveAbilityInSlot(
					UCataclysmStrikeSkill::StaticClass(),
					ECataclysmAbilitySlot::Heavy, /*Level=*/100, Player);
			FGameplayAbilitySpec* StrikeSpec =
				AbilitySystem->FindAbilitySpecFromHandle(StrikeHandle);
			UCataclysmStrikeSkill* Swing =
				StrikeSpec ? Cast<UCataclysmStrikeSkill>(
								 StrikeSpec->GetPrimaryInstance())
						   : nullptr;
			if (!Swing)
			{
				AddError(TEXT("Could not grant the strike."));
				return false;
			}

			Swing->SkillName = TEXT("Executioner's Arc");
			Swing->Params = UCataclysmSkillShapes::ParseParams(
				TEXT("Radius=4; Angle=180"));

			TestTrue(TEXT("the strike activates"),
				AbilitySystem->TryActivateAbility(
					StrikeHandle, /*bAllowRemoteActivation=*/false));

			TestNotNull(TEXT("and a strike still swings"),
				AnimInstance->GetCurrentActiveMontage());
		}
		else
		{
			CataclysmTestSkip::ReportSkippedHalf(
				*this, CataclysmPlayerAttackTest::NoArtReason());
		}
	}

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
