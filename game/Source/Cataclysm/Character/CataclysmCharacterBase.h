// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "CataclysmCharacterBase.generated.h"

class UAttributeSet;
class UCataclysmAbilitySet;
class UCataclysmAbilitySystemComponent;

/**
 * Shared base for anything with abilities: the player and every enemy.
 *
 * Does not own an ability system component. Where the component lives differs
 * between the player (player state, survives respawn) and enemies (the pawn
 * itself), so ownership is decided by the subclass and reached through
 * GetAbilitySystemComponent().
 */
UCLASS(Abstract)
class CATACLYSM_API ACataclysmCharacterBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ACataclysmCharacterBase();

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

protected:
	/**
	 * Points the ability system at this pawn and grants the starting kit.
	 * Must run on both server and client; where it is called from differs, which
	 * is the single most common source of bugs in this area.
	 */
	virtual void InitAbilityActorInfo();

	/** Granted on the server once the ability system is initialised. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Abilities")
	TObjectPtr<UCataclysmAbilitySet> StartingAbilitySet;
};
