// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmLootFilterLayout.h"

#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmItem.h"

namespace
{
	constexpr int32 RarityCount = static_cast<int32>(ECataclysmRarity::Cataclysmic) + 1;

	const TCHAR* ActionName(ECataclysmLootFilterAction Action)
	{
		switch (Action)
		{
		case ECataclysmLootFilterAction::Hide: return TEXT("Hide");
		case ECataclysmLootFilterAction::Restyle: return TEXT("Restyle");
		default: return TEXT("Show");
		}
	}

	const TCHAR* KindName(ECataclysmLootKind Kind)
	{
		switch (Kind)
		{
		case ECataclysmLootKind::Gear: return TEXT("gear");
		case ECataclysmLootKind::Material: return TEXT("crafting materials");
		case ECataclysmLootKind::Gold: return TEXT("gold");
		case ECataclysmLootKind::Gem: return TEXT("gems");
		default: return TEXT("everything");
		}
	}

	const TCHAR* EnchantName(ECataclysmLootTriState State)
	{
		switch (State)
		{
		case ECataclysmLootTriState::Yes: return TEXT("with an enchantment");
		case ECataclysmLootTriState::No: return TEXT("without an enchantment");
		default: return TEXT("either");
		}
	}

	/** Adds `Value` when absent, removes it when present. */
	template <typename T>
	void Toggle(TArray<T>& Values, const T& Value)
	{
		if (Values.Contains(Value))
		{
			Values.Remove(Value);
		}
		else
		{
			Values.Add(Value);
		}
	}

	/** Steps `Value` by +1 or -1 within [Low, High]. */
	void Step(int32& Value, bool bUp, int32 Low, int32 High)
	{
		Value = FMath::Clamp(Value + (bUp ? 1 : -1), Low, High);
	}

	/** Cycles a palette index through INDEX_NONE (none) and 0 to PaletteSize - 1. */
	void CyclePalette(int32& Index, bool bUp)
	{
		const int32 Count = UCataclysmLootFilterRules::PaletteSize + 1;
		const int32 Position = (Index + 1 + (bUp ? 1 : Count - 1)) % Count;
		Index = Position - 1;
	}

	FCataclysmLootEditorButton Button(const FString& Command, const FString& Label, bool bChosen = false)
	{
		FCataclysmLootEditorButton Out;
		Out.Command = Command;
		Out.Label = Label;
		Out.bChosen = bChosen;
		return Out;
	}

	FString AffixName(FName Row, const UDataTable* AffixTable)
	{
		const FString Name = UCataclysmForge::AffixNameOf(Row, AffixTable);
		return Name.IsEmpty() ? Row.ToString() : Name;
	}
}

TArray<FString> UCataclysmLootFilterLayout::SlotNames()
{
	TArray<FString> Names;
	for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
	{
		Names.AddUnique(UCataclysmGearSlots::BaseSlotFor(Slot));
	}
	return Names;
}

FString UCataclysmLootFilterLayout::RarityName(ECataclysmRarity Rarity)
{
	return StaticEnum<ECataclysmRarity>()->GetDisplayNameTextByValue(static_cast<int64>(Rarity)).ToString();
}

FString UCataclysmLootFilterLayout::PaletteName(int32 Index)
{
	static const TCHAR* Extras[] = {TEXT("Yellow"), TEXT("Cyan"), TEXT("Pink"), TEXT("Black")};
	if (Index >= 0 && Index < RarityCount)
	{
		return RarityName(static_cast<ECataclysmRarity>(Index));
	}
	if (Index >= RarityCount && Index < UCataclysmLootFilterRules::PaletteSize)
	{
		return Extras[Index - RarityCount];
	}
	return TEXT("None");
}

FString UCataclysmLootFilterLayout::FilterLine(const FString& Name, bool bBuiltIn, bool bActive, int32 RuleCount)
{
	FString Line = FString::Printf(TEXT("%s, %d %s"), *Name, RuleCount, RuleCount == 1 ? TEXT("rule") : TEXT("rules"));
	if (bBuiltIn)
	{
		Line += TEXT(" (built in)");
	}
	if (bActive)
	{
		Line += TEXT(" (active)");
	}
	return Line;
}

FString UCataclysmLootFilterLayout::RuleLine(const FCataclysmLootFilterRule& Rule, int32 Index,
											  const UDataTable* AffixTable)
{
	TArray<FString> Parts;
	if (Rule.Rarities.Num() > 0)
	{
		TArray<FString> Names;
		for (const ECataclysmRarity Rarity : Rule.Rarities)
		{
			Names.Add(RarityName(Rarity));
		}
		Parts.Add(FString::Join(Names, TEXT(" or ")));
	}
	if (Rule.bBelowWornRarity)
	{
		Parts.Add(TEXT("below what is worn"));
	}
	if (Rule.RungsBelowTierAtLeast > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d or more rungs below the tier's rarity"), Rule.RungsBelowTierAtLeast));
	}
	if (Rule.Slots.Num() > 0)
	{
		Parts.Add(FString::Join(Rule.Slots, TEXT(" or ")));
	}
	if (Rule.MinGearLevel > 0 || Rule.MaxGearLevel < 10)
	{
		Parts.Add(FString::Printf(TEXT("gear level +%d to +%d"), Rule.MinGearLevel, Rule.MaxGearLevel));
	}
	if (Rule.Affixes.Num() > 0)
	{
		TArray<FString> Names;
		for (const FName& Row : Rule.Affixes)
		{
			Names.Add(AffixName(Row, AffixTable));
		}
		Parts.Add(FString::Printf(TEXT("%d of %s at tier %d or above"), FMath::Max(1, Rule.MinAffixCount),
								  *FString::Join(Names, TEXT(", ")), Rule.MinAffixTier));
	}
	if (Rule.HasEnchantment != ECataclysmLootTriState::Either)
	{
		Parts.Add(EnchantName(Rule.HasEnchantment));
	}
	if (Rule.MinSockets > 0 || Rule.MaxSockets < 6)
	{
		Parts.Add(FString::Printf(TEXT("%d to %d sockets"), Rule.MinSockets, Rule.MaxSockets));
	}
	if (Rule.DamageTypes.Num() > 0)
	{
		TArray<FString> Names;
		for (const FName& Type : Rule.DamageTypes)
		{
			Names.Add(Type.ToString());
		}
		Parts.Add(FString::Join(Names, TEXT(" or ")));
	}
	if (Rule.MinMaterialTier > 1 || Rule.MaxMaterialTier < 5)
	{
		Parts.Add(FString::Printf(TEXT("material tier %d to %d"), Rule.MinMaterialTier, Rule.MaxMaterialTier));
	}

	FString Line = FString::Printf(TEXT("%d. %s %s"), Index + 1, ActionName(Rule.Action), KindName(Rule.Kind));
	if (Parts.Num() > 0)
	{
		Line += TEXT(": ") + FString::Join(Parts, TEXT("; "));
	}
	if (Rule.Action == ECataclysmLootFilterAction::Restyle)
	{
		Line += FString::Printf(TEXT(" -> text %s, background %s, size %.2gx"), *PaletteName(Rule.TextColour),
								*PaletteName(Rule.BackgroundColour),
								UCataclysmLootFilterRules::Sizes[FMath::Clamp(Rule.Size, 0,
																		 UCataclysmLootFilterRules::SizeCount - 1)]);
	}
	return Line;
}

bool UCataclysmLootFilterLayout::EditRule(FCataclysmLootFilterRule& Rule, const FString& Command)
{
	const bool bUp = Command.EndsWith(TEXT("+"));
	FString Field;
	FString Value;
	if (!Command.Split(TEXT("."), &Field, &Value))
	{
		Field = Command.EndsWith(TEXT("+")) || Command.EndsWith(TEXT("-")) ? Command.LeftChop(1) : Command;
	}

	if (Field == TEXT("Action"))
	{
		Rule.Action = static_cast<ECataclysmLootFilterAction>((static_cast<int32>(Rule.Action) + 1) % 3);
	}
	else if (Field == TEXT("Kind"))
	{
		Rule.Kind = static_cast<ECataclysmLootKind>((static_cast<int32>(Rule.Kind) + 1) % 5);
	}
	else if (Field == TEXT("Rarity"))
	{
		const int32 Index = FCString::Atoi(*Value);
		if (!Value.IsNumeric() || Index < 0 || Index >= RarityCount)
		{
			return false;
		}
		Toggle(Rule.Rarities, static_cast<ECataclysmRarity>(Index));
	}
	else if (Field == TEXT("BelowWorn"))
	{
		Rule.bBelowWornRarity = !Rule.bBelowWornRarity;
	}
	else if (Field == TEXT("TierRungs"))
	{
		Step(Rule.RungsBelowTierAtLeast, bUp, 0, RarityCount - 1);
	}
	else if (Field == TEXT("Slot"))
	{
		if (!SlotNames().Contains(Value))
		{
			return false;
		}
		Toggle(Rule.Slots, Value);
	}
	else if (Field == TEXT("GearMin"))
	{
		Step(Rule.MinGearLevel, bUp, 0, Rule.MaxGearLevel);
	}
	else if (Field == TEXT("GearMax"))
	{
		Step(Rule.MaxGearLevel, bUp, Rule.MinGearLevel, 10);
	}
	else if (Field == TEXT("Affix"))
	{
		if (Value.IsEmpty())
		{
			return false;
		}
		Toggle(Rule.Affixes, FName(*Value));
	}
	else if (Field == TEXT("AffixTier"))
	{
		Step(Rule.MinAffixTier, bUp, 1, 7);
	}
	else if (Field == TEXT("AffixCount"))
	{
		Step(Rule.MinAffixCount, bUp, 1, 4);
	}
	else if (Field == TEXT("Enchant"))
	{
		Rule.HasEnchantment = static_cast<ECataclysmLootTriState>((static_cast<int32>(Rule.HasEnchantment) + 1) % 3);
	}
	else if (Field == TEXT("SocketsMin"))
	{
		Step(Rule.MinSockets, bUp, 0, Rule.MaxSockets);
	}
	else if (Field == TEXT("SocketsMax"))
	{
		Step(Rule.MaxSockets, bUp, Rule.MinSockets, 6);
	}
	else if (Field == TEXT("Damage"))
	{
		if (!UCataclysmItemModifiers::DamageTypeNames().Contains(FName(*Value)))
		{
			return false;
		}
		Toggle(Rule.DamageTypes, FName(*Value));
	}
	else if (Field == TEXT("MatMin"))
	{
		Step(Rule.MinMaterialTier, bUp, 1, Rule.MaxMaterialTier);
	}
	else if (Field == TEXT("MatMax"))
	{
		Step(Rule.MaxMaterialTier, bUp, Rule.MinMaterialTier, 5);
	}
	else if (Field == TEXT("Text"))
	{
		CyclePalette(Rule.TextColour, bUp);
	}
	else if (Field == TEXT("Back"))
	{
		CyclePalette(Rule.BackgroundColour, bUp);
	}
	else if (Field == TEXT("Size"))
	{
		Step(Rule.Size, bUp, 0, UCataclysmLootFilterRules::SizeCount - 1);
	}
	else
	{
		return false;
	}
	return true;
}

TArray<FCataclysmLootEditorButton> UCataclysmLootFilterLayout::EditorButtonsFor(
	const FCataclysmLootFilterRule& Rule, const TArray<FName>& AffixRows, int32 AffixPick, const UDataTable* AffixTable)
{
	TArray<FCataclysmLootEditorButton> Out;
	Out.Add(Button(TEXT("Action"), FString::Printf(TEXT("Action: %s"), ActionName(Rule.Action)), true));
	Out.Add(Button(TEXT("Kind"), FString::Printf(TEXT("Drops: %s"), KindName(Rule.Kind)), true));

	for (int32 Index = 0; Index < RarityCount; ++Index)
	{
		const ECataclysmRarity Rarity = static_cast<ECataclysmRarity>(Index);
		Out.Add(Button(FString::Printf(TEXT("Rarity.%d"), Index), RarityName(Rarity), Rule.Rarities.Contains(Rarity)));
	}
	Out.Add(Button(TEXT("BelowWorn"), TEXT("Below what is worn in its slot"), Rule.bBelowWornRarity));
	Out.Add(Button(TEXT("TierRungs-"), TEXT("Rungs below the tier -")));
	Out.Add(Button(TEXT("TierRungs+"),
				   Rule.RungsBelowTierAtLeast > 0
					   ? FString::Printf(TEXT("%d or more rungs below the tier's rarity +"), Rule.RungsBelowTierAtLeast)
					   : FString(TEXT("Rungs below the tier: off +")),
				   Rule.RungsBelowTierAtLeast > 0));

	for (const FString& Slot : SlotNames())
	{
		Out.Add(Button(TEXT("Slot.") + Slot, Slot, Rule.Slots.Contains(Slot)));
	}

	Out.Add(Button(TEXT("GearMin-"), TEXT("Lowest gear level -")));
	Out.Add(Button(TEXT("GearMin+"), FString::Printf(TEXT("Lowest gear level +%d +"), Rule.MinGearLevel)));
	Out.Add(Button(TEXT("GearMax-"), TEXT("Highest gear level -")));
	Out.Add(Button(TEXT("GearMax+"), FString::Printf(TEXT("Highest gear level +%d +"), Rule.MaxGearLevel)));

	if (AffixRows.Num() > 0)
	{
		const FName Picked = AffixRows[FMath::Clamp(AffixPick, 0, AffixRows.Num() - 1)];
		Out.Add(Button(TEXT("AffixPick-"), TEXT("Previous affix")));
		Out.Add(Button(TEXT("AffixPick+"), TEXT("Next affix")));
		Out.Add(Button(TEXT("Affix.") + Picked.ToString(),
					   (Rule.Affixes.Contains(Picked) ? TEXT("Remove ") : TEXT("Add ")) + AffixName(Picked, AffixTable),
					   Rule.Affixes.Contains(Picked)));
	}
	for (const FName& Row : Rule.Affixes)
	{
		Out.Add(Button(TEXT("Affix.") + Row.ToString(), TEXT("Remove ") + AffixName(Row, AffixTable), true));
	}
	Out.Add(Button(TEXT("AffixTier-"), TEXT("Affix tier -")));
	Out.Add(Button(TEXT("AffixTier+"), FString::Printf(TEXT("Affix tier %d or above +"), Rule.MinAffixTier)));
	Out.Add(Button(TEXT("AffixCount-"), TEXT("Affixes needed -")));
	Out.Add(Button(TEXT("AffixCount+"), FString::Printf(TEXT("%d of them needed +"), FMath::Max(1, Rule.MinAffixCount))));

	Out.Add(Button(TEXT("Enchant"), FString::Printf(TEXT("Enchantment: %s"), EnchantName(Rule.HasEnchantment)),
				   Rule.HasEnchantment != ECataclysmLootTriState::Either));

	Out.Add(Button(TEXT("SocketsMin-"), TEXT("Fewest sockets -")));
	Out.Add(Button(TEXT("SocketsMin+"), FString::Printf(TEXT("Fewest sockets %d +"), Rule.MinSockets)));
	Out.Add(Button(TEXT("SocketsMax-"), TEXT("Most sockets -")));
	Out.Add(Button(TEXT("SocketsMax+"), FString::Printf(TEXT("Most sockets %d +"), Rule.MaxSockets)));

	for (const FName& Type : UCataclysmItemModifiers::DamageTypeNames())
	{
		Out.Add(Button(TEXT("Damage.") + Type.ToString(), Type.ToString(), Rule.DamageTypes.Contains(Type)));
	}

	Out.Add(Button(TEXT("MatMin-"), TEXT("Lowest material tier -")));
	Out.Add(Button(TEXT("MatMin+"), FString::Printf(TEXT("Lowest material tier %d +"), Rule.MinMaterialTier)));
	Out.Add(Button(TEXT("MatMax-"), TEXT("Highest material tier -")));
	Out.Add(Button(TEXT("MatMax+"), FString::Printf(TEXT("Highest material tier %d +"), Rule.MaxMaterialTier)));

	if (Rule.Action == ECataclysmLootFilterAction::Restyle)
	{
		Out.Add(Button(TEXT("Text-"), TEXT("Text colour -")));
		Out.Add(Button(TEXT("Text+"), FString::Printf(TEXT("Text colour: %s +"), *PaletteName(Rule.TextColour))));
		Out.Add(Button(TEXT("Back-"), TEXT("Background -")));
		Out.Add(Button(TEXT("Back+"), FString::Printf(TEXT("Background: %s +"), *PaletteName(Rule.BackgroundColour))));
		Out.Add(Button(TEXT("Size-"), TEXT("Size -")));
		Out.Add(Button(TEXT("Size+"),
					   FString::Printf(TEXT("Size %.2gx +"),
									   UCataclysmLootFilterRules::Sizes[FMath::Clamp(Rule.Size, 0,
																			   UCataclysmLootFilterRules::SizeCount - 1)])));
	}
	return Out;
}
