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
 *     damage = strength x Common's DamagePerScore x the archetype's DamageShare x DamageScale(tier, ring)
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

	/**
	 * The Starting figures of a creature taken as a thrall by a commander of `CommanderLevel`: what a summoned Imp
	 * has at that level, times this creature's kind's multiple of an Imp. Before its rarity's scale, which the creature
	 * applies itself, as it does to `StartingFiguresFor`'s. Issue #2868.
	 *
	 * THE OWNER'S RULING OF 2026-11-24: "Shouldn't minions scale based off your stats? Like not 1 for 1, but if A has
	 * 100 health, and B has 500 health, and minion health scales at like 2x per your level, A would have 200 health and
	 * B would have 1000 health." Read by the coordinating session: every minion, summoned or taken, has the base of its
	 * own type and rarity times one factor that grows with the summoner's level, and a taken creature's floor-tier
	 * scaling is replaced by it, so it is not scaled twice.
	 *
	 * THE FACTOR IS THE ONE A SUMMONED IMP ALREADY HAS, A JUDGEMENT: `Base + PerLevel x Level` of the Imp row of
	 * `game/Data/MinionTypes.csv`, which is the Imp's own base times `1 + (PerLevel / Base) x Level`. So a taken
	 * creature keeps its ratio to a summoned Imp at every level: a taken Common Imp is a summoned Imp, and anything else
	 * is the multiple `game/Data/EnemyArchetypes.csv` makes it of an Imp, for health and for damage apart.
	 *
	 *     health = Imp health at the level  x  the kind's HealthShare / the Imp's HealthShare
	 *     damage = Imp damage at the level  x  the kind's DamageShare / the Imp's DamageShare
	 *     armour = that health  x  (Common's ArmorPerScore x the kind's ArmorShare)
	 *                           /  (Common's HealthPerScore x the kind's HealthShare)
	 *
	 * ARMOUR KEEPS THE PROPORTION TO HEALTH THE DESIGN GIVES THE KIND, a judgement: the Imp states no armour, so there
	 * is no Imp figure to take a multiple of, and armour has to stay in step with what the creature's health became.
	 *
	 * @param CommanderLevel  `ACataclysmMinion::LevelOfSummonerFor` of the commander
	 * @return false, leaving `Out` untouched, when a table is missing, when either has no row for this archetype, for the
	 *         Imp archetype or for Common, when the minion type table has no Imp row, or when the Imp archetype states
	 *         no health or damage share; the caller then keeps the creature's own figures
	 */
	static bool ThrallFiguresFor(const UDataTable* EnemyRarityTable,
								 const UDataTable* EnemyArchetypeTable,
								 const UDataTable* MinionTypeTable,
								 FName ArchetypeRow,
								 int32 CommanderLevel,
								 FCataclysmStartingFigures& Out);

	/** The row of `game/Data/EnemyArchetypes.csv` and of `game/Data/MinionTypes.csv` a thrall is measured against. */
	static const TCHAR* ThrallMeasuredAgainst;
};
