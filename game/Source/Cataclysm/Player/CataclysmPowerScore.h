// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CataclysmPowerScore.generated.h"

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
 * THE WEIGHTS ARE DERIVED, NOT COPIED. Five weights turn a character into a
 * score, and every one of them falls out of two anchors and four shares:
 *
 *   THE ANCHORS are the score a player is expected to have reached at the end of
 *   each of the eight difficulty tiers, 385 at tier 1 through 6,327 at tier 8.
 *   They come from the enemy scoring model and are authoritative.
 *
 *   THE SHARES say what a finished character draws from each of the four
 *   sources: 10% from level, 50% from gear, 30% from gems, 10% from
 *   resistances. These are the only free choice in the whole model.
 *
 * Deriving rather than pasting the five numbers matters: changing a share here
 * and not in the Python model produces a visible test failure, where two copied
 * lists of five constants would quietly disagree. It also keeps the REASON the
 * weights are what they are in the code.
 *
 * WHAT THE ANCHORS DESCRIBE. The ceiling a tier can produce, not a typical
 * build. A character sitting exactly on the tier 8 anchor has eighteen
 * Cataclysmic pieces, which means 72 enchantments and no regular affixes at all.
 * A build that keeps ordinary stats scores less, and that is the design working.
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

	// The four shares. The only free choice in this model.
	static constexpr float ShareLevel = 0.10f;
	static constexpr float ShareGear = 0.50f;
	static constexpr float ShareGems = 0.30f;
	static constexpr float ShareResistances = 0.10f;

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
	 * How much one upgrade level adds, as a fraction of the piece's unupgraded
	 * contribution. At about 0.252 a +10 piece is worth 3.52 times a +0 one.
	 *
	 * UPGRADE LEVEL MULTIPLIES RARITY RATHER THAN ADDING TO IT, which is the
	 * only reason the score curve bends. It is also the same factor gear upgrade
	 * level uses to multiply every affix on a piece, rather than a second copy
	 * of it.
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
};
