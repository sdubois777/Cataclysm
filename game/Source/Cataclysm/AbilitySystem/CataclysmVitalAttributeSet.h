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

	/**
	 * Notice a health change that no gameplay effect caused.
	 *
	 * WHY THIS EXISTS. Issues #971 and #1072. The two notifications below were
	 * reached only from `PostGameplayEffectExecute`, which a resolved gameplay
	 * effect reaches and a direct write to the attribute does not. Three routes
	 * lower a character's health and only the first is a gameplay effect:
	 *
	 *   a blow                a gameplay effect on the Damage meta attribute
	 *   a health cost         `UCataclysmSkillTemplate::PayHealthCost`
	 *   a debt draining out   `UCataclysmHealthDebt::DrainIfDue`
	 *
	 * The last two call `UAbilitySystemComponent::ApplyModToAttribute`, so a
	 * Masochist paying health for every skill could empty its health without
	 * dying and cross every health threshold without any of them firing. The
	 * project owner played that character on 2026-08-31 and reported standing
	 * at zero health, alive, with every skill refused.
	 *
	 * WHY THIS HOOK. The engine calls it from
	 * `FActiveGameplayEffectsContainer::SetAttributeBaseValue`, which is where
	 * `ApplyModToAttribute` and `SetNumericAttributeBase` both end up, so it
	 * covers every route rather than the ones somebody remembered to wire.
	 *
	 * CONST BECAUSE THE ENGINE DECLARES IT SO, and it costs nothing: both
	 * notifications below read this set and change only things outside it, so
	 * both are const too and no cast is needed.
	 */
	virtual void PostAttributeBaseChange(const FGameplayAttribute& Attribute,
										 float OldValue, float NewValue) const override;

protected:
	/**
	 * Tell the owning character its health has reached zero, once.
	 *
	 * HERE RATHER THAN IN THE CHARACTER, because this is the only place in the
	 * project that sees a hit land and what it left behind. Nothing watched
	 * health before issue #517, so a creature at zero health kept chasing and
	 * kept swinging.
	 */
	void NotifyIfHealthReachedZero() const;

	/** Tell the character its health moved, so a health-triggered phase can
	 *  begin. Called from `PostGameplayEffectExecute` for a blow and from
	 *  `PostAttributeBaseChange` above for every other route. */
	void NotifyHealthChanged() const;

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
						  const struct FCataclysmIncomingHit& Hit,
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
	 * docs/Cataclysm_GDD_v2.md.
	 *
	 * THIS COMMENT USED TO SAY "NOTHING READS THESE THREE YET" AND THAT HAD BEEN
	 * FALSE SINCE ISSUE #895, which built leech. `UCataclysmLeech::NoteHit`
	 * reads all three where a hit lands and `UCataclysmLeech::PayOutStep` pays
	 * them out. It is the same fault issue #900 recorded elsewhere for the blunt
	 * weapon stun roll, and it does the same harm: a comment saying a capability
	 * is missing is read as a reason not to look for it, and the handoff written
	 * on 2026-08-27 told the next session to check whether leech existed at all
	 * before building on it. Issue #1048.
	 *
	 * LIFE LEECH IS READ IN A SECOND PLACE SINCE ISSUE #1048.
	 * `UCataclysmLeech::NoteRetaliation` reads it where retaliation is paid, for
	 * a character holding the Masochist's Feeding Wound. Retaliation is not a
	 * hit, so it never reaches `NoteHit`, and that capstone option is what buys
	 * the exception. Mana leech and energy shield leech stay on attacks alone.
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
	 * How many percentage points to take off the ceiling healing may reach,
	 * as a share of maximum health. Issue #988.
	 *
	 * The Masochist's Point of No Return keystone is its only source: "You
	 * cannot be healed above 50% of your maximum health, but you deal 25% more
	 * damage." A reduction of 50 puts the ceiling at half of maximum health.
	 *
	 *     ceiling = maximum * (100 - clamp(this, 0, 100)) / 100
	 *
	 * A REDUCTION RATHER THAN THE CEILING ITSELF, and the difference is not
	 * cosmetic. A stat holding the ceiling would need 0 to mean "no cap",
	 * which reads as "cannot be healed at all", and two sources of it would
	 * SUM in the flat bucket to 100 and remove the cap entirely. Written as a
	 * reduction the default of 0 means no cap with no sentinel, and two
	 * sources stack in the restrictive direction, which is the direction a
	 * player would expect two such nodes to stack in.
	 *
	 * IT REACHES ONLY `UCataclysmRegeneration::TopUp`, which is the one place
	 * health regeneration and life leech both restore health.
	 * `ACataclysmPlayerCharacter::Respawn` writes health back directly rather
	 * than healing, so a respawn is not capped -- a respawn is a new life
	 * rather than healing, and issue #956 is the open question about what else
	 * that direct write should and should not do.
	 *
	 * HEALTH ONLY. The node says health, and mana and the energy shield go
	 * through the same function.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Recovery", ReplicatedUsing = OnRep_HealingCeilingReduction)
	FGameplayAttributeData HealingCeilingReduction;
	ATTRIBUTE_ACCESSORS(UCataclysmVitalAttributeSet, HealingCeilingReduction)

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
	UFUNCTION() void OnRep_HealingCeilingReduction(const FGameplayAttributeData& OldValue);
};
