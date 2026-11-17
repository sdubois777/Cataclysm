// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmHarvestedSoul.generated.h"

class ACataclysmDungeonGameMode;
class ACataclysmEnemyCharacter;
class UWorld;

/** How a harvested soul's flight ended, or that it has not. Issue #1671. */
enum class ECataclysmHarvestedSoulEnd : uint8
{
	/** Still floating. */
	InFlight,
	/** It reached its demon, and the game mode gave that demon the soul. */
	Arrived,
	/** Its demon was gone and no other was within reach of where the soul was. It gave nothing. */
	NoDemonLeft,
	/** It flew `SoulHarvestSoulLifetimeSeconds` without arriving. It gave nothing. */
	Faded,
};

/**
 * A demonic soul, released where a creature died on a floor carrying Soul Harvest, floating to the nearest demon.
 * Issue #1671; the row and every ruling are on `UCataclysmDungeonModifierEffects::SoulHarvestKey`.
 *
 * RELEASED BY THE GAME MODE'S DEATH LISTENER, `ACataclysmDungeonGameMode::NoteDeathForSoulHarvest`, only when a demon
 * stands within reach of the death; the game mode chooses that demon. IT GIVES NOTHING ITSELF: on arrival it hands its
 * demon to `ACataclysmDungeonGameMode::SoulHarvestSoulArrived`, which writes the soul's figures where the instant grant
 * wrote them, so the figures, the cap and the save of what a creature holds stay in one place.
 *
 * NOT `ACataclysmProjectile`, WHICH #1671 OFFERED, AND THE REASON IS WHAT THAT CLASS DOES RATHER THAN WHAT IT IS.
 * `ACataclysmProjectile::Fire` refuses without a living-or-valid firer and takes the firer's side; it flies a fixed
 * direction for a fixed range, hits every enemy of its firer along its path with a share of the firer's weapon damage,
 * pierces, bounces, returns, is turned by Singularity Wells and is taken by a Void Aura. A soul has no firer (the
 * creature that released it is dead), follows a demon that moves, deals nothing and must touch nothing on the way.
 * Using that class would mean switching off nearly everything it does and adding the one thing it lacks, homing.
 * What is reused instead is its pattern: a public `Step` the tests drive, because the automation tests' worlds never
 * tick, and `Tick` calling it in play.
 *
 * NOTHING COLLIDES WITH IT AND NOTHING TARGETS IT. Neither the row nor #1671 gives the player a way to intercept a
 * soul, so it has no collision and passes through walls; no search for enemies can find it, having no ability system.
 *
 * WHAT THE PLAYER SEES: a tag, "Demonic soul", in one fixed colour, drawn by `ACataclysmHUD` over the soul as it
 * floats, as a Reaper's Soul's tag is drawn. No mesh.
 *
 * ENDED BY A CLEARED FLOOR, `UCataclysmFloorContents::ClearTheFloor` destroying every one beside the Reaper's Souls.
 * NOT SAVED: a save counts every soul in flight as arrived at its demon (`SoulHarvestHeldBy`), and a load clears the
 * floor, which destroys any soul standing in it.
 */
UCLASS()
class CATACLYSM_API ACataclysmHarvestedSoul : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmHarvestedSoul();

	/**
	 * Release a soul at `Where`, bound for `Toward`, for the game mode `By`. Nothing when either is gone or the world
	 * is. The soul does not move in the frame it is released: it is first stepped on the next tick, so no soul arrives
	 * in the instant of the death that released it.
	 *
	 * @return the soul, or null
	 */
	static ACataclysmHarvestedSoul* Release(ACataclysmDungeonGameMode* By, const FVector& Where,
											ACataclysmEnemyCharacter* Toward);

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Float for `DeltaSeconds`. Turns to another demon first when its own no longer qualifies, by asking the game mode;
	 * arrives, fades, or moves. PUBLIC SO A TEST CAN DRIVE IT, as `ACataclysmProjectile::Step` is.
	 *
	 * @return true while it is still floating, false once it has ended
	 */
	bool Step(float DeltaSeconds);

	/**
	 * The demon it is bound for now, or null once it has ended or lost it. In the .cpp, as `ReleasedBy` is, because the
	 * class is only declared here.
	 */
	ACataclysmEnemyCharacter* Target() const;

	/** How it ended, or `InFlight`. */
	ECataclysmHarvestedSoulEnd HowItEnded() const { return Ended; }

	/** The game mode that released it. */
	ACataclysmDungeonGameMode* ReleasedBy() const;

	/** Where it was released: the death that released it. */
	FVector ReleasedAt() const { return Origin; }

	/** Seconds it has floated. */
	float SecondsFloated() const { return Floated; }

	/** How many times it has turned to another demon. */
	int32 TimesTurned() const { return Turned; }

	/**
	 * Every soul in this world still floating, released by `By` when it is not null. Asked rather than kept, as
	 * `ACataclysmReaperSoul::SoulsToName` asks, so a destroyed soul is never in a list.
	 */
	static void FloatingIn(const UWorld* World, const ACataclysmDungeonGameMode* By,
						   TArray<ACataclysmHarvestedSoul*>& Out);

	/** The words on its tag. */
	static const TCHAR* NameText;

	/** The one colour its tag is drawn in: a deep red, which neither a rarity nor a Reaper's Soul uses. */
	static FLinearColor NameColour();

	/** How far above the soul its tag is drawn. */
	static constexpr float NameHeightCm = 40.0f;

private:
	/** End the flight: record how, forget the demon, and destroy the actor. */
	void End(ECataclysmHarvestedSoulEnd How);

	TWeakObjectPtr<ACataclysmDungeonGameMode> Releaser;
	TWeakObjectPtr<ACataclysmEnemyCharacter> Toward;
	FVector Origin = FVector::ZeroVector;
	float Floated = 0.0f;
	int32 Turned = 0;
	ECataclysmHarvestedSoulEnd Ended = ECataclysmHarvestedSoulEnd::InFlight;
};
