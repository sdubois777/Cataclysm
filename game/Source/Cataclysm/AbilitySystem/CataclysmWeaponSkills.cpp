// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

const TCHAR* UCataclysmWeaponSkills::WeaponIndependent = TEXT("All");

const TCHAR* UCataclysmWeaponSkills::TableAssetPath =
	TEXT("/Game/Data/DT_WeaponSkills.DT_WeaponSkills");

const UDataTable* UCataclysmWeaponSkills::LoadGeneratedTable()
{
	const UDataTable* Table =
		LoadObject<UDataTable>(nullptr, TableAssetPath);

	if (!Table)
	{
		// Loudly, and naming both scripts, because the two failures look the
		// same from here: the workbook never produced the CSV, or the CSV was
		// never imported as an asset.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load %s. It is produced by "
				 "tools/generate_datatable_assets.py from game/Data/"
				 "WeaponSkills.csv, which tools/generate_datatables.py produces "
				 "from the Weapon Skills sheet of "
				 "docs/All_Things_Cataclysm.xlsx."), TableAssetPath);
		return nullptr;
	}

	return Table;
}

ECataclysmAbilitySlot UCataclysmWeaponSkills::SlotFromName(const FString& SlotName)
{
	// Matched against the SLOT TAG's leaf rather than a list written here, so a
	// slot added to ECataclysmAbilitySlot needs no change in this function.
	//
	// THE TAG LEAF, NOT THE ENUM NAME, AND THAT DISTINCTION IS LOAD-BEARING. Six
	// of the seven spell the same both ways, but the basic attack is
	// ECataclysmAbilitySlot::BasicAttack in C++ and "Basic" everywhere else: in
	// the Slot.Basic tag, in the Skill Slots sheet, and in the Python model. The
	// workbook is authoritative, and the tag is generated from it, so the tag is
	// what the data actually says.
	//
	// This went unnoticed until the Skill Slots sheet arrived, because the Weapon
	// Skills matrix has no basic attack row at all -- basic attacks are
	// automatic, so nothing ever asked this function about one.
	for (const ECataclysmAbilitySlot Slot : CataclysmAbilitySlots::All())
	{
		const FGameplayTag Tag = CataclysmAbilitySlots::Tag(Slot);
		if (!Tag.IsValid())
		{
			continue;
		}

		FString Leaf = Tag.ToString();
		int32 Dot = INDEX_NONE;
		if (Leaf.FindLastChar(TEXT('.'), Dot))
		{
			Leaf = Leaf.RightChop(Dot + 1);
		}

		if (SlotName.Equals(Leaf, ESearchCase::IgnoreCase))
		{
			return Slot;
		}
	}
	return ECataclysmAbilitySlot::None;
}

TArray<FCataclysmWeaponSkill> UCataclysmWeaponSkills::SkillsFor(
	const UDataTable* Table, const FString& WeaponType, const FString& DamageType)
{
	TArray<FCataclysmWeaponSkill> Found;
	if (!Table)
	{
		return Found;
	}

	// One skill per slot. A second row claiming a slot already taken is a matrix
	// error rather than a choice, so the first wins and the rest are ignored
	// rather than silently replacing it.
	TSet<ECataclysmAbilitySlot> Taken;

	const auto Collect = [&](bool bWantWeaponIndependent)
	{
		Table->ForeachRow<FCataclysmWeaponSkillRow>(
			TEXT("UCataclysmWeaponSkills::SkillsFor"),
			[&](const FName&, const FCataclysmWeaponSkillRow& Row)
			{
				if (Row.SkillName.IsEmpty())
				{
					// Undesigned. The row exists so the matrix's shape is
					// explicit and an undesigned combination is visible.
					return;
				}
				if (!Row.DamageType.Equals(DamageType, ESearchCase::IgnoreCase))
				{
					return;
				}

				const bool bIsWeaponIndependent =
					Row.WeaponType.Equals(WeaponIndependent, ESearchCase::IgnoreCase);
				if (bIsWeaponIndependent != bWantWeaponIndependent)
				{
					return;
				}
				if (!bIsWeaponIndependent
					&& !Row.WeaponType.Equals(WeaponType, ESearchCase::IgnoreCase))
				{
					return;
				}

				const ECataclysmAbilitySlot Slot = SlotFromName(Row.Slot);
				if (Slot == ECataclysmAbilitySlot::None || Taken.Contains(Slot))
				{
					return;
				}

				Taken.Add(Slot);

				FCataclysmWeaponSkill Skill;
				Skill.Slot = Slot;
				Skill.Name = Row.SkillName;
				Skill.Description = Row.SkillDescription;
				Found.Add(MoveTemp(Skill));
			});
	};

	// The weapon's own skills first.
	Collect(/*bWantWeaponIndependent=*/false);

	// THE WEAPON-INDEPENDENT ROWS SUPPLEMENT A WEAPON, THEY DO NOT STAND ALONE.
	// Without this guard a weapon its damage type does not cover still collected
	// the aura, so a War Wand -- a combination the design says does not exist --
	// came back holding one skill, and so did a weapon type that was simply
	// misspelled. Covered or not covered has to be all or nothing, or a typo
	// produces a character with one ability and no error.
	if (!Found.IsEmpty())
	{
		Collect(/*bWantWeaponIndependent=*/true);
	}

	return Found;
}
