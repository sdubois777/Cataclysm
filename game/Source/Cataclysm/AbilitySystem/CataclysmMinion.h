// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmCharacterBase.h"
#include "CataclysmMinion.generated.h"

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
	 * IT MARKS AND DOES NOT REMOVE. The actor still goes away on the lifespan
	 * `Spawn` gave it, exactly as before, so nothing about spawning or the
	 * summon cap changes. A dead minion's body staying in the level until then
	 * is a separate fault and is issue #1528, which lists what
	 * `ACataclysmEnemyCharacter::HandleDeath` does that a minion may or may not
	 * want -- dropping loot and writing the run record are two it should not.
	 */
	virtual void HandleDeath() override;

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
