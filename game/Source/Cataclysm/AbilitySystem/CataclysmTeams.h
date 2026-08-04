// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "CataclysmTeams.generated.h"

/**
 * Which side something is on.
 *
 * The numbers are team identifiers, not an ordering, and they are what
 * `FGenericTeamId` stores. 255 is the engine's `FGenericTeamId::NoTeam` and is
 * deliberately not listed here: "no side" is the absence of a value rather than
 * a third side, and `UCataclysmTeams::TeamOf` returns `FGenericTeamId::NoTeam`
 * for anything that has not been given one.
 */
UENUM(BlueprintType)
enum class ECataclysmTeam : uint8
{
	/** The player, a second player in a co-operative session, and their summons. */
	Players = 0,

	/** Everything a dungeon spawns to fight them. */
	Monsters = 1,
};

/**
 * Which side an actor is on, and how two sides regard each other.
 *
 * WHY THE ENGINE'S OWN MECHANISM RATHER THAN A NEW ONE. `IGenericTeamAgentInterface`
 * and `FGenericTeamId` live in `AIModule`, which this project already depends on
 * for click-to-move. Using them means `UAIPerceptionComponent`, `AAIController`
 * and the environment query system all understand this project's sides without
 * being told, which matters because the enemy behaviour of issue #163 is the
 * next thing to be built on top of this.
 *
 * WHY NOT LYRA'S SHAPE. Epic's own Lyra sample does not use the generic
 * interface; it declares `ILyraTeamAgentInterface` and a `ULyraTeamSubsystem`
 * that owns team assignment. That extra machinery pays for itself when a game
 * mode assigns teams at runtime, drives team-based spawn points, and scores per
 * team. None of that exists here: there are two sides, they are fixed, and a
 * character is born onto one. A subsystem would be a layer with nothing in it.
 *
 * THREE ATTITUDES, NOT TWO. `ETeamAttitude` has Friendly, Neutral and Hostile.
 * Lyra collapses this to same-team or different-team. Keeping all three means a
 * neutral actor -- a shrine, a destructible barrel, a town guard -- can be added
 * later without every caller changing shape. Nothing in the project is neutral
 * today, and `AttitudeBetween` returns Neutral only when it is asked about a
 * null actor.
 *
 * WHAT HAVING NO TEAM MEANS: HOSTILE, NOT NEUTRAL, and that is a deliberate
 * choice of failure mode. An enemy class that forgets to set its team is still
 * something the player can kill; the alternative makes it silently invulnerable
 * and immune to every skill, which is far harder to notice. Note this is the
 * opposite of the engine's own default solver, which treats two actors that both
 * have no team as equal and therefore friendly -- so this class does not use
 * `FGenericTeamId::GetAttitude`.
 */
UCLASS()
class CATACLYSM_API UCataclysmTeams : public UObject
{
	GENERATED_BODY()

public:
	/** The team identifier for one of this project's sides. */
	static FGenericTeamId IdFor(ECataclysmTeam Team)
	{
		return FGenericTeamId(static_cast<uint8>(Team));
	}

	/**
	 * Which side an actor is on, or `FGenericTeamId::NoTeam`.
	 *
	 * FOLLOWS THE OWNER CHAIN, which is what puts a thing a character made on
	 * that character's side without the thing having to know about teams.
	 * `ACataclysmGroundZone` is the case that needs it: a patch of burning
	 * ground has no ability system and no team of its own, and it is passed as
	 * the instigator when it sweeps for who is standing in it.
	 *
	 * The engine's `FGenericTeamId::GetTeamIdentifier` asks only the actor
	 * itself, so the walk is done here rather than relying on it.
	 */
	static FGenericTeamId TeamOf(const AActor* Actor);

	/**
	 * How the first actor regards the second.
	 *
	 * Neutral only when either is null. Otherwise Friendly or Hostile, by the
	 * rules in the class comment.
	 */
	static ETeamAttitude::Type AttitudeBetween(const AActor* Actor, const AActor* Other);

	/**
	 * Whether one actor is the other's owner, however many links away.
	 *
	 * Checked in both directions, so a summoner is not an enemy to its own
	 * minion any more than the minion is to the summoner.
	 */
	static bool SharesAnOwnerChain(const AActor* A, const AActor* B);
};
