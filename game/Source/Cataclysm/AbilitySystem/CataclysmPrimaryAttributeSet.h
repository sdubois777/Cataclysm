// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmAttributeAccessors.h"
#include "CataclysmPrimaryAttributeSet.generated.h"

/**
 * The eight attributes a player spends points on, one per level.
 *
 * These are the points themselves, not what they produce. Every one of them
 * only ever SCALES a stat that has a base value from somewhere else -- the
 * class, the equipped weapon, or the skill being used. A stat with no base
 * gains nothing from its attribute, which is how a class declines to care about
 * a stat. See the Stat Calculation section of the design document.
 *
 * The conversion from these points into the stats they scale is deliberately
 * not here. It belongs in a gameplay effect driven by data, so that changing
 * what Vitality is worth does not mean recompiling.
 */
UCLASS()
class CATACLYSM_API UCataclysmPrimaryAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UCataclysmPrimaryAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** Move speed and evasion. */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Agility)
	FGameplayAttributeData Agility;
	ATTRIBUTE_ACCESSORS(UCataclysmPrimaryAttributeSet, Agility)

	/** Critical strike chance and critical strike multiplier. */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Ferocity)
	FGameplayAttributeData Ferocity;
	ATTRIBUTE_ACCESSORS(UCataclysmPrimaryAttributeSet, Ferocity)

	/** Armor and block chance. */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Constitution)
	FGameplayAttributeData Constitution;
	ATTRIBUTE_ACCESSORS(UCataclysmPrimaryAttributeSet, Constitution)

	/** Maximum health and health regeneration. */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Vitality)
	FGameplayAttributeData Vitality;
	ATTRIBUTE_ACCESSORS(UCataclysmPrimaryAttributeSet, Vitality)

	/** Maximum mana and mana regeneration. */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Mind)
	FGameplayAttributeData Mind;
	ATTRIBUTE_ACCESSORS(UCataclysmPrimaryAttributeSet, Mind)

	/** Maximum energy shield and energy shield regeneration. */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Spirit)
	FGameplayAttributeData Spirit;
	ATTRIBUTE_ACCESSORS(UCataclysmPrimaryAttributeSet, Spirit)

	/** Cooldown reduction, area of effect, and damage over time frequency. */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Efficacy)
	FGameplayAttributeData Efficacy;
	ATTRIBUTE_ACCESSORS(UCataclysmPrimaryAttributeSet, Efficacy)

	/** Magic find and loot quantity. */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Luck)
	FGameplayAttributeData Luck;
	ATTRIBUTE_ACCESSORS(UCataclysmPrimaryAttributeSet, Luck)

	/** Every attribute on this set, for iteration in tests and interface code. */
	static TArray<FGameplayAttribute> GetAllAttributes();

	/**
	 * An attribute's point count, rounded to the nearest whole number.
	 *
	 * STATED BY THE PROJECT OWNER, 2026-08-05, issue #225: players of this genre
	 * check the arithmetic, and a displayed number that is not the number the
	 * maths used sends them looking for a bug. Each of the eight attributes has
	 * one affix and it is a percentage increase, so 33 Spirit with a top-tier
	 * +12% affix reaches 36.96 and has to become 37 everywhere -- not 37 on the
	 * character screen and 36.96 in the calculation.
	 *
	 * HALF ROUNDS UP, which is what a player reads "nearest whole number" to
	 * mean. Halves are reachable: 10% of 5 is 5.5.
	 *
	 * It is a named function rather than a line inside PreAttributeChange so
	 * that a test can check the rule without building an ability system
	 * component, and so the call site says which rule it is applying.
	 * `attribute_points` in `sim/cataclysm_sim/character.py` is the matching
	 * function in the simulation and carries the same reasoning.
	 */
	static float RoundedPoints(float Raw);

protected:
	UFUNCTION() void OnRep_Agility(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Ferocity(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Constitution(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Vitality(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Mind(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Spirit(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Efficacy(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Luck(const FGameplayAttributeData& OldValue);
};
