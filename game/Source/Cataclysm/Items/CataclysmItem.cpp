// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmItem.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

namespace
{
	/**
	 * What each rarity is made of: how many of the four slots hold an
	 * enchantment, and how many hold a regular affix.
	 *
	 * Indexed by ECataclysmRarity. Mirrors RARITY_COMPOSITION in
	 * sim/cataclysm_sim/affixes.py, and an automation test asserts the two agree.
	 */
	struct FComposition { int32 Enchantments; int32 Affixes; };

	constexpr FComposition RarityComposition[] = {
		{ 0, 1 },  // Everyday
		{ 0, 2 },  // Quality
		{ 0, 3 },  // Superb
		{ 0, 4 },  // Masterful
		{ 1, 3 },  // Legendary
		{ 2, 2 },  // Mythical
		{ 3, 1 },  // Ascendant
		{ 4, 0 },  // Cataclysmic
	};

	constexpr int32 RarityCount = UE_ARRAY_COUNT(RarityComposition);

	const FComposition& CompositionOf(ECataclysmRarity Rarity)
	{
		const int32 Index = FMath::Clamp(static_cast<int32>(Rarity), 0, RarityCount - 1);
		return RarityComposition[Index];
	}

	/** Which stat pipeline bucket a "flat" or "increased" column means. */
	bool BucketFromKind(const FString& Kind, ECataclysmStatBucket& OutBucket)
	{
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
}

// ---------------------------------------------------------------------------
// The value curves
// ---------------------------------------------------------------------------

float UCataclysmItemValues::GearLevelMultiplier(int32 GearLevel)
{
	const int32 Level = FMath::Clamp(GearLevel, 0, MaxGearLevel);
	return 1.0f + GearLevelFactor * static_cast<float>(Level);
}

float UCataclysmItemValues::TierFraction(int32 Tier)
{
	const int32 Clamped = FMath::Clamp(Tier, 1, MaxAffixTier);
	return static_cast<float>(Clamped) / static_cast<float>(MaxAffixTier);
}

void UCataclysmItemValues::TierBand(float TopValue, int32 Tier,
									float& OutLow, float& OutHigh)
{
	OutHigh = TopValue * TierFraction(Tier);
	OutLow = OutHigh * (1.0f - RollBandFraction);
}

float UCataclysmItemValues::AffixValue(float TopValue, int32 Tier, float Roll,
									   int32 GearLevel, bool bTwoHanded)
{
	float Low = 0.0f;
	float High = 0.0f;
	TierBand(TopValue, Tier, Low, High);

	const float Within = Low + (High - Low) * FMath::Clamp(Roll, 0.0f, 1.0f);

	// The stated top values are the +10 figures, so the band is divided back to
	// +0 before the piece's own level is applied. Doing it the other way round
	// would make a +10 piece worth 3.52 times a number that already included
	// 3.52.
	const float AtZero = Within / GearLevelMultiplier(MaxGearLevel);
	return AtZero * GearLevelMultiplier(GearLevel)
		 * (bTwoHanded ? TwoHandedMultiplier : 1.0f);
}

float UCataclysmItemValues::ImplicitValue(float StatedValue, int32 GearLevel,
										  bool bTwoHanded)
{
	// No tier and no band: an implicit is what the base IS rather than what a
	// particular drop happened to get. Upgrade level and the two-handed
	// multiplier still apply, exactly as they do to an affix.
	const float AtZero = StatedValue / GearLevelMultiplier(MaxGearLevel);
	return AtZero * GearLevelMultiplier(GearLevel)
		 * (bTwoHanded ? TwoHandedMultiplier : 1.0f);
}

// ---------------------------------------------------------------------------
// Rarity, which is a label for the contents rather than a stored field
// ---------------------------------------------------------------------------

bool UCataclysmItemValues::RarityOf(int32 EnchantmentCount, int32 AffixCount,
									ECataclysmRarity& OutRarity)
{
	for (int32 Index = 0; Index < RarityCount; ++Index)
	{
		if (RarityComposition[Index].Enchantments == EnchantmentCount
			&& RarityComposition[Index].Affixes == AffixCount)
		{
			OutRarity = static_cast<ECataclysmRarity>(Index);
			return true;
		}
	}

	// Not every combination is an item. Below Legendary a piece fills only as
	// many slots as it has affixes; from there it fills all four. An
	// enchantment with slots left empty is neither.
	return false;
}

int32 UCataclysmItemValues::AffixSlotsFor(ECataclysmRarity Rarity)
{
	return CompositionOf(Rarity).Affixes;
}

int32 UCataclysmItemValues::EnchantmentsFor(ECataclysmRarity Rarity)
{
	return CompositionOf(Rarity).Enchantments;
}

void UCataclysmItemValues::PrefixSuffixSplit(int32 Slots, int32& OutPrefixes,
											 int32& OutSuffixes)
{
	const int32 Clamped = FMath::Clamp(Slots, 0, SlotsPerPiece);
	OutPrefixes = FMath::Min(PrefixesPerPiece, (Clamped + 1) / 2);
	OutSuffixes = Clamped - OutPrefixes;
}

// ---------------------------------------------------------------------------
// Turning an item into stat modifiers
// ---------------------------------------------------------------------------

bool UCataclysmItemModifiers::RarityOfItem(const FCataclysmItem& Item,
										   ECataclysmRarity& OutRarity)
{
	return UCataclysmItemValues::RarityOf(Item.EnchantmentCount,
										  Item.Affixes.Num(), OutRarity);
}

bool UCataclysmItemModifiers::IsTwoHanded(const FCataclysmItem& Item,
										  const UDataTable* BaseTable)
{
	if (!BaseTable)
	{
		return false;
	}
	const FCataclysmItemBaseRow* Row =
		BaseTable->FindRow<FCataclysmItemBaseRow>(Item.Base, TEXT("IsTwoHanded"),
												  /*bWarnIfMissing=*/false);
	return Row && Row->Hands == 2;
}

TArray<FCataclysmStatModifier> UCataclysmItemModifiers::ModifiersFor(
	const FCataclysmItem& Item,
	const UDataTable* BaseTable,
	const UDataTable* AffixTable)
{
	TArray<FCataclysmStatModifier> Out;
	if (!BaseTable || !AffixTable)
	{
		return Out;
	}

	const FCataclysmItemBaseRow* Base =
		BaseTable->FindRow<FCataclysmItemBaseRow>(Item.Base, TEXT("ModifiersFor"),
												  /*bWarnIfMissing=*/false);
	if (!Base)
	{
		UE_LOG(LogCataclysm, Warning,
			   TEXT("Item names base %s, which is not in the base table"),
			   *Item.Base.ToString());
		return Out;
	}

	const bool bTwoHanded = Base->Hands == 2;

	// The base's implicits. What the item IS, before any roll.
	const FString ImplicitStats[] = { Base->Implicit1Stat, Base->Implicit2Stat };
	const FString ImplicitKinds[] = { Base->Implicit1Kind, Base->Implicit2Kind };
	const float ImplicitValues[] = { Base->Implicit1Value, Base->Implicit2Value };

	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (ImplicitStats[Index].IsEmpty())
		{
			continue;
		}
		ECataclysmStatBucket Bucket;
		if (!BucketFromKind(ImplicitKinds[Index], Bucket))
		{
			UE_LOG(LogCataclysm, Warning,
				   TEXT("%s implicit %d has kind '%s', expected flat or increased"),
				   *Item.Base.ToString(), Index + 1, *ImplicitKinds[Index]);
			continue;
		}

		FCataclysmStatModifier Modifier;
		Modifier.Bucket = Bucket;
		Modifier.Source = ECataclysmModifierSource::GearImplicit;
		Modifier.Value = UCataclysmItemValues::ImplicitValue(
			ImplicitValues[Index], Item.GearLevel, bTwoHanded);
		Out.Add(Modifier);
	}

	// The rolled affixes.
	for (const FCataclysmRolledAffix& Rolled : Item.Affixes)
	{
		const FCataclysmAffixRow* Affix =
			AffixTable->FindRow<FCataclysmAffixRow>(Rolled.Affix,
													TEXT("ModifiersFor"),
													/*bWarnIfMissing=*/false);
		if (!Affix)
		{
			UE_LOG(LogCataclysm, Warning,
				   TEXT("Item carries affix %s, which is not in the affix table"),
				   *Rolled.Affix.ToString());
			continue;
		}

		ECataclysmStatBucket Bucket;
		if (!BucketFromKind(Affix->ValueKind, Bucket))
		{
			// Resistance families and ailment chances carry no flat/increased
			// kind: a resistance family grants a percentage to several damage
			// types, and an ailment grants a chance to apply an effect. Neither
			// is a single stat on the character sheet, so neither becomes one
			// modifier here. Both are handled where they are applied.
			continue;
		}

		FCataclysmStatModifier Modifier;
		Modifier.Bucket = Bucket;
		Modifier.Source = ECataclysmModifierSource::GearAffix;
		Modifier.Value = UCataclysmItemValues::AffixValue(
			Affix->TopValue, Rolled.Tier, Rolled.Roll, Item.GearLevel, bTwoHanded);
		Out.Add(Modifier);
	}

	return Out;
}
