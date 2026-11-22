// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmGems.h"

#include "Cataclysm.h"
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
									TMap<FName, TArray<FCataclysmStatModifier>>& Totals)
{
	return AddModifier(Row.Stat.IsEmpty() ? NAME_None : FName(*Row.Stat), Row.ValueKind, SheetValueAt(Row, Rarity),
					   Totals);
}

void UCataclysmGems::AddSocketedGems(const FCataclysmItem& Item, const UDataTable* GemTable,
									 TMap<FName, TArray<FCataclysmStatModifier>>& Totals)
{
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
		AddGemModifier(*Row, Socket.GemRarity, Totals);
	}
}

TArray<FString> UCataclysmGems::SocketLinesFor(const FCataclysmItem& Item)
{
	TArray<FString> Lines;
	for (const FCataclysmSocket& Socket : Item.Sockets)
	{
		const FString Holder = Socket.Type == ECataclysmSocketType::Any
			? FString(TEXT("Socket for any gem"))
			: FString::Printf(TEXT("%s socket"), *SocketTypeName(Socket.Type));
		const FString Held = Socket.Gem.IsNone()
			? FString(TEXT("empty"))
			: FString::Printf(TEXT("%s (%s)"), *NameOf(Socket.Gem), *RarityName(Socket.GemRarity));
		Lines.Add(FString::Printf(TEXT("%s: %s"), *Holder, *Held));
	}
	return Lines;
}

TArray<FString> UCataclysmGems::TooltipLinesFor(FName Gem, ECataclysmRarity Rarity, int32 Quantity)
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
