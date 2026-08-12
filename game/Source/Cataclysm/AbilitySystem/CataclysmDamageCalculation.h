// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CataclysmDamageCalculation.generated.h"

class UAbilitySystemComponent;

/**
 * What an incoming hit is, as far as the defender's mitigation cares.
 *
 * Kept separate from the gameplay effect that carries the damage so the
 * calculation can be tested directly, without building an effect spec.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmIncomingHit
{
	GENERATED_BODY()

	/** Damage before any mitigation. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	float Damage = 0.0f;

	/** Which of the eight resistances applies. Empty means none of them do. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	FName DamageType;

	/** Percentage points subtracted from the defender's resistance. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	float ResistancePenetration = 0.0f;

	/** Percentage of the defender's armor ignored, from gear and sub-type. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	float ArmorPenetration = 0.0f;

	/** Area damage cannot be evaded. It can still be blocked. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	bool bIsArea = false;

	/**
	 * Bleed, poison, burn and the rest. Routed differently: an energy shield
	 * does not absorb it, though it does still restart the shield's recharge.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	bool bIsDamageOverTime = false;

	/** 10% more damage to what reaches health. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	bool bIsSlashing = false;

	/** Strips 10% more energy shield per hit. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	bool bIsMagic = false;
};

/** What the calculation decided, step by step, so it can be inspected. */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmDamageResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Damage")
	bool bEvaded = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Damage")
	bool bBlocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Damage")
	float AbsorbedByMana = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Damage")
	float AbsorbedByShield = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Damage")
	float DealtToHealth = 0.0f;
};

/**
 * The damage calculation, ported from `sim/cataclysm_sim/damage.py`.
 *
 * THE ORDER, from the Damage Calculation section of the design document:
 * evasion, block, armor, resistance, flat damage reduction, mana, energy
 * shield, health.
 *
 * WHY THIS IS A SEPARATE CLASS rather than sitting inside the attribute set:
 * every step of it is arithmetic on numbers, and pulling it out means the whole
 * order can be tested by passing values in, without constructing an ability
 * system component and an effect spec for each case.
 */
UCLASS()
class CATACLYSM_API UCataclysmDamageCalculation : public UObject
{
	GENERATED_BODY()

public:
	/** K in `armor / (armor + K)` is this times the difficulty tier. */
	static constexpr float ArmorConstantPerTier = 800.0f;

	/** Armor alone never removes more than this share of a hit. */
	static constexpr float ArmorReductionCap = 75.0f;

	/** How much resistance is worth against damage, however high it is. */
	static constexpr float ResistanceCap = 70.0f;

	/** Negative resistance means taking extra damage. This bounds how bad. */
	static constexpr float ResistanceFloor = -100.0f;

	/** A successful block removes this share of the hit. */
	static constexpr float BlockDamageReduction = 50.0f;

	/** Slashing against health, and magic against energy shield. */
	static constexpr float SubtypeBonus = 10.0f;

	/** Seconds after taking damage before an energy shield starts refilling. */
	static constexpr float EnergyShieldRechargeDelay = 3.0f;

	/** What every damage type's gameplay tag begins with. `Element.` */
	static const TCHAR* ElementTagPrefix;

	/**
	 * The tag that says a hit swept a volume rather than touching one target.
	 *
	 * `Type.AOE`, the parent of the three the vocabulary declares -- Aura,
	 * Persistent and PointBlank. A parent matches any of its children, so an
	 * effect carrying a specific one is area damage without having to list them.
	 */
	static const TCHAR* AreaDamageTagName;

	/**
	 * The tag that says a hit is damage over time.
	 *
	 * `Keyword.DoT`, the parent of Bleed, Burn, Disease, Generic, Necrosis,
	 * Poison and VoidSplinter.
	 */
	static const TCHAR* DamageOverTimeTagName;

	/** `Type.AOE`, or an invalid tag if the vocabulary has lost it. */
	static FGameplayTag AreaDamageTag();

	/** `Keyword.DoT`, or an invalid tag if the vocabulary has lost it. */
	static FGameplayTag DamageOverTimeTag();

	/**
	 * A damage type as the gameplay tag that carries it on an effect.
	 *
	 * "Demonic" becomes `Element.Demonic`. Returns an invalid tag for a name the
	 * tag vocabulary has no `Element.*` entry for, and for `NAME_None`.
	 *
	 * WHY A TAG RATHER THAN THE NAME ITSELF: a gameplay effect can carry tags and
	 * cannot carry an FName, and the eight `Element.*` tags already exist in
	 * `game/Config/Tags/CataclysmTags.ini` because stat modifiers scope to them.
	 * See `DamageTypeFromTags` for the way back.
	 */
	static FGameplayTag ElementTagFor(FName DamageType);

	/**
	 * The damage type an effect's tags say it is, or `NAME_None` for an untyped
	 * hit.
	 *
	 * THE INVERSE OF `ElementTagFor`, AND IT LIVES BESIDE IT ON PURPOSE. The two
	 * are the only encoding and decoding of a damage type in the project, and if
	 * they ever disagreed the type would vanish silently and every resistance
	 * would go back to doing nothing, which is exactly the state issue #486
	 * describes.
	 */
	static FName DamageTypeFromTags(const FGameplayTagContainer& Tags);

	/** Armor as a percentage of damage removed. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Damage")
	static float ArmorReduction(float Armor, int32 Tier);

	/**
	 * Resistance after penetration, then capped.
	 *
	 * Penetration is subtracted BEFORE the cap. That order is the only thing
	 * that makes over-capping worth anything: against 30 penetration a defender
	 * at 100 resistance still sits at the 70 cap, where one at exactly 70 drops
	 * to 40. Capping first would make every point above 70 worthless.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Damage")
	static float EffectiveResistance(float Resistance, float Penetration);

	/**
	 * Run one hit through the whole order against a character's attribute sets.
	 *
	 * `EvasionRoll` and `BlockRoll` are 0-100 values supplied by the caller so
	 * that tests can pin them. Pass a negative number to roll here.
	 */
	static FCataclysmDamageResult Resolve(const FCataclysmIncomingHit& Hit,
										  const UAbilitySystemComponent* Defender,
										  int32 Tier,
										  float EvasionRoll = -1.0f,
										  float BlockRoll = -1.0f);
};
