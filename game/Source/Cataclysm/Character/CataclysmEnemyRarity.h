// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmEnemyRarity.generated.h"

class UDataTable;

/**
 * Which rarity a spawned enemy is.
 *
 * WHAT THIS ANSWERS, AND WHY IT DID NOT EXIST BEFORE. Every creature in the game
 * spawned Common unless a step was typed into `game/Config/DefaultGame.ini` by
 * hand, because nothing anywhere drew a rarity. The eight rarity colours, the
 * varying drop counts and the magic find a rarer enemy adds to its own drops
 * were therefore almost never seen in play. Issue #508.
 *
 * THE WEIGHTS WERE NOT INVENTED HERE AND THEY WERE NOT MISSING. Issue #508 was
 * filed saying the spawn weights "do not exist anywhere". They already did, in
 * two places that agree: `scoring.DUNGEON_SCORE_MIX` in
 * `sim/cataclysm_sim/scoring.py`, and the Dungeon Score Formula section of
 * `docs/Cataclysm_GDD_v2.md`, which states all five and says outright "The five
 * weights are how common each rarity is, and they sum to 1." What was missing
 * was any way for the engine to read them. That is now the SpawnWeight column on
 * `game/Data/EnemyRarities.csv`, generated from the model.
 *
 * A STATIC OVER A TABLE AND A STREAM, so it can be tested. The automation test
 * command in `tools/unreal_build.py` passes -nullrhi and spawning an actor needs
 * a world, so a roll that lived on the game mode could only be checked by
 * playing. This takes a table and a random stream and answers a number.
 *
 * ONE ENEMY AT A TIME, INDEPENDENTLY. Each creature draws its own rarity from
 * the five weights rather than a floor being given an exact count of each. That
 * is what "how common each rarity is" means and it is the simplest thing that
 * matches the sentence. Diablo II does it the other way: its `Levels.txt` gives
 * each area a `MonUMin` and `MonUMax`, "Minimum - Maximum Unique and Champion
 * Monsters Spawned in this Level", which guarantees a spread rather than leaving
 * it to chance. That guarantee is worth having when a floor holds dozens of
 * creatures and the variance would show; it is not worth having yet, because
 * there are no dungeon floors -- procedural generation is issue #40 and the
 * dungeon runtime is issue #41 -- and over the handful the sandbox places, five
 * independent draws come up all-Common about 8% of the time. `docs/DECISIONS.md`
 * records the comparison and where a minimum would go if it is ever wanted.
 */
UCLASS()
class CATACLYSM_API UCataclysmEnemyRarity : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The enemy rarity table, loaded once. Null when it cannot be read. */
	static const UDataTable* LoadEnemyRarityTable();

	/**
	 * Draws one enemy's rarity, as a step from 0 for Common.
	 *
	 * @return the step drawn, or 0 when the table is missing or holds no weight
	 *         at all. Common is the right answer to "something went wrong": it
	 *         is what every creature spawned as before this existed, and it is
	 *         the rung that gives the least, so a fault cannot quietly make the
	 *         game more generous.
	 *
	 * NEVER A CATACLYSM BOSS, because its weight is 0 and a weight of 0 cannot
	 * be drawn. That is the design's rule rather than a special case here: the
	 * Dungeon Score Formula section says a Cataclysm Boss "does not appear on an
	 * ordinary floor", so it is placed and not rolled.
	 */
	static int32 RollRarityStep(const UDataTable* EnemyRarityTable,
								FRandomStream& Stream);

	/**
	 * The weight a step carries, for a caller that wants to reason about the mix
	 * rather than draw from it.
	 *
	 * @return 0 for a step the table does not hold, and 0 for Cataclysm Boss
	 */
	static float SpawnWeightForStep(const UDataTable* EnemyRarityTable,
									int32 Step);

	/**
	 * Every step the table carries a positive weight for, lowest first.
	 *
	 * SORTED, BECAUSE A DataTable IS A MAP. Its rows come back in whatever order
	 * the container happens to hold them, so a draw that walked them unsorted
	 * would give a different answer from the same stream on a different run.
	 * `UCataclysmDropRoll::RollMaterial` gathers and sorts for the same reason.
	 */
	static void SpawnableSteps(const UDataTable* EnemyRarityTable,
							   TArray<int32>& OutSteps);

	/**
	 * The value a sandbox setting carries to mean "draw one" rather than "use
	 * this rung".
	 *
	 * NEGATIVE, so it cannot collide with a rung. The steps are 0 to 5 and every
	 * one of them is a rarity somebody might want to force.
	 */
	static constexpr int32 RollTheRarity = -1;
};
