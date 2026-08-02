// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayAbilitySpecHandle.h"
#include "CataclysmAbilitySet.generated.h"

class UAttributeSet;
class UCataclysmAbilitySystemComponent;
class UCataclysmGameplayAbility;
class UGameplayEffect;

/**
 * Records everything an ability set granted, so it can all be removed again.
 *
 * Equipping and unequipping weapons changes the available skill pool constantly
 * in this design, so granting has to be reversible without guesswork about what
 * came from where.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmAbilitySetHandles
{
	GENERATED_BODY()

	void AddAbility(const FGameplayAbilitySpecHandle& Handle);
	void AddEffect(const FActiveGameplayEffectHandle& Handle);
	void AddAttributeSet(UAttributeSet* Set);

	/** Removes everything this handle recorded. Safe to call more than once. */
	void TakeFromAbilitySystem(UCataclysmAbilitySystemComponent* AbilitySystem);

private:
	UPROPERTY() TArray<FGameplayAbilitySpecHandle> AbilitySpecHandles;
	UPROPERTY() TArray<FActiveGameplayEffectHandle> EffectHandles;
	UPROPERTY() TArray<TObjectPtr<UAttributeSet>> GrantedAttributeSets;
};

USTRUCT(BlueprintType)
struct FCataclysmAbilitySetEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UCataclysmGameplayAbility> Ability;

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	int32 AbilityLevel = 1;
};

/**
 * A bundle of abilities, always-on effects and attribute sets granted together.
 *
 * Used for a character's starting kit, and later for the skills a weapon makes
 * available while it is equipped.
 */
UCLASS(BlueprintType, Const)
class CATACLYSM_API UCataclysmAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Grants everything in this set. Server only; granting on a client does
	 * nothing useful and the ensure will fire.
	 *
	 * @param OutHandles optional; required if the grant is ever to be undone.
	 */
	void GiveToAbilitySystem(UCataclysmAbilitySystemComponent* AbilitySystem,
							 FCataclysmAbilitySetHandles* OutHandles = nullptr,
							 UObject* SourceObject = nullptr) const;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Abilities", meta = (TitleProperty = Ability))
	TArray<FCataclysmAbilitySetEntry> GrantedAbilities;

	/** Applied once at grant time and left active. Passives, not costs. */
	UPROPERTY(EditDefaultsOnly, Category = "Effects")
	TArray<TSubclassOf<UGameplayEffect>> GrantedEffects;

	UPROPERTY(EditDefaultsOnly, Category = "Attributes")
	TArray<TSubclassOf<UAttributeSet>> GrantedAttributeSets;
};
