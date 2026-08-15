// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

const TCHAR* UCataclysmWeaponSkills::WeaponIndependent = TEXT("All");

const TCHAR* UCataclysmWeaponSkills::BasicAttackName = TEXT("Basic Attack");

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
	// Skills matrix has no basic attack row at all and still has none: the basic
	// attack comes from the weapon base instead, through BasicAttackFor, which
	// names the slot directly and never asks this function. Issue #524.
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

TSubclassOf<UCataclysmGameplayAbility> UCataclysmWeaponSkills::TemplateFor(
	ECataclysmSkillShape Shape)
{
	switch (Shape)
	{
	case ECataclysmSkillShape::Strike:     return UCataclysmStrikeSkill::StaticClass();
	case ECataclysmSkillShape::Projectile: return UCataclysmProjectileSkill::StaticClass();
	case ECataclysmSkillShape::SelfBuff:   return UCataclysmSelfBuffSkill::StaticClass();
	case ECataclysmSkillShape::Movement:   return UCataclysmMovementSkill::StaticClass();
	case ECataclysmSkillShape::Summon:     return UCataclysmSummonSkill::StaticClass();
	case ECataclysmSkillShape::Aura:       return UCataclysmAuraSkill::StaticClass();
	case ECataclysmSkillShape::Debuff:     return UCataclysmDebuffSkill::StaticClass();
	case ECataclysmSkillShape::None:
	default:
		return nullptr;
	}
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
				Skill.Shape = UCataclysmSkillShapes::ShapeFromName(Row.Shape);
				Skill.Tags = UCataclysmSkillShapes::TagsFromCell(Row.Tags);

				// Named in the error so a bad cell says which of the 398 rows it
				// is. The generator refuses one already, so this only fires for
				// a table edited in the editor rather than generated.
				FString Error;
				Skill.Params = UCataclysmSkillShapes::ParseParams(Row.ShapeParams, &Error);
				if (!Error.IsEmpty())
				{
					UE_LOG(LogCataclysm, Warning,
						TEXT("'%s' (%s %s) has unreadable shape parameters: %s"),
						*Row.SkillName, *Row.WeaponType, *Row.Slot, *Error);
				}
				if (!Row.Shape.IsEmpty() && Skill.Shape == ECataclysmSkillShape::None)
				{
					UE_LOG(LogCataclysm, Warning,
						TEXT("'%s' names the shape '%s', which no template "
							 "implements. It will fill its slot and do nothing."),
						*Row.SkillName, *Row.Shape);
				}

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

FCataclysmWeaponSkill UCataclysmWeaponSkills::BasicAttackFor(
	const UDataTable* BaseTable, const FString& WeaponType)
{
	FCataclysmWeaponSkill Basic;
	if (!BaseTable || WeaponType.IsEmpty())
	{
		return Basic;
	}

	BaseTable->ForeachRow<FCataclysmItemBaseRow>(
		TEXT("UCataclysmWeaponSkills::BasicAttackFor"),
		[&](const FName&, const FCataclysmItemBaseRow& Row)
		{
			if (Basic.Slot != ECataclysmAbilitySlot::None
				|| !Row.WeaponType.Equals(WeaponType, ESearchCase::IgnoreCase))
			{
				return;
			}

			// An empty shape is the Shield, which grants no attack damage and so
			// has no hit to compose. Left as None, so the caller grants nothing
			// rather than granting an ability that does nothing.
			if (Row.BasicShape.IsEmpty())
			{
				return;
			}

			Basic.Slot = ECataclysmAbilitySlot::BasicAttack;
			Basic.Name = BasicAttackName;
			Basic.Shape = UCataclysmSkillShapes::ShapeFromName(Row.BasicShape);

			// THE SLOT TAG IS ADDED HERE AND THE ELEMENT TAG IS NOT, because
			// only one of the two is known here. Tags decide which of the
			// character's gear modifiers reach a skill:
			// UCataclysmStatPipeline::ModifierApplies asks whether the skill in
			// hand carries every tag a modifier requires, so a skill with an
			// empty container is reached by nothing scoped at all.
			//
			// Every row of the Weapon Skills sheet gets Slot.<Slot> derived into
			// its Tags cell by tools/generate_datatables.py, which is issue
			// #156. The basic attack has no row there, so without this line it
			// would be the one granted skill outside that guarantee, and
			// game/Data/EnchantmentsPositive.csv already ships three
			// enchantments scoped to Slot.Basic that could never match anything.
			//
			// THE ELEMENT IS THE WEAPON'S ROLLED DAMAGE TYPE, which this row
			// cannot state: one Item Bases row serves every damage type a weapon
			// can roll. UCataclysmWeaponSlotsComponent adds it, because the
			// component is what knows which type is equipped.
			Basic.Tags.AddTag(
				CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::BasicAttack));

			FString Error;
			Basic.Params = UCataclysmSkillShapes::ParseParams(
				Row.BasicShapeParams, &Error);
			if (!Error.IsEmpty())
			{
				UE_LOG(LogCataclysm, Warning,
					TEXT("The %s's basic attack has unreadable shape "
						 "parameters: %s"),
					*Row.WeaponType, *Error);
			}
			if (Basic.Shape == ECataclysmSkillShape::None)
			{
				UE_LOG(LogCataclysm, Warning,
					TEXT("The %s's basic attack names the shape '%s', which no "
						 "template implements. It will fill the slot and do "
						 "nothing."),
					*Row.WeaponType, *Row.BasicShape);
			}
		});

	return Basic;
}
