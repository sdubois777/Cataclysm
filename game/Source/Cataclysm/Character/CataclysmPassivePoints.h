// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmPassivePoints.generated.h"

/**
 * How many passive points a character has, and where they came from.
 *
 * `docs/Cataclysm_GDD_v2.md` section XII states the three sources exactly:
 *
 *     Per level: 1 passive skill point and 1 attribute point.
 *     Every 10 levels: 5 bonus passive points.
 *     Defeating a unique Cataclysm boss for the first time: 10 bonus passive
 *     points.
 *
 * NOTHING AWARDED A SINGLE ONE OF THEM UNTIL THIS. The design has said 230 since
 * the document was written, every class tree file carries `pointBudget: 230`, and
 * no code anywhere counted, stored or spent one.
 *
 * COMPUTED FROM THE LEVEL RATHER THAN ACCUMULATED, which is the same shape
 * `ACataclysmPlayerState::AttributePointsAvailable` uses and it is chosen for
 * the same reason. A running total that something adds to on each level-up has
 * two ways to go wrong that a computation does not: an award missed while
 * offline, and an award applied twice by a reload. A function of the level
 * cannot drift from the level.
 *
 * THE BOSS KILLS ARE THE EXCEPTION AND HAVE TO BE STORED, because "first time"
 * is a fact about history rather than about the character's present state.
 * `UCataclysmCharacterSave::DefeatedCataclysmBosses` holds which ones, so ten
 * points is granted once per boss however many times it dies afterwards.
 *
 * THE 230 REQUIRES EVERY BOSS AND THAT IS DELIBERATE. Levelling to 100 gives
 * 150; the eight bosses give the other 80. The project owner confirmed on
 * 2026-08-25 that a character which never fights one tops out at 150 and never
 * reaches the budget the trees are designed against. `docs/DECISIONS.md` carries
 * it.
 */
UCLASS()
class CATACLYSM_API UCataclysmPassivePoints : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** One passive point for each level. */
	static constexpr int32 PerLevel = 1;

	/** Five more on every tenth level: 10, 20, and so on to 100. */
	static constexpr int32 PerTenLevels = 5;

	/** How many levels apart those bonuses are. */
	static constexpr int32 BonusEvery = 10;

	/** Ten for the first defeat of each unique Cataclysm boss. */
	static constexpr int32 PerFirstBossKill = 10;

	/**
	 * How many unique Cataclysm bosses there are.
	 *
	 * EIGHT, ONE PER DAMAGE TYPE. `docs/Cataclysm_GDD_v2.md` gives each of the
	 * eight Cataclysms a boss at the end of its dungeon, and eight times ten is
	 * the 80 that takes the budget from 150 to 230. Nothing in the game creates
	 * one yet, so this is what the arithmetic is checked against rather than a
	 * count of anything that exists.
	 */
	static constexpr int32 UniqueBosses = 8;

	/** The per-character budget the class trees are designed against. */
	static constexpr int32 Budget = 230;

	/**
	 * Points from levelling alone, at this level.
	 *
	 * A LEVEL 1 CHARACTER HAS ONE, not zero. The design says "per level", and
	 * the first level is a level. That gives 100 from the per-level award and
	 * 50 from the ten bonuses, which is the 150 the document states.
	 *
	 * CLAMPED TO THE LEGAL RANGE rather than refused, because this is asked
	 * about a level a console variable can set.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	static int32 FromLevel(int32 Level);

	/** Points from having defeated this many unique Cataclysm bosses. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	static int32 FromBossKills(int32 UniqueBossesDefeated);

	/**
	 * Everything a character at this level with these first kills has earned.
	 *
	 * NOT CAPPED AT THE BUDGET, and it cannot exceed it: 150 from a level 100
	 * character plus 80 from all eight bosses is exactly 230. A cap here would
	 * hide the day those two stop adding up, which is what
	 * `Cataclysm.Passives.TheThreeAwardsAddUpToTheStatedBudget` is for.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	static int32 Available(int32 Level, int32 UniqueBossesDefeated);
};
