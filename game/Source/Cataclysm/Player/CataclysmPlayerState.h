// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "CataclysmPlayerState.generated.h"

class UCataclysmAbilitySystemComponent;
class UCataclysmVitalAttributeSet;
class UCataclysmPrimaryAttributeSet;
class UCataclysmCombatAttributeSet;
class UCataclysmResistanceAttributeSet;
class UCataclysmClassResourceAttributeSet;

/**
 * Owns the player's ability system component and attribute sets.
 *
 * WHY THE PLAYER STATE AND NOT THE PAWN.
 *
 * The design has the player die and respawn at the capital, at a cost of 5 to 15
 * days depending on difficulty. Death is a routine, repeated event, not the end
 * of a session. A pawn is destroyed on death; the player state is not.
 *
 * Putting the ability system on the pawn would mean every death destroyed the
 * player's attributes, active effects, cooldowns and granted abilities, and all
 * of it would have to be saved and restored by hand. On the player state, it
 * simply survives.
 *
 * Enemies do the opposite and own their component on the pawn, because an enemy
 * that dies is gone.
 */
UCLASS()
class CATACLYSM_API ACataclysmPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ACataclysmPlayerState();

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	UCataclysmAbilitySystemComponent* GetCataclysmAbilitySystemComponent() const { return AbilitySystemComponent; }

	const UCataclysmVitalAttributeSet* GetVitalAttributes() const { return VitalAttributes; }
	const UCataclysmPrimaryAttributeSet* GetPrimaryAttributes() const { return PrimaryAttributes; }
	const UCataclysmCombatAttributeSet* GetCombatAttributes() const { return CombatAttributes; }
	const UCataclysmResistanceAttributeSet* GetResistanceAttributes() const { return ResistanceAttributes; }
	const UCataclysmClassResourceAttributeSet* GetClassResourceAttributes() const { return ClassResourceAttributes; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Abilities")
	TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystemComponent;

	/**
	 * A player carries all five sets. Enemies carry only the three that describe
	 * a combatant: they have no attribute points to spend and no class tree, so
	 * giving them the primary or class resource sets would be dead weight on
	 * every spawn.
	 */
	UPROPERTY()
	TObjectPtr<UCataclysmVitalAttributeSet> VitalAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmPrimaryAttributeSet> PrimaryAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmCombatAttributeSet> CombatAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmResistanceAttributeSet> ResistanceAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmClassResourceAttributeSet> ClassResourceAttributes;
};
