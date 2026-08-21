// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmPlayerClassStats.generated.h"

class UAbilitySystemComponent;
class UDataTable;

/**
 * Puts a class's stat line onto the player, which nothing did before this.
 *
 * WHAT WAS WRONG, AND IT WAS REPORTED FROM PLAY. The project owner could not
 * reach the end of a dungeon floor and said the character had too little health.
 * They had exactly 100, which is the placeholder
 * UCataclysmVitalAttributeSet's constructor writes, and whose own comment says
 * "Placeholders only. Real starting values come from a class stat line applied
 * as a gameplay effect". No such line was ever applied by anything.
 *
 * `game/Data/ClassStats.csv` has held those numbers since the data pipeline was
 * built. `UCataclysmClassStats::BaseFor` has been able to read them, at a level,
 * since it was written. Nothing outside the test suite ever called it. So the
 * Ravager's 130 health, the Masochist's 150 and 3 per second regeneration, and
 * the Default line's own 100 plus 15 a level all existed as data and never
 * reached a character.
 *
 * WHAT THE ARITHMETIC LOOKED LIKE AT 100 HEALTH. The creatures on a dungeon
 * floor deal, per hit: Imp 9, Hellhound 19, Corrupted Sentinel 22, Succubus 32,
 * Brute 35, Abyssal Warden 38, Gatekeeper 42. Against 100 health with no armour,
 * no resistance, no block and no flat reduction, a Brute killed the player in
 * three hits and a group of ten Imps in about one second. Health came back at 1
 * per second.
 *
 * THE ENEMY NUMBERS WERE NEVER TUNED AGAINST THAT CHARACTER, and the simulation
 * says so in its own words. `sim/cataclysm_sim/enemy_stats.py` explains the two
 * damage constants: "a geared character at tier 8 takes about a tenth of what is
 * thrown at them -- 53% off from armour, 70% resistance, 28% block chance, 16%
 * flat reduction -- so a figure chosen without reference to that is a figure
 * chosen against nothing." The player has none of those four layers, so they
 * were taking roughly ten times what the numbers were fitted for.
 *
 * THIS CLASS DOES NOT FIX THAT, and saying otherwise would be wrong. It supplies
 * the base stat line, which is one of the missing pieces. Levelling, gear that
 * reaches attributes, potions and leech are the others and none of them exists.
 * Issue #806 records the whole gap.
 */
UCLASS()
class CATACLYSM_API UCataclysmPlayerClassStats : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The class stat table, generated from the design workbook. */
	static const TCHAR* ClassStatsAssetPath;

	/** Null when the table asset is missing. Loaded once and kept. */
	static const UDataTable* LoadTable();

	/**
	 * Which gameplay attribute each character-sheet stat name drives.
	 *
	 * ALL TWENTY NAMES IN `game/Data/ClassStats.csv` HAVE ONE, and that is not a
	 * coincidence worth relying on quietly: every stat the design gives a class
	 * already had an attribute waiting for it, which is why this wiring is a
	 * gap rather than a redesign.
	 * Cataclysm.PlayerStats.EveryClassStatDrivesAnAttribute reads the table and
	 * fails if the design ever names a twenty-first, because a stat with no
	 * attribute is silently dropped rather than reported.
	 *
	 * NOTHING OUTSIDE THIS LIST IS TOUCHED. Attack speed, critical strike
	 * chance, evasion, block and penetration are all real attributes that no
	 * class line names, so they keep whatever their attribute set starts them
	 * at.
	 */
	static const TMap<FString, FGameplayAttribute>& StatToAttribute();

	/** Which class the console variable asks for. `Default` is the shared line. */
	static FString ChosenClass();

	/** Which level the console variable asks for, clamped to 1 to 100. */
	static int32 ChosenLevel();

	/**
	 * Writes every mapped stat onto the ability system, and fills the pools.
	 *
	 * MAXIMUMS FIRST AND THEN THE CURRENT POOLS, and the order is not
	 * cosmetic: UCataclysmVitalAttributeSet clamps current health to maximum
	 * health, so filling before raising the maximum clamps it straight back to
	 * the old number and the character keeps its 100.
	 *
	 * THE CLASS RESOURCE IS DELIBERATELY NOT FILLED. Only its maximum is set.
	 * The Masochist's Anguish builds from health lost and starts empty; a
	 * resource that began full would be the opposite of what the design says.
	 *
	 * @return how many attributes were written, so a caller and a test can tell
	 *         "applied nothing" from "applied everything"
	 */
	static int32 ApplyTo(UAbilitySystemComponent* AbilitySystem,
						 const UDataTable* ClassTable,
						 const FString& ClassName, int32 Level);

	/**
	 * The level a character starts at until levelling exists.
	 *
	 * WHY IT IS NOT 1. Nothing in this project grants experience or raises a
	 * level -- there is no levelling system at all, so a level-1 character can
	 * never become anything else, and at level 1 the Default line is the same
	 * 100 health that could not finish a floor. The stand-in has to be high
	 * enough to play at.
	 *
	 * WHY TWENTY. The Default line is 100 health plus 15 a level, so level 20 is
	 * 385. `sim/cataclysm_sim/enemy_stats.py` states the target the enemy damage
	 * constants were solved for: "an average Common enemy is a threat in a pack
	 * rather than alone", fitted so a Common enemy needs about 25 hits and the
	 * Gatekeeper about 2. Against 385 health a Brute needs 11 hits and an Imp
	 * 43. That is the first level at which the designed shape appears at all.
	 *
	 * IT IS A PLACEHOLDER AND NOT A DESIGN NUMBER. `Cataclysm.PlayerLevel`
	 * changes it without a rebuild, the same way `Cataclysm.DungeonEnemyScale`
	 * changes how many creatures a floor holds. Issues #38 and #50 are the real
	 * answer.
	 */
	static constexpr int32 DefaultLevel = 20;
};
