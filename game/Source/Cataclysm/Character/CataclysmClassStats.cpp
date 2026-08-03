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
		Percent += Row->PercentPerPoint
				 * static_cast<float>(Points.PointsIn(Row->Attribute));
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
