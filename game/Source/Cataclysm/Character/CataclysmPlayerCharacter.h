// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmCharacterBase.h"
#include "AbilitySystem/CataclysmAbilitySet.h"
#include "CataclysmPlayerCharacter.generated.h"

class UCameraComponent;
class UCataclysmWeaponSlotsComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/**
 * The player pawn. Its ability system component lives on the player state, so
 * that it survives the death and respawn the design treats as routine.
 */
UCLASS()
class CATACLYSM_API ACataclysmPlayerCharacter : public ACataclysmCharacterBase
{
	GENERATED_BODY()

public:
	ACataclysmPlayerCharacter();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/** Server: the pawn has been possessed and the player state is available. */
	virtual void PossessedBy(AController* NewController) override;

	/** Client: the player state has replicated. There is no PossessedBy here. */
	virtual void OnRep_PlayerState() override;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Moves the camera nearer or further by whole wheel notches.
	 *
	 * Positive zooms in, which is what a wheel pushed forward reports and what
	 * every game in the genre does with it. The result is clamped, so a player
	 * holding the wheel down cannot end up inside the character or looking at the
	 * whole level.
	 */
	void AddCameraZoom(float Notches);

	/**
	 * The distance the camera is moving toward, in centimetres.
	 *
	 * This is not the same as the boom's current length: the camera eases toward
	 * this over a few frames rather than jumping. Reading the boom mid-glide
	 * gives an intermediate value, which is why the target is exposed separately.
	 */
	float GetTargetCameraDistance() const { return TargetCameraDistance; }

protected:
	virtual void InitAbilityActorInfo() override;

	/**
	 * How near and how far the camera may get, in centimetres.
	 *
	 * The range is a judgement, not something the genre settles. Path of Exile 2,
	 * Last Epoch and Diablo 4 all put zoom on the wheel between a fixed minimum
	 * and maximum, and all three keep the range deliberately narrow, but their
	 * numbers are in their own units and their own art scale and do not transfer.
	 * These were chosen by looking at the game and are expected to change.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Camera", meta = (ClampMin = "1.0"))
	float MinCameraDistance = 500.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Camera", meta = (ClampMin = "1.0"))
	float MaxCameraDistance = 1200.0f;

	/** How far one wheel notch moves the camera, in centimetres. Seven notches
	 *  cover the whole range, which is a short flick of the wheel. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Camera", meta = (ClampMin = "1.0"))
	float CameraZoomStep = 100.0f;

	/**
	 * How quickly the camera reaches a new distance. Larger is faster.
	 *
	 * Eased rather than snapped, which is a judgement. One notch is an eighth of
	 * the whole range, and moving that far between two frames reads as the view
	 * cutting rather than the camera moving. At 10 the camera covers most of a
	 * notch in about a fifth of a second.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Camera", meta = (ClampMin = "0.1"))
	float CameraZoomInterpSpeed = 10.0f;

	/** Holds the camera above and behind. Uses absolute rotation, so it does not
	 *  spin when the character turns. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Camera")
	TObjectPtr<UCameraComponent> TopDownCamera;

	/**
	 * A stand-in body, so there is something to see before there is any art.
	 *
	 * Two engine primitives: a cylinder for the body and a cone for the nose.
	 * The nose is not decoration. The character turns to face the direction it
	 * is moving, and with a bare cylinder that rotation is invisible, so there
	 * is no way to tell whether facing is working.
	 *
	 * Both come from /Engine/BasicShapes, so this adds no asset to the project
	 * and nothing to Git LFS. Replaced when there is a real mesh.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderFacingMarker;

	/**
	 * Fills the six ability slots from the equipped weapon.
	 *
	 * On the pawn rather than the player state, unlike the ability system
	 * component: what is held is a property of the body, and a respawned
	 * character equips again rather than inheriting what the corpse held.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Weapon")
	TObjectPtr<UCataclysmWeaponSlotsComponent> WeaponSlots;

private:
	/** What the starting ability set granted, so it can be removed on unequip. */
	FCataclysmAbilitySetHandles GrantedHandles;

	/** Where the camera is heading. Set from the boom's own length at BeginPlay,
	 *  so the resting distance is stated once, in the constructor. */
	float TargetCameraDistance = 0.0f;
};
