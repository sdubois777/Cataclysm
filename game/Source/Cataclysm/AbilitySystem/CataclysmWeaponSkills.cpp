// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
// For the melee tag a basic attack that is a strike carries. Issue #999.
#include "AbilitySystem/CataclysmDamageCalculation.h"
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

void UCataclysmWeaponSkills::StampOnto(UCataclysmSkillTemplate& Template,
									   const FCataclysmWeaponSkill& Skill)
{
	Template.SkillName = Skill.Name;
	Template.SkillDescription = Skill.Description;
	Template.Params = Skill.Params;
	Template.SkillTags = Skill.Tags;

	// AND ITS OWN CRITICAL STRIKE CHANCE, stamped here rather than
	// written onto the character, because six skills are granted at once
	// and the character has one CritChance attribute to hold them in.
	// Each hit carries the chance of the skill that dealt it instead.
	// Issue #657.
	Template.CritChancePercent = Skill.CritChancePercent;

	// AND WHAT IT IS WORTH, WAITS AND COSTS. A slot is a key and a
	// skill is worth what it is worth wherever it is put, decided
	// 2026-08-22. Each is -1 on every row today, and -1 is what the
	// ability already treats as "take the slot's figure", so this
	// changes nothing until a number is written. Issue #836.
	Template.DamagePercentOverride = Skill.DamagePercent;
	Template.CooldownOverride = Skill.Cooldown;
	Template.ManaCostOverride = Skill.ManaCost;

	// AND THE SLOT IT WAS MADE FOR, which every rule reads wherever the skill sits (O1, issue #837 layer 3). The key
	// is the caller's, as above.
	Template.DesignedSlot = DesignedSlotOf(Skill);
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
	case ECataclysmSkillShape::Deployable: return UCataclysmDeployableSkill::StaticClass();
	case ECataclysmSkillShape::Aura:       return UCataclysmAuraSkill::StaticClass();
	case ECataclysmSkillShape::Debuff:     return UCataclysmDebuffSkill::StaticClass();
	case ECataclysmSkillShape::None:
	default:
		return nullptr;
	}
}

namespace
{
	/**
	 * One row of the matrix as a skill, in the slot the caller read from it. ONE FUNCTION because two lookups read
	 * rows: a weapon's skills and every skill of a damage type. Written twice, the second would miss the next column.
	 */
	FCataclysmWeaponSkill WeaponSkillFromRow(const FName& RowName, const FCataclysmWeaponSkillRow& Row,
											 ECataclysmAbilitySlot Slot)
	{
		FCataclysmWeaponSkill Skill;
		Skill.Slot = Slot;
		// THE KEY AND THE DESIGNED SLOT ARE THE SAME HERE, the row's, and the fill moves the key alone (issue #837
		// layer 3). The row's name, weapon and type are what a choice is saved by and a screen shows.
		Skill.DesignedSlot = Slot;
		Skill.RowName = RowName;
		Skill.WeaponType = Row.WeaponType;
		Skill.DamageType = Row.DamageType;
		Skill.Name = Row.SkillName;
		Skill.Description = Row.SkillDescription;
		Skill.Shape = UCataclysmSkillShapes::ShapeFromName(Row.Shape);
		Skill.Tags = UCataclysmSkillShapes::TagsFromCell(Row.Tags);

		// CARRIED UNCHANGED, INCLUDING THE -1 THAT MEANS THE ROW SAYS
		// NOTHING. Turning that into 5% here would put the default in a
		// second place, and the two could then disagree silently. It is
		// applied once, where a character's attribute is written, in
		// UCataclysmWeaponSlotsComponent. Issue #657.
		Skill.CritChancePercent = Row.CritChancePercent;

		// THE SAME, AND FOR THE SAME REASON. A -1 means the row says
		// nothing and the slot's figure is used; turning it into that
		// figure here would put the fallback in a second place. Applied
		// once, where the ability is granted, in
		// UCataclysmWeaponSlotsComponent. Issue #836.
		Skill.DamagePercent = Row.DamagePercent;
		Skill.Cooldown = Row.Cooldown;
		Skill.ManaCost = Row.ManaCost;

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

		return Skill;
	}
}

TArray<FCataclysmWeaponSkill> UCataclysmWeaponSkills::SkillsOfDamageType(const UDataTable* Table,
																		 const FString& DamageType)
{
	TArray<FCataclysmWeaponSkill> Found;
	if (!Table)
	{
		return Found;
	}
	Table->ForeachRow<FCataclysmWeaponSkillRow>(
		TEXT("UCataclysmWeaponSkills::SkillsOfDamageType"),
		[&](const FName& RowName, const FCataclysmWeaponSkillRow& Row)
		{
			const ECataclysmAbilitySlot Slot = SlotFromName(Row.Slot);
			if (Row.SkillName.IsEmpty() || Slot == ECataclysmAbilitySlot::None
				|| !Row.DamageType.Equals(DamageType, ESearchCase::IgnoreCase))
			{
				return;
			}
			Found.Add(WeaponSkillFromRow(RowName, Row, Slot));
		});
	return Found;
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
			[&](const FName& RowName, const FCataclysmWeaponSkillRow& Row)
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

				Found.Add(WeaponSkillFromRow(RowName, Row, Slot));
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
			// MADE FOR THE BASIC KEY AND NEVER MOVED FROM IT: it cannot be chosen (O2, issue #837 layer 3).
			Basic.DesignedSlot = ECataclysmAbilitySlot::BasicAttack;
			Basic.WeaponType = Row.WeaponType;
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

			// AND A STRIKE IS A MELEE ATTACK. The project owner ruled on
			// 2026-08-26, under issue #999, that a strike is what melee means for
			// a weapon skill, and every row of the Weapon Skills sheet carrying
			// `Type.Strike` was given `Type.Melee` then. The basic attack has no
			// row there, so it was the one strike left out, and it is the attack a
			// player makes most. game/Data/ItemBases.csv gives every melee weapon
			// `BasicShape=Strike` and every ranged one `Projectile`, so the shape
			// decides it here. Issue #944 proposed exactly this.
			//
			// WITHOUT IT NO MELEE-SCOPED BONUS REACHED THE BASIC ATTACK. A hit is
			// melee only when its skill carries the tag -- `ApplyHit` reads it off
			// the skill's own tags -- so the Masochist's Carnage and Blood Offering,
			// both scoped to `Type.Melee`, never reached it, and Mutilation
			// Mastery's Bleeding on a melee critical strike never came from it.
			//
			// `Type.Strike` IS NOT ADDED. Nothing has asked for it, and every tag
			// added changes which gear reaches a skill, so it waits for a reason.
			if (Basic.Shape == ECataclysmSkillShape::Strike)
			{
				const FGameplayTag Melee = UCataclysmDamageCalculation::MeleeTag();
				if (Melee.IsValid())
				{
					Basic.Tags.AddTag(Melee);
				}
			}

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

TArray<FCataclysmWeaponSkill> UCataclysmWeaponSkills::PoolFor(const UDataTable* Table, const TArray<FString>& WeaponTypes,
															  const TArray<FString>& DamageTypes)
{
	TArray<FCataclysmWeaponSkill> Pool;
	if (!Table)
	{
		return Pool;
	}

	// A MATCHED PAIR COUNTS ONCE, and so does a damage type two weapons both carry: each list without its repeats, in
	// its own order, the case ignored as every match in this file ignores it.
	const auto Distinct = [](const TArray<FString>& Names)
	{
		TArray<FString> Kept;
		for (const FString& Name : Names)
		{
			if (!Name.IsEmpty()
				&& !Kept.ContainsByPredicate([&Name](const FString& Held) { return Held.Equals(Name, ESearchCase::IgnoreCase); }))
			{
				Kept.Add(Name);
			}
		}
		return Kept;
	};
	const TArray<FString> Weapons = Distinct(WeaponTypes);
	const TArray<FString> Types = Distinct(DamageTypes);

	for (const FString& Type : Types)
	{
		const int32 FirstOfThisType = Pool.Num();

		// EVERY WORN WEAPON TYPE WITH THIS DAMAGE TYPE, Q4: the rows `SkillsFor` reads for the pair, one per slot.
		for (int32 WeaponAt = 0; WeaponAt < Weapons.Num(); ++WeaponAt)
		{
			const FString& Weapon = Weapons[WeaponAt];
			TSet<ECataclysmAbilitySlot> TakenByThisPair;
			Table->ForeachRow<FCataclysmWeaponSkillRow>(
				TEXT("UCataclysmWeaponSkills::PoolFor"),
				[&](const FName& RowName, const FCataclysmWeaponSkillRow& Row)
				{
					const ECataclysmAbilitySlot Slot = SlotFromName(Row.Slot);
					if (Row.SkillName.IsEmpty() || Slot == ECataclysmAbilitySlot::None || TakenByThisPair.Contains(Slot)
						|| !Row.DamageType.Equals(Type, ESearchCase::IgnoreCase)
						|| Row.WeaponType.Equals(WeaponIndependent, ESearchCase::IgnoreCase)
						|| !Row.WeaponType.Equals(Weapon, ESearchCase::IgnoreCase))
					{
						return;
					}
					TakenByThisPair.Add(Slot);
					Pool.Add(WeaponSkillFromRow(RowName, Row, Slot));
				});
		}

		// AND THE TYPE'S AURA ONCE, only when a weapon row of the type joined, as `SkillsFor` adds it. A slot this type's
		// weapon rows already hold is left to them, which is `SkillsFor`'s rule for one pair.
		if (Pool.Num() == FirstOfThisType)
		{
			continue;
		}
		TSet<ECataclysmAbilitySlot> TakenByThisType;
		for (int32 At = FirstOfThisType; At < Pool.Num(); ++At)
		{
			TakenByThisType.Add(Pool[At].Slot);
		}
		Table->ForeachRow<FCataclysmWeaponSkillRow>(
			TEXT("UCataclysmWeaponSkills::PoolFor"),
			[&](const FName& RowName, const FCataclysmWeaponSkillRow& Row)
			{
				const ECataclysmAbilitySlot Slot = SlotFromName(Row.Slot);
				if (Row.SkillName.IsEmpty() || Slot == ECataclysmAbilitySlot::None || TakenByThisType.Contains(Slot)
					|| !Row.DamageType.Equals(Type, ESearchCase::IgnoreCase)
					|| !Row.WeaponType.Equals(WeaponIndependent, ESearchCase::IgnoreCase))
				{
					return;
				}
				TakenByThisType.Add(Slot);
				Pool.Add(WeaponSkillFromRow(RowName, Row, Slot));
			});
	}
	return Pool;
}

TArray<FCataclysmWeaponSkill> UCataclysmWeaponSkills::DefaultFillFrom(const TArray<FCataclysmWeaponSkill>& Pool)
{
	// THE FIRST FOR EACH SLOT, in the pool's order, so one weapon of one type keeps `SkillsFor`'s order exactly.
	TArray<FCataclysmWeaponSkill> Filled;
	TSet<ECataclysmAbilitySlot> Taken;
	for (const FCataclysmWeaponSkill& Skill : Pool)
	{
		if (Skill.Slot != ECataclysmAbilitySlot::None && !Taken.Contains(Skill.Slot))
		{
			Taken.Add(Skill.Slot);
			Filled.Add(Skill);
		}
	}
	return Filled;
}

TArray<FCataclysmWeaponSkill> UCataclysmWeaponSkills::FillWithChoices(const TArray<FCataclysmWeaponSkill>& Pool,
																	  const TArray<FCataclysmSkillChoice>& Choices)
{
	TMap<ECataclysmAbilitySlot, FCataclysmWeaponSkill> OnKey;
	TSet<FName> Held;

	// EACH CHOSEN ROW THAT IS IN THE POOL, ON ITS KEY, KEEPING THE SLOT IT WAS MADE FOR (O1). The first choice of a key
	// or of a row wins. A row not in the pool fills nothing here and stays chosen in the player state (Q5).
	for (const FCataclysmSkillChoice& Choice : Choices)
	{
		if (!IsChoosableSlot(Choice.Slot) || Choice.SkillRow.IsNone() || OnKey.Contains(Choice.Slot)
			|| Held.Contains(Choice.SkillRow))
		{
			continue;
		}
		const FCataclysmWeaponSkill* InPool = Pool.FindByPredicate(
			[&Choice](const FCataclysmWeaponSkill& Skill) { return Skill.RowName == Choice.SkillRow; });
		if (!InPool)
		{
			continue;
		}
		FCataclysmWeaponSkill OnThisKey = *InPool;
		OnThisKey.DesignedSlot = DesignedSlotOf(*InPool);
		OnThisKey.Slot = Choice.Slot;
		OnKey.Add(Choice.Slot, OnThisKey);
		Held.Add(Choice.SkillRow);
	}

	// EVERY OTHER KEY: THE FIRST POOL ENTRY DESIGNED FOR IT NOT ALREADY HELD (O4), or nothing.
	for (const FCataclysmWeaponSkill& Skill : Pool)
	{
		const ECataclysmAbilitySlot Designed = DesignedSlotOf(Skill);
		if (Designed == ECataclysmAbilitySlot::None || OnKey.Contains(Designed)
			|| (!Skill.RowName.IsNone() && Held.Contains(Skill.RowName)))
		{
			continue;
		}
		OnKey.Add(Designed, Skill);
		if (!Skill.RowName.IsNone())
		{
			Held.Add(Skill.RowName);
		}
	}

	// IN THE DEFAULT FILL'S KEY ORDER, so no choice is `DefaultFillFrom` element for element; then a key only a choice
	// filled.
	TArray<FCataclysmWeaponSkill> Filled;
	TSet<ECataclysmAbilitySlot> Written;
	for (const FCataclysmWeaponSkill& Default : DefaultFillFrom(Pool))
	{
		if (const FCataclysmWeaponSkill* Found = OnKey.Find(Default.Slot))
		{
			Filled.Add(*Found);
			Written.Add(Default.Slot);
		}
	}
	for (const ECataclysmAbilitySlot Key : CataclysmAbilitySlots::All())
	{
		if (const FCataclysmWeaponSkill* Found = OnKey.Find(Key); Found && !Written.Contains(Key))
		{
			Filled.Add(*Found);
			Written.Add(Key);
		}
	}
	return Filled;
}
