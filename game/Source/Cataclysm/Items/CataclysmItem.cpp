// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmItem.h"
// For the stat an Ailment affix's chance is kept under. Issue #899.
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmDropRoll.h"
// The gems in an item's sockets, issue #46 slice G3.
#include "Items/CataclysmGems.h"

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

	// GEOMETRIC, AND IT WAS LINEAR UNTIL 2026-09-03. It ran
	// `Clamped / MaxAffixTier`, so tier 7 was worth seven times tier 1;
	// multiplied by the gear level ladder that put a tier 1 roll on an
	// un-upgraded drop at 3.04% of the endgame value. Issue #1179.
	//
	// A CONSTANT RATIO RATHER THAN A FLATTER STRAIGHT LINE, and that is not a
	// taste. A roll band runs from 75% to 100% of its tier, so a tier is
	// undercut by the one below whenever the gap between them is less than a
	// quarter. Squashing a LINEAR ladder to span 3.0 lets a perfect tier 5 roll
	// beat a poor tier 7 one -- a two-tier overlap, which
	// Cataclysm.Item.BandsOverlapByExactlyOneTier forbids and which the note on
	// RollBandFraction proves cannot happen. A constant ratio of 1.2009 keeps
	// the bound: two tiers apart is 1.442 and the bound needs more than 1.333.
	//
	// AND IT IS BACK-LOADED, which serves the reason the linear ladder existed
	// rather than overturning it. The step from tier 6 to tier 7 is now worth
	// more than the step from tier 1 to tier 2, so the last tiers are the ones
	// that matter most and the hardest to skip.
	//
	// Tier 7 is 1.0 exactly, so the stated top values are unchanged.
	return FMath::Pow(TierLadderSpan,
					  static_cast<float>(Clamped - MaxAffixTier)
					  / static_cast<float>(MaxAffixTier - 1));
}

void UCataclysmItemValues::TierBand(float TopValue, int32 Tier,
									float& OutLow, float& OutHigh)
{
	OutHigh = TopValue * TierFraction(Tier);
	OutLow = OutHigh * (1.0f - RollBandFraction);
}

float UCataclysmItemValues::WorstMultiplier()
{
	return TierFraction(1) * (1.0f - RollBandFraction)
		 / GearLevelMultiplier(MaxGearLevel);
}

// ---------------------------------------------------------------------------
// Enchantment values
// ---------------------------------------------------------------------------

namespace
{
	/**
	 * How many decimal places a stated number needs: 0, 1 or 2.
	 *
	 * READ OFF THE VALUE RATHER THAN ITS TEXT, so an effect row's two floats and
	 * a sentence's two numbers give the same answer, and the hover text and the
	 * stat therefore cut a range into the same values. No enchantment states a
	 * number finer than a hundredth, so anything finer counts as two places.
	 */
	int32 EnchantmentDecimalPlaces(float Value)
	{
		for (int32 Places = 0; Places < 2; ++Places)
		{
			const float Scaled =
				FMath::Abs(Value) * FMath::Pow(10.0f, static_cast<float>(Places));
			if (FMath::IsNearlyEqual(Scaled, FMath::RoundToFloat(Scaled), 1.0e-3f))
			{
				return Places;
			}
		}
		return 2;
	}

	/** One number as an enchantment's sentence writes it. */
	struct FEnchantmentNumberText
	{
		/** Where its first digit is. */
		int32 Start = 0;

		/** One past its last character, the percent sign included. */
		int32 End = 0;

		float Value = 0.0f;
		bool bPercent = false;
		bool bCommas = false;
	};

	/**
	 * The number starting exactly at `At`: digits, which may be grouped by
	 * commas, an optional decimal part, then an optional percent sign. False
	 * when `At` does not hold a digit.
	 */
	bool ReadEnchantmentNumber(const FString& Text, int32 At,
							   FEnchantmentNumberText& Out)
	{
		if (!Text.IsValidIndex(At) || !FChar::IsDigit(Text[At]))
		{
			return false;
		}

		FString Digits;
		bool bCommas = false;
		int32 Cursor = At;
		while (Text.IsValidIndex(Cursor))
		{
			if (FChar::IsDigit(Text[Cursor]))
			{
				Digits.AppendChar(Text[Cursor]);
				++Cursor;
			}
			// A COMMA IS PART OF THE NUMBER ONLY WHEN A DIGIT FOLLOWS IT, so the
			// comma in "life, mana" ends the number before it.
			else if (Text[Cursor] == TEXT(',') && Text.IsValidIndex(Cursor + 1)
					 && FChar::IsDigit(Text[Cursor + 1]))
			{
				bCommas = true;
				++Cursor;
			}
			else
			{
				break;
			}
		}

		// SO IS A DECIMAL POINT, so the full stop ending "20% less hp." is not.
		if (Text.IsValidIndex(Cursor + 1) && Text[Cursor] == TEXT('.')
			&& FChar::IsDigit(Text[Cursor + 1]))
		{
			Digits.AppendChar(TEXT('.'));
			++Cursor;
			while (Text.IsValidIndex(Cursor) && FChar::IsDigit(Text[Cursor]))
			{
				Digits.AppendChar(Text[Cursor]);
				++Cursor;
			}
		}

		Out.Start = At;
		Out.Value = FCString::Atof(*Digits);
		Out.bCommas = bCommas;
		Out.bPercent = Text.IsValidIndex(Cursor) && Text[Cursor] == TEXT('%');
		Out.End = Out.bPercent ? Cursor + 1 : Cursor;
		return true;
	}

	/**
	 * The first range at or after `From`: a number, optional spaces, a hyphen,
	 * optional spaces and a number. False when none is left.
	 */
	bool FindEnchantmentRange(const FString& Text, int32 From,
							  FEnchantmentNumberText& OutFirst,
							  FEnchantmentNumberText& OutSecond)
	{
		int32 At = From;
		while (At < Text.Len())
		{
			// THE START OF A NUMBER, NOT ITS MIDDLE: a digit with no digit, comma
			// or decimal point directly before it.
			const bool bStartsNumber = FChar::IsDigit(Text[At])
				&& !(At > 0 && (FChar::IsDigit(Text[At - 1])
								|| Text[At - 1] == TEXT(',')
								|| Text[At - 1] == TEXT('.')));
			FEnchantmentNumberText First;
			if (!bStartsNumber || !ReadEnchantmentNumber(Text, At, First))
			{
				++At;
				continue;
			}

			int32 Cursor = First.End;
			while (Text.IsValidIndex(Cursor) && Text[Cursor] == TEXT(' '))
			{
				++Cursor;
			}

			FEnchantmentNumberText Second;
			bool bRange = Text.IsValidIndex(Cursor) && Text[Cursor] == TEXT('-');
			if (bRange)
			{
				++Cursor;
				while (Text.IsValidIndex(Cursor) && Text[Cursor] == TEXT(' '))
				{
					++Cursor;
				}
				bRange = ReadEnchantmentNumber(Text, Cursor, Second);
			}
			if (bRange)
			{
				OutFirst = First;
				OutSecond = Second;
				return true;
			}
			At = First.End;
		}
		return false;
	}

	/**
	 * A value written the way its sentence writes numbers: as many decimal
	 * places as the range needs and no trailing zeros, with thousands commas
	 * when the sentence used them.
	 */
	FString EnchantmentNumberAsWritten(float Value, int32 Places, bool bCommas)
	{
		FString Text = Places <= 0 ? FString::Printf(TEXT("%.0f"), Value)
					 : Places == 1 ? FString::Printf(TEXT("%.1f"), Value)
								   : FString::Printf(TEXT("%.2f"), Value);
		if (Places > 0)
		{
			while (Text.EndsWith(TEXT("0")))
			{
				Text.LeftChopInline(1);
			}
			if (Text.EndsWith(TEXT(".")))
			{
				Text.LeftChopInline(1);
			}
		}

		if (bCommas)
		{
			int32 Point = INDEX_NONE;
			if (!Text.FindChar(TEXT('.'), Point))
			{
				Point = Text.Len();
			}
			const int32 FirstDigit = Text.StartsWith(TEXT("-")) ? 1 : 0;
			for (int32 At = Point - 3; At > FirstDigit; At -= 3)
			{
				Text.InsertAt(At, TEXT(','));
			}
		}
		return Text;
	}
}

float UCataclysmItemValues::EnchantmentValue(float From, float To, float Roll)
{
	const int32 Places = FMath::Max(EnchantmentDecimalPlaces(From),
									EnchantmentDecimalPlaces(To));
	const float Scale = FMath::Pow(10.0f, static_cast<float>(Places));
	const int32 Steps = FMath::RoundToInt(FMath::Abs(To - From) * Scale);
	if (Steps <= 0)
	{
		return From;
	}

	// STEPS + 1 WRITTEN VALUES, EACH OWNING AN EQUAL SHARE OF THE ROLL. A roll
	// of exactly 1 would fall past the last share, so it is held in the last
	// one, which is where the default of 1 is meant to land.
	const int32 Index = FMath::Min(
		FMath::FloorToInt(FMath::Clamp(Roll, 0.0f, 1.0f)
						  * static_cast<float>(Steps + 1)),
		Steps);
	const float Direction = To >= From ? 1.0f : -1.0f;
	const float Value = From + Direction * static_cast<float>(Index) / Scale;

	// ROUNDED TO THE RANGE'S OWN PRECISION, so that five tenths added to 0.5
	// come to 1 rather than to 1.0000001.
	return FMath::RoundToFloat(Value * Scale) / Scale;
}

TArray<FVector2D> UCataclysmItemValues::EnchantmentRanges(const FString& Effect)
{
	TArray<FVector2D> Ranges;
	FEnchantmentNumberText First;
	FEnchantmentNumberText Second;
	int32 From = 0;
	while (FindEnchantmentRange(Effect, From, First, Second))
	{
		Ranges.Add(FVector2D(First.Value, Second.Value));
		From = Second.End;
	}
	return Ranges;
}

FString UCataclysmItemValues::EnchantmentTextAtRoll(const FString& Effect,
													 float Roll)
{
	return EnchantmentTextFor(Effect, FString(), Roll);
}

TSet<int32> UCataclysmItemValues::RangesRollingDown(const FString& RollsDown)
{
	TSet<int32> Places;
	TArray<FString> Parts;
	RollsDown.ParseIntoArray(Parts, TEXT(","));
	for (FString Part : Parts)
	{
		Part.TrimStartAndEndInline();
		if (Part.IsNumeric())
		{
			Places.Add(FCString::Atoi(*Part));
		}
	}
	return Places;
}

FString UCataclysmItemValues::EnchantmentTextFor(const FString& Effect,
												  const FString& RollsDown,
												  float Roll)
{
	const TSet<int32> Down = RangesRollingDown(RollsDown);

	FString Out;
	FEnchantmentNumberText First;
	FEnchantmentNumberText Second;
	int32 Copied = 0;
	int32 Place = 0;
	while (FindEnchantmentRange(Effect, Copied, First, Second))
	{
		++Place;
		const int32 Places = FMath::Max(EnchantmentDecimalPlaces(First.Value),
										EnchantmentDecimalPlaces(Second.Value));
		// A MARKED RANGE ROLLS FROM ITS SECOND NUMBER TO ITS FIRST, which is
		// the order the generator wrote its effect pair in.
		const bool bDown = Down.Contains(Place);
		Out += Effect.Mid(Copied, First.Start - Copied);
		Out += EnchantmentNumberAsWritten(
			EnchantmentValue(bDown ? Second.Value : First.Value,
							 bDown ? First.Value : Second.Value, Roll), Places,
			First.bCommas || Second.bCommas);
		if (First.bPercent || Second.bPercent)
		{
			Out.AppendChar(TEXT('%'));
		}
		Copied = Second.End;
	}
	Out += Effect.Mid(Copied);
	return Out;
}

// THE WHOLE-NUMBER VALUE RULE OF ISSUE #2883, approved by the owner on
// 2026-11-24 ("Approve as written (Recommended)"). Until then an affix was its
// TopValue times a geometric tier fraction, a 75%-to-100% band, divided back to
// +0, upgraded and remapped onto a stated Floor, all in floating point and never
// rounded, so a tool tip showed figures such as 3.4 that the character sheet
// then truncated. Now every affix and implicit is a whole number at every tier,
// roll and upgrade level, and the two ladders the owner kept (tier and upgrade;
// see "Ease both levers, delete neither", issue #1179) both still apply:
//   rule 3  each end of a tier's +0 range is upgraded on its own, exactly, in
//           integers: (AtZero x (100 + 15 x Level) x Hands + 50) / 100;
//   rule 4  the roll picks one of the whole numbers between the two ends,
//           each equally likely.
// The simulation's copy is sim/cataclysm_sim/affixes.py (`upgraded`,
// `roll_value`), and tools/tests/test_affix_values_port.py holds the two equal.

// Two constants written independently, so this compares two facts rather than
// one number with itself.
static_assert(UCataclysmItemValues::GearLevelStepHundredths
				  == static_cast<int32>(UCataclysmItemValues::GearLevelFactor * 100.0f + 0.5f),
			  "GearLevelStepHundredths must be GearLevelFactor in whole hundredths");

int32 UCataclysmItemValues::UpgradedWhole(int32 AtZero, int32 GearLevel,
										  bool bTwoHanded)
{
	const int32 Level = FMath::Clamp(GearLevel, 0, MaxGearLevel);
	const int32 Hands = bTwoHanded ? static_cast<int32>(TwoHandedMultiplier) : 1;
	const int32 Scaled = AtZero * (100 + GearLevelStepHundredths * Level) * Hands;
	// A half goes up. Every value here is at least zero, so integer division
	// after adding 50 is rounding half up.
	return (Scaled + 50) / 100;
}

int32 UCataclysmItemValues::WholeAtRoll(int32 Low, int32 High, float Roll)
{
	if (High <= Low)
	{
		return Low;
	}
	const int32 Steps = High - Low;
	const double Clamped = FMath::Clamp(static_cast<double>(Roll), 0.0, 1.0);
	const int32 Index = FMath::Min(
		FMath::FloorToInt32(Clamped * static_cast<double>(Steps + 1)), Steps);
	return Low + Index;
}

void UCataclysmItemValues::AffixRangeAt(const FCataclysmAffixRow& Affix, int32 Tier,
										int32 GearLevel, bool bTwoHanded,
										int32& OutLow, int32& OutHigh)
{
	int32 LowAtZero = 0;
	int32 HighAtZero = 0;
	Affix.TierRange(Tier, LowAtZero, HighAtZero);
	OutLow = UpgradedWhole(LowAtZero, GearLevel, bTwoHanded);
	OutHigh = UpgradedWhole(HighAtZero, GearLevel, bTwoHanded);
}

float UCataclysmItemValues::AffixValue(const FCataclysmAffixRow& Affix, int32 Tier,
									   float Roll, int32 GearLevel, bool bTwoHanded)
{
	// A HYBRID ROW STATES NO RANGE OF ITS OWN; its halves are other rows and
	// go through HybridHalfValue. All fourteen of its tier cells are zero.
	int32 Low = 0;
	int32 High = 0;
	AffixRangeAt(Affix, Tier, GearLevel, bTwoHanded, Low, High);
	return static_cast<float>(WholeAtRoll(Low, High, Roll));
}

float UCataclysmItemValues::HybridHalfValue(const FCataclysmAffixRow& Half,
											int32 HybridTier, float Roll,
											int32 GearLevel, bool bTwoHanded)
{
	const int32 Tier = FMath::Clamp(HybridTier, LowestHybridTier, MaxAffixTier);
	return AffixValue(Half, Tier - HybridTierDrop, Roll, GearLevel, bTwoHanded);
}

float UCataclysmItemValues::ImplicitValueAtLevel(int32 AtZero, int32 GearLevel,
												 bool bTwoHanded)
{
	// No tier and no roll: an implicit is what the base IS rather than what a
	// particular drop happened to get. Upgrade level and the two-handed
	// multiplier still apply, by the same rule as an affix's range.
	return static_cast<float>(UpgradedWhole(AtZero, GearLevel, bTwoHanded));
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

const TArray<FName>& UCataclysmItemModifiers::DamageTypeNames()
{
	// The order sim/cataclysm_sim/character.py lists them in.
	static const TArray<FName> Names = {
		TEXT("War"), TEXT("Demonic"), TEXT("Death"), TEXT("Pestilence"),
		TEXT("Famine"), TEXT("Celestial"), TEXT("Chaos"), TEXT("Void"),
	};
	return Names;
}

FName UCataclysmItemModifiers::ResistanceStatFor(FName DamageType)
{
	return FName(*FString::Printf(TEXT("resistance_%s"),
								  *DamageType.ToString().ToLower()));
}

TMap<FName, TArray<FCataclysmStatModifier>> UCataclysmItemModifiers::ModifiersFor(
	const FCataclysmItem& Item,
	const UDataTable* BaseTable,
	const UDataTable* AffixTable)
{
	TMap<FName, TArray<FCataclysmStatModifier>> Out;
	AccumulateInto(Out, Item, BaseTable, AffixTable);
	return Out;
}

void UCataclysmItemModifiers::AccumulateInto(
	TMap<FName, TArray<FCataclysmStatModifier>>& Totals,
	const FCataclysmItem& Item,
	const UDataTable* BaseTable,
	const UDataTable* AffixTable)
{
	if (!BaseTable || !AffixTable)
	{
		return;
	}

	const FCataclysmItemBaseRow* Base =
		BaseTable->FindRow<FCataclysmItemBaseRow>(Item.Base, TEXT("ModifiersFor"),
												  /*bWarnIfMissing=*/false);
	if (!Base)
	{
		UE_LOG(LogCataclysm, Warning,
			   TEXT("Item names base %s, which is not in the base table"),
			   *Item.Base.ToString());
		return;
	}

	const bool bTwoHanded = Base->Hands == 2;

	auto Add = [&Totals](FName Stat, ECataclysmStatBucket Bucket,
						 ECataclysmModifierSource Source, float Value)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = Bucket;
		Modifier.Source = Source;
		Modifier.Value = Value;
		Totals.FindOrAdd(Stat).Add(Modifier);
	};

	// The base's implicits. What the item IS, before any roll.
	const FString ImplicitStats[] = { Base->Implicit1Stat, Base->Implicit2Stat };
	const FString ImplicitKinds[] = { Base->Implicit1Kind, Base->Implicit2Kind };
	const int32 ImplicitsAtZero[] = { Base->Implicit1AtZero, Base->Implicit2AtZero };

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
		Add(FName(*ImplicitStats[Index]), Bucket,
			ECataclysmModifierSource::GearImplicit,
			UCataclysmItemValues::ImplicitValueAtLevel(ImplicitsAtZero[Index],
													   Item.GearLevel, bTwoHanded));
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

		// A HYBRID GRANTS TWO STATS AND HAS NONE OF ITS OWN, so it has to be
		// resolved before anything below reads Stat, ValueKind or TopValue. All
		// three of those columns are empty or zero on a hybrid row: it names its
		// two halves in HybridPart1 and HybridPart2 instead.
		//
		// UNTIL ISSUE #847 THIS FELL THROUGH TO THE CHECK BELOW, which refused
		// the empty ValueKind and skipped the affix in silence, so a hybrid on a
		// piece of gear granted the player NOTHING AT ALL. It was reported from
		// play as an affix showing a value of zero, which is what the tool tip
		// made of the same empty row.
		if (Affix->AffixKind.Equals(TEXT("Hybrid"), ESearchCase::IgnoreCase))
		{
			for (const FString& PartName : { Affix->HybridPart1, Affix->HybridPart2 })
			{
				const FCataclysmAffixRow* Part = AffixNamed(AffixTable, PartName);
				if (!Part)
				{
					if (!PartName.IsEmpty())
					{
						UE_LOG(LogCataclysm, Warning,
							TEXT("The hybrid affix '%s' names a part '%s' that "
								 "is not in the Affixes table, so half of it "
								 "grants nothing."),
							*Affix->AffixName, *PartName);
					}
					continue;
				}

				ECataclysmStatBucket PartBucket;
				if (!BucketFromKind(Part->ValueKind, PartBucket)
					|| Part->Stat.IsEmpty())
				{
					UE_LOG(LogCataclysm, Warning,
						TEXT("The hybrid affix '%s' names '%s' as a half, and "
							 "that row grants no stat, so half of it grants "
							 "nothing."),
						*Affix->AffixName, *PartName);
					continue;
				}

				// EACH HALF AS ITS OWN AFFIX TWO TIERS DOWN, since issue #2883,
				// which is the design's 70% share read off the half's own
				// whole-number ladder. See UCataclysmItemValues::HybridTierDrop.
				//
				// THE HALF'S OWN RANGES, NOT THE HYBRID'S. A hybrid is defined
				// in terms of the affixes it combines rather than by copying
				// their numbers, so it cannot drift from them.
				Add(FName(*Part->Stat), PartBucket,
					ECataclysmModifierSource::GearAffix,
					UCataclysmItemValues::HybridHalfValue(
						*Part, Rolled.Tier, Rolled.Roll, Item.GearLevel, bTwoHanded));
			}
			continue;
		}

		ECataclysmStatBucket Bucket;
		if (!BucketFromKind(Affix->ValueKind, Bucket))
		{
			// AN AILMENT AFFIX GRANTS A CHANCE, AND SINCE ISSUE #899 THE CHANCE IS
			// A STAT. This said the chance was "applied where the hit is
			// resolved", and nothing applied it there or anywhere else, so eleven
			// affixes did nothing. It is a flat modifier on the stat
			// `UCataclysmAilments` keeps that ailment's chance under, and
			// `UCataclysmAilments::RollOnLandedBlow` rolls it when a blow lands.
			//
			// DOUBLED ON A TWO-HANDED WEAPON LIKE EVERY OTHER AFFIX. The design
			// document: "A two-handed weapon multiplies both its implicit values
			// and every affix rolled on it by 2." The tooltip already printed the
			// doubled figure. The simulation does not double it, which is #1575.
			if (Affix->AffixKind.Equals(TEXT("Ailment"), ESearchCase::IgnoreCase))
			{
				if (const FCataclysmAilmentKind* Kind =
						UCataclysmAilments::KindNamed(Affix->Ailment))
				{
					Add(FName(Kind->Stat), ECataclysmStatBucket::Flat,
						ECataclysmModifierSource::GearAffix,
						UCataclysmItemValues::AffixValue(
							*Affix, Rolled.Tier, Rolled.Roll, Item.GearLevel,
							bTwoHanded));
				}
				else
				{
					UE_LOG(LogCataclysm, Warning,
						TEXT("The ailment affix %s names '%s', which is no ailment "
							 "the game knows, so it grants nothing."),
						*Affix->AffixName, *Affix->Ailment);
				}
			}
			continue;
		}

		const float Value = UCataclysmItemValues::AffixValue(
			*Affix, Rolled.Tier, Rolled.Roll, Item.GearLevel, bTwoHanded);

		if (Affix->AffixKind == TEXT("Resistance"))
		{
			// The affix says how many damage types it covers; the item says
			// which. A family covering all eight has no choice to make, so an
			// empty list means all of them.
			TArray<FName> Types = Rolled.DamageTypes;
			if (Types.Num() == 0 && Affix->Breadth == DamageTypeNames().Num())
			{
				Types = DamageTypeNames();
			}
			if (Types.Num() != Affix->Breadth)
			{
				UE_LOG(LogCataclysm, Warning,
					   TEXT("%s covers %d damage types but the item names %d"),
					   *Rolled.Affix.ToString(), Affix->Breadth, Types.Num());
				continue;
			}
			for (const FName& Type : Types)
			{
				Add(ResistanceStatFor(Type), Bucket,
					ECataclysmModifierSource::GearAffix, Value);
			}
			continue;
		}

		if (Affix->Stat.IsEmpty())
		{
			UE_LOG(LogCataclysm, Warning,
				   TEXT("%s grants no stat and is not a resistance family"),
				   *Rolled.Affix.ToString());
			continue;
		}
		Add(FName(*Affix->Stat), Bucket, ECataclysmModifierSource::GearAffix, Value);
	}

	// AND THE GEMS IN ITS SOCKETS, since issue #46 (slice G3). Only worn pieces are passed here
	// (`UCataclysmEquipmentComponent` reads its worn slots), so a gem in a carried piece does nothing. Once per gem,
	// not doubled on a two-handed weapon, ruled 2026-11-21. Since slice G10 the piece's own enchantments are read there
	// too: whether its sockets take any gem, and how effective its gems are (ruled 2026-11-22).
	UCataclysmGems::AddSocketedGems(Item, UCataclysmGems::LoadGeneratedTable(), LoadEnchantmentEffectTable(), Totals);
}

// ---------------------------------------------------------------------------
// The weapon's own damage, looked up by weapon type
// ---------------------------------------------------------------------------

const FCataclysmAffixRow* UCataclysmItemModifiers::AffixNamed(
	const UDataTable* AffixTable, const FString& AffixName)
{
	if (!AffixTable || AffixName.IsEmpty())
	{
		return nullptr;
	}

	const FCataclysmAffixRow* Found = nullptr;
	AffixTable->ForeachRow<FCataclysmAffixRow>(
		TEXT("UCataclysmItemModifiers::AffixNamed"),
		[&](const FName&, const FCataclysmAffixRow& Row)
		{
			if (!Found && Row.AffixName.Equals(AffixName, ESearchCase::IgnoreCase))
			{
				Found = &Row;
			}
		});
	return Found;
}

const TCHAR* UCataclysmItemModifiers::AttackDamageStat = TEXT("attack_damage");

const TCHAR* UCataclysmItemModifiers::AttackSpeedStat = TEXT("attack_speed");

const TCHAR* UCataclysmItemModifiers::CritChanceStat = TEXT("crit_chance");

const TCHAR* UCataclysmItemModifiers::AreaOfEffectStat = TEXT("area_of_effect");

const TCHAR* UCataclysmItemModifiers::BaseTableAssetPath =
	TEXT("/Game/Data/DT_ItemBases.DT_ItemBases");

const UDataTable* UCataclysmItemModifiers::LoadBaseTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, BaseTableAssetPath);
	if (!Table)
	{
		// Loudly, and naming both scripts, because the two failures look the
		// same from here: the workbook never produced the CSV, or the CSV was
		// never imported as an asset.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load %s. It is produced by "
				 "tools/generate_datatable_assets.py from game/Data/"
				 "ItemBases.csv, which tools/generate_datatables.py produces "
				 "from the Item Bases sheet of "
				 "docs/All_Things_Cataclysm.xlsx."), BaseTableAssetPath);
	}
	return Table;
}

const TCHAR* UCataclysmItemModifiers::EnchantmentEffectTableAssetPath =
	TEXT("/Game/Data/DT_EnchantmentEffects.DT_EnchantmentEffects");

const UDataTable* UCataclysmItemModifiers::LoadEnchantmentEffectTable()
{
	const UDataTable* Table =
		LoadObject<UDataTable>(nullptr, EnchantmentEffectTableAssetPath);
	if (!Table)
	{
		// Loudly, and naming both scripts, for the reason LoadBaseTable gives.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load %s. It is produced by "
				 "tools/generate_datatable_assets.py from game/Data/"
				 "EnchantmentEffects.csv, which tools/generate_datatables.py "
				 "produces from the Enchantment Effects sheet of "
				 "docs/All_Things_Cataclysm.xlsx."),
			EnchantmentEffectTableAssetPath);
	}
	return Table;
}

namespace
{
	/**
	 * Whether a row of an enchantment table belongs to a set.
	 *
	 * BY ITS TYPE, rather than by its set identifier.
	 * `UCataclysmDropRoll::EnchantmentSetId` answers 0 for a set row whose
	 * identifier cannot be read, and that row must still not be treated as an
	 * ordinary enchantment here. The type is also the column the generator
	 * checks when it refuses an effect on a set row.
	 */
	bool IsSetEnchantmentRow(const UDataTable* Table, FName Row)
	{
		const FCataclysmEnchantmentRow* Found = Table
			? Table->FindRow<FCataclysmEnchantmentRow>(
				  Row, TEXT("IsSetEnchantmentRow"), /*bWarnIfMissing=*/false)
			: nullptr;
		return Found
			&& Found->EnchantmentType.Equals(TEXT("Set"), ESearchCase::IgnoreCase);
	}

	/**
	 * Which named set a benefit row makes an item a piece of, or 0 for none.
	 *
	 * THE BENEFIT IS WHAT MARKS A PIECE. A drop that makes an item a piece of a
	 * set records that set's lowest threshold row as the benefit and the set's
	 * own drawback beside it, so the benefit alone says which set the piece
	 * belongs to. Issue #45.
	 */
	int32 SetOfBenefit(const UDataTable* PositiveTable, FName Row)
	{
		const FCataclysmEnchantmentRow* Found = PositiveTable
			? PositiveTable->FindRow<FCataclysmEnchantmentRow>(
				  Row, TEXT("SetOfBenefit"), /*bWarnIfMissing=*/false)
			: nullptr;
		return Found ? UCataclysmDropRoll::EnchantmentSetId(*Found) : 0;
	}

	/**
	 * The roll every set row is granted at.
	 *
	 * A SET ROW STATES ONE NUMBER, so the roll cannot change what it grants:
	 * `EnchantmentValue` returns that number at every roll. No item records a
	 * roll for a set's 6-piece or 10-piece row, because a piece records only the
	 * lowest threshold row, which is why the generator refuses a range on a set
	 * row rather than leaving the question open.
	 */
	constexpr float SetRowRoll = 1.0f;

	/**
	 * The modifier one effect row grants, or false when this build cannot read
	 * the row's condition or scale.
	 *
	 * A ROW THIS BUILD CANNOT READ GRANTS NOTHING, on either side of an
	 * enchantment. Applying it with the condition dropped would make a benefit
	 * hold all the time or a drawback's condition vanish. The generator refuses
	 * every such name, so only a hand-edited CSV can reach this, and granting
	 * nothing is the answer that errs in neither direction.
	 */
	/**
	 * Whether a row's condition is judged where its stack is granted rather than where its stat is read: an event
	 * condition, which `UCataclysmStatPipeline::IsJudgedAtItsEvent` names, and the rarity of the creature a kill slew,
	 * for a stack a kill grants.
	 *
	 * THE KILL'S RARITY IS THE VICTIM'S, AND ONLY THE GRANT KNOWS THE VICTIM. The Soul Collector's Trophy of the Strong,
	 * "Killing an Elite or stronger creature gives you 20% more damage for 10 seconds" (issue #2951, the keystone rework
	 * of 2026-11-27). On the stat the condition would be read against whatever enemy is struck next, so a Common kill
	 * would grant the bonus and an Elite would collect it. `UCataclysmAbilitySystemComponent::PoolActionAllowed` judges a
	 * grant's condition against the event's other character, the creature slain for `kill`. No stack row carried a
	 * rarity condition before this, so no row's meaning changed.
	 */
	bool IsJudgedWhereItsStackIsGranted(ECataclysmStatCondition Condition, const FCataclysmEnchantmentEffectRow& Effect)
	{
		if (UCataclysmStatPipeline::IsJudgedAtItsEvent(Condition))
		{
			return true;
		}
		return (Condition == ECataclysmStatCondition::TargetRarityAtLeast
				|| Condition == ECataclysmStatCondition::TargetRarityIs)
			&& Effect.Scale.Equals(TEXT("own_stacks"), ESearchCase::IgnoreCase)
			&& Effect.ActionEvent.Equals(TEXT("kill"), ESearchCase::IgnoreCase);
	}

	bool EnchantmentModifierFor(const FCataclysmEnchantmentEffectRow& Effect,
								float Roll, FCataclysmStatModifier& Out)
	{
		Out = FCataclysmStatModifier();
		Out.Source = ECataclysmModifierSource::Enchantment;

		// WHERE THIS ITEM'S ROLL LANDS INSIDE THE ROW'S RANGE, which for a row
		// stating one number is that number whatever the roll. The hover text
		// asks the same function with the same two ends, so the character gets
		// the number the player reads.
		Out.Value = UCataclysmItemValues::EnchantmentValue(Effect.ValueLow,
														   Effect.ValueHigh, Roll);

		if (Effect.ValueKind.Equals(TEXT("more"), ESearchCase::IgnoreCase))
		{
			Out.Bucket = ECataclysmStatBucket::More;
		}
		else if (Effect.ValueKind.Equals(TEXT("flat"), ESearchCase::IgnoreCase))
		{
			Out.Bucket = ECataclysmStatBucket::Flat;
		}
		// A ROW SAYING THE STAT IS GONE. Issue #1791. The value above is the 1
		// the row states, and nothing reads it.
		else if (Effect.ValueKind.Equals(TEXT("removed"), ESearchCase::IgnoreCase))
		{
			Out.Bucket = ECataclysmStatBucket::Removed;
		}
		else
		{
			Out.Bucket = ECataclysmStatBucket::Increased;
		}

		TArray<FString> Tags;
		Effect.RequiredTags.ParseIntoArray(Tags, TEXT(","), /*InCullEmpty=*/true);
		for (FString& Tag : Tags)
		{
			Tag.TrimStartAndEndInline();
			if (!Tag.IsEmpty())
			{
				Out.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(
					FName(*Tag), /*ErrorIfNotFound=*/false));
			}
		}

		if (!Effect.Condition.IsEmpty())
		{
			if (!UCataclysmStatPipeline::ConditionNamed(Effect.Condition,
														Out.Condition))
			{
				return false;
			}
			// ROLLED WITH THE VALUE WHEN THE ROW STATES TWO THRESHOLDS. Issue #1833,
			// ruled 2026-09-30.
			Out.ConditionValue = UCataclysmItemModifiers::RolledConditionValue(Effect, Roll);
			// AN EVENT CONDITION IS THE GRANT'S, NOT THE STAT'S: Avenging Dead's "when a hit takes more than 5%", the
			// Death trees' slice C6d2 (2026-11-25). The fact it reads exists only while the event is raised, so on the
			// stat it would refuse at every read. The grant below takes it. SO IS A KILL STACK'S RARITY, which is the
			// slain creature's (issue #2951): see `IsJudgedWhereItsStackIsGranted`.
			if (IsJudgedWhereItsStackIsGranted(Out.Condition, Effect))
			{
				Out.Condition = ECataclysmStatCondition::Always;
				Out.ConditionValue = 0.0f;
			}
		}

		// AND A SECOND CONDITION, WHICH MUST HOLD AS WELL. Issue #1833, ruled
		// 2026-09-30. Its threshold does not roll; no sentence names two ranges.
		if (!Effect.Condition2.IsEmpty())
		{
			if (!UCataclysmStatPipeline::ConditionNamed(Effect.Condition2,
														Out.Condition2))
			{
				return false;
			}
			Out.ConditionValue2 = Effect.ConditionValue2;
		}

		if (!Effect.Scale.IsEmpty())
		{
			if (!UCataclysmStatPipeline::ScaleNamed(Effect.Scale, Out.Scale))
			{
				return false;
			}
			// ROLLED WITH THE VALUE WHEN THE ROW STATES TWO STEPS. Issue #1833.
			Out.ScaleStep = UCataclysmItemModifiers::RolledScaleStep(Effect, Roll);
			// AND ITS CAP. Issue #1815.
			Out.ScaleMaxSteps = Effect.ScaleMaxSteps;
			// AND, FOR A ROW'S OWN STACKS, WHOSE THEY ARE. Issue #1833. The same
			// key for a row's hits in a row, which are counted the same way.
			if (Out.Scale == ECataclysmStatScale::PerOwnStack
				|| Out.Scale == ECataclysmStatScale::PerConsecutiveHit)
			{
				Out.StackKey = UCataclysmItemModifiers::OwnStackKeyFor(Effect);
			}
			// AND, FOR A ROW COUNTING ENEMIES ONLY WHILE ANOTHER ROW'S STACK IS
			// HELD, THAT ROW'S KEY AND THE REACH. Ruled by the project owner on
			// 2026-10-17. The key of an `own_stacks` row of the same enchantment
			// on the same stat, which the generator requires beside this one;
			// no grant is made here for this row, so it reads that row's window.
			if (Out.Scale == ECataclysmStatScale::PerEnemyInReachWhileOwnStackHeld)
			{
				Out.StackKey = UCataclysmItemModifiers::OwnStackKeyFor(Effect);
				Out.ReachMetres = UCataclysmItemModifiers::OwnStackWindowReachMetres;
			}
			// AND ITS OFFSET. Issue #1686.
			Out.ScaleOffset = Effect.ScaleOffset;
		}

		return true;
	}
}

FName UCataclysmItemModifiers::OwnStackKeyFor(const FCataclysmEnchantmentEffectRow& Effect)
{
	return FName(*FString::Printf(TEXT("%s:%s"), *Effect.Enchantment, *Effect.Stat));
}

float UCataclysmItemModifiers::RolledScaleStep(const FCataclysmEnchantmentEffectRow& Effect,
											   float Roll)
{
	// RANGED WHEN THE SECOND END IS STATED AND DIFFERS, NOT ONLY WHEN IT IS
	// HIGHER. A range its sentence marks as rolling down arrives with its ends
	// exchanged, ruled 2026-10-05, and nought still means no second end.
	return Effect.ScaleStepHigh > 0.0f && Effect.ScaleStepHigh != Effect.ScaleStep
		? UCataclysmItemValues::EnchantmentValue(Effect.ScaleStep, Effect.ScaleStepHigh, Roll)
		: Effect.ScaleStep;
}

float UCataclysmItemModifiers::RolledStackSeconds(const FCataclysmEnchantmentEffectRow& Effect,
												  float Roll)
{
	return Effect.StackSecondsHigh > 0.0f && Effect.StackSecondsHigh != Effect.StackSeconds
		? UCataclysmItemValues::EnchantmentValue(Effect.StackSeconds, Effect.StackSecondsHigh, Roll)
		: Effect.StackSeconds;
}

float UCataclysmItemModifiers::RolledConditionValue(const FCataclysmEnchantmentEffectRow& Effect,
													float Roll)
{
	return Effect.ConditionValueHigh > 0.0f && Effect.ConditionValueHigh != Effect.ConditionValue
		? UCataclysmItemValues::EnchantmentValue(Effect.ConditionValue, Effect.ConditionValueHigh, Roll)
		: Effect.ConditionValue;
}

bool UCataclysmItemModifiers::KillCrossesAStep(const FCataclysmItem& Item, int32 KillsBefore,
											   const UDataTable* EffectTable)
{
	if (!EffectTable)
	{
		return false;
	}
	const auto Crosses = [KillsBefore, EffectTable](FName Enchantment, float Roll)
	{
		if (Enchantment.IsNone())
		{
			return false;
		}
		const FString Named = Enchantment.ToString();
		for (const TPair<FName, uint8*>& Row : EffectTable->GetRowMap())
		{
			const FCataclysmEnchantmentEffectRow* Effect =
				reinterpret_cast<const FCataclysmEnchantmentEffectRow*>(Row.Value);
			if (!Effect || Effect->Enchantment != Named
				|| !Effect->Scale.Equals(TEXT("weapon_kills"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			const float Step = RolledScaleStep(*Effect, Roll);
			if (Step > 0.0f
				&& FMath::FloorToInt32(static_cast<float>(KillsBefore) / Step)
					!= FMath::FloorToInt32(static_cast<float>(KillsBefore + 1) / Step))
			{
				return true;
			}
		}
		return false;
	};
	for (const FCataclysmRolledEnchantment& Rolled : Item.Enchantments)
	{
		if (Crosses(Rolled.Positive, Rolled.PositiveRoll)
			|| Crosses(Rolled.Negative, Rolled.NegativeRoll))
		{
			return true;
		}
	}
	return false;
}

int32 UCataclysmItemModifiers::AccumulateEnchantmentsInto(
	TMap<FName, TArray<FCataclysmStatModifier>>& Totals,
	const TArray<FCataclysmItem>& Worn, const UDataTable* EffectTable,
	const UDataTable* PositiveTable, const UDataTable* NegativeTable,
	TArray<FCataclysmPoolAction>* Actions,
	const TArray<FCataclysmEnchantmentEffectRow>* ExtraRows)
{
	// ALL THREE TABLES OR NOTHING. The loaders report a missing table loudly, so
	// it is not reported a second time here.
	if (!EffectTable || !PositiveTable || !NegativeTable)
	{
		return 0;
	}

	// EVERY EFFECT ROW GROUPED BY THE ENCHANTMENT IT IS ABOUT, READ ONCE, and
	// sorted by stat so one enchantment's rows come back in the same order every
	// time. A DataTable keeps its rows in a map, and a map's order is not part of
	// the data. `UCataclysmPassiveTree::EffectsByNode` does the same for a node.
	TMap<FName, TArray<const FCataclysmEnchantmentEffectRow*>> EffectsFor;
	for (const TPair<FName, uint8*>& Row : EffectTable->GetRowMap())
	{
		const FCataclysmEnchantmentEffectRow* Effect =
			reinterpret_cast<const FCataclysmEnchantmentEffectRow*>(Row.Value);
		if (Effect && !Effect->Enchantment.IsEmpty())
		{
			EffectsFor.FindOrAdd(FName(*Effect->Enchantment)).Add(Effect);
		}
	}
	for (TPair<FName, TArray<const FCataclysmEnchantmentEffectRow*>>& Pair :
		 EffectsFor)
	{
		Pair.Value.Sort([](const FCataclysmEnchantmentEffectRow& Left,
						   const FCataclysmEnchantmentEffectRow& Right)
		{
			return Left.Stat < Right.Stat;
		});
	}

	int32 Added = 0;
	// `ItemKills` IS THE COUNT OF THE ITEM THE ENCHANTMENT IS ON, which only a
	// `weapon_kills` row reads. Issue #1833, the kill counter.
	//
	// A LIST OF ROWS GRANTED AT ONE ROLL. `Grant` below hands it one enchantment's
	// rows, and the rows a held random buff wears come to it at the end. Issue
	// #2538, batch 2.
	// A DRAWBACK'S ACTIONS ARE MARKED AS ONE, so Touch of Nothing, which refuses a buff the wearer would gain,
	// does not refuse a curse's timed stack. Issue #1722's Touch of Nothing, ruled 2026-10-30.
	auto GrantRows = [&Totals, &Added, Actions](
		const TArray<const FCataclysmEnchantmentEffectRow*>& Effects, float Roll, int32 ItemKills,
		bool bFromADrawback)
	{
		for (const FCataclysmEnchantmentEffectRow* Effect : Effects)
		{
			// A ROW SCALED BY ITS OWN STACKS ALSO NEEDS SOMETHING TO GRANT THEM:
			// an action on its event that gains a stack instead of moving a pool.
			// Issue #1833. Its modifier is added below, like any stat row's.
			//
			// AND SO DOES A ROW COUNTING HITS IN A ROW ON ONE ENEMY, through the
			// same lines, so the two cannot be capped differently. Issue #1833,
			// phase 2.
			const bool bConsecutive = Effect->Scale.Equals(
				TEXT("consecutive_hits"), ESearchCase::IgnoreCase);
			if (Actions && Effect->Action.IsEmpty()
				&& (bConsecutive
					|| Effect->Scale.Equals(TEXT("own_stacks"), ESearchCase::IgnoreCase)))
			{
				FCataclysmPoolAction Stack;
				Stack.Event = FName(*Effect->ActionEvent);
				Stack.StackKey = UCataclysmItemModifiers::OwnStackKeyFor(*Effect);
				Stack.StackSeconds = UCataclysmItemModifiers::RolledStackSeconds(*Effect, Roll);
				Stack.StackCap = Effect->ScaleMaxSteps;
				// AND AN OWN-STACK ROW WITH NO CAP HAS STACKS THAT LAPSE ALONE. Ruled 2026-10-21 (traps layer 3, M4 (a)):
				// "Each trap triggered in the last 10 seconds" names no cap, and each stack lasts its own 10 s.
				Stack.bOwnStacksLapseAlone = !bConsecutive && Effect->ScaleMaxSteps <= 0;
				// AND ITS CLOCK, WHEN IT GRANTS ON ONE. Issue #1833, timed grants.
				Stack.EverySeconds = Effect->EverySeconds;
				// AND THE THRESHOLD ITS EVENT CROSSES, AND A COOLDOWN ITS SENTENCE
				// STATES. Issue #1833 group D part 2, ruled 2026-09-30: "When your
				// health falls below 25%, you enter a 'Demonic Rage' ... (5 minute
				// cd)". The generator allows an explicit cooldown on an own stack
				// and writes none by default.
				Stack.EventValue = Effect->EventValue;
				Stack.TriggerCooldownSeconds = Effect->TriggerCooldown;
				// AND ITS CONDITION, WHEN THE ROW'S IS JUDGED AT ITS EVENT: Avenging Dead, the Death trees' slice C6d2.
				// `PoolActionAllowed` judges it as the event is raised; the stat above was given none.
				ECataclysmStatCondition AtTheEvent = ECataclysmStatCondition::Always;
				if (!Effect->Condition.IsEmpty() && UCataclysmStatPipeline::ConditionNamed(Effect->Condition, AtTheEvent)
					&& IsJudgedWhereItsStackIsGranted(AtTheEvent, *Effect))
				{
					Stack.Condition = AtTheEvent;
					Stack.ConditionValue = Effect->ConditionValue;
				}
				Stack.TriggerKey = FName(*FString::Printf(TEXT("%s:%s"),
					*Stack.StackKey.ToString(), *Effect->ActionEvent));
				// A COUNT OF HITS IN A ROW IS SCOPED BY THE ROW'S TAGS, ruled
				// 2026-09-24: "each consecutive melee hit" counts melee hits
				// only. An own stack's tags scope its stat and not its grant, the
				// phase 1 judgement, which this leaves as it was.
				Stack.bConsecutiveHits = bConsecutive;
				if (bConsecutive)
				{
					TArray<FString> CountedTags;
					Effect->RequiredTags.ParseIntoArray(
						CountedTags, TEXT(","), /*InCullEmpty=*/true);
					for (FString& Tag : CountedTags)
					{
						Tag.TrimStartAndEndInline();
						if (!Tag.IsEmpty())
						{
							Stack.RequiredTags.AddTag(
								FGameplayTag::RequestGameplayTag(
									FName(*Tag), /*ErrorIfNotFound=*/false));
						}
					}
				}
				Stack.bFromADrawback = bFromADrawback;
				Actions->Add(Stack);
			}

			// A ROW THAT MOVES A POOL LEAVES BY A DIFFERENT DOOR. It is not a stat
			// modifier and cannot become one: the pipeline reads a modifier when
			// something asks for a stat, and a pool moves at a moment instead.
			//
			// THE GENERATOR REFUSES A ROW NAMING BOTH A STAT AND AN ACTION, so this
			// test is exclusive rather than a preference between two things that
			// could both be set.
			if (!Effect->Action.IsEmpty())
			{
				if (Actions)
				{
					FCataclysmPoolAction Action;
					Action.Event = FName(*Effect->ActionEvent);
					Action.Pool = FName(*Effect->Action);
					Action.Percent = UCataclysmItemValues::EnchantmentValue(
						Effect->ValueLow, Effect->ValueHigh, Roll);

					// A CHARGE THE NEXT USE SPENDS, rather than a pool moved.
					// Issue #1833, phase 2. Keyed by the enchantment and the
					// action, the pair the generator never lets two rows share;
					// capped by Scale Max Steps, which the generator requires on
					// these rows.
					const bool bNextSkill = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::NextSkillDamageAction,
						ESearchCase::IgnoreCase);
					const bool bNextAttack = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::NextAttackDamageAction,
						ESearchCase::IgnoreCase);
					const bool bNextEffectiveness = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::NextSkillEffectivenessAction,
						ESearchCase::IgnoreCase);
					// AND ITS CLOCK, WHEN IT GRANTS ON ONE. Issue #1833, timed grants.
					Action.EverySeconds = Effect->EverySeconds;
					// A STACK PLACED ON THE OTHER CHARACTER OF THE EVENT. Issue
					// #1833, phase 2. Keyed by the enchantment and the action;
					// Stack Seconds is the window and Scale Max Steps the cap,
					// where nought is none ("stacking indefinitely").
					const bool bArmourRemovedNearby = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::EnemyArmorRemovedNearbyAction,
						ESearchCase::IgnoreCase);
					const bool bArmourRemoved = bArmourRemovedNearby || Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::EnemyArmorRemovedAction,
						ESearchCase::IgnoreCase);
					const bool bDamageRemoved = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::AttackerDamageRemovedAction,
						ESearchCase::IgnoreCase);
					// AND THORNWALL'S, THE THIRD KIND: retaliation taken. V3a, 2026-11-25.
					const bool bRetaliationTaken = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::AttackerRetaliationTakenAction,
						ESearchCase::IgnoreCase);
					// AND THE WAR TREES' TWO, engine slice E2 (2026-11-27): issue #964's mark and Rend's.
					const bool bDamageTakenRaised = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::EnemyDamageTakenAction, ESearchCase::IgnoreCase);
					const bool bDamageReductionRemoved = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::EnemyDamageReductionRemovedAction, ESearchCase::IgnoreCase);
					// AN "EVERY Nth" ROW, keyed by the enchantment and the action.
					// Issue #1833, phase 2. The action names what is counted, so
					// the row has no Action Event.
					const TPair<const TCHAR*, ECataclysmEveryNth> NthActions[] = {
						{UCataclysmAbilitySystemComponent::NthHitTakenDamageAction,
						 ECataclysmEveryNth::HitTaken},
						{UCataclysmAbilitySystemComponent::NthSpellManaCostAction,
						 ECataclysmEveryNth::SpellCast},
						{UCataclysmAbilitySystemComponent::NthAttackNoDamageAction,
						 ECataclysmEveryNth::Attack},
						// AND FLURRY'S, the War trees' engine slice E2 (2026-11-27).
						{UCataclysmAbilitySystemComponent::NthAttackMoreDamageAction,
						 ECataclysmEveryNth::AttackMore},
					};
					for (const TPair<const TCHAR*, ECataclysmEveryNth>& Nth : NthActions)
					{
						if (Effect->Action.Equals(Nth.Key, ESearchCase::IgnoreCase))
						{
							Action.NthKind = Nth.Value;
							Action.EveryNth = Effect->EveryNth;
							Action.NthKey = FName(*FString::Printf(
								TEXT("%s:%s"), *Effect->Enchantment, *Effect->Action));
						}
					}
					// A COOLDOWN RESET, keyed by the enchantment and the action.
					// Issue #1833, the cooldown reset action. The value is the
					// chance.
					const TPair<const TCHAR*, ECataclysmCooldownReset> ResetActions[] = {
						{UCataclysmAbilitySystemComponent::CooldownResetAllAction,
						 ECataclysmCooldownReset::All},
						{UCataclysmAbilitySystemComponent::CooldownResetOthersAction,
						 ECataclysmCooldownReset::Others},
						{UCataclysmAbilitySystemComponent::CooldownResetHeavyAction,
						 ECataclysmCooldownReset::Heavy},
						{UCataclysmAbilitySystemComponent::CooldownResetSpecialAction,
						 ECataclysmCooldownReset::Special},
						{UCataclysmAbilitySystemComponent::CooldownResetMovementAction,
						 ECataclysmCooldownReset::Movement},
						{UCataclysmAbilitySystemComponent::CooldownResetEventSkillAction,
						 ECataclysmCooldownReset::EventSkill},
					};
					for (const TPair<const TCHAR*, ECataclysmCooldownReset>& Reset : ResetActions)
					{
						if (Effect->Action.Equals(Reset.Key, ESearchCase::IgnoreCase))
						{
							Action.CooldownReset = Reset.Value;
							Action.ResetKey = FName(*FString::Printf(
								TEXT("%s:%s"), *Effect->Enchantment, *Effect->Action));
						}
					}
					if (bArmourRemoved || bDamageRemoved || bRetaliationTaken || bDamageTakenRaised
						|| bDamageReductionRemoved)
					{
						Action.PlacedKey = FName(*FString::Printf(
							TEXT("%s:%s"), *Effect->Enchantment, *Effect->Action));
						Action.StackSeconds = UCataclysmItemModifiers::RolledStackSeconds(*Effect, Roll);
						Action.StackCap = Effect->ScaleMaxSteps;
						Action.bPlacedCutsDamage = bDamageRemoved;
						Action.bPlacedRaisesRetaliationTaken = bRetaliationTaken;
						Action.bPlacedRaisesDamageTaken = bDamageTakenRaised;
						Action.bPlacedRemovesDamageReduction = bDamageReductionRemoved;
					}
					// A NEXT-SPELL COOLDOWN CHARGE, the fourth next-use kind. Issue
					// #1833, the cooldown reduction action.
					const bool bNextSpellCooldown = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::NextSpellCooldownReducedAction,
						ESearchCase::IgnoreCase);
					if (bNextSkill || bNextAttack || bNextEffectiveness || bNextSpellCooldown)
					{
						Action.NextUseKey = FName(*FString::Printf(
							TEXT("%s:%s"), *Effect->Enchantment, *Effect->Action));
						Action.NextUseCap = FMath::Max(1, Effect->ScaleMaxSteps);
						Action.bNextUseIsAttack = bNextAttack;
						Action.bNextUseIsEffectiveness = bNextEffectiveness;
						Action.bNextUseIsSpellCooldown = bNextSpellCooldown;
					}
					// A COOLDOWN REDUCTION, keyed like a reset. The value is seconds.
					if (Effect->Action.Equals(
							UCataclysmAbilitySystemComponent::CooldownReduceAllAction,
							ESearchCase::IgnoreCase))
					{
						Action.CooldownReduce = ECataclysmCooldownReset::All;
					}
					// OR A SHARE OF WHAT IS LEFT, every cooldown on the keys: Apex Predator (the War trees' engine slice
					// E2, 2026-11-27).
					else if (Effect->Action.Equals(
								 UCataclysmAbilitySystemComponent::CooldownReduceAllPercentAction,
								 ESearchCase::IgnoreCase))
					{
						Action.CooldownReduce = ECataclysmCooldownReset::All;
						Action.bCooldownReduceShare = true;
					}
					else if (Effect->Action.Equals(
								 UCataclysmAbilitySystemComponent::CooldownReduceHeavyAction,
								 ESearchCase::IgnoreCase))
					{
						Action.CooldownReduce = ECataclysmCooldownReset::Heavy;
					}
					else if (Effect->Action.Equals(
								 UCataclysmAbilitySystemComponent::CooldownReduceOthersAction,
								 ESearchCase::IgnoreCase))
					{
						// EVERY SKILL BUT THE EVENT'S OWN: Turn of the Wheel, the Death trees' slice C3b.
						Action.CooldownReduce = ECataclysmCooldownReset::Others;
					}
					// AND THE MOVEMENT SKILL'S, the Death trees' slice C6a (2026-11-25).
					else if (Effect->Action.Equals(
								 UCataclysmAbilitySystemComponent::CooldownReduceMovementAction,
								 ESearchCase::IgnoreCase))
					{
						Action.CooldownReduce = ECataclysmCooldownReset::Movement;
					}
					if (Action.CooldownReduce != ECataclysmCooldownReset::None)
					{
						Action.ResetKey = FName(*FString::Printf(
							TEXT("%s:%s"), *Effect->Enchantment, *Effect->Action));
					}
					// A CLEANSE. Ruled 2026-09-26. The row's value is not read: a cleanse has no size.
					Action.bCleanse = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::CleanseAction, ESearchCase::IgnoreCase);
					// A RANDOM DAMAGE OVER TIME ON THE OTHER CHARACTER. Issue #1833
					// group D, ruled 2026-09-30. The value is the chance.
					Action.bRandomDamageOverTime = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::ApplyRandomDotAction,
						ESearchCase::IgnoreCase);
					// AND THE ROW'S TRIGGER COOLDOWN, which the generator has
					// already defaulted. Keyed by the enchantment, the action and
					// the event, which no two rows share.
					Action.TriggerCooldownSeconds = Effect->TriggerCooldown;
					Action.TriggerKey = FName(*FString::Printf(TEXT("%s:%s:%s"),
						*Effect->Enchantment, *Effect->Action, *Effect->ActionEvent));
					// AND THE THRESHOLD ITS EVENT CROSSES, and whether it lowers
					// health to a share of its maximum. Issue #1833 group D part 2.
					Action.EventValue = Effect->EventValue;
					Action.bHealthCap = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::HealthCappedAtAction,
						ESearchCase::IgnoreCase);
					// AND WHETHER IT ACTS ON THE CHARACTERS NEAR THE WEARER. Issue
					// #1833 group D part 3. The value is a percentage of weapon
					// damage for a smite, and of each one's maximum health for a heal.
					if (Effect->Action.Equals(UCataclysmAbilitySystemComponent::SmiteNearbyAction,
											  ESearchCase::IgnoreCase))
					{
						Action.Nearby = ECataclysmNearbyAction::Smite;
					}
					else if (Effect->Action.Equals(
								 UCataclysmAbilitySystemComponent::HealNearbyEnemiesAction,
								 ESearchCase::IgnoreCase))
					{
						Action.Nearby = ECataclysmNearbyAction::HealEnemies;
					}
					// AND WHETHER IT DEALS THE REMAINING DAMAGE OF THE WEARER'S OWN
					// DAMAGE OVER TIME, and of which ailment. Issue #1833 group D
					// part 4. The value is a percentage of that remaining damage.
					if (Effect->Action.Equals(
							UCataclysmAbilitySystemComponent::RemainingDamageNearbyAction,
							ESearchCase::IgnoreCase))
					{
						Action.RemainingDamage = ECataclysmRemainingDamage::Nearby;
					}
					else if (Effect->Action.Equals(
								 UCataclysmAbilitySystemComponent::RemainingDamageTargetAction,
								 ESearchCase::IgnoreCase))
					{
						Action.RemainingDamage = ECataclysmRemainingDamage::Target;
					}
					if (const FCataclysmAilmentKind* Kind = Effect->Ailment.IsEmpty()
							? nullptr
							: UCataclysmAilments::KindNamed(Effect->Ailment))
					{
						Action.Ailment = FGameplayTag::RequestGameplayTag(
							FName(Kind->TagName), /*ErrorIfNotFound=*/false);
					}
					// AND WHETHER IT IS A RIDER ON THAT AILMENT, and of which of
					// the carrier's numbers. Issue #1833, ruled 2026-10-06. The
					// value is the percent.
					Action.Rider = UCataclysmAbilitySystemComponent::AilmentRiderNamed(
						Effect->Action);
					// AND WHETHER IT APPLIES A STATUS TO THE OTHER CHARACTER, and
					// which. Issue #1833 group E part 1. The value is the chance,
					// or the seconds for `apply_status_seconds`; the status is the
					// row's Ailment cell, an ailment or one of the two names
					// `UCataclysmAbilitySystemComponent` holds.
					if (Effect->Action.Equals(UCataclysmAbilitySystemComponent::ApplyStatusAction,
											  ESearchCase::IgnoreCase))
					{
						Action.ApplyStatus = ECataclysmApplyStatus::Chance;
					}
					else if (Effect->Action.Equals(
								 UCataclysmAbilitySystemComponent::ApplyStatusSecondsAction,
								 ESearchCase::IgnoreCase))
					{
						Action.ApplyStatus = ECataclysmApplyStatus::Seconds;
					}
					// AND THE THREE THAT LAY THE STATUS ON THE WEARER. Ruled 2026-10-06. The value is the
					// chance, the seconds, or the size of the one application.
					const TPair<const TCHAR*, ECataclysmApplyStatus> OnTheWearer[] = {
						{UCataclysmAbilitySystemComponent::ApplyStatusToSelfAction, ECataclysmApplyStatus::Chance},
						{UCataclysmAbilitySystemComponent::ApplyStatusToSelfSecondsAction, ECataclysmApplyStatus::Seconds},
						{UCataclysmAbilitySystemComponent::ApplyStatusToSelfSizedAction, ECataclysmApplyStatus::Sized},
					};
					for (const TPair<const TCHAR*, ECataclysmApplyStatus>& Self : OnTheWearer)
					{
						if (Effect->Action.Equals(Self.Key, ESearchCase::IgnoreCase))
						{
							Action.ApplyStatus = Self.Value;
							Action.bStatusOnTheWearer = true;
						}
					}
					if (Action.ApplyStatus != ECataclysmApplyStatus::None)
					{
						Action.StatusName = Effect->Ailment.TrimStartAndEnd();
					}
					// AND WHETHER IT OPENS THE WEARER'S NO-DAMAGE WINDOW. Issue
					// #1833 group E part 2. The value is the seconds.
					Action.bDamageImmunity = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::DamageImmunityAction,
						ESearchCase::IgnoreCase);
					// AND WHETHER IT MOVES THE WEARER TO A RANDOM REACHABLE POINT NEARBY. Ruled 2026-10-12. The
					// value is the furthest it may be moved, in metres.
					Action.bTeleportRandomly = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::TeleportRandomlyAction,
						ESearchCase::IgnoreCase);
					// AND WHETHER IT FIRES EVERY GADGET THE WEARER COMMANDS, ONCE, AT ONCE. Ruled 2026-10-19 (C1). The
					// value is 100 and is not read.
					Action.bGadgetsCastNow = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::GadgetsCastNowAction,
						ESearchCase::IgnoreCase);
					// AND THE TWO OF TRAPS LAYER 3, ruled 2026-10-21 (M3). A DAMAGE OVER TIME LAID ON THE OTHER CHARACTER
					// AT THE VALUE'S MULTIPLE OF ITS ORDINARY SIZE, "1-2 bleed stacks", named by the Ailment cell; and A
					// SLOW OF THE VALUE'S PER CENT FOR THE ROW'S STACK SECONDS, a new use of that column, as the stun near
					// the dying's.
					if (Effect->Action.Equals(UCataclysmAbilitySystemComponent::ApplyStatusSizedAction,
											  ESearchCase::IgnoreCase))
					{
						Action.ApplyStatus = ECataclysmApplyStatus::Sized;
						Action.StatusName = Effect->Ailment.TrimStartAndEnd();
					}
					Action.bSlowForSeconds = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::SlowForSecondsAction,
						ESearchCase::IgnoreCase);
					if (Action.bSlowForSeconds)
					{
						Action.StackSeconds = UCataclysmItemModifiers::RolledStackSeconds(*Effect, Roll);
					}
					// AND WHETHER AN ENEMY THE WEARER KILLS EXPLODES FOR A SHARE OF THE BLOW THAT KILLED IT. Ruled
					// 2026-10-21 (traps layer 4, F10 to F12). The value is the share, in per cent.
					Action.bExplodeVictimForKillingBlow = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::ExplodeVictimForKillingBlowAction,
						ESearchCase::IgnoreCase);
					// AND WHETHER IT ARMS, OR SETS OFF, THE WEARER'S OTHER TRAPS. Ruled 2026-10-22 (traps layer 5, H1 to
					// H11). The value is the radius in metres, nought at the wearer's death for every trap; a Scale Max
					// Steps of 1 is the nearest trap alone, and an empty one every trap.
					Action.bArmTrapsNearby = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::ArmTrapsNearbyAction, ESearchCase::IgnoreCase);
					Action.bSetOffTrapsNearby = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::TriggerTrapsNearbyAction, ESearchCase::IgnoreCase);
					Action.bNearestTrapOnly =
						(Action.bArmTrapsNearby || Action.bSetOffTrapsNearby) && Effect->ScaleMaxSteps == 1;
					// AND WHETHER THE WEARER'S NEAREST SKILL GADGET FIRES AT THE ENEMY THAT SET A TRAP OFF. Ruled 2026-10-22
					// (traps layer 6, J1 to J10). The value is a "more" in per cent on that one shot, 100 for none; the
					// Event Value, read above, the metres from the trap, nought for any distance; and Every Nth, above one,
					// the count of triggered traps it fires on, WITH NO WINDOW (J4), so the Stack Seconds are not read.
					Action.bGadgetFiresAtTrapEnemy = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::GadgetFiresAtTrapEnemyAction, ESearchCase::IgnoreCase);
					if (Action.bGadgetFiresAtTrapEnemy && Effect->EveryNth > 1)
					{
						Action.EveryNth = Effect->EveryNth;
						Action.CountWindowSeconds = 0.0f;
					}
					// AND WHETHER IT REPEATS THE SKILL JUST USED. Mechanism B2. The value is the chance. The share
					// is the row's Damage Share, and the whole of the damage when the row states none.
					Action.bRepeatSkill = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::RepeatSkillAction,
						ESearchCase::IgnoreCase);
					if (Action.bRepeatSkill && Effect->DamageShare > 0.0f)
					{
						Action.RepeatSharePercent = Effect->DamageShare;
					}
					// AND WHETHER IT GAINS A KEPT STACK ON A CLOCK AND, ON A USE OF A SKILL, SPENDS THEM ALL TO CAST THAT
					// SKILL AGAIN. Ruled 2026-10-12 under the owner's delegation. The value is the share of its damage
					// each extra cast deals, in per cent; Every Seconds is the period of the gain, set above; Scale Max
					// Steps is the most stacks held. KEYED BY THE ROW'S TRIGGER KEY, set above from the enchantment, the
					// action and the event, which no two rows share.
					if (Effect->Action.Equals(
							UCataclysmAbilitySystemComponent::KeptStacksRepeatSkillAction,
							ESearchCase::IgnoreCase))
					{
						Action.bKeptStacksRepeatSkill = true;
						Action.KeptStackKey = Action.TriggerKey;
						Action.KeptStackCap = Effect->ScaleMaxSteps;
						// AND WHAT THE ROW CALLS ITS STACKS, from the Ailment cell, for the text above the player's
						// bars. Nothing applies it as a status: this action's `ApplyStatus` is None.
						Action.StatusName = Effect->Ailment.TrimStartAndEnd();
					}
					// AND WHETHER IT GAINS A KEPT STACK ON ITS EVENT AND, AT ITS CAP, MAKES THE NEXT ABILITY BUT
					// THE BASIC ATTACK FREE AND EMPOWERED, spending them all. Ruled 2026-10-17 (O7, O8), the Null
					// Emperor 6-piece. The value is how much more damage that ability deals, in per cent; Scale Max
					// Steps is the cap; the Ailment cell is what the row calls its stacks. Keyed by the row's
					// trigger key, as the action above is.
					if (Effect->Action.Equals(
							UCataclysmAbilitySystemComponent::KeptStacksFreeNextAbilityAction,
							ESearchCase::IgnoreCase))
					{
						Action.bKeptStacksFreeNextAbility = true;
						Action.KeptStackKey = Action.TriggerKey;
						Action.KeptStackCap = Effect->ScaleMaxSteps;
						Action.StatusName = Effect->Ailment.TrimStartAndEnd();
					}
					// AND WHETHER IT GAINS A KEPT STACK ON ITS EVENT AND, AT ITS CAP, MAKES THE NEXT DAMAGE OVER TIME
					// APPLICATION SPREAD EVERY ONE OF THE WEARER'S ON ITS TARGET, spending them all. Ruled 2026-10-26 for
					// the random buff Plague (issue #2538, batch 4). Scale Max Steps is the cap; the Ailment cell is what
					// the row calls its stacks. Keyed by the row's trigger key, as the action above is.
					if (Effect->Action.Equals(
							UCataclysmAbilitySystemComponent::KeptStacksSpreadDotsAction,
							ESearchCase::IgnoreCase))
					{
						Action.bKeptStacksSpreadDots = true;
						Action.KeptStackKey = Action.TriggerKey;
						Action.KeptStackCap = Effect->ScaleMaxSteps;
						Action.StatusName = Effect->Ailment.TrimStartAndEnd();
					}
					// AND WHETHER IT TRIGGERS A DIFFERENT HELD SKILL: one with a cooldown, free, or a spell that
					// pays its cost. Ruled 2026-10-06. The value is the chance.
					Action.bTriggerHeldSkill = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::TriggerHeldSkillAction,
						ESearchCase::IgnoreCase);
					Action.bTriggerHeldSpell = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::TriggerHeldSpellAction,
						ESearchCase::IgnoreCase);
					// AND WHETHER IT ROLLS FOR THE USE IN HAND: no damage, or increased damage; for every skill
					// its tags allow, or for a skill with a cooldown only. Ruled 2026-10-06. The value is the
					// chance. THE SIZE OF THE INCREASE IS THE ROW'S SCALE STEP AND SCALE STEP HIGH, rolled with
					// the item's one roll as the chance is; on these two actions that pair is a size and not a
					// step. Set below.
					const bool bCooldownNoDamage = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::CooldownUseNoDamageAction, ESearchCase::IgnoreCase);
					const bool bCooldownIncrease = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::CooldownUseIncreasedDamageAction, ESearchCase::IgnoreCase);
					Action.bUseDealsNoDamage = bCooldownNoDamage || Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::UseNoDamageAction, ESearchCase::IgnoreCase);
					Action.bUseDealsIncreasedDamage = bCooldownIncrease || Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::UseIncreasedDamageAction, ESearchCase::IgnoreCase);
					Action.bOnlyASkillWithACooldown = bCooldownNoDamage || bCooldownIncrease;
					if (Action.bUseDealsIncreasedDamage)
					{
						Action.UseIncreasePercent = UCataclysmItemModifiers::RolledScaleStep(*Effect, Roll);
					}
					Action.bUseHitsAllNearby = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::UseHitsAllNearbyAction, ESearchCase::IgnoreCase);
					// AND WHETHER THE USE HITS ITS OWN USER: the whole of its hit, or half for a backfire.
					const bool bBackfires = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::UseBackfiresAction, ESearchCase::IgnoreCase);
					Action.bUseHitsItsUser = bBackfires || Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::UseHitsItsUserAction, ESearchCase::IgnoreCase);
					if (bBackfires)
					{
						Action.UseSelfHitSharePercent = UCataclysmAbilitySystemComponent::BackfireSharePercent;
					}
					// AND WHETHER AN ENEMY THE WEARER KILLS EXPLODES FOR ITS OVERKILL. Ruled 2026-10-07. The value
					// is the share of the overkill, in per cent.
					Action.bExplodeVictimForOverkill = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::ExplodeVictimForOverkillAction, ESearchCase::IgnoreCase);
					// AND WHETHER IT BURSTS FOR A SHARE OF ITS MAXIMUM HEALTH: The Ferryman, the Death trees' slice
					// C4c1 (2026-11-25). The value is the share, in per cent.
					Action.bBurstSlainForMaximumHealth = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::BurstSlainForMaximumHealthAction, ESearchCase::IgnoreCase);
					// AND WHETHER IT PASSES THE WEARER'S BLEED LEFT ON A DEAD ENEMY TO THE STRONGEST NEAR IT: Blood
					// Debt, the Death trees' slice C4c2 (2026-11-25). The value is the seconds.
					Action.bPassBleedLeftToStrongest = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::PassBleedLeftToStrongestAction, ESearchCase::IgnoreCase);
					// AND WHETHER IT MAKES THE NEXT LANDED HIT A CRITICAL STRIKE: Chain Execution, the Death trees' slice
					// C4d1 (2026-11-25). The value is the seconds.
					Action.bNextHitCritical = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::CriticalNextHitAction, ESearchCase::IgnoreCase);
					// AND WHETHER IT LENGTHENS A RUNNING HOLD BEFORE THE FERVOUR DECAY: Keep the Count, the Death trees'
					// slice C6a (2026-11-25). The value is the seconds.
					Action.bExtendFervourHold = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::FervourHoldExtendedAction, ESearchCase::IgnoreCase);
					// AND WHETHER IT DOUBLES AN EVASION'S CRITICAL STRIKE CHARGE ON A ROLL OF ITS CHANCE: Twice Answered,
					// the Death trees' slice C6b4 (2026-11-25). The chance is the row's `ChancePercent`.
					Action.bNextHitCriticalTwice = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::CriticalTwiceOnEvasionAction, ESearchCase::IgnoreCase);
					// AND WHETHER IT STRIKES THE STRONGEST ENEMY NEAR ON A KILL, Blade Echo, or moves beside it and
					// strikes, Thousand Cuts: the Death trees' slice C6e (2026-11-25).
					Action.bStrikeStrongestNear = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::StrikeStrongestNearAction, ESearchCase::IgnoreCase);
					Action.bDashToStrongestAndStrike = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::DashToStrongestAndStrikeAction, ESearchCase::IgnoreCase);
					// AND THE NEAREST ENEMY'S, the War trees' engine slice E2 (2026-11-27).
					Action.bStrikeNearestNear = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::StrikeNearestNearAction, ESearchCase::IgnoreCase);
					// AND WHETHER IT REFLECTS WHAT A BLOCK REMOVED, or smites by
					// armour, and whether it counts to an Nth event in a window.
					// Issue #1833 group E part 3. Stack Seconds is the window.
					Action.bReflectBlocked = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::ReflectBlockedAction,
						ESearchCase::IgnoreCase);
					// AND WHETHER IT BLASTS FROM AN ENEMY THAT DIED. Ruled 2026-10-07.
					Action.bBlastFromTheDying = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::BlastFromTheDyingAction,
						ESearchCase::IgnoreCase);
					// AND WHETHER IT STUNS EVERY ENEMY NEAR AN ENEMY THAT DIED. Ruled
					// 2026-10-08. The value is the chance. STACK SECONDS IS HOW LONG THE
					// STUN LASTS on this action, a new use of that column, read as every
					// other reader of it does.
					Action.bStunNearTheDying = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::StunNearTheDyingAction,
						ESearchCase::IgnoreCase);
					if (Action.bStunNearTheDying)
					{
						Action.StackSeconds =
							UCataclysmItemModifiers::RolledStackSeconds(*Effect, Roll);
					}
					// AND WHETHER IT STRIKES THE EVENT'S OTHER CHARACTER. Ruled 2026-10-07.
					// The value is the hit's size, a percentage of attack damage.
					Action.bStrikeTarget = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::StrikeTargetAction,
						ESearchCase::IgnoreCase);
					// AND WHETHER IT EXECUTES THE ENEMY A HIT LEAVES BELOW A SHARE OF ITS HEALTH. Ruled
					// 2026-10-12. The value is the chance; the share is the row's Condition Value, copied
					// below with its Condition as for every action.
					Action.bExecuteTarget = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::ExecuteTargetAction,
						ESearchCase::IgnoreCase);
					// AND WHETHER IT GRANTS A TEMPORARY ABSORB. The owner, 2026-10-07: separate
					// from the energy shield. The value is a percentage of maximum health.
					Action.bTemporaryAbsorb = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::TemporaryAbsorbAction,
						ESearchCase::IgnoreCase);
					// AND WHETHER IT PAYS WRATH INCARNATE'S RETALIATION PULSE, its radius in Event Value. V3a,
					// 2026-11-25.
					Action.bRetaliationPulse = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::RetaliationPulseAction,
						ESearchCase::IgnoreCase);
					// AND WHETHER IT IS THOUSAND DEATHS' CHAIN, its radius in Event Value. V3b, 2026-11-25.
					Action.bFrenzyChain = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::FrenzyChainAction,
						ESearchCase::IgnoreCase);
					// AND BAITED'S TAUNT AND TRAIL OF WIRE'S PLACING. V4a, 2026-11-25.
					Action.bTauntNearTrap = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::TauntEnemiesNearTrapAction,
						ESearchCase::IgnoreCase);
					Action.bPlaceLastTrapAgain = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::PlaceLastTrapAgainAction,
						ESearchCase::IgnoreCase);
					// AND COUNTERFIRE'S ANSWERS. V4b, 2026-11-25.
					Action.bCounterfire = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::CounterfireAction,
						ESearchCase::IgnoreCase);
					// AND WHERE THEY FALL'S AND CROSSFIRE'S. V5a, 2026-11-25.
					Action.bCopyLastTrapAtKill = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::CopyLastTrapAtKillAction,
						ESearchCase::IgnoreCase);
					Action.bCrossfire = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::CrossfireAction,
						ESearchCase::IgnoreCase);
					// AND SCRAP MINES'. V5b1, 2026-11-25.
					Action.bScrapMine = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::ScrapMineAction,
						ESearchCase::IgnoreCase);
					// AND TITANFALL'S. 2026-11-25.
					Action.bTitanfall = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::TitanfallAction,
						ESearchCase::IgnoreCase);
					if (Effect->Action.Equals(
							UCataclysmAbilitySystemComponent::SmiteNearbyByArmourAction,
							ESearchCase::IgnoreCase))
					{
						Action.Nearby = ECataclysmNearbyAction::SmiteByArmour;
					}
					// AND THE TWO OF THE WAR VOW OPTIONS' SECOND SLICE, V2b, 2026-11-25: a smite sized by the event's hit,
					// reaching its Event Value in metres (Pure Carnage, Ragnarok's shockwaves), and the armour stack
					// above placed on every enemy nearby (Warlord).
					if (Effect->Action.Equals(UCataclysmAbilitySystemComponent::SmiteNearbyByHitAction,
											  ESearchCase::IgnoreCase))
					{
						Action.Nearby = ECataclysmNearbyAction::SmiteByHit;
					}
					// AND THE WAR TREES' ACTIONS SIZED BY MAXIMUM HEALTH OR ARMOUR, engine slice E2 (2026-11-27).
					if (Effect->Action.Equals(UCataclysmAbilitySystemComponent::SmiteNearbyOfMaximumHealthAction,
											  ESearchCase::IgnoreCase))
					{
						Action.Nearby = ECataclysmNearbyAction::SmiteByMaximumHealth;
					}
					// AND THE LINE SIZED BY THE HIT, engine slice E2 (2026-11-27), Seismic Slam (Berserker).
					if (Effect->Action.Equals(UCataclysmAbilitySystemComponent::SmiteLineByHitAction,
											  ESearchCase::IgnoreCase))
					{
						Action.Nearby = ECataclysmNearbyAction::SmiteLineByHit;
					}
					// AND THE ONE SIZED BY DAMAGE TAKEN RECENTLY, engine slice E3 (2026-11-27), Wrathful Presence.
					if (Effect->Action.Equals(UCataclysmAbilitySystemComponent::SmiteNearbyOfRecentDamageTakenAction,
											  ESearchCase::IgnoreCase))
					{
						Action.Nearby = ECataclysmNearbyAction::SmiteByRecentDamageTaken;
					}
					Action.bStrikeTargetOfMaximumHealth = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::StrikeTargetOfMaximumHealthAction, ESearchCase::IgnoreCase);
					Action.bStrikeTargetOfTargetMissingHealth = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::StrikeTargetOfTargetMissingHealthAction, ESearchCase::IgnoreCase);
					Action.bKnockBackTarget = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::KnockBackTargetAction, ESearchCase::IgnoreCase);
					// AND WHETHER IT EXECUTES EVERY ENEMY NEAR AND LOW, the same slice (Killing Blow).
					Action.bExecuteNearbyBelow = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::ExecuteNearbyBelowAction, ESearchCase::IgnoreCase);
					// AND WHETHER IT RAISES FERVOUR BY THE SHARE OF MAXIMUM HEALTH LEECHED, the same slice (Vampiric Fury).
					Action.bFervourPerPercentOfMaxHealth = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::FervourPerPercentOfMaxHealthLeechedAction, ESearchCase::IgnoreCase);
					// AND WHETHER IT STORES MORE FOR THE NEXT RETALIATION, the same slice (Spiteful).
					Action.bNextRetaliationMore = Effect->Action.Equals(
						UCataclysmAbilitySystemComponent::NextRetaliationMoreAction, ESearchCase::IgnoreCase);
					// AND WHETHER IT REMOVES ITS NODE'S OWN STACKS, the War trees' engine slice E2 (2026-11-27).
					if (Effect->Action.Equals(UCataclysmAbilitySystemComponent::OwnStacksRemovedAction,
											  ESearchCase::IgnoreCase))
					{
						Action.bRemoveOwnStacks = true;
						Action.OwnStacksRemovedOf = UCataclysmAbilitySystemComponent::NodeOfRowName(Effect->Enchantment);
					}
					if (Effect->Action.Equals(UCataclysmAbilitySystemComponent::NextAttackBonusOfMaximumHealthAction,
											  ESearchCase::IgnoreCase))
					{
						Action.NextAttackBonusOf = 1;
					}
					if (Effect->Action.Equals(UCataclysmAbilitySystemComponent::NextAttackBonusOfArmourAction,
											  ESearchCase::IgnoreCase))
					{
						Action.NextAttackBonusOf = 2;
					}
					if (bArmourRemovedNearby)
					{
						Action.Nearby = ECataclysmNearbyAction::ArmourRemoved;
					}
					if (Action.Nearby != ECataclysmNearbyAction::None && Effect->EveryNth > 1)
					{
						Action.EveryNth = Effect->EveryNth;
						Action.CountWindowSeconds =
							UCataclysmItemModifiers::RolledStackSeconds(*Effect, Roll);
					}

					// EMPTY MEANS THE MAXIMUM, which is what the generator writes
					// when the column is blank and what most sentences mean.
					if (Effect->FractionOf.Equals(TEXT("current"),
												 ESearchCase::IgnoreCase))
					{
						Action.Base = ECataclysmPoolActionBase::Current;
					}
					else if (Effect->FractionOf.Equals(TEXT("event_amount"),
													  ESearchCase::IgnoreCase))
					{
						Action.Base = ECataclysmPoolActionBase::EventAmount;
					}
					// FOR EACH OF THE EVENT'S AMOUNT, OR EACH TEN OF IT: the Death trees' slice C3b. `ActOnEvent`
					// multiplies the value and puts the base back to the maximum before anything reads it.
					else if (Effect->FractionOf.Equals(TEXT("for_each_of_event_amount"),
													  ESearchCase::IgnoreCase))
					{
						Action.Base = ECataclysmPoolActionBase::ForEachOfEventAmount;
					}
					else if (Effect->FractionOf.Equals(TEXT("for_each_ten_of_event_amount"),
													  ESearchCase::IgnoreCase))
					{
						Action.Base = ECataclysmPoolActionBase::ForEachTenOfEventAmount;
					}
					// AND FOR EACH WHOLE HUNDRED OF THE CLASS RESOURCE HELD: Relentless, the Death trees' slice C4d1.
					else if (Effect->FractionOf.Equals(TEXT("for_each_hundred_class_resource_held"),
													  ESearchCase::IgnoreCase))
					{
						Action.Base = ECataclysmPoolActionBase::ForEachHundredClassResourceHeld;
					}
					// AND A FLAT AMOUNT OF THE CLASS RESOURCE, with the row's chance: Opening Found, Reaper's Due and Tear
					// Loose, the Death trees' slice C6b3 (2026-11-25).
					else if (Effect->FractionOf.Equals(TEXT("points"), ESearchCase::IgnoreCase))
					{
						Action.Base = ECataclysmPoolActionBase::Points;
					}
					Action.ChancePercent = Effect->ChancePercent;

					// THE SAME TAG PARSE THE STAT ROWS GET, so a scoped action row
					// and a scoped stat row cannot disagree about what a tag list
					// means.
					TArray<FString> ActionTags;
					Effect->RequiredTags.ParseIntoArray(
						ActionTags, TEXT(","), /*InCullEmpty=*/true);
					for (FString& Tag : ActionTags)
					{
						Tag.TrimStartAndEndInline();
						if (!Tag.IsEmpty())
						{
							Action.RequiredTags.AddTag(
								FGameplayTag::RequestGameplayTag(
									FName(*Tag), /*ErrorIfNotFound=*/false));
						}
					}

					// A CONDITION THE BUILD DOES NOT KNOW DROPS THE WHOLE ROW rather
					// than firing it unconditioned, which is the rule the stat rows
					// already follow: applying a drawback with its condition dropped
					// would make it hold all the time.
					if (!Effect->Condition.IsEmpty()
						&& !UCataclysmStatPipeline::ConditionNamed(
							Effect->Condition, Action.Condition))
					{
						UE_LOG(LogCataclysm, Warning,
							   TEXT("Enchantment '%s' moves a pool under the "
									"condition '%s', which this build does not know, "
									"so the row does nothing. Regenerate "
									"game/Data/EnchantmentEffects.csv from the "
									"workbook."),
							   *Effect->Enchantment, *Effect->Condition);
						continue;
					}
					Action.ConditionValue = Effect->ConditionValue;
					// AND A SECOND CONDITION, since the War vow options' second slice (V2b, 2026-11-25). Refused as the
					// first is when this build does not know its name.
					if (!Effect->Condition2.IsEmpty()
						&& !UCataclysmStatPipeline::ConditionNamed(Effect->Condition2, Action.Condition2))
					{
						UE_LOG(LogCataclysm, Warning,
							   TEXT("Enchantment '%s' acts under the second condition '%s', which this build does "
									"not know, so the row does nothing. Regenerate the CSV from the workbook."),
							   *Effect->Enchantment, *Effect->Condition2);
						continue;
					}
					Action.ConditionValue2 = Effect->ConditionValue2;
					Action.bFromADrawback = bFromADrawback;
					Actions->Add(Action);
				}
				continue;
			}

			FCataclysmStatModifier Modifier;
			if (!EnchantmentModifierFor(*Effect, Roll, Modifier))
			{
				UE_LOG(LogCataclysm, Warning,
					   TEXT("Enchantment '%s' names the condition '%s' or the "
							"scale '%s', which this build does not know, so the "
							"row grants nothing. Regenerate "
							"game/Data/EnchantmentEffects.csv from the workbook."),
					   *Effect->Enchantment, *Effect->Condition, *Effect->Scale);
				continue;
			}

			// THE ITEM'S OWN KILLS, FOLDED IN HERE rather than read when the stat
			// is asked, because no scale can see an item: every other scale reads
			// the character. Issue #1833, ruled 2026-09-25 under the owner's
			// delegation. The value is fixed until the count crosses its next
			// step, and the equipment refreshes the grant when it does
			// (`UCataclysmEquipmentComponent::NoteKillOnWornWeapons`).
			if (Modifier.Scale == ECataclysmStatScale::PerKillOfThisWeapon)
			{
				int32 Steps = Modifier.ScaleStep > 0.0f
					? FMath::FloorToInt32(static_cast<float>(FMath::Max(0, ItemKills))
										  / Modifier.ScaleStep)
					: 0;
				if (Modifier.ScaleMaxSteps > 0)
				{
					Steps = FMath::Min(Steps, Modifier.ScaleMaxSteps);
				}
				Modifier.Value *= static_cast<float>(Steps);
				Modifier.Scale = ECataclysmStatScale::Fixed;
				Modifier.ScaleStep = 0.0f;
				Modifier.ScaleMaxSteps = 0;
			}
			Totals.FindOrAdd(FName(*Effect->Stat)).Add(Modifier);
			++Added;
		}
	};
	auto Grant = [&EffectsFor, &GrantRows](FName Enchantment, float Roll, int32 ItemKills,
										  bool bFromADrawback)
	{
		const TArray<const FCataclysmEnchantmentEffectRow*>* Effects =
			EffectsFor.Find(Enchantment);
		if (!Effects)
		{
			// MOST ENCHANTMENTS ARE HERE, AND IT IS NOT A FAULT. A row is written
			// for an enchantment once the game has what its sentence needs.
			return;
		}
		GrantRows(*Effects, Roll, ItemKills, bFromADrawback);
	};

	// A BENEFIT ONCE HOWEVER MANY PIECES CARRY IT, AT THE HIGHER OF THEIR
	// ROLLS, AND A DRAWBACK FOR EVERY PIECE AT ITS OWN. The header says why
	// each. The benefits are gathered first and granted afterwards, so that the
	// higher roll is used whichever piece comes first in the list.
	TArray<FName> BenefitOrder;
	TMap<FName, float> HighestBenefitRoll;

	// AND THE KILLS OF THE ITEM HOLDING THAT HIGHER ROLL, the larger count on a
	// tie. Issue #1833, ruled 2026-09-25 under the owner's delegation: a
	// benefit is granted once, so the one grant reads one item's count.
	TMap<FName, int32> BenefitKills;

	// AND HOW MANY WORN ITEMS CARRY EACH NAMED SET. Issue #45. A set's rows turn
	// on by that count rather than per piece, so the loop below counts a set's
	// pieces where it grants an ordinary benefit. An item is ONE piece of a set
	// however many of its enchantment slots carry that set, which is why the
	// sets on one item are gathered before they are counted.
	TMap<int32, int32> PiecesBySet;

	for (const FCataclysmItem& Item : Worn)
	{
		if (Item.Base.IsNone())
		{
			continue;
		}

		TSet<int32> SetsOnThisItem;
		for (const FCataclysmRolledEnchantment& Rolled : Item.Enchantments)
		{
			if (!Rolled.Positive.IsNone()
				&& !IsSetEnchantmentRow(PositiveTable, Rolled.Positive))
			{
				if (float* Highest = HighestBenefitRoll.Find(Rolled.Positive))
				{
					int32& Kills = BenefitKills.FindOrAdd(Rolled.Positive);
					if (Rolled.PositiveRoll > *Highest)
					{
						*Highest = Rolled.PositiveRoll;
						Kills = Item.Kills;
					}
					else if (Rolled.PositiveRoll == *Highest)
					{
						Kills = FMath::Max(Kills, Item.Kills);
					}
				}
				else
				{
					HighestBenefitRoll.Add(Rolled.Positive, Rolled.PositiveRoll);
					BenefitKills.Add(Rolled.Positive, Item.Kills);
					BenefitOrder.Add(Rolled.Positive);
				}
			}
			else if (const int32 SetId = SetOfBenefit(PositiveTable, Rolled.Positive))
			{
				SetsOnThisItem.Add(SetId);
			}

			if (!Rolled.Negative.IsNone()
				&& !IsSetEnchantmentRow(NegativeTable, Rolled.Negative))
			{
				Grant(Rolled.Negative, Rolled.NegativeRoll, Item.Kills, /*bFromADrawback=*/true);
			}
		}

		for (const int32 SetId : SetsOnThisItem)
		{
			++PiecesBySet.FindOrAdd(SetId);
		}
	}

	for (const FName& Benefit : BenefitOrder)
	{
		Grant(Benefit, HighestBenefitRoll[Benefit], BenefitKills.FindRef(Benefit), /*bFromADrawback=*/false);
	}

	// A SET'S ROWS, ONCE ITS WORN PIECES ARE COUNTED. Issue #45, on the owner's
	// rules of 2026-09-08: a set's bonuses turn on by how many equipped items
	// carry it, and its drawback applies once for the whole set, joining at the
	// first bonus's threshold.
	//
	// EVERY THRESHOLD AT OR BELOW THE COUNT, so ten pieces hold the 2-piece,
	// 6-piece and 10-piece bonuses together rather than the highest one alone.
	// That is a labelled judgement of 2026-09-11 in `docs/DECISIONS.md`: no
	// design text says a lower bonus turns off, and Diablo III writes its
	// 6-piece bonuses to work with what the lower ones grant.
	//
	// A SET WITH NO DRAWBACK ROW GRANTS NOTHING, because `EveryEnchantmentSet`
	// leaves it out. Never a bonus without its cost.
	if (!PiecesBySet.IsEmpty())
	{
		TArray<FCataclysmEnchantmentSet> Sets;
		UCataclysmDropRoll::EveryEnchantmentSet(PositiveTable, NegativeTable, Sets);
		for (const FCataclysmEnchantmentSet& Set : Sets)
		{
			const int32* Pieces = PiecesBySet.Find(Set.SetId);
			if (!Pieces || Set.Thresholds.IsEmpty() || *Pieces < Set.Thresholds[0])
			{
				continue;
			}

			for (int32 Index = 0; Index < Set.Positives.Num(); ++Index)
			{
				if (*Pieces >= Set.Thresholds[Index])
				{
					Grant(Set.Positives[Index], SetRowRoll, /*ItemKills=*/0, /*bFromADrawback=*/false);
				}
			}
			Grant(Set.Negative, SetRowRoll, /*ItemKills=*/0, /*bFromADrawback=*/true);
		}
	}

	// AND THE ROWS A HELD RANDOM BUFF WEARS, as they are: each is already renamed
	// under the buff's own enchantment name, so every key it keeps is its own.
	// Issue #2538, batch 2. At the set rows' roll, as a set row's figure is; the
	// six rows worn so far each state one figure, so the roll changes none.
	if (ExtraRows)
	{
		TArray<const FCataclysmEnchantmentEffectRow*> Extra;
		for (const FCataclysmEnchantmentEffectRow& Row : *ExtraRows)
		{
			Extra.Add(&Row);
		}
		GrantRows(Extra, SetRowRoll, /*ItemKills=*/0, /*bFromADrawback=*/false);
	}

	return Added;
}

float UCataclysmItemModifiers::WeaponDamageForType(
	const UDataTable* BaseTable, const FString& WeaponType, int32 GearLevel)
{
	if (!BaseTable || WeaponType.IsEmpty())
	{
		return 0.0f;
	}

	float Found = 0.0f;
	BaseTable->ForeachRow<FCataclysmItemBaseRow>(
		TEXT("UCataclysmItemModifiers::WeaponDamageForType"),
		[&](const FName&, const FCataclysmItemBaseRow& Row)
		{
			if (Found > 0.0f
				|| !Row.WeaponType.Equals(WeaponType, ESearchCase::IgnoreCase))
			{
				return;
			}

			// Both implicit slots are searched. Which one carries the damage is
			// not fixed: a Fist reads attack_damage first and a Staff reads it
			// first too, but nothing in the sheet promises that, and assuming
			// slot one would silently return zero the day it moves.
			const FString ImplicitStats[] = { Row.Implicit1Stat, Row.Implicit2Stat };
			const int32 ImplicitsAtZero[] = { Row.Implicit1AtZero, Row.Implicit2AtZero };

			for (int32 Index = 0; Index < 2; ++Index)
			{
				if (!ImplicitStats[Index].Equals(AttackDamageStat, ESearchCase::IgnoreCase))
				{
					continue;
				}

				// THE +0 FIGURE IS UPGRADED and a two-hander doubles it.
				// Leaving either out is a silent halving or worse, and the
				// symptom is only that everything hits softly.
				Found = UCataclysmItemValues::ImplicitValueAtLevel(
					ImplicitsAtZero[Index], GearLevel,
					/*bTwoHanded=*/Row.Hands == 2);
				return;
			}
		});

	return Found;
}

float UCataclysmItemModifiers::WeaponAttackSpeedForType(
	const UDataTable* BaseTable, const FString& WeaponType)
{
	if (!BaseTable || WeaponType.IsEmpty())
	{
		return 0.0f;
	}

	float Found = 0.0f;
	BaseTable->ForeachRow<FCataclysmItemBaseRow>(
		TEXT("UCataclysmItemModifiers::WeaponAttackSpeedForType"),
		[&](const FName&, const FCataclysmItemBaseRow& Row)
		{
			if (Found > 0.0f
				|| !Row.WeaponType.Equals(WeaponType, ESearchCase::IgnoreCase))
			{
				return;
			}

			// STRAIGHT OFF THE COLUMN, WITH NO GEAR LEVEL AND NO TWO-HANDED
			// DOUBLING. Both of those apply to the damage above and would be
			// nonsense here: a two-hander doubling its rate would swing twice as
			// fast as a one-hander, which is the opposite of what a two-hander
			// is. FCataclysmItemBaseRow states that reasoning on the field.
			Found = Row.AttackSpeed;
		});

	return Found;
}

// ---------------------------------------------------------------------------
// What the weapons actually worn are worth. Issue #840
// ---------------------------------------------------------------------------

namespace
{
	/**
	 * The base row a worn item is made from, or null.
	 *
	 * FOUND BY ROW NAME RATHER THAN SEARCHED FOR. The two functions above walk
	 * every row because a weapon TYPE is not a row name and several rows can
	 * carry the same type. An item names its row, so there is nothing to search.
	 */
	const FCataclysmItemBaseRow* BaseRowOf(const FCataclysmItem& Item,
										   const UDataTable* BaseTable)
	{
		if (!BaseTable || Item.Base.IsNone())
		{
			return nullptr;
		}
		return BaseTable->FindRow<FCataclysmItemBaseRow>(
			Item.Base, TEXT("UCataclysmItemModifiers weapon blend"),
			/*bWarnIfMissing=*/false);
	}

	/** The attack damage implicit on a base row at +0, before upgrade or
	 *  doubling. A whole number since issue #2883. */
	int32 AttackDamageAtZero(const FCataclysmItemBaseRow& Row)
	{
		// BOTH IMPLICIT SLOTS, for the reason WeaponDamageForType gives above:
		// nothing in the sheet promises which slot carries the damage.
		const FString Stats[] = { Row.Implicit1Stat, Row.Implicit2Stat };
		const int32 Values[] = { Row.Implicit1AtZero, Row.Implicit2AtZero };

		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (Stats[Index].Equals(UCataclysmItemModifiers::AttackDamageStat,
								    ESearchCase::IgnoreCase))
			{
				return Values[Index];
			}
		}
		return 0;
	}
}

float UCataclysmItemModifiers::WeaponDamageForItem(
	const FCataclysmItem& Item, const UDataTable* BaseTable)
{
	const FCataclysmItemBaseRow* Row = BaseRowOf(Item, BaseTable);
	if (!Row)
	{
		return 0.0f;
	}

	const int32 AtZero = AttackDamageAtZero(*Row);
	if (AtZero <= 0)
	{
		return 0.0f;
	}

	// THE ITEM'S OWN UPGRADE LEVEL, which is the entire point of this function
	// existing beside WeaponDamageForType. The +0 figure is upgraded and a
	// two-hander doubles it; leaving either out is a silent halving.
	return UCataclysmItemValues::ImplicitValueAtLevel(
		AtZero, Item.GearLevel, /*bTwoHanded=*/Row->Hands == 2);
}

bool UCataclysmItemModifiers::WeaponIsArmed(
	const FCataclysmItem& Item, const UDataTable* BaseTable)
{
	const FCataclysmItemBaseRow* Row = BaseRowOf(Item, BaseTable);

	// ASKED OF THE STATED FIGURE AND NOT OF THE UPGRADED ONE, so the answer
	// does not depend on how good the weapon is. A +0 Shield and a +10 Shield
	// both arm nothing, and a +0 Whip arms.
	return Row != nullptr && AttackDamageAtZero(*Row) > 0;
}

float UCataclysmItemModifiers::BlendedWeaponDamage(
	const TArray<FCataclysmItem>& Weapons, const UDataTable* BaseTable)
{
	float Total = 0.0f;
	for (const FCataclysmItem& Weapon : Weapons)
	{
		// SUMMED. Adding a weapon adds its damage; it does not replace the
		// other weapon's. Weapons supplying nothing add nothing, so a Shield
		// costs the pair no damage rather than halving it.
		Total += WeaponDamageForItem(Weapon, BaseTable);
	}
	return Total;
}

float UCataclysmItemModifiers::BlendedAttackSpeed(
	const TArray<FCataclysmItem>& Weapons, const UDataTable* BaseTable)
{
	float Total = 0.0f;
	int32 Armed = 0;

	for (const FCataclysmItem& Weapon : Weapons)
	{
		if (!WeaponIsArmed(Weapon, BaseTable))
		{
			continue;
		}
		if (const FCataclysmItemBaseRow* Row = BaseRowOf(Weapon, BaseTable))
		{
			Total += Row->AttackSpeed;
			++Armed;
		}
	}

	// AVERAGED OVER WHAT ARMS, NOT OVER WHAT IS WORN. A weapon held with a
	// Shield swings at its own rate; dividing by two would have the shield drag
	// the rate toward a number it never contributed to.
	//
	// NOTHING ARMED ANSWERS ZERO rather than dividing by zero, and zero is read
	// by the automatic basic attack as never swinging.
	return Armed > 0 ? Total / static_cast<float>(Armed) : 0.0f;
}
