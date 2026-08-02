// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "CataclysmAttributeSet.generated.h"

/**
 * Generates the four accessors the Gameplay Ability System expects for every
 * attribute: a getter, a setter, an initialiser, and the FGameplayAttribute
 * property accessor used to reference the attribute in data.
 */
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * The vital attributes: health and its maximum.
 *
 * DELIBERATELY MINIMAL. The full attribute model -- the eight primary attributes,
 * eight resistances, energy shield, mana, and the per-class resources -- is
 * separate work. This set exists so the ability system can be proven end to end:
 * an ability activates, applies a gameplay effect, and an attribute changes.
 *
 * Damage is applied through the Damage meta attribute rather than by writing to
 * Health directly. A meta attribute is not replicated and is zeroed after every
 * execution; it exists so that mitigation, armour, block, resistance and energy
 * shield can all be resolved in one place instead of being scattered across
 * every effect that deals damage.
 */
UCLASS()
class CATACLYSM_API UCataclysmAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UCataclysmAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UCataclysmAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UCataclysmAttributeSet, MaxHealth)

	/** Meta attribute. Not replicated. Consumed and zeroed in PostGameplayEffectExecute. */
	UPROPERTY(BlueprintReadOnly, Category = "Vitals|Meta")
	FGameplayAttributeData Damage;
	ATTRIBUTE_ACCESSORS(UCataclysmAttributeSet, Damage)

protected:
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);
};
