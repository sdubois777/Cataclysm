// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmCharacterBase.h"
#include "AbilitySystem/CataclysmAbilitySet.h"
#include "CataclysmEnemyCharacter.generated.h"

class UCataclysmAbilitySystemComponent;
class UCataclysmVitalAttributeSet;
class UCataclysmCombatAttributeSet;
class UCataclysmResistanceAttributeSet;
class UStaticMeshComponent;

/**
 * Base for every enemy.
 *
 * Owns its ability system component directly, unlike the player. An enemy that
 * dies is destroyed and nothing about it needs to survive, so there is no reason
 * to separate the component from the pawn.
 */
UCLASS()
class CATACLYSM_API ACataclysmEnemyCharacter : public ACataclysmCharacterBase
{
	GENERATED_BODY()

public:
	ACataclysmEnemyCharacter();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void BeginPlay() override;

	/**
	 * Sets both maximum health and current health, before or after BeginPlay.
	 *
	 * WHY A SETTER RATHER THAN A PROPERTY ON THE CLASS. An enemy's real health
	 * comes from its rarity tier and the run's difficulty, neither of which
	 * exists yet, so nothing that ships should carry a hard-coded figure. This
	 * is for whoever is placing an enemy to say what they want, and today that
	 * is the sandbox training dummy spawner. Issue #39 replaces it.
	 *
	 * Both together, because setting the maximum alone leaves an enemy at its
	 * old current value, and setting the current alone is clamped to the old
	 * maximum -- so either one on its own quietly does nothing useful.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetHealth(float NewMaxHealth);

	/** A stand-in body, so an enemy is visible before there is any art. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

protected:
	virtual void InitAbilityActorInfo() override;

	/**
	 * Writes StartingMaxHealth onto the attributes, if they are ready for it.
	 *
	 * Called both from SetHealth and from InitAbilityActorInfo, so it does not
	 * matter which happens first.
	 */
	void ApplyStartingHealth();

	/** What SetHealth was last asked for. Zero means the attribute set's own default. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float StartingMaxHealth = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Abilities")
	TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystemComponent;

	/** Three sets, not the player's five. See the constructor for why. */
	UPROPERTY()
	TObjectPtr<UCataclysmVitalAttributeSet> VitalAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmCombatAttributeSet> CombatAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmResistanceAttributeSet> ResistanceAttributes;

private:
	FCataclysmAbilitySetHandles GrantedHandles;
};
