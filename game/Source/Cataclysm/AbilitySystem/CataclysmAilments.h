// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmAilments.generated.h"

class AActor;
class UAbilitySystemComponent;
class UGameplayAbility;
struct FGameplayEffectSpec;
struct FGameplayTagContainer;

/**
 * What an ailment does when its chance lands. Issue #899.
 *
 * SIX SHAPES FOR ELEVEN AILMENTS, because the ailments differ in what their rows
 * say and not in how they are applied. Every shape is one call into
 * `UCataclysmSkillEffects`, which is where every lasting effect in the game is
 * applied, so an ailment from gear is the same effect a skill applies and the
 * same one `UCataclysmDebuffs` counts.
 */
enum class ECataclysmAilmentShape : uint8
{
	/** Bleed, Poison, Disease, Necrosis and Burn: the row's damage a tick times
	 *  the magnitude, for the row's duration. Every row says "Magnitude scales
	 *  the damage". */
	DamageOverTime,

	/** Shred: the row's strength times the magnitude, for the row's duration.
	 *  Its row says magnitude raises the reduction. */
	StrongerWithMagnitude,

	/** Madness, whose row says "Magnitude extends the duration". */
	LongerWithMagnitude,

	/** Cripple and Weaken, at the row's own figures. Their rows say magnitude
	 *  raises the reduction to 80% and then extends the duration. Neither half
	 *  is built, so a chance past 100% applies them no harder yet. */
	AtItsRowsFigures,

	/** Stun, which shares one roll with a blunt weapon's own 10%. */
	Stun,

	/** Void Splinter: the row's share of the target's current health on each
	 *  tick, times the magnitude, for the row's duration. Issue #915. */
	ShareOfCurrentHealth,
};

/**
 * One ailment a blow can carry a chance to apply. Issue #899.
 *
 * EVERY NAME ONE AILMENT IS KNOWN BY IS ON ONE ROW, so the affix sheet, the stat
 * pipeline, the damage effect and the status effect table are joined in one
 * place rather than by four lists that could drift apart.
 */
struct FCataclysmAilmentKind
{
	/** The `Ailment` column of `game/Data/Affixes.csv`: "Bleed", "Void Splinter". */
	const TCHAR* Ailment;

	/** The stat holding the chance, in percent: "bleed_chance". The eleven names
	 *  were agreed with the session building enchantments, whose rows add to
	 *  the same stats. */
	const TCHAR* Stat;

	/** The name the chance travels under on a damage effect, as a set-by-caller
	 *  number: "Cataclysm.AilmentChance.Bleed". A plain name and not a gameplay
	 *  tag, for the reason `UCataclysmSkillEffects::StatedMagnitudeDataName`
	 *  gives: gameplay tags are generated from the workbook. */
	const TCHAR* DataName;

	/** The row of `game/Data/StatusEffects.csv` that says what the ailment does. */
	const TCHAR* StatusRow;

	/** The tag the applied effect grants. */
	const TCHAR* TagName;

	/** The gameplay attribute holding the chance. */
	FGameplayAttribute (*Attribute)();

	ECataclysmAilmentShape Shape;
};

/**
 * A chance to apply an ailment on a hit, worked out where the blow is struck and
 * rolled where it lands. Issue #899.
 *
 * WHAT WAS MISSING. Eleven gear affixes read "Chance to bleed", "Chance to stun"
 * and so on, and none of them did anything. `UCataclysmItemModifiers` skipped
 * them with a comment saying they were applied where the hit is resolved, and
 * nothing applied them there or anywhere else.
 *
 * THE CHANCE BELONGS TO THE ATTACKER AND IS WORKED OUT ON THE ATTACKER'S SIDE.
 * `UCataclysmSkillEffects::ApplyHit` is the only place that holds the skill's
 * full tags, and it asks `ChancesFor` for each chance with them, so a row scoped
 * to melee or carrying a condition counts when it holds. Each chance above zero
 * travels on the damage effect as a set-by-caller number.
 *
 * THE ROLL IS MADE ON THE DEFENDER'S SIDE, because only the defender knows what
 * the blow did. `UCataclysmVitalAttributeSet` calls `RollOnLandedBlow` once the
 * blow has resolved.
 */
UCLASS()
class CATACLYSM_API UCataclysmAilments : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Every ailment, in the order `AILMENT_AFFIXES` lists them in
	 *  `sim/cataclysm_sim/affixes.py`. Eleven. */
	static TArrayView<const FCataclysmAilmentKind> Kinds();

	/**
	 * The ailment an `Ailment` cell of `game/Data/Affixes.csv` names, compared
	 * without regard to case, or null for a name no ailment has.
	 */
	static const FCataclysmAilmentKind* KindNamed(const FString& Ailment);

	/**
	 * Where a chance to apply stops being a chance. Mirrors `AILMENT_CHANCE_CAP`
	 * in `sim/cataclysm_sim/affixes.py`.
	 */
	static constexpr float ChanceCap = 100.0f;

	/**
	 * A total chance, split into the chance that is rolled and the magnitude
	 * the ailment lands at. Mirrors `ailment_application` in
	 * `sim/cataclysm_sim/affixes.py`.
	 *
	 * THE PROJECT OWNER'S RULE OF 2026-08-03, which the design document tabulates
	 * under "Chance to apply caps at 100%": 60% applies on 60% of hits at the
	 * normal magnitude, 250% on every hit at 2.5 times it, and 800% on every hit
	 * at 8 times it.
	 *
	 * A NEGATIVE TOTAL IS NO CHANCE AT ALL, where the model raises an error,
	 * because a running game should not stop over one bad row.
	 */
	static void Application(float TotalChance, float& OutChance, float& OutMagnitude);

	/**
	 * What the attacker has a chance to apply with a skill carrying these tags,
	 * keyed by the name each chance travels under. A chance of zero is left out,
	 * so a blow from anything holding no chance carries nothing.
	 *
	 * ASKED FOR RATHER THAN READ, through `StatForSkill`, so a row requiring a
	 * tag or a state counts when it holds. The attribute is the fallback, which
	 * is what an enemy and a character before its first refresh answer.
	 */
	static TMap<FName, float> ChancesFor(const UAbilitySystemComponent* Attacker,
										 const FGameplayTagContainer& SkillTags,
										 float SkillHealthCostPercent = -1.0f);

	/**
	 * Roll every chance a blow carried, now that it has landed, and apply what
	 * the rolls give.
	 *
	 * THE BLOW MUST HAVE TAKEN A TENTH OF THE TARGET'S MAXIMUM HEALTH, the project
	 * owner's rule for an ailment that does not come from the skill's own row
	 * (#917). The threshold and the figure it is measured against, damage dealt
	 * to health, are the ones the incidental stun has always used. So an evaded
	 * blow, a blow a shield took, and a blow that scratched apply nothing.
	 *
	 * A BLOW THAT KILLED APPLIES NOTHING, as `UCataclysmSkillEffects::ApplyBurn`
	 * already refuses to set a corpse alight.
	 *
	 * STUN IS ONE POOL. A blunt weapon's own 10% is added to the chance to stun
	 * the blow carried, and `UCataclysmDamageCalculation::StunApplication` turns
	 * the total into a chance and a length. The project owner, 2026-08-16.
	 *
	 * WHOEVER THE BLOW IS CREDITED TO APPLIES WHAT LANDS: the effect context's
	 * instigator, whose chances these are. For every blow sent through
	 * `ApplyHit` that is also the actor that struck.
	 *
	 * @param bIsBlunt  whether the blow came from a blunt weapon
	 * @return how many ailments were applied
	 */
	static int32 RollOnLandedBlow(const FGameplayEffectSpec& Spec, AActor* Defender,
								  float DealtToHealth, bool bIsBlunt);

	/**
	 * Apply one ailment at a magnitude, as its row of
	 * `game/Data/StatusEffects.csv` says. Stun applies nothing here, because
	 * `RollOnLandedBlow`'s pool applies it.
	 *
	 * @param Magnitude  one for a chance up to 100%, and the chance divided by
	 *                   100 past it
	 * @param Skill      the skill whose blow rolled it, if a skill's blow
	 *                   did. Carried on the effect context, so that the
	 *                   notice of each tick names it. Issue #41, slice 4
	 * @return whether anything was applied
	 */
	static bool Apply(AActor* Instigator, AActor* Target,
					  const FCataclysmAilmentKind& Kind, float Magnitude,
					  const UGameplayAbility* Skill = nullptr);
};
