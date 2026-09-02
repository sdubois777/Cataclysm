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

	/**
	 * The caster and one of its own minions exchange places. Vesselstep.
	 *
	 * THE ONLY MOVEMENT THAT COSTS SOMETHING THE PLAYER OWNS. A blink needs
	 * only a destination; this needs a creature standing somewhere useful, so
	 * the Staff's minions become a mobility resource as well as a damage one.
	 *
	 * **NOT BUILT. IT RUNS AS A LEAP.** The Movement template has no branch for
	 * it, so Vesselstep moves the caster to the aimed point and leaves the minion
	 * where it was. Issue #1139, which named three modes in this state;
	 * `Recall` and `Flicker` below were built on 2026-09-02 and this is the one
	 * left. The paragraph above describes what it is meant to do, not what it
	 * does.
	 */
	Swap			UMETA(DisplayName = "Swap"),

	/**
	 * A return to a mark left earlier rather than a departure. Echo.
	 *
	 * WHAT SEPARATES IT FROM Blink is which end the player chooses. Ashwalk
	 * picks where to arrive; Echo picked that when it left, and chooses only
	 * when to go back.
	 */
	Recall			UMETA(DisplayName = "Recall"),

	/**
	 * Repeats, arriving at one enemy after another for as long as it runs.
	 * Everywhere at Once.
	 *
	 * A MOVEMENT THAT IS ALSO AN ULTIMATE, and the only one. It is why Movement
	 * reads Duration and Interval, which until 2026-09-01 only Strike, Summon
	 * and Deployable did.
	 */
	Flicker			UMETA(DisplayName = "Flicker"),
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

	/**
	 * How high a lobbed projectile rises above the straight line to where it
	 * lands, as a fraction of the distance thrown. Zero means it travels flat.
	 *
	 * A SHAPE RATHER THAN A SPEED, which is why it is not one. A lob follows real
	 * projectile motion, so there is no single speed to state: it is slowest at
	 * the top of its arc and fastest as it lands. The figure is not chosen
	 * either -- a projectile launched at 45 degrees, the angle that throws
	 * furthest, reaches an apex of one quarter of its range, which is why the
	 * Brute's thrown rock states 0.25.
	 *
	 * NO PLAYER SKILL STATES IT TODAY. It is read so that one can.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float ArcHeightFraction = 0.0f;

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

	// --- Scaling ----------------------------------------------------------

	/**
	 * Percentage points of MORE damage per unit of ScalingSource -- the
	 * multiplicative bucket, which applies separately from every additive
	 * source on the character.
	 *
	 * REPLACED `IncreasePerBurning` ON 2026-09-01. That key said one thing in
	 * two ways at once: what the number was per, and which bucket it joined.
	 * Burning Wrath was the only skill that wrote it, and it was written into
	 * the additive bucket where it competed with every gear affix the character
	 * wore. The project owner's reading is that a skill is chosen the way a
	 * passive node is chosen, so it may use the wording section VI reserves for
	 * things a drop cannot hand you by accident.
	 *
	 * Burning Wrath is now "4% more fire damage for every enemy currently
	 * burning within 15 meters": this is 4, ScalingSource is Burning, and the
	 * count is taken once when the buff goes up.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float MoreDamagePer = 0.0f;

	/**
	 * Percentage points of MORE damage on a blow landed from behind.
	 *
	 * ONE ROW STATES IT: the Dagger's Emberpierce, "the strike deals 40% more
	 * damage from behind". Its sentence had no parameter at all until 2026-09-02,
	 * so the row read as finished and dealt the same damage from every side.
	 *
	 * `PER`-LESS, UNLIKE `MoreDamagePer` ABOVE, AND THAT IS THE DIFFERENCE. That
	 * one is a rate multiplied by a count of something, so it needs a
	 * `ScalingSource` to say what it counts. This is a flat multiplier applied
	 * once when a condition holds, and the condition is geometric rather than
	 * countable.
	 *
	 * IT IS THE `more` BUCKET AND NOT THE `increased` ONE, which is why the name
	 * says so. `UCataclysmSkillTemplate::HitTargets` multiplies the skill's own
	 * damage percent by it, and everything else the design's pipeline applies --
	 * the character's increases, its other more multipliers, the target's
	 * mitigation -- is applied to that result.
	 *
	 * WHICH BLOWS COUNT IS `UCataclysmSkillEffects::IsBehind`, or the whole skill
	 * when its row states `RearHits=1`.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float MoreDamageFromBehind = 0.0f;

	/**
	 * Percentage points of INCREASED damage per unit of ScalingSource -- the
	 * additive bucket, summed and applied once.
	 *
	 * TWO KEYS RATHER THAN ONE WITH A BUCKET BESIDE IT, deliberately. Which
	 * bucket a number joins is the single most important thing about a damage
	 * number in this design, and a shared key would have put the two one typo
	 * apart. Searing Hook writes this one, at 1 per percent of maximum health
	 * missing.
	 *
	 * WHAT IT MOVES IS THE SKILL'S OWN DAMAGE PERCENT, not the character's
	 * increases sum. `UCataclysmSkillTemplate::ScaledDamagePercent` is where it
	 * is applied and its comment carries the reasoning: every row using this
	 * describes the result as a percentage of weapon damage, and
	 * `MaxDamagePercent` below is written in that same unit, so a bonus put into
	 * the character's sum could not be capped by it at all. What the bucket
	 * still decides is how this combines with `MoreDamagePer` on one skill --
	 * summed against multiplied.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float IncreasedDamagePer = 0.0f;

	/** Seconds added to the skill's own duration per unit of ScalingSource. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float DurationPer = 0.0f;

	/**
	 * What the three keys above count, as written in the sheet.
	 *
	 * One of Kill, Burning, Second, Meter, HitTaken, Consume, Consumed, Bounce,
	 * Pierced, Pinned or HealthMissing. The generator holds that closed list and
	 * refuses anything else, so this is stored rather than validated again here.
	 *
	 * SEVEN OF THE ELEVEN ARE ACTED ON. `Burning`, `Kill` and `Pinned` by
	 * UCataclysmSelfBuffSkill -- the first and third counted once when the buff
	 * goes up, the second counted forward as kills happen; `HealthMissing`,
	 * `Consumed` and `Consume` by UCataclysmSkillTemplate::ScalingUnits, which is
	 * asked once per blow; and `Bounce` by UCataclysmProjectileSkill, which
	 * counts how far along a chain of glances a hit is.
	 *
	 * THE REMAINING FOUR -- Second, Meter, HitTaken and Pierced -- are read so
	 * the design can state them and scale by nothing, which is the state
	 * StunSeconds is in and is recorded rather than hidden. A skill naming one
	 * deals its plain damage. Three of the four belong to the Greatsword and
	 * issue #1141 carries them.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	FString ScalingSource;

	/**
	 * Ceiling on a skill whose damage scales, as percent of weapon damage.
	 *
	 * APPLIED AFTER THE SKILL'S OWN SCALING AND BEFORE THE CHARACTER'S, by
	 * UCataclysmSkillTemplate::ScaledDamagePercent. Extinction states 500 and
	 * the Sword's Ultimate slot supplies its base, so what this caps is how far
	 * consuming many fires at once can carry one blow -- not what a character's
	 * gear and passive tree then multiply it by. Capping after those would put a
	 * ceiling on the whole character, which no row asks for.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float MaxDamagePercent = 0.0f;

	/**
	 * Floor for a charged skill released at once, as percent of weapon damage.
	 *
	 * NOT READ. The Greatsword's Backswing is the only row that states one, and
	 * a floor for an early release means nothing until something can be held and
	 * released early. Issue #1141 carries the whole hold-and-release verb --
	 * this, ChargeTime, ChargeBreaksOn and bDisarmsUntilRecalled.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float MinDamagePercent = 0.0f;

	// --- Self buff --------------------------------------------------------

	/** Percentage points added to the caster's attack range. Coil of Embers. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float RangeIncrease = 0.0f;

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
	 * True when that ground burns everything standing in it whatever side it is
	 * on, including whatever left it.
	 *
	 * FALSE IS WHAT EVERY PLAYER SKILL WANTS, and none states this. The
	 * Hellhound's fire trail is the only thing in the design that sets it, which
	 * is what makes its own trail dangerous to the pack behind it. It is read
	 * here so a player skill could state it rather than being silently refused.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	bool bGroundHitsAllies = false;

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

	/**
	 * Seconds the skill holds its target still, if it stuns.
	 *
	 * THE DURATION, NOT A PROMISE THAT A STUN LANDS. The anti-stun-lock rule
	 * still applies: a stunned target cannot be stunned again for 5 seconds and
	 * a boss cannot be stunned at all. The one rule this exempts a skill from is
	 * the damage threshold, and only because the skill also states `Effect=Stun`,
	 * which is what "the skill states stunning as its effect" means.
	 *
	 * FOUR PLAYER SKILLS STATE IT and none could until issue #588, because all
	 * four had an empty Shape cell and the generator refuses parameters on a row
	 * with no shape. Shield Bash 1.5, Shockwave Leap 1.0, Lunge 0.75, Whip Swing
	 * 0.75. The Brute's stomp states 1.5 on the enemy side.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float StunSeconds = 0.0f;

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

	/**
	 * Seconds an applied Effect lasts.
	 *
	 * NOT `Duration`, WHICH IS THE SKILL'S OWN LENGTH. Anathema runs for an
	 * instant and leaves a curse for ten seconds; Butcher's Bill runs for ten
	 * seconds and leaves nothing. Both wrote "10" against the same key until
	 * this was split out on 2026-09-01, and no reader could tell which was meant.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float EffectDuration = 0.0f;

	/**
	 * Size of the applied Effect, in whatever unit that effect is measured in.
	 *
	 * TWO READERS SINCE 2026-09-01, AND ONE OF THEM NEEDS NO NAMED EFFECT.
	 * `UCataclysmSkillEffects::ApplyNamedEffect` uses it as the size of the
	 * status the row's `Effect` names, and `ApplyPin` uses it as the percentage
	 * points a pinned target adds to its Damage Taken. The Spear's Impale is the
	 * only row in the sheet that states a magnitude without naming an effect --
	 * "while a target is pinned it takes 30% more damage from every source" --
	 * and no row states both a named effect and forced movement, so there is
	 * nothing today for which the two readings could disagree.
	 *
	 * A ROW THAT EVER STATED BOTH WOULD APPLY ONE NUMBER TWICE, and that is the
	 * thing to notice here rather than to guard against now: splitting the key in
	 * two before a row needs it would be a second name one typo away from the
	 * first, which is the mistake `MoreDamagePer` and `IncreasedDamagePer` above
	 * were deliberately kept apart to avoid making cheaply.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float EffectMagnitude = 0.0f;

	// --- Forced movement ---------------------------------------------------

	/**
	 * What the skill does to a target beyond pushing it, as written in the
	 * sheet: one or more of Knockdown, Launch, Pull, Drag or Pin.
	 *
	 * `Knockback` ABOVE IS NOT ONE OF THESE. That key is metres away from the
	 * caster and stays exactly what issue #626 made it; three enemy abilities
	 * write it. This carries the verbs a distance cannot express.
	 *
	 * ALL FIVE ARE READ, by `UCataclysmSkillTemplate::ApplyForcedMovementTo`,
	 * which rides every blow the way the knockback above does. Two of the five
	 * hold a target and three move it, and section VI of the design document
	 * treats those two groups completely differently: a Knockdown takes all
	 * three anti-stun-lock rules, a Pin takes none of them, and Pull, Drag and
	 * Launch are displacements limited only by halving on repeat. Issue #1149
	 * carries the open question of whether a Pin should be covered after all.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	FString ForcedMovement;

	/**
	 * Centimetres a pull, drag or launch carries the target.
	 *
	 * ZERO MEANS ALL THE WAY FOR A PULL OR A DRAG, which is what both rows that
	 * haul ask for and neither states: The Gathering brings its catch "into a
	 * burning heap at your feet" and Reel dumps them "at your feet", and a
	 * distance in a cell could only repeat the skill's own range.
	 *
	 * ZERO MEANS NOTHING AT ALL FOR A LAUNCH, deliberately, because there is no
	 * height a launch obviously means. Upthrust states 3.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float ForcedMovementDistanceCm = 0.0f;

	/**
	 * Seconds the target is held where it was put.
	 *
	 * READ BY A PIN AND A KNOCKDOWN AND BY NOTHING ELSE. A displacement is over
	 * the moment it lands, so the three rows that only displace state no
	 * duration and none is wanted.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float ForcedMovementDuration = 0.0f;

	// --- Terrain -----------------------------------------------------------

	/**
	 * Persistent geometry the skill leaves: Pit, Wall, Fissure or Thicket.
	 *
	 * DISTINCT FROM THE BURNING GROUND ABOVE. That is a damage patch and this
	 * changes where a fight can happen -- one burns you for standing there, the
	 * other decides where "there" is. The Warhammer's whole verb is this one.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	FString Terrain;

	/** Centimetres: radius for a pit, fissure or thicket, length for a wall. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float TerrainSizeCm = 0.0f;

	/** Seconds the geometry persists. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float TerrainDuration = 0.0f;

	// --- Conditions and commitment -----------------------------------------

	/**
	 * A condition the skill needs, as written: Burning, Target, Stationary or
	 * RearHit. More than one may be named, comma separated.
	 *
	 * IT REFUSES THE CAST, from `UCataclysmSkillTemplate::CanActivateAbility`,
	 * so a skill whose condition fails costs no mana and starts no cooldown.
	 * `Burning` and `Target` also decide where a Movement skill arrives, which
	 * is Flashpoint's "only something already alight can be reached".
	 *
	 * `RearHit` GATES NOTHING, and that is deliberate rather than missing.
	 * Slipstream is a support buff that reacts to blows landed from behind while
	 * it runs; refusing to cast it until the player was already behind something
	 * would be a different skill. `RequirementsAreMet` says so at more length.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	FString Requires;

	/**
	 * Seconds of hold before a full release. Backswing states 2.
	 *
	 * NOT READ. Nothing in the game can be held. Issue #1141.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float ChargeTime = 0.0f;

	/**
	 * What cancels a hold and loses the skill: Stagger, Death, Movement, or
	 * None when nothing can. Inexorable is the one that writes None, because
	 * the player cannot stop it either.
	 *
	 * NOT READ, for the same reason as ChargeTime above. Issue #1141.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	FString ChargeBreaksOn;

	// --- Consumption -------------------------------------------------------

	/**
	 * True when the skill spends the target's burn rather than only applying it.
	 *
	 * THE SWORD'S WHOLE VERB, and all five of its skills state it.
	 * `docs/DECISIONS.md` gives each Demonic weapon one mechanical verb no other
	 * weapon may use.
	 *
	 * IT TAKES THE FIRE OUT AND DEALS NOTHING BY ITSELF.
	 * `UCataclysmSkillTemplate::ConsumeBurnFrom` removes the burn; what it was
	 * worth is decided by the skill that spent it, through ScalingSource's
	 * `Consume` and `Consumed` and through ConsumeRadius below.
	 *
	 * AND THE BLOW THAT FOLLOWS LIGHTS THEM AGAIN, when the skill also states
	 * `Burn`, which every Sword row does. Quench's "the whole arc is set alight
	 * anew behind the blade" is that ordering rather than a separate mechanic.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	bool bConsumeBurn = false;

	/**
	 * Centimetres the fire spreads from an enemy whose burn was consumed.
	 *
	 * TWO ROWS STATE IT AND THEY USE IT DIFFERENTLY. Touch Off consumes and
	 * spreads three metres in the same use. Ashen Edge consumes nothing at all
	 * -- it is a ten second self buff stating four metres, which is how a row
	 * says "while this is up, what my OTHER skills consume also spreads".
	 * `UCataclysmSkillTemplate::IgniteAroundConsumed` adds the two.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float ConsumeRadiusCm = 0.0f;

	// --- Spreading fire from a target that already carries something --------

	/**
	 * What a target must already carry for the skill to spread fire from it, as
	 * written in the sheet. Only `Burning` today.
	 *
	 * ONE ROW STATES IT: the Wand's Hex of Cinders, "hexing an enemy that is
	 * ALREADY BURNING also sets alight everything within 4 meters of them". The
	 * condition is half the sentence and is the point of the skill -- it makes
	 * Hex of Cinders a follow-up to something else rather than a stand-alone
	 * curse.
	 *
	 * NOT `Requires` ABOVE, WHICH IS A DIFFERENT QUESTION. That one refuses the
	 * whole cast before anything is spent, and asks about the caster's situation.
	 * This one is asked of each TARGET after the skill has already landed, and a
	 * target failing it simply gets no spread while the curse still lands. A
	 * Hex of Cinders cast at an enemy that is not burning is a working cast.
	 *
	 * NOT `ConsumeRadius` ABOVE EITHER, and the difference is what happens to
	 * the fire. Consuming takes the burn OUT of the target and spends it; this
	 * leaves it burning and lights its neighbours as well. Issue #1146 records
	 * the choice between the two.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	FString SpreadWhen;

	/**
	 * Centimetres the fire spreads from a target that met `SpreadWhen`.
	 *
	 * ZERO SPREADS NOTHING, so a row stating the condition and no radius does
	 * nothing rather than spreading everywhere. Hex of Cinders states 4 metres.
	 *
	 * IT SETS ALIGHT WHAT IS INSIDE IT ONCE, at the moment the skill lands, and
	 * leaves nothing behind. That is what "sets alight everything within 4
	 * meters of them" says, and it is why this is not `GroundRadius`: a patch of
	 * burning ground would keep lighting whatever walked in a second later.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float SpreadRadiusCm = 0.0f;

	// --- On death ----------------------------------------------------------

	/** What happens when an affected enemy dies: Leap, SpreadDebuff or Release. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	FString OnDeath;

	/** Centimetres the on-death effect reaches for its next target. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float OnDeathRangeCm = 0.0f;

	// --- Projectile extras -------------------------------------------------

	/** How many times a thrown weapon glances onward. Carom states 3. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	int32 Bounces = 0;

	/** How many nearby enemies the target's debuffs are copied onto. Malefice. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	int32 SpreadCurses = 0;

	/**
	 * Who a repeating projectile picks: All, Nearest or Furthest.
	 *
	 * THE SAME WORD AND THE SAME VALUES as the TargetMode column in the Minion
	 * Types sheet, which is where a turret already says who it shoots.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	FString TargetMode;

	/**
	 * True when attack speed raises the rate rather than the count.
	 *
	 * THE COUNT STAYS FIXED, which is the point. Butcher's Bill throws thirty
	 * axes whatever the character's attack speed; a faster one empties the rack
	 * sooner. Raising the count instead would take the skill out of the
	 * 300-500% Ultimate band the moment the player found attack speed.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	bool bScalesWithAttackSpeed = false;

	/** True when the skill orders every active minion onto its target. Compel. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	bool bCommandStrike = false;

	/** How many enemies a tether binds. Tether states 2. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	int32 TetherTargets = 0;

	/** Centimetres a tether lets those enemies get from one another. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float TetherLengthCm = 0.0f;

	/** Seconds a tether holds. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float TetherDuration = 0.0f;

	// --- Summon extras -----------------------------------------------------

	/**
	 * True when the skill converts an enemy into a permanent minion of the
	 * caster's rather than spawning one. Subjugate is the only skill that does.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	bool bPossess = false;

	/**
	 * Class resource held out of use for as long as what it paid for lives.
	 *
	 * THE ARMY CAP IS THE POOL, NOT A COUNT. Each thrall reserves 30 of the
	 * Ritualist's 150 Fervour, so it holds five, and every point of maximum
	 * Fervour a passive tree grants is progress toward a sixth. A separate
	 * maximum would have made the tree's Fervour nodes worthless to a summoner.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float FervourReserve = 0.0f;

	/** Percent of maximum health the target must be under, checked after the hit. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	float HealthThresholdPercent = 0.0f;

	// --- Other riders ------------------------------------------------------

	/** Which cooldown the skill returns: Self or Movement. Empty returns none. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	FString RefundsCooldown;

	/** True when the caster cannot be hit while the skill runs. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	bool bUntargetable = false;

	/** True when the player fights unarmed until the weapon is retrieved. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	bool bDisarmsUntilRecalled = false;

	/** True when every blow the skill lands counts as struck from behind. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Shape")
	bool bRearHits = false;

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
