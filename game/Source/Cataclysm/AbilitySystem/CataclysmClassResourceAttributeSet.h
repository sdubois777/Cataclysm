// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmAttributeAccessors.h"
#include "CataclysmClassResourceAttributeSet.generated.h"

/**
 * Fervour: the one resource every class shares.
 *
 * ONE BAR RATHER THAN ONE PER CLASS, decided by the project owner on 2026-08-25.
 * A two-handed weapon can roll 8 damage types and each unlocks 3 classes, so one
 * character can reach all 24 class trees, and 24 separately-generating bars is
 * not readable. What differs by class is how Fervour is filled and what it is
 * spent on, not which bar it goes into. A character in several trees therefore
 * has several ways to fill it and several things to spend it on, which is what
 * multiclassing buys beyond the nodes themselves. `docs/DECISIONS.md` has the
 * reasoning and the genre precedent.
 *
 * THE NAME IN THE DATA IS STILL `class_resource`, in `game/Data/ClassStats.csv`,
 * `game/Data/PassiveEffects.csv` and `UCataclysmPlayerClassStats::StatToAttribute`.
 * That name is accurate and is never shown to a player, so it was left alone
 * rather than renamed across three files for no gain.
 *
 * A SEPARATE SET FROM THE VITALS, which is what allows it to be granted to a
 * character that has a generator and withheld from one that has none.
 *
 * BEHAVIOUR IS NOT MODELLED HERE, only the pool. Fervour does not decay by
 * default, and a class may add a rule that changes that on its own starting
 * node -- the Berserker's generator empties it at 10 per second out of combat,
 * the Saboteur's adds no rule at all. Whether it fills from critical strikes,
 * from health lost or from placing traps likewise belongs with the passive
 * trees. The pool is 100 for every class and 150 for the Ritualist.
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
