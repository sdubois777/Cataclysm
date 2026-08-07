// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "CataclysmCharacterBase.generated.h"

class UAttributeSet;
class UCataclysmAbilitySet;
class UCataclysmAbilitySystemComponent;

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
