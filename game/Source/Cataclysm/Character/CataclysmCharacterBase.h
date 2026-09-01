// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "GenericTeamAgentInterface.h"
#include "CataclysmCharacterBase.generated.h"

class UAttributeSet;
class UCataclysmAbilitySet;
class UCataclysmAbilitySystemComponent;

/**
 * One thing an enemy can do, described so the brain can choose between them.
 *
 * DESCRIPTION AND EXECUTION ARE SEPARATE ON PURPOSE. This says when an ability
 * may be used; ACataclysmCharacterBase::UseEnemyAbility does it. The controller
 * needs only the description, so it can pick for any enemy without knowing what
 * any of them actually do, and a test can check the choosing without anything
 * being cast.
 *
 * NOT THE GAMEPLAY ABILITY SYSTEM, and that is worth saying out loud because
 * the player's skills are. No enemy has a granted ability, an ability set or a
 * cooldown effect, and building that is a change of its own. This is the
 * smallest thing that lets one enemy choose between three attacks by range.
 */
USTRUCT(BlueprintType)
struct FCataclysmEnemyAbility
{
	GENERATED_BODY()

	/** What it is called. For logs and test failures, not for logic. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	FName Name;

	/**
	 * How far the target must be for this to be worth using, in centimetres.
	 *
	 * A ranged attack sets this to the melee reach: there is no sense throwing
	 * a rock at something already being hit. A melee attack leaves it at zero.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float MinRangeCm = 0.0f;

	/** How far it reaches, in centimetres. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float MaxRangeCm = 0.0f;

	/** Seconds before it may be used again. Zero is always available. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float CooldownSeconds = 0.0f;

	/**
	 * Seconds the enemy stands committed before it lands, in which the player
	 * can walk clear. Zero lands at once.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float WindUpSeconds = 0.0f;

	/**
	 * What shape of ground marker warns of it, if any.
	 *
	 * THE SAME ENUMERATION A PLAYER SKILL USES, because the model says an enemy
	 * ability carries "exactly what a player skill row carries, so an ability
	 * can be executed by the same code and its telegraph marker drawn from its
	 * own numbers". A Strike marks a circle around the creature; a Projectile
	 * marks the lane it will fly down.
	 *
	 * None draws nothing, which is right for anything the player answers by
	 * interrupting rather than by walking out of.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	ECataclysmSkillShape Shape = ECataclysmSkillShape::None;

	/**
	 * Whether it flies in an arc and lands where it was aimed, rather than
	 * travelling flat and hitting whatever it passes on the way.
	 *
	 * IT CHANGES WHAT THE MARKER MEANS, which is why it is here rather than
	 * only inside the ability's own code. A flat projectile is warned about
	 * with a lane, because everything along the lane is in danger. One that
	 * arcs passes over all of that and endangers only where it comes down, so
	 * it is warned about with a circle there instead.
	 *
	 * Pairing a ground marker with a flat projectile is a known mistake rather
	 * than a matter of taste. Issue #459, and the sources are recorded with the
	 * decision in docs/DECISIONS.md.
	 *
	 * THE SHAPE STAYS Projectile EITHER WAY. A lob is still a projectile, and
	 * `ACataclysmEnemyController::AbilityNeedsFacing` reads that shape to decide
	 * whether the creature must be pointed at its target before it may begin,
	 * which a lob must be exactly as a flat throw must.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	bool bArcsOntoItsTarget = false;

	/**
	 * How wide the marked area is, in centimetres. A radius for a Strike, the
	 * projectile's own radius for a Projectile.
	 *
	 * THIS IS NOT A SECOND COPY OF THE ABILITY'S SIZE, and it must never become
	 * one. Fill it from the same constant the ability's own code uses. A marker
	 * that showed a different area from the one that hurts is worse than no
	 * marker, because the player would have learnt to trust it.
	 *
	 * Below one metre nothing is drawn: see
	 * ACataclysmTelegraphMarker::SmallestUsefulRadiusCm.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	float MarkerRadiusCm = 0.0f;

	/**
	 * Which phase of the fight this becomes available in. 1 from the start.
	 *
	 * **A PHASE SELECTS WHICH ABILITIES ARE IN THE ROTATION AND CHANGES NO
	 * NUMBER.** That is the finding the whole design leans on, from the
	 * research recorded with issue #354 in `docs/DECISIONS.md`: across ten
	 * shipped bosses in Path of Exile and Last Epoch, not one gains damage,
	 * armour, attack speed or critical strike at a transition. Escalation is
	 * adding a named ability. So this field is the whole of what a phase does,
	 * and the two-layer rule -- rarity scales magnitude, archetype sets
	 * behaviour -- survives a multi-phase boss untouched.
	 *
	 * **PHASES ADD, THEY NEVER TAKE AWAY.** An ability available from phase N
	 * stays available in every later phase, so
	 * `ACataclysmEnemyController::ChooseAbility` asks whether this is at most
	 * the creature's current phase rather than equal to it.
	 *
	 * ONE IS THE DEFAULT AND EVERY CREATURE BUT THE BOSS LEAVES IT THERE. A
	 * creature with no phases is a creature permanently in phase 1, which
	 * costs one comparison and needs no special case anywhere.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|AI")
	int32 Phase = 1;
};

/**
 * Shared base for anything with abilities: the player and every enemy.
 *
 * Does not own an ability system component. Where the component lives differs
 * between the player (player state, survives respawn) and enemies (the pawn
 * itself), so ownership is decided by the subclass and reached through
 * GetAbilitySystemComponent().
 *
 * DOES carry the side it is on, and here rather than in the subclasses, because
 * both need one and a character with no side is hostile to everything including
 * its own kind. Each subclass sets the value in its constructor; see
 * `ECataclysmTeam`.
 */
UCLASS(Abstract)
class CATACLYSM_API ACataclysmCharacterBase : public ACharacter,
											  public IAbilitySystemInterface,
											  public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ACataclysmCharacterBase();

	/**
	 * How large one of the engine's basic shapes is, in centimetres.
	 *
	 * `/Engine/BasicShapes/Cylinder` and the rest occupy a 100cm cube, so a
	 * scale of 1 is 100cm and a component sized to a capsule has to divide by
	 * this. Both the player and every enemy build a stand-in body from those
	 * shapes because this project's Content folder holds no meshes at all.
	 *
	 * HERE RATHER THAN IN EACH .cpp, and that is not tidiness. It was a
	 * `constexpr` in an anonymous namespace in two different .cpp files, and
	 * Unreal's unity build concatenates .cpp files into one translation unit --
	 * so the two anonymous namespaces merged and the second definition was a
	 * redefinition error. It compiled only while the build happened to put the
	 * two files in different unity chunks, which is not something a change can
	 * control.
	 */
	static constexpr float BasicShapeSize = 100.0f;

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	//~ IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	//~ End IGenericTeamAgentInterface

	// ----------------------------------------------------------------------
	// What ACataclysmEnemyController needs in order to drive this character.
	//
	// FIVE VIRTUALS ON THE SHARED BASE RATHER THAN AN INTERFACE, and the reason
	// is that the controller has to work for two classes that have nothing else
	// in common -- a monster and a summoned imp -- and both already derive from
	// here. The defaults make the base inert: a reach of zero means a character
	// is never in range of anything, a roam radius of zero means it never
	// wanders, and AttackTarget does nothing. The player character inherits
	// those defaults and is possessed by a player controller rather than this
	// one, so nothing drives it.
	// ----------------------------------------------------------------------

	/** How close it must be to hit something, in centimetres. Zero never reaches. */
	virtual float AttackReachCm() const { return 0.0f; }

	/** How far it notices a target, in centimetres. Zero notices nothing. */
	virtual float SightRadiusCm() const { return 0.0f; }

	/** Seconds between one of its attacks and the next. */
	virtual float SecondsBetweenAttacks() const { return 1.0f; }

	/**
	 * Which phase of its fight this character is in. One unless it says
	 * otherwise.
	 *
	 * HERE RATHER THAN ON THE ENEMY, so the controller can ask any character
	 * it drives without a cast, the same arrangement the other five hooks on
	 * this class use. A character with no phases answers 1 for ever and every
	 * ability, whose default phase is also 1, is available to it.
	 */
	virtual int32 CurrentPhase() const { return 1; }

	/**
	 * Its health just changed. Inert here.
	 *
	 * THE SAME SHAPE AS `HandleDeath`, and called from the same two places in
	 * `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` -- every write
	 * to health goes through one of them. A creature with health-triggered
	 * phases overrides this; everything else pays one virtual call per hit.
	 *
	 * WHY NOT FROM Tick. A phase decides which ability the brain may choose,
	 * and the brain thinks on its own schedule, so a phase that arrived a
	 * frame late could be a phase that arrived after the choice it should
	 * have changed. Noticing on the hit removes the question.
	 */
	virtual void HealthChanged() {}

	/**
	 * How far from where it started it wanders with nothing in sight, in
	 * centimetres. Zero never roams, which is the default.
	 *
	 * OPT-IN RATHER THAN OPT-OUT, AND THAT IS A BEHAVIOUR DECISION RATHER THAN
	 * A STYLE ONE. Two of the three characters this controller drives should
	 * not wander. A summoned imp exists to fight what its summoner is fighting,
	 * and one that strolled off between fights would be a bug rather than a
	 * feature. A Corrupted Sentinel is designed stationary. Making the base
	 * inert means adding roaming changed the behaviour of exactly the one
	 * character that asked for it, and every existing test that asserts a
	 * monster with nothing in sight is Idle still passes unedited.
	 */
	virtual float RoamRadiusCm() const { return 0.0f; }

	/** Hit the given target once. Called by the controller when it is in reach. */
	virtual void AttackTarget(AActor* Target) {}

	/**
	 * Called once when this character's health first reaches zero.
	 *
	 * INERT ON THE BASE, AND THAT IS NOW ONLY ABOUT MINIONS. The enemy override
	 * marks the creature dead and removes it from the level (issue #517); the
	 * player override marks the player dead, stops them, and stands them back up
	 * after a delay (issue #570). ACataclysmMinion is the one subclass left with
	 * neither, and it leaves the level by its own lifespan rather than by damage.
	 *
	 * WHAT A PLAYER'S DEATH STILL OWES, and it is why the player override stops
	 * where it does: the designed penalty is days off the empire clock, a
	 * per-piece equipment drop chance and a respawn at the capital, and the
	 * running game has no day clock, no equipped inventory and no capital. The
	 * corruption cost and the Last Stand mechanic (#43) are not designed at all.
	 *
	 * CALLED FROM UCataclysmVitalAttributeSet::PostGameplayEffectExecute, which
	 * is the one place in the project that knows a hit has landed and what it
	 * left behind.
	 */
	virtual void HandleDeath() {}

	/**
	 * Show this character using a skill. Issue #1126.
	 *
	 * CALLED FROM `UCataclysmSkillTemplate::CommitAndBegin`, which every one of
	 * the eight skill shapes calls first, so one call there reaches all of them
	 * and the basic attack too. That is the same place and the same reasoning as
	 * `UCataclysmCastEffect::PlayFor`, which issue #811 put there to give every
	 * skill the beat at the caster that none of them had.
	 *
	 * A VIRTUAL RATHER THAN A HELPER, UNLIKE THE CAST EFFECT, because the clip
	 * depends on the skeleton and the skeletons differ. The player wears the
	 * Mannequin; `ACataclysmHellhoundCharacter` wears a Paragon body and also
	 * uses a skill template, so a Mannequin clip played on it would be an
	 * animation for a skeleton it does not have.
	 *
	 * DOING NOTHING IS THE CORRECT DEFAULT AND NOT AN OMISSION. Every enemy that
	 * plays an attack animation already does it from its own class, in
	 * `AttackTarget` or `UseEnemyAbility`, with clips from its own pack. This
	 * exists for the player, which had no such path because until issue #1124 it
	 * had no skeleton to play anything on.
	 */
	virtual void PlayAttackAnimation() {}

	/**
	 * How long from now until the swing just started lands its blow.
	 *
	 * WHY IT IS A STORED ANSWER RATHER THAN A CALCULATION. Issue #1133. The
	 * figure depends on which clip was chosen, and the player cycles through
	 * three of different lengths, so only `PlayAttackAnimation` knows. It works
	 * that number out and leaves it here for `UCataclysmSkillTemplate` to read
	 * on the very next line.
	 *
	 * ZERO IS THE CORRECT DEFAULT AND MEANS "NOW". A character that played no
	 * animation has no swing to wait for, so its blow lands in the activation
	 * frame exactly as every blow in the game did before issue #1133. That
	 * covers every enemy, which animates from its own class and never sets
	 * this, and it covers a checkout with no animation assets, and it covers
	 * every automation test, which runs in a world that is never ticked.
	 *
	 * READ IT ONLY STRAIGHT AFTER `PlayAttackAnimation`. It is not cleared
	 * afterwards, so it holds whatever the last swing decided.
	 */
	float SecondsUntilTheSwingConnects() const
	{
		return SwingConnectsInSeconds;
	}

	/**
	 * What this character can do beyond its ordinary attack, in the order it
	 * would rather use them. Empty means it only has AttackTarget.
	 *
	 * ORDER IS PRIORITY, and the caller takes the first entry that is in range
	 * and off cooldown. Put the ability you would rather use first.
	 *
	 * THE ORDINARY ATTACK IS NOT IN HERE, deliberately. It has no cooldown --
	 * the attack interval is a rate limit, not a cooldown -- so it is always
	 * available and is what happens when nothing in this list qualifies. That
	 * is the property that stops an enemy standing in reach doing nothing
	 * because everything it owns is cooling down, and keeping the fallback out
	 * of the list is what makes it structural rather than a rule to remember.
	 */
	virtual TArray<FCataclysmEnemyAbility> EnemyAbilities() const { return {}; }

	/**
	 * Do the ability at the given index of EnemyAbilities.
	 *
	 * @param Index    into EnemyAbilities, already checked by the caller
	 * @param Target   what it was aimed at, which may since have moved
	 * @param AimedAt  where it was aimed when the wind-up started. A telegraphed
	 *   attack lands where it was marked, not where the target is now, which is
	 *   what makes walking out of one work.
	 */
	virtual void UseEnemyAbility(int32 Index, AActor* Target,
								 const FVector& AimedAt) {}

	/**
	 * Called when an ability's wind-up begins, so the character can start the
	 * animation that goes with it. Does nothing by default.
	 */
	virtual void BeginEnemyAbilityWindUp(int32 Index, AActor* Target) {}

	// ----------------------------------------------------------------------
	// Health, mana and energy shield coming back over time. Issue #653.
	//
	// ON THE SHARED BASE BECAUSE ALL THREE CHARACTERS CARRY THE SAME POOLS and
	// none of them is special: a player, a monster and a summoned minion each
	// have a vital attribute set with the same three regeneration attributes on
	// it. The arithmetic and the design's rules live in UCataclysmRegeneration.
	// What is here is only the clock that drives them and the record of when
	// this character was last hurt.
	// ----------------------------------------------------------------------

	/**
	 * Records that this character has just taken damage, for the energy
	 * shield's refill delay.
	 *
	 * CALLED FOR EVERY HIT THAT TOOK SOMETHING, including one an energy shield
	 * absorbed entirely and including a damage over time tick. The design
	 * requires both: the shield "refills 3 seconds after the character last took
	 * damage", taking damage again inside that window restarts the wait, and
	 * damage over time restarts it as well.
	 *
	 * A hit that was evaded, or that armour and resistance stopped completely,
	 * is not damage taken and does not restart the wait.
	 */
	void NoteDamageTaken();

	/**
	 * Seconds since this character last took damage.
	 *
	 * A LARGE NUMBER RATHER THAN ZERO WHEN IT HAS NEVER BEEN HURT, because zero
	 * would read as "hurt this instant" and would stop the shield ever filling
	 * on a character nothing has touched.
	 */
	float SecondsSinceLastDamage() const;

	/**
	 * Whether the regeneration clock is running on this character.
	 *
	 * EXISTS FOR A TEST, AND THE TEST IS WORTH THE ACCESSOR. Every other check
	 * on this feature calls UCataclysmRegeneration::ApplyStep directly, which is
	 * what the timer calls -- so all of them would still pass if the timer were
	 * never started and nothing regenerated in the running game at all. That is
	 * precisely the failure this feature exists to fix, so it needs its own
	 * guard. A world built by UWorld::CreateWorld is never ticked, so a test can
	 * ask whether the timer is running but can never watch it fire.
	 */
	bool IsRegenerating() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Set by `PlayAttackAnimation` in whichever class overrides it, and read
	 * back through `SecondsUntilTheSwingConnects`. Issue #1133.
	 *
	 * ZERO UNTIL SOMETHING SETS IT, which is what every enemy leaves it at.
	 */
	float SwingConnectsInSeconds = 0.0f;

	/** One step of regeneration. Driven by RegenerationTimer. */
	void RegenerationStep();

	/** Fires every UCataclysmRegeneration::StepSeconds while alive. */
	FTimerHandle RegenerationTimer;

	/**
	 * World time when this character last took damage, in seconds. Negative
	 * means it never has, which is what SecondsSinceLastDamage turns into a
	 * large number rather than into a small one.
	 */
	float LastDamagedAtSeconds = -1.0f;

	/**
	 * Which side this character is on. Set in the subclass constructor.
	 *
	 * NOT REPLICATED, AND IT DOES NOT NEED TO BE. Both subclasses set it in
	 * their constructor, so it is part of the class default object and a client
	 * spawning the class already has the right value before any property could
	 * arrive over the network. A summon does have its side assigned at spawn
	 * time rather than in a constructor, and it stays correct on a client for a
	 * different reason: `UCataclysmTeams::TeamOf` follows the owner chain, and
	 * ownership is replicated. Anything that changes a character's side at
	 * runtime would need this replicated, and nothing does.
	 */
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;

	/**
	 * Points the ability system at this pawn and grants the starting kit.
	 * Must run on both server and client; where it is called from differs, which
	 * is the single most common source of bugs in this area.
	 */
	virtual void InitAbilityActorInfo();

	/** Granted on the server once the ability system is initialised. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Abilities")
	TObjectPtr<UCataclysmAbilitySet> StartingAbilitySet;
};
