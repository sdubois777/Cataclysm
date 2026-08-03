// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmAttributeAccessors.h"
#include "CataclysmCombatAttributeSet.generated.h"

/**
 * The derived combat and utility stats: defence, offence, and utility.
 *
 * WHAT IS AND IS NOT CLAMPED HERE. The design has two kinds of cap, and only one
 * of them is a clamp:
 *
 *   HARD -- crit chance at 100%. Above that it means nothing, so it is clamped.
 *
 *   SOFT -- evasion at 60%. Gear enchantments may exceed it, so it is NOT
 *           clamped. Clamping would delete the over-cap the design allows.
 *
 * Block chance has no cap at all, because a block is not a full avoid: it
 * removes 50% of a hit's damage. A character at 100% block chance therefore has
 * 50% damage reduction against what block applies to, which is strong but is not
 * immunity. Cooldown reduction likewise needs no cap, because it is calculated
 * by division and can never reach zero.
 *
 * WHERE THESE VALUES COME FROM. Most have a class base. Attack speed's base
 * comes from the equipped weapon and critical strike chance's from the skill
 * being used; the character contributes only increases to those two. Area of
 * effect and damage over time frequency are percentages of whatever the skill
 * does, so they baseline at 100 rather than zero.
 */
UCLASS()
class CATACLYSM_API UCataclysmCombatAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UCataclysmCombatAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** Hard cap. Above 100% a critical strike chance means nothing. */
	static constexpr float CritChanceCap = 100.0f;

	/** Soft cap. Recorded, and deliberately not enforced as a clamp. */
	static constexpr float EvasionSoftCap = 60.0f;

	/** A successful block removes this share of the hit's damage. */
	static constexpr float BlockDamageReduction = 50.0f;

	// -- Defence ----------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_Armor)
	FGameplayAttributeData Armor;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, Armor)

	/** Chance to avoid a direct attack entirely. Does not apply to area damage. */
	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_Evasion)
	FGameplayAttributeData Evasion;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, Evasion)

	/** Chance a hit is blocked. Applies to area damage as well as direct hits. */
	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_BlockChance)
	FGameplayAttributeData BlockChance;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, BlockChance)

	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_DamageReduction)
	FGameplayAttributeData DamageReduction;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DamageReduction)

	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_Retaliation)
	FGameplayAttributeData Retaliation;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, Retaliation)

	UPROPERTY(BlueprintReadOnly, Category = "Defence", ReplicatedUsing = OnRep_CrowdControlResistance)
	FGameplayAttributeData CrowdControlResistance;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, CrowdControlResistance)

	// -- Offence ----------------------------------------------------------

	/** Base comes from the skill being used, not from the character. */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_CritChance)
	FGameplayAttributeData CritChance;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, CritChance)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_CritMultiplier)
	FGameplayAttributeData CritMultiplier;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, CritMultiplier)

	/** Base comes from the equipped weapon, not from the character. */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_AttackSpeed)
	FGameplayAttributeData AttackSpeed;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, AttackSpeed)

	/** A percentage of whatever the skill does, so 100 means unchanged. */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_AreaOfEffect)
	FGameplayAttributeData AreaOfEffect;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, AreaOfEffect)

	/** A percentage of whatever the skill does, so 100 means unchanged. */
	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_DotFrequency)
	FGameplayAttributeData DotFrequency;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, DotFrequency)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_Penetration)
	FGameplayAttributeData Penetration;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, Penetration)

	UPROPERTY(BlueprintReadOnly, Category = "Offence", ReplicatedUsing = OnRep_SpellDamage)
	FGameplayAttributeData SpellDamage;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, SpellDamage)

	// -- Utility ----------------------------------------------------------

	/** Metres per second. */
	UPROPERTY(BlueprintReadOnly, Category = "Utility", ReplicatedUsing = OnRep_MovementSpeed)
	FGameplayAttributeData MovementSpeed;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, MovementSpeed)

	/**
	 * The accumulated sum of cooldown increases, NOT the displayed percentage.
	 *
	 * A skill's final cooldown is its base divided by (1 + this). The displayed
	 * reduction is this divided by (1 + this). Storing the sum rather than the
	 * displayed figure is what keeps cooldowns from ever reaching zero.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Utility", ReplicatedUsing = OnRep_CooldownReduction)
	FGameplayAttributeData CooldownReduction;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, CooldownReduction)

	UPROPERTY(BlueprintReadOnly, Category = "Utility", ReplicatedUsing = OnRep_MagicFind)
	FGameplayAttributeData MagicFind;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, MagicFind)

	UPROPERTY(BlueprintReadOnly, Category = "Utility", ReplicatedUsing = OnRep_LootQuantity)
	FGameplayAttributeData LootQuantity;
	ATTRIBUTE_ACCESSORS(UCataclysmCombatAttributeSet, LootQuantity)

	static TArray<FGameplayAttribute> GetAllAttributes();

	/** A skill's cooldown after this character's accumulated increases. */
	static float FinalCooldown(float BaseCooldown, float CooldownIncreases);

	/** What the interface shows the player, as a percentage. Never reaches 100. */
	static float DisplayedCooldownReduction(float CooldownIncreases);

protected:
	UFUNCTION() void OnRep_Armor(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Evasion(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_BlockChance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageReduction(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Retaliation(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CrowdControlResistance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CritChance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CritMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_AttackSpeed(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_AreaOfEffect(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DotFrequency(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_Penetration(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_SpellDamage(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MovementSpeed(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CooldownReduction(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MagicFind(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_LootQuantity(const FGameplayAttributeData& OldValue);
};
