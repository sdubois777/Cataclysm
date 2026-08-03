// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CataclysmStatPipeline.generated.h"

/**
 * Which of the three buckets a modifier enters.
 *
 * Which bucket a modifier lands in is what decides whether it has diminishing
 * returns, and that is the whole point of having three.
 */
UENUM(BlueprintType)
enum class ECataclysmStatBucket : uint8
{
	/** Added to the base before anything multiplies. In the stat's own units. */
	Flat		UMETA(DisplayName = "Flat"),

	/** Summed with every other increase reaching this stat, then applied once. */
	Increased	UMETA(DisplayName = "Increased"),

	/** Multiplies on its own, outside that sum. */
	More		UMETA(DisplayName = "More"),
};

/**
 * Where a modifier came from. This is not decoration: it decides whether the
 * modifier is allowed into the More bucket.
 */
UENUM(BlueprintType)
enum class ECataclysmModifierSource : uint8
{
	/** A rolled affix on a piece of gear. Flat or Increased, never More. */
	GearAffix		UMETA(DisplayName = "Gear Affix"),

	/** The inherent stat on an item base. Flat or Increased, never More. */
	GearImplicit	UMETA(DisplayName = "Gear Implicit"),

	/** An attribute point. Only ever adds to the sum of increases. */
	Attribute		UMETA(DisplayName = "Attribute"),

	/** A socketed gem. May grant More. */
	Gem				UMETA(DisplayName = "Gem"),

	/** A passive tree keystone. May grant More. */
	PassiveKeystone	UMETA(DisplayName = "Passive Keystone"),

	/** An enchantment on a Legendary or better item. May grant More. */
	Enchantment		UMETA(DisplayName = "Enchantment"),
};

/**
 * One modifier the character carries, and what it applies to.
 *
 * The character holds its own increases; they are not properties of any one
 * skill. An item granting increased area of effect applies to every skill
 * tagged for area of effect, and to no others. `RequiredTags` empty means the
 * modifier applies to everything.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmStatModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Stats")
	ECataclysmStatBucket Bucket = ECataclysmStatBucket::Flat;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Stats")
	ECataclysmModifierSource Source = ECataclysmModifierSource::GearAffix;

	/**
	 * PERCENTAGE POINTS for Increased and More. Points of the stat for Flat.
	 *
	 * So an Increased of 125 is +125% and a More of 60 is a 1.60x multiplier.
	 * The Python model in sim/cataclysm_sim/character.py stores these as
	 * fractions instead -- 1.25 and 0.60 -- because that is what reads naturally
	 * in the tuning rig. Percentage points are used here because every other
	 * percentage in this module is already in points: evasion's soft cap is
	 * 60.0, the resistance cap is 70.0, and the damage calculation divides by
	 * 100 throughout. Mixing the two conventions inside one module would be
	 * worse than differing from the model, so the conversion happens at the
	 * boundary and a test pins a value against the model to prove it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Stats")
	float Value = 0.0f;

	/**
	 * Every one of these must be matched by the skill in hand, or the modifier
	 * does not apply. Empty applies to everything, and so does Scope.Global.
	 *
	 * Matching is hierarchical, the way the design's tag names are built: a
	 * modifier requiring Type.AOE is satisfied by a skill tagged
	 * Type.AOE.PointBlank. FGameplayTagContainer::HasTag already does this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Stats")
	FGameplayTagContainer RequiredTags;
};

/**
 * What the pipeline decided, step by step, so it can be inspected and shown.
 *
 * The counts at the end exist so a test can prove a rule fired without reading
 * the log, and so a character sheet can show a player that something on their
 * gear is being ignored.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmStatBreakdown
{
	GENERATED_BODY()

	/** Before anything applies: class, weapon or skill, per the design. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float Base = 0.0f;

	/** Everything in the Flat bucket that reached this stat, added up. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float Flat = 0.0f;

	/** Percentage points. 825 means +825%, applied as one multiplication. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SumOfIncreases = 0.0f;

	/** Every More multiplier that reached this stat, multiplied together. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float MoreMultiplier = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float Final = 0.0f;

	/** How many More multipliers applied. Two of them is not one big one. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	int32 MoreSourceCount = 0;

	/** More multipliers refused because their source may not grant one. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	int32 RejectedMoreCount = 0;

	/** Less multipliers that were clamped away from -100%. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	int32 ClampedLessCount = 0;
};

/**
 * The three-bucket stat pipeline, ported from `sim/cataclysm_sim/character.py`.
 *
 *     Final = (base + flat) x (1 + sum of increases) x more1 x more2 x ...
 *
 * WHY THIS EXISTS WHEN THE ABILITY SYSTEM ALREADY AGGREGATES. Unreal's own
 * aggregator computes
 *
 *     ((Base + AddBase) * MultiplyAdditive / DivideAdditive * MultiplyCompound)
 *         + AddFinal
 *
 * and MultiplyAdditive sums with a bias of 1.0 while MultiplyCompound
 * multiplies each modifier separately. So AddBase is Flat, MultiplyAdditive is
 * Increased, and MultiplyCompound is More: the engine's arithmetic and the
 * design's are the same arithmetic. A test builds an FAggregator and asserts
 * they agree, so gear applied as ordinary Gameplay Effects cannot drift from
 * what is computed here.
 *
 * What the engine does NOT do, and what this class is for:
 *
 *   TAG SCOPING BY THE SKILL IN HAND. An aggregator mod can be filtered by the
 *   source and target actors' tags, but the design scopes an increase by the
 *   tags of the ability being used. A character's area of effect has no single
 *   value -- it is one number for an area skill and another for a single-target
 *   one -- so a plain attribute read cannot express it. Evaluate takes the
 *   skill's tags for exactly this reason.
 *
 *   THE RULE THAT ONLY SOME SOURCES MAY GRANT A MORE MULTIPLIER. Gems, passive
 *   keystones and enchantments may. Ordinary gear affixes may not, which is
 *   what keeps a rare drop readable and gives the designed enchantments a job
 *   affixes cannot do. The engine has no opinion on this.
 *
 *   THE FLOOR UNDER A LESS MULTIPLIER. Nothing in the engine stops a modifier
 *   of -100% or worse, which would zero a stat outright or invert it.
 *
 * WHY IT IS A SEPARATE CLASS of static functions, like
 * UCataclysmDamageCalculation: every step is arithmetic on numbers, so pulling
 * it out means the whole pipeline can be tested by passing values in, without
 * constructing an ability system component and an effect spec for each case.
 */
UCLASS()
class CATACLYSM_API UCataclysmStatPipeline : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * A Less multiplier is clamped to this, in percentage points.
	 *
	 * The model refuses anything at or below -100% outright, because one source
	 * could otherwise zero a stat or turn it negative. Refusing is not available
	 * at runtime, so the value is clamped here and counted in the breakdown. -99
	 * keeps the invariant -- the stat can be made very small and can never reach
	 * zero or invert -- while still honouring what a -150% was reaching for.
	 * Data import should reject the modifier outright instead; see
	 * ValidateModifier.
	 */
	static constexpr float LessMultiplierFloor = -99.0f;

	/** A required tag of this name is satisfied by any skill at all. */
	static FGameplayTag GlobalScopeTag();

	/** Whether a modifier from this source is allowed into the More bucket. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Stats")
	static bool CanGrantMore(ECataclysmModifierSource Source);

	/** Whether the skill in hand carries every tag this modifier requires. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Stats")
	static bool ModifierApplies(const FCataclysmStatModifier& Modifier,
								const FGameplayTagContainer& SkillTags);

	/**
	 * Why a modifier is illegal, or an empty string if it is fine.
	 *
	 * For data import and editor validation, which can refuse a row. Evaluate
	 * cannot refuse anything at runtime, so it ignores or clamps instead and
	 * records that it did.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Stats")
	static FString ValidateModifier(const FCataclysmStatModifier& Modifier);

	/**
	 * Run a stat through all three buckets for the skill in hand.
	 *
	 * `Base` comes from whichever source the design names for that stat: the
	 * class for most, the equipped weapon for attack speed, the skill itself
	 * for critical strike chance.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Stats")
	static FCataclysmStatBreakdown Evaluate(float Base,
											const TArray<FCataclysmStatModifier>& Modifiers,
											const FGameplayTagContainer& SkillTags);

	/**
	 * The same three buckets, for a stat that is a rate rather than a quantity.
	 *
	 *     Final = base / ((1 + sum of increases) x more1 x more2 x ...)
	 *
	 * Cooldown reduction is the only one. An increase makes the interval
	 * shorter, so it divides; a More source has to divide for the same reason,
	 * or a cooldown reduction gem would make the cooldown longer. Because both
	 * buckets divide, no number of them reaches zero, which is why the stat
	 * needs no cap.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Stats")
	static FCataclysmStatBreakdown EvaluateRate(float Base,
												const TArray<FCataclysmStatModifier>& Modifiers,
												const FGameplayTagContainer& SkillTags);

	/** What a player is shown, as a percentage. Never reaches 100. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Stats")
	static float DisplayedRateReduction(const FCataclysmStatBreakdown& Breakdown);

private:
	/** Shared by Evaluate and EvaluateRate; they differ only in the last step. */
	static FCataclysmStatBreakdown Accumulate(float Base,
											  const TArray<FCataclysmStatModifier>& Modifiers,
											  const FGameplayTagContainer& SkillTags);
};
