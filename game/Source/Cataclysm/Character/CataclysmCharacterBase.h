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
	 * INERT ON THE BASE, so the player character is unaffected. A player's death
	 * is a much larger question than an enemy's -- it owes a death penalty, a
	 * corruption cost and the Last Stand mechanic, none of which is designed --
	 * so issue #517 does the enemy half alone and this default is what leaves the
	 * other half untouched rather than half-built.
	 *
	 * CALLED FROM UCataclysmVitalAttributeSet::PostGameplayEffectExecute, which
	 * is the one place in the project that knows a hit has landed and what it
	 * left behind.
	 */
	virtual void HandleDeath() {}

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

protected:
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
