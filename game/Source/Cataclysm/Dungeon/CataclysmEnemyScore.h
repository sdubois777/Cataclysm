// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CataclysmEnemyScore.generated.h"

/**
 * The four kinds of dungeon, from `docs/Cataclysm_GDD_v2.md` section VIII.
 *
 * THE ORDER IS THE ORDER THEY GET HARDER IN, and the numbers are deliberate: a
 * saved dungeon or a data table row that stores one of these stores the number,
 * so inserting a kind in the middle later would change what an existing value
 * means. Add to the end.
 */
UENUM(BlueprintType)
enum class ECataclysmDungeonType : uint8
{
	Basic       = 0,
	Quest       = 1,
	FallenCity  = 2	UMETA(DisplayName = "Fallen City"),
	Cataclysm   = 3,
};

/**
 * What a dungeon does differently, from `docs/Cataclysm_GDD_v2.md` section VIII.
 *
 * `None` IS A REAL VALUE AND THE COMMON ONE. Most dungeons have no sub-type at
 * all, and the score model gives it a weight of zero rather than treating it as
 * missing data.
 *
 * TIMED CARRIES NO WEIGHT EITHER, and that is a design statement rather than an
 * oversight: the design document says "A time limit is a constraint on the
 * player rather than on the enemies, so it changes nothing about what an
 * encounter is worth."
 */
UENUM(BlueprintType)
enum class ECataclysmDungeonSubType : uint8
{
	None         = 0,
	Timed        = 1,
	Horde        = 2,
	Siege        = 3,
	CowLevel     = 4	UMETA(DisplayName = "Cow Level"),
	Elite        = 5,
	Volatile     = 6,
	Sacrificial  = 7,
};

/**
 * Everything the Enemy Score model reads about where a fight is happening.
 *
 * NOTHING HERE IS ABOUT THE ENEMY except through its rarity, which is passed
 * separately, because one floor produces a score for every rarity at once and
 * the expensive part of the formula is shared between them.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmScoredFloor
{
	GENERATED_BODY()

	/** 1 to 8. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Score")
	int32 DifficultyTier = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Score")
	ECataclysmDungeonType Type = ECataclysmDungeonType::Basic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Score")
	ECataclysmDungeonSubType SubType = ECataclysmDungeonSubType::None;

	/**
	 * How many floors the whole dungeon has.
	 *
	 * NOT OPTIONAL, AND NOT ONE TERM AMONG SEVERAL. The model's baseline is
	 * driven by `currentFloor / totalFloors`, so without a total there is no
	 * floor ratio and the formula has no value at all. That is why a dungeon
	 * needed a length before this port could be used, which is issue #926.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Score",
			  meta = (ClampMin = "1"))
	int32 TotalFloors = 1;

	/** Which floor of it, counted from 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Score",
			  meta = (ClampMin = "1"))
	int32 FloorNumber = 1;

	/**
	 * What the floor's modifiers add, as a flat number of points.
	 *
	 * ZERO UNTIL DUNGEON MODIFIERS EXIST. It is a flat addend in the model
	 * rather than a multiplier, so leaving it at zero is exactly "no modifiers"
	 * and not an approximation. Issue #41 is what fills it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Score")
	float ModifierScore = 0.0f;
};

/**
 * Enemy Score, ported from `enemy_scores` in `sim/cataclysm_sim/scoring.py`.
 *
 * WHAT IT IS FOR. Two things, and the second is why it was ported now.
 * Enemy Score is what the Overwhelm ladder in section IV of
 * `docs/Cataclysm_GDD_v2.md` is rated against, and since 2026-08-24 it is also
 * the experience a kill grants: "An enemy's Enemy Score IS the experience it
 * grants." `UCataclysmExperience` has the curve that spends it.
 *
 * THE CHAIN OF AUTHORITY THIS SITS AT THE END OF, and it is four links long:
 *
 *     src/utils/calculateScores.tsx        authoritative, another repository
 *       ^ checked by sim/verify_scoring_port.py
 *     sim/cataclysm_sim/scoring.py         a verified port, never hand-edited
 *       ^ checked by tools/tests/test_enemy_score_formula.py   (the document)
 *       ^ checked by tools/tests/test_enemy_score_port.py      (this file)
 *     docs/Cataclysm_GDD_v2.md section X   what a person reads
 *     THIS FILE                            what the game runs
 *
 * `CLAUDE.md` records that the Python port has silently drifted from its own
 * source twice. This is a third copy of the same numbers, so it is compared
 * against the second rather than trusted.
 *
 * DEPTH IS LENGTH, NOT DIFFICULTY, and this surprises people. Because the
 * baseline term is driven by `FloorNumber / TotalFloors`, a 150-floor dungeon
 * and a 20-floor dungeon at the same difficulty tier are nearly equally hard per
 * floor. The deep one just takes longer.
 *
 * NOTHING IN THE FORMULA MULTIPLIES. Every contribution is either a fraction of
 * the difficulty tier's width or a flat number of points, and they are added.
 * That is what makes a rarity's weight the share of mitigation Overwhelm strips,
 * which section X relies on.
 */
UCLASS()
class CATACLYSM_API UCataclysmEnemyScore : public UObject
{
	GENERATED_BODY()

public:
	static constexpr int32 LowestDifficultyTier = 1;
	static constexpr int32 HighestDifficultyTier = 8;

	/** Six rarities, Common through Cataclysm Boss, as Step 0 to 5 in
	 *  `game/Data/EnemyRarities.csv`. */
	static constexpr int32 RarityStepCount = 6;

	// The three bare numbers in the formula. Named rather than inlined so the
	// port test can read them back and compare them with the model, which is
	// how issue #6 happened in the first place.
	static constexpr double BaselineWeight = 0.9;
	static constexpr double ProceduralDivisor = 20.0;
	static constexpr double ProceduralPerFloor = 0.5;
	static constexpr double DepthTensionPerTier = 1.2;

	/**
	 * The maximum Power Score a player is expected to reach by the end of each
	 * difficulty tier. Index 0 is the floor of tier 1.
	 *
	 * THE SAME EIGHT ANCHORS `UCataclysmPowerScore` DERIVES ITS WEIGHTS FROM.
	 * They are duplicated rather than shared because the two classes are in
	 * different folders and neither owns the other; the port test compares both
	 * lists against `sim/cataclysm_sim/scoring.py`, so a drift in either fails.
	 */
	static const TArray<double>& TierAnchors();

	/** The bottom and top of a difficulty tier's Power Score band. */
	static void TierBounds(int32 Tier, double& OutMin, double& OutMax);

	/** How wide a difficulty tier's band is. Every fractional term is a share
	 *  of this. */
	static double TierWidth(int32 Tier);

	/** What a dungeon of this kind adds, as a fraction of the tier width. */
	static double TypeWeight(ECataclysmDungeonType Type);

	/** What its sub-type adds, as a fraction of the tier width. */
	static double SubTypeWeight(ECataclysmDungeonSubType SubType);

	/** What a rarity adds, as a fraction of the tier width. Step 0 to 5. */
	static double RarityWeight(int32 RarityStep);

	/** The per-dungeon-type constant the procedural term is built from. */
	static double FloorScalingBase(ECataclysmDungeonType Type);

	/**
	 * One rarity's score on one floor.
	 *
	 * ROUNDED WITH floor(x + 0.5), which is what JavaScript's `Math.round` does
	 * and what `_js_round` in `sim/cataclysm_sim/scoring.py` does. It is NOT
	 * `FMath::RoundToInt`: the reference model rounds every score it returns and
	 * exact halves are common here, because the formula is built from halves and
	 * fifths. Banker's rounding disagreed with the reference on about 2% of
	 * inputs, always by exactly one.
	 *
	 * A NONSENSE FLOOR IS CLAMPED RATHER THAN REFUSED. The difficulty tier is
	 * held inside 1 to 8, the total floors to at least 1, and the floor number
	 * to somewhere inside the dungeon. This is reached from a running game where
	 * a console variable can say anything, and a score of zero for a creature
	 * that should be worth something is worse than the nearest sensible answer.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Score")
	static int32 ScoreFor(const FCataclysmScoredFloor& Floor, int32 RarityStep);

	/** Every rarity's score on one floor, indexed by rarity step 0 to 5. */
	static TArray<int32> ScoresFor(const FCataclysmScoredFloor& Floor);

	/**
	 * Where a fight is happening, read out of the world's game mode.
	 *
	 * SHAPED LIKE `ACataclysmGameMode::DifficultyTierIn`, and it answers with
	 * the defaults above when there is no world or no Cataclysm game mode. That
	 * matters: an automation test that kills a creature in a bare world still
	 * gets a real score rather than nothing.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Score",
			  meta = (WorldContext = "WorldContext"))
	static FCataclysmScoredFloor FloorIn(const UObject* WorldContext);
};
