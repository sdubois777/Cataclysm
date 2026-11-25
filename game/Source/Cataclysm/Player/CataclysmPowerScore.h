// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CataclysmPowerScore.generated.h"

class ACataclysmPlayerCharacter;
class ACataclysmEnemyCharacter;
struct FCataclysmScoredFloor;
struct FCataclysmItem;
enum class ECataclysmGearSlot : uint8;

/**
 * One equipped item, as far as Power Score cares.
 *
 * Only two things about a piece matter here: how rare it is and how far it has
 * been upgraded. What it rolled does not enter the score at all.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmScoredGear
{
	GENERATED_BODY()

	/** 1 for Everyday through 8 for Cataclysmic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Power")
	int32 Rarity = 1;

	/** The +0 to +10 upgrade level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Power")
	int32 Upgrade = 0;
};

/**
 * Everything Power Score reads about a character.
 *
 * NOTHING HERE IS A VITAL. Health, mana and energy shield are deliberately
 * absent: the design's list of Power Score inputs contains none of them, so a
 * character's stat line is not needed to compute this number. That is why this
 * can be scored without the stat pipeline.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmScoredCharacter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Power")
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Power")
	TArray<FCataclysmScoredGear> Gear;

	/** The rarity of each socketed gem. How many entries there are is how many
	 *  sockets are filled, so socket count enters as the term count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Power")
	TArray<int32> Gems;

	/** The percentage value of each of the eight resistances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Power")
	TArray<float> Resistances;
};

/** What each of the four sources contributed, so a score can be explained. */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmPowerBreakdown
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Power") float FromLevel = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Power") float FromGear = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Power") float FromGems = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Power") float FromResistances = 0.0f;

	/** The four added together and rounded, which is the Power Score. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Power") int32 Total = 0;
};

/**
 * The player's Power Score, ported from `sim/cataclysm_sim/player_power.py`.
 *
 * WHY IT EXISTS. Enemies and dungeons already have a score. This puts the player
 * on the same scale, and the comparison is what the whole difficulty system runs
 * on: a dungeon near the player's score gives average rewards, below it gives
 * less, above it gives more. Overwhelm reads the same comparison, stripping
 * mitigation from a player fighting above their score.
 *
 * THE WEIGHTS ARE FIVE PLAIN NUMBERS, ruled by the project owner on 2026-11-24
 * (issue #2853, "Adopt, and look at drop supply (Recommended)"): 10 points a
 * level, 2 points a rarity step of a gear piece with each upgrade level adding
 * the piece's +0 worth again (a piece is 2 x rarity x (1 + gear level)), 5 points
 * a gem rarity step, and 1 point a resistance percent up to the cap. The owner
 * asked for numbers a player can add up: "why would I think a level is worth 6.3
 * points?"
 *
 * UNTIL THAT RULING THEY WERE DERIVED, from two anchors and four shares (10%
 * level, 50% gear, 30% gems, 10% resistances of a finished character), and were
 * 6.327, 6.233, 0.2525, 5.2725 and 1.1298. NOW THE SHARES FALL OUT of the
 * weights: 15% level, 49% gear, 28% gems and 9% resistances at tier 8.
 *
 *   THE ANCHORS are the score a player is expected to have reached at the end of
 *   each of the eight difficulty tiers, 385 at tier 1 through 6,327 at tier 8.
 *   They come from the enemy scoring model, are authoritative, and did not move.
 *   The reference character lands near them rather than on them: 364 at tier 1
 *   (-5.5%) to 6,528 at tier 8 (+3.2%).
 *
 * The five are written out here and in `sim/cataclysm_sim/player_power.py`, and
 * `tools/tests/test_power_score_port.py` reads these literals back and compares
 * them with the model's, so the two cannot part silently.
 *
 * WHAT THE ANCHORS DESCRIBE. The ceiling a tier can produce, not a typical
 * build. The tier 8 reference character has eighteen Cataclysmic pieces, which
 * means 72 enchantments and no regular affixes at all. A build that keeps
 * ordinary stats scores less, and that is the design working.
 */
UCLASS()
class CATACLYSM_API UCataclysmPowerScore : public UObject
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxLevel = 100;

	/** Eight item and gem rarities, Everyday through Cataclysmic. */
	static constexpr int32 MaxRarity = 8;

	/** A piece upgrades from +0 to +10. */
	static constexpr int32 MaxUpgrade = 10;

	/** Seven armour pieces, eight rings, the necklace, the relic and the weapon.
	 *  Two one-handed weapons count as one, so dual wielding is not free power. */
	static constexpr int32 GearPieces = 18;

	/** Total sockets across all equipment, fixed by the design document. */
	static constexpr int32 TotalSockets = 45;

	static constexpr int32 ResistanceCount = 8;

	/**
	 * Resistance counted by Power Score stops at the design's cap.
	 *
	 * Affixes may push actual resistance above it, and that headroom is real
	 * because enemy penetration eats into it. But it is headroom against
	 * penetration rather than power, so it does not raise the score.
	 */
	static constexpr float ResistanceCap = 70.0f;

	// THE FIVE WEIGHTS, the owner's plain numbers of 2026-11-24 (issue #2853). `LevelWeight()` and the four functions
	// below return these; `tools/tests/test_power_score_port.py` holds them to `player_power.WEIGHTS`.

	/** Points for each character level: a level 6 character's level part is 60. */
	static constexpr float PointsPerLevel = 10.0f;

	/** Points for each rarity step of a gear piece at +0: an Everyday +0 piece is 2, a Masterful +0 piece 8. */
	static constexpr float PointsPerGearRarityStep = 2.0f;

	/** What each upgrade level adds, as a multiple of the piece's +0 worth: a piece at +k is (1 + k) times itself. */
	static constexpr float UpgradeFactorPerGearLevel = 1.0f;

	/** Points for each rarity step of a socketed gem. */
	static constexpr float PointsPerGemRarityStep = 5.0f;

	/** Points for each resistance percent, each of the eight counted up to `ResistanceCap`. */
	static constexpr float PointsPerResistancePercent = 1.0f;

	/**
	 * The score a player is expected to have reached at the end of each
	 * difficulty tier, indexed by tier so entry 0 is unused.
	 *
	 * Authoritative, from `scoring.PLAYER_MAX_SCORES`. A Python test reads these
	 * literals back out of this header and compares them, so the two cannot
	 * drift apart silently.
	 */
	static const TArray<int32>& TierAnchors();

	/** How much one point of level is worth. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Power")
	static float LevelWeight();

	/** How much one point of gear rarity is worth, before any upgrade. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Power")
	static float GearWeight();

	/**
	 * How much one upgrade level adds, as a multiple of the piece's unupgraded
	 * contribution. At 1 a +10 piece is worth 11 times a +0 one (3.52 times until
	 * 2026-11-24).
	 *
	 * UPGRADE LEVEL MULTIPLIES RARITY RATHER THAN ADDING TO IT, which is the
	 * only reason the score curve bends. It is NOT the factor gear upgrade level
	 * multiplies an item's affixes by: that is `UCataclysmItemValues::
	 * GearLevelFactor`, 0.15, since 2026-09-03 (issue #1179).
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Power")
	static float UpgradeFactor();

	/** How much one point of gem rarity is worth. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Power")
	static float GemWeight();

	/** How much one percentage point of resistance is worth, up to the cap. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Power")
	static float ResistanceWeight();

	/** The four terms and the total, so a score can be shown broken down. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Power")
	static FCataclysmPowerBreakdown Breakdown(const FCataclysmScoredCharacter& Character);

	/** The player's Power Score, on the same scale as an enemy's. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Power")
	static int32 Score(const FCataclysmScoredCharacter& Character);

	/**
	 * The ceiling a player can reach at the end of a difficulty tier.
	 *
	 * Gear and gem rarity equal the tier, because there are eight of each and
	 * the best upgrade stone that can drop is capped by the current tier. Gear
	 * level is tier plus two, capped at +10.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Power")
	static FCataclysmScoredCharacter ReferenceCharacter(int32 Tier);

	// ---------------------------------------------------------------------
	// THE PLAYER'S OWN SCORE. Issue #2824, slice a, ruled by the coordinating session on 2026-11-22.
	// ---------------------------------------------------------------------

	/** How many of the hands' filled sockets count: six, what every loadout gives (design section V). */
	static constexpr int32 HandSockets = 6;

	/**
	 * A character as the score reads it, from its level, what it wears and its eight resistances.
	 *
	 * `Worn` is indexed by `ECataclysmGearSlot`, as `UCataclysmEquipmentComponent::GetSlots` holds it; a slot with no
	 * base is empty. Each worn piece outside the hands is one entry: its rarity (`RarityOfItem`, from what fills its
	 * slots, 1 for Everyday to 8 for Cataclysmic) and its gear level. THE HANDS ARE ONE PIECE (design section IV): of
	 * the two weapon slots, the higher-scoring piece counts, the coordinating session's judgement (Q4), so an offhand
	 * never lowers a score. Each filled socket of a piece outside the hands is one gem; of the hands' filled sockets,
	 * the six of the highest rarity count, because the hands count as one piece and every loadout gives six, so the
	 * capstone's twelve are partly uncounted by design.
	 *
	 * EACH RESISTANCE IS HELD AT 0 TO 70: above 70 adds nothing (the design's rule), and below 0, which a tier's penalty
	 * can leave, takes nothing away (the coordinating session's judgement: the score's resistance term runs from no
	 * resistance to the cap).
	 */
	static FCataclysmScoredCharacter ScoredCharacterFrom(int32 Level, const TArray<FCataclysmItem>& Worn,
														 const TArray<float>& Resistances);

	/**
	 * What one piece adds to a Power Score when it is worn in `Slot`: `Breakdown`'s gear term and gem term of a character
	 * wearing that piece alone, at level 0 with no resistance, so the two terms a worn piece moves and nothing else. The
	 * owner's report of 2026-11-24, ruled by the coordinating session: when every ring slot is full, a ring replaces the
	 * worn ring of its own rarity with the lowest Power Score (`UCataclysmGearSlots::SlotToReplace`).
	 *
	 * THROUGH `ScoredCharacterFrom` AND `Breakdown`, NOT A THIRD COPY OF THEIR ARITHMETIC: its rarity and gear level, and
	 * its filled sockets' gems, exactly as the character's own score reads them, the hands' rule included (a weapon in a
	 * weapon slot counts six of its gems). Nought for a piece with no rarity, which the score does not count, and for a
	 * slot outside the enum.
	 */
	static float WornPieceScoreOf(const FCataclysmItem& Item, ECataclysmGearSlot Slot);

	/**
	 * The character playing, scored at `DifficultyTier`: its player state's level, its equipment component's slots,
	 * and its eight resistances as a hit meets them before the cap, after that tier's penalty
	 * (`UCataclysmDamageCalculation::ResistanceBeforeTheCapFor`), the ruling's Q5. A level of 0, and nothing worn or
	 * resisted, for a character with no player state, equipment or abilities.
	 */
	static FCataclysmScoredCharacter ScoredCharacterOf(const ACataclysmPlayerCharacter* Character, int32 DifficultyTier);

	/** `Breakdown` of `ScoredCharacterOf` at the difficulty tier of the character's world. */
	static FCataclysmPowerBreakdown LiveBreakdownOf(const ACataclysmPlayerCharacter* Character);

	/**
	 * `LiveBreakdownOf`'s total, or `INDEX_NONE` for no character.
	 *
	 * CACHED ON THE CHARACTER SINCE SLICE B (issue #2824), because Overwhelm reads it on every hit the player takes.
	 * Worked out again when any of four things differs from the last time: the equipment component's `ChangeCount`
	 * (gear worn or removed), its `RefreshCount` (every stat refresh: gear, a level gained, passive points, a dungeon
	 * modifier's stat change), the player state's level, and the world's difficulty tier. A resistance written straight
	 * onto the attribute with no refresh, as a test does, is NOT seen until one of the four moves.
	 */
	static int32 LiveScoreOf(const ACataclysmPlayerCharacter* Character);

	// ---------------------------------------------------------------------
	// OVERWHELM. Issue #2824, slice b, ruled by the coordinating session on 2026-11-22 (Q7, Q8).
	// ---------------------------------------------------------------------

	/** Mitigation lost per tier width of shortfall: `OVERWHELM_RATE` in `sim/cataclysm_sim/combat.py`. */
	static constexpr float OverwhelmRate = 0.25f;

	/** The most Overwhelm can take: `OVERWHELM_CAP` in `sim/cataclysm_sim/combat.py`. */
	static constexpr float OverwhelmCap = 0.50f;

	/**
	 * The design's Overwhelm, as a fraction: min(50%, 25% x (Enemy Score - Player Score) / Tier Width), nothing below 0.
	 * A port of `overwhelm` in `sim/cataclysm_sim/combat.py`; 0 for a tier width of 0 or less, as there.
	 */
	static float OverwhelmFor(int32 PlayerScore, int32 EnemyScore, double TierWidth);

	/**
	 * The Overwhelm, in percentage points, a creature of `RarityStep` (0 Common to 5 Cataclysm Boss) on `Floor` applies
	 * to a player scoring `PlayerScore`: `OverwhelmFor` of that floor's `UCataclysmEnemyScore::ScoreFor` at that rung,
	 * with the floor's tier's `TierWidth`, times 100. 0 for a `PlayerScore` of `INDEX_NONE`.
	 */
	static float OverwhelmPointsAt(int32 PlayerScore, const FCataclysmScoredFloor& Floor, int32 RarityStep);

	/**
	 * What a hit from `Attacker` takes off `Defender`'s mitigation, in percentage points: `OverwhelmPointsAt` of the
	 * defender's live score, the attacker's world's floor (`UCataclysmEnemyScore::FloorIn`) and the attacker's own rung
	 * (`RarityStep`, what it stands at, not what its death pays as). 0 when either is missing: a hazard, a player's blow,
	 * a blow on a creature.
	 */
	static float OverwhelmPointsAgainst(const ACataclysmPlayerCharacter* Defender,
										const ACataclysmEnemyCharacter* Attacker);

	/**
	 * The floor panel's line: what Overwhelm a Common and a Cataclysm Boss apply on `Floor` to a player scoring
	 * `PlayerScore`, so a player can see why they take more damage. Empty for `INDEX_NONE`. "Overwhelm here: none, your
	 * Power Score is at or above every creature's." when both are 0.
	 */
	static FString OverwhelmText(int32 PlayerScore, const FCataclysmScoredFloor& Floor);
};
