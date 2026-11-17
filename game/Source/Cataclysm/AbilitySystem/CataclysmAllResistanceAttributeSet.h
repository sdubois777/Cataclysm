// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmAttributeAccessors.h"
#include "CataclysmAllResistanceAttributeSet.generated.h"

/**
 * One resistance, applied to all incoming damage whatever its type.
 *
 * WHO HOLDS THIS: enemies, and only enemies. A player holds
 * `UCataclysmResistanceAttributeSet` instead, which is eight figures, one per
 * damage type. **No character holds both**, and that is the whole design:
 *
 *     an ENEMY resists everything equally, so it has one figure and not eight
 *     a PLAYER is attacked by eight Cataclysms, so it has eight and not one
 *
 * WHY A SEPARATE ATTRIBUTE SET RATHER THAN A NINTH ATTRIBUTE ON THE OTHER ONE.
 * An attribute set is all-or-nothing: a character that registers
 * `UCataclysmResistanceAttributeSet` gets all of its attributes and there is no
 * way to hold some of them. So a ninth attribute there would have given every
 * player a generic resistance they can never have, and every enemy the eight
 * typed resistances it must not have. The project owner ruled on 2026-08-12 that
 * an enemy should not carry the eight at all. Two sets is the only shape that
 * expresses that. Issue #486.
 *
 * WHY AN ENEMY CANNOT SIMPLY WRITE ONE FIGURE INTO EIGHT TYPED SLOTS, which is
 * what it used to do. `ResistanceFor` in CataclysmDamageCalculation.cpp selects a
 * slot from the incoming hit's damage type, and a player's hit deliberately
 * carries no type -- the enemy resists everything equally, so a type would be
 * choosing between eight copies of one number. With no type there was no slot to
 * select, all eight were skipped, and the enemy resisted nothing at all.
 *
 * THE 70% CAP IS NOT ENFORCED HERE, for the same reason it is not enforced on
 * the eight: the cap governs how much resistance REDUCES DAMAGE, not how much a
 * character may have, and it lives in
 * `UCataclysmDamageCalculation::EffectiveResistance`.
 */
UCLASS()
class CATACLYSM_API UCataclysmAllResistanceAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UCataclysmAllResistanceAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** Percent of all incoming damage resisted, whatever its type. */
	UPROPERTY(BlueprintReadOnly, Category = "Resistances", ReplicatedUsing = OnRep_AllResistance)
	FGameplayAttributeData AllResistance;
	ATTRIBUTE_ACCESSORS(UCataclysmAllResistanceAttributeSet, AllResistance)

	static TArray<FGameplayAttribute> GetAllAttributes();

protected:
	UFUNCTION() void OnRep_AllResistance(const FGameplayAttributeData& OldValue);
};
