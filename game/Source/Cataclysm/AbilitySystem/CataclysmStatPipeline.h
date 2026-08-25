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

	/**
	 * A buff a skill put on its own caster, lasting only as long as the skill.
	 *
	 * Burning Wrath's "4% increased fire damage for every enemy currently
	 * burning within 15 meters" is one. Unlike every source above it, this one
	 * is added and removed at runtime rather than being a property of what the
	 * character is wearing, which is why UCataclysmAbilitySystemComponent holds
	 * these in a list a skill can add to and take from.
	 *
	 * MAY GRANT MORE. The rule that ordinary gear may not is about a ROLLED
	 * modifier staying readable on a drop. A skill buff is authored, in the same
	 * way a gem, a keystone and an enchantment are authored, so it sits with
	 * those three rather than with the affix pool.
	 */
	SkillBuff		UMETA(DisplayName = "Skill Buff"),
};

/**
 * A state of the character a modifier can be made to depend on. Issue #959.
 *
 * NOT THE SAME QUESTION AS `RequiredTags`, which asks about the SKILL in hand:
 * "increased area of effect, for traps". This asks about the CHARACTER: "while
 * at or below 20% health". A modifier can carry both and both must hold.
 *
 * A CONDITION IS NOT A SECOND MULTIPLIER. `docs/DECISIONS.md` states the rule
 * outright -- "a conditional increase joins the increases bracket rather than
 * becoming a third multiplier. That is what Diablo 4 and Last Epoch both do" --
 * so a condition decides only whether the modifier is in the sum at all.
 */
UENUM(BlueprintType)
enum class ECataclysmStatCondition : uint8
{
	/** No condition. Every modifier in the game before issue #959. */
	Always					UMETA(DisplayName = "Always"),

	/**
	 * The character's health is at or below `ConditionValue` percent of its
	 * maximum.
	 *
	 * AT OR BELOW, NOT BELOW, because every node that states one is written "at
	 * or below": a character sitting exactly on 20% health gets the bonus.
	 */
	HealthAtOrBelowPercent	UMETA(DisplayName = "Health At Or Below Percent"),

	/**
	 * The character paid a health cost within the last `ConditionValue` seconds.
	 *
	 * A WINDOW THAT OPENS ON AN EVENT AND SHUTS BY ITSELF, which is the second
	 * shape the design uses and the first that depends on WHEN something
	 * happened rather than on what is true now. Blood Rush is the node: "+2%
	 * increased damage per point for 2 seconds after you pay a health cost".
	 * Issue #962.
	 *
	 * NAMED FOR ITS EVENT RATHER THAN BEING A GENERAL TIMER, and deliberately.
	 * The design's other window -- "for 5 seconds after you take damage of a
	 * Cataclysm type other than Demonic" -- opens on a different event that
	 * nothing records yet, and a general timer would have to carry which event
	 * it means anyway. One enumerator per event says plainly what is being
	 * remembered, and the character remembers exactly the events something asks
	 * about.
	 *
	 * A CHARACTER THAT HAS NEVER PAID ONE REFUSES IT, which is not the same as
	 * an expired window and does not need to be: both answer no.
	 */
	WithinSecondsOfHealthCost
		UMETA(DisplayName = "Within Seconds Of A Health Cost"),
};

/**
 * A state of the character a modifier's SIZE can be made to grow with. #968.
 *
 * NOT THE SAME QUESTION AS `ECataclysmStatCondition`, and they are two axes
 * rather than two spellings of one. A condition decides IF a modifier applies;
 * this decides HOW MUCH it is worth. A modifier may carry both, and nothing
 * about one implies anything about the other.
 *
 * WHOLE STEPS, ROUNDED DOWN. "For every 5% of your maximum health that is
 * missing" grants the bonus once per completed 5%, so a character 12% down has
 * two steps rather than two and two fifths. The design states no rounding rule,
 * so it is read off the words -- "for every" is a count of completed blocks --
 * and off the genre: Path of Exile pays a "per 10 Strength" bonus once at 15
 * Strength, not one and a half times. `docs/DECISIONS.md` carries the sources.
 *
 * AN UNKNOWN STATE SCALES TO NOTHING, for the reason an unknown state refuses a
 * condition. The character sheet has no character in hand, and a bonus whose
 * size depends on where health is must not be written onto a gameplay attribute
 * where it would be stale the moment the next blow landed.
 */
UENUM(BlueprintType)
enum class ECataclysmStatScale : uint8
{
	/** The value is what it says. Every modifier in the game before #968. */
	Fixed	UMETA(DisplayName = "Fixed"),

	/**
	 * Multiplied by how many whole `ScaleStep` percent of maximum health are
	 * missing.
	 *
	 * MISSING, NOT REMAINING. A character at full health has no steps and gets
	 * nothing, which is what makes the node a reward for being hurt.
	 */
	PerPercentOfMaximumHealthMissing
		UMETA(DisplayName = "Per Percent Of Maximum Health Missing"),
};

/**
 * What is true of the character at the moment a stat is being worked out.
 *
 * SEPARATE FROM THE SKILL'S TAGS BECAUSE IT CHANGES WITHOUT ANYTHING BEING
 * APPLIED OR REMOVED. A character's health moves several times a second and no
 * gear changed, so a conditional bonus cannot be folded into a gameplay
 * attribute the way an unconditional one is: it would be stale the moment the
 * next blow landed. `UCataclysmAbilitySystemComponent::StatForSkill` builds one
 * of these from the character's own vitals and hands it to the pipeline, so no
 * caller has to know that a stat has a condition on it.
 *
 * UNKNOWN IS THE DEFAULT AND IT REFUSES EVERY CONDITION. A caller with no
 * character in hand -- the character sheet, a test passing plain numbers -- gets
 * the unconditional answer, which is what it was getting before conditions
 * existed. Answering "the condition holds" for an unknown state would give a
 * character sheet the low-health bonus while the character stood at full health.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmStatConditions
{
	GENERATED_BODY()

	/** Current health as a percentage of maximum, 0 to 100. Negative is unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float HealthPercent = -1.0f;

	/**
	 * Seconds since the character last paid a health cost. Issue #962.
	 *
	 * NEGATIVE MEANS NEITHER KNOWN NOR EVER, and the two do not have to be told
	 * apart because both answer no. A caller with no character in hand and a
	 * character that has never cast a skill charging health are the same
	 * question as far as a window is concerned.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SecondsSinceHealthCost = -1.0f;

	/** A state built from a character's own numbers. Refuses nothing it knows. */
	static FCataclysmStatConditions FromHealth(float Health, float MaxHealth)
	{
		FCataclysmStatConditions State;
		if (MaxHealth > 0.0f)
		{
			State.HealthPercent =
				FMath::Clamp(Health / MaxHealth * 100.0f, 0.0f, 100.0f);
		}
		return State;
	}
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

	/**
	 * A state of the CHARACTER this modifier depends on, or Always. Issue #959.
	 *
	 * BOTH THIS AND `RequiredTags` MUST HOLD. They ask about different things --
	 * this about the character, that about the skill in hand -- so a modifier
	 * carrying both applies only when both are satisfied.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Stats")
	ECataclysmStatCondition Condition = ECataclysmStatCondition::Always;

	/** What the condition compares against. A percentage for the health one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Stats")
	float ConditionValue = 0.0f;

	/**
	 * A state of the CHARACTER this modifier's size grows with, or Fixed. #968.
	 *
	 * A SECOND AXIS BESIDE `Condition`, NOT AN ALTERNATIVE TO IT. One decides
	 * whether the modifier is in the sum, the other how large it is when it is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Stats")
	ECataclysmStatScale Scale = ECataclysmStatScale::Fixed;

	/**
	 * How large one step of that state is, in the state's own units.
	 *
	 * `Value` IS WHAT ONE WHOLE STEP IS WORTH. Vicious Onslaught at ten points
	 * is a `Value` of 10 with a `ScaleStep` of 5, so a character 12% below full
	 * health carries two steps and gets +20%.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Stats")
	float ScaleStep = 0.0f;
};

/**
 * Everything one stat needs to be worked out again for a particular skill.
 *
 * WHY THIS HAS TO BE KEPT. This class's own reason for existing, stated at the
 * top of it, is that "a character's area of effect has no single value -- it is
 * one number for an area skill and another for a single-target one". But
 * `UCataclysmPlayerClassStats::ApplyTo` worked every stat out once, with no
 * skill in hand, and wrote a single number onto the gameplay attribute. The base
 * and the modifier list were local variables that went out of scope, so a skill
 * had nothing left to ask with, and every modifier naming a required tag was
 * discarded and never seen again. Issue #943.
 *
 * SO THE INPUTS ARE KEPT AND THE ANSWER IS NOT. `ApplyTo` stores one of these
 * per stat on `UCataclysmAbilitySystemComponent`, and `EvaluateForSkill` runs
 * the same pipeline over them with the skill's own tags.
 *
 * THE WHOLE MODIFIER LIST, NOT ONLY THE SCOPED PART, AND THAT IS THE POINT.
 * Applying the scoped modifiers on top of an attribute that already had the
 * unscoped ones folded in would multiply two increase brackets together instead
 * of summing them into one. A base of 100 with an unscoped +50% and a scoped
 * +50% is 200 through one pipeline and 225 through two. The design says
 * increases add: `docs/DECISIONS.md`, "a conditional increase joins the
 * increases bracket rather than becoming a third multiplier."
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmStatInputs
{
	GENERATED_BODY()

	/** Where the stat starts, before anything applies. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float Base = 0.0f;

	/** Every modifier on it, scoped and unscoped alike. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	TArray<FCataclysmStatModifier> Modifiers;
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

	/**
	 * Whether this modifier applies right now.
	 *
	 * TWO QUESTIONS AND BOTH MUST BE YES: the skill in hand carries every tag
	 * the modifier requires, and the character is in the state it requires.
	 *
	 * @param State  what is true of the character. The default knows nothing, so
	 *               a modifier with a condition is refused -- which is right for
	 *               a caller with no character in hand, such as the character
	 *               sheet or a test passing plain numbers. Issue #959.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Stats")
	static bool ModifierApplies(const FCataclysmStatModifier& Modifier,
								const FGameplayTagContainer& SkillTags,
								const FCataclysmStatConditions& State =
									FCataclysmStatConditions());

	/** Whether the character is in the state this condition names. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Stats")
	static bool ConditionHolds(ECataclysmStatCondition Condition, float Value,
							   const FCataclysmStatConditions& State);

	/**
	 * What this modifier is worth right now, after any scaling. Issue #968.
	 *
	 * `Modifier.Value` FOR A FIXED ONE, which is every modifier in the game
	 * before that issue, so nothing that does not scale changes at all.
	 *
	 * ZERO WHEN THE STATE IS UNKNOWN OR THE STEP IS NOT A REAL SIZE. Both are
	 * answers rather than errors: the character sheet has no character in hand,
	 * and a step of zero would be a division by nothing. `ValidateModifier`
	 * refuses the second when data is imported.
	 *
	 * PUBLIC SO A TEST CAN ASK IT DIRECTLY, and because a character sheet
	 * showing a player what a node is worth right now needs the same answer.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Stats")
	static float ScaledValue(const FCataclysmStatModifier& Modifier,
							 const FCataclysmStatConditions& State);

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
	 *
	 * @param State  what is true of the character, for a modifier that carries a
	 *               condition. The default knows nothing and refuses every
	 *               condition, which is what every caller got before issue #959.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Stats")
	static FCataclysmStatBreakdown Evaluate(float Base,
											const TArray<FCataclysmStatModifier>& Modifiers,
											const FGameplayTagContainer& SkillTags,
											const FCataclysmStatConditions& State =
												FCataclysmStatConditions());

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
												const FGameplayTagContainer& SkillTags,
												const FCataclysmStatConditions& State =
													FCataclysmStatConditions());

	/** What a player is shown, as a percentage. Never reaches 100. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Stats")
	static float DisplayedRateReduction(const FCataclysmStatBreakdown& Breakdown);

private:
	/** Shared by Evaluate and EvaluateRate; they differ only in the last step. */
	static FCataclysmStatBreakdown Accumulate(float Base,
											  const TArray<FCataclysmStatModifier>& Modifiers,
											  const FGameplayTagContainer& SkillTags,
											  const FCataclysmStatConditions& State);
};
