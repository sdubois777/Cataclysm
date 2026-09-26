// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmEquipmentComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Character/CataclysmPassiveTree.h"
#include "Player/CataclysmPlayerState.h"
#include "GameFramework/Pawn.h"
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
	if (!First || MayHoldTwoTwoHanded())
	{
		return false;
	}
	return UCataclysmItemModifiers::IsTwoHanded(
		*First, UCataclysmItemModifiers::LoadBaseTable());
}

const TCHAR* UCataclysmEquipmentComponent::BothHandsFullStat =
	TEXT("two_handed_weapon_in_each_hand");

bool UCataclysmEquipmentComponent::MayHoldTwoTwoHanded() const
{
	const UCataclysmAbilitySystemComponent* Holder =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(GetOwner()));
	return Holder
		&& Holder->StatForSkill(FName(BothHandsFullStat), FGameplayTagContainer(),
								0.0f) > 0.0f;
}

int32 UCataclysmEquipmentComponent::WeaponsComingOffFor(bool bTwoHandedWeapon) const
{
	const FCataclysmItem* First = EquippedAt(ECataclysmGearSlot::Weapon1);
	const FCataclysmItem* Second = EquippedAt(ECataclysmGearSlot::Weapon2);
	if (!First && !Second)
	{
		return 0;
	}

	// WHETHER THE NEW WEAPON MAY STAND BESIDE A HELD ONE. The same kind, and a
	// two-handed pair only with the option. EVERY LEGAL LOADOUT HOLDS ONE KIND,
	// so with both hands held the first one says what the pair is.
	const FCataclysmItem* Held = First ? First : Second;
	const bool bHeldIsTwoHanded = UCataclysmItemModifiers::IsTwoHanded(
		*Held, UCataclysmItemModifiers::LoadBaseTable());
	const bool bFitsBeside = bTwoHandedWeapon == bHeldIsTwoHanded
		&& (!bTwoHandedWeapon || MayHoldTwoTwoHanded());

	// ONE HAND FREE: `Equip` puts it there, and the held weapon comes off only
	// when the two cannot stand together. BOTH HELD: `Equip` replaces the first,
	// and the second comes off too when the two cannot stand together.
	if (!First || !Second)
	{
		return bFitsBeside ? 0 : 1;
	}
	return bFitsBeside ? 1 : 2;
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
	// was given, unless Both Hands Full is held, when it goes where it was put.
	// So the slot reported back follows the same rule rather than the slot
	// asked for.
	OutSlot = Chosen;
	if (UCataclysmGearSlots::IsWeaponSlot(Chosen)
		&& UCataclysmItemModifiers::IsTwoHanded(Item, BaseTable)
		&& !MayHoldTwoTwoHanded())
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
	const bool bTwoHandedWeapon = UCataclysmGearSlots::IsWeaponSlot(Slot)
		&& UCataclysmItemModifiers::IsTwoHanded(Item, BaseTable);
	const ECataclysmGearSlot OtherHand = Slot == ECataclysmGearSlot::Weapon1
		? ECataclysmGearSlot::Weapon2 : ECataclysmGearSlot::Weapon1;

	// -- with Both Hands Full, a two-handed weapon takes one hand ---------
	if (bTwoHandedWeapon && MayHoldTwoTwoHanded())
	{
		// WHERE IT WAS PUT, and a two-handed weapon in the other hand stays.
		// Issue #1515. A ONE-HANDED WEAPON IN THE OTHER HAND COMES OFF, because
		// the design's four loadouts never mix the two kinds.
		FCataclysmItem FromThisHand;
		FCataclysmItem FromOtherHand;
		PlaceInto(Item, Slot, FromThisHand);
		const FCataclysmItem* Beside = EquippedAt(OtherHand);
		if (Beside && !UCataclysmItemModifiers::IsTwoHanded(*Beside, BaseTable))
		{
			TakeOutOf(OtherHand, FromOtherHand);
		}

		// THE FIRST THING THAT CAME OFF IS REPORTED FIRST, whichever hand it
		// left, and the second is written only when there is one, as the
		// header promises.
		if (FromThisHand.Base.IsNone())
		{
			OutRemoved = FromOtherHand;
		}
		else
		{
			OutRemoved = FromThisHand;
			if (!FromOtherHand.Base.IsNone())
			{
				OutAlsoRemoved = FromOtherHand;
			}
		}

		AnnounceChange();
		return (FromThisHand.Base.IsNone() && FromOtherHand.Base.IsNone())
			? ECataclysmEquipResult::Equipped
			: ECataclysmEquipResult::Swapped;
	}

	// -- a two-handed weapon takes both hands ------------------------------
	if (bTwoHandedWeapon)
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

		AnnounceChange();
		return (FromFirst.Base.IsNone() && FromSecond.Base.IsNone())
			? ECataclysmEquipResult::Equipped
			: ECataclysmEquipResult::Swapped;
	}

	// -- a one-handed weapon may not go beside a two-handed one -------------
	//
	// ASKED OF THE OTHER HAND, NOT OF Weapon1. Issue #1515. Without Both Hands
	// Full a two-handed weapon is only ever in Weapon1, and this reads the same
	// as it always did. With it, one may be in Weapon2, and two may be held; a
	// one-handed weapon put in either hand then takes off what is in that hand
	// by being placed there, and the two-handed weapon in the other hand here.
	// A one-handed weapon put in the two-handed weapon's own hand needs no
	// branch: the placement below takes it off, as any swap in one slot does.
	//
	// THE TWO-HANDED WEAPON COMES OFF rather than refusing, which would leave
	// the player unable to change weapon without an explicit unequip they have
	// no reason to know about.
	//
	// TakeOutOf AND NOT Unequip, WHICH IS THE WHOLE OF ISSUE #1214. Unequip
	// announces, so this branch used to raise the change twice for one player
	// action -- and the first of the two fired between the two-handed weapon
	// coming off and the new one going on, with the character holding nothing.
	// ACataclysmPlayerCharacter::OnEquipmentChanged is the only listener and it
	// takes back and regrants every ability from the worn weapon's type, so it
	// did that once against an unarmed character and once against the real one.
	const FCataclysmItem* InOtherHand = UCataclysmGearSlots::IsWeaponSlot(Slot)
		? EquippedAt(OtherHand) : nullptr;
	if (InOtherHand && UCataclysmItemModifiers::IsTwoHanded(*InOtherHand, BaseTable))
	{
		FCataclysmItem Beside;
		FCataclysmItem FromThisHand;
		TakeOutOf(OtherHand, Beside);
		PlaceInto(Item, Slot, FromThisHand);
		OutRemoved = Beside;
		// ONLY WHEN SOMETHING CAME OFF THIS HAND TOO, which is two two-handed
		// weapons held. The header promises it untouched otherwise.
		if (!FromThisHand.Base.IsNone())
		{
			OutAlsoRemoved = FromThisHand;
		}

		AnnounceChange();
		return ECataclysmEquipResult::Swapped;
	}

	PlaceInto(Item, Slot, OutRemoved);

	AnnounceChange();
	return bWasEmpty ? ECataclysmEquipResult::Equipped
					 : ECataclysmEquipResult::Swapped;
}

void UCataclysmEquipmentComponent::AnnounceChange()
{
	// THE COUNT FIRST. A listener on the delegate may read ChangeCount while
	// handling the broadcast, and it should see the change it is being told
	// about rather than the one before it.
	++Changes;
	EquipmentChanged.Broadcast();
}

bool UCataclysmEquipmentComponent::TakeOutOf(ECataclysmGearSlot Slot,
											 FCataclysmItem& OutRemoved)
{
	const int32 Index = static_cast<int32>(Slot);
	if (!Slots.IsValidIndex(Index) || Slots[Index].Base.IsNone())
	{
		return false;
	}

	OutRemoved = Slots[Index];
	Slots[Index] = FCataclysmItem();
	return true;
}

bool UCataclysmEquipmentComponent::Unequip(ECataclysmGearSlot Slot,
										   FCataclysmItem& OutRemoved)
{
	// TAKING A WEAPON OFF ON ITS OWN IS STILL ONE CHANGE AND IS STILL
	// ANNOUNCED. What moved into TakeOutOf above is only the part that
	// EquipInto's two-handed swap needs to do silently, mid-swap.
	if (!TakeOutOf(Slot, OutRemoved))
	{
		return false;
	}

	AnnounceChange();
	return true;
}

bool UCataclysmEquipmentComponent::NoteKillOnWornWeapons(UAbilitySystemComponent* AbilitySystem)
{
	const UDataTable* Effects = UCataclysmItemModifiers::LoadEnchantmentEffectTable();
	bool bCrossed = false;
	for (const ECataclysmGearSlot Slot : {ECataclysmGearSlot::Weapon1, ECataclysmGearSlot::Weapon2})
	{
		const int32 Index = static_cast<int32>(Slot);
		if (!Slots.IsValidIndex(Index) || Slots[Index].Base.IsNone()
			|| Slots[Index].Kills >= MAX_int32)
		{
			continue;
		}
		FCataclysmItem& Weapon = Slots[Index];
		bCrossed |= UCataclysmItemModifiers::KillCrossesAStep(Weapon, Weapon.Kills, Effects);
		++Weapon.Kills;
	}
	if (bCrossed && AbilitySystem)
	{
		RefreshAttributes(AbilitySystem);
	}
	return bCrossed;
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
		AnnounceChange();
	}
}

// ---------------------------------------------------------------------------
// What it is all for
// ---------------------------------------------------------------------------

TMap<FName, TArray<FCataclysmStatModifier>>
UCataclysmEquipmentComponent::GatherModifiers(
	TArray<FCataclysmPoolAction>* Actions) const
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

	// A SLOT A FLOOR RULE HAS SWITCHED OFF IS READ AS EMPTY, by this loop and by the
	// enchantments below, so its item gives no stat and no enchantment and is not a piece
	// of any set. A copy is made only when a slot is off and holds something, which is
	// one floor rule's case (`Famine_Scarcity`; issues #1820 and #41).
	const TArray<FCataclysmItem>* Worn = &Slots;
	TArray<FCataclysmItem> Counted;
	const int32 Off = static_cast<int32>(DisabledSlot);
	if (Slots.IsValidIndex(Off) && !Slots[Off].Base.IsNone())
	{
		Counted = Slots;
		Counted[Off] = FCataclysmItem();
		Worn = &Counted;
	}

	for (const FCataclysmItem& Item : *Worn)
	{
		if (Item.Base.IsNone())
		{
			continue;
		}
		UCataclysmItemModifiers::AccumulateInto(Totals, Item, BaseTable, AffixTable);
	}

	// AND WHAT THE WORN ENCHANTMENTS GRANT, since issue #45. After the loop
	// rather than inside it, because a benefit applies once however many pieces
	// carry it, and only a pass over every worn item can know that. Before this,
	// a piece recorded its enchantments and none of them changed anything.
	UCataclysmItemModifiers::AccumulateEnchantmentsInto(
		Totals, *Worn, UCataclysmItemModifiers::LoadEnchantmentEffectTable(),
		UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
		UCataclysmDropRoll::LoadNegativeEnchantmentTable(), Actions);

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

TArray<FCataclysmItem> UCataclysmEquipmentComponent::WornWeapons() const
{
	TArray<FCataclysmItem> Worn;

	// NO TABLE IS CONSULTED HERE. Whether a weapon arms, what it is worth and
	// how fast it swings are all questions about the base, and they belong to
	// UCataclysmItemModifiers. This answers only what is in the two slots, so a
	// missing item bases table cannot make a worn weapon disappear.
	for (const ECataclysmGearSlot HeldWeaponSlot : UCataclysmGearSlots::WeaponSlots())
	{
		if (const FCataclysmItem* Item = EquippedAt(HeldWeaponSlot))
		{
			Worn.Add(*Item);
		}
	}

	return Worn;
}

TMap<FName, float> UCataclysmEquipmentComponent::StatBasesFromWeapons() const
{
	// ALWAYS WRITTEN, EVEN WHEN IT IS ZERO. Leaving either entry out would
	// leave its attribute holding whatever the last weapon set, so a character
	// who ended up holding nothing would keep swinging and keep critically
	// striking.
	//
	// CRITICAL STRIKE CHANCE JOINED THESE IN ISSUE #894. The design gives it to
	// the skill being used rather than to the character -- "A character has no
	// critical strike chance in the abstract" -- and every skill in the game
	// takes the default, so what a character holds is the only question, and
	// this component is what knows the answer. Before that,
	// UCataclysmWeaponSlotsComponent::ApplyBaseCritChance SET the attribute, so
	// the four affixes naming critical strike chance could not have scaled it
	// even if the map had let them through.
	//
	// THE SKILL'S DEFAULT AND NOT THE WEAPON'S, which is why the constant
	// belongs to the weapon slots component: it is the figure every one of the
	// six granted skills uses because no skill row can state its own. Issue
	// #657.
	const bool bHoldsAWeapon = WornWeapons().Num() > 0;

	return {
		{ FName(UCataclysmItemModifiers::AttackSpeedStat),
		  UCataclysmItemModifiers::BlendedAttackSpeed(
			  WornWeapons(), UCataclysmItemModifiers::LoadBaseTable()) },
		{ FName(UCataclysmItemModifiers::CritChanceStat),
		  bHoldsAWeapon
			  ? UCataclysmWeaponSlotsComponent::DefaultSkillCritChancePercent
			  : 0.0f },
	};
}

int32 UCataclysmEquipmentComponent::RefreshAttributes(
	UAbilitySystemComponent* AbilitySystem, ECataclysmPoolFill PoolFill) const
{
	if (!AbilitySystem)
	{
		return 0;
	}

	// NOT CONST ANY MORE, because the passive tree adds to it below. What
	// the worn items grant and what the spent passive points grant are the
	// same three buckets applied to the same character, so they belong in
	// one map rather than in two that a caller has to merge.
	// AND WHAT THE WORN ITEMS DO WHEN AN EVENT HAPPENS, gathered on the same
	// walk and handed to the same component. Issue #1815. Refreshed here rather
	// than read at the moment of an event, for the reason the stat line is: a
	// character that swapped a helmet must act on what it is wearing now, and
	// nothing should walk a DataTable on every block.
	TArray<FCataclysmPoolAction> Actions;
	TMap<FName, TArray<FCataclysmStatModifier>> Modifiers =
		GatherModifiers(&Actions);
	if (UCataclysmAbilitySystemComponent* Cataclysm =
			Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		// WHOLESALE, NOT MERGED, for the reason `SetStatInputs` is: a row that
		// came off with the gear must stop firing rather than keep firing.
		Cataclysm->SetPoolActions(MoveTemp(Actions));
	}
	TMap<FName, float> Bases = StatBasesFromWeapons();

	// AND THE EIGHT ATTRIBUTES, FOR THE SAME REASON THE OTHER CALLER DOES IT.
	// Leaving them out here would have made swapping a helmet reset every
	// attribute to zero, because a base override that is absent falls back to
	// the class line and no class line names an attribute. Issue #50.
	//
	// FOUND THROUGH THE OWNING ACTOR rather than passed in, because this
	// component is asked to refresh from several places and a parameter would
	// have to be threaded through all of them.
	// AND THE LEVEL COMES FROM THE SAME PLACE, for the same reason. This used to
	// read `Cataclysm.PlayerLevel` directly, which cannot see a level the
	// character has gained. Levelling up re-runs this, so it has to resolve at
	// the level the character now is. `GetCharacterLevel` falls back to that
	// console variable until a level is earned or loaded. Issue #50.
	int32 Level = UCataclysmPlayerClassStats::ChosenLevel();
	if (const AActor* Owner = GetOwner())
	{
		if (const APawn* Pawn = Cast<APawn>(Owner))
		{
			if (const ACataclysmPlayerState* State =
					Pawn->GetPlayerState<ACataclysmPlayerState>())
			{
				UCataclysmPlayerClassStats::MergeAttributeBases(
					State->GetSpentAttributePoints(), Bases);
				Level = State->GetCharacterLevel();

				// AND THE PASSIVE TREE, since 2026-08-25. Issue #50.
				//
				// HERE, WHERE THE WORN ITEMS' MODIFIERS ALREADY ARE, because a
				// passive node is another authored source of the same three
				// buckets. Adding it anywhere else would mean a second place
				// that has to be re-run whenever anything changes.
				//
				// THE DAMAGE TYPE DECIDES WHICH TREES COUNT. Points in a tree no
				// equipped weapon reaches stay spent and add nothing, which is
				// the project owner's decision of 2026-08-25.
				const TArray<FName> Carried = {State->GetChosenDamageType()};
				UCataclysmPassiveTree::AccumulateInto(
					Modifiers, State->GetPassiveAllocation(),
					UCataclysmPassiveTree::LoadNodeTable(),
					UCataclysmPassiveTree::LoadEffectTable(), Carried);
			}
		}
	}

	// AND THE DUNGEON FLOOR BEING STOOD ON, since issue #41. Starvation takes a
	// share of maximum health and shield and Dehydration a share of maximum
	// mana, each as a Less multiplier on the finished figure.
	//
	// READ FROM THE ABILITY SYSTEM RATHER THAN ASKED OF THE DUNGEON, so that this
	// refresh keeps them whatever called it. `UCataclysmDungeonModifierEffects::
	// ApplyToCharacter` puts them there when a floor begins; everywhere outside a
	// dungeon the map is empty and nothing is added.
	if (const UCataclysmAbilitySystemComponent* Cataclysm =
			Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		for (const TPair<FName, TArray<FCataclysmStatModifier>>& Stat :
			 Cataclysm->GetDungeonStatModifiers())
		{
			Modifiers.FindOrAdd(Stat.Key).Append(Stat.Value);
		}
	}

	return UCataclysmPlayerClassStats::ApplyTo(
		AbilitySystem,
		UCataclysmPlayerClassStats::LoadTable(),
		UCataclysmPlayerClassStats::ChosenClass(),
		Level,
		&Modifiers,
		// WHAT THE CALLER ASKED FOR, AND THE DEFAULT IS TO LEAVE THEM ALONE.
		// Filling the pools is right for a character arriving in the world and
		// wrong for every other caller: a player who swapped a helmet during a
		// fight would be healed to full by doing it.
		PoolFill,
		&Bases);
}
