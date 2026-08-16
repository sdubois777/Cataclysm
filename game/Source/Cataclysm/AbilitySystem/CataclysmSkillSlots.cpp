// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "GameplayTagsManager.h"

const TCHAR* UCataclysmSkillSlots::TableAssetPath =
	TEXT("/Game/Data/DT_SkillSlots.DT_SkillSlots");

namespace
{
	/**
	 * The default class's maximum mana, from the Class Stats sheet.
	 *
	 * Duplicated here as constants rather than read from the class stat table,
	 * because a mana cost has to resolve before any class is known -- the cost
	 * is the same number for every class, which is the whole point of it being
	 * flat. The test Cataclysm.Skills.ManaCostRidesTheDefaultManaProgression
	 * checks these against ClassStats.csv so they cannot drift.
	 */
	constexpr float DefaultMaxManaBase = 50.0f;
	constexpr float DefaultMaxManaPerLevel = 6.0f;
}

const UDataTable* UCataclysmSkillSlots::LoadGeneratedTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, TableAssetPath);
	if (!Table)
	{
		// Naming both scripts, because the two failures look the same from
		// here: the workbook never produced the CSV, or the CSV was never
		// imported as an asset.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load %s. It is produced by "
				 "tools/generate_datatable_assets.py from game/Data/"
				 "SkillSlots.csv, which tools/generate_datatables.py produces "
				 "from the Skill Slots sheet of "
				 "docs/All_Things_Cataclysm.xlsx."), TableAssetPath);
		return nullptr;
	}
	return Table;
}

FCataclysmSkillSlotNumbers UCataclysmSkillSlots::NumbersFor(
	const UDataTable* Table, ECataclysmAbilitySlot Slot)
{
	FCataclysmSkillSlotNumbers Numbers;
	Numbers.Slot = Slot;

	if (!Table || Slot == ECataclysmAbilitySlot::None)
	{
		return Numbers;
	}

	Table->ForeachRow<FCataclysmSkillSlotRow>(
		TEXT("UCataclysmSkillSlots::NumbersFor"),
		[&](const FName&, const FCataclysmSkillSlotRow& Row)
		{
			if (Numbers.bFound)
			{
				return;
			}
			if (UCataclysmWeaponSkills::SlotFromName(Row.Slot) != Slot)
			{
				return;
			}
			Numbers.DamagePercent = Row.DamagePercent;
			Numbers.Cooldown = Row.Cooldown;
			Numbers.ManaCostAtLevel100 = Row.ManaCost;
			Numbers.ManaOnHitAtLevel100 = Row.ManaOnHit;
			Numbers.bFound = true;
		});

	return Numbers;
}

float UCataclysmSkillSlots::DefaultMaxManaAtLevel(int32 Level)
{
	const int32 Clamped = FMath::Max(1, Level);
	return DefaultMaxManaBase + DefaultMaxManaPerLevel * static_cast<float>(Clamped - 1);
}

float UCataclysmSkillSlots::ManaCostAtLevel(float CostAtLevel100, int32 Level)
{
	if (CostAtLevel100 <= 0.0f)
	{
		return 0.0f;
	}

	const float Reference = DefaultMaxManaAtLevel(ManaCostReferenceLevel);
	if (Reference <= 0.0f)
	{
		// Cannot happen with the shipped stat line, but returning the level 100
		// figure is the safe direction: it overcharges rather than making every
		// skill free, and a free skill is the failure nobody notices.
		return CostAtLevel100;
	}

	return CostAtLevel100 * (DefaultMaxManaAtLevel(Level) / Reference);
}

float UCataclysmSkillSlots::ManaOnHitAtLevel(float OnHitAtLevel100, int32 Level)
{
	if (OnHitAtLevel100 <= 0.0f)
	{
		// Every slot but the basic attack. Six of the seven rows state no mana
		// on hit at all, so this is the ordinary answer rather than an error.
		return 0.0f;
	}

	const float Reference = DefaultMaxManaAtLevel(ManaCostReferenceLevel);
	if (Reference <= 0.0f)
	{
		// Cannot happen with the shipped stat line. Returning the level 100
		// figure is the safe direction here in the OPPOSITE sense to the cost
		// above: it over-pays rather than paying nothing, and paying nothing is
		// the failure that would go unnoticed, because it looks exactly like the
		// mana economy this was built to repair.
		return OnHitAtLevel100;
	}

	return OnHitAtLevel100 * (DefaultMaxManaAtLevel(Level) / Reference);
}

FGameplayTag UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot Slot)
{
	// The Basic Attack is automatic and the Aura is a toggle. Neither waits, so
	// neither has a tag saying it is waiting, and there is deliberately no
	// Cooldown.Basic or Cooldown.Aura in the workbook's Tags sheet. A tag
	// nothing can ever apply is the kind of thing this project keeps finding.
	if (Slot == ECataclysmAbilitySlot::None
		|| Slot == ECataclysmAbilitySlot::BasicAttack
		|| Slot == ECataclysmAbilitySlot::Aura)
	{
		return FGameplayTag();
	}

	const UEnum* SlotEnum = StaticEnum<ECataclysmAbilitySlot>();
	if (!SlotEnum)
	{
		return FGameplayTag();
	}

	const FString Name = SlotEnum->GetNameStringByValue(static_cast<int64>(Slot));
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(*FString::Printf(TEXT("Cooldown.%s"), *Name)),
		/*ErrorIfNotFound=*/false);
}
