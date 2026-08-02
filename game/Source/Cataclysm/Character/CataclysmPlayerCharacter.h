// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmCharacterBase.h"
#include "AbilitySystem/CataclysmAbilitySet.h"
#include "CataclysmPlayerCharacter.generated.h"

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

private:
	/** What the starting ability set granted, so it can be removed on unequip. */
	FCataclysmAbilitySetHandles GrantedHandles;
};
