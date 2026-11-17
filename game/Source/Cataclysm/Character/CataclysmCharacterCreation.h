// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmCharacterCreation.generated.h"

class UDataTable;

/**
 * The two choices a player makes when a character is created.
 *
 * `docs/Cataclysm_GDD_v2.md` section IV: "When starting a new character, players
 * choose a starting weapon type and damage type, which determines their initial
 * skill set and first available passive class tree."
 *
 * BOTH EMPTY MEANS NOBODY HAS CHOSEN, which is what every character had before
 * the creator existed and what a character stood up by an automation test still
 * has. `NAME_None` is not a weapon type and not a damage type, so it cannot be
 * mistaken for either -- the same reason
 * `ACataclysmPlayerState::LevelNotYetDecided` is zero.
 *
 * THE APPEARANCE HALF OF THE CREATOR IS NOT HERE. The design also offers preset
 * body types, skin tones, hairstyles and height. The project owner asked on
 * 2026-08-24 to leave those out of this change, because the project has no
 * player character art for them to change. Issue #931.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmCreationChoice
{
	GENERATED_BODY()

	/** A WeaponType in `game/Data/ItemBases.csv`, such as `Greataxe`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Creation")
	FName WeaponType;

	/** One of the eight, such as `Demonic`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Creation")
	FName DamageType;

	/** Whether both halves have been chosen. Neither is optional. */
	bool IsComplete() const
	{
		return !WeaponType.IsNone() && !DamageType.IsNone();
	}

	bool operator==(const FCataclysmCreationChoice& Other) const
	{
		return WeaponType == Other.WeaponType && DamageType == Other.DamageType;
	}
};

/**
 * Everything the character creation screen has to work out, outside the widget.
 *
 * WHY IT IS A SEPARATE CLASS FROM THE WIDGET, which is the reason
 * `UCataclysmInventoryScreen` is separate from `UCataclysmInventoryWidget`: the
 * automation test command in `tools/unreal_build.py` passes `-nullrhi`, so
 * nothing that reaches the screen can be watched by a test. Everything here is a
 * static function over plain values, so all of it is covered while the drawing
 * itself stays uncovered.
 *
 * WHAT IT REPLACES. Two hard-coded strings that both said they were waiting for
 * this. `UCataclysmWeaponSlotsComponent::StartingWeaponType` was `Greataxe` and
 * its comment named issue #50 and the character creator by name;
 * `UCataclysmWeaponSlotsComponent::DamageType` was `Demonic` for the same
 * reason. They remain as the values a character has when nobody has chosen,
 * exactly the way `Cataclysm.PlayerLevel` became the starting level rather than
 * being deleted.
 *
 * WHERE THE ANSWERS COME FROM. Both tables are generated from the design
 * workbook and neither is copied here:
 *
 *   `game/Data/ItemBases.csv`     which weapon types exist, and how many hands
 *   `game/Data/WeaponSkills.csv`  which damage types each weapon can carry, and
 *                                 which of those pairings has a skill designed
 *
 * The one list that is stated here rather than read is which three classes a
 * damage type unlocks, because no table holds it. It is checked against the
 * design document by
 * `tools/tests/test_character_creation_matches_the_design.py`, which is the same
 * arrangement `tools/tests/test_damage_type_availability_matches_the_design.py`
 * gives the weapon table.
 */
UCLASS()
class CATACLYSM_API UCataclysmCharacterCreation : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The weapon type a character has when nobody has chosen one.
	 *
	 * GREATAXE, WHICH IS WHAT `UCataclysmWeaponSlotsComponent` ALREADY HELD.
	 * Keeping it means an automation test that stands a character up without
	 * touching the creator gets exactly the character it got before.
	 */
	static const FName DefaultWeaponType;

	/** The damage type a character has when nobody has chosen one. Demonic,
	 *  for the same reason: it is what the vertical slice designs. */
	static const FName DefaultDamageType;

	/**
	 * The one weapon type the creator does not offer.
	 *
	 * THE SHIELD, AND IT IS EXCLUDED BECAUSE IT CANNOT HIT ANYTHING.
	 * `docs/Cataclysm_GDD_v2.md`: "The Shield is a one-handed weapon that grants
	 * no attack damage... A held weapon that grants no attack damage contributes
	 * nothing to the basic attack -- neither damage nor swing rate." A skill's
	 * damage is a percentage of weapon damage, so a character whose only weapon
	 * is a Shield deals nothing with its basic attack and nothing with any of
	 * its six skills either.
	 *
	 * IT IS STILL A LEGAL THING TO WEAR, and this does not change that. A
	 * character may pick a Shield up and hold it in the second hand for its
	 * block chance, its armour, its affix slots and its sockets, which is what
	 * the design intends it for. What it may not be is the weapon a character
	 * starts its life holding and nothing else.
	 */
	static const FName WeaponTypeWithNoAttack;

	/**
	 * The WeaponType value the weapon skill matrix uses for skills that do not
	 * care what is held.
	 *
	 * THE AURAS. Eight rows, one per damage type. Counting them as a weapon
	 * would give every weapon every damage type, which is the same trap
	 * `tools/tests/test_damage_type_availability_matches_the_design.py`
	 * documents under `WEAPON_INDEPENDENT`.
	 */
	static const FName WeaponIndependent;

	/**
	 * Every weapon type a character may start holding, in table order.
	 *
	 * IN THE ORDER `game/Data/ItemBases.csv` LISTS THEM, which is the eight
	 * one-handed weapons and then the six two-handed ones, matching the order
	 * the design document's Weapon Types section prints. A screen showing them
	 * in a different order every run would be worse than one showing them in an
	 * arbitrary but fixed one.
	 *
	 * @return empty when the table is missing, which a screen shows as a screen
	 *         with no options rather than as a crash
	 */
	static TArray<FName> StartingWeaponTypes(const UDataTable* BaseTable);

	/**
	 * The damage types this weapon type may carry, in the design's order.
	 *
	 * READ OFF THE WEAPON SKILL MATRIX rather than off the document's table,
	 * because since issue #857 the matrix is what decides what a weapon can
	 * carry -- `UCataclysmDropRoll::RollDamageTypes` draws a dropped weapon's
	 * types from it. Two answers to one question is how they drift.
	 *
	 * @return empty for a weapon type the matrix does not name
	 */
	static TArray<FName> DamageTypesFor(const UDataTable* WeaponSkillTable,
										FName WeaponType);

	/**
	 * The weapon types this damage type may appear on, in table order.
	 *
	 * THE SAME QUESTION FROM THE OTHER SIDE, so a player who knows they want to
	 * play Void can be shown the eight weapons that carry it instead of picking
	 * a weapon and discovering Void is not on it.
	 */
	static TArray<FName> WeaponTypesFor(const UDataTable* WeaponSkillTable,
										const UDataTable* BaseTable,
										FName DamageType);

	/** Whether this pair is one the matrix allows and the creator offers. */
	static bool IsLegalChoice(const UDataTable* WeaponSkillTable,
							  const UDataTable* BaseTable,
							  const FCataclysmCreationChoice& Choice);

	/**
	 * How many of this pairing's six skills have actually been written.
	 *
	 * WHY A SCREEN SHOWS THIS. A pairing the design allows is not the same as a
	 * pairing that has been designed: `game/Data/WeaponSkills.csv` carries a row
	 * for all 390 legal combinations and only 58 of them have a skill name on
	 * them, because writing the other 332 is issues #62 and #836. A player who
	 * picks Sword and Celestial today gets a character with no skills at all,
	 * and the creator should say so before they choose rather than after.
	 *
	 * @return 0 to 6, counting only rows whose SkillName is not empty
	 */
	static int32 DesignedSkillCount(const UDataTable* WeaponSkillTable,
									FName WeaponType, FName DamageType);

	/**
	 * The three classes a damage type unlocks, in the design document's order.
	 *
	 * WHAT THE CHOICE BUYS, AND WHY THE CREATOR SHOWS IT. The design gives the
	 * damage type its whole meaning here: "Each damage type unlocks three class
	 * passive trees. Players can spec into one class per damage type available
	 * on their weapon." So a player choosing Demonic is choosing between
	 * Ravager, Ritualist and Masochist later, and choosing War is choosing
	 * between Bulwark, Berserker and Saboteur. Showing the six words is the
	 * difference between a choice and a coin toss.
	 *
	 * STATED HERE RATHER THAN READ FROM A TABLE, because no table holds it.
	 * `game/Data/ClassStats.csv` names only the four classes that have a stat
	 * line and says nothing about damage types. The design document's "Classes
	 * by Damage Type" section is the only statement of it, and
	 * `tools/tests/test_character_creation_matches_the_design.py` compares this
	 * list against that section so the two cannot drift.
	 *
	 * @return an empty array for a name that is not one of the eight damage
	 *         types
	 */
	static const TArray<FName>& ClassesFor(FName DamageType);

	/** Every damage type and its three classes. The whole map, for a test and
	 *  for a screen that wants to show all of it at once. */
	static const TMap<FName, TArray<FName>>& ClassesByDamageType();

	/**
	 * Which `game/Data/ItemBases.csv` row is this weapon type.
	 *
	 * WHAT IT IS FOR. `ACataclysmPlayerCharacter::StartingWeaponBase` names a
	 * row rather than a weapon type, because it wears a real item. The creator
	 * chooses a weapon TYPE, so something has to turn one into the other, and
	 * doing it by reading the table means a base renamed in the design workbook
	 * moves this with it.
	 *
	 * @return NAME_None when no row carries that weapon type
	 */
	static FName ItemBaseFor(const UDataTable* BaseTable, FName WeaponType);

	/**
	 * The line the screen prints once both halves are chosen.
	 *
	 * @return empty when the choice is incomplete, which says nothing rather
	 *         than saying "you have chosen nothing"
	 */
	static FString SummaryFor(const UDataTable* WeaponSkillTable,
							  const FCataclysmCreationChoice& Choice);

	/**
	 * The line naming the three classes the chosen damage type unlocks.
	 *
	 * @return empty when no damage type is chosen
	 */
	static FString UnlockedClassesFor(const FCataclysmCreationChoice& Choice);

	/**
	 * Why this choice cannot be confirmed, or empty when it can be.
	 *
	 * A REASON RATHER THAN A BOOLEAN, so the screen and the console command can
	 * both say which of the three refusals it was: nothing chosen yet, a weapon
	 * type the creator does not offer, or a pairing the design does not allow.
	 */
	static FString RefusalFor(const UDataTable* WeaponSkillTable,
							  const UDataTable* BaseTable,
							  const FCataclysmCreationChoice& Choice);
};
