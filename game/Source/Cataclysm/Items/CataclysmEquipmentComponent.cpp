// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmEquipmentComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Items/CataclysmDropRoll.h"
#include "Engine/DataTable.h"

// ---------------------------------------------------------------------------
// Which slots exist, and what may go in them
// ---------------------------------------------------------------------------

const TArray<ECataclysmGearSlot>& UCataclysmGearSlots::AllSlots()
{
	// BUILT FROM Count RATHER THAN LISTED, so adding a slot to the enum reaches
	// every loop in the project without anybody editing a second list.
	static const TArray<ECataclysmGearSlot> Slots = []
	{
		TArray<ECataclysmGearSlot> Built;
		for (int32 Index = 0; Index < static_cast<int32>(ECataclysmGearSlot::Count); ++Index)
		{
			Built.Add(static_cast<ECataclysmGearSlot>(Index));
		}
		return Built;
	}();
	return Slots;
}

const TArray<ECataclysmGearSlot>& UCataclysmGearSlots::RingSlots()
{
	static const TArray<ECataclysmGearSlot> Slots = {
		ECataclysmGearSlot::Ring1, ECataclysmGearSlot::Ring2,
		ECataclysmGearSlot::Ring3, ECataclysmGearSlot::Ring4,
		ECataclysmGearSlot::Ring5, ECataclysmGearSlot::Ring6,
		ECataclysmGearSlot::Ring7, ECataclysmGearSlot::Ring8,
	};
	return Slots;
}

const TArray<ECataclysmGearSlot>& UCataclysmGearSlots::WeaponSlots()
{
	static const TArray<ECataclysmGearSlot> Slots = {
		ECataclysmGearSlot::Weapon1, ECataclysmGearSlot::Weapon2,
	};
	return Slots;
}

bool UCataclysmGearSlots::IsRingSlot(ECataclysmGearSlot Slot)
{
	return RingSlots().Contains(Slot);
}

bool UCataclysmGearSlots::IsWeaponSlot(ECataclysmGearSlot Slot)
{
	return WeaponSlots().Contains(Slot);
}

FString UCataclysmGearSlots::DisplayName(ECataclysmGearSlot Slot)
{
	// READ OFF THE ENUM'S OWN DisplayName META rather than a second table, so
	// the name on screen and the name in the editor's dropdown cannot disagree.
	if (const UEnum* Enum = StaticEnum<ECataclysmGearSlot>())
	{
		return Enum->GetDisplayNameTextByValue(static_cast<int64>(Slot)).ToString();
	}
	return FString();
}

FString UCataclysmGearSlots::BaseSlotFor(ECataclysmGearSlot Slot)
{
	if (IsRingSlot(Slot))
	{
		return TEXT("Ring");
	}
	if (IsWeaponSlot(Slot))
	{
		return TEXT("Weapon");
	}

	// EVERY OTHER SLOT'S NAME IS ITS OWN BASE VALUE, which is true of all nine
	// of them today and is checked by
	// Cataclysm.Equipment.EverySlotNameMatchesAnItemBaseSlotValue rather than
	// trusted. A slot added to the enum whose name is not a value in the Slot
	// column of game/Data/ItemBases.csv fails that test.
	if (const UEnum* Enum = StaticEnum<ECataclysmGearSlot>())
	{
		return Enum->GetNameStringByValue(static_cast<int64>(Slot));
	}
	return FString();
}

TArray<ECataclysmGearSlot> UCataclysmGearSlots::CandidateSlotsFor(const FString& BaseSlot)
{
	if (BaseSlot.IsEmpty())
	{
		return {};
	}

	if (BaseSlot.Equals(TEXT("Ring"), ESearchCase::IgnoreCase))
	{
		return RingSlots();
	}
	if (BaseSlot.Equals(TEXT("Weapon"), ESearchCase::IgnoreCase))
	{
		return WeaponSlots();
	}

	for (const ECataclysmGearSlot Slot : AllSlots())
	{
		if (BaseSlotFor(Slot).Equals(BaseSlot, ESearchCase::IgnoreCase))
		{
			return {Slot};
		}
	}

	// AN EMPTY ANSWER IS NOT AN ERROR AND IS NOT LOGGED. A consumable's base
	// names a slot this enum deliberately does not carry -- see the header on
	// why the four potion slots are absent -- and a caller asking about one
	// should get "nowhere", not a warning.
	return {};
}

// ---------------------------------------------------------------------------
// The component
// ---------------------------------------------------------------------------

UCataclysmEquipmentComponent::UCataclysmEquipmentComponent()
{
	// NOTHING TICKS, for the same reason the carried inventory does not: what a
	// character is wearing changes when something puts an item on or takes one
	// off, and neither is a frame event.
	PrimaryComponentTick.bCanEverTick = false;

	// Every slot exists from the start, empty, so an empty one is representable
	// and the interface can draw all of them.
	Slots.SetNum(SlotCount);
}

const FCataclysmItem* UCataclysmEquipmentComponent::EquippedAt(ECataclysmGearSlot Slot) const
{
	const int32 Index = static_cast<int32>(Slot);
	if (!Slots.IsValidIndex(Index) || Slots[Index].Base.IsNone())
	{
		return nullptr;
	}
	return &Slots[Index];
}

bool UCataclysmEquipmentComponent::SlotIsEmpty(ECataclysmGearSlot Slot) const
{
	return EquippedAt(Slot) == nullptr;
}

int32 UCataclysmEquipmentComponent::NumEquipped() const
{
	int32 Count = 0;
	for (const FCataclysmItem& Item : Slots)
	{
		if (!Item.Base.IsNone())
		{
			++Count;
		}
	}
	return Count;
}

bool UCataclysmEquipmentComponent::TwoHandedOccupiesBothWeaponSlots() const
{
	const FCataclysmItem* First = EquippedAt(ECataclysmGearSlot::Weapon1);
	if (!First)
	{
		return false;
	}
	return UCataclysmItemModifiers::IsTwoHanded(
		*First, UCataclysmItemModifiers::LoadBaseTable());
}

// ---------------------------------------------------------------------------
// Changing what is worn
// ---------------------------------------------------------------------------

void UCataclysmEquipmentComponent::PlaceInto(const FCataclysmItem& Item,
											 ECataclysmGearSlot Slot,
											 FCataclysmItem& OutRemoved)
{
	const int32 Index = static_cast<int32>(Slot);
	if (!Slots.IsValidIndex(Index))
	{
		return;
	}

	OutRemoved = Slots[Index];
	Slots[Index] = Item;
}

ECataclysmEquipResult UCataclysmEquipmentComponent::Equip(
	const FCataclysmItem& Item,
	FCataclysmItem& OutRemoved,
	FCataclysmItem& OutAlsoRemoved,
	ECataclysmGearSlot& OutSlot)
{
	if (Item.Base.IsNone())
	{
		return ECataclysmEquipResult::NotAnItem;
	}

	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	if (!BaseTable)
	{
		return ECataclysmEquipResult::NoBaseTable;
	}

	const FCataclysmItemBaseRow* Base = BaseTable->FindRow<FCataclysmItemBaseRow>(
		Item.Base, TEXT("Equip"), /*bWarnIfMissing=*/false);
	if (!Base)
	{
		return ECataclysmEquipResult::NotAnItem;
	}

	const TArray<ECataclysmGearSlot> Candidates =
		UCataclysmGearSlots::CandidateSlotsFor(Base->Slot);
	if (Candidates.Num() == 0)
	{
		return ECataclysmEquipResult::NoSlotForThisItem;
	}

	// THE FIRST FREE ONE, AND THE FIRST ONE WHEN NONE IS FREE. With eight ring
	// slots, filling a free one rather than replacing an occupied one is what a
	// player means by "wear this"; replacing only becomes the answer once there
	// is nowhere left to put it. EquipInto is how a player says which.
	ECataclysmGearSlot Chosen = Candidates[0];
	for (const ECataclysmGearSlot Candidate : Candidates)
	{
		if (SlotIsEmpty(Candidate))
		{
			Chosen = Candidate;
			break;
		}
	}

	const ECataclysmEquipResult Result =
		EquipInto(Item, Chosen, OutRemoved, OutAlsoRemoved);

	// EquipInto sends a two-handed weapon to Weapon1 whichever weapon slot it
	// was given, so the slot reported back is read from the component rather
	// than from what was asked for.
	OutSlot = Chosen;
	if (UCataclysmGearSlots::IsWeaponSlot(Chosen)
		&& TwoHandedOccupiesBothWeaponSlots())
	{
		OutSlot = ECataclysmGearSlot::Weapon1;
	}
	return Result;
}

ECataclysmEquipResult UCataclysmEquipmentComponent::EquipInto(const FCataclysmItem& Item,
															   ECataclysmGearSlot Slot,
															   FCataclysmItem& OutRemoved,
															   FCataclysmItem& OutAlsoRemoved)
{
	if (Item.Base.IsNone())
	{
		return ECataclysmEquipResult::NotAnItem;
	}

	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	if (!BaseTable)
	{
		return ECataclysmEquipResult::NoBaseTable;
	}

	const FCataclysmItemBaseRow* Base = BaseTable->FindRow<FCataclysmItemBaseRow>(
		Item.Base, TEXT("EquipInto"), /*bWarnIfMissing=*/false);
	if (!Base)
	{
		return ECataclysmEquipResult::NotAnItem;
	}

	const TArray<ECataclysmGearSlot> Candidates =
		UCataclysmGearSlots::CandidateSlotsFor(Base->Slot);
	if (Candidates.Num() == 0)
	{
		return ECataclysmEquipResult::NoSlotForThisItem;
	}
	if (!Candidates.Contains(Slot))
	{
		return ECataclysmEquipResult::WrongSlotForThisItem;
	}

	const bool bWasEmpty = SlotIsEmpty(Slot);

	// -- a two-handed weapon takes both hands ------------------------------
	if (UCataclysmGearSlots::IsWeaponSlot(Slot)
		&& UCataclysmItemModifiers::IsTwoHanded(Item, BaseTable))
	{
		// IT LANDS IN Weapon1 WHICHEVER SLOT WAS ASKED FOR. The two weapon
		// slots are interchangeable -- the design says there is no primary hand
		// -- so a two-handed weapon in "the second hand" would mean nothing, and
		// storing it once is what stops every accumulation counting its affixes
		// twice.
		FCataclysmItem FromFirst;
		FCataclysmItem FromSecond;
		PlaceInto(Item, ECataclysmGearSlot::Weapon1, FromFirst);

		const int32 SecondIndex = static_cast<int32>(ECataclysmGearSlot::Weapon2);
		FromSecond = Slots[SecondIndex];
		Slots[SecondIndex] = FCataclysmItem();

		OutRemoved = FromFirst;
		OutAlsoRemoved = FromSecond;

		EquipmentChanged.Broadcast();
		return (FromFirst.Base.IsNone() && FromSecond.Base.IsNone())
			? ECataclysmEquipResult::Equipped
			: ECataclysmEquipResult::Swapped;
	}

	// -- a one-handed weapon may not go beside a two-handed one -------------
	if (UCataclysmGearSlots::IsWeaponSlot(Slot) && TwoHandedOccupiesBothWeaponSlots())
	{
		// THE TWO-HANDED WEAPON COMES OFF, whichever hand the new one was aimed
		// at. It occupies both, so there is no free hand to put anything in and
		// refusing would leave the player unable to change weapon without an
		// explicit unequip they have no reason to know about.
		FCataclysmItem Displaced;
		Unequip(ECataclysmGearSlot::Weapon1, Displaced);
		PlaceInto(Item, Slot, OutRemoved);
		OutRemoved = Displaced;

		EquipmentChanged.Broadcast();
		return ECataclysmEquipResult::Swapped;
	}

	PlaceInto(Item, Slot, OutRemoved);

	EquipmentChanged.Broadcast();
	return bWasEmpty ? ECataclysmEquipResult::Equipped
					 : ECataclysmEquipResult::Swapped;
}

bool UCataclysmEquipmentComponent::Unequip(ECataclysmGearSlot Slot,
										   FCataclysmItem& OutRemoved)
{
	const int32 Index = static_cast<int32>(Slot);
	if (!Slots.IsValidIndex(Index) || Slots[Index].Base.IsNone())
	{
		return false;
	}

	OutRemoved = Slots[Index];
	Slots[Index] = FCataclysmItem();

	EquipmentChanged.Broadcast();
	return true;
}

void UCataclysmEquipmentComponent::UnequipEverything()
{
	bool bAnything = false;
	for (FCataclysmItem& Item : Slots)
	{
		if (!Item.Base.IsNone())
		{
			Item = FCataclysmItem();
			bAnything = true;
		}
	}

	// BROADCAST ONLY IF SOMETHING CHANGED, so taking everything off a character
	// wearing nothing does not make every listener recompute for no reason.
	if (bAnything)
	{
		EquipmentChanged.Broadcast();
	}
}

// ---------------------------------------------------------------------------
// What it is all for
// ---------------------------------------------------------------------------

TMap<FName, TArray<FCataclysmStatModifier>> UCataclysmEquipmentComponent::GatherModifiers() const
{
	TMap<FName, TArray<FCataclysmStatModifier>> Totals;

	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	if (!BaseTable || !AffixTable)
	{
		// Nothing to say. An empty map means "wearing nothing that grants
		// anything", and a caller cannot tell that from "the tables are
		// missing" -- but the tables missing is already reported loudly by the
		// loaders themselves, so it is not reported twice here.
		return Totals;
	}

	for (const FCataclysmItem& Item : Slots)
	{
		if (Item.Base.IsNone())
		{
			continue;
		}
		UCataclysmItemModifiers::AccumulateInto(Totals, Item, BaseTable, AffixTable);
	}

	return Totals;
}

FString UCataclysmEquipmentComponent::EquippedWeaponType() const
{
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	if (!BaseTable)
	{
		return FString();
	}

	for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::WeaponSlots())
	{
		const FCataclysmItem* Item = EquippedAt(Slot);
		if (!Item)
		{
			continue;
		}

		const FCataclysmItemBaseRow* Base = BaseTable->FindRow<FCataclysmItemBaseRow>(
			Item->Base, TEXT("EquippedWeaponType"), /*bWarnIfMissing=*/false);
		if (Base && !Base->WeaponType.IsEmpty())
		{
			// THE FIRST OCCUPIED SLOT, AND THAT IS A PLACEHOLDER. With two
			// one-handed weapons of different types the answer depends on which
			// slot the player dropped which weapon into, and the two slots are
			// supposed to carry no meaning.
			//
			// THE ANSWER IS NOT TO PICK A BETTER WEAPON HERE. Both weapons
			// contribute their skills to one pool and the player assigns any of
			// them to any slot, which the design said before this was written.
			// Issue #837 is that work and this function goes with it.
			return Base->WeaponType;
		}
	}

	return FString();
}

int32 UCataclysmEquipmentComponent::RefreshAttributes(
	UAbilitySystemComponent* AbilitySystem) const
{
	if (!AbilitySystem)
	{
		return 0;
	}

	const TMap<FName, TArray<FCataclysmStatModifier>> Modifiers = GatherModifiers();

	return UCataclysmPlayerClassStats::ApplyTo(
		AbilitySystem,
		UCataclysmPlayerClassStats::LoadTable(),
		UCataclysmPlayerClassStats::ChosenClass(),
		UCataclysmPlayerClassStats::ChosenLevel(),
		&Modifiers,
		// THE POOLS ARE LEFT WHERE THEY ARE. Filling them is right for a
		// character arriving in the world and wrong here: a player who swapped a
		// helmet during a fight would be healed to full by doing it.
		ECataclysmPoolFill::LeaveAsTheyAre);
}
