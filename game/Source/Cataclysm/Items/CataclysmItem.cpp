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

float UCataclysmItemValues::AffixValue(float TopValue, float Floor,
									   int32 Tier, float Roll,
									   int32 GearLevel, bool bTwoHanded)
{
	float Low = 0.0f;
	float High = 0.0f;
	TierBand(TopValue, Tier, Low, High);

	const float Within = Low + (High - Low) * FMath::Clamp(Roll, 0.0f, 1.0f);

	// BOTH LADDERS ARE KEPT AND BOTH WERE EASED ON 2026-09-03, for issue #1179.
	// The shape of this function did not change; the two constants it leans on
	// did. TierFraction now spans 3.0 rather than 7.0 and GearLevelFactor gives
	// 2.5 rather than 3.52, so the two multiply to about 10x instead of 32.9x.
	//
	// WHY THEY NEEDED EASING. At 32.9x a tier 1 roll on an un-upgraded drop was
	// 3.04% of the endgame value, against Last Epoch's 16.7% across its own
	// seven tiers. Twelve affixes could not show more than three different
	// numbers early in the game and three could only ever display 0.0, which is
	// a slot granting a number the player reads as nothing.
	//
	// WHY NEITHER WAS DELETED. An earlier attempt removed the gear level ladder
	// entirely, on the reasoning that Path of Exile and Last Epoch both express
	// progression once by gating which tiers can appear. The project owner
	// rejected that: two levers are the point, and the fault was the size of
	// their product rather than their number. Deleting one also put a hole in
	// the late game -- a character's affix tier caps at 7 by difficulty tier 6,
	// so beyond that the gear ladder is the only thing still raising their
	// affixes, and without it their damage rose 7.7% across the last three
	// difficulty tiers while everything scaling with character level kept
	// climbing.
	//
	// The stated top values are still the +10 figures, so the band is divided
	// back to +0 before the piece's own level is applied. Doing it the other way
	// round would make a +10 piece worth 2.5 times a number that already
	// included 2.5. The endgame value is therefore unchanged by all of this;
	// what moved is everything below it.
	const float AtZero = Within / GearLevelMultiplier(MaxGearLevel);
	const float Unfloored = AtZero * GearLevelMultiplier(GearLevel);

	// THE FLOOR IS ADDED AND THE LADDER RUNS ACROSS WHAT IS LEFT, rather
	// than the tier ladder being squeezed to raise the bottom. Issue #1230.
	//
	// WHY NOT SQUEEZE THE TIER LADDER. Measured on 2026-09-04: a floor of a
	// sixth of the top needs a tier ladder spanning 1.80, whose ratio
	// between adjacent tiers is 1.1029, and
	// Cataclysm.Item.BandsOverlapByExactlyOneTier then fails because a
	// perfect roll two tiers down beats the worst roll here. The last floor
	// that survives that route is about an eighth of the top, which is
	// barely better than the tenth it already had.
	//
	// THIS MAP KEEPS THAT GUARD BECAUSE IT IS MONOTONE. It is an affine map
	// from [TopValue * WorstMultiplier(), TopValue] onto [Floor, TopValue],
	// so it preserves the order of every tier, roll and upgrade level
	// combination, and every comparison the guard makes is unchanged.
	//
	// WHAT IT COSTS IS STEEPNESS, and only on the affixes that state a
	// floor. A floor of a quarter of the top takes that affix's tier 1 to
	// tier 7 ladder from 3.00 to 2.25 and its +0 to +10 ladder from 2.50 to
	// 2.00. An affix with no floor keeps both in full.
	const float Worst = TopValue * WorstMultiplier();
	const float Span = TopValue - Worst;
	if (Span <= 0.0f)
	{
		// A HYBRID ROW STATES NO VALUE OF ITS OWN. Its two halves are named
		// in other columns and carry their own tops and floors.
		return 0.0f;
	}

	const float Bottom = Floor > 0.0f ? Floor : Worst;
	const float Value = Bottom
					  + (TopValue - Bottom) * (Unfloored - Worst) / Span;

	return Value * (bTwoHanded ? TwoHandedMultiplier : 1.0f);
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
		Add(FName(*ImplicitStats[Index]), Bucket,
			ECataclysmModifierSource::GearImplicit,
			UCataclysmItemValues::ImplicitValue(ImplicitValues[Index],
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

				// EACH HALF AT 70% OF THE WHOLE AFFIX, which the design states
				// and the simulation derives. See UCataclysmItemValues::
				// HybridFraction for where the figure comes from.
				//
				// THE HALF'S OWN TOP VALUE, NOT THE HYBRID'S. A hybrid is
				// defined in terms of the affixes it combines rather than by
				// copying their numbers, so it cannot drift from them.
				Add(FName(*Part->Stat), PartBucket,
					ECataclysmModifierSource::GearAffix,
					UCataclysmItemValues::AffixValue(
						Part->TopValue, Part->Floor, Rolled.Tier, Rolled.Roll,
						Item.GearLevel, bTwoHanded)
						* UCataclysmItemValues::HybridFraction);
			}
			continue;
		}

		ECataclysmStatBucket Bucket;
		if (!BucketFromKind(Affix->ValueKind, Bucket))
		{
			// An ailment affix grants a chance to apply an effect rather than a
			// stat, so it is not a modifier at all. It is applied where the hit
			// is resolved.
			continue;
		}

		const float Value = UCataclysmItemValues::AffixValue(
			Affix->TopValue, Affix->Floor, Rolled.Tier, Rolled.Roll,
		Item.GearLevel, bTwoHanded);

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
			const float ImplicitValues[] = { Row.Implicit1Value, Row.Implicit2Value };

			for (int32 Index = 0; Index < 2; ++Index)
			{
				if (!ImplicitStats[Index].Equals(AttackDamageStat, ESearchCase::IgnoreCase))
				{
					continue;
				}

				// THE STATED FIGURE IS THE +10 ONE and a two-hander doubles it.
				// Leaving either out is a silent halving or worse, and the
				// symptom is only that everything hits softly.
				Found = UCataclysmItemValues::ImplicitValue(
					ImplicitValues[Index], GearLevel,
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

	/** The attack damage implicit on a base row, before upgrade or doubling. */
	float StatedAttackDamage(const FCataclysmItemBaseRow& Row)
	{
		// BOTH IMPLICIT SLOTS, for the reason WeaponDamageForType gives above:
		// nothing in the sheet promises which slot carries the damage.
		const FString Stats[] = { Row.Implicit1Stat, Row.Implicit2Stat };
		const float Values[] = { Row.Implicit1Value, Row.Implicit2Value };

		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (Stats[Index].Equals(UCataclysmItemModifiers::AttackDamageStat,
								    ESearchCase::IgnoreCase))
			{
				return Values[Index];
			}
		}
		return 0.0f;
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

	const float Stated = StatedAttackDamage(*Row);
	if (Stated <= 0.0f)
	{
		return 0.0f;
	}

	// THE ITEM'S OWN UPGRADE LEVEL, which is the entire point of this function
	// existing beside WeaponDamageForType. The stated figure is the +10 one and
	// a two-hander doubles it; leaving either out is a silent halving.
	return UCataclysmItemValues::ImplicitValue(
		Stated, Item.GearLevel, /*bTwoHanded=*/Row->Hands == 2);
}

bool UCataclysmItemModifiers::WeaponIsArmed(
	const FCataclysmItem& Item, const UDataTable* BaseTable)
{
	const FCataclysmItemBaseRow* Row = BaseRowOf(Item, BaseTable);

	// ASKED OF THE STATED FIGURE AND NOT OF THE UPGRADED ONE, so the answer
	// does not depend on how good the weapon is. A +0 Shield and a +10 Shield
	// both arm nothing, and a +0 Whip arms.
	return Row != nullptr && StatedAttackDamage(*Row) > 0.0f;
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
