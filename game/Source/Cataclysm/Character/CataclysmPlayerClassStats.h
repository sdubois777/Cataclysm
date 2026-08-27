// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmClassStats.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmPlayerClassStats.generated.h"

class UAbilitySystemComponent;
class UDataTable;

/**
 * Whether health, mana and the energy shield are filled after stats are written.
 *
 * THE TWO CASES ARE GENUINELY DIFFERENT AND GETTING IT WRONG IS A CHEAT.
 * A character arriving in the world should stand up with full pools. A
 * character whose gear changed should not: a player who swapped a helmet
 * during a fight would be healed to full by doing it, and swapping back and
 * forth would be an unlimited heal.
 */
UENUM()
enum class ECataclysmPoolFill : uint8
{
	/** A character arriving in the world. Health, mana and shield go to maximum. */
	FillToMaximum,

	/** A character already in play. The pools are left exactly where they are. */
	LeaveAsTheyAre,
};

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

	/** What one point of each attribute is worth. Same workbook. */
	static const TCHAR* AttributesAssetPath;

	/** Null when the table asset is missing. Loaded once and kept. */
	static const UDataTable* LoadTable();

	/**
	 * The same, for the attribute table.
	 *
	 * SEPARATE FROM LoadTable BECAUSE THE TWO ARE DIFFERENT QUESTIONS. The class
	 * table says where a stat starts; this one says what a spent attribute point
	 * does to it. `ApplyTo` needs both and a character can have the first
	 * without the second, which is what the game did until issues #50 and #897.
	 */
	static const UDataTable* LoadAttributeTable();

	/**
	 * Put a character's spent attribute points into a base-override map.
	 *
	 * WHY THE POINTS ARE A BASE AND NOT A MODIFIER. `docs/Cataclysm_GDD_v2.md`
	 * says "Gear does not grant attribute points. It increases the attribute the
	 * character already has", so the count a character spent is where the
	 * attribute starts and the eight affixes are increases on top of it. No
	 * class line can state how many points a particular character has spent,
	 * which is exactly what a base override is for.
	 *
	 * BOTH CALLERS OF ApplyTo USE THIS, so the two cannot disagree about it.
	 * Issues #50 and #897.
	 */
	static void MergeAttributeBases(const FCataclysmAttributePoints& Points,
									TMap<FName, float>& Bases);

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

	/**
	 * Bases the engine states in C++, for stats no class line should name.
	 *
	 * WHAT IT IS FOR. An `increased` row multiplies a base, and a base of zero
	 * leaves it worth nothing however many points are spent. Most stats get
	 * their base from `game/Data/ClassStats.csv`, but that sheet mirrors
	 * `sim/cataclysm_sim/classes.py`, which is a statement about what makes each
	 * class feel different. A timing window belonging to one passive node is not
	 * that, and the passive effects sheet cannot supply it either, because that
	 * sheet carries values PER POINT and a base is not one. This is the third
	 * place, and it is C++ because the value is a constant of a mechanic.
	 *
	 * IT EXISTS BECAUSE THE FIRST STAT IN IT SILENTLY RESOLVED TO ZERO.
	 * Issue #1025. `damage_to_bleeding_window` was named by
	 * `ENGINE_SUPPLIED_BASES` in `tools/generate_datatables.py`, which exempted
	 * it from the check that refuses an increase with no base under it -- and
	 * nothing anywhere actually supplied the base, so The Breaking Point opened
	 * a conversion window of zero seconds and converted nothing. The exemption
	 * silenced the check and no code kept its side of the bargain.
	 *
	 * IT REPLACES THE CLASS LINE RATHER THAN ADDING TO IT, and nothing here may
	 * also be named by a class line.
	 * `Cataclysm.PlayerStats.EveryClassStatDrivesAnAttribute` fails if one ever
	 * is, so that rule is enforced rather than assumed.
	 *
	 * A `BaseOverrides` ENTRY STILL WINS OVER THIS, because that is a base built
	 * from a particular character -- the weapons it holds, the attribute points
	 * it spent -- and this is a constant true of every character.
	 */
	static const TMap<FName, float>& EngineSuppliedBases();

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
	 * MODIFIERS COME FROM ANYWHERE, AND UNTIL 2026-08-22 THEY CAME FROM
	 * NOWHERE. This wrote the class line alone, so every character at a given
	 * level was identical whatever they found or chose. Gear is the first
	 * source (issue #828, `UCataclysmEquipmentComponent::GatherModifiers`);
	 * the passive tree (#38, #50) and buffs are the next two and plug in here
	 * rather than needing their own path.
	 *
	 * They are keyed by the character-sheet stat name, which is the same key
	 * `StatToAttribute` above uses, so a modifier naming a stat no class line
	 * mentions is simply not written -- the loop is over the attribute map, not
	 * over the modifiers. That is deliberate: an affix granting a stat with no
	 * attribute behind it has nowhere to go, and inventing an attribute for it
	 * here would hide the gap rather than report it.
	 *
	 * A BASE OVERRIDE IS FOR A STAT NO CLASS LINE CAN STATE. Issue #845 added
	 * one user and it is expected to stay the only one: attack speed. A
	 * character's swing rate comes from the weapons it holds, and two weapons
	 * AVERAGE their rates rather than summing them, so neither the class table
	 * nor the modifier pipeline can produce it. `UCataclysmEquipmentComponent::
	 * StatBasesFromWeapons` is what builds the map.
	 *
	 * ATTACK DAMAGE DELIBERATELY DOES NOT USE IT, and supplying one would double
	 * the character's damage. A weapon's damage is an `attack_damage` implicit
	 * on its base, so it already arrives as a flat modifier and the base is
	 * correctly zero.
	 *
	 * @return how many attributes were written, so a caller and a test can tell
	 *         "applied nothing" from "applied everything"
	 */
	static int32 ApplyTo(UAbilitySystemComponent* AbilitySystem,
						 const UDataTable* ClassTable,
						 const FString& ClassName, int32 Level,
						 const TMap<FName, TArray<FCataclysmStatModifier>>* Modifiers = nullptr,
						 ECataclysmPoolFill PoolFill = ECataclysmPoolFill::FillToMaximum,
						 const TMap<FName, float>* BaseOverrides = nullptr);

	/**
	 * The level a character starts at until levelling exists.
	 *
	 * WHY IT IS NOT 1. Nothing in this project grants experience or raises a
	 * level -- there is no levelling system at all, so a level-1 character can
	 * never become anything else. At level 1 every class in the table loses the
	 * fight the sandbox puts in front of them, measured for issue #806, so the
	 * stand-in has to be high enough to play at.
	 *
	 * WHY TWENTY. `sim/cataclysm_sim/enemy_stats.py` states the target the enemy
	 * damage constants were solved for: "an average Common enemy is a threat in
	 * a pack rather than alone", fitted so a Common enemy needs about 25 hits
	 * and the Gatekeeper about 2. Twenty is the first level at which the
	 * designed shape appears at all.
	 *
	 * THE FIGURES HERE WERE WRITTEN FOR THE SHARED `Default` LINE, which a
	 * character no longer starts on -- StartingClassName below is the Ravager
	 * since 2026-08-24. They are kept because the level was chosen against them:
	 * the shared line is 100 health plus 15 a level, so level 20 was 385, and a
	 * Brute needed 11 hits and an Imp 43 against that. A Ravager at the same
	 * level has 510 and some armour, so it is strictly further inside the shape
	 * the level was picked for.
	 *
	 * IT IS A PLACEHOLDER AND NOT A DESIGN NUMBER. `Cataclysm.PlayerLevel`
	 * changes it without a rebuild, the same way `Cataclysm.DungeonEnemyScale`
	 * changes how many creatures a floor holds. Issues #38 and #50 are the real
	 * answer.
	 */
	static constexpr int32 DefaultLevel = 20;

	/**
	 * Which class stat line a character sits on having chosen nothing.
	 *
	 * NOT `UCataclysmClassStats::DefaultClassName`, AND THE DIFFERENCE IS THE
	 * POINT. That one is the shared line every class inherits a stat from when
	 * it does not state its own, and it carries no defensive layer at all. This
	 * one is the class a player actually plays as until there is a selection
	 * screen. They were the same string until 2026-08-24, which meant every
	 * character ever played had no armour, no resistance, no block, no flat
	 * damage reduction and no leech. Issue #806.
	 *
	 * Read by the `Cataclysm.PlayerClass` console variable as its default, and
	 * by ChosenClass when that variable is set to nothing at all.
	 */
	static const FString StartingClassName;
};
