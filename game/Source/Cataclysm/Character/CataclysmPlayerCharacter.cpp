// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmPlayerCharacter.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Half-height and radius of the collision capsule, in centimetres. */
	constexpr float CapsuleRadius = 42.0f;
	constexpr float CapsuleHalfHeight = 96.0f;

	/** The engine's basic shapes are 100cm cubes of space, so a scale of 1 is 100cm. */
	constexpr float BasicShapeSize = 100.0f;
}

ACataclysmPlayerCharacter::ACataclysmPlayerCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);

	// The character faces where it is going, not where the camera points. A
	// top-down game has no independent aim direction to keep the body aligned
	// with, and letting the controller drive yaw would make the character spin
	// with the mouse rather than turn toward its destination.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0f, 640.0f, 0.0f);

	// Movement is confined to the horizontal plane. Dungeon floors are flat and
	// this keeps a click on a wall from pushing the character upward.
	Movement->bConstrainToPlane = true;
	Movement->bSnapToPlaneAtStart = true;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);

	// Absolute rotation: the arm keeps its world angle while the character turns
	// underneath it. Without this the whole view swings every time the character
	// changes direction.
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->TargetArmLength = 800.0f;
	CameraBoom->SetRelativeRotation(FRotator(-60.0f, 0.0f, 0.0f));

	// No collision test. The arm would otherwise pull the camera in whenever
	// scenery passed between it and the character, which in a dungeon corridor
	// is most of the time.
	CameraBoom->bDoCollisionTest = false;

	TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCamera->bUsePawnControlRotation = false;

	// The arm length and the sixty degree pitch are a starting point taken from
	// Unreal's own top-down template, not a tuned choice. They are expected to
	// change once there is art and a real sense of scale.

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(RootComponent);
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetRelativeScale3D(FVector(
		(CapsuleRadius * 2.0f) / BasicShapeSize,
		(CapsuleRadius * 2.0f) / BasicShapeSize,
		(CapsuleHalfHeight * 2.0f) / BasicShapeSize));

	PlaceholderFacingMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderFacingMarker"));
	PlaceholderFacingMarker->SetupAttachment(RootComponent);
	PlaceholderFacingMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Tipped forward so the cone points along +X, which is the character's
	// forward axis, and pushed out to the front of the body at head height.
	PlaceholderFacingMarker->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
	PlaceholderFacingMarker->SetRelativeLocation(FVector(CapsuleRadius, 0.0f, CapsuleHalfHeight * 0.5f));
	PlaceholderFacingMarker->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.4f));

	// Found by path rather than referenced as an asset, because these are engine
	// content and the project's own Content folder holds no meshes at all yet.
	// A failure here is not fatal: the capsule still moves, it is just invisible.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(CylinderMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(
		TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeMesh.Succeeded())
	{
		PlaceholderFacingMarker->SetStaticMesh(ConeMesh.Object);
	}
}

UAbilitySystemComponent* ACataclysmPlayerCharacter::GetAbilitySystemComponent() const
{
	if (const ACataclysmPlayerState* PS = GetPlayerState<ACataclysmPlayerState>())
	{
		return PS->GetAbilitySystemComponent();
	}
	return nullptr;
}

void ACataclysmPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Server path. The player state exists by the time possession happens.
	InitAbilityActorInfo();
}

void ACataclysmPlayerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// Client path. PossessedBy does not run on clients, so without this the
	// owning client's ability system would never learn its avatar, and every
	// locally predicted activation and every attribute-driven widget would fail
	// with no obvious cause.
	InitAbilityActorInfo();
}

void ACataclysmPlayerCharacter::InitAbilityActorInfo()
{
	ACataclysmPlayerState* PS = GetPlayerState<ACataclysmPlayerState>();
	if (!PS)
	{
		return;
	}

	UCataclysmAbilitySystemComponent* ASC = PS->GetCataclysmAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}

	// Owner is the player state, which holds the component and survives death.
	// Avatar is this pawn, which is what acts in the world.
	ASC->InitAbilityActorInfo(PS, this);

	// Granting is server-only; the results replicate.
	if (HasAuthority() && StartingAbilitySet)
	{
		GrantedHandles.TakeFromAbilitySystem(ASC);
		StartingAbilitySet->GiveToAbilitySystem(ASC, &GrantedHandles, this);
	}
}
