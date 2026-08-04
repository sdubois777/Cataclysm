// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CataclysmGameMode.generated.h"

/**
 * Names the pawn, controller and player state the game runs with.
 *
 * GameModeBase rather than GameMode: the match state machine GameMode adds --
 * waiting to start, in progress, waiting post match -- describes a session with
 * rounds. This game has a run that ends when the capital falls or the boss dies,
 * and the empire layer owns that. There is no round to wait for.
 *
 * Set as the project default in Config/DefaultEngine.ini rather than per level,
 * so a level opened directly still gets the real player pawn.
 */
class ACataclysmEnemyCharacter;

UCLASS()
class CATACLYSM_API ACataclysmGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACataclysmGameMode();

	virtual void StartPlay() override;

	/**
	 * Puts training dummies in a ring around the player start.
	 *
	 * Public so a test can call it against a world it built, and so it can be
	 * called again from the console while playing.
	 *
	 * @return how many were spawned
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Sandbox")
	int32 SpawnTrainingDummies();

	/** The dummies this game mode spawned, oldest first. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Sandbox")
	TArray<TObjectPtr<ACataclysmEnemyCharacter>> TrainingDummies;

protected:
	/**
	 * How many enemies to put in the level at the start of play.
	 *
	 * WHY THE GAME MODE SPAWNS THEM RATHER THAN THE LEVEL HOLDING THEM. Issue
	 * #170: game/Content/Maps/L_Sandbox.umap contained a floor, a light, a sky,
	 * a player start and navigation, and no enemy at all, so every skill built
	 * so far had nothing to act on. Placing actors by hand would put them inside
	 * a binary .umap, which cannot be reviewed in a diff and which issue #140
	 * already records as a source of churn every time the editor opens.
	 *
	 * SANDBOX SCAFFOLDING, NOT THE REAL SPAWNER. Dungeon population is issue
	 * #40 and the seven designed Demonic enemies are issue #39. Set this to zero
	 * to turn it off.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	int32 TrainingDummyCount = 5;

	/** How far from the player start the ring sits, in centimetres. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float TrainingDummyRingRadius = 600.0f;

	/**
	 * How much health each one has.
	 *
	 * HIGH ON PURPOSE. A Heavy Attack with an unupgraded Greataxe deals about
	 * 102, so at an enemy's default 100 health a dummy dies to one press and
	 * there is nothing to watch. At this figure it survives long enough to see a
	 * burn tick, a ground zone, and an Ultimate. Real enemy health comes from
	 * rarity and difficulty, which is issue #39.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "1"))
	float TrainingDummyHealth = 5000.0f;

	/**
	 * What one of a dummy's own attacks is worth.
	 *
	 * THEY FIGHT BACK NOW, WHICH THEY DID NOT BEFORE. Until issue #163 no pawn
	 * except the player had a controller, so a dummy stood where it was spawned
	 * and never acted. It now walks to the player and hits them, which is the
	 * only way to see in a play session that the behaviour works at all.
	 *
	 * A JUDGEMENT, AND DELIBERATELY SMALL. Five of them at 20 damage every 1.5
	 * seconds come to about 67 damage a second against a character starting at
	 * 100 health, so standing in the middle of the ring doing nothing is fatal in
	 * a couple of seconds while walking out of it is not. Set this to zero for a
	 * ring that only absorbs damage, which is what it used to be.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float TrainingDummyAttackDamage = 20.0f;
};
