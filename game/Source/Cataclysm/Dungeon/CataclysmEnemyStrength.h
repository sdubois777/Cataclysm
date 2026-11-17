// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "UObject/Object.h"
#include "CataclysmEnemyStrength.generated.h"

class UDataTable;

/**
 * What a creature is built with at Common, before its rarity's scale.
 *
 * THE RARITY IS NOT IN HERE, and it must not be: the creature multiplies these by
 * `UCataclysmEnemyRarity::ScalingFromCommon` itself, in
 * `ACataclysmEnemyCharacter::ApplyStartingAttributes`, so a figure carrying the
 * rarity already would be scaled twice. What the rarity DOES reach here is the
 * score: a rarer creature on the same floor scores higher, as the design's
 * `RarityWeight` says. The model counts rarity in both places, and so does this.
 */
USTRUCT()
struct CATACLYSM_API FCataclysmStartingFigures
{
	GENERATED_BODY()

	UPROPERTY()
	float Health = 0.0f;

	UPROPERTY()
	float AttackDamage = 0.0f;

	UPROPERTY()
	float Armour = 0.0f;
};

/**
 * A creature's health, damage and armour, from the design's enemy score on the
 * floor it is placed on. Issue #1569.
 *
 * THE OWNER CHOSE IT ON 2026-09-11: "Creature health and damage come from the
 * design's enemy score on each floor". Until this existed every creature carried
 * fixed figures from `ACataclysmGameMode` -- a Common Imp had 87 health on floor 1
 * and on floor 50, at every tier.
 *
 * THE ARITHMETIC IS `stats_for` IN `sim/cataclysm_sim/enemy_stats.py`, at the
 * score `UCataclysmEnemyScore::StrengthScoreFor` gives:
 *
 *     health = strength x Common's HealthPerScore x the archetype's HealthShare
 *     damage = strength x Common's DamagePerScore x the archetype's DamageShare x DamageScale(tier)
 *     armour = strength x Common's ArmorPerScore  x the archetype's ArmorShare
 *
 * with Common's figures from `DT_EnemyRarities` and the shares from
 * `DT_EnemyArchetypes`, the same two tables the simulation writes. Energy shield
 * is not here: the creature already makes it a share of its health.
 *
 * ONLY A DUNGEON FLOOR BUILDS CREATURES THIS WAY. The sandbox keeps its fixed
 * figures, ruled 2026-10-30, because it has no floors. Creatures a dungeon rule
 * spawns directly, the Corrupted Stalker and a player's summons are not built
 * here either; `docs/DECISIONS.md` lists them.
 */
UCLASS()
class CATACLYSM_API UCataclysmEnemyStrength : public UObject
{
	GENERATED_BODY()

public:
	/** `DT_EnemyArchetypes`, the creature profile table. Logs and answers null when the asset is missing. */
	static const UDataTable* LoadEnemyArchetypeTable();

	/**
	 * The Starting figures for a creature of this archetype row, at this rarity
	 * rung, on this floor.
	 *
	 * @return false, leaving `Out` untouched, when either table is missing or has
	 *         no row for Common or for the archetype, so the caller can keep the
	 *         creature's own figures rather than build it at nothing
	 */
	static bool StartingFiguresFor(const UDataTable* EnemyRarityTable,
								   const UDataTable* EnemyArchetypeTable,
								   FName ArchetypeRow,
								   const FCataclysmScoredFloor& Floor,
								   int32 RarityStep,
								   FCataclysmStartingFigures& Out);
};
