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
	 * Burning Wrath's "4% more fire damage for every enemy currently
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

	/**
	 * A rule of the dungeon floor the character is standing on. Issue #41.
	 *
	 * Starvation's "each floor the players's maximum hp and energy shield are
	 * reduced by 1%" is one: `UCataclysmDungeonModifierEffects` turns it into a
	 * Less multiplier on the finished maximum, and it is held and dropped at
	 * runtime as the player moves between floors.
	 *
	 * MAY GRANT MORE, AND IN PRACTICE GRANTS LESS. Every row of the dungeon
	 * modifier table is authored rather than rolled, so it sits with the
	 * authored sources above rather than with the affix pool, whose readability
	 * rule is what forbids More.
	 */
	DungeonRule		UMETA(DisplayName = "Dungeon Rule"),
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
	 * The character's health is AT OR ABOVE `ConditionValue` percent of its
	 * maximum. Issues #1653 and #41.
	 *
	 * THE FOURTH HEALTH PREDICATE AND THE SECOND THAT POINTS UPWARDS. It differs
	 * from `HealthAbovePercent` above at exactly the threshold and nowhere else,
	 * which is the same single reading that makes `HealthAtOrBelowPercent` and
	 * `HealthBelowPercent` two predicates rather than one.
	 *
	 * WHAT ASKS FOR IT, and it is a row rather than a hypothetical. The
	 * enchantment "Your ultimate ability cannot be used unless you are below 50%
	 * HP" locks the skill when health is at or above 50. Written with
	 * `HealthAbovePercent` instead, a character sitting on exactly half health
	 * could use a skill the sentence forbids -- so the row would be delivered
	 * differently from how it reads, for one value of health.
	 *
	 * A CHARACTER CAN SIT ON THIS BOUNDARY AND STAY THERE, which is why the
	 * distinction is worth an enumerator. That is not true of every threshold:
	 * `StationaryForSeconds` compares at least against rows that say "more
	 * than", and those differ only at an instant an accumulating clock passes
	 * through. Health is a state a character can hold, so the boundary is
	 * reachable and the two readings really are different rules.
	 *
	 * NOT `HealthBelowPercent` NEGATED, for the reason `HealthAbovePercent`
	 * gives: strictly below 50 and at or above 50 are complements, but a
	 * modifier carries one predicate and there is no "not".
	 *
	 * AN UNKNOWN READING REFUSES, and the guard is written out rather than left
	 * to the comparison -- the same reason `HealthAbovePercent` gives. An unknown
	 * health reads -1, which is not at or above any threshold the validator
	 * allows, so the comparison alone would already answer no. By accident,
	 * though: it depends on that 0-to-100 bound holding. A negative threshold
	 * would make `HealthPercent >= Value` pass for a character sheet with no
	 * character.
	 *
	 * SO THE FOUR SPLIT TWO AND TWO ON THIS, which is worth stating because the
	 * first draft of this comment claimed the opposite. The guard changes an
	 * answer for `HealthAtOrBelowPercent` and `HealthBelowPercent`, where -1 is
	 * below and at-or-below every allowed threshold; it changes no answer for the
	 * two that point upwards. All four write it out anyway, so that none of them
	 * relies on a bound enforced somewhere else.
	 */
	HealthAtOrAbovePercent	UMETA(DisplayName = "Health At Or Above Percent"),

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
	 * The character used a skill carrying `Keyword.Charge` within the last
	 * `ConditionValue` seconds. Issue #1826.
	 *
	 * THE FIRST WINDOW AN ENCHANTMENT OPENS RATHER THAN A PASSIVE NODE, and
	 * the first of three added together for that reason. "After using a charge
	 * skill gain 20%-40% increased attack speed for 4 seconds" is the row.
	 *
	 * ONE ENUMERATOR PER EVENT, which is the rule the two windows above state
	 * and the reason these are three names and not one parameterised one. A
	 * general timer would have to carry which event it means, and the row has
	 * nowhere to put that: `ConditionValue` is already the window's length.
	 *
	 * THE SKILL'S OWN TAGS DECIDE, NOT THE SLOT. Six weapon skills carry
	 * `Keyword.Charge` and all six happen to sit in the movement slot today,
	 * so slot and tag agree by coincidence and would stop agreeing the moment
	 * a charge skill is authored anywhere else. The sentence says "a charge
	 * skill", so the tag is what is read.
	 */
	WithinSecondsOfChargeSkill
		UMETA(DisplayName = "Within Seconds Of A Charge Skill"),

	/**
	 * The character used its basic attack within the last `ConditionValue`
	 * seconds. Issue #1826. "Gain 5%-10% attack speed on basic attack for 4
	 * seconds" is the row.
	 *
	 * THE SLOT DECIDES, NOT A TAG, and it is the one window here that reads
	 * the slot. The basic attack is not a row of `game/Data/WeaponSkills.csv`
	 * at all -- `UCataclysmWeaponSkills::BasicAttackFor` builds it from the
	 * weapon base and sets `ECataclysmAbilitySlot::BasicAttack` on it -- so
	 * there is no authored tag to read and the slot is the only thing that
	 * identifies it.
	 *
	 * IT RE-OPENS ITS OWN WINDOW, WHICH IS THE INTENDED READING. Each swing
	 * stamps the clock afresh, so at any swing rate faster than one per
	 * `ConditionValue` seconds the bonus is continuous while the character
	 * keeps attacking, and it lapses once the character stops. The bonus never
	 * compounds: the row grants what it says once, however often the window is
	 * re-opened. `docs/DECISIONS.md` carries the ruling.
	 */
	WithinSecondsOfBasicAttack
		UMETA(DisplayName = "Within Seconds Of A Basic Attack"),

	/**
	 * The character blocked a blow within the last `ConditionValue` seconds.
	 * Issue #1826. "Blocking an attack grants 10%-20% increased damage for 3
	 * seconds" is the row.
	 *
	 * A BLOW IT TOOK, NOT ONE IT DEALT, which is the same side as
	 * `WithinSecondsOfForeignDamage` and a different question. That one opens
	 * on damage of a Cataclysm type the character does not share, whatever
	 * became of it; this one opens on a block, whatever type it was. A blocked
	 * hit of a foreign type opens both, an unblocked one of a foreign type
	 * opens only that one, and a blocked hit of the character's own type opens
	 * only this one. The three cases are why both exist.
	 *
	 * A BLOCK THAT REDUCED THE BLOW TO NOTHING STILL COUNTS. The sentence says
	 * "blocking an attack" and says nothing about what got through, so the
	 * stamp sits on the block decision rather than on the damage that survived
	 * it.
	 */
	WithinSecondsOfBlock
		UMETA(DisplayName = "Within Seconds Of A Block"),

	/**
	 * The character used a skill carrying `Type.Summon` within the last
	 * `ConditionValue` seconds. Issue #1815.
	 *
	 * `Type.Summon` AND NOT `Keyword.Summon`, which is the ruling #1824
	 * records. Five weapon skills carry the keyword and only two of them
	 * create a creature: Quarry, Compel and Vesselstep command creatures that
	 * already exist. "Summoning a minion" is the two, so the narrower tag is
	 * the right one and the wider one would fire on three skills that summon
	 * nothing.
	 *
	 * A TAG, LIKE THE CHARGE WINDOW AND UNLIKE THE BASIC-ATTACK ONE, and
	 * stamped in the same place.
	 */
	WithinSecondsOfSummon
		UMETA(DisplayName = "Within Seconds Of A Summon"),

	/**
	 * The character evaded a blow within the last `ConditionValue` seconds.
	 * Issue #1815. "When you dodge an attack gain 15%-30% increased damage for
	 * 3 seconds" is the row.
	 *
	 * THE THIRD QUESTION ASKED OF ONE RESOLVED BLOW, beside
	 * `WithinSecondsOfBlock` and `WithinSecondsOfForeignDamage`, and the three
	 * are deliberately independent. A blow is evaded or it is not; blocked or
	 * not; of a foreign Cataclysm type or not. **An evaded blow deals nothing,
	 * so it opens this window and NOT the foreign-damage one**, which is gated
	 * on health or shield actually losing something. That is the case which
	 * separates them.
	 */
	WithinSecondsOfEvade
		UMETA(DisplayName = "Within Seconds Of An Evade"),

	/**
	 * The character was hit within the last `ConditionValue` seconds, whatever
	 * became of the blow. Issue #1815. "Taking a hit reduces your damage by
	 * 5%-10% for 3 seconds" is the row.
	 *
	 * THIS ONE DELIBERATELY OVERLAPS ITS NEIGHBOURS, and it is the only window
	 * here that does. A blocked hit is still a hit and opens both; an evaded
	 * hit is still a hit and opens both; a hit of a foreign type opens this and
	 * the foreign-damage window. So "opens on its own event and nothing else"
	 * is false of it, and a test written to that rule would assert something
	 * untrue. The three-way table in the tests is what states it instead.
	 *
	 * ANY BLOW THAT REACHED THE CHARACTER, INCLUDING ONE THAT DEALT NOTHING.
	 * The sentence says "taking a hit" and says nothing about damage, so this
	 * is stamped beside the block window rather than inside the branch that
	 * asks what got through.
	 */
	WithinSecondsOfHitTaken
		UMETA(DisplayName = "Within Seconds Of A Hit Taken"),

	/**
	 * The character's class resource BECAME full within the last
	 * `ConditionValue` seconds. Issue #1815.
	 *
	 * AN EVENT, AND `ClassResourceAtMaximum` IS THE STATE. The two read the
	 * same pool and answer different questions: that one holds for as long as
	 * the bar is full, this one opens at the moment it fills and then ages
	 * while the bar sits there. A row saying "when your class resource is
	 * full, gain X for 3 seconds" wants this; a row saying "while your class
	 * resource is full" wants that.
	 *
	 * THE STAMP APPLIES `ClassResourceAtMaximum`'S OWN RULE, including its
	 * refusal of a maximum of zero. If it did not, a character with no pool
	 * at all would open this window and the two would disagree about the same
	 * bar.
	 */
	WithinSecondsOfClassResourceFull
		UMETA(DisplayName = "Within Seconds Of The Class Resource Filling"),

	/**
	 * The character's class resource REACHED zero within the last
	 * `ConditionValue` seconds. Issue #1815.
	 *
	 * A CROSSING, AS ABOVE, and only ever the pool: a maximum of zero is a
	 * character with no class resource rather than one that has spent it.
	 */
	WithinSecondsOfClassResourceEmpty
		UMETA(DisplayName = "Within Seconds Of The Class Resource Emptying"),

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

	/**
	 * The blow being taken is a melee attack: struck in melee, and not a spell.
	 * Issue #666. "You take 15%-30% less damage from melee attacks" is a row.
	 *
	 * THE FIRST FOUR PREDICATES THAT ASK ABOUT THE BLOW rather than the
	 * character. They read `FCataclysmStatConditions::Blow`, which only the
	 * damage taken lookup in `UCataclysmDamageCalculation::Resolve` fills in.
	 * Every other caller, the character sheet included, has no blow in hand and
	 * is refused, the same as a character that is not Bleeding is refused
	 * `WhileBleeding`.
	 *
	 * AN ATTACK IS A HIT THAT IS NOT A SPELL, which is where Path of Exile draws
	 * the line: a skill is an attack or a spell, and melee and projectile are
	 * further tags either can carry. No skill in the game is both melee and a
	 * spell today; the rule is here so the three predicates agree on what an
	 * attack is.
	 *
	 * `ConditionValue` IS UNUSED by all four, as it is by `WhileBleeding`, and
	 * `tools/generate_datatables.py` refuses a value on a row carrying one.
	 */
	HitIsMeleeAttack
		UMETA(DisplayName = "Hit Is Melee Attack"),

	/**
	 * The blow being taken is a ranged attack: from range, and not a spell.
	 * Issue #666. "You take 10%-30% more damage from ranged attacks" is a row.
	 *
	 * A PROJECTILE COUNTS AS RANGED. The vocabulary says `Type.Projectile` is
	 * "Skills that fire a traveling entity" and `Type.Ranged` is "Any ranged
	 * skill regardless of delivery method"; `UCataclysmSkillEffects::IsRanged`
	 * accepts either.
	 *
	 * A SPELL THAT FIRES A PROJECTILE IS NOT A RANGED ATTACK, because the row
	 * says "attacks". It meets `HitIsSpell` instead. Two Demonic weapon skills,
	 * the heavy Wand and Staff, are that shape.
	 */
	HitIsRangedAttack
		UMETA(DisplayName = "Hit Is Ranged Attack"),

	/**
	 * The blow being taken is a spell: `Type.Spell` on the skill that threw it.
	 * Issue #666. "You take 20%-40% less damage from spells" is a row.
	 */
	HitIsSpell
		UMETA(DisplayName = "Hit Is Spell"),

	/**
	 * Whoever is on the other side of the blow is a boss. Issue #666.
	 *
	 * "You take 20%-40% less damage from Boss enemies" is a row, so for the
	 * damage taken lookup the other side is the attacker. A boss is what
	 * `ACataclysmEnemyCharacter::IsBoss` says: the Boss and Cataclysm Boss
	 * rarities. A Herald is below that line, the same line the stun rule uses.
	 */
	OpponentIsBoss
		UMETA(DisplayName = "Opponent Is Boss"),

	/**
	 * The character moved in the last sample. Issue #41, slice 2.
	 *
	 * "While moving you deal 15%-30% increased damage" is a row. MOVING MEANS A
	 * CHANGE OF POSITION, whatever caused it: walking, a movement skill that
	 * travels, a charge, a shove or a pull. An instant relocation -- a blink, a
	 * recall, a position swap, a Flicker, the Phasewalker modifier, the placement
	 * of the player at a floor's start -- is not moving and adds no distance.
	 *
	 * `ConditionValue` IS UNUSED, as it is by `WhileBleeding`.
	 */
	WhileMoving
		UMETA(DisplayName = "While Moving"),

	/**
	 * The character did not move in the last sample. Issue #41, slice 2.
	 *
	 * "While stationary you take 15%-30% less damage" is a row.
	 *
	 * NOT THE NEGATION OF `WhileMoving`, because a caller with no character must
	 * be refused by both. The predicate asks for a character first.
	 *
	 * NOT A DELAY EITHER. A row wanting the character to have stood still for a
	 * while wants `StationaryForSeconds` below.
	 *
	 * `ConditionValue` IS UNUSED.
	 */
	WhileStationary
		UMETA(DisplayName = "While Stationary"),

	/**
	 * The character has not moved for AT LEAST THAT LONG. Issue #41, slice 2.
	 *
	 * "After remaining stationary for 3 seconds" and "while you have not moved in
	 * the last 2 seconds" are rows.
	 *
	 * `ConditionValue` IS SECONDS AND THE COMPARISON IS AT LEAST: three seconds
	 * of standing still meets a threshold of three.
	 *
	 * AN INSTANT RELOCATION RESETS THIS CLOCK though it adds no distance. That is
	 * a judgement recorded in `docs/DECISIONS.md`: a teleport is not movement, but
	 * a bonus that builds up while standing still should not survive one.
	 */
	StationaryForSeconds
		UMETA(DisplayName = "Stationary For Seconds"),

	/**
	 * The blow in hand was dealt after the character moved AT LEAST THAT FAR since
	 * its own last attack. Issue #41, slice 2.
	 *
	 * "Your first melee attack after moving 5 metres deals 50% increased damage"
	 * is a row. `ConditionValue` IS METRES and the comparison is at least.
	 *
	 * "FIRST" IS THE RESET RATHER THAN A FLAG, which is why this is a condition
	 * and not an event. The distance is counted since the character's own last
	 * attack and copied onto the blow when the skill is paid for, so the first
	 * attack after moving five metres reads five or more and the next reads about
	 * nothing.
	 *
	 * NEGATIVE MEANS NO BLOW IS IN HAND, the way `SkillHealthCostPercent` does.
	 */
	MetresMovedBeforeAttack
		UMETA(DisplayName = "Metres Moved Before Attack"),

	/**
	 * The character has not attacked for AT LEAST THAT LONG. Issue #41, slice 2.
	 *
	 * "While you have not attacked in the last 3 seconds" is a row.
	 * `ConditionValue` IS SECONDS and the comparison is at least.
	 *
	 * ITS OWN ATTACK, NOT A BLOW IT TOOK, which `WithinSecondsOfForeignDamage`
	 * already covers. Every attack a character makes resets it, creatures and
	 * minions included, so the reading means the same thing on both sides of a
	 * fight. That is a judgement recorded in `docs/DECISIONS.md`.
	 */
	NotAttackedForSeconds
		UMETA(DisplayName = "Not Attacked For Seconds"),

	/**
	 * Whoever is on the other side of the blow stood MORE than `ConditionValue`
	 * metres away when it landed.
	 *
	 * THE FIRST PREDICATE HERE THAT COMPARES A DISTANCE. Standing Apart is the
	 * node: "You take 25% less damage from enemies more than 6 metres away from
	 * you", which is the Ritualist's 100-point capstone third option.
	 *
	 * FOR THE DAMAGE TAKEN LOOKUP, SO THE OTHER SIDE IS THE ATTACKER, exactly as
	 * `OpponentIsBoss` above. A row wanting the reverse -- the ATTACKER asking
	 * how far away its target is -- must use `TargetWithinMetres` below, which
	 * reads a different number carried by a different route. The blow context
	 * reaches only the defender's damage taken lookup, which was established by
	 * counting every call of `StatForSkill`: 33 outside tests, and exactly one
	 * passes a blow. THIS COMMENT SAID THE REVERSE READING DID NOT EXIST AND WAS
	 * ONLY AN ISSUE NUMBER; it was built by
	 * https://github.com/sdubois777/Cataclysm/issues/1596.
	 *
	 * STRICTLY MORE THAN, BECAUSE THE NODE WRITES "more than". A character
	 * standing at exactly 6 metres is not more than 6 metres away, so it takes
	 * full damage. That is the same boundary `SkillHealthCostAbovePercent` and
	 * `HealthAbovePercent` draw and for the same reason.
	 *
	 * AN UNKNOWN DISTANCE REFUSES, which is what -1 means. A damage over time
	 * tick reports -1 deliberately, so this predicate grants nothing for a tick.
	 * `docs/DECISIONS.md` carries that judgement and what it costs the player.
	 */
	OpponentBeyondMetres
		UMETA(DisplayName = "Opponent Beyond Metres"),

	/**
	 * The character being HIT stood at most `ConditionValue` metres away when the
	 * blow was worked out. Issue #1596.
	 *
	 * THE MIRROR OF `OpponentBeyondMetres` ABOVE AND NOT A SPECIAL CASE OF IT.
	 * That one serves the damage taken lookup, where the other side of the blow
	 * is the attacker. This one serves the ATTACKER's own lookups, where the
	 * other side is the target. They read different fields filled by different
	 * routes, so a row cannot quietly get the wrong one: whichever number is not
	 * on the path in hand is -1, and -1 refuses.
	 *
	 * AT OR WITHIN, BECAUSE BOTH ROWS WRITE "within 5 meters". A target standing
	 * at exactly 5 metres IS within 5 metres and earns the bonus. That is the
	 * opposite boundary from `OpponentBeyondMetres`, whose node writes "more
	 * than", and the project keeps `HealthAtOrBelowPercent` and
	 * `HealthBelowPercent` apart for this same reason.
	 *
	 * THE TWO ROWS, AND WHY THIS IS A CONDITION RATHER THAN A TERM ADDED IN CODE.
	 * Brute's Heart's 2-piece bonus is "You gain 25% INCREASED damage against
	 * enemies that are within 5 meters of you" and Demon King's Regalia's is "You
	 * deal 25% MORE damage to enemies that are within 5 meters of you". One is an
	 * increase and the other a multiplier. A value added into the increases sum
	 * could express the first and would silently mis-build the second, which
	 * looks right on a fresh character and wrong on an invested one.
	 *
	 * THE MELEE TAG ON BOTH ROWS IS NOT HONOURED, RULED BY THE PROJECT OWNER ON
	 * 2026-09-12. Any attack type earns the bonus when the target is within 5
	 * metres, so a ranged or spell build has to close the distance. Honouring it
	 * would have made the condition nearly always true: the longest melee weapon
	 * shape reaches 3.3 metres and enemies default to 2. See
	 * https://github.com/sdubois777/Cataclysm/issues/1620 for the scope tags in
	 * general, which nothing enforces.
	 *
	 * AN UNKNOWN DISTANCE REFUSES, which is what -1 means, and zero is a real
	 * distance because two characters can stand on one spot. A MINION'S BLOW
	 * REPORTS -1 DELIBERATELY: a player's conditional damage bonus does not reach
	 * a minion's blow, which is how the genre works and what `docs/DECISIONS.md`
	 * records with its sources.
	 */
	TargetWithinMetres
		UMETA(DisplayName = "Target Within Metres"),

	/**
	 * Whoever is on the other side of the blow is staggered. Issue #45.
	 *
	 * "Staggered enemies deal 15%-30% increased damage to you" is a row, so for
	 * the damage taken lookup the other side is the attacker. Staggered is the
	 * `State.Staggered` tag that a landed knockback, pull or knockdown leaves
	 * for a second, and `UCataclysmSkillEffects::IsStaggered` is what answers.
	 *
	 * NOT LIMITED TO ENEMY CREATURES, unlike `OpponentIsBoss` above, and that is
	 * why the two facts are read in different places. A boss is a creature
	 * rarity, so that one can only come from an enemy creature and is read
	 * inside a cast to that class. The Staggered state lands on anything a
	 * displacement moves, the player included, so reading this one inside that
	 * cast would leave it false for every blow a player throws.
	 */
	OpponentIsStaggered
		UMETA(DisplayName = "Opponent Is Staggered"),

	/**
	 * The character being HIT is staggered. Issue #45.
	 *
	 * "Staggered enemies take 20%-35% increased damage from all sources" is a
	 * row, and it is the mirror of `OpponentIsStaggered` directly above: the
	 * same question about the same state, asked from the other end of the blow.
	 *
	 * THE TWO ARE DELIBERATELY SEPARATE NAMES READING SEPARATE FIELDS, for the
	 * reason `TargetWithinMetres` and `OpponentBeyondMetres` are. The blow record
	 * is filled only on the defender's damage taken lookup and
	 * `bTargetIsStaggered` only on the attacker's own lookups, so a row that used
	 * the wrong one of this pair reads a field nothing filled and grants nothing,
	 * rather than reading the staggered state of the character at the wrong end.
	 *
	 * "FROM ALL SOURCES" IS THE WEARER'S OWN DAMAGE, ACROSS ITS TYPES, and that
	 * is the workbook's reading rather than a choice made in this file. The row
	 * carries `Stat.Offense.Global`, the wearer's offence, as do both other rows
	 * whose words use that phrase about enemies; the rows using it about damage
	 * the wearer takes carry `Stat.Defense.*` instead. A debuff on the enemy that
	 * every attacker's pipeline read would be a different mechanism and is not
	 * this one. `docs/DECISIONS.md` carries the evidence and the rejection.
	 */
	TargetIsStaggered
		UMETA(DisplayName = "Target Is Staggered"),

	/**
	 * At least `ConditionValue` enemies stand within the row's own `ReachMetres`.
	 * Issue #1597.
	 *
	 * ONE NAME FOR BOTH SHAPES THE ROWS USE, because "while an enemy is within 4
	 * metres" is "at least one". Two names would have been two things to hold
	 * equal for no gain. `Ravager_keystone_a_kA` is the row that needs a number
	 * other than one: "You take 15% less damage while THREE OR MORE enemies are
	 * within 4 metres of you."
	 *
	 * THE RADIUS IS ON THE ROW AND NOT HERE, which is why this reads a count and
	 * `ReachMetres` carries the distance. A condition holds one number and this
	 * question asks two. Three radii appear in the authored rows -- 3, 4 and 8
	 * metres -- so a fixed radius would have been convenient now and wrong later.
	 * Path of Exile's developers say the same of their own "nearby": it is
	 * deliberately not one measurement, and four different distances are used.
	 *
	 * COUNTED CENTRE TO CENTRE, the same arithmetic as
	 * `UCataclysmTargeting::MetresBetween`, so a node saying "within 4 metres"
	 * agrees with every other distance the game reports for that pair.
	 * `UCataclysmTargetCandidates::HostileDistancesWithinMetres` records why that
	 * is deliberately NOT the capsule test a creature's target search uses.
	 *
	 * AN EMPTY READING REFUSES. A lookup with no character in hand carries no
	 * distances at all, and a row asking for at least one enemy correctly gets
	 * nothing rather than holding by accident.
	 */
	EnemiesInReachAtLeast
		UMETA(DisplayName = "Enemies In Reach At Least"),

	/**
	 * The character being HIT is carrying Cripple. Issue #1515.
	 *
	 * `Ravager_basic_c_a2` Run Them Ragged is the node: "+2% increased Attack
	 * Damage per point against Crippled enemies."
	 *
	 * THE FIRST CONDITION THAT READS AN AILMENT ON THE OTHER CHARACTER, and it
	 * is built as the staggered pair above is: a fact about the target read
	 * where the blow is struck and carried into the lookup, not a search done
	 * inside `ConditionHolds`. The pipeline is handed facts and judges them; it
	 * has no actor to ask.
	 *
	 * WHAT "CARRYING" MEANS IS THE EXPLICIT TAG AND NOT AN IMPLIED PARENT.
	 * `UCataclysmDebuffs::TagsOnActor` returns what was really applied, one
	 * entry per effect, and `HasTagExact` compares those. Asking `HasTag` would
	 * match a child tag nobody applied the day the branch grew one.
	 *
	 * A CHARACTER SHEET WITH NO TARGET GETS NOTHING, the refusal every blow
	 * predicate makes. `TargetDebuffs` is empty for a caller with no target, so
	 * this answers false rather than holding by accident.
	 */
	TargetCarriesCripple
		UMETA(DisplayName = "Target Carries Cripple"),

	/**
	 * The character being HIT is carrying Cripple AND Weaken at once. Issue
	 * #1515.
	 *
	 * `Ravager_basic_c_c1` Nothing Left In Them is the node: "+2% increased
	 * Attack Damage per point against enemies that are both Crippled and
	 * Weakened."
	 *
	 * ONE NAME AND NOT TWO ROWS, AND THAT IS ARITHMETIC RATHER THAN TASTE.
	 * `UCataclysmStatPipeline::Accumulate` sums increases, so a row for each
	 * ailment would pay when EITHER is present and pay TWICE when both are. The
	 * node's sentence says it pays when both are present and not otherwise, and
	 * only one condition can say that: a modifier carries one condition.
	 *
	 * IT DOES NOT SCALE AND THAT IS DELIBERATE. Twenty-eight debuffs, two ends
	 * of the blow and every combination is not a vocabulary. A name is added
	 * when a node's own sentence names the ailment, never speculatively, and at
	 * a fourth and fifth the right answer is a column naming the ailment on the
	 * row instead. `docs/DECISIONS.md` carries that rule so the person adding
	 * the sixth reads it before this comment.
	 */
	TargetCarriesCrippleAndWeaken
		UMETA(DisplayName = "Target Carries Cripple And Weaken"),

	/**
	 * The character being HIT is carrying a void splinter. Issue #1642.
	 *
	 * The enchantment "Enemies carrying a void splinter take 9%-15% increased
	 * damage from you" is what asks. It is the FOURTH name of this shape, and
	 * the rule above says a fourth is where the decision is revisited rather
	 * than extended. It was: `docs/DECISIONS.md` records the revisit, and the
	 * short reason is that one row asks, the parameterised column's cost now
	 * falls on a different row struct than the analysis assumed, and
	 * `ConditionValue` is a float that cannot carry an ailment's identity.
	 *
	 * THREE NAMES WERE ASKED FOR AND ONE WAS BUILT. The other two, for bleeding
	 * and poisoned targets, were dropped because the two rows that looked like
	 * they needed them turned out to move a number on the ENEMY -- "take
	 * increased damage from all sources", "are slowed" -- rather than on the
	 * attacker, so they need an enemy-side modifier and not a condition on this
	 * character's lookup. Adding them anyway would have been the speculative
	 * addition the rule forbids.
	 *
	 * UNDER `Keyword.DoT` AND NOT `Status.Debuff`, which is the one difference
	 * from the three names above it. A void splinter deals damage over time, so
	 * its tag hangs off the damage over time branch; `DebuffRootNames` names
	 * that branch as a root, so it reaches `TargetDebuffs` exactly as Cripple
	 * does. `UCataclysmDebuffs::VoidSplinterTag` is where the string is written,
	 * once.
	 *
	 * A CHARACTER SHEET WITH NO TARGET GETS NOTHING, the refusal every blow
	 * predicate makes, for the reason `TargetCarriesCripple` gives above.
	 */
	TargetCarriesVoidSplinter
		UMETA(DisplayName = "Target Carries Void Splinter"),

	/**
	 * THIS character's attacks can apply Cripple or Weaken. Issue #1718.
	 *
	 * `Ravager_basic_c_c0` Spreading Hurt is the node: "+4% increased Area of
	 * Effect per point for attacks that Cripple or Weaken."
	 *
	 * ABOUT THE ATTACKER, NOT THE TARGET, unlike the four names above it. It is
	 * grouped with them because it names the same two ailments, and it is read
	 * from a different place for the reason the node's own wording forces: an
	 * area of effect shapes an attack BEFORE it lands, so "an attack that
	 * applied a Cripple" is not knowable when the bonus is worked out. The
	 * question that is knowable is whether this character's attacks are ones
	 * that cripple or weaken, and `cripple_chance` and `weaken_chance` say so.
	 *
	 * THE NODE SITS DIRECTLY BELOW THE CHANCE NODES ON THE TREE, which is what
	 * makes this reading the designed one rather than a convenient one. A
	 * Ravager reaches Spreading Hurt through `Hamstring` and `Take the Edge
	 * Off`, so the condition is a statement about a build that has invested in
	 * those, and it is false for one that has not.
	 *
	 * EITHER CHANCE, NOT BOTH, because the node says "Cripple or Weaken". The
	 * conjunction that pays only on both is a different question and
	 * `TargetCarriesCrippleAndWeaken` above is what asks it.
	 *
	 * ZERO IS NOT A CHANCE. A character with neither chance gets nothing, which
	 * is the ordinary answer for every class but an invested Ravager and for
	 * every enemy in the game.
	 */
	CanCrippleOrWeaken
		UMETA(DisplayName = "Can Cripple Or Weaken"),

	/**
	 * Whoever threw the blow is carrying Weaken. Issue #1515.
	 *
	 * THE MIRROR OF `TargetCarriesCripple` ABOVE, READING A DIFFERENT FIELD, and
	 * that is the whole safeguard -- the same one `OpponentIsStaggered` and
	 * `TargetIsStaggered` rely on. The blow record is filled only on the
	 * defender's damage taken lookup and `TargetDebuffs` only on the attacker's
	 * own lookups, so a row carrying the wrong one of this pair reads a field
	 * nothing filled and grants nothing, rather than reading the ailments of the
	 * character at the other end of the blow.
	 *
	 * "AGAINST ENEMIES YOU HAVE WEAKENED" IS READ AS "AN ENEMY CARRYING WEAKEN".
	 * `Ravager_basic_c_b2` Wearing Them Down words it the first way, and nothing
	 * in the game records who applied a debuff: a tag has no applier, and
	 * `UCataclysmAilments` reads the instigator at the moment of application and
	 * passes it on rather than storing it. The bonus already shipped for this
	 * idea does not ask either -- `UCataclysmDebuffs::DamageAgainstSharedDebuff`,
	 * "enemies carrying a debuff you also carry", compares two tag lists.
	 * `docs/DECISIONS.md` carries the ruling and says the stricter reading
	 * remains available and would need its own mechanism.
	 *
	 * NO ROW USES THIS YET, AND THAT IS DELIBERATE RATHER THAN AN OVERSIGHT.
	 * Wearing Them Down grants increased DAMAGE REDUCTION, and
	 * `UCataclysmDamageCalculation::Resolve` does not hand the blow to its
	 * damage reduction lookup -- only to the damage taken one. Which of the two
	 * stats that row should use is with the project owner on issue #1748, and
	 * the answer decides whether a line in `Resolve` changes. The condition is
	 * correct for either answer, which is why it is here now.
	 */
	OpponentCarriesWeaken
		UMETA(DisplayName = "Opponent Carries Weaken"),

	/**
	 * The character being HIT is below `ConditionValue` per cent of its maximum
	 * health. Issue #1515.
	 *
	 * Two nodes:
	 *
	 *   Ravager_basic_d_a2     Cornered Quarry   below 35% health
	 *   Ritualist_basic_a_stem1 Broken Will      below half health
	 *
	 * STRICTLY BELOW, AND THERE IS NO INCLUSIVE TWIN. Both node sentences say
	 * "below" and neither says "at or below", so one name is what the data
	 * needs. The character's own pair -- `HealthBelowPercent` and
	 * `HealthAtOrBelowPercent` -- exists because real nodes differ, and the
	 * second name here should be added when a node's sentence asks for it and
	 * not before.
	 *
	 * A SHARE OF MAXIMUM HEALTH, which is this project's reading in two other
	 * places. `UCataclysmSkillEffects::ApplyStagger` compares
	 * `Health / MaxHealth * 100` against the ceiling that "You cannot stagger
	 * enemies above 50% HP" sets, and the Staff's Subjugate says why a share and
	 * not an amount: "what matters is whether the blow was a real blow for that
	 * creature, and a Common enemy and a Rare one do not have the same numbers."
	 * `FCataclysmStatConditions::FromHealth` is the arithmetic, shared with the
	 * character's own reading so the two ends of a blow cannot compute a share
	 * differently.
	 *
	 * MEASURED BEFORE THE BLOW LANDS, AND SUBJUGATE MEASURES AFTER. That
	 * difference is deliberate and is written here because nothing in the code
	 * enforces it. Subjugate takes an enemy "if the blow leaves it below half
	 * health", and `CataclysmSkillTemplates.cpp` states the rule for it: "the
	 * health that matters is what is left when the damage has landed, not what
	 * it had when the skill was pressed." **This condition must read the other
	 * one**, because it increases the damage of the blow being priced and
	 * reading the result of that blow would be circular. It comes out right
	 * today purely because the stat lookup runs before the damage is applied --
	 * so it is true by an ordering that carries meaning and says nothing about
	 * itself, which is what a later tidy-up breaks without noticing.
	 *
	 * AN UNREADABLE TARGET EARNS NOTHING, AND THE STAGGER CEILING DOES THE
	 * OPPOSITE ON PURPOSE. That code leaves a target whose health cannot be read
	 * staggerable, "because refusing on an unknown would make the row stronger
	 * than it says." **Same rule, opposite direction, decided by which way the
	 * row points**: that one is a drawback, so refusing would widen it; these are
	 * bonuses, so granting would widen them. `TargetHealthPercent` is negative
	 * when nothing was read, and this refuses on it.
	 */
	TargetHealthBelowPercent
		UMETA(DisplayName = "Target Health Below Percent"),

	/**
	 * The character's energy shield is at the top of its bar. Issue #1515.
	 * Cold Reading is the node: "+2% increased Spell Damage per point while your
	 * Energy Shield is full."
	 *
	 * THE SECOND POOL TO ASK THIS, AND IT COPIES `ClassResourceAtMaximum`'S
	 * SHAPE DELIBERATELY. Three clauses, in the same order, for the same three
	 * reasons: an unknown reading refuses, a maximum of nothing refuses, and
	 * anything at or above the top holds.
	 *
	 * NO THRESHOLD, SO `Value` IS NOT READ. "Full" names the top of the bar
	 * rather than a number, and `tools/generate_datatables.py` refuses a value
	 * on a row carrying it.
	 *
	 * THE REASON IS NOT THE ONE THE CLASS RESOURCE GIVES, and copying that one
	 * would have been wrong. Its argument is that classes disagree with each
	 * other -- the Ritualist's `class_resource` is 150 where every other
	 * class's is 100 -- but the Ritualist is the ONLY class with an energy
	 * shield, so there is no disagreement between classes to point at. The
	 * conclusion survives on a different fact: `max_energy_shield` is 40 with 8
	 * added per level, so the top of the bar moves as a character grows. A
	 * points threshold of 40 would be exactly full at level one and about 36%
	 * of the bar at level ten. Points and percentage still disagree; they
	 * disagree over one character's lifetime rather than between two classes.
	 *
	 * THOSE FIGURES SAID 48 AND 40% AND WERE WRONG, BECAUSE THEY USED THE
	 * MINION FORMULA FOR A CLASS STAT. `UCataclysmClassStats::BaseFor` computes
	 * `Base + PerLevel * (Level - 1)` and says so -- "the per-level gain applies
	 * to levels ABOVE the first, so a level 1 character has exactly the base" --
	 * while `RaisedByLevel` in `CataclysmMinion.cpp` computes
	 * `Base + PerLevel * Level` and carries its own comment warning that the two
	 * readings differ. So the bar is 40 at level one and 112 at level ten, not
	 * 48 and 120. **48 is the level TWO maximum**, which is why the wrong figure
	 * looked reasonable.
	 * A future "while your Energy Shield is above 75%" is a threshold and wants
	 * its own enumerator, exactly as a future "while above 75 Fervour" does.
	 *
	 * A MAXIMUM OF NOTHING REFUSES, the same judgement and for the same stated
	 * reason as the class resource: a bar that cannot hold anything is not at
	 * its maximum in any sense a node means. It is not a hypothetical case
	 * here. `game/Data/ClassStats.csv` gives `max_energy_shield` to the
	 * Ritualist alone, so every other class, every enemy and every test
	 * character built without one sits at a maximum of zero, and each of them
	 * would otherwise satisfy a node written for a full shield.
	 *
	 * AN UNKNOWN READING REFUSES BECAUSE THIS IS A BONUS. An unknown reading
	 * must never make a row stronger than its own sentence; a drawback may
	 * hold on unknown and this may not. See `TargetHealthBelowPercent` for the
	 * same rule stated against the stagger ceiling, which points the other way.
	 */
	EnergyShieldAtMaximum
		UMETA(DisplayName = "Energy Shield At Maximum"),
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
	 * Seven Masochist nodes grow with it and they write it the same way: "for
	 * each unique debuff on you", and once "Every debuff on you". A step of 1,
	 * so the count is the debuffs themselves. Counted from
	 * `game/Data/PassiveEffects.csv` on 2026-09-04, where seven distinct nodes
	 * carry `debuffs_carried` in their Scale column. This said four and issue
	 * #1145 said eleven; neither matched the sheet.
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

	/**
	 * Multiplied by how many minions the character is commanding. Issue #1518.
	 *
	 * THE RITUALIST'S GENERATOR IS THE FIRST USE: "1 per second for each minion
	 * you have". A step of 1, so the count is the minions themselves.
	 *
	 * IMPS AND THRALLS TOGETHER, WHICH IS WHAT "MINION" MEANS HERE. The
	 * decision of 2026-09-08 settled the word: an imp is summoned and temporary,
	 * a thrall is possessed and permanent, and the tree says "minion" wherever
	 * it means both. `UCataclysmCommand::ThingsCommandedBy` is that list, and it
	 * is deliberately NOT `ThrallCountOf`, which counts only the taken ones.
	 *
	 * COUNTED THE SAME WAY AS A DEBUFF AND A STACK, because a minion is a whole
	 * thing: there is nothing to divide and nothing to round.
	 *
	 * IT READS ZERO WHEN THE CHARACTER COMMANDS NOTHING, which is what makes a
	 * Ritualist holding no minions gain nothing rather than gain the row's bare
	 * value. That is the case a build that forgets the count gets wrong.
	 */
	PerMinionHeld
		UMETA(DisplayName = "Per Minion Held"),

	/**
	 * Per enemy standing within the row's own `ReachMetres`. Issue #1597.
	 *
	 * THE THIRD SCALE THAT IS A COUNT OF THINGS, after `PerDebuffCarried` and
	 * `PerMinionHeld`, and built the same way for the same reason. The entry of
	 * 2026-09-09 in `docs/DECISIONS.md` records that the Ritualist's generator
	 * "needed no new mechanism, only a new count"; this is that sentence again
	 * with a different count.
	 *
	 * NOT CAPPED, AND THE PROJECT OWNER RULED SO ON 2026-09-12 having been asked.
	 * `Ravager_keystone_d_kB` is "You deal 2% more damage for each enemy within 4
	 * metres of you", and a Horde wave has been measured at 125 to 174 creatures
	 * at once, so the count is unbounded in the mode the owner plays. It is not a
	 * new exception: `docs/Cataclysm_GDD_v2.md` already says "Multiplicative
	 * sources are not capped because they cannot reach immunity... there is no
	 * bound on how many may combine." The decisions entry carries the ruling, the
	 * quotation, and that a cap was considered and declined.
	 *
	 * IT READS ZERO WITH NOBODY IN REACH, which is what makes a character alone
	 * gain nothing rather than the row's bare value -- the case a build that
	 * forgets the count gets wrong, exactly as `PerMinionHeld` records.
	 */
	PerEnemyInReach
		UMETA(DisplayName = "Per Enemy In Reach"),
};

/**
 * What one blow is, for a condition that asks about the blow being taken rather
 * than the character taking it. Issue #666.
 *
 * PASSED IN RATHER THAN READ. Everything else in `FCataclysmStatConditions` is a
 * property of the character and is read off it; this is a property of the one
 * hit landing, so two hits an instant apart answer it differently.
 * `UCataclysmDamageCalculation::Resolve` builds one from the hit it is resolving
 * and hands it to the damage taken lookup, the only caller with a hit in hand.
 *
 * FALSE IS THE ONLY "NOTHING" THESE NEED, the argument `bIsBleeding` makes. A
 * caller with no blow and a blow from no such source both answer no, and a
 * bonus for being hit by a spell is correctly withheld from both.
 *
 * THREE FACTS RATHER THAN ONE KIND OF DELIVERY, because a hit can be two of them
 * at once: a spell that fires a projectile is a spell and is ranged. The
 * predicates, not this struct, decide what "an attack" is.
 *
 * AND ONE READING THAT IS A NUMBER RATHER THAN A FACT, added when the passive
 * trees needed it: how far apart the two characters stood. It uses -1 for
 * unknown, the way `SkillHealthCostPercent` does and for the same reason -- zero
 * is a real reading, because two characters can stand on the same spot.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmBlowContext
{
	GENERATED_BODY()

	/** Struck in melee: `Type.Melee` on the damage effect. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	bool bIsMelee = false;

	/** From range: `Type.Ranged` on the damage effect, which a projectile sets. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	bool bIsRanged = false;

	/** A spell: `Type.Spell` on the damage effect. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	bool bIsSpell = false;

	/** The character on the other side of the blow is a boss. See `IsBoss`. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	bool bOpponentIsBoss = false;

	/**
	 * Metres from the character that dealt this blow to the one taking it, or
	 * -1 when it is not known.
	 *
	 * THE ONE FIELD HERE THAT A PREDICATE COMPARES A NUMBER AGAINST. The four
	 * above are facts and answer no when unknown; this answers "refuse" when
	 * unknown, which is the same direction by a different route.
	 *
	 * A DAMAGE OVER TIME TICK REPORTS -1 AND IT IS A CHOICE, not a limitation.
	 * `docs/DECISIONS.md` carries the judgement and its consequence. The short
	 * version: the creature that applied the effect usually still has a position
	 * when the tick fires, and `UCataclysmCombatEvents` reports a real distance
	 * for ticks today, but that creature may have walked away or died, so the
	 * number would describe something that is not striking.
	 *
	 * MEASURED BY `UCataclysmTargeting::MetresBetween`, which is also what the
	 * blow announcement uses. One definition, so a passive row and the combat
	 * log cannot disagree about one strike.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float OpponentDistanceMetres = -1.0f;

	/**
	 * The character on the other side of the blow is staggered. Issue #45.
	 * See `UCataclysmSkillEffects::IsStaggered`.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	bool bOpponentIsStaggered = false;

	/**
	 * The debuffs the character on the other side of the blow is carrying, as
	 * the explicit tags `UCataclysmDebuffs::TagsOnActor` returns. Issue #1515.
	 *
	 * A CONTAINER RATHER THAN A BOOLEAN PER AILMENT, and the reason is the
	 * signature chain rather than this struct. A fact about the target has to be
	 * read where the blow is struck and threaded through every lookup between
	 * there and `ConditionHolds`; `bOpponentIsStaggered` above costs one
	 * parameter on each of seven of them. Twenty-eight debuffs cannot each cost
	 * that, so the tags travel once and a condition names the one it wants.
	 *
	 * EMPTY IS "NO BLOW, OR NOTHING CARRIED", AND BOTH CORRECTLY REFUSE. A
	 * character sheet built with no blow leaves it empty, so a bonus against a
	 * Weakened attacker is withheld from it, which is the argument every other
	 * field here makes.
	 *
	 * A DAMAGE OVER TIME TICK CARRIES NONE OF THEM. `BlowContextFor` returns a
	 * default-constructed context for a tick before reading any field, so this
	 * is empty there with no special case, the same as every fact beside it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	FGameplayTagContainer OpponentDebuffs;
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
	 * Seconds since the character last used a skill carrying `Keyword.Charge`.
	 * Issue #1826.
	 *
	 * NEGATIVE MEANS NEITHER KNOWN NOR EVER, the same convention as the two
	 * readings above and for the same reason: both answer no.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SecondsSinceChargeSkill = -1.0f;

	/**
	 * Seconds since the character last used its basic attack. Issue #1826.
	 *
	 * NEGATIVE MEANS NEITHER KNOWN NOR EVER, as above.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SecondsSinceBasicAttack = -1.0f;

	/**
	 * Seconds since the character last blocked a blow. Issue #1826.
	 *
	 * NEGATIVE MEANS NEITHER KNOWN NOR EVER, as above.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SecondsSinceBlock = -1.0f;

	/**
	 * Seconds since the character last used a skill carrying `Type.Summon`.
	 * Issue #1815. Negative means neither known nor ever, as above.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SecondsSinceSummon = -1.0f;

	/**
	 * Seconds since the character last evaded a blow. Issue #1815. Negative
	 * means neither known nor ever, as above.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SecondsSinceEvade = -1.0f;

	/**
	 * Seconds since the character was last hit, whatever became of the blow.
	 * Issue #1815. Negative means neither known nor ever, as above.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SecondsSinceHitTaken = -1.0f;

	/**
	 * Seconds since the character's class resource became full. Issue #1815.
	 * Negative means neither known nor ever, as above.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SecondsSinceClassResourceFull = -1.0f;

	/**
	 * Seconds since the character's class resource reached zero. Issue #1815.
	 * Negative means neither known nor ever, as above.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SecondsSinceClassResourceEmpty = -1.0f;

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
	 * Whether this character's attacks can apply Cripple or Weaken at all, which
	 * is to say whether either chance is above zero. Issue #1718.
	 *
	 * `Ravager_basic_c_c0` Spreading Hurt is the node: "+4% increased Area of
	 * Effect per point for attacks that Cripple or Weaken."
	 *
	 * A FACT ABOUT THE ATTACKER AND NOT ABOUT THE BLOW, which is what the node's
	 * wording forces. An area of effect is used to SHAPE an attack before it
	 * lands, so "attacks that applied one" cannot be known when this is read.
	 * What can be known is whether this character's attacks are ones that
	 * cripple or weaken, and the two chance stats are what say so.
	 *
	 * FALSE IS THE ONLY "NOTHING" THIS NEEDS, the argument `bIsBleeding` above
	 * makes. A character with no combat attribute set and one whose chances are
	 * both zero are alike here: neither can apply either ailment, and a bonus
	 * for attacks that do is correctly worth nothing to both.
	 *
	 * READ OFF THE ATTRIBUTES RATHER THAN RESOLVED AGAIN, and that is a real
	 * limit worth stating. This is built while the pipeline is being run, so
	 * asking the pipeline for the two chances here would re-enter it. The
	 * attribute holds the chance worked out with no skill in hand, so a chance
	 * that exists only on a row scoped by a required tag is not in it. Every
	 * authored chance row today is unscoped -- the four Ravager nodes granting
	 * one carry no required tags -- so the reading is complete for the rows that
	 * exist, and a future scoped row would narrow it rather than break it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	bool bCanCrippleOrWeaken = false;

	/**
	 * How many minions the character is commanding right now. Issue #1518.
	 *
	 * IMPS AND THRALLS TOGETHER. `UCataclysmCommand::ThingsCommandedBy` is the
	 * list and it holds both, which is what the Ritualist tree means by the
	 * word: the decision of 2026-09-08 kept "imp" and "thrall" only where a node
	 * means one of the two and says "minion" everywhere it means both.
	 *
	 * ZERO IS THE ANSWER FOR EVERY CHARACTER THAT COMMANDS NOTHING, which is
	 * every character in the game but a Ritualist that has summoned or
	 * subjugated something. It is a count rather than a measurement, so its
	 * default is 0 and not the -1 the readings above use to mean "not asked".
	 */
	int32 MinionsHeld = 0;

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

	/**
	 * What the blow being taken is. Issue #666.
	 *
	 * THE SECOND READING HERE THAT IS NOT A STATE OF THE CHARACTER, after the
	 * skill's cost above, and passed in for the same reason. Only the damage
	 * taken lookup in `UCataclysmDamageCalculation::Resolve` has a hit in hand;
	 * every other caller leaves every fact false, and the four predicates that
	 * read it refuse. See `FCataclysmBlowContext`.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	FCataclysmBlowContext Blow;

	/**
	 * Whether the character moved in the last sample. Issue #41, slice 2.
	 *
	 * FALSE IS THE ONLY "NOTHING" THIS NEEDS, the argument `bIsBleeding` makes: a
	 * caller with no character and a character standing still both answer no, and
	 * a bonus for moving is correctly withheld from both. `WhileStationary` cannot
	 * be this field negated, for that reason, and asks the clock below for a
	 * character first.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	bool bIsMoving = false;

	/**
	 * Seconds since the character last moved, or zero while it is moving.
	 * Issue #41, slice 2.
	 *
	 * NEGATIVE MEANS THERE IS NO CHARACTER TO READ, which is the character sheet
	 * and a test passing plain numbers. A character's clock starts when it spawns,
	 * so one that has never moved reads the seconds since it spawned rather than
	 * -1, and "stationary for 3 seconds" is true of it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SecondsSinceMoved = -1.0f;

	/**
	 * Seconds since the character last attacked. Issue #41, slice 2.
	 *
	 * ITS OWN ATTACK, not a blow it took. Negative means there is no character to
	 * read, and a character that has not attacked since it spawned reads the
	 * seconds since then.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float SecondsSinceOwnAttack = -1.0f;

	/**
	 * How far the character moved before the blow in hand, in metres.
	 * Issue #41, slice 2.
	 *
	 * THE THIRD READING HERE THAT IS NOT A STATE OF THE CHARACTER, after the
	 * skill's cost and the blow's facts, and passed in for the same reason: it is
	 * measured when the skill is paid for and carried on the blow, so two blows an
	 * instant apart from one character can hold different values.
	 *
	 * NEGATIVE MEANS NO BLOW IS IN HAND. Zero is a real reading and means the
	 * character had not moved since its own last attack.
	 *
	 * NOT EITHER DISTANCE BETWEEN THE TWO CHARACTERS. There are now three
	 * distance-shaped readings in this struct and they are easy to confuse, so:
	 *
	 *   MetresMovedBeforeBlow       how far the ATTACKER travelled, this field
	 *   Blow.OpponentDistanceMetres how far away the ATTACKER stood, filled only
	 *                               on the defender's damage taken lookup
	 *   TargetDistanceMetres        how far away the TARGET stood, filled only on
	 *                               the attacker's own lookups
	 *
	 * The second and third are the same measurement read from opposite ends, and
	 * whichever one is not on the path in hand is -1, which refuses.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float MetresMovedBeforeBlow = -1.0f;

	/**
	 * How far away the character being HIT stood, in metres, when the blow was
	 * worked out. Issue #1596.
	 *
	 * FILLED ONLY ON THE ATTACKER'S OWN LOOKUPS -- its attack damage, both
	 * buckets, and its spell damage -- because those are the ones with a target
	 * in hand. `UCataclysmSkillEffects::ApplyHit` measures it once with
	 * `UCataclysmTargeting::MetresBetween`, which is the one definition of the
	 * distance between two actors in the tree, and passes it to all three.
	 *
	 * NEGATIVE MEANS NOT KNOWN, and zero is a real distance, because two
	 * characters can stand on one spot. So the guard cannot be folded into the
	 * comparison: `>= 0` first, then the threshold.
	 *
	 * A MINION'S BLOW REPORTS -1 DELIBERATELY. `ACataclysmMinion` strikes with the
	 * summoner as the attacker, so a number measured here would be the summoner's
	 * distance to the minion's target. That number is defensible by the row's own
	 * sentence, and it is refused anyway: a player's conditional damage bonus does
	 * not reach a minion's blow. `FCataclysmHitDelivery::bCarriesNoTargetDistance`
	 * says so at the call site, beside the five exclusions already there.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float TargetDistanceMetres = -1.0f;

	/**
	 * Whether the character being HIT is staggered. Issue #45.
	 *
	 * THE SECOND READING OF THE TARGET FILLED ONLY ON THE ATTACKER'S OWN LOOKUPS,
	 * beside `TargetDistanceMetres` above and for the same reasons. It is the
	 * mirror of `Blow.bOpponentIsStaggered`, which is the same state read from the
	 * other end and filled only on the defender's damage taken lookup.
	 *
	 * FALSE IS BOTH "NOT STAGGERED" AND "NO TARGET IN HAND", and unlike the
	 * distance beside it there is no third value to tell those apart. That costs
	 * nothing, because both answers refuse: a lookup with no target must not grant
	 * a bonus conditioned on one. The distance needs its -1 only because zero is a
	 * real distance, and a bool has no such collision.
	 *
	 * A MINION'S BLOW REPORTS FALSE DELIBERATELY, exactly as the distance reports
	 * -1. `ACataclysmMinion` strikes with the summoner as the attacker, so every
	 * attacker-side reading reaches it unless it is stopped, and a player's
	 * conditional damage bonus should not reach a minion's blow at all.
	 * `FCataclysmHitDelivery::bCarriesNoTargetState` says so at the call site.
	 * Note that the ground is different from the distance's: the staggered state
	 * of the minion's target is the RIGHT reading, not a wrong one, and it is
	 * refused anyway because of whose bonus it is.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	bool bTargetIsStaggered = false;

	/**
	 * How far away each hostile character near this one is, in metres. Empty
	 * means either that nobody is near or that this lookup never asked. Issue
	 * #1597.
	 *
	 * DISTANCES RATHER THAN A COUNT, because the radius belongs to the ROW and
	 * one state serves every row in a stat's list. A character carrying rows at
	 * three and at four metres needs one walk, not two, and each row counts the
	 * entries inside its own `ReachMetres`.
	 *
	 * FILLED ONLY WHEN A ROW ACTUALLY ASKS. `UCataclysmAbilitySystemComponent`
	 * looks at the modifiers first and leaves this empty unless one of them uses
	 * `EnemiesInReachAtLeast` or `PerEnemyInReach`, so a stat lookup that does not
	 * count enemies walks nothing. That matters because a stat is worked out many
	 * times a second: the project owner's Horde capture of 2026-09-10 measured the
	 * physics sphere query this deliberately avoids at 809 to 855 milliseconds of
	 * EVERY SECOND of game time, holding the frame rate at 2.5 frames a second.
	 *
	 * EMPTY AND "NOBODY IS NEAR" ARE THE SAME ANSWER HERE, and that is safe
	 * because both mean every row reading it grants nothing. A row asking for at
	 * least one enemy refuses, and a row scaling per enemy scales by zero.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	TArray<float> HostileDistancesMetres;

	/**
	 * The debuffs the character being HIT is carrying, as the explicit tags
	 * `UCataclysmDebuffs::TagsOnActor` returns. Issue #1515.
	 *
	 * THE MIRROR OF `FCataclysmBlowContext::OpponentDebuffs`, READING THE OTHER
	 * END OF THE BLOW, and the pair is separate for the reason
	 * `bTargetIsStaggered` and `bOpponentIsStaggered` are: the blow record is
	 * filled only on the defender's damage taken lookup and this only on the
	 * attacker's own lookups, so a row carrying the wrong condition of a pair
	 * reads a field nothing filled and grants nothing.
	 *
	 * FILLED ONLY WHEN A ROW ACTUALLY ASKS, exactly as
	 * `HostileDistancesMetres` above is, and for a cost this project has already
	 * named. `UCataclysmDebuffs::DamageAgainstSharedDebuff` reads its stat before
	 * comparing anything and says why: "asking the other way round would walk two
	 * tag containers on every blow anybody strikes." A walk here would be a third,
	 * on every blow every creature throws. `UCataclysmAbilitySystemComponent::
	 * WithTargetState` is the gate, and its whole cost to a lookup that asks
	 * about no ailment is one pass over that stat's own modifier list.
	 *
	 * EMPTY IS "NO TARGET, NOTHING CARRIED, OR NO ROW ASKED", AND ALL THREE
	 * CORRECTLY REFUSE. The third is not a hidden failure: a lookup with no row
	 * asking has no condition to answer, so there is nothing for the empty
	 * container to be wrong about.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	FGameplayTagContainer TargetDebuffs;

	/**
	 * How much health the character being HIT has left, as a percentage of its
	 * own maximum. Negative means nothing was read. Issue #1515.
	 *
	 * NEGATIVE FOR "NOT READ", the same convention `HealthPercent` above uses and
	 * for the same reason: zero is a real reading -- a target on no health at all
	 * -- so "no target" and "dying" would otherwise be the same number.
	 *
	 * FILLED BESIDE `TargetDebuffs` AND GATED THE SAME WAY.
	 * `UCataclysmAbilitySystemComponent::WithTargetState` looks at the rows
	 * first, and one pass over them answers both questions.
	 *
	 * THREE THINGS MAKE IT NEGATIVE AND ALL THREE CORRECTLY REFUSE: no target in
	 * hand, a target whose health cannot be read, and no row in this lookup
	 * asking about it. The second is the interesting one -- see
	 * `ECataclysmStatCondition::TargetHealthBelowPercent` for why an unknown
	 * refuses here while the stagger ceiling deliberately does not.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float TargetHealthPercent = -1.0f;

	/**
	 * How much energy shield the character is carrying. Issue #1515.
	 *
	 * NEGATIVE MEANS UNKNOWN, the same convention as the class resource pair
	 * above and set the same way: an ability system with no vital attribute set
	 * never runs the fill, so the default stands.
	 *
	 * ZERO IS A REAL READING AND IS NOT UNKNOWN. A shield that has been broken
	 * is a character at zero of a real maximum, which is the ordinary case this
	 * condition has to answer "no" for. "There is no shield" is a different
	 * statement, carried by the maximum below, and the two are told apart on
	 * purpose.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float EnergyShieldHeld = -1.0f;

	/**
	 * The largest that shield can be for this character. Issue #1515.
	 *
	 * A SECOND READING RATHER THAN A PERCENTAGE ON THE FIRST, for the reason
	 * `ClassResourceMaximum` gives: "full" asks about the top of the bar, and a
	 * percentage would have to be divided back out to answer it.
	 *
	 * READ BESIDE THE SHIELD AND NOT DERIVED FROM IT, so the two cannot be a
	 * frame apart.
	 *
	 * A MAXIMUM OF ZERO REFUSES `EnergyShieldAtMaximum`, and it is the common
	 * case rather than a corner. Only the Ritualist has a `max_energy_shield`
	 * line in `game/Data/ClassStats.csv`, so every other class and every enemy
	 * carries a maximum of zero while holding zero -- and without this reading
	 * the condition would compare nothing against nothing and answer yes for
	 * all of them.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float EnergyShieldMaximum = -1.0f;

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

	/**
	 * How far "in reach" is for this row, in metres. Negative means the row is
	 * not about anything near the character. Issue #1597.
	 *
	 * A FOURTH NUMBER, BECAUSE COUNTING ENEMIES NEARBY ASKS TWO QUESTIONS AT
	 * ONCE: how far away, and how many. `ConditionValue` holds the count for
	 * `EnemiesInReachAtLeast` and `ScaleStep` holds the size per enemy for
	 * `PerEnemyInReach`, so neither of them can also hold the distance.
	 *
	 * THREE ALTERNATIVES WERE REJECTED. A single project-wide radius: three
	 * radii appear in the authored rows, and the genre deliberately varies it.
	 * Encoding the radius in the condition's name: that multiplies names by
	 * distance. Borrowing `ScaleStep` on a row that has no scale: it reads as a
	 * scale, and it breaks the first row wanting a count condition and a scale
	 * together.
	 *
	 * NOT TO BE CONFUSED WITH THE CONDITION `TargetWithinMetres`, which is about
	 * the one character being hit rather than a count of who is nearby, and which
	 * carries its distance in `ConditionValue` because it asks only one question.
	 * This field is named for the reach a count uses, and the condition and scale
	 * that read it are both named "in reach" to match.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Stats")
	float ReachMetres = -1.0f;
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
 * What a pool action's percentage is a percentage OF.
 *
 * AN ENUM RATHER THAN TWO BOOLS, because there are three answers and two bools
 * would carry an unstated invariant that both must not be true at once.
 */
UENUM(BlueprintType)
enum class ECataclysmPoolActionBase : uint8
{
	/** The most the pool can hold. What a sentence means when it says nothing. */
	Maximum UMETA(DisplayName = "The pool's maximum"),

	/**
	 * What the character holds right now. Different from the maximum on a hurt
	 * character, and a percentage of it can never empty the pool by itself.
	 */
	Current UMETA(DisplayName = "What is currently held"),

	/**
	 * The amount the event itself carried -- the health a skill's cost took, for
	 * the row that restores that amount as mana. Only events that carry an
	 * amount may be named by such a row, and the generator refuses the rest.
	 */
	EventAmount UMETA(DisplayName = "The amount the event carried"),
};

/**
 * What a worn enchantment DOES when an event happens, rather than what it
 * changes. Issue #1815.
 *
 * A STAT MODIFIER IS PULLED AND THIS IS PUSHED, which is the whole difference.
 * The pipeline reads a modifier when something asks for the stat; an action
 * happens at the moment its event does and is then over. Seventeen authored
 * enchantments say "restore", "generate" or "drain", and not one of them can be
 * written as a modifier.
 *
 * THE POOL IS A NAME RATHER THAN THE TWO ATTRIBUTES IT MEANS, so this header
 * needs to know about no attribute set.
 * `UCataclysmAbilitySystemComponent::PoolAttributesFor` is the one place that
 * turns a name into that pair, which makes it the one place a name nobody
 * implemented can be reported rather than silently granting nothing.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmPoolAction
{
	GENERATED_BODY()

	/**
	 * The event that fires it, as `game/Data/EnchantmentEffects.csv` spells it:
	 * `block`, `dodge`, `hit_taken` and the rest. These are the clock condition
	 * names with `seconds_after_` removed, because a clock asks "within N seconds
	 * of X" and this happens AT X.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	FName Event;

	/** `health`, `mana`, `energy_shield` or `class_resource`. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	FName Pool;

	/**
	 * PERCENTAGE POINTS, SIGNED. Positive restores and negative drains, which is
	 * the convention the sentence uses and the one `FCataclysmStatModifier::Value`
	 * already uses.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float Percent = 0.0f;

	/** What the percentage is a percentage OF. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	ECataclysmPoolActionBase Base = ECataclysmPoolActionBase::Maximum;

	/**
	 * Tags the thing that caused the event must carry, or empty for any.
	 *
	 * TWO AUTHORED ROWS ARE SCOPED THIS WAY: "melee kills" and "strike skills
	 * ... on hit". The tags tested are the SKILL'S, carried by the announcement
	 * that raised the event -- `Type.Melee` is on 30 of the 403 rows of
	 * `game/Data/WeaponSkills.csv` and `Type.Strike` on 31, measured 2026-09-14.
	 *
	 * AN EVENT THAT CARRIES NO TAGS CANNOT SATISFY A SCOPED ROW, which is the
	 * right answer rather than a missing one: a row scoped to melee must not
	 * fire on an event that cannot say whether it was melee.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	FGameplayTagContainer RequiredTags;

	/**
	 * A state of the character this only fires in, judged AT THE MOMENT the
	 * event happens rather than when a stat is read.
	 *
	 * `Always` is the default and means no condition, which is what that
	 * enumerator has meant since issue #959. One authored row wants a real
	 * one: "killing an enemy while below 30% HP".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	ECataclysmStatCondition Condition = ECataclysmStatCondition::Always;

	/** What that condition compares against. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Stats")
	float ConditionValue = 0.0f;
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
							   const FCataclysmStatConditions& State,
							   float ReachMetres = -1.0f);

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

	/**
	 * The condition a data sheet names, or false for a name this build does not
	 * know. Issue #45.
	 *
	 * THE NAMES ARE `CONDITIONS` IN `tools/generate_datatables.py`, which refuses
	 * any other when a sheet is written, and
	 * `tools/tests/test_stat_condition_names_match_the_engine.py` fails if this
	 * file's list and that one ever differ. So a condition added there and not
	 * here is caught when the tests run, rather than a row granting nothing with
	 * only a log line to say so.
	 *
	 * BOTH AUTHORED SOURCES READ THROUGH THIS SINCE ISSUE #1581. Enchantment
	 * effects always did, through `UCataclysmItemModifiers`;
	 * `UCataclysmPassiveTree::AccumulateInto` carried its own chain of eight of
	 * these names until then, and a name in this table but not in that chain was
	 * applied by the passive tree with NO condition at all -- a bonus that held
	 * all the time. Nothing compared the two lists, because the test above reads
	 * this file and the generator and did not read that chain.
	 */
	static bool ConditionNamed(const FString& Name,
							   ECataclysmStatCondition& OutCondition);

	/**
	 * Every condition name a data sheet may write, in this file's own order.
	 * Issue #1581.
	 *
	 * FOR A TEST THAT HAS TO COVER ALL OF THEM RATHER THAN A LIST WRITTEN OUT
	 * TWICE. A test naming the conditions by hand passes for ever after somebody
	 * adds a twenty-second, which is the drift that put the passive tree eight
	 * names behind this table in the first place.
	 */
	static void AllConditionNames(TArray<FString>& OutNames);

	/**
	 * Whether a condition compares `ConditionValue` against anything.
	 * Issue #1581.
	 *
	 * FOURTEEN OF THE TWENTY-EIGHT COMPARE NOTHING: `WhileBleeding`,
	 * `ClassResourceAtMaximum`, `EnergyShieldAtMaximum`, the three that ask
	 * what kind of blow this is, the two that ask whether whoever threw it is a
	 * boss or staggered, the two that ask whether the character is moving or
	 * standing still, the one that asks whether the character being hit is
	 * staggered, and the three that ask which ailment the character at the
	 * other end of the blow is carrying.
	 * Each says so in its own comment above, and
	 * `tools/generate_datatables.py` refuses to write a value on a row carrying
	 * one, so there is no number to carry across.
	 *
	 * THIS SENTENCE SAID "TEN OF THE TWENTY-ONE" AND WAS WRONG IN BOTH NUMBERS
	 * AND IN ITS LIST. Issue #1750 added the three ailment conditions, corrected
	 * the matching count in `ConditionTakesAValue` from ten to thirteen, wrote a
	 * comment there explaining that a list nobody counts is a list somebody
	 * extends without reading -- and did not touch this one, which is the list
	 * that comment points at. The same change added 131 lines to this file.
	 *
	 * SO THE TWO COUNTS HAVE TO MOVE TOGETHER, and nothing enforces it. Neither
	 * is reachable by a test: both are prose. `ConditionTakesAValue` in
	 * `CataclysmStatPipeline.cpp` holds the other one.
	 *
	 * A READER COPYING THE VALUE ANYWAY IS WRONG EVEN THOUGH IT LOOKS HARMLESS,
	 * because the column is only empty while the generator is the only writer.
	 * A hand-edited sheet, or a future condition that does compare something,
	 * would make a predicate judge a number it was never meant to have.
	 */
	static bool ConditionTakesAValue(ECataclysmStatCondition Condition);

	/**
	 * The scale a data sheet names, or false for a name this build does not
	 * know. Issue #45. The names are `SCALES` in `tools/generate_datatables.py`,
	 * held to this file's list by the same test as `ConditionNamed`.
	 */
	static bool ScaleNamed(const FString& Name, ECataclysmStatScale& OutScale);

private:
	/** Shared by Evaluate and EvaluateRate; they differ only in the last step. */
	static FCataclysmStatBreakdown Accumulate(float Base,
											  const TArray<FCataclysmStatModifier>& Modifiers,
											  const FGameplayTagContainer& SkillTags,
											  const FCataclysmStatConditions& State);
};
