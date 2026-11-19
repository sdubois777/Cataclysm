// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireMap.h"
#include "CataclysmEnemyScore.generated.h"

class ACataclysmGameMode;

// `ECataclysmDungeonType` AND `ECataclysmDungeonSubType` USED TO BE DECLARED
// HERE, and are now in `Empire/CataclysmDungeonKind.h` in the `CataclysmEmpire`
// module, which is included above. Every file that includes this one still gets
// both, unchanged and with the same numbering.
//
// They moved because what kind a dungeon is, is decided by the surge scheduler
// in the empire layer, and `CataclysmEmpire` must not depend on `Cataclysm` --
// so the empire layer could not see them here. Declaring a second copy would
// have made one design fact into two numbers that could drift apart. That header
// says the rest. Issue #1083.

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

	/**
	 * The ring the dungeon was rolled on: `FCataclysmDungeon::CityTier`, which also set its depth. Issue #2749's ring
	 * slice.
	 *
	 * READ BY THE STRENGTH PATH ALONE. At difficulty tier 1 it moves a creature's damage (`DamageScale`); `ScoreFor`
	 * does not read it, so the experience a kill grants is the same on every ring, as the ruling asked for damage only.
	 *
	 * AN OUTPOST WHEN NOTHING SAYS OTHERWISE, which is what every floor read before the ring existed: the sandbox, a
	 * world with no game mode, and a dungeon walked with no empire dungeon bound.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Score")
	ECataclysmCityTier CityTier = ECataclysmCityTier::Outpost;
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

	// CONTENT RELIEF AT TIER 2 AND UP, AND NOTHING IN THE GAME APPLIES IT YET.
	// The model lowers every enemy score by
	//
	//     ContentReliefTierWidths + ContentReliefPerTierWidths x (tier - 2)
	//
	// tier widths in its comparison with the player's power: 0.19 and 0.32 at
	// difficulty tiers 2 and 3, 0.45, 0.58, 0.71, 0.84 and 0.97 at tiers 4 to
	// 8, none at tier 1. The project owner, 2026-10-29, on issue #1425:
	// "Weaken tier content", then "Relief grows by tier"; and on 2026-10-30,
	// "Wins fall every tier", which moved its start from tier 4 down to tier 2
	// on the same line. These are the same three numbers as
	// `TuningConfig.content_relief_*` in `sim/cataclysm_sim/config.py`, and
	// `tools/tests/test_content_relief_port.py` holds them equal.
	//
	// THEY ARE NOT PART OF THE PORTED FORMULA and `ScoreFor` does not read
	// them: the score is also the experience a kill grants, and the owner's
	// ruling weakens creatures, not what they are worth. They are read on the
	// strength path only, by `ContentRelief` and `StrengthScoreFor` below,
	// which issue #1569 built: the conversion from enemy score to the health
	// and damage a creature on a dungeon floor is placed with.
	static constexpr double ContentReliefTierWidths = 0.19;
	static constexpr double ContentReliefPerTierWidths = 0.13;
	static constexpr int32 ContentReliefFromTier = 2;

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

	/**
	 * The same, read off a game mode handed over rather than found in a world.
	 *
	 * **SPLIT OUT SO THE READING CAN BE TESTED AT ALL.** `FloorIn` finds its
	 * game mode with `UWorld::GetAuthGameMode`, and a world built by
	 * `UWorld::CreateWorld` has no authority game mode: `UWorld::
	 * AuthorityGameMode` is private and only a game instance sets it, and an
	 * automation world has no game instance. So a game mode spawned into a test
	 * world is never found, and every field this copies -- the floor number, the
	 * length, the kind, the sub-type and the modifier score -- was read by
	 * nothing any test could reach. A `nullptr` here gives the defaults, which
	 * is what `FloorIn` did for a world with no game mode and still does.
	 *
	 * THE DIFFICULTY TIER IS PASSED IN because it does not come from the game
	 * mode's virtuals: `ACataclysmGameMode::DifficultyTierFor` reads a console
	 * variable first and the mode's own setting second.
	 */
	static FCataclysmScoredFloor FloorFor(const ACataclysmGameMode* Mode,
										  int32 DifficultyTier);

	// ---------------------------------------------------------------------
	// THE STRENGTH THE GAME BUILDS A CREATURE WITH. Issue #1569.
	// ---------------------------------------------------------------------
	//
	// The project owner, 2026-09-11: creature health and damage come from the
	// design's enemy score on each floor. Three rulings of 2026-10-30 ease that
	// score before it becomes health and damage, and all three live here and in
	// `UCataclysmEnemyStrength`, never in `ScoreFor` above: `ScoreFor` is also
	// the experience a kill grants, and the rulings weaken creatures, not what
	// they are worth. `enemy_stats.strength_on_floor` in the simulation is the
	// same arithmetic, and `Cataclysm.EnemyStrength.EveryFigureIsTheModels`
	// holds the two equal through `game/Tests/EnemyStrength/ExpectedFigures.csv`.

	/**
	 * Damage is multiplied by this at difficulty tier 1. The owner: "Scale damage
	 * to a target", then "Approve" (issue #1569).
	 *
	 * 0.05 SINCE ISSUE #2749, FROM 0.20. 0.20 was fitted to a level 20 character,
	 * and a character starts at level 1 since issue #2744. The owner ruled on
	 * 2026-11-18 to "re-set tier 1 enemy damage so a character levelling at that
	 * pace survives". 0.05 is the largest step of 0.01 at which the starting
	 * Ravager, levelling as it kills half of each floor's creatures, wins every
	 * floor of every first-surge Outpost dungeon the simulation's seeds 0 to 299
	 * make, against 10 Common Imps, with at least 10% of its health left on the
	 * last floor: a judgement, approved by the coordinating session on
	 * 2026-11-19. `tools/tests/test_the_starting_character_survives_a_pack.py`
	 * is the guard; `docs/DECISIONS.md`, 2026-11-19, has the measurement.
	 *
	 * AN OUTPOST'S SINCE ISSUE #2749'S RING SLICE: the three constants below are the other rings'.
	 */
	static constexpr double DamageScaleAtFirstTier = 0.05;

	/**
	 * DAMAGE AT DIFFICULTY TIER 1 RISES BY THE RING THE DUNGEON WAS ROLLED ON. The project owner, 2026-11-19, chose
	 * "Rise by city ring" over a rise with character level and over leaving it flat: Outpost dungeons stay at 0.05,
	 * Bulwark and Sanctuary dungeons hit harder. Each figure is the coordinating session's ruling of the same day, by the
	 * rule that chose the Outpost's 0.05: the largest step of 0.01 at which every dungeon of that ring the simulation's
	 * tier 1 campaigns clear (seeds 0 to 59 of every policy), walked again from the level the character held when it
	 * entered, is won with at least 10% of its health left on the last floor, against 10 Common Imps. Checked on seeds
	 * 60 to 119. A judgement; `sim/analyse_opening.py` section 5 is the measurement and
	 * `tools/tests/test_the_starting_character_survives_a_pack.py` the guard.
	 */
	static constexpr double DamageScaleAtFirstTierOnABulwark = 0.11;

	/** A Sanctuary's, by the same rule. See `DamageScaleAtFirstTierOnABulwark`. */
	static constexpr double DamageScaleAtFirstTierOnASanctuary = 0.13;

	/**
	 * The Pillar's, by the same rule: BELOW THE BULWARK'S, THE ONE EXCEPTION TO "RISES BY RING", ruled by the
	 * coordinating session on 2026-11-19. Only the earned Cataclysm boss dungeon has this ring -- no surge targets the
	 * Pillar, and `UCataclysmSurgeScheduler::MakeCataclysmDungeon` labels the boss dungeon Pillar though it stands on
	 * no city -- and it is reached soonest, at the lowest levels, by the characters that win soonest, 100 to 197 floors
	 * deep. At 0.11 two of the 219 measured clears ended under 10% health.
	 */
	static constexpr double DamageScaleAtFirstTierOnThePillar = 0.10;

	/**
	 * TIERS 2 TO 8 LIE ON A LINE RISING EVENLY FROM THIS AT TIER 1 TO ONE AT THE
	 * LAST TIER: 0.314 at tier 2 up to 1.00 at tier 8. It is the line tier 1 was
	 * on until issue #2749, which moved tier 1 alone and left tiers 2 to 8 where
	 * they were, as the coordinating session ruled on 2026-11-19: a new
	 * character always begins at tier 1, so the characters entering tiers 2 to
	 * 8 are the same levelled characters as before, and nothing measured them
	 * as wrong.
	 */
	static constexpr double DamageScaleLineAtFirstTier = 0.20;

	/** The minimum strength is what the same creature scores on floor 1 of a
	 *  dungeon this many floors deep, of the same kind, after the relief. */
	static constexpr int32 MinimumStrengthReferenceFloors = 12;

	/** The tier widths taken off every score on the strength path at this tier:
	 *  the three `ContentRelief` constants above, 0 below their first tier. */
	static double ContentRelief(int32 Tier);

	/**
	 * What a creature's damage is multiplied by at this tier on a dungeon rolled on this ring: at tier 1 the ring's own
	 * figure (`FirstTierDamageScaleOn`), then 0.314 at tier 2 rising evenly to 1.00 at tier 8 on every ring.
	 *
	 * TIERS 2 TO 8 ARE THE SAME ON EVERY RING, ruled by the coordinating session on 2026-11-19: the gap in level between
	 * a tier's Outpost and Sanctuary dungeons is largest at tier 1 and shrinks with the tier. The ring reaches this
	 * function at every tier, so giving tiers 2 to 8 ring figures later is a table here, not a new path.
	 */
	static double DamageScale(int32 Tier, ECataclysmCityTier Ring = ECataclysmCityTier::Outpost);

	/** Tier 1's damage scale on a dungeon rolled on this ring: 0.05 on an Outpost, 0.11 on a Bulwark, 0.13 on a
	 *  Sanctuary, 0.10 on the Pillar. */
	static double FirstTierDamageScaleOn(ECataclysmCityTier Ring);

	/**
	 * The score a creature on this floor is BUILT from: `ScoreFor` less the
	 * tier relief, never below the same creature on floor 1 of a
	 * `MinimumStrengthReferenceFloors`-floor dungeon of the same kind (which
	 * without it scores nothing on the first floors of a deep dungeon), and
	 * never below zero.
	 */
	static double StrengthScoreFor(const FCataclysmScoredFloor& Floor, int32 RarityStep);
};
