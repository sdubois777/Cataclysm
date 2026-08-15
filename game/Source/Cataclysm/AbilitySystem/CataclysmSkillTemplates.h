// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "CataclysmSkillTemplates.generated.h"

class ACataclysmMinion;
class ACataclysmProjectile;

/**
 * The seven shared skill templates.
 *
 * ONE CLASS PER SHAPE. Between them they implement all sixteen of the designed
 * Demonic skills, and adding a seventeenth of an existing shape is a row in
 * docs/All_Things_Cataclysm.xlsx and no C++ at all.
 *
 * | Template   | The designed skills it runs                                |
 * |------------|------------------------------------------------------------|
 * | Strike     | Molten Cleave, Searing Hook, Pyroclasm                      |
 * | Projectile | Emberhurl, Blood Pyre, Infernal Lance                       |
 * | SelfBuff   | Burning Wrath, Martyr's Ember                               |
 * | Movement   | Infernal Plunge, Cinder Rush, Emberstep                     |
 * | Summon     | Summon Imp, Open the Rift                                   |
 * | Aura       | Conflagration, Living Pyre                                  |
 * | Debuff     | Subjugate                                                   |
 */

/**
 * Hits everything in a cone or a ring around the caster.
 *
 * An angle of 360 is a ring, which is what Pyroclasm's spin is. With a Duration
 * and an Interval it repeats for that long, and FinalHitPercent lands once at
 * the end -- Pyroclasm's "final hit at the end of the spin deals 300% weapon
 * damage". With neither it is a single swing.
 */
UCLASS()
class CATACLYSM_API UCataclysmStrikeSkill : public UCataclysmSkillTemplate
{
	GENERATED_BODY()

public:
	virtual ECataclysmSkillShape Shape() const override { return ECataclysmSkillShape::Strike; }

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
								 const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;

	/** One swing. Public so a test can drive it without a timer. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	int32 SwingOnce(float DamagePercent = -1.0f);

	/** How many swings have landed this activation. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 SwingsMade = 0;

private:
	void Repeat();
	void Finish();

	FTimerHandle RepeatTimer;
	FTimerHandle FinishTimer;
};

/**
 * Sends something out from the caster toward where they are aiming.
 *
 * TWO BEHAVIOURS, TOLD APART BY PIERCE, and that is written down rather than
 * inferred at the call site. A projectile that pierces travels along a line and
 * hits what it passes -- Emberhurl "through a group of enemies in a line",
 * Infernal Lance "piercing every enemy in a 12 meter line". One that does not
 * lands at the aim point and hits in a radius there -- Blood Pyre "ignites on
 * impact, dealing damage in a 3 meter radius".
 *
 * A SPEED FIRES A REAL ACTOR THAT OCCUPIES SPACE. `ACataclysmProjectile` moves
 * in steps, sweeps the capsule between where it was and where it now is, and
 * stops or passes through according to Pierce. Before issue #164 a Speed was
 * turned into a delay: the whole hit was resolved after `Range/Speed` seconds
 * using positions at that moment, so nothing occupied the space in between, an
 * enemy could cross the path untouched, and a wall stopped nothing.
 *
 * A SPEED OF ZERO IS STILL A BEAM, resolved at once by Land without any actor.
 * Infernal Lance is written that way and its description says it arrives
 * immediately. Aiming at your own feet resolves the same way, because there is
 * no path to fly along.
 */
UCLASS()
class CATACLYSM_API UCataclysmProjectileSkill : public UCataclysmSkillTemplate
{
	GENERATED_BODY()

public:
	virtual ECataclysmSkillShape Shape() const override { return ECataclysmSkillShape::Projectile; }

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
								 const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;

	/**
	 * Resolve the hit as a beam, with no actor and no flight.
	 *
	 * ONLY FOR A SPEED OF ZERO now, and for a throw with nowhere to go. A skill
	 * with a real speed fires an ACataclysmProjectile instead, and that actor
	 * does its own hitting as it travels.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	int32 Land();

	/** How many times it has landed or a projectile of its has finished. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 Landings = 0;

	/** The projectile in flight, or null when there is none. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	TObjectPtr<ACataclysmProjectile> InFlight;

private:
	void LandThenFinish();
	void Return();

	/** Called when a fired projectile stops. Leaves ground and ends the ability. */
	void OnProjectileFinished(ACataclysmProjectile* Projectile);

	/** Burn the path a projectile took, or the point it stopped at. */
	void LeaveGroundForFlight(const FVector& From, const FVector& To);

	/** Fixed at activation, so aiming elsewhere mid-flight does not move it. */
	FVector Origin = FVector::ZeroVector;
	FVector Destination = FVector::ZeroVector;

	FTimerHandle FlightTimer;
};

/**
 * Grants the caster an effect for a duration.
 *
 * WHAT THIS DOES AND DOES NOT DO, PLAINLY. It applies a gameplay effect to the
 * caster for the written duration and grants a tag naming the skill, so anything
 * asking whether the buff is up gets a true answer and the duration is real.
 * WHERE THE MAGNITUDE GOES. Into the caster's stat modifier list, held by
 * UCataclysmAbilitySystemComponent, and taken out again when the duration
 * expires. Burning Wrath's "4% increased fire damage for every enemy currently
 * burning within 15 meters" becomes one Increased modifier of 4 times the count,
 * scoped to the skill's own Element tag so it reaches Demonic skills and no
 * others. Issue #166.
 *
 * MARTYR'S EMBER IS STILL ONLY A DURATION. "Store 40% of all damage you take and
 * spend it as bonus fire damage on your hits" needs a damage-taken hook and a
 * store that drains as it is spent, neither of which the stat modifier route
 * above provides. Issue #192.
 */
UCLASS()
class CATACLYSM_API UCataclysmSelfBuffSkill : public UCataclysmSkillTemplate
{
	GENERATED_BODY()

public:
	virtual ECataclysmSkillShape Shape() const override { return ECataclysmSkillShape::SelfBuff; }

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
								 const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
							const FGameplayAbilityActorInfo* ActorInfo,
							const FGameplayAbilityActivationInfo ActivationInfo,
							bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Enemies burning within Radius when the buff went up. Burning Wrath scales on it. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 BurningEnemiesAtCast = 0;

	/**
	 * Percentage points of increased damage this buff is currently granting.
	 *
	 * Zero while the buff is down, and zero for a self buff whose row carries no
	 * IncreasePerBurning. Read by tests and by anything that shows the player
	 * what a buff is worth.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	float GrantedIncrease = 0.0f;

	/**
	 * Which tag the granted increase is scoped to, or an invalid tag for none.
	 *
	 * The skill's own Element tag. Burning Wrath carries Element.Demonic, so its
	 * increase reaches Demonic skills; a War self buff written the same way
	 * would scope to Element.War without any code changing.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	FGameplayTag GrantedScope;

private:
	void Finish();

	/** Put the increase on the caster. Does nothing when there is none to grant. */
	void GrantIncrease();

	/** Take it off again. Safe to call when nothing was granted. */
	void RevokeIncrease();

	FTimerHandle FinishTimer;

	/** The caster's handle for the granted modifier. Zero means none is live. */
	int32 IncreaseHandle = 0;
};

/**
 * Moves the caster, and hits according to how it travels.
 *
 * A Leap hits in a radius where it lands. A Charge hits everything along the
 * line it crosses. A Blink hits at both ends and nothing between, which is what
 * Emberstep's "enemies at the point you left and the point you arrive" says.
 *
 * THE MOVE IS INSTANT FOR ALL THREE. Without an animation to play or a mesh to
 * play it on, a leap that took the right amount of time would be a character
 * standing still and then being somewhere else, which is what this is. What is
 * real: where it ends up, what it hits, and where it leaves ground burning.
 */
UCLASS()
class CATACLYSM_API UCataclysmMovementSkill : public UCataclysmSkillTemplate
{
	GENERATED_BODY()

public:
	virtual ECataclysmSkillShape Shape() const override { return ECataclysmSkillShape::Movement; }

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
								 const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;

	/** Where the caster ended up. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	FVector ArrivedAt = FVector::ZeroVector;

	/** How many enemies the move hit. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 EnemiesHit = 0;
};

/**
 * Spawns minions that fight for the caster.
 *
 * TWO PATTERNS, AND BOTH DESIGNED SKILLS USE ONE EACH. Summon Imp spawns Count
 * at once and holds MaxActive, destroying the oldest when a new one would exceed
 * the cap -- "summoning a fourth destroys the oldest, which explodes for damage
 * in a 3 meter radius". Open the Rift has a Duration and an Interval, so it
 * spawns one every Interval up to MaxActive, burns the ground it stands on for
 * that whole time, and at the end deals FinalHitPercent and destroys what it
 * made.
 */
UCLASS()
class CATACLYSM_API UCataclysmSummonSkill : public UCataclysmSkillTemplate
{
	GENERATED_BODY()

public:
	virtual ECataclysmSkillShape Shape() const override { return ECataclysmSkillShape::Summon; }

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
								 const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;

	/** Spawn one. Public so a test can drive the cap without waiting. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	ACataclysmMinion* SummonOne();

	/**
	 * Minions this ability instance is holding, oldest first.
	 *
	 * ON THE ABILITY INSTANCE, WHICH IS WHY IT WORKS. The base class instances
	 * per actor, so one instance stands for one character's Summon Imp across
	 * every use of it, and the cap is per character rather than per press.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	TArray<TObjectPtr<ACataclysmMinion>> Minions;

	/** How many are alive, after dropping the ones that expired. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	int32 LivingMinionCount();

private:
	void SpawnTick();
	void Collapse();

	/** Fixed at activation: a rift does not follow the cursor. */
	FVector RiftLocation = FVector::ZeroVector;

	FTimerHandle SpawnTimer;
	FTimerHandle CollapseTimer;
};

/**
 * Places machines where they are put and leaves them there.
 *
 * THE EIGHTH SHAPE, AND IT EXISTED IN THE DATA BEFORE IT EXISTED HERE. Three War
 * skills name it -- Bolt Turret, Ballista and Iron Fortress -- and until issue
 * #621 the C++ did not know the word, so all three were granted the placeholder
 * ability that fills a slot and does nothing.
 *
 * WHAT MAKES IT DIFFERENT FROM UCataclysmSummonSkill IS LESS THAN IT LOOKS. Two
 * things, and neither of them is movement:
 *
 *   IT PLACES AT AN AIMED POINT rather than at the caster. One with no Range is
 *   placed at the caster's feet, which is what a spike trap laid underfoot is.
 *
 *   IT PLACES MORE THAN ONE KIND AT ONCE. Iron Fortress deploys two ballistae
 *   AND three spike traps. No summoning skill produces two kinds.
 *
 * WHETHER WHAT IT PLACES WALKS IS NOT DECIDED HERE. That is a property of the
 * thing placed: a bolt turret, a ballista and a spike trap all state a move speed
 * of zero in game/Data/MinionTypes.csv, while an imp states 4.4. So "stays where
 * it is put" falls out of the data rather than being a rule this class imposes.
 *
 * NO OLDEST-EXPLODES RULE. That belongs to Summon Imp, whose design says a fourth
 * imp destroys the oldest. Nothing in the three deployable skills says anything
 * of the kind, so reaching the cap here simply stops placing more.
 */
UCLASS()
class CATACLYSM_API UCataclysmDeployableSkill : public UCataclysmSkillTemplate
{
	GENERATED_BODY()

public:
	virtual ECataclysmSkillShape Shape() const override
	{
		return ECataclysmSkillShape::Deployable;
	}

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
								 const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;

	/** Put one of a named type down. Public so a test can drive it directly. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	ACataclysmMinion* DeployOne(const FString& InTypeName);

	/**
	 * What this ability instance has out, oldest first.
	 *
	 * ON THE ABILITY INSTANCE for the same reason the summon skill's list is:
	 * the base class instances per actor, so the cap counts what this character
	 * has out rather than what one press produced.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	TArray<TObjectPtr<ACataclysmMinion>> Deployed;

	/** How many are still out, after dropping the ones that expired. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	int32 LivingDeployedCount();

private:
	/** Fixed at activation: what is put down does not follow the cursor. */
	FVector DeployLocation = FVector::ZeroVector;
};

/**
 * A radius around the caster, held while it is affordable.
 *
 * TOGGLED WHEN IT HAS NO DURATION, TIMED WHEN IT HAS ONE. Conflagration is the
 * aura slot and toggles: pressing it again turns it off, and it drains 20 mana a
 * second until it is switched off or the mana runs out, which issue #36 requires.
 * Living Pyre is an Ultimate with a Duration of 6 seconds and ends on its own.
 */
UCLASS()
class CATACLYSM_API UCataclysmAuraSkill : public UCataclysmSkillTemplate
{
	GENERATED_BODY()

public:
	UCataclysmAuraSkill();

	virtual ECataclysmSkillShape Shape() const override { return ECataclysmSkillShape::Aura; }

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
								 const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
							const FGameplayAbilityActorInfo* ActorInfo,
							const FGameplayAbilityActivationInfo ActivationInfo,
							bool bReplicateEndAbility, bool bWasCancelled) override;

	/**
	 * The key was pressed again while the aura is running. Switch it off.
	 *
	 * THIS IS WHERE A TOGGLE BELONGS, and the first attempt put it in the wrong
	 * place. UCataclysmAbilitySystemComponent::ProcessAbilityInput already knows
	 * the difference: a press on an ability that is not running activates it,
	 * and a press on one that IS running comes here instead. Setting
	 * bRetriggerInstancedAbility and testing a flag in ActivateAbility cannot
	 * work, because the engine ends the running instance itself before
	 * re-activating -- so the flag is always clear by the time the code sees it,
	 * and the second press restarts the aura rather than stopping it.
	 */
	virtual void InputPressed(const FGameplayAbilitySpecHandle Handle,
							  const FGameplayAbilityActorInfo* ActorInfo,
							  const FGameplayAbilityActivationInfo ActivationInfo) override;

	/** One pulse: drain the mana, then burn everything inside. Driven by tests too. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	int32 Pulse();

	/** How many pulses this activation has run. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 Pulses = 0;

	/** True while it is held. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	bool IsHeld() const { return bHeld; }

	/** True when the last end was caused by running out of mana. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	bool bEndedForLackOfMana = false;

private:
	/** Pulse's return value is for tests; a timer can only call a void. */
	void PulseTick();
	void Finish();

	bool bHeld = false;
	FTimerHandle PulseTimer;
	FTimerHandle FinishTimer;
};

/**
 * Applies an effect to enemies at range without necessarily damaging them.
 *
 * Subjugate is the only designed one: "seize an enemy's mind, applying Madness".
 * It grants the target a tag for the duration, and doubles that duration against
 * an enemy that is already burning, which is what the skill says.
 *
 * The effect the tag stands for is not itself implemented -- there is no AI for
 * a maddened enemy to turn on its neighbours with. The tag, the target choice
 * and the duration are real. Issue #163.
 */
UCLASS()
class CATACLYSM_API UCataclysmDebuffSkill : public UCataclysmSkillTemplate
{
	GENERATED_BODY()

public:
	virtual ECataclysmSkillShape Shape() const override { return ECataclysmSkillShape::Debuff; }

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
								 const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;

	/** How long the last application lasted, after any doubling. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	float LastDurationApplied = 0.0f;

	/** How many enemies it took hold of. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 EnemiesAffected = 0;
};
