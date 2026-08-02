// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "CataclysmGameplayAbility.generated.h"

/**
 * Which of the seven input slots an ability occupies.
 *
 * The design ties skills to the combination of weapon type and damage type: the
 * equipped weapons determine the POOL of available skills, and the player then
 * assigns chosen skills to slots. This enum is the slot side of that; it is what
 * lets input be bound to "whatever is in the Heavy slot" instead of to a
 * specific ability.
 */
UENUM(BlueprintType)
enum class ECataclysmAbilitySlot : uint8
{
	None		UMETA(DisplayName = "None"),
	BasicAttack	UMETA(DisplayName = "Basic Attack (automatic)"),
	Heavy		UMETA(DisplayName = "Heavy (right mouse)"),
	Special		UMETA(DisplayName = "Special (Q)"),
	Support		UMETA(DisplayName = "Support (W)"),
	Aura		UMETA(DisplayName = "Aura (E, toggle)"),
	Ultimate	UMETA(DisplayName = "Ultimate (R)"),
	Movement	UMETA(DisplayName = "Movement (space)"),
};

/**
 * Base class for every ability in the game.
 *
 * Defaults to InstancedPerActor. Abilities in this project hold per-activation
 * state -- charge levels, deployable handles, stacking counters -- so the
 * non-instanced policy is not workable, and per-execution instancing allocates
 * far more than is needed.
 */
UCLASS(Abstract)
class CATACLYSM_API UCataclysmGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UCataclysmGameplayAbility();

	/** Which input slot this ability is designed to sit in. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Ability")
	ECataclysmAbilitySlot Slot = ECataclysmAbilitySlot::None;

	/**
	 * Activate as soon as the ability is granted, and keep it active.
	 * Used for passive effects and for auras before the player toggles them.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Ability")
	bool bActivateOnGranted = false;

	virtual void OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo,
							 const FGameplayAbilitySpec& Spec) override;
};
