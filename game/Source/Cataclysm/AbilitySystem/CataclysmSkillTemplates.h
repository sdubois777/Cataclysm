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
	void NoteBlowLanded(const FVector& Where, bool bFromBehind = false);

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
					   bool bWasDamageOverTime);

	/** How many attackers this buff has set alight. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	int32 AttackersLit = 0;

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

private:
	/** Pulse's return value is for tests; a timer can only call a void. */
	void PulseTick();
	void Finish();

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
