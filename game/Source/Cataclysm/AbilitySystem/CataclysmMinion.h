// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmCharacterBase.h"
// For the slot a skill gadget casts. Ruled 2026-10-16.
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "CataclysmMinion.generated.h"

struct FCataclysmHitDelivery;
struct FCataclysmWeaponSkill;

class ACataclysmGroundZone;
class UCataclysmAbilitySystemComponent;
class UCataclysmVitalAttributeSet;
class UCataclysmSummonSkill;
class UStaticMeshComponent;

/**
 * A summoned imp: it chases the nearest enemy, hits it, and expires.
 *
 * A CHARACTER, WHICH IT WAS NOT. It used to be a bare AActor with an ability
 * system component and nothing else, and that had two consequences that were not
 * obvious from reading it. An AActor whose components are all non-scene
 * components gets no root component, and an actor with no root component reports
 * its location as the world origin however it was spawned -- so an imp searched
 * for targets around (0,0,0) rather than around itself. It also had no collision
 * of any kind, so nothing could find it: the class comment claimed it was "a
 * thing the world can damage and kill" and no sphere overlap could return it.
 * Both are fixed by it being a character, which brings a capsule. Issue #163.
 *
 * IT SHARES ITS BRAIN WITH A MONSTER. ACataclysmEnemyController possesses this
 * and ACataclysmEnemyCharacter alike; the only difference is which side it is
 * on and what its attacks are worth. Its side is copied from its summoner, so
 * everything the summoner is hostile to, it is hostile to.
 */
UCLASS()
class CATACLYSM_API ACataclysmMinion : public ACataclysmCharacterBase
{
	GENERATED_BODY()

public:
	ACataclysmMinion();

	/**
	 * How far it can reach, in centimetres, when its type states nothing.
	 *
	 * A DEFAULT NOW, NOT THE ANSWER. Every minion type in
	 * `game/Data/MinionTypes.csv` states its own reach -- 2 m for an imp, 10 m
	 * for a bolt turret, 15 m for a ballista -- and `Spawn` applies it. This is
	 * what a minion spawned without naming a type gets. Issue #340 asked for
	 * this and issue #622 is what needed it.
	 */
	static constexpr float DefaultReachCm = 300.0f;

	/**
	 * How far it notices something to chase, in centimetres, when its type
	 * states nothing.
	 *
	 * A DEFAULT NOW, NOT THE ANSWER, for the same reason as the reach above:
	 * every minion type states its own, from 2 m for a spike trap to 15 m for a
	 * ballista. Fifteen metres remains the fallback because that is what issue
	 * #163 settled for an imp summoned across a room.
	 */
	static constexpr float DefaultNoticeRadiusCm = 1500.0f;

	/**
	 * Seconds between its attacks, when its type states nothing.
	 *
	 * THE COMMENT HERE USED TO CITE A RULE THE DESIGN HAS REVERSED. It said the
	 * design "states one attack per second for every minion in the game". Issue
	 * #209 reversed that: `docs/Cataclysm_GDD_v2.md` now states "Every minion
	 * type has its own stats. A minion is not a percentage of its summoner." The
	 * table gives five different intervals, from 0.8 s for a mote to 3.0 s for a
	 * spike trap, and `Spawn` applies them.
	 */
	static constexpr float DefaultAttackIntervalSeconds = 1.0f;

	/**
	 * WHAT A MINION'S BLOW NO LONGER TAKES FROM ITS SUMMONER. Issue #1515.
	 *
	 * A constant lived here until 2026-09-17: 30% of the summoner's weapon
	 * damage, which a minion with no type row dealt. Issue #209 had already
	 * made it a fallback rather than the rule, and the project owner then
	 * ruled the fallback itself a bug -- a minion's blow carries the minion's
	 * own numbers -- so it is gone. `AttackTarget` deals nothing without a
	 * type row, and `tools/generate_datatables.py` refuses a summoning row
	 * that names no minion type.
	 *
	 * WHAT IS STILL MISSING IS THE ATTRIBUTE CHANNEL, the third of the three
	 * routes the design allows from a summoner to its minion.
	 * `game/Data/MinionScaling.csv` states it -- spirit for creatures,
	 * agility for machines, one percent of damage per point -- and no code in
	 * the engine reads that table. **Issue #340 tracks that gap**, and
	 * `test_the_attribute_channel_is_recorded_as_still_missing` in
	 * `tools/tests/test_minion_damage.py` is what keeps it recorded now that
	 * the constant it used to watch has gone.
	 */

	/**
	 * Put one in the world.
	 *
	 * @param Summoner       whose skill made it; its damage and its side are theirs
	 * @param Location       where it appears
	 * @param Lifetime       seconds before it goes away on its own
	 * @param bBurns         whether what it hits is set alight
	 * @param TypeName       a row of game/Data/MinionTypes.csv, such as "Ballista".
	 *                       Empty spawns one carrying the defaults above, which is
	 *                       what every caller did before issue #622
	 * @param HealthPercent  percent of its type's own health to give it, or 0 for
	 *                       that type's health unchanged. Iron Fortress states 150
	 */
	static ACataclysmMinion* Spawn(AActor* Summoner, const FVector& Location,
								   float Lifetime, bool bBurns,
								   const FString& TypeName = FString(),
								   float HealthPercent = 0.0f);

	/**
	 * The imported minion type table, or null with the reason logged.
	 *
	 * Loads the DataTable ASSET rather than the CSV, which is what makes it work
	 * in a packaged build: game/Data/ is reviewable output and is not cooked.
	 */
	static const UDataTable* LoadTypeTable();

	/** The row for a type name, or null when the table has no such row. */
	static const struct FCataclysmMinionTypeRow* FindType(const UDataTable* Table,
														  const FString& TypeName);

	/** Which row of the minion type table this one was made from. Empty if none. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	FString TypeName;

	/**
	 * The order this one was spawned in, among every minion this process has
	 * made; a larger number is younger. Issue #1479.
	 *
	 * WHAT "THE OLDEST" MEANS ONCE TWO SOURCES SHARE ONE CAP. Summon Imp's list
	 * held its own imps in order, and a risen imp is in no skill's list, so
	 * `UCataclysmCommand::MinionsOfTypeCommandedBy` sorts by this. A spawn time
	 * would not do: two made in one frame share it, and every test makes its
	 * imps in one frame.
	 */
	int64 SpawnOrder = 0;

	/**
	 * World time this one rose from a cursed enemy's death, or a negative number
	 * if it never did. Issue #1479: the Ritualist's starting node raises "a
	 * lesser imp that fights for you" and the status line says "Risen" over it
	 * for `RisenLabelSeconds`.
	 */
	float RisenAtSeconds = -1.0f;

	/**
	 * True for a minion that holds no place under any summon cap: one raised by
	 * "Enemies killed by necrosis rise as temporary minions for 5-10 seconds".
	 * Issue #1833 group E part 4b, ruled 2026-10-02: it is raised by the rule,
	 * lasts only its seconds, and neither fills a cap nor is destroyed to make
	 * room. `UCataclysmCommand::MinionsOfTypeCommandedBy`, which both cap counts
	 * read, leaves it out.
	 */
	bool bOutsideSummonCaps = false;

	/**
	 * Its type row's Tags, such as `Type.Minion, Type.Deployable,
	 * Minion.Machine`. Issue #1833, deployable Part 1. Empty for a minion with
	 * no type row.
	 *
	 * WHAT A SUMMONER'S ROW IS ASKED WITH, so a row scoped to `Type.Deployable`
	 * reaches a ballista and not an imp. Until this the type row's Tags column
	 * was imported and read by nothing, and every summoner lookup passed no tags.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	FGameplayTagContainer TypeTags;

	/** Whether this minion is a placed machine: its type carries `Type.Deployable`. */
	bool IsDeployable() const;

	/**
	 * Whether this machine is a trap: a placed machine whose type also carries
	 * `Type.Trap`. Ruled 2026-10-07.
	 *
	 * ONLY THE TAG. The owner decided on 2026-10-07 that of the trap feature
	 * only the tag is added now, and the ruling of 2026-09-11, "traps are a
	 * separate feature, for later", holds for the rest. A trap does not arm,
	 * trigger, re-arm or expire differently. It swings as any machine does.
	 *
	 * WHAT READS IT: the two counts `traps_active` and `gadgets_active`, and the
	 * armour its blow ignores (`AttackTarget`). A minion carrying `Type.Trap`
	 * without `Type.Deployable` is not a trap; no type row is written that way.
	 */
	bool IsTrap() const;

	/**
	 * Whether this machine is a gadget: a placed machine that is not a trap. A Bolt Turret and a Ballista are; a
	 * Spike Trap, which carries `Type.Trap`, is a trap and not a gadget (ruled 2026-10-07).
	 *
	 * THE TEST THE COUNT `gadgets_active` MAKES, stated once so the two cannot differ:
	 * `UCataclysmAbilitySystemComponent::CurrentConditions` counts a gadget by calling this. Every behaviour a
	 * summoner's row gives "your gadgets" asks it too.
	 */
	bool IsGadget() const;

	/**
	 * The flag by which a summoner's gadgets choose the enemy with the most health. Ruled 2026-10-13 by the
	 * coordinating session under the owner's delegation, for "Your gadgets prioritize the highest HP enemy within
	 * their range instead of the nearest". A row sets it at a flat 1, scoped to `Type.Deployable`, and it is read
	 * as above nought. No gameplay attribute and no base.
	 */
	static const TCHAR* GadgetsTargetMostHealthStat;

	/**
	 * What one stat of this gadget's summoner stands at for this gadget, or nought.
	 *
	 * NOUGHT FOR EVERYTHING THAT IS NOT A GADGET, before the summoner is asked anything: an imp, a mote, a Spike
	 * Trap and a minion with no type row. So a row that says "your gadgets" reaches the Bolt Turret and the
	 * Ballista and nothing else, whatever tags the row requires.
	 *
	 * ASKED WITH THIS GADGET'S OWN TYPE TAGS, as `UCataclysmCommand::SummonerMultiplierFor` asks a multiplier, so
	 * a row scoped to `Type.Deployable` matches. `StatForSkill` and not a multiplier, because these stats are
	 * flat figures with no base: a flag at 1, or a number of seconds or a percent.
	 *
	 * NOUGHT FOR A SUMMONER WITH NO STAT LINE, which is every enemy summoner, and for a summoner that is gone.
	 */
	float SummonersGadgetStat(const TCHAR* Stat) const;

	/**
	 * How far past its reach a body may measure and still be within it, in centimetres. A body placed at exactly
	 * the reach can measure a hair beyond it in floating point; this is far inside the 2 cm the brain already
	 * allows a creature in contact (`ACataclysmEnemyController::ContactToleranceCm`).
	 */
	static constexpr float ReachAllowanceCm = 0.01f;

	/**
	 * Every living character this minion is hostile to within its reach, nearest first.
	 *
	 * "WITHIN ITS REACH" IS THE BRAIN'S OWN MEASURE: the distance along the floor between the two actors, centre
	 * to centre, no more than `ReachCm`. `ACataclysmEnemyController::Think` attacks a target at that distance and
	 * walks toward one beyond it, and a gadget does not walk. The search itself is the sphere `AttackOnce` uses,
	 * which also finds a body whose edge reaches in while its centre is outside; those are dropped here, or a
	 * gadget could choose a target it then never fires at.
	 */
	TArray<AActor*> HostilesWithinReach() const;

	/**
	 * The enemy within this gadget's reach holding the most health now, or null. Null for everything that is not
	 * a gadget of a summoner holding `GadgetsTargetMostHealthStat`, and null when nothing is within reach.
	 *
	 * CURRENT HEALTH, AS A FIGURE AND NOT AS A SHARE, ruled 2026-10-13. Two holding the same: the nearer, a
	 * judgement of the writing session.
	 *
	 * CALLED BY `ACataclysmEnemyController::ChooseTarget` ON EVERY PASS, after the Quarry mark and before the
	 * search for the nearest, so the choice is made again each time the gadget picks a target.
	 */
	AActor* MostHealthWithinReach() const;

	/**
	 * How many seconds apart a summoner's gadgets fire at every enemy within their reach. Ruled 2026-10-13 by the
	 * coordinating session under the owner's delegation, for "Gadgets fire at all enemies within range
	 * simultaneously once every 5-8 seconds". A row states it flat, scoped to `Type.Deployable`; nought or less is
	 * no such rule. No gameplay attribute and no base.
	 *
	 * THE ROW ROLLS DOWN: 5 is its best figure, by the owner's rule of 2026-10-05 that a better roll gives the
	 * better outcome.
	 */
	static const TCHAR* GadgetVolleyEverySecondsStat;

	/**
	 * The seconds between this gadget's volleys, or nought for none: the SHORTEST figure among its summoner's flat
	 * rows on `GadgetVolleyEverySecondsStat` that match this gadget's type tags.
	 *
	 * THE SHORTEST AND NOT THE SUM, ruled 2026-10-13. The stat pipeline adds flat rows together, which for two
	 * rows of 5 and 8 seconds would make a volley every 13; so this reads the rows themselves and not
	 * `StatForSkill`. A row that is not flat, or that states nought or less, is passed over. A condition on a row
	 * is judged with nothing known and so refused; the generator refuses such a row.
	 *
	 * NOT SHORTENED BY `minion_attack_speed`, ruled the same day: "Gadgets fire 20%-40% faster" is the time
	 * between a gadget's ordinary shots and is not read here.
	 *
	 * NOUGHT FOR EVERYTHING THAT IS NOT A GADGET, as `SummonersGadgetStat` is.
	 */
	float VolleyEverySeconds() const;

	/**
	 * What a gadget does because time passed. Called by `ACataclysmCharacterBase::RegenerationStep` for every
	 * character, every quarter of a second; it does nothing for a character that is not a minion, for a minion
	 * that is not a gadget, and for a dead one.
	 *
	 * THE VOLLEY'S CLOCK IS STEPPED HERE, AND IT IS THE GADGET'S OWN. THE FIRST VOLLEY COMES ON THE FIRST STEP
	 * THAT FINDS AN ENEMY WITHIN THE GADGET'S REACH, the coordinating session's judgement of 2026-10-13; after
	 * it, one each time the row's seconds have passed. The seconds run from the first volley and the beat is
	 * kept from it: the gadget's age is compared with the age at which the clock last came round, and a volley
	 * that fires late does not make the next one later. What was first built waited the row's whole seconds
	 * from the placing, under which a Bolt Turret, which stands 5 seconds, fired at most once as it left.
	 *
	 * A STEP THAT FINDS NOBODY WITHIN REACH BEFORE THE FIRST VOLLEY COSTS NOTHING: it fires nothing and starts
	 * no clock. A GADGET THAT CANNOT ACT, stunned or knocked down, DOES NOT LOSE ITS FIRST VOLLEY: it comes on
	 * the first step that finds an enemy within reach while the gadget can act. A gadget that comes to hold the
	 * row late is under the same rule.
	 *
	 * A VOLLEY CAN FIRE UP TO A QUARTER OF A SECOND LATE, because this is where the clock is looked at and this
	 * runs four times a second.
	 *
	 * AFTER THE FIRST, the clock does not wait for anything: a volley that comes round while the gadget cannot
	 * act is lost, and one that comes round with nobody within reach is fired at nobody.
	 *
	 * AND BEFORE THE VOLLEY, `StepNoDamageAndDrain`, since the layer that built it. `Spawn` also calls this once
	 * as it places a minion, so a gadget's first step is at an age of nought.
	 */
	static void GadgetStep(AActor* Character);

	/**
	 * Deal this minion's ordinary blow, `AttackTarget`, to every enemy within its reach, now. Each is a single
	 * blow that can be evaded, and each raises `deployable_hit`. No limit on how many. In addition to its
	 * ordinary fire: the brain's own time between shots is not read and not moved.
	 *
	 * AN ENEMY THAT DIES BEFORE ITS TURN IS PASSED OVER, and a gadget that dies part way through, to the
	 * retaliation its own blow provoked, strikes nobody after that.
	 *
	 * @return how many enemies it struck
	 */
	int32 FireVolley();

	/** How many volleys it has fired. Read by tests. */
	int32 VolleysFired = 0;

	/** Whether it has fired its first volley. Until it has, its clock is not running. */
	bool bHasFiredAVolley = false;

	/** The age, in seconds since it was placed, at which its volley clock last came round. Nought until it has. */
	float LastVolleyAtAgeSeconds = 0.0f;

	/**
	 * The flag by which a summoner's gadgets cast a copy of the last skill their summoner used, at their own target,
	 * every `SecondsBetweenSkillCopies`. Ruled 2026-10-14: the owner's "the WHOLE skill is cast from the gadget (not
	 * hits only)", and the coordinating session's rulings on the enchantment session's plan, for "Gadgets copy the
	 * last skill you used and fire it at their target once every 2 seconds". A row sets it at a flat 1, scoped to
	 * `Type.Deployable`, and it is read through `SummonersGadgetStat` as above nought. No gameplay attribute and no
	 * base.
	 */
	static const TCHAR* GadgetsCopyYourLastSkillStat;

	/**
	 * The seconds between a gadget's copies. THE SENTENCE'S OWN 2, AS A FIGURE OF THE ENGINE AND NOT OF THE ROW: the
	 * row is a flag, flat 1 to 1, as the other gadget flags are, and the sentence states one figure and no range to
	 * roll. NOT SHORTENED BY "Gadgets fire faster", ruled: that is the time between a gadget's ordinary shots.
	 */
	static constexpr float SecondsBetweenSkillCopies = 2.0f;

	/**
	 * What this gadget's step does for the flag above. Called by `GadgetStep` after the drain and before the volley,
	 * every quarter of a second; nothing for a gadget whose summoner holds no such row, which is every gadget until
	 * the row is written, and nothing for anything that is not a gadget.
	 *
	 * THE CLOCK IS THE GADGET'S OWN, KEPT AS THE VOLLEY'S IS, by the gadget's age. Ruled 2026-10-14. THE FIRST COPY
	 * COMES ON THE FIRST STEP THAT CASTS ONE: a step that finds no target within reach, no skill on record or a
	 * gadget that cannot act casts nothing and starts no clock. After it, one copy each time 2 seconds have passed,
	 * the beat kept from the first, and a beat that comes round and casts nothing is passed over: the clock does not
	 * wait for it. IN ADDITION TO THE GADGET'S ORDINARY SHOT and to its volley; neither is read or moved.
	 *
	 * THE CLOCK STARTS ON A COPY THAT STARTED, and not on a step that only found a target and a record: a copy can
	 * still be refused, and the one `Spawn`'s own step asks for always is, because it runs inside the summoner's
	 * deploying skill, where the engine defers a grant. A judgement of the writing session; the option not taken
	 * started the clock on the step that found everything, which would have lost the first copy of every gadget.
	 *
	 * @return whether a copy started on this step
	 */
	bool StepSkillCopy();

	/**
	 * Cast one copy of the last skill this gadget's summoner used, now, at this gadget's target. Ruled 2026-10-14.
	 *
	 * THE CAST IS THE SUMMONER'S. The summoner's own row, granted to the summoner for one use by
	 * `UCataclysmTriggeredSkill::Trigger`, started FREE from this gadget's place and aimed at its target's location:
	 * the summoner's weapon, stats, rows, level and tags at the moment of the copy, and nothing run on this gadget's
	 * own ability system. Its hits are the summoner's, as a repeated cast's are. It costs nothing and waits for and
	 * starts no cooldown, and it sends no skill-used notice, so it raises no `skill_use` and no `attack_use`.
	 *
	 * THE TARGET IS THE ONE THIS GADGET'S BRAIN CHOOSES, `ACataclysmEnemyController::ChooseTarget`, while it stands
	 * within this gadget's reach; anything else is no target. THE SKILL IS THE SUMMONER'S RECORD,
	 * `UCataclysmAbilitySystemComponent::SkillGadgetsCopy`, the row AS IT WAS USED, cast whether or not the summoner
	 * still holds it: the coordinating session's ruling of 2026-10-14. So after a change of weapon the copy is the
	 * skill of the weapon held before, at the numbers of the weapon held now.
	 *
	 * @return whether a copy started. False, casting nothing, for anything that is not a gadget, for a gadget that
	 *         cannot act, for a summoner that is gone or dead, with no record or no target, and
	 *         when the skill refuses itself or the engine defers its grant.
	 */
	bool CopySummonersLastSkill();

	/** The enemy this gadget's copy would be aimed at now, or null. See `CopySummonersLastSkill`. */
	AActor* SkillCopyTarget() const;

	/** How many copies of its summoner's skill it has cast. Read by tests. */
	int32 SkillCopiesMade = 0;

	/** Whether it has cast its first copy. Until it has, its copy clock is not running. */
	bool bHasCopiedASkill = false;

	/** The age, in seconds since it was placed, at which its copy clock last came round. Nought until it has. */
	float LastSkillCopyAtAgeSeconds = 0.0f;

	/**
	 * THE SKILL GADGET. Ruled 2026-10-16 by the coordinating session (S1-S14), for the four rows "Your heavy attack /
	 * special ability / support ability / ultimate ability is converted into a deployable gadget that fires (or
	 * activates) your ... once per ... cooldown ... Scales off gadget damage." The owner's words: pressing a converted
	 * slot's key PLACES its gadget where the player stands, one gadget per converted slot, a new press moves it, and
	 * the player no longer casts that skill.
	 *
	 * A GADGET OF ITS OWN TYPE ROW, `SkillGadgetTypeName`, and outside every summon cap. It has no lifespan: the floor
	 * being cleared destroys it as every deployable is destroyed (`UCataclysmFloorContents::ClearTheFloor`), and it
	 * can be struck and killed as any gadget can. EVERY OTHER GADGET ROW REACHES IT, because it is a gadget (S9).
	 *
	 * WHICH SLOT IT IS RECORDED HERE AND ON ITS PLAYER'S ABILITY SYSTEM, keyed by slot, so the basic attack's gadget
	 * of the second stack is one more key and not another field. Nothing here assumes the basic slot cannot be one.
	 */
	static const TCHAR* SkillGadgetTypeName;

	/**
	 * How near the player must stand to a support gadget for its self buff to be granted, in centimetres: 8 m. S13,
	 * a number under the owner's delegation of unstated numbers. Measured along the floor, gadget to player.
	 */
	static constexpr float SkillGadgetSelfBuffReachCm = 800.0f;

	/**
	 * Place a skill gadget for `Player`'s `Slot` at `Location`, or null with the reason logged.
	 *
	 * NULL, AND NOTHING LEFT STANDING, WHEN THE TYPE ROW IS MISSING: `Spawn` would otherwise make a minion carrying
	 * the defaults, which is no gadget and would expire, walk and fight. The row is the enchantment session's to
	 * author, so an older DataTable asset is the one way to reach this.
	 */
	static ACataclysmMinion* PlaceSkillGadget(AActor* Player, ECataclysmAbilitySlot Slot, const FVector& Location);

	/** Which of its player's slots this gadget casts, or `None` for every minion that is not a skill gadget. */
	ECataclysmAbilitySlot SkillGadgetSlot = ECataclysmAbilitySlot::None;

	/** Whether this is a skill gadget: a gadget placed for a slot. */
	bool IsSkillGadget() const;

	/**
	 * Move this skill gadget to `Location`, as a new press does (S6). ITS HEALTH AND ITS CLOCK ARE KEPT, so pressing
	 * again and again cannot force a cast; nothing is destroyed and no `gadget_destroyed` is raised.
	 */
	void MoveSkillGadgetTo(const FVector& Location);

	/** Seconds since this skill gadget was placed or last moved; its own measure of standing still. */
	float SecondsSinceSkillGadgetPlaced() const;

	/**
	 * The row of the skill this gadget casts: what its player holds in its slot NOW, copied into `Out`, so a change
	 * of weapon changes what the gadget casts. False for a player holding nothing in the slot.
	 */
	bool SkillGadgetsSkill(FCataclysmWeaponSkill& Out) const;

	/**
	 * The seconds between this gadget's casts of `Skill` (S2, S3): the row's own cooldown when it states one, else
	 * its slot table's, through `UCataclysmGameplayAbility::CooldownAfterReduction` with the skill's tags, then
	 * shortened by the player's `minion_attack_speed` rows that reach this gadget, as an interval is shortened:
	 * divided by one plus the increases. NEVER `GetBaseCooldown`, whose Mana Weaver nought must not reach it.
	 * FOR THE BASIC ATTACK, the player's swing interval in place of the cooldown, and nought for "never" (stack 2).
	 */
	float SkillGadgetSecondsBetweenCasts(const FCataclysmWeaponSkill& Skill) const;

	/**
	 * How far from this gadget `Skill` reaches, in centimetres (S4): a Strike's radius, a Projectile's or a Debuff's
	 * range, as the row states it, raised by the player's `minion_range` rows that reach this gadget. Nought for a
	 * shape with no reach to aim by.
	 */
	float SkillGadgetReachCm(const FCataclysmWeaponSkill& Skill) const;

	/**
	 * Whether a skill gadget may ever cast `Skill`: it has a shape (S10), and the rule a row repeats a skill by lets
	 * it through, LESS THAT RULE'S REFUSAL OF A SELF BUFF, for this caller only (S13). So a movement skill, an aura, a
	 * summon or deployable, a held or channelled skill, a planted weapon, a `Requires` and a health cost are left out,
	 * as `UCataclysmTriggeredSkill::RepeatsFromARow` leaves them out. The skill bar asks it to say "casts nothing".
	 */
	static bool SkillGadgetMayCast(const FCataclysmWeaponSkill& Skill);

	/**
	 * What this gadget does for its slot on its step, called by `GadgetStep` after the copy and before the volley.
	 * Nothing for a minion that is not a skill gadget.
	 *
	 * ONCE PER ITS CLOCK, AS A COOLDOWN RUNS: the first cast on the first step that casts one, and each later cast on
	 * the first step at least `SkillGadgetSecondsBetweenCasts` after the last that started. A step that casts nothing,
	 * for want of a target or of a living player, starts nothing and waits. A CAST CAN COME UP TO A QUARTER OF A
	 * SECOND LATE, because this is where the clock is looked at and this runs four times a second.
	 * THE BASIC ATTACK'S GADGET KEEPS ITS BEAT INSTEAD, and never casts on an interval of nought (stack 2, below).
	 *
	 * @return whether a cast started on this step
	 */
	bool StepSkillGadget();

	/**
	 * Cast the player's skill of this gadget's slot once, now, from this gadget's place. S1, S4, S5, S10, S11, S13.
	 *
	 * A FREE START, `UCataclysmTriggeredSkill::Trigger` with this gadget as the actor cast by, of the player's own
	 * row with `Type.Deployable` added to its tags, so the rows that raise gadget damage reach it. A SELF BUFF is
	 * granted to the player while the player stands within `SkillGadgetSelfBuffReachCm`, and not while one this
	 * gadget granted still runs; EVERY OTHER SHAPE is aimed at the nearest enemy within `SkillGadgetReachCm`, or at
	 * `MostHealthWithin` that reach when the player holds the highest-health row (stack 2).
	 *
	 * @return whether it started. False, casting nothing, for a gadget that cannot act, a player gone or dead, a
	 *         slot holding no skill or a shapeless one, a skill no row may cast (a movement skill, a summon, a held
	 *         or channelled skill, one with a `Requires` or a health cost), no target, or the skill refusing itself
	 *         (a lock of its slot).
	 *
	 * @param bEvenForADeadPlayer  true for the one cast a player's death makes (`FireForItsSummonersDeath`): a dead
	 *        player is then not refused, and every other refusal stands. False, the default, for the clock's cast.
	 */
	bool CastPlayersSkillFromHere(bool bEvenForADeadPlayer = false);

	/**
	 * A PLAYER'S DEATH FIRES ITS GADGETS. Ruled 2026-10-19 by the coordinating session (C1-C5), for "When you die, all
	 * your active traps and gadgets immediately trigger simultaneously": the gadget half, reached by the row action
	 * `gadgets_cast_now` on `player_death` (`UCataclysmAbilitySystemComponent::GadgetsCastNowAction`). The trap half
	 * is not built, and joins when traps are.
	 *
	 * EVERY SKILL GADGET `Dying` COMMANDS, TAKEN ONCE, FIRES ONCE, ALL INSIDE THIS ONE CALL (C3): `UCataclysmCommand::
	 * ThingsCommandedBy`, which leaves out the dead, less every minion that is not a skill gadget (an imp, a mote, a
	 * trap, and War's gadgets, which this action does not fire, by the no-War mandate: ruled 2026-10-19, J2).
	 * WHAT A DEATH LEAVES STANDING: everything, when `player_death` is raised. The skill gadget stays and casts nothing by
	 * its clock while the player is dead (S11 of 2026-10-16); inside a dungeon of the empire the respawn's leaving of the
	 * dungeon ends everything the player commands (`ACataclysmDungeonGameMode::EndEveryPlayersFollowers`).
	 *
	 * @return how many gadgets fired
	 */
	static int32 FireEveryGadgetAtTheDeathOf(AActor* Dying);

	/**
	 * Fire this gadget once, now, for its summoner's death. See `FireEveryGadgetAtTheDeathOf`.
	 *
	 * A SKILL GADGET CASTS ITS PLAYER'S SKILL ONCE (C2) by the path its clock casts by, `CastPlayersSkillFromHere`,
	 * less only that path's refusal of a dead player: at the target its clock would choose, at the whole of its damage,
	 * with every other refusal standing (no target within its reach, a slot no longer converted, a shapeless skill, a
	 * lock). ITS CLOCK IS LEFT AS IT WAS (C4): `bSkillGadgetHasCast` and `LastSkillGadgetCastAtAgeSeconds` are not
	 * touched, so the death cast is one more and moves no later cast.
	 *
	 * NOTHING for a minion that is not a skill gadget (War's gadgets included, J2), a dead one or one that cannot act.
	 *
	 * @return whether it fired
	 */
	bool FireForItsSummonersDeath();

	/** How many casts it has made for its summoner's death. Read by tests. Each is counted in `SkillGadgetCastsMade` too. */
	int32 SkillGadgetDeathCastsMade = 0;

	/** Seconds until this gadget may cast again and the length of its clock, both nought before its first cast. */
	void SkillGadgetClock(float& OutRemaining, float& OutDuration) const;

	/**
	 * The skill gadget whose cast `Skill` is, or null: the template's `FreeRepeatCastBy`, when that is a skill
	 * gadget. A gadget's copy of its summoner's last skill is not one. Read by the overload taking a delivery, below,
	 * which `UCataclysmSkillEffects::ApplyHit` calls, and by `UCataclysmSkillTemplate::LeaveZoneAlong` (stack 2).
	 */
	static const ACataclysmMinion* SkillGadgetCasting(const class UGameplayAbility* Skill);

	/** How many casts it has made for its slot. Read by tests. */
	int32 SkillGadgetCastsMade = 0;

	/** Whether it has cast for its slot. Until it has, its clock is not running. */
	bool bSkillGadgetHasCast = false;

	/** Its age when its last cast for its slot started. */
	float LastSkillGadgetCastAtAgeSeconds = 0.0f;

	/** World time at which it was placed or last moved. */
	float SkillGadgetPlacedAtSeconds = 0.0f;

	/**
	 * SKILL GADGETS, STACK 2. Ruled 2026-10-16 by the coordinating session ("Stack 2 rulings", items 1-7 and P1-P9),
	 * for "Your basic attack is converted into a deployable gadget that fires your basic attack at the nearest enemy
	 * within range, once per basic attack cooldown. Scales off gadget damage."
	 *
	 * THE BASIC ATTACK'S GADGET is the same skill gadget, for `ECataclysmAbilitySlot::BasicAttack`: placed by a left
	 * click on an enemy (`ACataclysmPlayerController::PressOnAnEnemy`), and cast by `StepSkillGadget`. Two things
	 * differ from the keyed slots. ITS CLOCK IS THE PLAYER'S SWING INTERVAL, `UCataclysmBasicAttack::
	 * SecondsBetweenSwingsFor`, shortened by "Gadgets fire 20%-40% faster" (`SkillGadgetSecondsBetweenCasts`); an
	 * interval of nought or less, which is "never swing", NEVER CASTS. AND IT KEEPS ITS BEAT (P1): each cast after the
	 * first is due one interval after the last one was DUE, not after the step that made it, and a beat more than one
	 * interval behind is brought to now. So the average rate is the interval's, though the step that looks at the
	 * clock runs four times a second. IT CANNOT CAST MORE THAN FOUR TIMES A SECOND, whatever the interval: the step
	 * is a quarter of a second and casts once at most. A step that casts nothing, for want of a target, moves nothing,
	 * as the keyed clock waits. The keyed slots' clock is unchanged.
	 */

	/**
	 * Every living character this minion is hostile to within `Reach` of it, nearest first, by the brain's measure
	 * along the floor: `HostilesWithinReach` for any reach. Stack 2, item 4: a skill gadget aims by its skill's reach
	 * and not by its type row's.
	 */
	TArray<AActor*> HostilesWithin(float Reach) const;

	/**
	 * The enemy within `Reach` holding the most health now, or null: `MostHealthWithinReach` for any reach. Null for
	 * everything that is not a gadget of a summoner holding `GadgetsTargetMostHealthStat`. CURRENT HEALTH, AS A
	 * FIGURE, and of two holding the same the nearer, as there. `CastPlayersSkillFromHere` aims at it first, by the
	 * skill's reach, so "Your gadgets prioritize the highest HP enemy within their range instead of the nearest"
	 * reaches a skill gadget's cast (stack 1's judgement f, reversed).
	 */
	AActor* MostHealthWithin(float Reach) const;

	/**
	 * The skill gadget whose cast `Delivery` is a blow of, or null. THE GADGET THE DELIVERY CARRIES FIRST,
	 * `FCataclysmHitDelivery::CastBy`, when that is a skill gadget; ELSE the delivery's skill, as the overload above
	 * reads it. Stack 2, item 3: a blow that lands after its one-use grant has ended (the last axes of a rack, a
	 * buried axe's later leap, anything in flight when its use is cancelled) has lost its skill, because the grant's
	 * end clears `FreeRepeatCastBy` and the engine marks the instance for removal, so only what it carries can say.
	 * A GADGET DESTROYED BEFORE ITS BLOW LANDS is no gadget, and the blow is priced as the player's.
	 * Read by `UCataclysmSkillEffects::ApplyHit`.
	 */
	static const ACataclysmMinion* SkillGadgetCasting(const struct FCataclysmHitDelivery& Delivery);

	/**
	 * The flag by which a summoner's gadget that kills an enemy with a blow of its own strikes once more. Ruled
	 * 2026-10-13 by the coordinating session under the owner's delegation, for "When a gadget kills an enemy, it
	 * fires an additional burst at the nearest enemy within its range". A row sets it at a flat 1, scoped to
	 * `Type.Deployable`, and it is read as above nought. No gameplay attribute and no base.
	 *
	 * READ AT THE END OF `AttackTarget`, WHICH IS WHERE THE BURST IS. The burst is one ordinary blow, at once, at
	 * the nearest enemy within the gadget's reach, or nothing if there is none. It is `AttackTarget` again, so a
	 * kill by a burst fires another.
	 *
	 * WHAT COUNTS AS THE GADGET'S KILL: a blow dealt by `AttackTarget` that reached the enemy's health and left
	 * it dead, where the record of the last blow on that enemy names this gadget as what dealt it and is not a
	 * tick. That is the gadget's ordinary shot, a blow of a volley and a burst. It is not a burn the gadget set,
	 * which is a tick; not its death explosion, which is not `AttackTarget`; and not a row its hit fired, which
	 * the summoner deals. Under Conduit the kill is credited to the summoner and the record still names the
	 * gadget as what dealt it, so it counts.
	 *
	 * NO ROW EVENT IS RAISED FOR THE KILL, ruled: the action is kept on the gadget.
	 */
	static const TCHAR* GadgetsBurstOnKillStat;

	/** How many bursts it has fired on a kill. Read by tests. */
	int32 BurstsFired = 0;

	/**
	 * The two stats of "Your gadgets are invincible but lose 5% of their maximum HP per second passively". Ruled
	 * 2026-10-13 by the coordinating session under the owner's delegation. Both are asked of the summoner with
	 * the gadget's type tags, both are flat, and neither has a gameplay attribute or a base.
	 *
	 * THE FIRST IS A FLAG: at a flat 1 the summoner's gadgets take no damage from any blow. THE SECOND IS A
	 * PERCENT: that share of the gadget's maximum health AS IT WAS PLACED is lost each second, as a direct loss
	 * that no defence and no immunity meets. Each works without the other; the one sentence states both, as two
	 * rows of one enchantment.
	 */
	static const TCHAR* GadgetsTakeNoDamageStat;
	static const TCHAR* GadgetHealthLostPercentPerSecondStat;

	/**
	 * How long the no-damage window is opened for each time a holder's gadget is stepped, in seconds. The step
	 * comes every quarter of a second, so the window is renewed four times before it could lapse; and a gadget
	 * whose summoner stops holding the flag can be hurt again a second after its last renewal at the latest.
	 */
	static constexpr float NoDamageRenewalSeconds = 1.0f;

	/**
	 * The share of its maximum health as placed below which the drain's next step ends a gadget, as a fraction.
	 * Twenty steps of a twentieth each do not add up to exactly the whole in floating point; this is the
	 * allowance for that, a ten-thousandth of the maximum.
	 */
	static constexpr float DrainedToNothingShare = 0.0001f;

	/**
	 * Keep this gadget's no-damage window open if its summoner's row says so, and take what the drain owes for
	 * the time since the last step. Called by `GadgetStep`, first of its jobs.
	 *
	 * NO DAMAGE IS THE WINDOW EVERY CHARACTER ALREADY HAS, `UCataclysmAbilitySystemComponent::GrantDamageImmunity`
	 * on the gadget's own ability system, renewed on each step while the flag is held. It was chosen as the least
	 * change: `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` already empties every blow and every tick
	 * inside it, and nothing in that file is touched. The window empties DAMAGE and nothing else: a stun, a
	 * knockback, the summon cap removing the gadget and an enemy choosing it as a target do not read it.
	 *
	 * THE DRAIN IS A DIRECT WRITE OF HEALTH, as the delayed share of damage taken is drained
	 * (`UCataclysmDelayedDamage::DrainStep`), so the window above does not stop it. It is counted by the gadget's
	 * age, so a step that arrives late takes what the late time owes, and no time before the first step is
	 * charged. THE MAXIMUM IS THE ONE THE GADGET WAS PLACED WITH, read at its first step, which `Spawn` takes as
	 * it places it; what raises or lowers the maximum afterwards does not move the drain.
	 *
	 * A GADGET THE DRAIN EMPTIES DIES AS DESTROYED: its health is written to nought and `HandleDeath` is called,
	 * so `gadget_destroyed` is raised and everything a minion's death sets off is set off. KILLED BY NOBODY: the
	 * record of the last blow on it is emptied first, as the Sacrificial Ward empties it, or the death would name
	 * whoever last hurt it before the row was held.
	 *
	 * @return whether the gadget is still standing
	 */
	bool StepNoDamageAndDrain();

	/**
	 * Its maximum health as it was placed, which the drain takes its percent of. Nought until its first step.
	 * AFTER `minion_health`, which `Spawn` applies before the first step: "Gadgets have 40%-70% increased HP" makes
	 * this larger and the drain larger with it, so the gadget is emptied in the same time.
	 */
	float SpawnedMaxHealth = 0.0f;

	/** The age, in seconds since it was placed, at which the drain was last counted. */
	float LastDrainAtAgeSeconds = 0.0f;

	/**
	 * Percent of its type's own health the skill that deployed it asked for, or
	 * zero for that type's health unchanged.
	 *
	 * RECORDED AND NOT YET APPLIED, WHICH IS SAID HERE RATHER THAN HIDDEN.
	 * THE BLOCKER THIS COMMENT USED TO NAME IS GONE: it said health could not be
	 * set from the type until the summoner's level could be read, and issue #340
	 * built that -- `Spawn` now sets a minion's health from `BaseHealth` and
	 * `HealthPerLevel` in its type row, raised by that level.
	 *
	 * What is still not applied is THIS number, the percentage of that health a
	 * deploying skill asked for. Iron Fortress is the only skill that states
	 * one, at 150.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	float DeployedHealthPercent = 0.0f;

	/**
	 * Whose it is. Its side is this actor's and its blows are dealt in this
	 * actor's name, but credit for what it hits and kills is the MINION'S
	 * unless this actor holds the Conduit keystone -- see
	 * `HitsCountAsTheSummoners`. Its DAMAGE is its own, from its type row --
	 * see `AttackTarget`.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	TObjectPtr<AActor> Summoner;

	/**
	 * Whether this minion's hits and kills are credited to its summoner.
	 *
	 * FALSE UNLESS THE SUMMONER HOLDS THE CONDUIT KEYSTONE, which is the
	 * Ritualist node `Ritualist_keystone_spine_003`: "Damage dealt by your
	 * minions counts as damage you dealt, for every effect of yours that asks."
	 * The project owner ruled on 2026-09-17 that a minion's blow carries only
	 * the minion's own stats and minion affixes, and that this keystone is what
	 * turns the summoner's on-hit and on-kill effects, kill credit and damage
	 * bonuses back on for it.
	 *
	 * IT IS A FLAG AND NOT A MULTIPLIER, so the stat
	 * `minion_hits_count_as_yours` is read for whether it stands above zero, in
	 * the same way `minion_explodes_on_death` is.
	 *
	 * THE ONE CALLER IS `UCataclysmCombatEvents::NoteBlow`, which is where a
	 * blow is turned into a notice and a record of who last hit whom. Everything
	 * that asks "was this mine" -- nine places at the time of writing, in
	 * `CataclysmPlayerCharacter.cpp` and `CataclysmDungeonGameMode.cpp` --
	 * reads what that one line decided and needs no check of its own.
	 *
	 * WHAT THIS DOES NOT TOUCH: a minion still never critically strikes,
	 * penetrates, leeches, carries a weapon sub-type or rolls its summoner's
	 * ailment chances, with or without the keystone. Those are the flags on
	 * `FCataclysmHitDelivery` that `MinionDelivery` sets, and the ruling leaves
	 * them where they are.
	 */
	static bool HitsCountAsTheSummoners(const ACataclysmMinion* Minion);

	/**
	 * How a blow dealt by `Follower` in its own name is delivered: a single-
	 * target blow with every flag a minion's own swing carries -- no critical
	 * strike, no penetration, no leech, no weapon sub-type, no ailment chance.
	 * For a blow something other than the minion's own swing makes it deal,
	 * which is Chorus's repeat (issue #1515), so it cannot drift from the swing.
	 * A subjugated enemy is given the same, since it too is "your minion".
	 */
	static FCataclysmHitDelivery OwnBlowDelivery(AActor* Follower);

	/**
	 * Whether it is a deployable machine rather than a creature: its type row's
	 * Family is Machine. A Second Self is never one (issue #1515): "the minion
	 * you have held" is a creature summoned or an enemy taken.
	 */
	bool bIsMachine = false;

	/** Whether what it hits is set alight. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	bool bBurnsWhatItHits = false;

	/** How many times it has attacked. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	int32 AttacksMade = 0;

	/**
	 * What one of this minion's own blows is worth, settled when it was
	 * summoned. Issue #340.
	 *
	 * ITS OWN, NOT A SHARE OF ITS SUMMONER'S. The design of 2026-08-06 says a
	 * minion reaches its summoner through three channels and nothing else, and
	 * one of them is "the type's own base, raised by the summoner's level".
	 * A percentage of the summoner's weapon is a fourth channel however it is
	 * written, which is why this is a flat figure.
	 *
	 * SETTLED ONCE, AT SPAWN, rather than read on every blow. A minion's
	 * damage rises with the summoner's LEVEL, and a level does not change
	 * during the twenty seconds an imp exists. Reading it per blow would cost
	 * a table lookup for every swing of every minion in a Horde wave.
	 *
	 * ZERO MEANS NO TYPE WAS NAMED, and `AttackTarget` then falls back to the
	 * old share of the summoner. That is reachable in the game rather than
	 * only in tests: `CataclysmSkillTemplates.cpp:3260` produces an empty type
	 * name whenever a summoning skill's shape parameters name no minion kind.
	 * No shipped skill row leaves it empty today.
	 */
	float OwnDamagePerHit = 0.0f;

	/** A stand-in body, so an imp is visible before there is any art. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	/**
	 * Blow up, hurting everything within RadiusCm, then be destroyed.
	 *
	 * Summon Imp: "Summoning a fourth destroys the oldest, which explodes for
	 * damage in a 3 meter radius."
	 */
	/**
	 * Blow up, hurting everything within the radius it was told, then be
	 * destroyed.
	 *
	 * NO FIGURES PASSED IN, SINCE ISSUE #1515. The radius is the summoning
	 * skill's, told at the summoning, and the damage is this minion's own:
	 * its blow times the share its type row states. Until then the caller
	 * passed the skill's damage percentage, which was read against the
	 * SUMMONER'S weapon -- the rule the owner corrected.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Minion")
	void Explode();

	/**
	 * The share of the dying creature's own maximum health its death deals,
	 * and the radius in metres it reaches. Issue #1515, the Third Pact's option
	 * Shared Ruin: "When a minion of yours dies, everything within 4 metres
	 * takes damage equal to 20% of that minion's maximum health." Read off the
	 * COMMANDER, since the node is the commander's; both must be above zero.
	 */
	static const TCHAR* DeathBlastPercentOfMaximumHealthStat;
	static const TCHAR* DeathBlastRadiusMetresStat;

	/**
	 * Shared Ruin: `Lost`, a minion or thrall of `Commander` that has just died,
	 * damages every enemy within the commander's stated radius by the stated
	 * share of its own maximum health. Answers how many it struck.
	 *
	 * CALLED BEFORE THE DEATH IS HANDLED, because a minion whose death is an
	 * explosion is destroyed inside `HandleDeath` and its body is what the
	 * blast is measured from and dealt by.
	 *
	 * A SECOND BLAST, NOT A CHANGE TO THE FIRST. Ruled on 2026-09-23 under the
	 * owner's delegation: with Every One Bursts held, a minion's death is both
	 * its explosion and this, each at its own figures.
	 *
	 * THE DYING CREATURE DEALS IT, as a minion's explosion is its own: the
	 * owner ruled on 2026-09-17 that a minion's hits are the minion's, and the
	 * Conduit keystone is what makes them count as the summoner's.
	 */
	static int32 DeathBlast(AActor* Lost, const AActor* Commander);

	/**
	 * Tyrant's Chains' six-piece bonus: "When a pet or minion dies, it explodes, dealing 50% of its maximum health
	 * as Death damage to all nearby enemies. Additionally, a new, powerful Tyrant's Minion is summoned to fight for
	 * you for a short duration". Owner's ruling #3 of 2026-10-17 and the coordinating session's rulings of
	 * 2026-10-18 (Q1-Q5); `docs/DECISIONS.md` carries both.
	 *
	 * THE BLAST IS SHARED RUIN'S, ON ITS TWO STATS, AND THIS FLAG MAKES IT DEATH DAMAGE (Q1). Above nought on the
	 * commander, `DeathBlast` delivers its blow typed Death; at nought the blow carries the dying creature's own
	 * type, as Shared Ruin's always has. A flag, a flat 1. No gameplay attribute and no base.
	 */
	static const TCHAR* DeathBlastDealsDeathStat;

	/**
	 * The seconds a Tyrant's Minion lasts, read off the commander when a minion or thrall of its dies (Q2: 10 s, a
	 * number under the owner's delegation, stated by the row). Nought means none is summoned. No gameplay attribute
	 * and no base.
	 */
	static const TCHAR* TyrantsMinionSecondsStat;

	/**
	 * The minion type a Tyrant's Minion is made from: its own row, `TyrantsMinion` in `game/Data/MinionTypes.csv`,
	 * from the design workbook's Minion Types sheet. Ruled 2026-10-18 under the owner's delegation: twice the
	 * Ballista's health and the Ballista's damage a second as one melee hit a second, every other cell the Imp's.
	 */
	static const TCHAR* TyrantsMinionTypeName;

	/**
	 * True for a Tyrant's Minion. WHAT TELLS IT FROM AN IMP, since it is made from the Imp's row: its own death
	 * summons none (ruling #3), and the two replacement paths, Press-Ganged's and Rekindled's `ReplaceLost` and the
	 * re-summon `ScheduleResummon`, are not asked at its death (Q4).
	 */
	bool bIsTyrantsMinion = false;

	/** The Tyrant's Minion `Commander` has standing, alive, or null. At most one stands (ruling #3). */
	static ACataclysmMinion* TyrantsMinionOf(const AActor* Commander);

	/**
	 * Summon `Commander`'s Tyrant's Minion at `Where`, where a minion or thrall of its has just died, or refresh
	 * the one standing. Called by the commander's death hook, `UCataclysmVitalAttributeSet::
	 * NotifyIfHealthReachedZero`, after the death is handled, so for exactly the deaths `DeathBlast` fires for (Q3).
	 *
	 * NOTHING WHEN THE LOST ONE WAS A TYRANT'S MINION (ruling #3: it explodes but summons none), when the
	 * commander's `TyrantsMinionSecondsStat` is nought, or when the commander is dead.
	 *
	 * ONE AT MOST (ruling #3). With one standing, no second is summoned: the standing one's time restarts at the
	 * full length and its health is kept (Q5). The length is raised by `minion_duration` as `Spawn` raises it.
	 *
	 * OUTSIDE EVERY SUMMON CAP (Q4): it fills none, is never destroyed to make room in one, and is in no summon
	 * skill's list.
	 *
	 * @return the one summoned or refreshed, or null
	 */
	static ACataclysmMinion* SummonTyrantsMinion(AActor* Commander, bool bLostWasTyrantsMinion,
												 const FVector& Where);

	/**
	 * Warlord's Will's six-piece bonus: "When you take damage, you have a 10% chance to summon a "Warhound" to fight for
	 * you for a short duration. The Warhound taunts enemies and deals damage based on your armor". Owner's ruling #6 of
	 * 2026-10-17 and the coordinating session's rulings of 2026-10-18 (Q1-Q6); `docs/DECISIONS.md` carries both.
	 *
	 * FOUR STATS, EACH READ OFF THE WEARER WITH NO TAGS AND NO GAMEPLAY ATTRIBUTE OR BASE: the percent chance a blow
	 * taken summons one (Q4: 10), the seconds it lasts (Q3: 10), the percent of the wearer's armour each of its hits
	 * deals (Q5: 20), and the seconds each hit taunts the enemy struck (#6: 3). The two 10s and the 20 are numbers under
	 * the owner's delegation. Any of the first two at nought summons nothing.
	 */
	static const TCHAR* WarhoundChanceStat;
	static const TCHAR* WarhoundSecondsStat;
	static const TCHAR* WarhoundArmourPercentStat;
	static const TCHAR* WarhoundTauntSecondsStat;

	/**
	 * The minion type a Warhound is made from: its own row, `Warhound` in `game/Data/MinionTypes.csv`, from the design
	 * workbook's Minion Types sheet. Ruled 2026-10-18 (A) under the owner's delegation: twice the Imp's health, no
	 * damage of its own (its blow is a share of its summoner's armour), every other cell the Imp's.
	 */
	static const TCHAR* WarhoundTypeName;

	/**
	 * How long after a blow summons or refreshes a Warhound the next blow may not (Q4: "with its 0.25 s trigger
	 * cooldown"). The quarter second every hit-fired row waits, `DEFAULT_TRIGGER_COOLDOWN` in
	 * `tools/generate_datatables.py`; `tools/tests/test_warlords_will_six_rows.py` compares the two.
	 */
	static constexpr float WarhoundSummonCooldownSeconds = 0.25f;

	/**
	 * True for a Warhound. WHAT TELLS IT FROM AN IMP, since it is made from the Imp's row: its blow is a share of its
	 * summoner's armour and taunts what it strikes (`AttackTarget`), and the replacement paths are not asked at its
	 * death (Q3).
	 */
	bool bIsWarhound = false;

	/** The Warhound `Summoner` has standing, alive, or null. At most one stands (Q3). */
	static ACataclysmMinion* WarhoundOf(const AActor* Summoner);

	/**
	 * Summon `Summoner`'s Warhound beside it, or refresh the one standing, as `SummonTyrantsMinion` does a Tyrant's
	 * Minion (Q3): for the summoner's `WarhoundSecondsStat` raised by `minion_duration`, at most one, a new summon
	 * restarting the standing one's time at the full length and keeping its health, outside every summon cap.
	 * Nothing for a summoner that is dead or holds the seconds at nought.
	 *
	 * @return the one summoned or refreshed, or null
	 */
	static ACataclysmMinion* SummonWarhound(AActor* Summoner);

	/**
	 * A blow `Struck` has taken may summon its Warhound (Q4): called by `UCataclysmAbilitySystemComponent::
	 * NoteHitTaken`, beside the event `hit_taken`, for a landed blow that is not a tick of damage over time. Rolls the
	 * struck character's `WarhoundChanceStat`; a summon or a refresh starts `WarhoundSummonCooldownSeconds`, within
	 * which no blow rolls. A roll that fails starts no wait. `Cataclysm.WarhoundRoll` pins the roll for tests.
	 *
	 * @return the one summoned or refreshed, or null
	 */
	static ACataclysmMinion* RollForWarhoundOnHitTaken(AActor* Struck);

	/**
	 * What one of this Warhound's hits is worth: its summoner's armour, asked as the armour nova asks it, times the
	 * summoner's `WarhoundArmourPercentStat` (Q5). Nought for a minion that is not a Warhound and for a summoner with
	 * no ability system.
	 */
	float WarhoundBlow() const;

	/**
	 * Tyrant's Chains' ten-piece bonus: "Your Tyrant's Minion now casts your ultimate ability every 3 seconds". The
	 * coordinating session's ruling #7 of 2026-10-17 (a FREE START, as the skill gadget's) and its rulings T1-T7 of
	 * 2026-10-18; `docs/DECISIONS.md` carries both.
	 *
	 * ONE STAT, THE SECONDS BETWEEN CASTS, READ OFF THE COMMANDER WITH NO TAGS (T6: 3, the sentence's own number). No
	 * gameplay attribute and no base. Nought means the Tyrant's Minion casts nothing (T7).
	 */
	static const TCHAR* TyrantsMinionCastsUltimateEverySecondsStat;

	/** The seconds between this Tyrant's Minion's casts: its commander's stat above. Nought for any other minion. */
	float TyrantsUltimateSecondsBetweenCasts() const;

	/**
	 * What a Tyrant's Minion does for the ten-piece on the step every character takes four times a second, called by
	 * `ACataclysmCharacterBase` beside `GadgetStep`. Nothing for anything that is not a Tyrant's Minion, and nothing
	 * for one whose commander holds the stat at nought.
	 *
	 * THE KEYED SKILL GADGET'S CLOCK (T6), at the row's seconds, FIXED: the first cast on the first step that casts one,
	 * each later cast on the first step at least the seconds after the last that started, and a step that casts
	 * nothing starts nothing and waits. NOT SHORTENED by an Ultimate cooldown row or `minion_attack_speed`: neither is
	 * read. A cast can come up to a quarter of a second late, as a skill gadget's can.
	 *
	 * @return whether a cast started on this step
	 */
	static bool TyrantsMinionStep(AActor* Character);

	/**
	 * Cast the commander's Ultimate once, now, from this Tyrant's Minion's place. T1-T5, T7.
	 *
	 * THE SKILL IS WHAT THE COMMANDER HOLDS IN `ECataclysmAbilitySlot::Ultimate` NOW (T2), refused as
	 * `SkillGadgetMayCast` refuses (a shapeless skill, a movement skill, an aura, a summon or deployable, a held or
	 * channelled skill, a planted weapon, a `Requires`, a health cost). AN ULTIMATE CONVERTED INTO A SKILL GADGET IS
	 * STILL CAST (T3). A SELF BUFF follows the skill gadget's S13 (T4): granted only while the commander stands within
	 * `SkillGadgetSelfBuffReachCm` of this minion, and not while one this minion granted still runs. EVERY OTHER SHAPE
	 * is aimed at the nearest living enemy within `SkillGadgetReachCm` of this minion (T5); no target, no cast.
	 *
	 * THE COMMANDER'S OWN ROW AT THE COMMANDER'S NUMBERS (T1): no tag is added, so no gadget or minion row reaches it,
	 * and `SkillGadgetCasting` answers null for it, because this minion is no skill gadget. A free start through
	 * `StartPlayersSkillFromHere`, the skill gadget's own path: no cost, no cooldown, no `skill_use` or `attack_use`.
	 *
	 * @return whether it started. False, casting nothing, for anything that is not a Tyrant's Minion, one dead or
	 *         unable to act, a commander gone or dead, a slot holding no skill or one refused above, no target, or the
	 *         skill refusing itself.
	 */
	bool CastPlayersUltimateFromHere();

	/**
	 * THE PART OF A CAST FROM THIS MINION'S PLACE THAT A SKILL GADGET AND A TYRANT'S MINION SHARE, factored out of
	 * `CastPlayersSkillFromHere` for the ten-piece: `Skill`, which the caller has chosen and refused, started for
	 * `Player` free from here. A self buff is granted only while `Player` stands within `SkillGadgetSelfBuffReachCm`
	 * and not while one this minion granted still runs; every other shape is aimed at `MostHealthWithin(SkillReachCm)` (a
	 * skill gadget's highest-health row; null for anything that is not a gadget) or else the nearest enemy within
	 * `SkillReachCm`. A reach of nought casts nothing. The caller adds any tag; this adds none.
	 *
	 * WIDENED FOR THE SKILL TRAP (ruled 2026-10-19, M5): `AimedAt`, when given, is the target, and no search is made and
	 * no reach is asked; `CastFromPlace`, when given, is where the skill is cast from, and with no target it is also where
	 * the skill is aimed (T11). Both null, as every caller before the trap passes, is the search above from here. A self
	 * buff's 8 m is measured from this minion either way.
	 *
	 * @return whether `UCataclysmTriggeredSkill::Trigger` started it
	 */
	bool StartPlayersSkillFromHere(AActor* Player, const FCataclysmWeaponSkill& Skill, float SkillReachCm,
								   AActor* AimedAt = nullptr, const FVector* CastFromPlace = nullptr);

	/** How many Ultimates this Tyrant's Minion has cast for the ten-piece. Read by tests. */
	int32 TyrantsUltimateCastsMade = 0;

	/** Whether it has cast for the ten-piece. Until it has, its clock is not running. */
	bool bTyrantsUltimateHasCast = false;

	/** Its age when its last cast for the ten-piece started. */
	float LastTyrantsUltimateCastAtAgeSeconds = 0.0f;

	/**
	 * THE SKILL TRAP. Ruled 2026-10-19 by the coordinating session (T1-T19), for the rows "Your heavy attack / special
	 * ability / ultimate ability is converted into a placeable trap. When triggered, it fires your ... at (or centered
	 * on) the triggering enemy. Scales off trap damage. All placed copies share the ... cooldown." The shape copies the
	 * skill gadget's: a converted slot's press PLACES the trap at the player's feet (T3), PAYING THE SKILL'S COST AND
	 * STARTING ITS COOLDOWN (T5); the press is the skill use and the trap's cast is not (T19).
	 *
	 * A MACHINE OF ITS OWN TYPE ROW, `SkillTrapTypeName`, whose tags make it a trap (`IsTrap`) and not a gadget: so no
	 * gadget behaviour reaches it, the skill gadget's step and pricing included. It ARMS after `SkillTrapArmSeconds`
	 * (T1), TRIGGERS on the first enemy within `SkillTrapTriggerRadiusCm` (T2) on the quarter-second step, CASTS the
	 * player's skill held in its slot once, free (M5), and is USED UP: destroyed, with no explosion and no
	 * `gadget_destroyed` (T14). It lasts `SkillTrapSeconds` and A TRAP THAT EXPIRES DOES NOT TRIGGER (T4). At most
	 * `SkillTrapsMostPerPlayer` stand for one player across all converted slots, and over the cap the oldest is
	 * destroyed without triggering (T6). 1.0 s, 3 m, 20 s, 5 and the row's health of 40 are numbers under the owner's
	 * delegation of unstated numbers.
	 *
	 * ITS TYPE ROW IS THE ENCHANTMENT SESSION'S, `SkillTrap` in `game/Data/MinionTypes.csv`. Without it a press places
	 * nothing and logs why, as a skill gadget's does. NOTHING HERE IS FOR IRON FORTRESS'S `SpikeTrap` (T15): every rule
	 * below asks `IsSkillTrap`, which a Spike Trap never is.
	 */
	static const TCHAR* SkillTrapTypeName;

	/** Seconds from placing to armed, before `TrapArmingSpeedStat` (T1). */
	static constexpr float SkillTrapArmSeconds = 1.0f;

	/** How near an enemy must stand for an armed trap to trigger, in centimetres, before `TrapTriggerRadiusStat` (T2). */
	static constexpr float SkillTrapTriggerRadiusCm = 300.0f;

	/** Seconds a trap lasts, before the summoner's `minion_duration` rows that name its tags (T4). */
	static constexpr float SkillTrapSeconds = 20.0f;

	/** The most skill traps one player has standing, across every converted slot (T6). */
	static constexpr int32 SkillTrapsMostPerPlayer = 5;

	/**
	 * "Traps arm 30%-60% faster" and "Traps have 30%-50% increased trigger radius": two increased stats of the player,
	 * asked with the trap's type tags when it is placed and on each step. No gameplay attribute and no base. Read here
	 * from this layer; their rows are a later layer's.
	 */
	static const TCHAR* TrapArmingSpeedStat;
	static const TCHAR* TrapTriggerRadiusStat;

	/** `SkillTrapArmSeconds` for `Player`'s trap of `TrapTags`: divided by one plus the arming-speed increases. */
	static float SkillTrapArmingSecondsFor(const AActor* Player, const FGameplayTagContainer& TrapTags);

	/** `SkillTrapTriggerRadiusCm` for `Player`'s trap of `TrapTags`: times one plus the trigger-radius increases. */
	static float SkillTrapTriggerRadiusCmFor(const AActor* Player, const FGameplayTagContainer& TrapTags);

	/**
	 * Place a skill trap for `Player`'s `Slot` at `Location`, or null with the reason logged. NULL, AND NOTHING LEFT
	 * STANDING AND NOTHING DESTROYED, WHEN THE TYPE ROW IS MISSING, as `PlaceSkillGadget` refuses. Over the cap the
	 * player's oldest skill traps are destroyed without triggering (T6). Raises `trap_placed` on the player with the
	 * trap's type tags. Free: the press that calls this is what pays.
	 */
	static ACataclysmMinion* PlaceSkillTrap(AActor* Player, ECataclysmAbilitySlot Slot, const FVector& Location);

	/** Every living skill trap `Player` commands, the oldest first by `SpawnOrder`. */
	static TArray<ACataclysmMinion*> SkillTrapsOf(const AActor* Player);

	/** Which of its player's slots this trap casts, or `None` for every minion that is not a skill trap. */
	ECataclysmAbilitySlot SkillTrapSlot = ECataclysmAbilitySlot::None;

	/** Whether this is a skill trap: a trap placed for a slot. Never a Spike Trap (T15). */
	bool IsSkillTrap() const;

	/**
	 * Whether a trap of `Slot` casts its skill FROM the triggering enemy's place, "centered on the triggering enemy":
	 * the Special, the Support and the Ultimate. The Heavy and the basic attack "fire ... at the nearest enemy", and are
	 * cast from the trap at the nearest living enemy when it triggers (`SkillTrapFiresAtTheNearestEnemy`).
	 */
	static bool SkillTrapCastsAroundItsEnemy(ECataclysmAbilitySlot Slot);

	/** The row of the skill this trap casts: what its player holds in its slot NOW, as `SkillGadgetsSkill` reads it. */
	bool SkillTrapsSkill(FCataclysmWeaponSkill& Out) const;

	/**
	 * What this trap does on the quarter-second step, called by `GadgetStep` before its refusal of anything that is not
	 * a gadget. ARMED once its age reaches `SkillTrapArmsAtAgeSeconds`; then, on the first step finding an enemy within
	 * `SkillTrapTriggerRadiusCmFor`, the nearest is the triggering enemy and `TriggerSkillTrap` is called. A trap that
	 * cannot act, of a player gone or dead, or whose slot is no longer converted, does nothing. Up to a quarter of a
	 * second late, as every gadget clock is.
	 *
	 * @return whether the trap triggered on this step
	 */
	bool StepSkillTrap();

	/**
	 * Trigger this trap now at `TriggeringEnemy` (T13), or at its own place when that is null (T11): cast its player's
	 * skill once, free, by `StartPlayersSkillFromHere` with `Type.Trap` and not `Type.Deployable` added to its tags; at
	 * the nearest living enemy from here for the Heavy and the basic attack (`SkillTrapNearestEnemyTo`, ruled
	 * 2026-10-21), from the enemy's place for `SkillTrapCastsAroundItsEnemy`. A SKILL THAT CANNOT
	 * BE CAST NEVER TRIGGERS (T8): the trap stays armed and is not used up. A cast that starts raises `trap_triggered`
	 * on the player with the trap's type tags and the enemy, and THEN the trap is used up: destroyed, with no explosion
	 * and no `gadget_destroyed`.
	 *
	 * @return whether the cast started and the trap was used up
	 */
	bool TriggerSkillTrap(AActor* TriggeringEnemy);

	/** Its age at which it arms, set when it is placed. */
	float SkillTrapArmsAtAgeSeconds = 0.0f;

	/** Whether it is armed. */
	bool bSkillTrapArmed = false;

	/** How many times it has triggered, at most once, since it is then used up. Read by tests before that. */
	int32 SkillTrapTriggersMade = 0;

	/**
	 * The skill trap whose cast `Delivery` is a blow of, or null, read as `SkillGadgetCasting` reads a gadget: the actor
	 * the delivery carries, then its skill's `FreeRepeatCastBy`. Read by `UCataclysmSkillEffects::ApplyHit` to price
	 * the blow with the player's `Type.Trap` `minion_damage` rows (T10). A TRAP IS USED UP AS ITS CAST STARTS, so a
	 * blow landing after that (a shot in flight) finds no trap and is priced as the player's.
	 */
	static const ACataclysmMinion* SkillTrapCasting(const struct FCataclysmHitDelivery& Delivery);
	static const ACataclysmMinion* SkillTrapCasting(const class UGameplayAbility* Skill);

	/**
	 * WHAT THE PLAYER SEES OF IT (T12): its stand-in body DIM WHILE ARMING AND LIT WHEN ARMED, through the engine basic
	 * shape material's `Color` parameter. Its trigger radius is not drawn. The colour each state is drawn in, and the
	 * one this trap was last given, which tests read because a test world draws nothing.
	 */
	static FLinearColor SkillTrapTintFor(bool bArmed);
	FLinearColor SkillTrapTintShown = FLinearColor::Black;

	/** Give the stand-in body the colour of the state this trap is in now. */
	void ShowSkillTrapTint();

	/**
	 * TRAPS LAYER 2 (ruled 2026-10-21, L3, and the heavy trap's aim on the same day): whether a trap of `Slot` FIRES AT
	 * THE NEAREST LIVING ENEMY WHEN IT TRIGGERS. "When triggered, it fires your basic attack at the nearest enemy" and
	 * "When triggered, it fires your heavy attack at the nearest enemy": the same words, the same reading, so the basic
	 * attack and the Heavy. The Special, the Support and the Ultimate stay on the triggering enemy, "centered on the
	 * triggering enemy" (`SkillTrapCastsAroundItsEnemy`).
	 */
	static bool SkillTrapFiresAtTheNearestEnemy(ECataclysmAbilitySlot Slot);

	/**
	 * The nearest living enemy to this trap now, for a trap set off by `TriggeringEnemy`, nearest first as
	 * `HostilesWithin` answers. SEARCHED NO FARTHER THAN THAT ENEMY STANDS: anything nearer stands within that distance.
	 * Through `StepSkillTrap` the two are always one enemy, since the step's triggering enemy is already the nearest within
	 * the trigger radius; they differ only for a trap set off by an enemy named some other way. THE TRIGGERING ENEMY
	 * ITSELF when the search finds nothing living, and null for null (T11).
	 */
	AActor* SkillTrapNearestEnemyTo(AActor* TriggeringEnemy) const;

	/**
	 * TRAPS LAYER 3 (ruled 2026-10-21, M1 and M2): the two reads layer 1 left. "Your trap skills get +1-6 to max trap
	 * count" is `TrapCapBonusStat`, a flat bonus added to `SkillTrapsMostPerPlayer`: ONE CAP ACROSS EVERY CONVERTED SLOT
	 * (M1 (a)), which `PlaceSkillTrap` counts and the skill bar shows as "Traps N/M". "Traps do not expire, they remain
	 * armed until triggered" is `TrapsDoNotExpireStat`, a flag: A TRAP PLACED WHILE IT IS HELD HAS NO LIFE SPAN (M2 (a)),
	 * taken at placing, and the cap still destroys the oldest. Both are the player's, asked with `Type.Trap` alone, so
	 * a row requiring nothing or `Type.Trap` reaches them. No gameplay attribute and no base.
	 */
	static const TCHAR* TrapCapBonusStat;
	static const TCHAR* TrapsDoNotExpireStat;

	/** The most skill traps `Player` may have standing: `SkillTrapsMostPerPlayer` plus the cap bonus, rounded, at least 1. */
	static int32 SkillTrapsMostFor(const AActor* Player);

	/** Whether a skill trap `Player` places now has no life span: the no-expire flag, read as above nought. */
	static bool SkillTrapsDoNotExpireFor(const AActor* Player);

	/**
	 * WHAT THIS TRAP'S CAST TOOK FROM THE ENEMY THAT SET IT OFF, for the slow's rule of a tenth. The owner, 2026-10-21:
	 * "Follow the tenth rule" — "The slow lands only if the trap's cast then hits that same enemy for at least a tenth of
	 * its maximum health." `TriggerSkillTrap` names the enemy and clears the figure before the cast starts;
	 * `UCataclysmSkillEffects::ApplyHit` keeps the largest blow's `DealtToHealth` on that enemy while the cast is the
	 * trap's (`NoteSkillTrapBlowOn`); `trap_triggered` then carries it as its amount. A blow landing after the trap is
	 * used up (a shot in flight) finds no trap and is not counted, so such a cast slows nobody.
	 */
	TWeakObjectPtr<AActor> SkillTrapTriggeringEnemy;
	mutable float SkillTrapTookFromItsEnemy = 0.0f;

	/** Keep `DealtToHealth` if `Struck` is the enemy that set this trap off and the blow is the largest on it so far. */
	void NoteSkillTrapBlowOn(const AActor* Struck, float DealtToHealth) const;

	/**
	 * The chaos pool a killed minion leaves, where its commander carries `minions_leave_chaos_pools`: its radius in
	 * centimetres, how long it lasts in seconds, and the percent of the dead minion's own blow a sweep deals. Ruled
	 * 2026-10-06 by the coordinating session under the owner's delegation; the row's sentence states none of them.
	 *
	 * 3 METRES IS Summon Imp's `Radius`. 4 SECONDS IS the commonest `GroundDuration` among the rows that leave
	 * ground. 10 IS the share a row's zone deals, `UCataclysmSkillTemplate::RowZonePercentPerSweep`, which is the
	 * smallest an authored row states.
	 */
	static constexpr float ChaosPoolRadiusCm = 300.0f;
	static constexpr float ChaosPoolSeconds = 4.0f;
	static constexpr float ChaosPoolPercentOfOwnBlow = 10.0f;

	/**
	 * Leave this minion's chaos pool where it stands, if its commander's row says so and it is a minion by its
	 * type's tags. Called by `HandleDeath`, which is the one way a minion is killed.
	 *
	 * NOT CALLED FOR A MINION THAT IS REMOVED WITHOUT BEING KILLED. One whose time ran out, one the summon cap
	 * removed to make room and one a closing rift took are destroyed without `HandleDeath`, and leave nothing.
	 *
	 * THE POOL IS ITS COMMANDER'S, so every row of the commander's that a zone reads reaches it, asked with the
	 * dead minion's type tags. It deals Chaos damage.
	 *
	 * @return the pool, or null when none was left
	 */
	ACataclysmGroundZone* LeaveChaosPool();

	/**
	 * Remember what the skill that summoned this one states its explosion is,
	 * so a death can use the figures the summon cap already uses.
	 *
	 * TOLD AT THE SUMMONING RATHER THAN READ AT THE DEATH. The radius and the
	 * damage are the skill row's, and a dying minion has no way back to the
	 * ability that made it. `Ritualist_keystone_b_kB` Every One Bursts says
	 * "with the radius and damage of the skill that brought it", so the two
	 * figures have to travel with the minion. Issue #1515.
	 *
	 * A MINION NOBODY TOLD KEEPS ZEROES AND NEVER EXPLODES ON ITS DEATH, which
	 * is the deployable shape's case: `UCataclysmDeployableSkill` states no
	 * explosion radius, so a ballista dying leaves a body exactly as it does
	 * now.
	 *
	 * ONLY THE RADIUS COMES FROM THE SKILL, SINCE ISSUE #1515. What the
	 * explosion is worth is the minion's own, taken from its type row at the
	 * summoning, so nothing about a minion's blow reads the summoner's weapon.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Minion")
	void RecordExplosionRadius(float RadiusCm);

	/** Hit the nearest enemy in reach. Called by tests. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Minion")
	void AttackOnce();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/**
	 * This one's own numbers, taken from its type's row when it was given one.
	 *
	 * PER INSTANCE RATHER THAN PER CLASS, WHICH IS THE WHOLE POINT. A ballista
	 * and a spike trap are both this class and must not share a reach or a
	 * firing rate. They start at the defaults above so a minion spawned without
	 * a type behaves exactly as every minion did before issue #622.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	float ReachCm = DefaultReachCm;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	float NoticeRadiusCm = DefaultNoticeRadiusCm;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	float AttackIntervalSeconds = DefaultAttackIntervalSeconds;

	/**
	 * What this one's death explodes: how wide, and what it is worth.
	 *
	 * THE RADIUS IS THE SUMMONING SKILL'S and arrives through
	 * `RecordExplosionRadius`; the share is this kind's own, read from
	 * `game/Data/MinionTypes.csv` at the summoning. Both are zero until
	 * something says otherwise, and a zero in either refuses the explosion
	 * rather than making a silent one of no size.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	float ExplosionRadiusCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	float ExplosionPercentOfOwnDamage = 0.0f;

	/**
	 * Whether it goes to its target or stays where it was put.
	 *
	 * READ OFF THE TYPE'S MOVE SPEED RATHER THAN SET BY THE SKILL. A bolt
	 * turret, a ballista and a spike trap all state a move speed of zero in
	 * `game/Data/MinionTypes.csv`, which is what makes a deployable a thing that
	 * stays put; an imp states 4.4 and a mote 5.5. So the difference between the
	 * Summon shape and the Deployable shape is where the thing is placed, and
	 * whether it then walks is a property of what was placed.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Minion")
	bool bStaysWhereItIsPut = false;

	//~ Driven by ACataclysmEnemyController
	virtual float AttackReachCm() const override { return ReachCm; }
	virtual float SightRadiusCm() const override { return NoticeRadiusCm; }
	virtual float SecondsBetweenAttacks() const override { return AttackIntervalSeconds; }

	/**
	 * A DEPLOYED GADGET NEVER WALKS, AND THIS IS WHAT SAYS SO TO THE BRAIN.
	 * `bStaysWhereItIsPut` was computed by `Spawn` from the type's move speed
	 * and read by nothing at all until issue #1517 gave a minion something to
	 * walk toward.
	 */
	virtual bool StaysWhereItIsPut() const override { return bStaysWhereItIsPut; }
	virtual void AttackTarget(AActor* Target) override;

	/**
	 * Record that this minion has died. Issue #1518.
	 *
	 * WITHOUT THIS A MINION WAS NEVER RECORDED AS DEAD AT ALL.
	 * `ACataclysmCharacterBase::HandleDeath` is empty, the enemy and player
	 * classes override it, and this one did not -- so a minion whose health
	 * reached zero ran the inert base, never took the Dead tag, and stayed in
	 * every list that asks whether it is alive.
	 *
	 * WHAT THAT BROKE, WHICH IS WHY IT IS FIXED HERE. Both halves of the
	 * Ritualist's generator ask about the minions a character commands, and
	 * `UCataclysmCommand::ThingsCommandedBy` drops a follower by asking
	 * `UCataclysmSkillEffects::IsDead`. An imp at zero health answered "alive",
	 * so it went on generating Fervour, and
	 * `UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero` -- which uses
	 * the same tag to run a death once -- would have granted the death bonus
	 * again on every later write to a corpse's health.
	 *
	 * AND IT TAKES THE BODY AWAY. Issue #1528. A minion that explodes is
	 * destroyed by the explosion, as before. One that dies quietly, a gadget
	 * included, stops its movement and its collision at once and is
	 * removed `DeadBodySeconds` later. Only the first death does any of this;
	 * a second call returns at once.
	 */
	virtual void HandleDeath() override;

	/**
	 * How long a minion that died without exploding stays before it is
	 * removed. Issue #1528, ruled 2026-09-30. A judgement: long enough for the
	 * killing blow's impact and damage number to be drawn on it.
	 */
	static constexpr float DeadBodySeconds = 0.5f;

	/**
	 * Whether this minion's death will be an explosion. Issue #1515.
	 *
	 * THE SAME TEST `HandleDeath` MAKES, stated once so a caller can ask it
	 * BEFORE the death: the death hook for Press-Ganged and Rekindled has to
	 * know whether the minion it is replacing died quietly or exploded, and an
	 * exploded minion has been destroyed by the time `HandleDeath` returns.
	 */
	bool ExplodesOnDeath() const;

	/**
	 * The summon skill that made this minion, or null. Issue #1515.
	 *
	 * A REPLACEMENT IS THE KIND THIS SKILL MAKES, ruled 2026-09-23 under the
	 * owner's delegation: Press-Ganged and Rekindled summon what the summon skill
	 * that made the lost minion summons. Weak, because the skill belongs to the
	 * summoner and can go before the minion does.
	 */
	TWeakObjectPtr<UCataclysmSummonSkill> SummonedBy;
	//~ End

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Minion")
	TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UCataclysmVitalAttributeSet> VitalAttributes;
};
