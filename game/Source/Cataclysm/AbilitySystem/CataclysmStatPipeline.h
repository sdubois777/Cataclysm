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
	 * AT OR BELOW, NOT BELOW, because the seven nodes that take this predicate
	 * are all written "at or below": a character sitting exactly on 20% health
	 * gets the bonus. `HealthBelowPercent` below is the other side of that
	 * boundary, and until issue #1051 this comment said EVERY node stating a
	 * health threshold was worded this way. That stopped being true when The
	 * Last Drop was written.
	 */
	HealthAtOrBelowPercent	UMETA(DisplayName = "Health At Or Below Percent"),

	/**
	 * The character's health is STRICTLY below `ConditionValue` percent of its
	 * maximum. Issue #1051.
	 *
	 * A SECOND HEALTH THRESHOLD BECAUSE THE TWO DIFFER AT EXACTLY THE
	 * THRESHOLD, and one node needs this side of it: The Final Vow's first
	 * option, The Last Drop, reads "While below 20% health your skills cost no
	 * health, and every skill you cast grants 10 Fervour." It is the only node
	 * in the game that states a health threshold as a STATE and words it
	 * "below".
	 *
	 * NOT DELIVERABLE AS THE PREDICATE ABOVE. A character sitting on exactly 20%
	 * health gets nothing from this node and does get the bonus from every "at
	 * or below 20%" node, which is what the two sentences say.
	 * `SkillHealthCostAbovePercent` exists for the same reason on the other
	 * boundary.
	 *
	 * NOT THE SAME THING AS "DROPPING BELOW". That is an EVENT, which The
	 * Breaking Point and Rock Bottom both state, and an event is not a condition
	 * on a modifier at all: it happens once at a crossing rather than holding
	 * for as long as it is true. `UCataclysmDamageConversion` keeps its own
	 * threshold as a constant and says why.
	 */
	HealthBelowPercent		UMETA(DisplayName = "Health Below Percent"),

	/**
	 * The character's health is STRICTLY above `ConditionValue` percent of its
	 * maximum. Issue #1070.
	 *
	 * THE FIRST HEALTH PREDICATE THAT POINTS UPWARDS. The two above both ask
	 * whether health has fallen far enough. The Second Vow's third option,
	 * Ceaseless Penance, asks the other question: "Debuffs on you no longer
	 * expire while you are above 50% health."
	 *
	 * NOT `HealthAtOrBelowPercent` NEGATED. The two really are complements --
	 * strictly above 50 and at or below 50 cover every character between them --
	 * but a modifier carries one predicate and there is no "not", so a node
	 * wanting the upper side needs an enumerator that says so.
	 *
	 * AN UNKNOWN READING REFUSES, exactly as it does for the two above, and the
	 * guard cannot be folded into the comparison here either: an unknown health
	 * reads -1, which is not above any threshold, so this one would refuse by
	 * accident rather than on purpose. Saying it outright is what keeps the
	 * three predicates reading alike.
	 */
	HealthAbovePercent		UMETA(DisplayName = "Health Above Percent"),

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

	/**
	 * The character took damage of a Cataclysm type other than its own
	 * within the last `ConditionValue` seconds.
	 *
	 * THE SECOND WINDOW THE DESIGN USES, and the reason there is one
	 * enumerator per event rather than a general timer: this one opens on
	 * something entirely different from a health cost. Cataclysmic Resonance
	 * is the node: "+1% increased damage per point for 5 seconds after you
	 * take damage of a Cataclysm type other than Demonic". Issue #975.
	 *
	 * OTHER THAN ITS OWN, NOT LITERALLY OTHER THAN DEMONIC. The two cannot
	 * differ for any character that can take the node -- the Masochist tree
	 * needs a Demonic weapon to grant anything -- and hard-coding one of the
	 * eight type names into an enumerator would be unreusable.
	 * `docs/DECISIONS.md` carries the reading.
	 *
	 * A HIT THE CHARACTER DEALS NEVER OPENS IT. A player's own hit is
	 * untyped in the field this reads, by the design decision of 2026-08-12
	 * that a player's damage arrives untyped because an enemy holds one
	 * generic resistance.
	 */
	WithinSecondsOfForeignDamage
		UMETA(DisplayName = "Within Seconds Of Foreign Damage"),

	/**
	 * The skill dealing this blow cost more than `ConditionValue` percent of
	 * the character's maximum health. Issue #983.
	 *
	 * THE FIRST CONDITION THAT ASKS ABOUT THE SKILL RATHER THAN THE CHARACTER,
	 * and that is what makes it different from the three above. Grand Tithe is
	 * the node: "A skill whose health cost is above 10% of your maximum health
	 * deals 4% increased damage per point." Two skills used one after the other
	 * by the same character at the same instant can answer this differently,
	 * which is true of `RequiredTags` and of nothing else here.
	 *
	 * SO THE READING TRAVELS WITH THE BLOW rather than being built from the
	 * character. `FCataclysmHitDelivery` carries it, the way it already carries
	 * the skill's own critical strike chance and for the same reason.
	 *
	 * STRICTLY ABOVE, NOT AT OR BELOW. The design writes "above 10%", which is
	 * the opposite boundary from every health threshold in the tree, and the
	 * difference is reachable rather than theoretical: Deeper Cuts at its full
	 * ten points adds exactly 10% of maximum health to every skill, so a
	 * character with that and nothing else sits precisely on the number and
	 * correctly gets nothing.
	 *
	 * A SHARE OF MAXIMUM HEALTH, whatever the cost was measured against. A
	 * skill's own cost is a share of CURRENT health and the character's added
	 * cost is a share of MAXIMUM health; `UCataclysmSkillTemplate::PayHealthCost`
	 * sums them and records the total against maximum health, because that is
	 * what the node asks about.
	 */
	SkillHealthCostAbovePercent
		UMETA(DisplayName = "Skill Health Cost Above Percent"),

	/**
	 * The character is Bleeding. Issue #962.
	 *
	 * THE FIRST PREDICATE THAT ASKS WHAT THE CHARACTER IS CARRYING rather than
	 * where a number of its own stands. Thirst for Pain is the node: "While you
	 * are Bleeding, +2% increased Attack Speed per point."
	 *
	 * `ConditionValue` IS UNUSED, AND IT IS THE ONLY PREDICATE HERE THAT NEEDS
	 * NO NUMBER. The other four compare a reading against a threshold; this one
	 * names a kind of effect, and the kind is in the enumerator rather than in a
	 * float. `tools/generate_datatables.py` refuses to write a value on a row
	 * carrying it, so a number typed into that column is caught when the file is
	 * written rather than being quietly ignored here.
	 *
	 * ONE ENUMERATOR PER NAMED EFFECT, rather than one enumerator and a column
	 * saying which. That is what the three stack scales below already do, and
	 * the argument is the same: a further column on the effects sheet is a row
	 * struct change, which means a build before the DataTable asset can be
	 * regenerated. This one could not use a column anyway -- `ConditionValue` is
	 * a float and a tag name is not a number. The design's other sentences of
	 * this shape name Poison, Chill and being Stunned, and each would be its own
	 * enumerator here.
	 *
	 * BLEEDING RATHER THAN "ANY DEBUFF", BECAUSE THE NODE SAYS BLEEDING. A
	 * character that is stunned and not bleeding must not get this bonus.
	 * `ECataclysmStatScale::PerDebuffCarried` below is the separate question of
	 * how many harmful effects of any kind the character is under, and four
	 * other nodes ask that one.
	 */
	WhileBleeding
		UMETA(DisplayName = "While Bleeding"),

	/**
	 * The character's class resource is full. Issue #1026.
	 *
	 * Communion of Pain is the node: "While your Fervour is at maximum you deal
	 * 20% more damage and take 20% more damage."
	 *
	 * `ConditionValue` IS UNUSED, the second predicate here needing no number and
	 * for the reason `WhileBleeding` above needs none: the sentence names a state
	 * rather than a threshold. "At maximum" is the top of whatever pool the class
	 * has, not a figure a designer types. `tools/generate_datatables.py` refuses a
	 * value on a row carrying it, so a number typed into that column is caught
	 * when the file is written rather than being quietly ignored here.
	 *
	 * NOT A THRESHOLD WITH THE VALUE SET TO A HUNDRED, and the difference is
	 * reachable rather than pedantic. A threshold has to be either points or a
	 * percentage of the maximum, and the two disagree: the Ritualist's
	 * `class_resource` is 150 where every other class's is 100. A future node
	 * reading "while above 75 Fervour" is a POINTS threshold and wants its own
	 * enumerator; this one is neither, because it asks about the top of the bar.
	 *
	 * NO CLASS RESOURCE MEANS NO, which is every enemy in the game. A bonus that
	 * asks about a bar the character does not have is correctly worth nothing to
	 * it, and that is the right answer rather than a fault.
	 */
	ClassResourceAtMaximum
		UMETA(DisplayName = "Class Resource At Maximum"),
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

	/**
	 * Multiplied by how many whole `ScaleStep` points of the class resource the
	 * character is currently holding. Issue #980.
	 *
	 * Reciprocity is the node: "Your Retaliation damage is increased by 1% for
	 * each point of Fervour you currently hold." A step of 1, so the count is
	 * the Fervour itself.
	 *
	 * THE CLASS RESOURCE RATHER THAN FERVOUR BY NAME. There is one pool and
	 * every class shares it -- `UCataclysmFervour` records the project owner's
	 * decision of 2026-08-25 and why -- so the attribute is called
	 * `ClassResource` and only the Masochist's name for it is Fervour. An
	 * enumerator naming one class's word for the shared pool would have to be
	 * renamed the first time another tree used it.
	 *
	 * A POINT OF THE RESOURCE IS ALREADY AN ABSOLUTE NUMBER, not a percentage
	 * of the maximum. The pool runs 0 to 100 for every class today, so the two
	 * readings happen to agree, and they would stop agreeing the moment a class
	 * had a different maximum. The design writes "for each point", so the count
	 * is of points.
	 */
	PerPointOfClassResourceHeld
		UMETA(DisplayName = "Per Point Of Class Resource Held"),

	/**
	 * Multiplied by how many whole `ScaleStep` percent of maximum health the
	 * character currently OWES and has not yet paid. Issue #994.
	 *
	 * Compound Interest is the node: "+1% increased damage per point for every
	 * 5% of your maximum health you currently owe." The Reckoning reads the same
	 * state with a step of 2 and a `more` multiplier instead of an increase.
	 *
	 * OWED IS NOT MISSING, and the two are independent readings. A character
	 * that deferred a cost owes health it is still standing on, so at that
	 * instant it is at full health and owes a fifth of it; a character that paid
	 * the same cost outright is a fifth down and owes nothing. Answering either
	 * through the other would hand the bonus to the wrong node.
	 *
	 * THE ATTRIBUTE HOLDS POINTS AND THIS READS PERCENT. `HealthOwed` is stored
	 * in points of health, because that is what is addable when a second cast
	 * defers more; the design asks about it as a share of maximum health, so the
	 * division happens where it is read, exactly as
	 * `PerPercentOfMaximumHealthMissing` divides health by its maximum.
	 */
	PerPercentOfMaximumHealthOwed
		UMETA(DisplayName = "Per Percent Of Maximum Health Owed"),

	/**
	 * Multiplied by how much life leech the character has, in percent.
	 * Issue #1045.
	 *
	 * The Masochist's Glutton capstone option is its only source: "Your
	 * retaliation damage is increased by 1% for every 1% of life leech you
	 * have."
	 *
	 * A READING OF A STAT RATHER THAN OF A STATE, which is what makes it unlike
	 * every scale above it. Those read where the character's health is, what it
	 * owes, how full its pool is, or how many things are on it -- all of which
	 * change from moment to moment. Life leech changes when the character's gear
	 * or passive points change and not otherwise, so this is a bonus that grows
	 * with an investment rather than with a situation.
	 *
	 * IT IS STILL NOT FOLDED INTO AN ATTRIBUTE, and that is the point of it
	 * being a scale at all. `UCataclysmPlayerClassStats::ApplyTo` resolves every
	 * stat with a default `FCataclysmStatConditions`, in which this reading is
	 * unknown, so the bonus is worth nothing there and is worked out wherever
	 * retaliation is asked for.
	 */
	PerPercentOfLifeLeech
		UMETA(DisplayName = "Per Percent Of Life Leech"),

	//~ THREE STACK COUNTS, ONE PER KIND, RATHER THAN ONE ENUMERATOR AND A
	//~ COLUMN NAMING THE KIND. Issues #1002, #1003 and #1004. A fourth column on
	//~ the effects sheet would be a row struct change, which is a build and an
	//~ asset regeneration and a column list to move; three names cost nothing
	//~ but three lines each and follow what the three scales above already do.
	//~ The count of them is expected to stay small: a stack is a mechanic a
	//~ designer writes deliberately, not a stat anyone can add.

	/**
	 * Multiplied by how many Sanguine Momentum stacks the character holds.
	 * Issue #1002.
	 *
	 * "Each health cost paid within 3 seconds of the last grants a stack, up to
	 * 5 stacks. Each stack gives +1% increased attack and cast speed per point."
	 * A step of 1, so the count is the stacks themselves.
	 */
	PerStackOfSanguineMomentum
		UMETA(DisplayName = "Per Stack Of Sanguine Momentum"),

	/**
	 * Multiplied by how many Bloodlust stacks the character holds. Issue #1003.
	 *
	 * "Taking damage grants a stack of Bloodlust for 5 seconds, up to 5 stacks.
	 * Each stack gives +1% increased melee damage per point."
	 */
	PerStackOfBloodlust
		UMETA(DisplayName = "Per Stack Of Bloodlust"),

	/**
	 * Multiplied by how many Carnage stacks the character holds. Issue #1004.
	 *
	 * "Killing an enemy while above 75 Fervour grants a stack of Carnage for 8
	 * seconds, up to 10 stacks. Each stack gives 3% more melee damage." The one
	 * stack scale used in the `more` bucket, so ten stacks is a 1.30x
	 * multiplier rather than thirty points added to the increased sum.
	 */
	PerStackOfCarnage
		UMETA(DisplayName = "Per Stack Of Carnage"),

	/**
	 * Multiplied by how many distinct debuffs the character is carrying.
	 * Issue #962.
	 *
	 * Four Masochist nodes grow with it and all four write it the same way:
	 * "for each unique debuff on you", and once "Every debuff on you". A step of
	 * 1, so the count is the debuffs themselves.
	 *
	 * NOT A FOURTH STACK COUNT, THOUGH IT IS COUNTED THE SAME WAY. A stack is
	 * granted by an event this project chose to remember and expires on a timer
	 * this project chose; a debuff is a gameplay effect somebody applied, and the
	 * ability system is already holding the list for its own reasons.
	 * `UCataclysmDebuffs::CountOn` reads that list and says what counts as one.
	 *
	 * UNIQUE MEANS DISTINCT KINDS. Bleeding and burning at once is two; bleeding
	 * from two sources is one, because every lasting effect this project applies
	 * is aggregated by target and limited to a single stack.
	 */
	PerDebuffCarried
		UMETA(DisplayName = "Per Debuff Carried"),
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

	/**
	 * Seconds since the character took damage of a Cataclysm type other than
	 * its own. Issue #975.
	 *
	 * NEGATIVE MEANS NEITHER KNOWN NOR EVER, the same as the reading above and
	 * for the same reason: both answer no.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SecondsSinceForeignDamage = -1.0f;

	/**
	 * How much of the class resource the character is holding. Issue #980.
	 *
	 * NEGATIVE MEANS UNKNOWN, the same convention as the three readings above.
	 * An ability system with no class resource attribute set -- every enemy in
	 * the game -- leaves it there, and a bonus that grows with the pool is worth
	 * nothing to it. That is the right answer rather than a fault.
	 *
	 * ZERO IS A REAL READING AND IS NOT UNKNOWN. An empty bar is a character
	 * that has generated nothing yet, and a bonus counting points of it is
	 * correctly worth nothing. That is a different statement from "there is no
	 * bar to read", and only the second one has to refuse.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float ClassResourceHeld = -1.0f;

	/**
	 * The largest that pool can be for this character. Issue #1026.
	 *
	 * A SECOND READING RATHER THAN A PERCENTAGE ON THE FIRST, so the two
	 * questions the design asks stay separable. `PerPointOfClassResourceHeld`
	 * counts POINTS -- "for each point of Fervour you currently hold" -- and
	 * `ClassResourceAtMaximum` asks about the TOP OF THE BAR. Storing a
	 * percentage instead would make the scale divide it back out, and storing
	 * only points would leave the condition with nothing to compare against.
	 *
	 * NEGATIVE MEANS UNKNOWN, the same convention as the reading above and set by
	 * the same thing: an ability system with no class resource attribute set,
	 * which is every enemy in the game. `ClassResourceAtMaximum` refuses an
	 * unknown maximum, so an enemy never satisfies it.
	 *
	 * A MAXIMUM OF ZERO REFUSES THE CONDITION TOO, and deliberately. A character
	 * whose pool cannot hold anything is not "at maximum" in any sense a node
	 * means, and answering yes would hand Communion of Pain's bonus to every
	 * class that never generates a point.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float ClassResourceMaximum = -1.0f;

	/**
	 * How much health the character owes, as a percentage of its maximum.
	 * Issue #994.
	 *
	 * NEGATIVE MEANS UNKNOWN, the same convention as the readings above. Three
	 * things leave it there and all three are ordinary rather than faults: an
	 * ability system with no class resource attribute set, which is every enemy;
	 * one with no vital attribute set, so there is no maximum to compare
	 * against; and a maximum health of zero, which would otherwise be a division
	 * by nothing.
	 *
	 * ZERO IS A REAL READING AND IS NOT UNKNOWN, the same distinction the
	 * class resource above draws. A character that owes nothing is correctly
	 * worth nothing to a bonus counting what it owes; that is not the same
	 * statement as "there is nothing to read".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float HealthOwedPercent = -1.0f;

	/**
	 * How much life leech the character has, in percent. Issue #1045.
	 *
	 * NEGATIVE MEANS UNKNOWN, the same convention as the readings above. One
	 * thing leaves it there and it is ordinary rather than a fault: an ability
	 * system with no vital attribute set, which is where life leech lives.
	 *
	 * READ OFF THE ATTRIBUTE RATHER THAN ASKED FOR, which is what every reading
	 * in this struct does and is the reason to say so here. It means a future
	 * node granting life leech under a CONDITION would not be seen by a bonus
	 * scaling with it, because a conditional bonus is never folded into an
	 * attribute. Nothing grants conditional life leech today. Asking for the
	 * stat here instead would run the pipeline inside the function that builds
	 * the input to the pipeline, which is why no reading in this struct does it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float LifeLeechPercent = -1.0f;

	/**
	 * How many stacks of each kind the character is holding. Issues #1002,
	 * #1003 and #1004.
	 *
	 * ZERO IS THE ONLY "NOTHING" THESE NEED, unlike every reading above them.
	 * The others carry a negative "unknown" because a caller with no character
	 * in hand must be told apart from a character whose reading really is zero:
	 * a health percentage of zero is a corpse and an unknown one is the
	 * character sheet, and those must not be treated alike. A stack count has no
	 * such pair. A caller with no character holds no stacks, a character that
	 * has earned none holds no stacks, and a bonus counting them is worth
	 * nothing for both. Nothing can act differently on the two, so there is
	 * nothing to distinguish.
	 *
	 * INTEGERS, BECAUSE A STACK IS A WHOLE THING. The other scales count whole
	 * steps of a continuous reading and round down to get there; these are
	 * already whole and there is nothing to round.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	int32 SanguineMomentumStacks = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	int32 BloodlustStacks = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	int32 CarnageStacks = 0;

	/**
	 * Whether the character is Bleeding, and how many distinct debuffs of any
	 * kind it is carrying. Issue #962.
	 *
	 * FALSE AND ZERO ARE THE ONLY "NOTHING" THESE NEED, exactly as the three
	 * stack counts above. A caller with no character in hand carries no debuffs,
	 * a character nothing has hurt carries no debuffs, and every bonus that
	 * counts them is worth nothing for both. Nothing can act differently on the
	 * two, so there is nothing to distinguish, and the negative "unknown" the
	 * readings further up carry would buy nothing here.
	 *
	 * TWO READINGS RATHER THAN ONE, because they answer different questions and
	 * neither implies the other. A character that is stunned and not bleeding
	 * has one debuff and is not Bleeding; one that is bleeding has one debuff and
	 * is Bleeding. Deriving either from the other would give a node somebody
	 * else's answer.
	 *
	 * AN INTEGER, BECAUSE A DEBUFF IS A WHOLE THING, the same as a stack. The
	 * continuous readings above count whole steps of something and round down to
	 * get there; this is already whole.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	bool bIsBleeding = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	int32 DebuffsCarried = 0;

	/**
	 * What the skill dealing this blow cost, as a percentage of the character's
	 * maximum health. Issue #983.
	 *
	 * NOT A STATE OF THE CHARACTER AT ALL, unlike everything above it, and it
	 * sits here because this is what the pipeline is handed besides the skill's
	 * tags. It is a property of the skill in hand, so two blows an instant apart
	 * from one character can carry different values.
	 *
	 * NEGATIVE MEANS THE SKILL IS NOT KNOWN, which is every caller that has no
	 * blow in hand -- the character sheet, an enemy's plain attack, a burning
	 * patch of ground. Zero is a real reading and means the skill cost nothing,
	 * which is every skill in the game except Blood Pyre for a character without
	 * the Deeper Cuts node. Both answer no to a threshold above zero, so the two
	 * do not have to be told apart by any caller; they are kept distinct because
	 * the distinction is real.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SkillHealthCostPercent = -1.0f;

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
