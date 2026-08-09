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

	/**
	 * Puts Brutes in the sandbox, beyond where the ring of training dummies
	 * would be. Public for the same reasons SpawnTrainingDummies is.
	 *
	 * SEPARATE FROM SpawnTrainingDummies RATHER THAN A PARAMETER ON IT, so the
	 * dummy ring's settled behaviour is untouched by adding a second kind of
	 * enemy. Returns how many were placed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Sandbox")
	int32 SpawnBrutes();

	/** Every Brute this game mode placed. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Sandbox")
	TArray<TObjectPtr<class ACataclysmBruteCharacter>> Brutes;

	/**
	 * Puts Abyssal Wardens in the sandbox, on the same arrangement as the
	 * Brutes and for the same reasons. Returns how many were placed.
	 *
	 * IT CANNOT CHASE, which changes what watching it is like. Its designed
	 * movement speed is 2.8 metres per second and it has no chase speed at all,
	 * against player classes at 3.5, 4.0 and 4.6, so a player who walks
	 * backwards is never caught. Its charge, which is what the design gives it
	 * to close with, cannot be executed by the current brain -- issue #491.
	 * Stand still to see it fight.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Sandbox")
	int32 SpawnAbyssalWardens();

	/** Every Abyssal Warden this game mode placed. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Sandbox")
	TArray<TObjectPtr<class ACataclysmAbyssalWardenCharacter>> AbyssalWardens;

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
	 * #40 and the seven designed Demonic enemies are issue #39.
	 *
	 * ZERO SINCE 2026-08-07, ON THE PROJECT OWNER'S INSTRUCTION. Five cylinders
	 * chasing the same player collide with and crowd around the Brute, which
	 * makes it hard to watch what the Brute itself is doing. They were the only
	 * thing in the sandbox to hit before there was a designed enemy; now there
	 * is one, and they are in the way. Set this back to 5 to restore them.
	 *
	 * PUBLIC, unlike the other sandbox settings, so a test can ask for dummies
	 * regardless of what the default happens to be. The two tests in
	 * CataclysmSandboxTests.cpp check that the spawner works, not that anybody
	 * currently wants five of them, and they would otherwise fail the moment
	 * this default changed -- which is exactly what happened when it went to 0.
	 */
public:
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	int32 TrainingDummyCount = 0;

protected:

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

	/** How many Brutes to place. Zero for none. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	int32 BruteCount = 1;

	/**
	 * How far from the player start, in centimetres.
	 *
	 * Beyond the dummy ring at 600, so the Brute is not standing among them and
	 * has ground to walk across. Inside the sandbox's 4000 x 4000 cm navigation
	 * bounds, because outside them it cannot path at all.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float BruteDistanceCm = 1200.0f;

	/**
	 * How much health each one has.
	 *
	 * SANDBOX SCAFFOLDING, NOT THE DESIGNED FIGURE, exactly as
	 * TrainingDummyHealth is. This is the dummy's 5000 times the Brute's
	 * health_share of 2.20 from ARCHETYPES in sim/cataclysm_sim/enemy_stats.py,
	 * so a Brute is as much tougher than a dummy as the model says it should be,
	 * on a scale the sandbox player can actually fight. The real figure comes
	 * from tier, floor and rarity through the enemy score model, which has no
	 * port into the engine yet: issues #39 and #355.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "1"))
	float BruteHealth = 11000.0f;

	/**
	 * What one of its attacks is worth. The dummy's 20 times the Brute's
	 * damage_share of 1.75, on the same scaffolding reasoning as BruteHealth.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float BruteAttackDamage = 35.0f;

	/** How many Abyssal Wardens to place. Zero for none. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	int32 AbyssalWardenCount = 1;

	/**
	 * How far from the player start each one is put.
	 *
	 * FURTHER OUT THAN THE BRUTES AT 1200 BY MORE THAN ITS OWN RING, because
	 * this creature's Molten Roar marks a ring 6.5 metres across and the two
	 * should not be standing inside each other's telegraphs before the player
	 * has done anything.
	 *
	 * IT WAS 1800 UNTIL 2026-08-09, and the ring growing from 5.6 to 6.5 metres
	 * closed the gap: 1800 less the Brutes' 1200 is 600 cm, which the 650 cm
	 * ring now reaches across. `test_it_is_spawned_further_out_than_the_brutes`
	 * in `tools/tests/test_warden_matches_the_model.py` failed on exactly that
	 * and is what caught it. Issues #487 and #496.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float AbyssalWardenDistanceCm = 1900.0f;

	/**
	 * How much health each one has.
	 *
	 * SANDBOX SCAFFOLDING, NOT THE DESIGNED FIGURE, on exactly the reasoning
	 * BruteHealth records: the training dummy's 5000 times this creature's
	 * health_share of 3.50 from ARCHETYPES in sim/cataclysm_sim/enemy_stats.py.
	 * The real figure comes from tier, floor and rarity through the enemy score
	 * model, which has no port into the engine yet.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "1"))
	float AbyssalWardenHealth = 17500.0f;

	/**
	 * What one of its ordinary swings is worth. The dummy's 20 times its
	 * damage_share of 1.90, on the same scaffolding reasoning.
	 *
	 * MOLTEN ROAR IS WORTH FOUR OF THESE, because the Ultimate slot is 400%.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Sandbox", meta = (ClampMin = "0"))
	float AbyssalWardenAttackDamage = 38.0f;
};
