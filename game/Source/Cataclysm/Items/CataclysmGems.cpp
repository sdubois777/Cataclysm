// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmGems.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Cataclysm.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

const TCHAR* UCataclysmGems::TableAssetPath = TEXT("/Game/Data/DT_Gems.DT_Gems");

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

const UDataTable* UCataclysmGems::LoadGeneratedTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, TableAssetPath);
	if (!Table)
	{
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load %s. It is produced by tools/generate_datatable_assets.py from game/Data/Gems.csv, which "
				 "tools/generate_datatables.py produces from the Gems sheet of docs/All_Things_Cataclysm.xlsx."),
			TableAssetPath);
	}
	return Table;
}

const FCataclysmGemRow* UCataclysmGems::RowOf(const UDataTable* Table, FName Gem)
{
	return Table && !Gem.IsNone()
		? Table->FindRow<FCataclysmGemRow>(Gem, TEXT("UCataclysmGems::RowOf"), /*bWarnIfMissing=*/false)
		: nullptr;
}

FString UCataclysmGems::NameOf(FName Gem)
{
	const FCataclysmGemRow* Row = RowOf(LoadGeneratedTable(), Gem);
	return Row && !Row->GemName.IsEmpty() ? Row->GemName : Gem.ToString();
}

FString UCataclysmGems::RarityName(ECataclysmRarity Rarity)
{
	return StaticEnum<ECataclysmRarity>()->GetDisplayNameTextByValue(static_cast<int64>(Rarity)).ToString();
}

FString UCataclysmGems::EffectAt(const FCataclysmGemRow& Row, ECataclysmRarity Rarity)
{
	// THE FIRST FIGURE BEFORE A "%", which is the one the generator reads as the Everyday value ("(\d*\.?\d+)\s*%").
	const FString& Text = Row.Effect;
	int32 Percent = INDEX_NONE;
	if (!Text.FindChar(TEXT('%'), Percent))
	{
		return Text;
	}
	int32 End = Percent;
	while (End > 0 && FChar::IsWhitespace(Text[End - 1]))
	{
		--End;
	}
	int32 Start = End;
	while (Start > 0 && (FChar::IsDigit(Text[Start - 1]) || Text[Start - 1] == TEXT('.')))
	{
		--Start;
	}
	if (Start == End)
	{
		return Text;
	}

	// TWO PLACES AT MOST, WITH NO TRAILING NOUGHTS: 60, 0.5, 1.5.
	FString Figure = FString::Printf(TEXT("%.2f"), SheetValueAt(Row, Rarity) * PointsPerSheetUnit);
	while (Figure.Contains(TEXT(".")) && (Figure.EndsWith(TEXT("0")) || Figure.EndsWith(TEXT("."))))
	{
		Figure.LeftChopInline(1);
	}
	return Text.Left(Start) + Figure + Text.Mid(End);
}

FString UCataclysmGems::SocketTypeName(ECataclysmSocketType Type)
{
	return StaticEnum<ECataclysmSocketType>()->GetDisplayNameTextByValue(static_cast<int64>(Type)).ToString();
}

bool UCataclysmGems::GemFits(ECataclysmSocketType Type, const FString& GemType)
{
	return Type == ECataclysmSocketType::Any
		|| GemType.TrimStartAndEnd().Equals(SocketTypeName(Type), ESearchCase::IgnoreCase);
}

const TCHAR* UCataclysmGems::SocketFitsAnyGemStat = TEXT("socket_fits_any_gem");
const TCHAR* UCataclysmGems::SocketedGemEffectStat = TEXT("socketed_gem_effect");

namespace CataclysmGemEnchantments
{
	/**
	 * Calls `Each` with the value, at the piece's own roll, of every effect row of the piece's drawn enchantments that
	 * states `Stat`: each positive half at `PositiveRoll`, each negative half at `NegativeRoll`. The shape of
	 * `UCataclysmDropRoll::DamageTypesCarriedBy`, which reads `damage_types_carried` off a piece the same way.
	 */
	template <typename FEach>
	void ForEachValueOn(const FCataclysmItem& Item, const TCHAR* Stat, const UDataTable* EffectTable, FEach&& Each)
	{
		if (EffectTable == nullptr || Item.Enchantments.Num() == 0)
		{
			return;
		}
		const auto Read = [EffectTable, Stat, &Each](FName Half, float Roll)
		{
			if (Half.IsNone())
			{
				return;
			}
			const FString Named = Half.ToString();
			for (const TPair<FName, uint8*>& Row : EffectTable->GetRowMap())
			{
				const FCataclysmEnchantmentEffectRow* Effect =
					reinterpret_cast<const FCataclysmEnchantmentEffectRow*>(Row.Value);
				if (Effect && Effect->Enchantment == Named && Effect->Stat.Equals(Stat, ESearchCase::IgnoreCase))
				{
					Each(UCataclysmItemValues::EnchantmentValue(Effect->ValueLow, Effect->ValueHigh, Roll));
				}
			}
		};
		for (const FCataclysmRolledEnchantment& Rolled : Item.Enchantments)
		{
			Read(Rolled.Positive, Rolled.PositiveRoll);
			Read(Rolled.Negative, Rolled.NegativeRoll);
		}
	}
}

bool UCataclysmGems::FitsAnyGem(const FCataclysmItem& Item, const UDataTable* EnchantmentEffectTable)
{
	bool bFits = false;
	CataclysmGemEnchantments::ForEachValueOn(Item, SocketFitsAnyGemStat, EnchantmentEffectTable,
											 [&bFits](float Value) { bFits = bFits || Value > 0.0f; });
	return bFits;
}

float UCataclysmGems::SocketedGemEffectOf(const FCataclysmItem& Item, const UDataTable* EnchantmentEffectTable)
{
	// EACH ROW MULTIPLIES, as "more" and "less" do in the stat pipeline: two rows of 50% less would leave a quarter.
	float Effect = 1.0f;
	CataclysmGemEnchantments::ForEachValueOn(Item, SocketedGemEffectStat, EnchantmentEffectTable,
											 [&Effect](float Value) { Effect *= 1.0f + Value / 100.0f; });
	return FMath::Max(0.0f, Effect);
}

bool UCataclysmGems::GemFitsIn(const FCataclysmItem& Item, int32 Socket, const FString& GemType,
							   const UDataTable* EnchantmentEffectTable)
{
	return Item.Sockets.IsValidIndex(Socket)
		&& (GemFits(Item.Sockets[Socket].Type, GemType) || FitsAnyGem(Item, EnchantmentEffectTable));
}

FName UCataclysmGems::RollGem(const UDataTable* GemTable, FRandomStream& Stream)
{
	if (GemTable == nullptr)
	{
		return NAME_None;
	}
	const TArray<FName> Gems = GemTable->GetRowNames();
	return Gems.Num() > 0 ? Gems[Stream.RandRange(0, Gems.Num() - 1)] : NAME_None;
}

FString UCataclysmGems::FloorNameOf(FName Gem, ECataclysmRarity Rarity)
{
	return FString::Printf(TEXT("%s (%s)"), *NameOf(Gem), *RarityName(Rarity));
}

ECataclysmSocketType UCataclysmGems::RollSocketType(FRandomStream& Stream)
{
	// EVENLY AMONG THE THREE TYPES A GEM CAN HAVE, and never Any, which no drop rolls.
	switch (Stream.RandRange(0, 2))
	{
	case 0:		return ECataclysmSocketType::Attack;
	case 1:		return ECataclysmSocketType::Defense;
	default:	return ECataclysmSocketType::Utility;
	}
}

bool UCataclysmGems::AddGemModifier(const FCataclysmGemRow& Row, ECataclysmRarity Rarity,
									TMap<FName, TArray<FCataclysmStatModifier>>& Totals, float Effect)
{
	return AddModifier(Row.Stat.IsEmpty() ? NAME_None : FName(*Row.Stat), Row.ValueKind,
					   SheetValueAt(Row, Rarity) * Effect, Totals);
}

void UCataclysmGems::AddSocketedGems(const FCataclysmItem& Item, const UDataTable* GemTable,
									 const UDataTable* EnchantmentEffectTable,
									 TMap<FName, TArray<FCataclysmStatModifier>>& Totals)
{
	// THE PIECE'S OWN TWO GEM ENCHANTMENTS, read once for all its sockets. Slice G10, ruled 2026-11-22.
	const bool bFitsAnyGem = FitsAnyGem(Item, EnchantmentEffectTable);
	const float Effect = SocketedGemEffectOf(Item, EnchantmentEffectTable);
	for (const FCataclysmSocket& Socket : Item.Sockets)
	{
		if (Socket.Gem.IsNone())
		{
			continue;
		}
		const FCataclysmGemRow* Row = RowOf(GemTable, Socket.Gem);
		if (!Row)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("The gem '%s' socketed in '%s' is not in the gem table, so it grants nothing."),
				*Socket.Gem.ToString(), *Item.Base.ToString());
			continue;
		}
		// A GEM THAT FITS NEITHER ITS SOCKET NOR THE PIECE DOES NOTHING, AND STAYS (Q2, a judgement). It got there
		// through "Sockets on this equipment fit any gem type", which the piece no longer carries; the socket kept its
		// own type, so it acts again if the piece carries the enchantment again. Not logged: it is a state a player
		// reaches, not a fault.
		if (!bFitsAnyGem && !GemFits(Socket.Type, Row->GemType))
		{
			continue;
		}
		// THE VALUE TIMES THE PIECE'S OWN GEM EFFECT, BEFORE ITS BUCKET (Q3).
		AddGemModifier(*Row, Socket.GemRarity, Totals, Effect);
	}
}

TArray<FString> UCataclysmGems::SocketLinesFor(const FCataclysmItem& Item)
{
	return SocketLinesFor(Item, LoadGeneratedTable(), UCataclysmItemModifiers::LoadEnchantmentEffectTable());
}

TArray<FString> UCataclysmGems::SocketLinesFor(const FCataclysmItem& Item, const UDataTable* GemTable,
											   const UDataTable* EnchantmentEffectTable)
{
	const bool bFitsAnyGem = FitsAnyGem(Item, EnchantmentEffectTable);
	TArray<FString> Lines;
	for (const FCataclysmSocket& Socket : Item.Sockets)
	{
		const FString Holder = bFitsAnyGem
			? FString(TEXT("Socket for any gem (enchanted)"))
			: Socket.Type == ECataclysmSocketType::Any
			? FString(TEXT("Socket for any gem"))
			: FString::Printf(TEXT("%s socket"), *SocketTypeName(Socket.Type));
		FString Held = Socket.Gem.IsNone()
			? FString(TEXT("empty"))
			: FString::Printf(TEXT("%s (%s)"), *NameOf(Socket.Gem), *RarityName(Socket.GemRarity));
		// AND A GEM THAT FITS NEITHER, which `AddSocketedGems` skips (Q2). A gem the table lacks is not marked: it is
		// skipped for that reason, and logged there.
		const FCataclysmGemRow* Row = Socket.Gem.IsNone() ? nullptr : RowOf(GemTable, Socket.Gem);
		if (Row != nullptr && !bFitsAnyGem && !GemFits(Socket.Type, Row->GemType))
		{
			Held += TEXT(" (does not fit: inactive)");
		}
		Lines.Add(FString::Printf(TEXT("%s: %s"), *Holder, *Held));
	}
	return Lines;
}

FString UCataclysmGems::NothingToIncreaseLineFor(const FCataclysmGemRow& Row, const UAbilitySystemComponent* Character)
{
	// THE FOUR STATS NO CLASS HAS A BASE OF, `game/Data/ClassStats.csv`. This list is checked against that file and
	// the gems that raise them by `tools/tests/test_every_gem_names_a_stat_the_game_reads.py`.
	static const TCHAR* const NoBase[][2] = {
		{ TEXT("block_chance"), TEXT("block chance") },
		{ TEXT("evasion"), TEXT("evasion") },
		{ TEXT("cooldown_reduction"), TEXT("cooldown reduction") },
		{ TEXT("magic_find"), TEXT("magic find") },
	};
	if (!Row.ValueKind.TrimStartAndEnd().Equals(TEXT("increased"), ESearchCase::IgnoreCase))
	{
		return FString();
	}
	const FString Stat = Row.Stat.TrimStartAndEnd();
	for (const auto& Pair : NoBase)
	{
		if (!Stat.Equals(Pair[0], ESearchCase::IgnoreCase))
		{
			continue;
		}
		// WITH NO CHARACTER, THE RULE, true for every character (the form ruled on 2026-11-22).
		if (Cast<UCataclysmAbilitySystemComponent>(Character) == nullptr)
		{
			return FString::Printf(TEXT("An increase multiplies what you already have. No class has a base %s, so this "
										"gem does nothing until your gear gives some."), Pair[1]);
		}
		// WITH ONE, ITS OWN STATE (issue #2835): nothing to say when it has some, which the effect line covers.
		if (AmountToIncreaseFor(Character, FName(Pair[0])) > 0.0f)
		{
			return FString();
		}
		return FString::Printf(TEXT("An increase multiplies what you already have. You have no %s, from your class or "
									"your items, so this gem does nothing for you until your gear gives some."), Pair[1]);
	}
	return FString();
}

float UCataclysmGems::AmountToIncreaseFor(const UAbilitySystemComponent* Character, FName Stat)
{
	const UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(Character);
	if (Cataclysm == nullptr)
	{
		return 0.0f;
	}
	if (const FCataclysmStatInputs* Inputs = Cataclysm->GetStatInputs(Stat))
	{
		const FCataclysmStatBreakdown Breakdown =
			UCataclysmStatPipeline::Evaluate(Inputs->Base, Inputs->Modifiers, FGameplayTagContainer());
		return Breakdown.Base + Breakdown.Flat;
	}
	// NO MODIFIER WAS RECORDED, so the attribute holds the base unchanged (`UCataclysmPlayerClassStats::ApplyTo` records
	// only a stat that has one).
	const FGameplayAttribute* Attribute = UCataclysmPlayerClassStats::StatToAttribute().Find(Stat.ToString());
	if (Attribute == nullptr || !Attribute->IsValid() || !Cataclysm->HasAttributeSetForAttribute(*Attribute))
	{
		return 0.0f;
	}
	return Cataclysm->GetNumericAttribute(*Attribute);
}

TArray<FString> UCataclysmGems::TooltipLinesFor(FName Gem, ECataclysmRarity Rarity, int32 Quantity,
											   const UAbilitySystemComponent* Character)
{
	TArray<FString> Lines;
	const FCataclysmGemRow* Row = RowOf(LoadGeneratedTable(), Gem);
	Lines.Add(Row && !Row->GemName.IsEmpty() ? Row->GemName : Gem.ToString());
	const FString Type = Row ? Row->GemType.TrimStartAndEnd() : FString();
	Lines.Add(Type.IsEmpty() ? FString::Printf(TEXT("%s gem"), *RarityName(Rarity))
							 : FString::Printf(TEXT("%s %s gem"), *RarityName(Rarity), *Type));
	if (Row)
	{
		Lines.Add(EffectAt(*Row, Rarity));
		const FString Explained = NothingToIncreaseLineFor(*Row, Character);
		if (!Explained.IsEmpty())
		{
			Lines.Add(Explained);
		}
	}
	if (!Type.IsEmpty())
	{
		// "AN" BEFORE A VOWEL SOUND: an Attack socket, a Defense socket, a Utility socket.
		const bool bAn = Type.StartsWith(TEXT("A")) || Type.StartsWith(TEXT("E")) || Type.StartsWith(TEXT("I"))
			|| Type.StartsWith(TEXT("O"));
		Lines.Add(FString::Printf(TEXT("Fits %s %s socket"), bAn ? TEXT("an") : TEXT("a"), *Type));
	}
	if (Quantity > 1)
	{
		Lines.Add(FString::Printf(TEXT("%d carried"), Quantity));
	}
	return Lines;
}
