// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmGems.h"

#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"

float UCataclysmGems::SheetValueAt(const FCataclysmGemRow& Row, ECataclysmRarity Rarity)
{
	switch (Rarity)
	{
	case ECataclysmRarity::Everyday:	return Row.Everyday;
	case ECataclysmRarity::Quality:		return Row.Quality;
	case ECataclysmRarity::Superb:		return Row.Superb;
	case ECataclysmRarity::Masterful:	return Row.Masterful;
	case ECataclysmRarity::Legendary:	return Row.Legendary;
	case ECataclysmRarity::Mythical:	return Row.Mythical;
	case ECataclysmRarity::Ascendant:	return Row.Ascendant;
	case ECataclysmRarity::Cataclysmic:	return Row.Cataclysmic;
	}
	return 0.0f;
}

bool UCataclysmGems::BucketNamed(const FString& ValueKind, ECataclysmStatBucket& OutBucket)
{
	const FString Kind = ValueKind.TrimStartAndEnd();
	if (Kind.Equals(TEXT("flat"), ESearchCase::IgnoreCase))
	{
		OutBucket = ECataclysmStatBucket::Flat;
		return true;
	}
	if (Kind.Equals(TEXT("increased"), ESearchCase::IgnoreCase))
	{
		OutBucket = ECataclysmStatBucket::Increased;
		return true;
	}
	return false;
}

bool UCataclysmGems::AddModifier(FName Stat, const FString& ValueKind, float SheetValue,
								 TMap<FName, TArray<FCataclysmStatModifier>>& Totals)
{
	ECataclysmStatBucket Bucket;
	if (Stat.IsNone() || !BucketNamed(ValueKind, Bucket) || SheetValue <= 0.0f)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("A gem naming stat '%s' as '%s' at %.3f grants nothing: a gem needs a stat, a kind of flat or "
				 "increased, and a value above nought. Check the Gems sheet's Stat and ValueKind columns."),
			*Stat.ToString(), *ValueKind, SheetValue);
		return false;
	}

	FCataclysmStatModifier Modifier;
	Modifier.Bucket = Bucket;
	Modifier.Source = ECataclysmModifierSource::Gem;
	Modifier.Value = SheetValue * PointsPerSheetUnit;
	Totals.FindOrAdd(Stat).Add(Modifier);
	return true;
}
