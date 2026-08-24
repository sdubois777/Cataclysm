// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmClassStats.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

const FString UCataclysmClassStats::DefaultClassName = TEXT("Default");

int32 FCataclysmAttributePoints::Total() const
{
	return Agility + Ferocity + Constitution + Vitality
		 + Mind + Spirit + Efficacy + Luck;
}

int32 FCataclysmAttributePoints::PointsIn(const FString& Attribute) const
{
	// Compared without case, because the sheet writes them lower case and a
	// caller naturally writes "Vitality".
	if (Attribute.Equals(TEXT("agility"), ESearchCase::IgnoreCase))      return Agility;
	if (Attribute.Equals(TEXT("ferocity"), ESearchCase::IgnoreCase))     return Ferocity;
	if (Attribute.Equals(TEXT("constitution"), ESearchCase::IgnoreCase)) return Constitution;
	if (Attribute.Equals(TEXT("vitality"), ESearchCase::IgnoreCase))     return Vitality;
	if (Attribute.Equals(TEXT("mind"), ESearchCase::IgnoreCase))         return Mind;
	if (Attribute.Equals(TEXT("spirit"), ESearchCase::IgnoreCase))       return Spirit;
	if (Attribute.Equals(TEXT("efficacy"), ESearchCase::IgnoreCase))     return Efficacy;
	if (Attribute.Equals(TEXT("luck"), ESearchCase::IgnoreCase))         return Luck;

	UE_LOG(LogCataclysm, Warning,
		   TEXT("Attributes table names %s, which is not one of the eight"),
		   *Attribute);
	return 0;
}

TArray<FString> FCataclysmAttributePoints::Names()
{
	// DECLARATION ORDER, AND SPELLED AS game/Data/Attributes.csv SPELLS THEM.
	// Everything that has to name all eight -- the stat-to-attribute map, the
	// console commands, the save record, the tests -- asks here, so a ninth
	// attribute is one edit rather than a hunt.
	return {TEXT("agility"), TEXT("ferocity"), TEXT("constitution"),
			TEXT("vitality"), TEXT("mind"), TEXT("spirit"),
			TEXT("efficacy"), TEXT("luck")};
}

bool FCataclysmAttributePoints::AddTo(const FString& Attribute, int32 Count)
{
	if (Attribute.Equals(TEXT("agility"), ESearchCase::IgnoreCase))      { Agility += Count;      return true; }
	if (Attribute.Equals(TEXT("ferocity"), ESearchCase::IgnoreCase))     { Ferocity += Count;     return true; }
	if (Attribute.Equals(TEXT("constitution"), ESearchCase::IgnoreCase)) { Constitution += Count; return true; }
	if (Attribute.Equals(TEXT("vitality"), ESearchCase::IgnoreCase))     { Vitality += Count;     return true; }
	if (Attribute.Equals(TEXT("mind"), ESearchCase::IgnoreCase))         { Mind += Count;         return true; }
	if (Attribute.Equals(TEXT("spirit"), ESearchCase::IgnoreCase))       { Spirit += Count;       return true; }
	if (Attribute.Equals(TEXT("efficacy"), ESearchCase::IgnoreCase))     { Efficacy += Count;     return true; }
	if (Attribute.Equals(TEXT("luck"), ESearchCase::IgnoreCase))         { Luck += Count;         return true; }

	// NO WARNING HERE, unlike PointsIn above. A mistyped name arriving from the
	// console is a person's slip and the caller says so in its own words; a
	// mistyped name arriving from the data table is a fault in the project.
	return false;
}

float FCataclysmAttributeValues::ValueIn(const FString& Attribute) const
{
	if (Attribute.Equals(TEXT("agility"), ESearchCase::IgnoreCase))      return Agility;
	if (Attribute.Equals(TEXT("ferocity"), ESearchCase::IgnoreCase))     return Ferocity;
	if (Attribute.Equals(TEXT("constitution"), ESearchCase::IgnoreCase)) return Constitution;
	if (Attribute.Equals(TEXT("vitality"), ESearchCase::IgnoreCase))     return Vitality;
	if (Attribute.Equals(TEXT("mind"), ESearchCase::IgnoreCase))         return Mind;
	if (Attribute.Equals(TEXT("spirit"), ESearchCase::IgnoreCase))       return Spirit;
	if (Attribute.Equals(TEXT("efficacy"), ESearchCase::IgnoreCase))     return Efficacy;
	if (Attribute.Equals(TEXT("luck"), ESearchCase::IgnoreCase))         return Luck;

	UE_LOG(LogCataclysm, Warning,
		   TEXT("Attributes table names %s, which is not one of the eight"),
		   *Attribute);
	return 0.0f;
}

bool FCataclysmAttributeValues::SetIn(const FString& Attribute, float Value)
{
	if (Attribute.Equals(TEXT("agility"), ESearchCase::IgnoreCase))      { Agility = Value;      return true; }
	if (Attribute.Equals(TEXT("ferocity"), ESearchCase::IgnoreCase))     { Ferocity = Value;     return true; }
	if (Attribute.Equals(TEXT("constitution"), ESearchCase::IgnoreCase)) { Constitution = Value; return true; }
	if (Attribute.Equals(TEXT("vitality"), ESearchCase::IgnoreCase))     { Vitality = Value;     return true; }
	if (Attribute.Equals(TEXT("mind"), ESearchCase::IgnoreCase))         { Mind = Value;         return true; }
	if (Attribute.Equals(TEXT("spirit"), ESearchCase::IgnoreCase))       { Spirit = Value;       return true; }
	if (Attribute.Equals(TEXT("efficacy"), ESearchCase::IgnoreCase))     { Efficacy = Value;     return true; }
	if (Attribute.Equals(TEXT("luck"), ESearchCase::IgnoreCase))         { Luck = Value;         return true; }
	return false;
}

FCataclysmAttributeValues FCataclysmAttributeValues::FromPoints(
	const FCataclysmAttributePoints& Points)
{
	FCataclysmAttributeValues Values;
	Values.Agility      = static_cast<float>(Points.Agility);
	Values.Ferocity     = static_cast<float>(Points.Ferocity);
	Values.Constitution = static_cast<float>(Points.Constitution);
	Values.Vitality     = static_cast<float>(Points.Vitality);
	Values.Mind         = static_cast<float>(Points.Mind);
	Values.Spirit       = static_cast<float>(Points.Spirit);
	Values.Efficacy     = static_cast<float>(Points.Efficacy);
	Values.Luck         = static_cast<float>(Points.Luck);
	return Values;
}

namespace
{
	/** The class stat row for a class and stat, or null. */
	const FCataclysmClassStatRow* FindClassStat(const UDataTable* Table,
												const FString& ClassName,
												const FString& Stat)
	{
		if (!Table)
		{
			return nullptr;
		}
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const auto* Row = reinterpret_cast<const FCataclysmClassStatRow*>(Pair.Value);
			if (Row->ClassName.Equals(ClassName, ESearchCase::IgnoreCase)
				&& Row->Stat.Equals(Stat, ESearchCase::IgnoreCase))
			{
				return Row;
			}
		}
		return nullptr;
	}
}

float UCataclysmClassStats::BaseFor(const UDataTable* ClassTable,
									const FString& ClassName,
									const FString& Stat, int32 Level)
{
	const FCataclysmClassStatRow* Row = FindClassStat(ClassTable, ClassName, Stat);
	if (!Row)
	{
		// The class does not override this stat, so it inherits the shared line.
		Row = FindClassStat(ClassTable, DefaultClassName, Stat);
	}
	if (!Row)
	{
		// Neither names it, which is legitimate: most classes leave most of the
		// 33 stats alone, and a stat nobody supplies is simply zero.
		return 0.0f;
	}

	// The per-level gain applies to levels ABOVE the first, so a level 1
	// character has exactly the base.
	const int32 Clamped = FMath::Clamp(Level, 1, MaxLevel);
	return Row->Base + Row->PerLevel * static_cast<float>(Clamped - 1);
}

bool UCataclysmClassStats::Overrides(const UDataTable* ClassTable,
									 const FString& ClassName,
									 const FString& Stat)
{
	return FindClassStat(ClassTable, ClassName, Stat) != nullptr;
}

bool UCataclysmClassStats::AttributeModifierFor(
	const UDataTable* AttributeTable,
	const FCataclysmAttributePoints& Points,
	const FString& Stat,
	FCataclysmStatModifier& OutModifier)
{
	// SPENT POINTS ARE RESOLVED VALUES WITH NOTHING SCALING THEM, so this is
	// the same question asked of a character wearing no gear. The reference
	// build test asks it that way deliberately; the game asks the other one.
	return AttributeModifierForValues(
		AttributeTable, FCataclysmAttributeValues::FromPoints(Points),
		Stat, OutModifier);
}

bool UCataclysmClassStats::AttributeModifierForValues(
	const UDataTable* AttributeTable,
	const FCataclysmAttributeValues& Values,
	const FString& Stat,
	FCataclysmStatModifier& OutModifier)
{
	if (!AttributeTable)
	{
		return false;
	}

	float Percent = 0.0f;
	bool bAnyApplies = false;

	for (const TPair<FName, uint8*>& Pair : AttributeTable->GetRowMap())
	{
		const auto* Row = reinterpret_cast<const FCataclysmAttributeEffectRow*>(Pair.Value);
		if (!Row->Stat.Equals(Stat, ESearchCase::IgnoreCase))
		{
			continue;
		}
		bAnyApplies = true;
		Percent += Row->PercentPerPoint * Values.ValueIn(Row->Attribute);
	}

	if (!bAnyApplies)
	{
		return false;
	}

	// ALWAYS THE INCREASED BUCKET, NEVER FLAT. An attribute point scales a base
	// that something else supplied; it never creates one. That is why a stat
	// with no base gains nothing from its attribute.
	OutModifier = FCataclysmStatModifier();
	OutModifier.Bucket = ECataclysmStatBucket::Increased;
	OutModifier.Source = ECataclysmModifierSource::Attribute;
	OutModifier.Value = Percent;
	return true;
}

TArray<FString> UCataclysmClassStats::StatsNamedByClasses(const UDataTable* ClassTable)
{
	TSet<FString> Seen;
	if (ClassTable)
	{
		for (const TPair<FName, uint8*>& Pair : ClassTable->GetRowMap())
		{
			const auto* Row = reinterpret_cast<const FCataclysmClassStatRow*>(Pair.Value);
			Seen.Add(Row->Stat);
		}
	}
	TArray<FString> Out = Seen.Array();
	Out.Sort();
	return Out;
}

TArray<FString> UCataclysmClassStats::ClassNames(const UDataTable* ClassTable)
{
	TSet<FString> Seen;
	if (ClassTable)
	{
		for (const TPair<FName, uint8*>& Pair : ClassTable->GetRowMap())
		{
			const auto* Row = reinterpret_cast<const FCataclysmClassStatRow*>(Pair.Value);
			if (!Row->ClassName.Equals(DefaultClassName, ESearchCase::IgnoreCase))
			{
				Seen.Add(Row->ClassName);
			}
		}
	}
	TArray<FString> Out = Seen.Array();
	Out.Sort();
	return Out;
}
