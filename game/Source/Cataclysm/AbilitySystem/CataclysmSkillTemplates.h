// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "CataclysmSkillTemplates.generated.h"

class ACataclysmMinion;
class ACataclysmPlantedWeapon;
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
 *
 * AND TWO ROWS DO NOT SWING WHEN THE KEY GOES DOWN. A `ChargeTime` draws the
 * swing back and holds it: the Greatsword's Backswing, "draw the greatsword back
 * and hold, release at any time", and its The Whole Weight, "raise the
 * greatsword and hold it for 3 seconds". They are Strikes because what they
 * eventually do is hit everything in a cone, but when that happens is the
 * player's decision rather than the animation's. `BeginTheHold`,
 * `ReleaseTheSwing` and `BreakTheHold` are the whole of it. Issue #1141.
 *
 * AND ONE ROW DOES NOT SWING AT ALL. The Greatsword's Buried Fire states
 * `DisarmsUntilRecalled=1`: "drive the greatsword into the ground and leave it
 * there ... pull it free within 10 seconds to erupt". It is a Strike because
 * what it eventually does is hit everything in a ring, but the ring goes off
 * where the sword is standing and at a moment the player chooses, so the whole
 * of its behaviour is `PlantTheWeapon` and `PullTheWeaponFree` below rather than
 * `SwingOnce`.
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

	/**
	 * The key was pressed again while the sword is still in the ground. Pull it
	 * free.
	 *
	 * THE SECOND PRESS ARRIVES HERE AND NOT AS A FRESH ACTIVATION, AND THAT IS
	 * WHAT MAKES THE ROW'S TEN SECONDS TEN SECONDS. The Special slot waits five
	 * seconds between uses, so a second press routed through
	 * `TryActivateAbility` would be refused by the engine's own cooldown check
	 * for the first half of the window -- "pull it free within 10 seconds" would
	 * mean "between 5 and 10". `UCataclysmAbilitySystemComponent::
	 * ProcessAbilityInput` already tells the two cases apart: a press on an
	 * ability that is not running activates it, and a press on one that IS
	 * running comes here. So the skill stays active for as long as the sword
	 * stands.
	 *
	 * THE DAGGER'S ECHO SOLVES THE SAME PROBLEM THE OTHER WAY ROUND AND COULD
	 * NOT HERE. `Mode=Recall` leaves its mark before `CommitAndBegin`, so the
	 * first press is free and the second one pays. That works because leaving a
	 * mark moves nobody and burns nothing. Planting a sword sets ten seconds of
	 * ground alight, so a free first press would be ten seconds of area denial
	 * for nothing, repeatable as fast as the key can be pressed.
	 *
	 * AND IT NEEDS A RELEASE FIRST, WHICH IS THE WHOLE OF ISSUE #1114 AGAIN.
	 * `ACataclysmPlayerController::Input_AbilitySlotPressed` is bound to
	 * `ETriggerEvent::Triggered`, which fires every frame the key is held.
	 * Without waiting for a release, holding the Special key would plant the
	 * sword on one frame and pull it straight back out on the next.
	 *
	 * A HELD SWING IGNORES THIS ENTIRELY, and that is the same fact read the
	 * other way round. Every frame the key stays down arrives here, so a hold
	 * that acted on a press would release on the frame after it began.
	 */
	virtual void InputPressed(const FGameplayAbilitySpecHandle Handle,
							  const FGameplayAbilityActorInfo* ActorInfo,
							  const FGameplayAbilityActivationInfo ActivationInfo) override;

	/**
	 * The key came up.
	 *
	 * FOR A PLANTED SWORD, nothing happens except arming the second press above.
	 *
	 * FOR A HELD SWING, this is what lets it go -- but only for a row that
	 * states a `MinDamagePercent`, because that floor is what an early release
	 * lands for and a row without one has no answer for being let go early. See
	 * `ChargedDamagePercent`.
	 */
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle,
							   const FGameplayAbilityActorInfo* ActorInfo,
							   const FGameplayAbilityActivationInfo ActivationInfo) override;

	/**
	 * Whatever ends this skill takes the sword out of the ground with it.
	 *
	 * THE ONE PLACE THE SWORD IS DESTROYED, so a character cannot be left
	 * fighting unarmed by a skill that ended for a reason nobody thought of --
	 * dying during the ten seconds, a cancel, or the window running out. The
	 * eruption happens before this and separately; reaching here with a sword
	 * still standing means it was never pulled free, and the row's own answer to
	 * that is below.
	 */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
							const FGameplayAbilityActorInfo* ActorInfo,
							const FGameplayAbilityActivationInfo ActivationInfo,
							bool bReplicateEndAbility, bool bWasCancelled) override;

	/** One swing. Public so a test can drive it without a timer. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	int32 SwingOnce(float DamagePercent = -1.0f);

	/**
	 * Drive the weapon into the ground and leave it standing. Public so a test
	 * can drive it without waiting for a swing.
	 *
	 * @return the sword, or null if it could not be planted
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	ACataclysmPlantedWeapon* PlantTheWeapon();

	/**
	 * Pull it free: erupt where it stands, then end. Public so a test can drive
	 * it without pressing a key.
	 *
	 * "ERUPT FOR DAMAGE THAT RISES WITH HOW LONG YOU LEFT IT", which is the row's
	 * `MoreDamagePer=12` beside `ScalingSource=Second`. The seconds are counted
	 * by the sword rather than by this skill, because the sword is what has been
	 * standing there; `UCataclysmSkillTemplate::ScalingUnits` asks it.
	 *
	 * AT THE SWORD AND NOT AT THE PLAYER, which is what makes the ten seconds a
	 * decision rather than a delay. Walking away and pressing again erupts where
	 * the sword is.
	 *
	 * @return how many enemies the eruption caught
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	int32 PullTheWeaponFree();

	/**
	 * The ten seconds ran out and the sword was never pulled free.
	 *
	 * IT COMES BACK AND NOTHING ERUPTS. The row leaves this open -- it says "pull
	 * it free within 10 seconds to erupt" and does not say what a player who does
	 * not gets -- and this is the reading recorded in `docs/DECISIONS.md` on
	 * 2026-09-02. It is the same reading `Mode=Recall` took for a return that is
	 * refused: the skill was paid for, the burning ground it left was the payment
	 * earned, and the eruption is what the second press buys. The alternative,
	 * fighting unarmed for ever on a mistimed press, is a worse outcome than any
	 * the design asks for.
	 *
	 * Public so a test can drive it without a world that ticks.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	void LetTheWindowClose();

	// ----------------------------------------------------------------------
	// Backswing and The Whole Weight -- the two Strikes that are held
	// ----------------------------------------------------------------------

	/**
	 * Whether this skill is drawn back with its swing not yet released.
	 *
	 * A ROW WITH A `ChargeTime` AND NO OTHER, which is two rows in the whole
	 * sheet. Every other Strike swings in the frame it activates.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	bool IsHoldingASwing() const { return bHolding; }

	/**
	 * The held swing this character is drawing back, or null when there is none.
	 *
	 * THE SAME SHAPE AS `UCataclysmMovementSkill::RunningAdvanceOn`, and for the
	 * same reason: a skill that lasts IS an active ability for as long as it
	 * lasts, so asking the running abilities beats registering and
	 * unregistering something that then has to be cleaned up on death.
	 */
	static UCataclysmStrikeSkill* HeldSwingOn(const AActor* Who);

	/**
	 * Whether a swing is being held on this character.
	 *
	 * READ BY THE MOVEMENT GATE. Both rows say the caster is rooted --
	 * Backswing "you cannot move while holding", The Whole Weight "you cannot
	 * move, act or be healed" -- and
	 * `ACataclysmPlayerController::PawnCannotWalk` is where every movement site
	 * in the project asks one question about whether a step is allowed.
	 */
	static bool IsHoldingASwing(const AActor* Who);

	/**
	 * What the swing lands for, as percent of weapon damage, for a hold of this
	 * length.
	 *
	 * TWO ROWS, TWO SHAPES, AND `MinDamagePercent` IS WHAT TELLS THEM APART.
	 *
	 *   A ROW STATING A FLOOR RAMPS WITH TIME AND MAY BE LET GO EARLY.
	 *   Backswing: "release at any time: the swing lands for 175% weapon damage
	 *   at once, rising to 350% if you hold the full 2 seconds". So the percent
	 *   runs from `MinDamagePercent` to `MaxDamagePercent` across `ChargeTime`.
	 *
	 *   A ROW STATING NONE CANNOT BE LET GO EARLY AND DOES NOT RAMP WITH TIME.
	 *   The Whole Weight: "raise the greatsword and hold it for 3 seconds", and
	 *   the only escape its text names is being killed. What raises its blow is
	 *   `MoreDamagePer=8` beside `ScalingSource=HitTaken`, which is a count of
	 *   blows rather than a length of time, so a time ramp on top would charge
	 *   the same sentence twice.
	 *
	 * LINEAR BETWEEN THE TWO, WHICH IS A JUDGEMENT AND IS RECORDED AS ONE.
	 * `docs/DECISIONS.md` for 2026-09-02 has the genre survey behind it: Path of
	 * Exile's Blade Flurry builds six discrete stages and Monster Hunter's great
	 * sword three discrete levels, so a staged ramp is the commoner shape --
	 * but a stage count is a number the sheet does not carry, and a stage the
	 * player cannot see is not a stage. The row is written as a continuous rise
	 * and that is what this is.
	 *
	 * PURE AND PUBLIC SO A TEST CAN WALK THE WHOLE RAMP without a world that
	 * ticks. Nothing in an automation world advances time, so a test that could
	 * only release now or release at the end would leave the middle unproven.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	float ChargedDamagePercent(float SecondsOfHold) const;

	/**
	 * Let the swing go now, for whatever the hold so far is worth.
	 *
	 * Public so a test can drive it without pressing a key.
	 *
	 * @return how many enemies the swing caught
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	int32 ReleaseTheSwing();

	/**
	 * The hold reached its full `ChargeTime`, so the swing goes at its ceiling.
	 *
	 * THE HOLD RELEASES ITSELF RATHER THAN WAITING FOR THE KEY, which is what
	 * "rising to 350% if you hold the full 2 seconds" and "hold it for 3
	 * seconds" both describe: neither row offers anything for holding longer. It
	 * also means a player who keeps the key down still swings, rather than
	 * standing rooted until something kills them.
	 *
	 * Public so a test can drive it without a world that ticks.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	void LetTheHoldFinish();

	/**
	 * Something the row names in `ChargeBreaksOn` happened. The swing is lost.
	 *
	 * NOTHING LANDS, WHICH IS WHAT BOTH ROWS SAY. Backswing: "being staggered
	 * loses the swing entirely." The Whole Weight: "if you are killed during the
	 * wind-up, nothing lands at all." The mana and the cooldown were spent at
	 * activation and are not given back, which is what makes a break a cost.
	 *
	 * Public so a test can drive it without arranging a real stagger.
	 *
	 * @param What  the word from `ChargeBreaksOn` that fired, for the log
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	void BreakTheHold(const FString& What);

	/**
	 * Whether this row's `ChargeBreaksOn` names the given word.
	 *
	 * `Movement` AND `Stagger` ARE THE SAME EVENT HERE, and that is worth saying
	 * rather than leaving to be discovered. A character holding a swing cannot
	 * walk -- the movement gate refuses it -- so the only way it moves is by
	 * being shoved, which is exactly what this project's vocabulary calls a
	 * stagger: `Keyword.Stagger` is described as "stagger and knockback
	 * effects". A row naming either breaks on a displacement. No row names
	 * `Movement` today.
	 */
	bool HoldBreaksOn(const TCHAR* What) const;

	/**
	 * A blow landed on the caster while the swing was drawn back. Count it.
	 *
	 * THE WHOLE WEIGHT'S `ScalingSource=HitTaken`: "every hit you take during the
	 * wind-up adds 8% more damage to what follows, to a maximum of 500%."
	 * `UCataclysmSkillTemplate::NoteBlowTaken` calls this, from the one place in
	 * the game every incoming blow is resolved.
	 *
	 * IT COUNTS ON EVERY HELD SWING, NOT ONLY ON ONE NAMING THE SOURCE. The
	 * count is cheap and a row that does not name `HitTaken` never reads it, so
	 * gating the count on the row would only make the number wrong for anything
	 * that later wanted to look at it.
	 */
	void NoteBlowTakenWhileHolding();

	/** How many swings have landed this activation. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 SwingsMade = 0;

	/** What the eruption dealt, in total. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	float Erupted = 0.0f;

	/** Blows landed on the caster during this wind-up. `HitTaken`. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 BlowsTakenWhileHolding = 0;

	/** What the released swing landed for, as a percent. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	float ReleasedAtPercent = 0.0f;

private:
	void Repeat();
	void Finish();

	/**
	 * Draw the swing back and wait.
	 *
	 * NO BLOW IS SCHEDULED HERE. Every other Strike hands its blow to
	 * `WhenTheSwingConnects` and is done with it; a held swing has no idea yet
	 * when it lands, because that is the player's decision.
	 */
	void BeginTheHold();

	/** How long the swing has been drawn back, in seconds. */
	float SecondsOfHoldSoFar() const;

	/** True between `BeginTheHold` and whatever ends the hold. */
	bool bHolding = false;

	/**
	 * World time when the swing was drawn back.
	 *
	 * ONLY AN EARLY RELEASE READS IT. A hold that runs its full length is
	 * released by `HoldTimer`, which fires at exactly `ChargeTime`, so that path
	 * never consults the clock and the two cannot disagree.
	 */
	float HoldBeganAtSeconds = 0.0f;

	/** When the hold lets go by itself. `ChargeTime`. */
	FTimerHandle HoldTimer;

	/**
	 * Whether the key has come up since the sword went in.
	 *
	 * FALSE UNTIL `InputReleased` ARRIVES. See `InputPressed` above: without it,
	 * holding the Special key plants and erupts inside two frames.
	 */
	bool bKeyReleasedSincePlanting = false;

	FTimerHandle RepeatTimer;
	FTimerHandle FinishTimer;

	/**
	 * When the sword comes back by itself. `GroundDuration`.
	 *
	 * THE SAME TEN AS THE BURNING GROUND, AND THE ROW STATES ONE TEN. "It burns
	 * everything within 4 meters ... pull it free within 10 seconds" is one
	 * sentence about one object: the sword stands as long as the fire around it
	 * burns. A second parameter would let the two drift apart.
	 */
	FTimerHandle WindowTimer;
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

	/** How many curse applications this use copied outward. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 CursesSpread = 0;

	/**
	 * Bind a burning line between enemies near the caster, if the row asks.
	 *
	 * ONE ROW ASKS: the Whip's Tether, "bind two enemies within 12 meters
	 * together with a burning line". `TetherTargets` says how many, `TetherLength`
	 * how far apart they may get, and `TetherDuration` how long it holds. A row
	 * stating none of them does nothing here, which is every other projectile.
	 *
	 * THE NEAREST ONES, because `FindEnemiesInSphere` answers nearest first and
	 * the row asks for "two enemies within 12 meters" without saying which two.
	 *
	 * FEWER THAN IT ASKED FOR BINDS NOTHING AND IS NOT A FAILURE. A Tether thrown
	 * at a lone enemy has nothing to tie it to. The bolt still flies.
	 *
	 * Public so a test can bind a line without waiting for a throw to connect.
	 *
	 * @return the line, or null when none was made
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	class ACataclysmTether* BindTether(AActor* Caster);

	/** The line this use bound, or null. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	TObjectPtr<class ACataclysmTether> Tethered;

	/**
	 * How many commanded creatures this use ordered onto its target.
	 *
	 * ONE ROW ORDERS ANY: the Staff's Compel, "everything you command strikes
	 * that same enemy at once". Zero for every other projectile, and zero for
	 * Compel cast by a character commanding nothing.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 CommandedStrikes = 0;

	/**
	 * Copy the curses on each of these enemies onto the nearest others.
	 *
	 * THE WAND'S MALEFICE AND NOTHING ELSE TODAY: "copying every curse it
	 * already carries onto the two nearest enemies". `SpreadCurses` says how
	 * many others each struck enemy passes its curses to, and a skill stating
	 * none does nothing here.
	 *
	 * NEAREST TO THE ENEMY THAT CARRIED THEM, not to the caster. The curse
	 * spreads from the cursed creature outward, which is what the sentence
	 * describes and what makes hitting a clustered enemy worth more than hitting
	 * a lone one.
	 *
	 * WITHIN THE SKILL'S OWN RANGE, because nothing else bounds it and a curse
	 * jumping to an enemy the bolt could never have reached is not what the row
	 * says. Malefice reaches fourteen metres.
	 *
	 * Public so a test can drive it without a projectile in flight.
	 *
	 * @return how many effect applications landed, summed
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	int32 SpreadCursesFrom(const TArray<AActor*>& Struck);

	/**
	 * Leave the weapon in each of these enemies, if this skill's row says so.
	 *
	 * THE AXE'S HARROWER AND NOTHING ELSE TODAY: "the axe stays where it lands.
	 * When that enemy dies it tears free and buries itself in the nearest living
	 * enemy within 10 meters." Said by `OnDeath=Leap` and `OnDeathRange=10`.
	 *
	 * `OnDeath=SpreadDebuff` IS THE WAND'S AND DOES NOT COME THROUGH HERE. It
	 * copies tags and deals nothing; this deals a blow and carries the skill's
	 * identity with it. `Release` is the Spear's third value and is not built.
	 *
	 * Public so a test can drive it without a projectile in flight.
	 *
	 * @return how many enemies were left carrying it
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	int32 BuryInStruck(const TArray<AActor*>& Struck);

	/**
	 * Whether this skill empties a rack rather than making one throw.
	 *
	 * THE AXE'S BUTCHER'S BILL AND NOTHING ELSE TODAY: "empty the rack: thirty
	 * burning axes thrown over 10 seconds at every enemy within 10 meters." Said
	 * by `Count=30` beside an `Interval`, which is the same pair the Strike and
	 * Summon shapes already use to mean "again and again".
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	bool ThrowsRepeatedly() const;

	/**
	 * Throw one, at whoever this skill's `TargetMode` picks.
	 *
	 * Public so a test can drive the rack without waiting on a timer, the same
	 * way `SwingOnce` and `SummonOne` are.
	 *
	 * @return whether anything was thrown
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	bool ThrowOne();

	/** How many have been thrown this use. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 ThrowsMade = 0;

	/**
	 * Seconds between throws, after the caster's attack speed if the row says so.
	 *
	 * "A higher attack speed throws them faster and empties the rack sooner."
	 * Said by `ScalesWithAttackSpeed`, which no other row states.
	 *
	 * A BASELINE OF ONE ATTACK A SECOND, which is what the row's own numbers
	 * assume: thirty axes at 0.333 seconds is 10 seconds exactly, and 10 seconds
	 * is what the description states. So dividing by the attribute directly
	 * leaves the stated figures intact at one attack a second and shortens the
	 * rack above it.
	 *
	 * THE COUNT DOES NOT MOVE, which is the point `ScalesWithAttackSpeed`'s own
	 * header makes: thirty axes are thrown whatever the speed, and a faster
	 * character empties the rack sooner rather than throwing more.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	float SecondsBetweenThrows() const;

private:
	/**
	 * What the repeat timer calls, because a timer wants a void function and
	 * `ThrowOne` answers whether it threw.
	 */
	void ThrowTick();

	/** Start the rack: throw the first and set the timer for the rest. */
	void BeginEmptyingTheRack();

	/** Stop throwing and end the ability. */
	void StopThrowing();

	/** Who the next throw is aimed at, or null when nothing is in range. */
	AActor* NextThrowTarget();

	/** Fires one throw per interval while a rack is being emptied. */
	FTimerHandle ThrowTimer;

	/** Ends a rack that has run its stated time. */
	FTimerHandle RackTimer;

	/** Where the last throw was aimed, so a rack spreads across a group. */
	int32 NextTargetIndex = 0;

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
 * expires. Burning Wrath's "4% more fire damage for every enemy currently
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
	 * Enemies pinned within Radius when the buff went up. Held Fast scales on it.
	 *
	 * COUNTED ONCE, AT THE CAST, EXACTLY AS THE BURNING COUNT ABOVE IS. The
	 * Spear's Held Fast reads "you deal 10% more damage for every enemy you
	 * currently have pinned", and Burning Wrath's "every enemy currently burning"
	 * is the same word settled the same way: the count is taken when the buff
	 * goes up and does not move afterwards. So the skill is cast after pinning
	 * rather than before, and a pin that expires during the ten seconds does not
	 * lower it.
	 *
	 * IT COUNTS PINNED ENEMIES AND NOT PINS THE CASTER APPLIED. "Enemy you
	 * currently have pinned" could mean either, and tracking who applied each pin
	 * would mean carrying an instigator on every pin for the benefit of one row.
	 * Nothing else in the game pins, so the two readings give the same number
	 * today; the day a second thing pins, this is where the difference would
	 * show.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 PinnedEnemiesAtCast = 0;

	/**
	 * Percentage points of MORE damage this buff is currently granting.
	 *
	 * Zero while the buff is down, and zero for a self buff whose row carries no
	 * MoreDamagePer. Read by tests and by anything that shows the player
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

	/**
	 * How many enemies have been killed since this buff went up.
	 *
	 * THE AXE'S BUTCHER'S HEAT: "every enemy you kill while it lasts grants 1%
	 * more damage and adds another second to the heat. The bonus has no ceiling
	 * and ends when the heat does."
	 *
	 * COUNTED AS THEY HAPPEN, WHICH IS THE OPPOSITE OF `BurningEnemiesAtCast`
	 * ABOVE. Burning Wrath counts once, when the buff goes up, because its
	 * sentence says "currently burning" at that moment. This counts forward,
	 * because its sentence says "while it lasts".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 KillsCounted = 0;

	/**
	 * Take one kill into account: raise the bonus and lengthen the buff.
	 *
	 * CALLED FROM `UCataclysmSkillTemplate::NoteKill`, which the enemy death
	 * path calls once per kill. It does nothing for a buff whose
	 * `ScalingSource` is not `Kill`, which is every other buff in the game.
	 *
	 * `DurationPer` LENGTHENS THE BUFF AND IS ONLY READ HERE. Butcher's Heat is
	 * the one row that states it, at one second a kill.
	 *
	 * Public so a test can drive it without killing anything.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	void NoteKill();

	/**
	 * React to one of this character's blows landing, if this buff's row says to.
	 *
	 * CALLED FROM `UCataclysmSkillTemplate::NoteBlowLanded`, which every blow in
	 * the game goes through. Two rows react and they react to different halves of
	 * what it is told; every other buff in the game ignores it.
	 *
	 * THE DAGGER'S SLIPSTREAM READS THE SIDE. "For 8 seconds every enemy you
	 * strike from behind returns your movement skill to you at once. Blows landed
	 * from the front do nothing for it." Its row states `Requires=RearHit` and
	 * `RefundsCooldown=Movement`, and both halves of that sentence are those two
	 * keys.
	 *
	 * `Requires=RearHit` IS READ HERE AND NOWHERE ELSE, which is what its comment
	 * in `RequirementsAreMet` has said since before anything read it: it is a
	 * condition on what a running buff reacts to, not on whether the buff may be
	 * cast. Refusing to cast Slipstream until the player was already behind
	 * something would be a different skill.
	 *
	 * THE WARHAMMER'S GROUNDBREAKER READS THE POSITION: "for 10 seconds every
	 * blow you land cracks the ground beneath what it hits, leaving a fissure
	 * that knocks down the next enemy to cross it. Fissures last 6 seconds and
	 * there is no limit to how many you may open."
	 *
	 * "NO LIMIT TO HOW MANY YOU MAY OPEN" IS TAKEN LITERALLY, and it is bounded
	 * by the skill rather than by a cap here: a fissure is spent by the first
	 * creature to cross it, the buff runs ten seconds, and a character swings a
	 * little over once a second. What stops a room filling with cracks is that
	 * each one is worth a single knockdown and every knockdown takes the five
	 * second immunity window it shares with the stun.
	 *
	 * Public so a test can drive it without landing a blow.
	 *
	 * @param Where  the position of what was hit. The crack opens beneath what
	 *               the blow struck, not beneath the character that swung
	 * @param bFromBehind  whether the attacker was behind what it struck. Only
	 *               a buff stating `Requires=RearHit` asks
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	void NoteBlowLanded(AActor* Target, const FVector& Where,
						bool bFromBehind = false);

	/** How many cooldowns this buff has returned. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 CooldownsReturned = 0;

	/**
	 * React to a blow landed ON this buff's holder, if its row says to.
	 *
	 * ONE ROW STATES IT: the Greataxe's Burning Wrath, "while it lasts, any enemy
	 * that strikes you in melee is set alight". Its `BurnsAttackers=1` is what
	 * asks, and until 2026-09-02 the sentence had no parameter and no hook behind
	 * it -- the skill's `Burn=1` had been there since it was written and had never
	 * set anything alight. Issue #1157.
	 *
	 * THE MIRROR OF `NoteBlowLanded` ABOVE. That one is told when the holder hits
	 * something; this is told when something hits the holder.
	 *
	 * MELEE ONLY, AND NOT A DAMAGE OVER TIME TICK. The row says "strikes you in
	 * melee", so an arrow, a spell and a fire already burning all set nothing
	 * alight. Both refusals are here rather than at the caller, because the
	 * caller is one place that has to serve every buff and only this row cares.
	 *
	 * Public so a test can drive it without taking a blow.
	 *
	 * @param Striker   what hit the holder, or null when the blow had no causer
	 * @param bWasMelee whether the blow carried the melee tag
	 * @param bWasDamageOverTime  whether it was a tick rather than a blow
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	void NoteBlowTaken(AActor* Striker, bool bWasMelee,
					   bool bWasDamageOverTime, float DealtToHealth);

	/** How many attackers this buff has set alight. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 AttackersLit = 0;

	// ----------------------------------------------------------------------
	// Martyr's Ember -- the one buff that holds damage rather than granting a
	// number. Issue #1162.
	// ----------------------------------------------------------------------

	/**
	 * Damage held in this buff's store, in damage rather than in percent.
	 *
	 * "40% OF ALL DAMAGE YOU TAKE WHILE IT LASTS IS STORED." Filled by
	 * `NoteBlowTaken` from `StoresFromHitTaken`, capped by `StoreCeiling`, and
	 * emptied by `NoteBlowLanded` at `StoreSpentPerHit` of weapon damage a blow.
	 *
	 * ZERO FOR EVERY OTHER BUFF IN THE GAME, which state none of the three keys.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	float Stored = 0.0f;

	/** How much the store has paid out this activation. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	float StoreSpent = 0.0f;

	/**
	 * The most the store may hold, in damage.
	 *
	 * `StoreCapPercent` OF THE CASTER'S WEAPON DAMAGE, which is the row's own
	 * unit: "the store is capped at 200% weapon damage". Zero for a buff stating
	 * no cap, and a store with no ceiling is refused rather than left unbounded
	 * -- see `NoteBlowTaken`.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	float StoreCeiling() const;

	/**
	 * The most one landed blow may take out of the store, in damage.
	 *
	 * `StoreSpentPerHit` OF THE CASTER'S WEAPON DAMAGE. A blow spends this or
	 * whatever is left, whichever is smaller, which is what lets "until it is
	 * empty" actually happen.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	float StoreSpendPerBlow() const;

	/**
	 * One second of a buff whose bonus grows with time.
	 *
	 * ONE ROW STATES IT: the Greatsword's Unbroken, "while you do not move you
	 * gain 5% more damage every second. The whole bonus is lost the instant you
	 * take a step." Its `ScalingSource=Second` is one of the sources nothing
	 * counted until 2026-09-02.
	 *
	 * IT CHECKS THE STEP AS WELL AS COUNTING THE SECOND, because both halves of
	 * that sentence are about the same beat: the tally climbs while the holder
	 * stands still and resets when it does not. Splitting them would need a
	 * second timer answering the same question.
	 *
	 * A SECOND IS NOT A CONFIGURABLE INTERVAL. The row says "every second" and
	 * `Interval` already means something else on this shape -- the Spear's Held
	 * Fast uses it to relight pinned enemies -- so a buff can state one of each
	 * and they do not interfere.
	 *
	 * Public so a test can drive it without a world that ticks.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	void SecondPassed();

	/** How many seconds this buff's bonus has been building. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 SecondsHeld = 0;

	/**
	 * One repeat of a self buff that states an `Interval`.
	 *
	 * ONE ROW STATES ONE: the Spear's Held Fast, "any pinned enemy within 12
	 * meters is set alight again each second it is held". Until 2026-09-02 a
	 * self buff was the only lasting shape that could not repeat, so that half
	 * of the row could not be written down at all.
	 *
	 * WHAT IT DOES IS DECIDED BY THE ROW AND NOT BY THIS SHAPE. A buff that
	 * states `Burn` sets alight what its radius catches; one that states none
	 * ticks and does nothing, which costs a search and is the honest answer for
	 * a row that asked to repeat without saying what repeats.
	 *
	 * IT SETS ALIGHT WHAT THE ROW'S `ScalingSource` NAMES, WHEN IT NAMES ONE.
	 * Held Fast says "any PINNED enemy", and its `ScalingSource=Pinned` is
	 * already the word for that, so the tick reuses it rather than adding a
	 * second parameter meaning the same thing. A buff naming no source lights
	 * everything its radius catches.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	void RepeatTick();

	/** How many fissures this buff has opened. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 TerrainLeft = 0;

	/** How many times it has repeated. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 Repeats = 0;

	/** How many enemies the last repeat set alight. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 LastRepeatLit = 0;

	/** How many enemies the last repeat shoved away. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 LastRepeatShoved = 0;

	/** How long this buff will now run in total, after any extensions. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	float TotalDuration = 0.0f;

private:
	void Finish();

	/** Put the increase on the caster. Does nothing when there is none to grant. */
	void GrantIncrease();

	/** Take it off again. Safe to call when nothing was granted. */
	void RevokeIncrease();

	/**
	 * How many units this buff's `ScalingSource` is worth right now.
	 *
	 * `Burning` is the count taken at the cast; `Kill` is the running tally.
	 * Every other source answers zero, so a buff naming one grants nothing
	 * rather than a figure taken from the wrong thing.
	 */
	int32 ScalingCount() const;


	FTimerHandle FinishTimer;

	/** Fires once per `Interval` while the buff runs. Idle for a row with none. */
	FTimerHandle RepeatTimer;

	/**
	 * Fires once a second for a buff whose bonus grows with time.
	 *
	 * SEPARATE FROM `RepeatTimer` ABOVE, because the two answer different
	 * questions on different beats: that one fires on the row's stated
	 * `Interval` and does whatever the row says, this one counts seconds. A row
	 * may state both.
	 */
	FTimerHandle SecondTimer;

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

	/**
	 * Where this move ends: an enemy its `Requires` names, or the aimed point.
	 *
	 * TWO SKILLS TRAVEL TO A CREATURE RATHER THAN TO A PLACE. The Sword's
	 * Flashpoint: "dart to a burning enemy up to 14 meters away ... only
	 * something already alight can be reached". The Axe's Emberhaul: "bury your
	 * axe in the first enemy within 12 meters and haul yourself to it". Both say
	 * so through the `Requires` column, and until this the destination was
	 * wherever the cursor happened to be, so either could be used to travel to an
	 * empty patch of floor with an enemy burning behind the player.
	 *
	 * THE NEAREST ONE, because `FindEnemiesInSphere` answers nearest first and
	 * neither description asks to reach past a nearer candidate.
	 *
	 * IT FALLS BACK TO THE AIMED POINT RATHER THAN REFUSING.
	 * `CanActivateAbility` has already turned the cast down if no such enemy
	 * existed, so arriving here with none means it died between the check and
	 * the move. Standing still having already paid for a movement skill is worse
	 * than travelling.
	 *
	 * Public so a test can ask where a move would go without moving anything.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	FVector ConditionalDestination(const FVector& Start) const;

	/**
	 * One arrival of a move that repeats. `Mode=Flicker`.
	 *
	 * ONE ROW STATES IT: the Dagger's Everywhere at Once, "for 4 seconds you are
	 * nowhere long enough to be hit. You flicker between every enemy within 10
	 * meters, striking each from behind as you arrive for 30% weapon damage and
	 * setting them alight."
	 *
	 * EVERY ENEMY, ONE AT A TIME, IN A CIRCUIT. Each firing takes the next
	 * creature in the list gathered when the skill went up and moves to it. When
	 * the list runs out it starts again, which is what "flicker between" says: a
	 * four second skill at a third of a second an arrival makes twelve arrivals,
	 * and a fight with three enemies is meant to see each of them four times
	 * rather than to stop after three.
	 *
	 * THE LIST IS FIXED WHEN THE SKILL GOES UP AND IS NOT RE-SEARCHED. "Every
	 * enemy within 10 meters" is read at the moment of casting; a creature that
	 * wanders into range afterwards was not one of them. It also stops the skill
	 * chasing a fleeing enemy across the level, because each arrival moves the
	 * caster and a fresh search would be centred somewhere new every time.
	 *
	 * ANYTHING THAT DIED OR LEFT IS SKIPPED, and a circuit with nothing left in
	 * it stops moving the caster while the skill runs out its duration. The
	 * untargetability is the other half of what the row pays for, so ending
	 * early would take that away as well.
	 *
	 * Public so a test can drive it without a world that ticks.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	void FlickerOnce();

	/** How many arrivals a flicker has made. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 Arrivals = 0;

	/**
	 * The creature a `Swap` traded places with, or null. Read by tests.
	 *
	 * THE STAFF'S VESSELSTEP IS THE ONE ROW: "trade places with a creature you
	 * command up to 14 meters away." It is the only movement mode whose
	 * destination is a creature rather than a place, which is why it is the only
	 * one that has anything to record here.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	TObjectPtr<AActor> SwappedWith;

	/**
	 * Strike whatever the charge has passed through since this was last asked.
	 *
	 * ONE ROW STATES IT: the Greatsword's Inexorable, "begin an advance that
	 * cannot be turned aside. You charge 14 meters in 1.5 seconds, immune to
	 * crowd control and unable to change direction or stop, throwing aside and
	 * setting alight everything you pass through. The further you charged, the
	 * harder it hits."
	 *
	 * IT NO LONGER MOVES ANYTHING, AND UNTIL 2026-09-02 IT DID. It used to
	 * teleport the character 46.7cm with `SetActorLocation`, ten times a second.
	 * That is what the project owner saw in the first play session: the camera
	 * held still for five rendered frames and then jumped, and the character slid
	 * rather than walked, because the walk animation is driven by the movement
	 * component's velocity and a teleport never writes one. Issue #1169.
	 *
	 * `UCharacterMovementComponent` MOVES THE CHARACTER NOW, through the root
	 * motion source `BeginAdvance` applies. That gives a real velocity, so the
	 * animation and the camera both follow, and the movement component does the
	 * sweeping, so a charge still stops against a wall.
	 *
	 * SO THIS IS AN OBSERVER. It notices how far the character has actually
	 * moved since it was last asked, adds that to the tally, and strikes what
	 * the line between the two positions passed through. Ten times a second is
	 * often enough to miss nothing, because the search covers the whole line
	 * rather than a point.
	 *
	 * A CHARGE WITHOUT A DURATION IS UNCHANGED AND STILL ARRIVES AT ONCE. The
	 * Fist's Cinder Rush and the Whip's Reel both state `Mode=Charge` and no
	 * duration, and both are one move to a point that hits what the line crosses.
	 * Stating a duration is what makes a charge last.
	 *
	 * THE DIRECTION IS FIXED AT THE CAST AND NEVER RE-READ, which is what "cannot
	 * be turned aside" and "unable to change direction" say. Reading the cursor
	 * each step would make it a chase.
	 *
	 * IT HITS WHAT IT PASSES AND NOTHING TWICE. A creature the charge runs
	 * through is struck once and remembered afterwards -- without that, it would
	 * strike whatever it was standing beside on every one of its searches.
	 *
	 * A TEST HAS TO MOVE THE CHARACTER ITSELF BEFORE CALLING THIS. A world built
	 * by `UWorld::CreateWorld` is never ticked, so no root motion is ever
	 * applied, and the test fighters are plain actors with no movement component
	 * for it to be applied to. What a search DOES is covered; that the engine
	 * moves the character is covered by asking whether the root motion source was
	 * applied, in `Cataclysm.Skills.AnAdvanceHandsItsMovementToTheMovementComponent`.
	 *
	 * Public so a test can drive it without a world that ticks.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	void AdvanceOneStep();

	/**
	 * Whether a skill this character is running is walking it right now.
	 *
	 * WHAT ASKS: the player controller's movement gate. The Greatsword's
	 * Inexorable is "an advance that cannot be turned aside ... unable to change
	 * direction or stop", so while it runs the player's own movement input is
	 * refused and the skill's step timer is the only thing moving the character.
	 *
	 * NOT AN ENTRY IN `Immune`, THOUGH IT REFUSES SOMETHING. That parameter names
	 * effects done TO a character by somebody else, from a closed list the design
	 * document supplies. Being carried along by your own skill is not one of
	 * them, and putting it there would have meant inventing a value the design
	 * never wrote.
	 *
	 * ASKED RATHER THAN FLAGGED, which is the shape every other question of this
	 * kind in this file takes: a skill that lasts IS an active ability while it
	 * lasts, so nothing has to be written onto the character and nothing has to
	 * be cleared when it ends, is cancelled, or its owner dies.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	static bool IsBeingWalkedByASkill(const AActor* Who);

	/**
	 * Which way a skill is carrying this character, or a zero vector.
	 *
	 * WHAT ASKS, AND WHY A ROOT MOTION SOURCE IS NOT ENOUGH BY ITSELF.
	 * `ACataclysmPlayerController::PostProcessInput` feeds this back to the pawn
	 * as movement input every frame while a charge runs. The root motion source
	 * decides where the character goes and how fast; this decides what the
	 * movement component THINKS about it, and two things read that rather than
	 * the velocity:
	 *
	 * - `ABP_Unarmed`, the animation Blueprint, sets its `ShouldMove` flag from
	 *   `GroundSpeed > 0.01 AND GetCurrentAcceleration != 0`. Read out of the
	 *   asset's own event graph on 2026-09-02. Root motion writes velocity and
	 *   never writes acceleration, so a charged character was moving with the
	 *   idle animation playing -- which the project owner reported as sliding.
	 * - `bOrientRotationToMovement` turns the character to face its
	 *   ACCELERATION, not its velocity, so without this a charge could carry a
	 *   character sideways.
	 *
	 * IT CHANGES NEITHER THE SPEED NOR THE DIRECTION OF TRAVEL. The root motion
	 * source is in `Override` mode, so whatever velocity the input would have
	 * produced is discarded. This is the skill saying which way it is going, not
	 * a second thing pushing.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	static FVector AdvanceDirectionFor(const AActor* Who);

	/** How many steps a lasting charge has taken. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 StepsTaken = 0;

	/**
	 * How far a lasting charge has walked, in centimetres. Read by tests.
	 *
	 * WHAT `ScalingSource=Meter` COUNTS. Inexorable: "the further you walked, the
	 * harder it hits." Measured as the distance actually covered rather than as
	 * the distance from where it began, so an advance that walks into a wall
	 * stops adding to it -- which is the honest reading of "the further you
	 * walked".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	float WalkedCm = 0.0f;

	/**
	 * Whether this skill is holding a mark to return to. `Mode=Recall`.
	 *
	 * THE DAGGER'S ECHO IS TWO PRESSES AND THIS IS WHICH ONE THE NEXT WILL BE.
	 * "Leave a burning after-image where you stand. For 8 seconds you may return
	 * to it from anywhere within 14 meters."
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	bool HasMark() const;

	/** Where a held mark is. Meaningless when `HasMark` is false. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	FVector MarkedAt = FVector::ZeroVector;

private:
	/**
	 * When a held mark stops being usable, on the world's clock.
	 *
	 * A DEADLINE RATHER THAN A TIMER, so nothing has to be cancelled. Echo's mark
	 * is not an object and does nothing while it waits; it only has to answer
	 * whether it is still good at the moment the player presses again. A timer
	 * would need clearing when the ability ended, when the character died and
	 * when a second cast replaced the mark, and each of those is a way to leave
	 * one running.
	 */
	double MarkExpiresAt = 0.0;

	/** The circuit a flicker walks, fixed when the skill goes up. */
	TArray<TWeakObjectPtr<AActor>> FlickerCircuit;

	/** Which of them the next arrival takes. */
	int32 NextInCircuit = 0;

	FTimerHandle FlickerTimer;

	/** Ends a flicker when its duration runs out. */
	void FinishFlicker();

	/**
	 * How often a lasting charge looks for what it has run through, in seconds.
	 *
	 * TEN TIMES A SECOND, AND WHAT THAT RATE DECIDES CHANGED ON 2026-09-02. It
	 * used to be how often the character was teleported forward, so it decided
	 * how the charge LOOKED, and it looked like stuttering. The movement
	 * component moves the character every frame now, so this only decides how
	 * often the skill asks what it has passed through.
	 *
	 * TEN IS STILL RIGHT FOR THAT, and for a different reason from before. The
	 * search covers the whole line between two positions rather than a point, so
	 * nothing is missed however far apart they are; what a fast rate buys is that
	 * a creature is struck near the moment the charge reaches it rather than up
	 * to a tenth of a second late. Searching every frame would ask the level the
	 * same question six times over for no visible gain, which is the reason
	 * `ACataclysmGroundZone` gives for not ticking.
	 */
	static constexpr float SecondsPerAdvanceStep = 0.1f;

	/** Sets up a charge that lasts, hands its movement to the engine, and starts
	 *  its search timer. */
	void BeginAdvance(const FVector& Start);

	/** Ends a lasting charge when its duration runs out. */
	void FinishAdvance();

	/**
	 * The lasting charge running on this character, or null.
	 *
	 * ONE SEARCH BEHIND TWO QUESTIONS. `IsBeingWalkedByASkill` and
	 * `AdvanceDirectionFor` differ only in what they read off the answer, and
	 * two copies of the loop would be two places for the definition of "is being
	 * carried by a charge" to drift apart.
	 */
	static const UCataclysmMovementSkill* RunningAdvanceOn(const AActor* Who);

	/** The direction a lasting charge travels, fixed when it began. */
	FVector Advance = FVector::ZeroVector;

	/**
	 * How fast it travels, in centimetres per second.
	 *
	 * THE ROW'S TWO NUMBERS ARE ITS SPEED. `Range` is how far the charge goes and
	 * `Duration` is how long it takes, so the pair is a speed and the sheet
	 * states both. Inexorable's fourteen metres in one and a half seconds is 9.3
	 * metres a second, a little over twice a Ravager's 4.6 metre walk.
	 *
	 * IT WAS 4.67 UNTIL 2026-09-02, which is 1.5% faster than that walk. The
	 * project owner played it and said it was "just less optimal walking", and
	 * the arithmetic agreed. Issue #1170. The comment that used to sit here
	 * compared the old figure against the Ritualist's 3.5, which is the slowest
	 * of the three classes rather than the one holding the weapon, and that
	 * comparison is part of what let it through.
	 */
	float SpeedCmPerSecond = 0.0f;

	/**
	 * Where the last search for things to strike started from.
	 *
	 * NEEDED BECAUSE THE SKILL NO LONGER MOVES THE CHARACTER. The movement
	 * component does, every frame, so by the time a search runs the character has
	 * already travelled and there is no step to read the two ends off. This is
	 * the far end of the last search and therefore the near end of the next.
	 */
	FVector LastSearchedFrom = FVector::ZeroVector;

	/**
	 * What a lasting charge has already struck, so nothing is struck twice.
	 *
	 * A CHARGE NEEDS THIS AND AN ARRIVAL DOES NOT. A creature standing beside the
	 * path would otherwise be found by the line search on every one of a charge's
	 * fifteen searches.
	 */
	TSet<TWeakObjectPtr<AActor>> StruckAlready;

	FTimerHandle AdvanceTimer;
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

	/**
	 * Strike one enemy and take it if the blow left it weak enough.
	 *
	 * ONE ROW STATES IT: the Staff's Subjugate, "drive your will into an enemy up
	 * to 15 meters away for 300% weapon damage, setting it alight. If the blow
	 * leaves it below half health you take it permanently: it fights for you
	 * until it dies, keeps its own abilities, and sets alight what it strikes.
	 * Holding a thrall reserves 30 Fervour, so your army is only as large as your
	 * pool. Bosses cannot be taken."
	 *
	 * A SUMMON THAT SUMMONS NOTHING, and this shape is still the right home. What
	 * the row produces is a creature fighting for the caster; whether it was torn
	 * out of a rift or taken off the other side is one branch rather than a ninth
	 * skill template.
	 *
	 * THE BLOW LANDS WHETHER OR NOT ANYTHING IS TAKEN. Every refusal below it --
	 * too healthy, a boss, no room in the pool -- leaves an enemy that has been
	 * hit for 300% and set alight, which is what "if the blow leaves it" says.
	 *
	 * Public so a test can drive it without aiming.
	 *
	 * @return whether an enemy was taken
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	bool Possess();

	/** Whether the last use took an enemy. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	bool bTookIt = false;

	/**
	 * Whether the last use was refused for want of room in the pool.
	 *
	 * SEPARATE FROM `bTookIt` BEING FALSE, which has four causes: nothing where
	 * the player pointed, a target left too healthy, a boss, and this. A test
	 * that only asked whether something was taken could not tell the cap working
	 * from the threshold working.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	bool bRefusedForRoom = false;

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

	/**
	 * The key came up. Nothing happens here except arming the switch-off above.
	 *
	 * WHY A TOGGLE HAS TO WATCH FOR THE RELEASE. Issue #1114.
	 * `ACataclysmPlayerController::Input_AbilitySlotPressed` is bound to
	 * `ETriggerEvent::Triggered`, which fires EVERY FRAME the key is held -- its
	 * own comment says so, from issue #1016 -- and that is right for a skill
	 * with a cooldown, where holding the button keeps casting.
	 *
	 * THE AURA SLOT HAS NO COOLDOWN, so nothing rate-limited it. Holding the key
	 * activated the aura on one frame, switched it off on the next, activated it
	 * again on the third, and every activation paid a full health cost. The
	 * project owner pressed the key once on 2026-08-31 and the log recorded five
	 * costs of about 253 health inside 117 milliseconds, which killed them.
	 *
	 * SO THE SWITCH-OFF NOW NEEDS A RELEASE FIRST. A press arriving while the
	 * key was never let go is a repeat frame of the press that started the aura,
	 * and does nothing.
	 */
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle,
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

	/**
	 * How many allies are currently carrying this aura's damage bonus.
	 *
	 * CONFLAGRATION: "allies within it deal 8% increased fire damage." Zero for
	 * an aura whose row states no `AllyIncreasedDamage`, which is every other
	 * aura. Read by tests. Issue #1182.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	int32 AlliesHelped() const { return HelpedAllies.Num(); }

	/** Whether this ally is currently carrying the bonus. Read by tests. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	bool IsHelping(const AActor* Ally) const;

	/**
	 * A blow landed on the aura's holder. Count it, and give back what the row
	 * says it is worth.
	 *
	 * THE FIST'S LIVING PYRE: "every hit you take raises the pyre's fire damage
	 * by 8% with no cap, and returns health equal to 25% of the damage that hit
	 * dealt." Two sentences, two parameters, one event. Issue #1162.
	 *
	 *   `MoreDamagePer=8; ScalingSource=HitTaken`  raises the pulse, through
	 *   `UCataclysmSkillTemplate::ScalingUnits`, which counts `BlowsTaken`.
	 *   `HealthFromHitTaken=25`                    gives health back, here.
	 *
	 * "WITH NO CAP" IS WHY THE ROW STATES NO `MaxDamagePercent`, and it is worth
	 * saying out loud because every other row using `MoreDamagePer` states one.
	 * `ScaledDamagePercent` only caps when a ceiling is stated, so leaving it out
	 * is how the row says "no cap" rather than an omission.
	 *
	 * CALLED FROM `UCataclysmSkillTemplate::NoteBlowTaken`, from the one place in
	 * the game every incoming blow is resolved. An aura whose row names neither
	 * parameter counts the blow and does nothing with the count.
	 *
	 * Public so a test can drive it without taking a real blow.
	 *
	 * @param DealtToHealth  how much of the blow reached the holder's health
	 * @return how much health was given back
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Skill")
	float NoteBlowTaken(float DealtToHealth);

	/** Blows landed on the holder since this aura went up. `HitTaken`. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 BlowsTaken = 0;

	/** Health this aura has given back, in total. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	float HealthReturned = 0.0f;

private:
	/** Pulse's return value is for tests; a timer can only call a void. */
	void PulseTick();
	void Finish();

	/**
	 * Give the bonus to allies standing inside, and take it back from those who
	 * left.
	 *
	 * RECOMPUTED EVERY PULSE RATHER THAN WATCHED FOR. Who is inside a moving ring
	 * is a question about now, which is exactly the reasoning
	 * `ACataclysmGroundZone::Sweep` gives for asking afresh every sweep. Nothing
	 * in the project reports "an actor left a radius", and building that for one
	 * row would be a worse trade than one search a second.
	 *
	 * A MODIFIER PER ALLY, HELD BY HANDLE. `UCataclysmAbilitySystemComponent::
	 * AddStatModifier` answers a handle and `RemoveStatModifier` takes one, so
	 * the aura keeps the handle it was given for each ally and gives exactly that
	 * one back. Applying an effect with a one-pulse duration instead would make
	 * the bonus flicker off between pulses.
	 *
	 * REFRESHED, NOT RE-ADDED, for an ally who is still inside. Adding a second
	 * modifier every second would stack the bonus without limit, which is the
	 * one way this could go badly wrong and is why the set is kept at all.
	 */
	void HelpAlliesInside();

	/** Take the bonus back from every ally still carrying it. */
	void StopHelpingEveryone();

	bool bHeld = false;

	/**
	 * Whether the key has come up since this activation started.
	 *
	 * FALSE UNTIL `InputReleased` ARRIVES, which is what makes the switch-off
	 * need a second, separate press rather than the next frame of the first
	 * one. Issue #1114. Reset in `ActivateAbility` rather than in `EndAbility`,
	 * because the aura also ends for reasons that are not a key press -- running
	 * out of mana, or a duration expiring -- and a fresh activation is the only
	 * moment at which "the key has not been let go yet" is true again.
	 */
	bool bKeyReleasedSinceActivation = false;

	/**
	 * Every ally currently carrying this aura's bonus, and the handle to take
	 * back.
	 *
	 * WEAK KEYS, so an ally that dies or is destroyed while standing in the ring
	 * cannot be reached for through a dangling pointer. `HelpAlliesInside`
	 * drops any key that has gone stale rather than trying to remove a modifier
	 * from something that no longer exists.
	 */
	TMap<TWeakObjectPtr<AActor>, int32> HelpedAllies;

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

	/**
	 * How many bystanders the spread set alight. Read by tests.
	 *
	 * SEPARATE FROM `EnemiesAffected`, which counts the ones the curse itself
	 * took. Hex of Cinders lays its hex on one enemy and may light several
	 * around it, so one number could not answer both questions.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 SpreadsLit = 0;
};
