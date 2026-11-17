// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmCharacterCreation.h"
#include "Data/CataclysmDataRows.h"
#include "Items/CataclysmItem.h"
#include "Engine/DataTable.h"

const FName UCataclysmCharacterCreation::DefaultWeaponType = TEXT("Greataxe");
const FName UCataclysmCharacterCreation::DefaultDamageType = TEXT("Demonic");
const FName UCataclysmCharacterCreation::WeaponTypeWithNoAttack = TEXT("Shield");
const FName UCataclysmCharacterCreation::WeaponIndependent = TEXT("All");

namespace
{
	/**
	 * The eight damage types in the design's order.
	 *
	 * BORROWED RATHER THAN REPEATED. `UCataclysmItemModifiers::DamageTypeNames`
	 * already holds the one list, and a second copy here would be a second
	 * answer to "what are the eight and in what order".
	 */
	const TArray<FName>& DamageTypesInOrder()
	{
		return UCataclysmItemModifiers::DamageTypeNames();
	}

	/** Every row of the weapon skill matrix, or an empty view. */
	void ForEachSkillRow(const UDataTable* Table,
						 const TFunctionRef<void(const FCataclysmWeaponSkillRow&)>& Visit)
	{
		if (!Table)
		{
			return;
		}

		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			if (const FCataclysmWeaponSkillRow* Row =
					reinterpret_cast<const FCataclysmWeaponSkillRow*>(Pair.Value))
			{
				Visit(*Row);
			}
		}
	}

	/** Whether this matrix row is about a weapon at all. */
	bool RowIsAboutAWeapon(const FCataclysmWeaponSkillRow& Row)
	{
		const FName Type(*Row.WeaponType);
		return !Type.IsNone() &&
			   Type != UCataclysmCharacterCreation::WeaponIndependent;
	}
}

TArray<FName> UCataclysmCharacterCreation::StartingWeaponTypes(
	const UDataTable* BaseTable)
{
	TArray<FName> Types;
	if (!BaseTable)
	{
		return Types;
	}

	// ONE-HANDED FIRST AND THEN TWO-HANDED, ALPHABETICAL WITHIN EACH, and both
	// halves of that are read off the table rather than stated.
	//
	// SORTED RATHER THAN TAKEN IN ROW ORDER, and this is not fussiness. A
	// DataTable's rows live in a TMap, and a TMap's iteration order is an
	// implementation detail rather than the order the CSV was written in.
	// Relying on it would give a screen whose fourteen buttons could sit in a
	// different order after any re-import. `UCataclysmClassStats::ClassNames`
	// gathers and sorts for the same reason.
	TMap<FName, int32> HandsOf;
	for (const TPair<FName, uint8*>& Pair : BaseTable->GetRowMap())
	{
		const FCataclysmItemBaseRow* Row =
			reinterpret_cast<const FCataclysmItemBaseRow*>(Pair.Value);
		if (!Row)
		{
			continue;
		}

		const FName Type(*Row->WeaponType);
		if (Type.IsNone() || Type == WeaponTypeWithNoAttack)
		{
			continue;
		}

		HandsOf.Add(Type, Row->Hands);
	}

	HandsOf.GetKeys(Types);
	Types.Sort([&HandsOf](const FName& Left, const FName& Right)
	{
		const int32 LeftHands = HandsOf.FindChecked(Left);
		const int32 RightHands = HandsOf.FindChecked(Right);
		if (LeftHands != RightHands)
		{
			return LeftHands < RightHands;
		}
		return Left.LexicalLess(Right);
	});

	return Types;
}

TArray<FName> UCataclysmCharacterCreation::DamageTypesFor(
	const UDataTable* WeaponSkillTable, FName WeaponType)
{
	TArray<FName> Found;
	if (WeaponType.IsNone() || WeaponType == WeaponIndependent)
	{
		return Found;
	}

	TSet<FName> Carried;
	ForEachSkillRow(WeaponSkillTable, [&](const FCataclysmWeaponSkillRow& Row)
	{
		if (FName(*Row.WeaponType) == WeaponType)
		{
			Carried.Add(FName(*Row.DamageType));
		}
	});

	// IN THE DESIGN'S ORDER RATHER THAN THE TABLE'S. The eight damage types have
	// an order the design document prints them in and every screen in this
	// project uses it. Row order here would put them in whatever order the
	// workbook's rows happen to sit in, which differs per weapon.
	for (const FName& Type : DamageTypesInOrder())
	{
		if (Carried.Contains(Type))
		{
			Found.Add(Type);
		}
	}

	return Found;
}

TArray<FName> UCataclysmCharacterCreation::WeaponTypesFor(
	const UDataTable* WeaponSkillTable, const UDataTable* BaseTable,
	FName DamageType)
{
	TArray<FName> Found;
	if (DamageType.IsNone())
	{
		return Found;
	}

	TSet<FName> Carrying;
	ForEachSkillRow(WeaponSkillTable, [&](const FCataclysmWeaponSkillRow& Row)
	{
		if (RowIsAboutAWeapon(Row) && FName(*Row.DamageType) == DamageType)
		{
			Carrying.Add(FName(*Row.WeaponType));
		}
	});

	// FILTERED THROUGH THE OFFERED LIST rather than returned raw, so the Shield
	// is left out here for the same reason it is left out there, and a weapon
	// type the matrix names but the item bases table does not is left out too.
	for (const FName& Type : StartingWeaponTypes(BaseTable))
	{
		if (Carrying.Contains(Type))
		{
			Found.Add(Type);
		}
	}

	return Found;
}

bool UCataclysmCharacterCreation::IsLegalChoice(
	const UDataTable* WeaponSkillTable, const UDataTable* BaseTable,
	const FCataclysmCreationChoice& Choice)
{
	return RefusalFor(WeaponSkillTable, BaseTable, Choice).IsEmpty();
}

int32 UCataclysmCharacterCreation::DesignedSkillCount(
	const UDataTable* WeaponSkillTable, FName WeaponType, FName DamageType)
{
	if (WeaponType.IsNone() || DamageType.IsNone())
	{
		return 0;
	}

	int32 Designed = 0;
	ForEachSkillRow(WeaponSkillTable, [&](const FCataclysmWeaponSkillRow& Row)
	{
		if (FName(*Row.WeaponType) == WeaponType &&
			FName(*Row.DamageType) == DamageType &&
			!Row.SkillName.IsEmpty())
		{
			++Designed;
		}
	});

	return Designed;
}

const TMap<FName, TArray<FName>>& UCataclysmCharacterCreation::ClassesByDamageType()
{
	// THE "CLASSES BY DAMAGE TYPE" SECTION OF `docs/Cataclysm_GDD_v2.md`, in the
	// order it prints them, both down the damage types and across the three
	// classes of each.
	//
	// COMPARED AGAINST THAT SECTION BY
	// `tools/tests/test_character_creation_matches_the_design.py`, which reads
	// the eight tables out of the document and fails naming whichever side is
	// missing a class or spells one differently. A copy nothing checks is how a
	// renamed class survives in the game for a year.
	static const TMap<FName, TArray<FName>> Map = []()
	{
		TMap<FName, TArray<FName>> Built;
		Built.Add(TEXT("War"),
				  {TEXT("Bulwark"), TEXT("Berserker"), TEXT("Saboteur")});
		Built.Add(TEXT("Demonic"),
				  {TEXT("Ravager"), TEXT("Ritualist"), TEXT("Masochist")});
		Built.Add(TEXT("Death"),
				  {TEXT("Soul Collector"), TEXT("Necromancer"), TEXT("Shadow")});
		Built.Add(TEXT("Pestilence"),
				  {TEXT("Plague Lord"), TEXT("Virion"), TEXT("Poison Master")});
		Built.Add(TEXT("Famine"),
				  {TEXT("Vampire"), TEXT("Energy Leech"), TEXT("Shield Breaker")});
		Built.Add(TEXT("Celestial"),
				  {TEXT("Nephilim"), TEXT("Zealous Inquisitor"), TEXT("Dawnbringer")});
		Built.Add(TEXT("Chaos"),
				  {TEXT("Agent of Chaos"), TEXT("Chaos Shaper"),
				   TEXT("Discordant Trickster")});
		Built.Add(TEXT("Void"),
				  {TEXT("Singularity"), TEXT("Avatar of Madness"), TEXT("The Maw")});
		return Built;
	}();

	return Map;
}

const TArray<FName>& UCataclysmCharacterCreation::ClassesFor(FName DamageType)
{
	static const TArray<FName> None;
	const TArray<FName>* Found = ClassesByDamageType().Find(DamageType);
	return Found ? *Found : None;
}

FName UCataclysmCharacterCreation::ItemBaseFor(const UDataTable* BaseTable,
											   FName WeaponType)
{
	if (!BaseTable || WeaponType.IsNone())
	{
		return NAME_None;
	}

	// THE SMALLEST ROW KEY RATHER THAN THE FIRST ONE FOUND. `ItemBases.csv` has
	// exactly one row per weapon type today, so there is nothing to choose
	// between -- but a TMap's iteration order is not the CSV's, so were a second
	// ever added this would otherwise answer differently on different runs and
	// the character would begin holding a different item each time.
	FName Best = NAME_None;
	for (const TPair<FName, uint8*>& Pair : BaseTable->GetRowMap())
	{
		const FCataclysmItemBaseRow* Row =
			reinterpret_cast<const FCataclysmItemBaseRow*>(Pair.Value);
		if (Row && FName(*Row->WeaponType) == WeaponType &&
			(Best.IsNone() || Pair.Key.LexicalLess(Best)))
		{
			Best = Pair.Key;
		}
	}

	return Best;
}

FString UCataclysmCharacterCreation::SummaryFor(
	const UDataTable* WeaponSkillTable, const FCataclysmCreationChoice& Choice)
{
	if (!Choice.IsComplete())
	{
		return FString();
	}

	const int32 Designed = DesignedSkillCount(WeaponSkillTable,
											  Choice.WeaponType,
											  Choice.DamageType);

	// THE SKILL COUNT IS PART OF THE SUMMARY AND NOT A FOOTNOTE. Six of the 390
	// legal pairings out of seven have no skill written at all, so a player
	// picking one gets a character that cannot do anything, and finding that out
	// after confirming is the worst moment to find it out. Issues #62 and #836.
	if (Designed == 0)
	{
		return FString::Printf(
			TEXT("%s, %s    no skills are designed for this pairing yet"),
			*Choice.DamageType.ToString(), *Choice.WeaponType.ToString());
	}

	return FString::Printf(TEXT("%s, %s    %d of 6 skills designed"),
						   *Choice.DamageType.ToString(),
						   *Choice.WeaponType.ToString(), Designed);
}

FString UCataclysmCharacterCreation::UnlockedClassesFor(
	const FCataclysmCreationChoice& Choice)
{
	const TArray<FName>& Classes = ClassesFor(Choice.DamageType);
	if (Classes.Num() == 0)
	{
		return FString();
	}

	TArray<FString> Names;
	Names.Reserve(Classes.Num());
	for (const FName& Class : Classes)
	{
		Names.Add(Class.ToString());
	}

	return FString::Printf(TEXT("Unlocks the %s class trees:  %s"),
						   *Choice.DamageType.ToString(),
						   *FString::Join(Names, TEXT("    ")));
}

FString UCataclysmCharacterCreation::RefusalFor(
	const UDataTable* WeaponSkillTable, const UDataTable* BaseTable,
	const FCataclysmCreationChoice& Choice)
{
	if (Choice.WeaponType.IsNone() && Choice.DamageType.IsNone())
	{
		return TEXT("Choose a weapon type and a damage type.");
	}

	if (Choice.WeaponType.IsNone())
	{
		return TEXT("Choose a weapon type.");
	}

	if (Choice.DamageType.IsNone())
	{
		return TEXT("Choose a damage type.");
	}

	// THE OFFERED LIST RATHER THAN THE TABLE, so the Shield is refused here by
	// the same rule that keeps it off the screen. A console command can reach
	// this with a Shield in hand where the screen cannot.
	const TArray<FName> Offered = StartingWeaponTypes(BaseTable);
	if (Offered.Num() == 0)
	{
		return TEXT("The item bases table could not be read, so no weapon type "
					"can be offered.");
	}

	if (!Offered.Contains(Choice.WeaponType))
	{
		if (Choice.WeaponType == WeaponTypeWithNoAttack)
		{
			return FString::Printf(
				TEXT("A character cannot begin holding only a %s, because it "
					 "grants no attack damage and every skill is a percentage "
					 "of weapon damage."),
				*Choice.WeaponType.ToString());
		}

		return FString::Printf(TEXT("%s is not a weapon type."),
							   *Choice.WeaponType.ToString());
	}

	const TArray<FName> Carried = DamageTypesFor(WeaponSkillTable,
												 Choice.WeaponType);
	if (!Carried.Contains(Choice.DamageType))
	{
		return FString::Printf(TEXT("A %s cannot carry %s damage."),
							   *Choice.WeaponType.ToString(),
							   *Choice.DamageType.ToString());
	}

	return FString();
}
