// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmLootFilter.h"

#include "Engine/DataTable.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmLootFilterPreferences.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

const float UCataclysmLootFilterRules::Sizes[UCataclysmLootFilterRules::SizeCount] = {1.0f, 1.25f, 1.5f};

const FLinearColor UCataclysmLootFilterRules::ExtraColours[4] = {
	FLinearColor(FColor(0xFF, 0xE1, 0x4D)), // yellow
	FLinearColor(FColor(0x33, 0xE6, 0xFF)), // cyan
	FLinearColor(FColor(0xFF, 0x66, 0xCC)), // pink
	FLinearColor::Black,
};

const TCHAR* UCataclysmLootFilterRules::DefaultFilterName = TEXT("Hide worse than worn");

namespace
{
	constexpr int32 RarityCount = static_cast<int32>(ECataclysmRarity::Cataclysmic) + 1;

	bool IsAQuantity(ECataclysmLootKind Kind)
	{
		return Kind == ECataclysmLootKind::Material || Kind == ECataclysmLootKind::Gold
			|| Kind == ECataclysmLootKind::Gem;
	}

	/** Whether the drop has a rarity a rarity condition can read: gear and gems. */
	bool HasARarity(ECataclysmLootKind Kind)
	{
		return Kind == ECataclysmLootKind::Gear || Kind == ECataclysmLootKind::Gem;
	}
}

FLinearColor UCataclysmLootFilterRules::PaletteColour(int32 Index, const UDataTable* GearRarityTable)
{
	if (Index >= 0 && Index < RarityCount)
	{
		return GearRarityTable != nullptr
			? UCataclysmDropSpawner::ColourFor(GearRarityTable, static_cast<ECataclysmRarity>(Index))
			: FLinearColor::White;
	}
	if (Index >= RarityCount && Index < PaletteSize)
	{
		return ExtraColours[Index - RarityCount];
	}
	return FLinearColor::White;
}

ECataclysmRarity UCataclysmLootFilterRules::TierRarity(int32 DifficultyTier)
{
	// Section IV's tier table: the reference character wears the tier's own rarity, Everyday at tier 1.
	return static_cast<ECataclysmRarity>(FMath::Clamp(DifficultyTier, 1, RarityCount) - 1);
}

bool UCataclysmLootFilterRules::Matches(const FCataclysmLootFilterRule& Rule, const FCataclysmLootFacts& Facts,
								   const FCataclysmLootContext& Context)
{
	if (Rule.Kind != ECataclysmLootKind::Any && Rule.Kind != Facts.Kind)
	{
		return false;
	}

	const bool bGear = Facts.Kind == ECataclysmLootKind::Gear;

	// RARITY: gear's or a gem's. A material and gold have none, so a rule that names a rarity never matches them.
	if (Rule.Rarities.Num() > 0 && (!HasARarity(Facts.Kind) || !Rule.Rarities.Contains(Facts.Rarity)))
	{
		return false;
	}

	if (Rule.RungsBelowTierAtLeast > 0)
	{
		const int32 Below = static_cast<int32>(TierRarity(Context.DifficultyTier)) - static_cast<int32>(Facts.Rarity);
		if (!HasARarity(Facts.Kind) || Below < Rule.RungsBelowTierAtLeast)
		{
			return false;
		}
	}

	// THE OWNER'S RULE: strictly below what is worn in the slot this piece would fill. Nothing worn there, nothing
	// matched (the coordinating session's settlement: an empty slot hides nothing).
	if (Rule.bBelowWornRarity)
	{
		const ECataclysmRarity* Worn = bGear ? Context.WornRarity.Find(Facts.Slot) : nullptr;
		if (Worn == nullptr || static_cast<int32>(Facts.Rarity) >= static_cast<int32>(*Worn))
		{
			return false;
		}
	}

	// THE GEAR-ONLY CONDITIONS. Each one, when it narrows at all, matches only gear.
	if (Rule.Slots.Num() > 0 && (!bGear || !Rule.Slots.Contains(Facts.Slot)))
	{
		return false;
	}
	if ((Rule.MinGearLevel > 0 || Rule.MaxGearLevel < 10)
		&& (!bGear || Facts.GearLevel < Rule.MinGearLevel || Facts.GearLevel > Rule.MaxGearLevel))
	{
		return false;
	}
	if (Rule.Affixes.Num() > 0)
	{
		if (!bGear)
		{
			return false;
		}
		int32 Count = 0;
		for (const FCataclysmRolledAffix& Rolled : Facts.Affixes)
		{
			if (Rolled.Tier >= Rule.MinAffixTier && Rule.Affixes.Contains(Rolled.Affix))
			{
				++Count;
			}
		}
		if (Count < FMath::Max(1, Rule.MinAffixCount))
		{
			return false;
		}
	}
	if (Rule.HasEnchantment != ECataclysmLootTriState::Either)
	{
		const bool bHas = Facts.EnchantmentCount > 0;
		if (!bGear || bHas != (Rule.HasEnchantment == ECataclysmLootTriState::Yes))
		{
			return false;
		}
	}
	if ((Rule.MinSockets > 0 || Rule.MaxSockets < 6)
		&& (!bGear || Facts.SocketCount < Rule.MinSockets || Facts.SocketCount > Rule.MaxSockets))
	{
		return false;
	}
	if (Rule.DamageTypes.Num() > 0)
	{
		bool bAny = false;
		for (const FName& Type : Facts.DamageTypes)
		{
			bAny |= Rule.DamageTypes.Contains(Type);
		}
		if (!bGear || !bAny)
		{
			return false;
		}
	}

	// A MATERIAL'S TIER narrows only materials, as its name says: a rule about tiers 3 to 5 is about materials.
	if ((Rule.MinMaterialTier > 1 || Rule.MaxMaterialTier < 5)
		&& (Facts.Kind != ECataclysmLootKind::Material || Facts.MaterialTier < Rule.MinMaterialTier
			|| Facts.MaterialTier > Rule.MaxMaterialTier))
	{
		return false;
	}

	return true;
}

FCataclysmLootVerdict UCataclysmLootFilterRules::VerdictFor(const FCataclysmLootFilter& Filter,
														const FCataclysmLootFacts& Facts,
														const FCataclysmLootContext& Context)
{
	FCataclysmLootVerdict Verdict;
	const int32 Read = FMath::Min(Filter.Rules.Num(), MaxRules);
	for (int32 Index = 0; Index < Read; ++Index)
	{
		const FCataclysmLootFilterRule& Rule = Filter.Rules[Index];
		if (!Matches(Rule, Facts, Context))
		{
			continue;
		}

		// THE HIGHEST RULE THAT MATCHES DECIDES, and nothing below it is read.
		Verdict.Rule = Index;
		if (Rule.Action == ECataclysmLootFilterAction::Hide)
		{
			// A QUANTITY IS NEVER HIDDEN: it is collected whether named or not. Shown unstyled.
			Verdict.bHidden = !IsAQuantity(Facts.Kind);
		}
		else if (Rule.Action == ECataclysmLootFilterAction::Restyle)
		{
			Verdict.TextColour = Rule.TextColour >= 0 && Rule.TextColour < PaletteSize ? Rule.TextColour : INDEX_NONE;
			Verdict.BackgroundColour = Rule.BackgroundColour >= 0 && Rule.BackgroundColour < PaletteSize
				? Rule.BackgroundColour
				: INDEX_NONE;
			Verdict.Scale = Sizes[FMath::Clamp(Rule.Size, 0, SizeCount - 1)];
		}
		return Verdict;
	}
	return Verdict;
}

TMap<FString, ECataclysmRarity> UCataclysmLootFilterRules::WornRaritiesOf(const TArray<const FCataclysmItem*>& Worn,
																	  bool bTwoHandedInBothHands)
{
	TMap<FString, ECataclysmRarity> Out;

	TSet<FString> BaseSlots;
	for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
	{
		BaseSlots.Add(UCataclysmGearSlots::BaseSlotFor(Slot));
	}

	for (const FString& BaseSlot : BaseSlots)
	{
		TArray<ECataclysmGearSlot> Candidates = UCataclysmGearSlots::CandidateSlotsFor(BaseSlot);

		// ONE TWO-HANDED WEAPON FILLS BOTH HANDS, and the engine stores it in one weapon slot and leaves the other empty
		// and blocked. Only the slot that holds it is read, so its rarity decides rather than the empty hand.
		if (BaseSlot == TEXT("Weapon") && bTwoHandedInBothHands)
		{
			Candidates.RemoveAll([&Worn](ECataclysmGearSlot Slot)
			{
				const int32 Index = static_cast<int32>(Slot);
				return !Worn.IsValidIndex(Index) || Worn[Index] == nullptr;
			});
		}

		bool bEveryCandidateWorn = Candidates.Num() > 0;
		int32 Lowest = RarityCount;
		for (const ECataclysmGearSlot Slot : Candidates)
		{
			const int32 Index = static_cast<int32>(Slot);
			const FCataclysmItem* Item = Worn.IsValidIndex(Index) ? Worn[Index] : nullptr;
			ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
			if (Item == nullptr || !UCataclysmItemValues::RarityOf(Item->EnchantmentCount, Item->Affixes.Num(), Rarity))
			{
				// AN EMPTY SLOT HIDES NOTHING, and for rings and weapons one empty slot is enough: the lowest of what is
				// worn there is "nothing".
				bEveryCandidateWorn = false;
				break;
			}
			Lowest = FMath::Min(Lowest, static_cast<int32>(Rarity));
		}
		if (bEveryCandidateWorn)
		{
			Out.Add(BaseSlot, static_cast<ECataclysmRarity>(Lowest));
		}
	}
	return Out;
}

FCataclysmLootContext UCataclysmLootFilterRules::ContextOf(const AActor* Character)
{
	FCataclysmLootContext Context;
	if (Character == nullptr)
	{
		return Context;
	}
	Context.DifficultyTier = ACataclysmGameMode::DifficultyTierIn(Character);

	if (const UCataclysmEquipmentComponent* Equipment = Character->FindComponentByClass<UCataclysmEquipmentComponent>())
	{
		TArray<const FCataclysmItem*> Worn;
		Worn.Init(nullptr, UCataclysmEquipmentComponent::SlotCount);
		for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
		{
			Worn[static_cast<int32>(Slot)] = Equipment->EquippedAt(Slot);
		}
		Context.WornRarity = WornRaritiesOf(Worn, Equipment->TwoHandedOccupiesBothWeaponSlots());
	}
	return Context;
}

FCataclysmLootFacts UCataclysmLootFilterRules::FactsOf(const ACataclysmDroppedItem& Drop, const UDataTable* BaseTable)
{
	FCataclysmLootFacts Facts;
	if (Drop.IsGold())
	{
		Facts.Kind = ECataclysmLootKind::Gold;
		return Facts;
	}
	if (Drop.IsGem())
	{
		Facts.Kind = ECataclysmLootKind::Gem;
		Facts.Rarity = Drop.GemRarity;
		return Facts;
	}
	if (Drop.IsMaterial())
	{
		Facts.Kind = ECataclysmLootKind::Material;
		Facts.MaterialTier = Drop.MaterialTier;
		return Facts;
	}

	const FCataclysmItem& Item = Drop.Item;
	Facts.Kind = ECataclysmLootKind::Gear;
	// THE RARITY FROM THE ITEM ITSELF, by the definition of rarity, rather than the drop's `Rarity` field, which only
	// a described drop has set.
	UCataclysmItemValues::RarityOf(Item.EnchantmentCount, Item.Affixes.Num(), Facts.Rarity);
	Facts.Slot = UCataclysmForge::SlotOf(Item, BaseTable);
	Facts.GearLevel = Item.GearLevel;
	Facts.Affixes = Item.Affixes;
	Facts.EnchantmentCount = Item.EnchantmentCount;
	Facts.SocketCount = Item.Sockets.Num();
	Facts.DamageTypes = Item.DamageTypes;
	return Facts;
}

void UCataclysmLootFilterRules::VisibleDrops(const TArray<ACataclysmDroppedItem*>& Named,
										const FCataclysmLootFilter& Filter, const FCataclysmLootContext& Context,
										bool bRevealHidden, const UDataTable* BaseTable,
										TArray<ACataclysmDroppedItem*>& OutDrops,
										TArray<FCataclysmLootVerdict>& OutVerdicts)
{
	OutDrops.Reset();
	OutVerdicts.Reset();
	for (ACataclysmDroppedItem* Drop : Named)
	{
		if (!IsValid(Drop))
		{
			continue;
		}
		const FCataclysmLootVerdict Verdict = VerdictFor(Filter, FactsOf(*Drop, BaseTable), Context);
		if (Verdict.bHidden && !bRevealHidden)
		{
			continue;
		}
		OutDrops.Add(Drop);
		OutVerdicts.Add(Verdict);
	}
}

FCataclysmLootFilter UCataclysmLootFilterRules::Everything()
{
	FCataclysmLootFilter Filter;
	Filter.Name = TEXT("Everything");
	return Filter;
}

FCataclysmLootFilter UCataclysmLootFilterRules::HideWorseThanWorn()
{
	FCataclysmLootFilter Filter;
	Filter.Name = DefaultFilterName;

	FCataclysmLootFilterRule Rule;
	Rule.Action = ECataclysmLootFilterAction::Hide;
	Rule.Kind = ECataclysmLootKind::Gear;
	Rule.bBelowWornRarity = true;
	Filter.Rules.Add(Rule);
	return Filter;
}

FCataclysmLootFilter UCataclysmLootFilterRules::Strict()
{
	FCataclysmLootFilter Filter = HideWorseThanWorn();
	Filter.Name = TEXT("Strict");

	FCataclysmLootFilterRule Rule;
	Rule.Action = ECataclysmLootFilterAction::Hide;
	Rule.Kind = ECataclysmLootKind::Gear;
	Rule.Rarities = {ECataclysmRarity::Everyday, ECataclysmRarity::Quality};
	Filter.Rules.Add(Rule);
	return Filter;
}

bool UCataclysmLootFilterRules::BuiltInNamed(const FString& Name, FCataclysmLootFilter& OutFilter)
{
	const TArray<FCataclysmLootFilter> BuiltIns = {Everything(), HideWorseThanWorn(), Strict()};
	for (const FCataclysmLootFilter& Candidate : BuiltIns)
	{
		if (Candidate.Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			OutFilter = Candidate;
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Cataclysm.LootFilter. Issue #2846: makes a filter active, a built-in or one of the player's own, and writes the
// preferences file (slice LF2). With no name it says which is active and lists every filter.
// ---------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmLootFilter(
	TEXT("Cataclysm.LootFilter"),
	TEXT("Choose the loot filter the floor's names are drawn through: Cataclysm.LootFilter <name>, a built-in "
		 "(Everything, Hide worse than worn, Strict) or one of your own. With no name, lists them. Hold Alt to show what "
		 "it hides; L opens the screen that edits them."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			UCataclysmGameInstance* Instance = UCataclysmGameInstance::In(World);
			if (Instance == nullptr)
			{
				Ar.Log(TEXT("No game instance of this project's to hold a loot filter."));
				return;
			}
			FCataclysmLootFilterPreferences& Preferences = Instance->LootFilterPreferences();
			if (Args.Num() == 0)
			{
				Ar.Logf(TEXT("The loot filter is \"%s\". The filters are: %s."),
						*UCataclysmLootFilterStore::ActiveFilter(Preferences).Name,
						*FString::Join(UCataclysmLootFilterStore::AllNames(Preferences), TEXT(", ")));
				return;
			}
			// A NAME WITH SPACES ARRIVES AS SEVERAL ARGUMENTS, so they are joined again.
			const FString Name = FString::Join(Args, TEXT(" "));
			FString Refusal;
			if (!UCataclysmLootFilterStore::SetActive(Preferences, Name, Refusal))
			{
				Ar.Logf(TEXT("%s (\"%s\")"), *Refusal, *Name);
				return;
			}
			Instance->SaveLootFilterPreferences();
			Ar.Logf(TEXT("The loot filter is now \"%s\"."), *UCataclysmLootFilterStore::ActiveFilter(Preferences).Name);
		}));

// ---------------------------------------------------------------------------
// Cataclysm.EditLootFilters. Issue #2846, slice LF2: opens and closes the loot filter screen, beside its key L, so a
// checkout whose input assets predate the key can still open it -- the skill-choice screen's reason for its command.
// ---------------------------------------------------------------------------

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmEditLootFilters(
	TEXT("Cataclysm.EditLootFilters"),
	TEXT("Open or close the loot filter screen: choose the filter in use, and make, copy, rename, delete and edit your "
		 "own. L opens it too."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			ACataclysmPlayerController* Controller =
				Cast<ACataclysmPlayerController>(World ? World->GetFirstPlayerController() : nullptr);
			if (!Controller)
			{
				Ar.Log(TEXT("There is no Cataclysm player controller, so there is nothing to open a screen on."));
				return;
			}
			Controller->ToggleLootFilterScreen();
		}));
