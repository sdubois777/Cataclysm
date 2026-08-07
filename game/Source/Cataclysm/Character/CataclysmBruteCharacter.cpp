// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmBruteCharacter.h"
#include "Cataclysm.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/SoftObjectPath.h"

const TCHAR* ACataclysmBruteCharacter::BodyMeshPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Meshes/Rampage.Rampage");

const TCHAR* ACataclysmBruteCharacter::BodyAnimBlueprintPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Rampage_AnimBlueprint.Rampage_AnimBlueprint_C");

ACataclysmBruteCharacter::ACataclysmBruteCharacter()
{
	// The designed numbers, overriding the base enemy's judgement figures. Each
	// one is cited on its declaration in the header.
	MeleeReachCm = DesignedMeleeReachCm;
	AttackIntervalSeconds = DesignedAttackIntervalSeconds;

	// NoticeRadiusCm is left at the base's 1500. There is no design figure for
	// how far any enemy notices the player -- the base class says so about its
	// own three numbers -- so inventing one for the Brute would be inventing it
	// for all seven.

	GetCapsuleComponent()->InitCapsuleSize(BruteCapsuleRadius, BruteCapsuleHalfHeight);

	// SLOW, AND SLOW TO TURN. These two are the whole of "heavily armored slow
	// melee. Can be outmaneuvered". Without them a Brute is a training dummy
	// with more health.
	GetCharacterMovement()->MaxWalkSpeed = DesignedWalkSpeedCmPerSecond;
	GetCharacterMovement()->RotationRate =
		FRotator(0.0f, DesignedTurnRateDegreesPerSecond, 0.0f);
}

void ACataclysmBruteCharacter::BeginPlay()
{
	Super::BeginPlay();

	ResolveBody();
}

bool ACataclysmBruteCharacter::ResolveBody()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return false;
	}

	USkeletalMesh* Body = Cast<USkeletalMesh>(
		FSoftObjectPath(BodyMeshPath).TryLoad());

	if (!Body)
	{
		// NOT AN ERROR, AND SAID OUT LOUD RATHER THAN LEFT TO BE NOTICED. The
		// Paragon packs are gitignored, so this is the expected state on a fresh
		// clone and in continuous integration. The placeholder cylinder the base
		// class made stays visible, so the Brute is still there to fight -- it
		// just looks like every other enemy.
		UE_LOG(LogCataclysm, Warning,
			TEXT("Brute art not found at %s, so it is keeping the placeholder "
				 "cylinder. This is expected without the Paragon Rampage pack; "
				 "see game/docs/enemy-source-assets.md."),
			BodyMeshPath);
		return false;
	}

	MeshComponent->SetSkeletalMesh(Body);

	// FEET ON THE CAPSULE BOTTOM. A skeletal mesh is authored with its origin at
	// the feet, and the capsule's origin is its centre, so the mesh drops by the
	// half-height. The yaw is the engine's convention for character meshes,
	// which face -Y while the actor faces +X.
	MeshComponent->SetRelativeLocationAndRotation(
		FVector(0.0f, 0.0f, -BruteCapsuleHalfHeight),
		FRotator(0.0f, -90.0f, 0.0f));

	if (UClass* AnimClass = Cast<UClass>(
			FSoftObjectPath(BodyAnimBlueprintPath).TryLoad()))
	{
		MeshComponent->SetAnimInstanceClass(AnimClass);
	}
	else
	{
		// A mesh with no animation blueprint stands in its reference pose. Worth
		// saying, because "the Brute does not move its legs" is otherwise a
		// puzzling thing to see in a level.
		UE_LOG(LogCataclysm, Warning,
			TEXT("Brute animation blueprint not found at %s, so it will stand "
				 "in its reference pose."),
			BodyAnimBlueprintPath);
	}

	// OTHERWISE THE CYLINDER SITS INSIDE THE DEMON. The base class creates
	// PlaceholderBody in its constructor and nothing about assigning a skeletal
	// mesh removes it.
	if (PlaceholderBody)
	{
		PlaceholderBody->SetVisibility(false);
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("Brute is wearing %s."), BodyMeshPath);

	return true;
}
