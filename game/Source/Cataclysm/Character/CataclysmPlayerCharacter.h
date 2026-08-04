// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmCharacterBase.h"
#include "AbilitySystem/CataclysmAbilitySet.h"
#include "CataclysmPlayerCharacter.generated.h"

class UCameraComponent;
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

protected:
	virtual void InitAbilityActorInfo() override;

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

private:
	/** What the starting ability set granted, so it can be removed on unequip. */
	FCataclysmAbilitySetHandles GrantedHandles;
};
