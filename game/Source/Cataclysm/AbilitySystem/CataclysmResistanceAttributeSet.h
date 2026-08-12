// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmAttributeAccessors.h"
#include "CataclysmResistanceAttributeSet.generated.h"

/**
 * One resistance per damage type, as a percentage.
 *
 * THE 70% CAP IS SOFT AND IS DELIBERATELY NOT ENFORCED HERE. The design states
 * that resistances cap at 70% and that over-capping is possible via certain
 * affixes. Those two statements are only compatible if the cap governs how much
 * resistance REDUCES DAMAGE, not how much resistance a character may have.
 *
 * Clamping the attribute at 70 would silently delete over-capping, and
 * over-capping is the point: enemy penetration reduces effective resistance, so
 * the headroom above the cap is what keeps a character at the cap in practice.
 *
 * The cap therefore belongs in the damage calculation, and it is now there:
 * `UCataclysmDamageCalculation::EffectiveResistance` in
 * CataclysmDamageCalculation.h holds the 70% figure and applies it.
 *
 * THIS CLASS USED TO HOLD A SECOND COPY of that figure and a helper that
 * applied it, and both are gone. The helper took a raw resistance and clamped
 * it at 70 with no penetration argument, which is the wrong answer: penetration
 * is subtracted BEFORE the cap, and that is the whole reason the cap is called
 * soft and the whole reason over-capping is worth anything. Nothing outside a
 * test ever called it. Issue #232.
 */
UCLASS()
class CATACLYSM_API UCataclysmResistanceAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UCataclysmResistanceAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/**
	 * Resistance that applies to a hit of ANY damage type, including an untyped
	 * one, and is added to whichever of the eight below the hit's type selects.
	 *
	 * WHAT IT IS FOR: ENEMIES. The project owner ruled on 2026-08-12 that "enemies
	 * will have a generic all res" and that "the only damage that should actually
	 * be typed is enemy damage so the player's resistances can take effect". So
	 * the two sides of a fight use this set differently and deliberately:
	 *
	 *     an ENEMY carries one figure, here, and zero in all eight below
	 *     a PLAYER carries eight figures below, and zero here
	 *
	 * WHY A NINTH ATTRIBUTE RATHER THAN WRITING ONE FIGURE INTO EIGHT SLOTS,
	 * which is what an enemy used to do. The two are the same number as long as
	 * every incoming hit is typed. They stop being the same the moment one is not,
	 * and player damage is deliberately untyped: `ResistanceFor` in
	 * CataclysmDamageCalculation.cpp cannot pick a slot from an empty type, so
	 * eight identical figures would all be skipped and the enemy would resist
	 * nothing. One generic figure is met by every hit, whatever it is.
	 *
	 * It is also one number to write rather than eight, and one to read when the
	 * archetype table starts supplying it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Resistances", ReplicatedUsing = OnRep_AllResistance)
	FGameplayAttributeData AllResistance;
	ATTRIBUTE_ACCESSORS(UCataclysmResistanceAttributeSet, AllResistance)

	UPROPERTY(BlueprintReadOnly, Category = "Resistances", ReplicatedUsing = OnRep_WarResistance)
	FGameplayAttributeData WarResistance;
	ATTRIBUTE_ACCESSORS(UCataclysmResistanceAttributeSet, WarResistance)

	UPROPERTY(BlueprintReadOnly, Category = "Resistances", ReplicatedUsing = OnRep_DemonicResistance)
	FGameplayAttributeData DemonicResistance;
	ATTRIBUTE_ACCESSORS(UCataclysmResistanceAttributeSet, DemonicResistance)

	UPROPERTY(BlueprintReadOnly, Category = "Resistances", ReplicatedUsing = OnRep_DeathResistance)
	FGameplayAttributeData DeathResistance;
	ATTRIBUTE_ACCESSORS(UCataclysmResistanceAttributeSet, DeathResistance)

	UPROPERTY(BlueprintReadOnly, Category = "Resistances", ReplicatedUsing = OnRep_PestilenceResistance)
	FGameplayAttributeData PestilenceResistance;
	ATTRIBUTE_ACCESSORS(UCataclysmResistanceAttributeSet, PestilenceResistance)

	UPROPERTY(BlueprintReadOnly, Category = "Resistances", ReplicatedUsing = OnRep_FamineResistance)
	FGameplayAttributeData FamineResistance;
	ATTRIBUTE_ACCESSORS(UCataclysmResistanceAttributeSet, FamineResistance)

	UPROPERTY(BlueprintReadOnly, Category = "Resistances", ReplicatedUsing = OnRep_CelestialResistance)
	FGameplayAttributeData CelestialResistance;
	ATTRIBUTE_ACCESSORS(UCataclysmResistanceAttributeSet, CelestialResistance)

	UPROPERTY(BlueprintReadOnly, Category = "Resistances", ReplicatedUsing = OnRep_ChaosResistance)
	FGameplayAttributeData ChaosResistance;
	ATTRIBUTE_ACCESSORS(UCataclysmResistanceAttributeSet, ChaosResistance)

	UPROPERTY(BlueprintReadOnly, Category = "Resistances", ReplicatedUsing = OnRep_VoidResistance)
	FGameplayAttributeData VoidResistance;
	ATTRIBUTE_ACCESSORS(UCataclysmResistanceAttributeSet, VoidResistance)

	static TArray<FGameplayAttribute> GetAllAttributes();

protected:
	UFUNCTION() void OnRep_AllResistance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_WarResistance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DemonicResistance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DeathResistance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_PestilenceResistance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_FamineResistance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CelestialResistance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_ChaosResistance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_VoidResistance(const FGameplayAttributeData& OldValue);
};
