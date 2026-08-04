// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "CataclysmSkillSlots.generated.h"

class UDataTable;

/**
 * What a skill in one slot is worth, how long it waits, and what it costs.
 *
 * A reading of one row of game/Data/SkillSlots.csv, which is generated from the
 * Skill Slots sheet of docs/All_Things_Cataclysm.xlsx.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmSkillSlotNumbers
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Slot")
	ECataclysmAbilitySlot Slot = ECataclysmAbilitySlot::None;

	/** Percent of weapon damage one use deals. For the Aura, per second. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Slot")
	float DamagePercent = 0.0f;

	/** Seconds before the skill can be used again, before any reduction. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Slot")
	float Cooldown = 0.0f;

	/** Mana one use costs AT LEVEL 100. Scale it with ManaCostAtLevel. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Slot")
	float ManaCostAtLevel100 = 0.0f;

	/** Mana restored per hit at level 100. Only the Basic Attack has any. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Slot")
	float ManaOnHitAtLevel100 = 0.0f;

	/** False when no row was found, which is the only way this is invalid. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Slot")
	bool bFound = false;
};

/**
 * The seven slots' numbers, read from the generated skill slot table.
 *
 * WHY THIS IS NOT PART OF THE WEAPON SKILL MATRIX. Cooldown and mana cost belong
 * to the SLOT, not to the skill. No designed skill states either one, so a column
 * on the Weapon Skills sheet would be 77 copies of seven values. This mirrors how
 * the damage multiplier already worked: the slot supplies it, and a skill states
 * its own only when it differs, which is what Skull Splitter does at 500%.
 *
 * WHAT WENT WRONG WITHOUT IT. Issue #155. The design already had a cooldown
 * reduction formula, the Efficacy attribute scaling it, an affix granting it and
 * 41 enchantments mentioning it -- all dividing a base cooldown that no skill
 * supplied. Nothing reported it, because zero divided by anything is still zero.
 */
UCLASS()
class CATACLYSM_API UCataclysmSkillSlots : public UObject
{
	GENERATED_BODY()

public:
	/** Where the imported skill slot table lives. */
	static const TCHAR* TableAssetPath;

	/**
	 * The level every mana cost in the data is quoted at.
	 *
	 * Every other figure in this project is quoted at level 100 as well: the
	 * class stat lines, the reference character and the damage targets.
	 */
	static constexpr int32 ManaCostReferenceLevel = 100;

	/** The skill slot table, or null with the reason logged. */
	static const UDataTable* LoadGeneratedTable();

	/** The numbers for one slot. bFound is false when the table has no such row. */
	static FCataclysmSkillSlotNumbers NumbersFor(const UDataTable* Table,
												 ECataclysmAbilitySlot Slot);

	/**
	 * A mana cost quoted at level 100, scaled to a character of this level.
	 *
	 * COSTS RIDE THE DEFAULT MANA PROGRESSION, so a skill takes the same share
	 * of a pool at level 1 as at level 100. A cost that never moved would be
	 * crippling early and beneath notice late: a Ravager's pool runs from 40 to
	 * 436. The number a player reads is still a flat quantity of mana, and it is
	 * the same number for every class, which is what makes a larger pool buy
	 * more casts rather than a proportionally larger price per cast.
	 *
	 * @param CostAtLevel100  the figure from the table
	 * @param Level           the character's level, clamped to 1 and above
	 */
	static float ManaCostAtLevel(float CostAtLevel100, int32 Level);

	/**
	 * The tag marking that this slot is waiting to be used again.
	 *
	 * Invalid for the Basic Attack and the Aura, which is correct rather than
	 * missing: the Basic Attack is automatic so attack speed sets its rate, and
	 * the Aura is a toggle so there is nothing to wait for. Neither has a
	 * cooldown, so neither needs a tag saying it is on one.
	 */
	static FGameplayTag CooldownTag(ECataclysmAbilitySlot Slot);

private:
	/** The default class's maximum mana at a level, from the Class Stats line. */
	static float DefaultMaxManaAtLevel(int32 Level);
};
