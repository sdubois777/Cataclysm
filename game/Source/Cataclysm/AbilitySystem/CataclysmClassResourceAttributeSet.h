// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmAttributeAccessors.h"
#include "CataclysmClassResourceAttributeSet.generated.h"

/**
 * The class resource: Resolve for the Bulwark, Fury for the Berserker, and so
 * on. One pool, whose name and behaviour depend on the class.
 *
 * A SEPARATE SET SO A CHARACTER ONLY CARRIES WHAT IT USES. Multiclassing means a
 * character can hold several class trees at once, and a character with no class
 * resource should not carry a dead attribute. Keeping this apart from the vitals
 * is what allows the set to be granted per class rather than to everyone.
 *
 * BEHAVIOUR IS NOT MODELLED HERE, only the pool. Whether a resource builds from
 * damage dealt, from damage taken, or is reserved by active minions rather than
 * spent, and whether it decays out of combat, all differ per class and belong
 * with the passive trees. The three designed resources run 0 to 100; the
 * Ritualist's proposed pool is 150.
 */
UCLASS()
class CATACLYSM_API UCataclysmClassResourceAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UCataclysmClassResourceAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_ClassResource)
	FGameplayAttributeData ClassResource;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, ClassResource)

	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_MaxClassResource)
	FGameplayAttributeData MaxClassResource;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, MaxClassResource)

	static TArray<FGameplayAttribute> GetAllAttributes();

protected:
	UFUNCTION() void OnRep_ClassResource(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MaxClassResource(const FGameplayAttributeData& OldValue);
};
