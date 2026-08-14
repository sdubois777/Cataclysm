// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmAttributeAccessors.h"
#include "CataclysmVitalAttributeSet.generated.h"

/**
 * The three vitals, their maxima, their recovery rates, and the meta attribute
 * every source of damage routes through.
 *
 * Health, mana and energy shield all come from the same pipeline: a class base
 * plus per-level scaling plus flat values from gear, all multiplied once by the
 * sum of increases. Attributes contribute only to that sum. See the Stat
 * Calculation section of the design document.
 *
 * A maximum of zero is legitimate. Two of the three Demonic classes have no
 * energy shield at all, so MaxEnergyShield floors at zero rather than at one.
 * MaxHealth is the exception: a maximum of zero would collapse the health clamp
 * to a single point and make every percentage-of-maximum calculation divide by
 * zero, so it floors at one.
 */
UCLASS()
class CATACLYSM_API UCataclysmVitalAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UCataclysmVitalAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

protected:
	/**
	 * Tell the owning character its health has reached zero, once.
	 *
	 * HERE RATHER THAN IN THE CHARACTER, because this is the only place in the
	 * project that sees a hit land and what it left behind. Nothing watched
	 * health before issue #517, so a creature at zero health kept chasing and
	 * kept swinging.
	 */
	void NotifyIfHealthReachedZero();

	/**
	 * Play the hit effect where the blow landed.
	 *
	 * HERE FOR THE SAME REASON NotifyIfHealthReachedZero IS: this is the one
	 * place in the project that sees a hit land, knows its damage type and knows
	 * who took it. Every blow from either side arrives through the Damage meta
	 * attribute, so one call covers player and enemy attacks alike.
	 *
	 * Draws nothing for a blow that was evaded or wholly mitigated, which is
	 * what makes the effect mean "that connected".
	 */
	void PlayImpactEffect(const FGameplayEffectModCallbackData& Data,
						  FName DamageType,
						  const struct FCataclysmDamageResult& Outcome);

public:

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_Mana)
	FGameplayAttributeData Mana;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, Mana)

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_MaxMana)
	FGameplayAttributeData MaxMana;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, MaxMana)

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_EnergyShield)
	FGameplayAttributeData EnergyShield;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, EnergyShield)

	UPROPERTY(BlueprintReadOnly, Category = "Vitals", ReplicatedUsing = OnRep_MaxEnergyShield)
	FGameplayAttributeData MaxEnergyShield;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, MaxEnergyShield)

	/** Points of health restored per second, before increases. */
	UPROPERTY(BlueprintReadOnly, Category = "Recovery", ReplicatedUsing = OnRep_HealthRegen)
	FGameplayAttributeData HealthRegen;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, HealthRegen)

	UPROPERTY(BlueprintReadOnly, Category = "Recovery", ReplicatedUsing = OnRep_ManaRegen)
	FGameplayAttributeData ManaRegen;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, ManaRegen)

	UPROPERTY(BlueprintReadOnly, Category = "Recovery", ReplicatedUsing = OnRep_EnergyShieldRegen)
	FGameplayAttributeData EnergyShieldRegen;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, EnergyShieldRegen)

	/**
	 * Percentage of damage dealt returned as health.
	 *
	 * The damage counted is what the target really took, after its mitigation
	 * and capped at the health it had left, and the amount is paid out over
	 * three seconds rather than at once. See the Leech section of
	 * docs/Cataclysm_GDD_v2.md. Nothing reads these three yet.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Recovery", ReplicatedUsing = OnRep_LifeLeech)
	FGameplayAttributeData LifeLeech;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, LifeLeech)

	/** Percentage of damage dealt returned as mana. */
	UPROPERTY(BlueprintReadOnly, Category = "Recovery", ReplicatedUsing = OnRep_ManaLeech)
	FGameplayAttributeData ManaLeech;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, ManaLeech)

	/** Percentage of damage dealt returned as energy shield. */
	UPROPERTY(BlueprintReadOnly, Category = "Recovery", ReplicatedUsing = OnRep_EnergyShieldLeech)
	FGameplayAttributeData EnergyShieldLeech;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, EnergyShieldLeech)

	/**
	 * Meta attribute. Not replicated, and zeroed after every execution.
	 *
	 * Exists so that mitigation is resolved in exactly one place instead of
	 * being scattered across every effect that deals damage.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Vitals|Meta")
	FGameplayAttributeData Damage;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, Damage)

	static TArray<FGameplayAttribute> GetAllAttributes();

protected:
	UFUNCTION() void OnRep_Health(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Mana(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MaxMana(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_EnergyShield(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MaxEnergyShield(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_HealthRegen(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_ManaRegen(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_EnergyShieldRegen(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_LifeLeech(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_ManaLeech(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_EnergyShieldLeech(const FGameplayAttributeData& OldValue);
};
