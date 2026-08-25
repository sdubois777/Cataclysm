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
 * THE POOL AND THE RATES THAT MOVE IT. Fervour does not decay by default, and a
 * class may add a rule that changes that on its own starting node -- the
 * Berserker's generator empties it at 10 per second out of combat, the
 * Saboteur's adds no rule at all. The pool is 100 for every class and 150 for
 * the Ritualist.
 *
 * ONE CLASS'S GENERATOR IS BUILT AND THE OTHER 23 ARE NOT. Issue #954 added the
 * Masochist's, which is the three rates below: health lost to damage and health
 * spent as an ability cost both fill the bar, and healing empties it. The
 * Berserker filling on a critical strike and the Saboteur filling on a trap are
 * different rules, not different values of these, and each needs its own code.
 * `UCataclysmFervour` holds the Masochist's and is where a second one would join.
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

	//~ The three rates that move the pool. Issue #954.
	//
	// ALL THREE ARE FERVOUR PER 1% OF MAXIMUM HEALTH, and all three start at
	// zero for every class. A character that has spent no point on a generator
	// gains no Fervour and loses none, which is what makes a generator node
	// worth a point. The Masochist's starting node grants 1 to each of the
	// three; `game/Data/PassiveEffects.csv` is where that is written down.
	//
	// WHY A RATE PER 1% RATHER THAN PER POINT OF HEALTH. The design states the
	// Masochist's generator as "1 per 1% of maximum health lost to damage", so a
	// character with 500 health and one with 5000 fill the bar at the same speed
	// relative to how much of themselves they have spent. A rate per point of
	// health would make more health mean slower generation.
	//
	// THEY ARE NOT ON THE CHARACTER SHEET, for the reason the maximum critical
	// strike chance is not: no affix grants one, nothing scales one, and none
	// has a baseline of its own. `Cataclysm.Attributes.CharacterSheetIsComplete`
	// keeps that count honest and states the same three exclusions.

	/** Fervour gained per 1% of maximum health lost to damage. */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_FervourFromDamage)
	FGameplayAttributeData FervourFromDamage;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, FervourFromDamage)

	/** Fervour gained per 1% of maximum health spent as an ability cost. */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_FervourFromCost)
	FGameplayAttributeData FervourFromCost;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, FervourFromCost)

	/**
	 * Fervour REMOVED per 1% of maximum health restored.
	 *
	 * A POSITIVE NUMBER THAT TAKES SOMETHING AWAY, which is why it is named for
	 * what it does rather than for its sign. The Masochist's starting node sets
	 * it to 1, so healing empties the bar exactly as fast as damage fills it,
	 * and its Staunch node reduces it by 5% per point.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_FervourLostToHealing)
	FGameplayAttributeData FervourLostToHealing;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, FervourLostToHealing)

	static TArray<FGameplayAttribute> GetAllAttributes();

	/** The three rates above, without the pool. For a caller that wants to ask
	 *  whether this character has any way of moving Fervour at all. */
	static TArray<FGameplayAttribute> GetRateAttributes();

protected:
	UFUNCTION() void OnRep_ClassResource(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MaxClassResource(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_FervourFromDamage(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_FervourFromCost(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_FervourLostToHealing(const FGameplayAttributeData& OldValue);
};
