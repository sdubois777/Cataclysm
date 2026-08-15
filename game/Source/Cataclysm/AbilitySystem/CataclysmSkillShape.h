// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CataclysmSkillShape.generated.h"

/**
 * Which shared template runs a skill.
 *
 * EIGHT SHAPES FOR 398 ROWS. Issue #37 asks for shared templates rather than
 * sixteen one-off implementations, because the full weapon-and-damage-type
 * matrix is 398 rows and bespoke work on the first sixteen would make the other
 * 382 unaffordable.
 *
 * THIS LIST AND `SHAPE_PARAMS` IN `tools/generate_datatables.py` MUST AGREE. When
 * they did not, the consequence was silent: `Deployable` was added to the
 * generator on issue #338 and not here, so the three skills naming it read as
 * None, were granted the placeholder ability, and filled their slot doing
 * nothing for as long as nobody ran the full automation suite. Issue #621.
 *
 * THE LIST IS NOT INVENTED. Path of Exile's own `active_skill_types` list, which
 * ships in its data files, carves the same joints: Projectile, Melee,
 * MeleeSingleTarget, Movement, Blink, Travel, Aura, Buff, Minion, CreatesMinion,
 * Channel and AppliesCurse are all separate entries in it. Seven is that list
 * collapsed to what the sixteen designed Demonic skills actually need.
 *
 * WHAT IS NOT A SHAPE: the persistent burning ground zone. Eight of the sixteen
 * leave one behind whatever else they do -- Molten Cleave is a strike that also
 * leaves slag, Emberhurl is a projectile that also leaves a burning flight path
 * -- so it is a rider every shape can carry, not a shape of its own. Issue #37's
 * own table hints at this: every other entry names skills, and that one says
 * "used by most of the above".
 */
UENUM(BlueprintType)
enum class ECataclysmSkillShape : uint8
{
	/** No behaviour designed yet. The slot is filled and nothing happens. */
	None			UMETA(DisplayName = "None"),

	/** Hits everything in a cone or ring around the caster, once or repeatedly. */
	Strike			UMETA(DisplayName = "Strike"),

	/** Travels out from the caster, hitting what it passes or where it lands. */
	Projectile		UMETA(DisplayName = "Projectile"),

	/** Grants an effect to the caster for a duration. */
	SelfBuff		UMETA(DisplayName = "Self Buff"),

	/** Moves the caster: a leap, a charge or a blink. */
	Movement		UMETA(DisplayName = "Movement"),

	/** Spawns minions that fight for the caster. They walk to the enemy. */
	Summon			UMETA(DisplayName = "Summon"),

	/**
	 * Places machines that stay where they are put. A turret, a ballista, a
	 * spike trap.
	 *
	 * THE SPLIT FROM Summon IS BEHAVIOURAL, not a naming preference: a summon
	 * spawns things that walk to the enemy, a deployable places things that do
	 * not move. The data already carried it as a tag -- Bolt Turret, Ballista
	 * and Iron Fortress all have Type.Deployable -- before it was a shape.
	 */
	Deployable		UMETA(DisplayName = "Deployable"),

	/** A radius around the caster, held as a toggle or for a duration. */
	Aura			UMETA(DisplayName = "Aura"),

	/** Applies an effect to enemies at range without necessarily damaging them. */
	Debuff			UMETA(DisplayName = "Debuff"),
};

/** How a Movement skill travels. */
UENUM(BlueprintType)
enum class ECataclysmMovementMode : uint8
{
	/** An arc to the destination, hitting on landing. Infernal Plunge. */
	Leap			UMETA(DisplayName = "Leap"),

	/** A run along the ground, hitting everything on the way. Cinder Rush. */
	Charge			UMETA(DisplayName = "Charge"),

	/** Instant, hitting at both ends and nothing between. Emberstep. */
	Blink			UMETA(DisplayName = "Blink"),
};

/**
 * One kind of minion a skill produces, and how many of it.
 *
 * A LIST RATHER THAN ONE NAME, because Iron Fortress deploys two ballistae AND
 * three spike traps, which no single key and value can say. The Shape Params
 * cell writes it as `Ballista:2, SpikeTrap:3`.
 *
 * THE NAME IS NOT VALIDATED HERE. `validate_minion_references` in
 * `tools/generate_datatables.py` refuses a name the Minion Types sheet does not
 * have, so a generated table cannot carry one. Repeating that check in C++ would
 * be a second copy of the list that could go stale.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmMinionSpawn
{
	GENERATED_BODY()

	/** The row name in the Minion Types sheet, such as "Ballista". */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	FString Type;

	/** How many of that type. Always one or more; the generator refuses zero. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	int32 Count = 0;
};

/**
 * One skill's numbers, read from the Shape Params cell of the Weapon Skills sheet.
 *
 * DISTANCES ARE HELD IN CENTIMETRES AND THE SHEET WRITES METRES. Unreal's world
 * unit is the centimetre and every design document in this project speaks in
 * metres, so a conversion has to happen somewhere. It happens once, here, at the
 * parse; the fields are named for the unit they hold so that a later reader
 * cannot mistake which side of the conversion they are on. A skill that hit a
 * hundredth of its written radius would still run, still spend mana and still
 * look almost right, which is the kind of failure this project keeps finding.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmSkillShapeParams
{
	GENERATED_BODY()

	/** Metres per Unreal world unit is 1/100. Written once. */
	static constexpr float CentimetresPerMetre = 100.0f;

	// --- Reach ------------------------------------------------------------

	/** How wide the effect is, in centimetres. Zero hits nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float RadiusCm = 0.0f;

	/** How far from the caster it reaches, in centimetres. Zero means at self. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float RangeCm = 0.0f;

	/** Full width of a Strike's cone, in degrees. 360 is a ring. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float AngleDegrees = 0.0f;

	/** How many enemies one use may affect. Zero means no limit. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	int32 MaxTargets = 0;

	// --- Time -------------------------------------------------------------

	/** Seconds the skill's own effect lasts. Zero is instant. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float Duration = 0.0f;

	/** Seconds between repeats while it lasts. Zero means it does not repeat. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float Interval = 0.0f;

	// --- Projectile -------------------------------------------------------

	/** How many enemies a projectile passes through. Zero stops at the first. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	int32 Pierce = 0;

	/** True when the projectile comes back, hitting a second time. Emberhurl. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	bool bReturns = false;

	/** Centimetres per second. Zero means it arrives instantly, like a beam. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float SpeedCmPerSecond = 0.0f;

	// --- Movement ---------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	ECataclysmMovementMode MovementMode = ECataclysmMovementMode::Blink;

	/** How far a hit enemy is pushed, in centimetres. Zero pushes nobody. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float KnockbackCm = 0.0f;

	// --- Summon -----------------------------------------------------------

	/** How many minions one use spawns. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	int32 Count = 1;

	/** The cap on minions alive at once. Zero means no cap. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	int32 MaxActive = 0;

	/**
	 * What the skill produces, and how many of each. Empty when it produces
	 * nothing.
	 *
	 * WITHOUT THIS EVERY SUMMON SPAWNED THE SAME THING. The parameter was
	 * written in the sheet from issue #338 and this parser rejected the whole
	 * cell as unreadable, so a skill naming a Ballista and one naming an Imp
	 * arrived here identical. Issue #622.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	TArray<FCataclysmMinionSpawn> Minions;

	/**
	 * Percent of its own type's health each thing produced is given, when the
	 * skill raises it. Zero means the type's own health, unchanged.
	 *
	 * NOT `HealthCostPercent`, WHICH IS A DIFFERENT NUMBER ENTIRELY. That one is
	 * a cost in the caster's own health, which Blood Pyre charges. This one
	 * makes what a skill deploys tougher than the same machine deployed by
	 * something else, and Iron Fortress is the only skill that states it, at
	 * 150.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float MinionHealthPercent = 0.0f;

	// --- Self buff --------------------------------------------------------

	/**
	 * Percentage points of increased damage the buff grants, per burning enemy
	 * that was inside Radius when it went up.
	 *
	 * Burning Wrath is "4% increased fire damage for every enemy currently
	 * burning within 15 meters", so this is 4 and the count is taken once. Zero
	 * means the buff grants no increase, which is every other self buff.
	 *
	 * WHY PER BURNING ENEMY AND NOT A PLAIN INCREASE. Because that is the only
	 * form the design uses for a self buff's magnitude, and a plain increase is
	 * the case where the count happens to be one. A second key for the flat
	 * case can be added when a skill needs it; inventing it now would be a
	 * parameter nothing writes.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float IncreasePerBurning = 0.0f;

	// --- Riders every shape may carry -------------------------------------

	/** True when the skill sets what it hits alight. Fifteen of sixteen do. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	bool bBurns = false;

	/** Radius of the burning ground left behind, in centimetres. Zero leaves none. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float GroundRadiusCm = 0.0f;

	/** Seconds that ground burns. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float GroundDuration = 0.0f;

	/**
	 * Percent of the skill's own damage that ground deals per second.
	 *
	 * THE RULE IT CARRIES, decided on issue #361: standing in a patch for its
	 * whole GroundDuration costs one hit of the skill that left it, so this is
	 * 100 divided by GroundDuration. That keeps burning ground area denial
	 * rather than a second damage source, and stops a longer patch being
	 * automatically a bigger one.
	 *
	 * BEFORE ISSUE #590 THE ENGINE DID NOT READ IT. It derived the figure from
	 * the Burn status effect instead -- 20% of a hit spread over 4 seconds, so
	 * 5% per second whatever the patch's own duration was. A three second patch
	 * was therefore worth 15% of a hit and a ten second one 50%, which is
	 * exactly the "longer means bigger" property the rule was chosen to remove.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float GroundPercent = 0.0f;

	/** Percent of weapon damage a closing hit deals. Pyroclasm states 300. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float FinalHitPercent = 0.0f;

	/** Percent of current health one use costs. Blood Pyre states 8. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float HealthCostPercent = 0.0f;

	/**
	 * The named status effect this skill applies, if it applies one.
	 *
	 * A name from the Buffs, Debuffs or DoTs sheets, such as "Madness". Empty
	 * when the skill applies no named effect, which is true of the two designed
	 * self buffs: the design gives them a duration and a magnitude but never
	 * names the buff itself.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	FString Effect;

	/** True when the cell parsed with no unreadable entry. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	bool bValid = true;

	/** Leaves a burning patch of ground behind. Both halves are needed. */
	bool LeavesGround() const
	{
		return GroundRadiusCm > 0.0f && GroundDuration > 0.0f;
	}
};

/**
 * Reading a skill's shape and numbers out of the two generated columns.
 *
 * Static functions over strings, like UCataclysmDamageCalculation is static
 * functions over numbers, so the whole of it can be tested by passing text in
 * without constructing an ability or a world.
 *
 * THE GENERATOR ALREADY REFUSED A BAD CELL. tools/generate_datatables.py rejects
 * an unknown shape, an unknown parameter, a repeated one and a non-numeric
 * value, so anything reaching here has been checked once. This parser is
 * deliberately not a second validator of the same rules -- it is the reader --
 * but it does record that it failed rather than returning zeros, because a
 * radius of zero and a radius nobody wrote look identical at the point of use.
 */
UCLASS()
class CATACLYSM_API UCataclysmSkillShapes : public UObject
{
	GENERATED_BODY()

public:
	/** The shape a Shape column value names, or None if it names no shape. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Shape")
	static ECataclysmSkillShape ShapeFromName(const FString& ShapeName);

	/** The name a shape is written as in the sheet. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Shape")
	static FString NameOfShape(ECataclysmSkillShape Shape);

	/**
	 * Read a `Key=Value; Key=Value` cell.
	 *
	 * @param Text      the Shape Params cell
	 * @param OutError  set to the first unreadable entry, empty when all read
	 */
	static FCataclysmSkillShapeParams ParseParams(const FString& Text,
												  FString* OutError = nullptr);

	/**
	 * The Status.* tag for a named effect, or an invalid tag for none.
	 *
	 * The tags are generated from the Buffs, Debuffs and DoTs sheets by
	 * tools/generate_gameplay_tags.py, so an effect the design lists has a tag
	 * and one it does not, does not. Punctuation is dropped, because a tag
	 * segment allows only letters, digits and underscores: "Void Splinter"
	 * becomes Status.VoidSplinter.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Shape")
	static FGameplayTag StatusTagFor(const FString& EffectName);

	/**
	 * Read a comma-separated Tags cell into a container.
	 *
	 * WHAT THE TAGS ARE FOR. Scoping. UCataclysmStatPipeline::ModifierApplies
	 * asks whether the skill in hand carries every tag a modifier requires, so
	 * an increase scoped to Element.Demonic reaches a Demonic skill and no
	 * other.
	 *
	 * A NAME THAT IS NOT A REGISTERED TAG IS SKIPPED, not treated as an error.
	 * tools/generate_datatables.py already checks every tag in every Tags cell
	 * against the generated tag list and refuses the row otherwise, so a name
	 * arriving here that the manager does not know means the table was edited
	 * in the editor rather than generated. Skipping narrows what that skill
	 * scales with; failing would stop it running at all, which is worse.
	 */
	static FGameplayTagContainer TagsFromCell(const FString& Cell);
};
